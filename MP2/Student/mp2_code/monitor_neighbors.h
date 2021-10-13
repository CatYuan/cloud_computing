#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>

// struct for Link State Announcement
typedef struct LSA{
	int cost;
	unsigned int initial_cost;
	unsigned int seq_num;
	int next_hop;
} LSA;

extern int globalMyID;
//last time you heard from each node. you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
extern struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
extern int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
extern struct sockaddr_in globalNodeAddrs[256];

// forwarding table stored by each router
extern struct LSA local_lsa[256];

extern char *output_filename;

// id of router that the connection was dropped from. -1 if no connections were dropped
int connection_dropped = -1;

// mutexes for globalLastHeartbeat and connection_dropped(?)
pthread_mutex_t lastHeartbeat_mutex = PTHREAD_MUTEX_INITIALIZER;

// list of added functions
void* monitorNeighbors(void* unusedParam);
bool isNeighbor(int router_id);

bool isNeighbor(int router_id) {
	time_t timeout = 1;
	struct timeval currTime;
	gettimeofday(&currTime, 0);
	pthread_mutex_lock(&lastHeartbeat_mutex);
	bool output = (globalLastHeartbeat[router_id].tv_sec != 0) && 
		(currTime.tv_sec - globalLastHeartbeat[router_id].tv_sec < timeout);
	pthread_mutex_unlock(&lastHeartbeat_mutex);
	return output;
}

//Yes, this is terrible. It's also terrible that, in Linux, a socket
//can't receive broadcast packets unless it's bound to INADDR_ANY,
//which we can't do in this assignment.
void hackyBroadcast(const char* buf, int length) {
	int i;
	for(i=0;i<256;i++)
		if(i != globalMyID) //(although with a real broadcast you would also get the packet yourself)
			sendto(globalSocketUDP, buf, length, 0,
				  (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

void* announceToNeighbors(void* unusedParam) {
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 300 * 1000 * 1000; //300 ms
	while(1) {
		hackyBroadcast("HEREIAM", 7);
		nanosleep(&sleepFor, 0);
	}
}

void* monitorNeighbors(void* unusedParam) {
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 1000 * 1000 * 1000; //1000 ms
	while(1) {
		time_t timeout = 1; // 1 sec
		pthread_mutex_lock(&lastHeartbeat_mutex);
		for (int i = 0; i < 256; i++) {
			if (i == globalMyID) {
				continue;
			}
			struct timeval currTime;
			gettimeofday(&currTime, 0);
			if ((globalLastHeartbeat[i].tv_sec != 0) &&
				(currTime.tv_sec - globalLastHeartbeat[i].tv_sec > timeout)) {
				// TODO: logic for when connection is dropped
			}
		}
		pthread_mutex_unlock(&lastHeartbeat_mutex);			
		nanosleep(&sleepFor, 0);
	}
}

void listenForNeighbors() {
	char fromAddr[100];
	struct sockaddr_in theirAddr;
	socklen_t theirAddrLen;
	unsigned char recvBuf[1000];

	int bytesRecvd;
	while(1) {
		theirAddrLen = sizeof(theirAddr);
		if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1000 , 0, 
					(struct sockaddr*)&theirAddr, &theirAddrLen)) == -1) {
			perror("connectivity listener: recvfrom failed");
			exit(1);
		}
		
		inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);
		
		short int heardFrom = -1;
		if(strstr(fromAddr, "10.1.1.")) {
			heardFrom = atoi(
					strchr(strchr(strchr(fromAddr,'.')+1,'.')+1,'.')+1);
			
			//TODO: this node can consider heardFrom to be directly connected to it; do any such logic now.
			
			//record that we heard from heardFrom just now.
			pthread_mutex_lock(&lastHeartbeat_mutex);
			gettimeofday(&globalLastHeartbeat[heardFrom], 0);
			pthread_mutex_unlock(&lastHeartbeat_mutex);
		}
		
		//Is it a packet from the manager? (see mp2 specification for more details)
		//send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
		if(!strncmp(recvBuf, "send", 4)) {
			//TODO send the requested message to the requested destination node
			// ...
		}
		//'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
		else if(!strncmp(recvBuf, "cost", 4)) {
			//TODO record the cost change (remember, the link might currently be down! in that case,
			//this is the new cost you should treat it as having once it comes back up.)
			// ...
		}
		
		//TODO now check for the various types of packets you use in your own protocol
		//else if(!strncmp(recvBuf, "your other message types", ))
		// ... 
	}
	//(should never reach here)
	close(globalSocketUDP);
}

