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

// struct for Local Link State Announcement entry - stored by each router
typedef struct Local_lsa_entry{
	uint32_t cost;
	uint32_t initial_cost;
	uint32_t seq_num;
} Local_lsa_entry;

// struct for Link State Announcement - broadcast to each router
typedef struct LSA{
	char msg_type[6];
	uint32_t from;
	uint32_t cost;
	uint32_t seq_num;
	uint32_t edge_costs[256][256];
} LSA;

extern int globalMyID;
//last time you heard from each node. you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
extern struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
extern int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
extern struct sockaddr_in globalNodeAddrs[256];

// forwarding table stored by each router - forward_table[dest_id] = next_hop
extern int forward_table[256];

extern struct LSA local_lsa[256][256];

extern char *output_filename;

extern int init_cost_nodes[256];

// mutexes for threads
extern pthread_mutex_t lastHeartbeat_mutex = PTHREAD_MUTEX_INITIALIZER;
extern pthread_mutex_t local_lsa_mutex = PTHREAD_MUTEX_INITIALIZER;
extern pthread_mutex_t init_costs_mutex = PTHREAD_MUTEX_INITIALIZER;

// list of added functions
LSA* copyLsa(LSA* og_lsa);
void destroyLsa(LSA* lsa);
void convertHton(LSA* lsa);
void convertNtoh(LSA* lsa);
void* monitorNeighbors(void* unusedParam);
void broadcastInitCosts(void* unusedParam);
bool isNeighbor(int router_id);
void* announceToNeighbors(void* unusedParam);
void listenForNeighbors();

LSA* copyLsa(LSA* og_lsa) {
	LSA* lsa = (LSA*) malloc(sizeof(LSA));
	memcpy(lsa->msg_type,"hello\0", 6);
	lsa->id = og_lsa->id;
	lsa->cost = og_lsa->cost;
	lsa->initial_cost = og_lsa->initial_cost;
	lsa->seq_num = og_lsa->seq_num;
	lsa->next_hop = og_lsa->next_hop;
	return lsa;
}

void convertHton(LSA* lsa) {
	lsa->id = htonl(lsa->id);
	lsa->cost = htonl(lsa->cost);
	lsa->initial_cost = htonl(lsa->initial_cost);
	lsa->seq_num = htonl(lsa->seq_num);
	lsa->next_hop = htonl(lsa->next_hop);
}

void convertHton(LSA* lsa) {
	lsa->id = ntohl(lsa->id);
	lsa->cost = ntohl(lsa->cost);
	lsa->initial_cost = ntohl(lsa->initial_cost);
	lsa->seq_num = ntohl(lsa->seq_num);
	lsa->next_hop = ntohl(lsa->next_hop);
}

void* monitorNeighbors(void* unusedParam) {
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 1000 * 1000 * 1000; //1000 ms
	time_t timeout = 1; // 1 sec
	while(1) {
		pthread_mutex_lock(&lastHeartbeat_mutex);
		for (int i = 0; i < 256; i++) {
			if (i == globalMyID) {
				continue;
			}
			// check if connection is dropped
			struct timeval currTime;
			gettimeofday(&currTime, 0);
			if ((globalLastHeartbeat[i].tv_sec != 0) &&
				(currTime.tv_sec - globalLastHeartbeat[i].tv_sec > timeout)) {
				// TODO: logic for when connection is dropped
				// set cost to -1 to indicate no connection
			}
		}
		pthread_mutex_unlock(&lastHeartbeat_mutex);			
		nanosleep(&sleepFor, 0);
	}
}

// TODO: may have memory leaks from sending ocal_lsa[lsa_index]
void broadcastInitCosts(void* unusedParam) {
	pthread_mutex_lock(&local_lsa_mutex);
	pthread_mutex_lock(&init_costs_mutex);
	for (int dest_node = 0; dest_node < 256; dest_node++) {
		if (dest_node == globalMyID) { continue; }
		if (isNeighbor(dest_node)) {
			for (int j = 0; init_cost_nodes[j] != -1 || j < 256; j++) {
				if (!isNeighbor(init_cost_nodes[j])) { continue; }
				// create LSA to be sent
				int lsa_index = init_cost_nodes[j];
				LSA *lsa = copyLsa(&local_lsa[lsa_index]);
				lsa->seq_num = ++local_lsa[lsa_index].seq_num;
				convertHton(lsa);
				// send lsa to neighbor
				sendto(globalSocketUDP, lsa, sizeof(LSA), 0,
				  (struct sockaddr*)&globalNodeAddrs[dest_node], sizeof(globalNodeAddrs[dest_node]));
				free(lsa);
			}
		}
	}
	pthread_mutex_unlock(&init_costs_mutex);
	pthread_mutex_unlock(&local_lsa_mutex);
}

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
			
			//record that we heard from heardFrom just now.
			pthread_mutex_lock(&lastHeartbeat_mutex);
			gettimeofday(&globalLastHeartbeat[heardFrom], 0);
			pthread_mutex_unlock(&lastHeartbeat_mutex);
		}
		
		//Is it a packet from the manager? (see mp2 specification for more details)
		// format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
		if(!strncmp(recvBuf, "send", 4)) {
			//TODO send the requested message to the requested destination node
			// ...
		}
		// format: 'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
		else if(!strncmp(recvBuf, "cost", 4)) {
			//TODO record the cost change (remember, the link might currently be down! in that case,
			//this is the new cost you should treat it as having once it comes back up.)
			// ...
		}
		// format: 'hello'<rest of LSA>
		else if (!strncmp(recvBuf, "hello", 5)) {
			LSA* l = ((void*)recvBuf);
			convertNtoh(l);
			if (local_lsa[l->id].initial_cost == 1 || )
		}
		//TODO now check for the various types of packets you use in your own protocol
		//else if(!strncmp(recvBuf, "your other message types", ))
	}
	//(should never reach here)
	close(globalSocketUDP);
}

