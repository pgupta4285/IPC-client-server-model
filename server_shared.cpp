#include<iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <queue>
#include <vector>
#include <map>
#include <fstream>
#include <string>
#include <sstream>


#define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10     // how many pending connections queue will hold
#define MAXDATASIZE 100
#define N 2   // No. of exchanges
#define M 3   // MAX  no. of stocks in a exchange.

#define CHUNKS_OF_RESULT 1
#define MAX_STOCK_INFO N*M

using namespace std;

struct stock{
	int exchange_id;
	int stock_id;
};

class CompareStock {
	public:
		bool operator()(stock& s1, stock& s2)
		{
			if (s1.stock_id > s2.stock_id) return true;
			if (s1.stock_id == s2.stock_id) return true;

			return false;
		}
};

int ShmID;

int *allot_mem()
{
	int * ShmPTR;
	pid_t  pid;
	int    status;

	ShmID = shmget(IPC_PRIVATE, (3*2*sizeof(int)), IPC_CREAT | 0666);
	if (ShmID < 0) 
	{
		printf("*** shmget error (server) ***\n");
		exit(1);
	}

	printf("Server has received a shared memory of four integers...\n");

	ShmPTR = (int *) shmat(ShmID, NULL, 0);
	if ((int) ShmPTR == -1) {
		printf("*** shmat error (server) ***\n");
		exit(1);
	}
	printf("Server has attached the shared memory...\n");
	return ShmPTR;
}


void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

bool isReadyToPop(map<int,int>m)
{
	map<int,int>::iterator it;
	for (it=m.begin(); it!=m.end(); ++it)
	{
		if(it->second == 0)
		{
			return false;	
		}
	}
	return true;
}

int main(void)
{
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
	int* sharedMem = allot_mem();
	int status;


	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
						p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
					sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	priority_queue<stock,vector<stock>,CompareStock> pq;
	map<int,int> requestCount;
	vector<stock>results;	
	int file_size =0;
	ofstream myfile;
	myfile.open ("MasterFile.txt");


	for(int i =1 ;i < N ;i++)
	{
		requestCount[i] = 0;
	}


	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,get_in_addr((struct sockaddr *)&their_addr),s, sizeof s);
		printf("server: got connection from %s\n", s);

		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener
			char buf[100];
			int numbytes;
			if ((numbytes = recv(new_fd, buf,99, 0)) == -1) 
			{		
				perror("recv");
				exit(1);
			}
			buf[numbytes] = '\0';
			printf("server: recieved %s \n", buf);
			string stock_info(buf);
			stringstream ss;
			ss <<stock_info;
			ss >>sharedMem[0]>>sharedMem[1];

			close(new_fd);

			exit(0);
		}
		wait(&status);

		struct stock stock_temp = {sharedMem[0],sharedMem[1]};

		requestCount[stock_temp.exchange_id]++;
		pq.push(stock_temp);

		if(isReadyToPop(requestCount))
		{
			stock deletedStock = pq.top(); 
			results.push_back(deletedStock);
			pq.pop();
			requestCount[deletedStock.exchange_id]--;			
		}


		if(results.size() >= CHUNKS_OF_RESULT)
		{
			myfile.open ("MasterFile.txt",ios::app);
			for(int i=0;i<results.size();i++)
			{
				myfile<<results[i].exchange_id << "    "<<results[i].stock_id;
				myfile<<"\n";
				file_size++;
			}
			results.clear();
			myfile.close();
		}
		if(file_size + pq.size() +results.size() >= MAX_STOCK_INFO)
		{
			myfile.open ("MasterFile.txt",ios::app);
			for(int i=0;i<results.size();i++)
			{
				myfile<<results[i].exchange_id << "    "<<results[i].stock_id;
				myfile<<"\n";
				file_size++;
			}
			while(!pq.empty())	
			{
				struct stock stk = pq.top();
				myfile<<stk.exchange_id << "    "<<stk.stock_id;
				myfile<<"\n";
				file_size++;
				pq.pop();
			}
			myfile.close();
			break;
		}

		close(new_fd);  // parent doesn't need this
	}

	shmdt((void *) sharedMem);
	printf("Server has detached its shared memory...\n");
	shmctl(ShmID, IPC_RMID, NULL);
	printf("Server has removed its shared memory...\n");
	printf("Server exits...\n");
	return 0;
}
