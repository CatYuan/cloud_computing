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
#include <limits.h>

typedef struct RouterEdge {
  bool connected;
  int init_cost;
  int seq_num;
} RouterEdge;

typedef struct InitCostLsa {
	int source;
	int dest;
	int seq_num;
	int init_cost;
} InitCostLsa;

extern int globalMyID;
//last time you heard from each node. you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
extern struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
extern int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
extern struct sockaddr_in globalNodeAddrs[256];

extern char *output_filename;
extern const int num_routers = 256;
extern struct RouterEdge network[num_routers][num_routers];
extern int init_cost_nodes[num_routers];

// mutexes for threads
extern pthread_mutex_t lastHeartbeat_mutex = PTHREAD_MUTEX_INITIALIZER;
extern pthread_mutex_t network_mutex = PTHREAD_MUTEX_INITIALIZER;
extern pthread_mutex_t init_costs_mutex = PTHREAD_MUTEX_INITIALIZER;

// list of added functions
void convertHton(InitCostLsa* lsa);
void convertNtoh(InitCostLsa* lsa);
void* monitorNeighbors(void* unusedParam);
void broadcastInitCosts(void* unusedParam);
bool isNeighbor(int router_id);
void hackyBroadcast(const char* buf, int length);
int minDistRouter(int dist[], bool visited[]);
int* Disjkstra();
void* announceToNeighbors(void* unusedParam);
void listenForNeighbors();

void convertHton(InitCostLsa* lsa) {
	lsa->source = htonl(lsa->source);
	lsa->dest = htonl(lsa->dest);
	lsa->init_cost = htonl(lsa->init_cost);
	lsa->seq_num = htonl(lsa->seq_num);
}

void convertHton(InitCostLsa* lsa) {
	lsa->source = ntohl(lsa->source);
	lsa->dest = ntohl(lsa->dest);
	lsa->init_cost = ntohl(lsa->init_cost);
	lsa->seq_num = ntohl(lsa->seq_num);
}

void* monitorNeighbors(void* unusedParam) {
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 1000 * 1000 * 1000; //1000 ms
	time_t timeout = 1; // 1 sec
	while(1) {
		pthread_mutex_lock(&lastHeartbeat_mutex);
		for (int i = 0; i < num_routers; i++) {
			if (i == globalMyID) {
				continue;
			}
			// check if connection is dropped
			struct timeval currTime;
			gettimeofday(&currTime, 0);
			if ((globalLastHeartbeat[i].tv_sec != 0) &&
				(currTime.tv_sec - globalLastHeartbeat[i].tv_sec > timeout)) {
				// TODO: logic for when connection is dropped
				// set connected to false in network to indicate no connection
			}
		}
		pthread_mutex_unlock(&lastHeartbeat_mutex);			
		nanosleep(&sleepFor, 0);
	}
}

// TODO: may have memory leaks from sending ocal_lsa[lsa_index]
void broadcastInitCosts(void* unusedParam) {
	pthread_mutex_lock(&network_mutex);
	pthread_mutex_lock(&init_costs_mutex);
	// broadcst only to neighbors
	for (int dest_node = 0; dest_node < num_routers; dest_node++) {
		if (dest_node == globalMyID) { continue; }
		if (isNeighbor(dest_node)) {
			// broadcast init_cost only if the init_node is a neighbor
			for (int j = 0; init_cost_nodes[j] != -1 && j < num_routers; j++) {
				if (!isNeighbor(init_cost_nodes[j])) { continue; }
				// create LSA to be sent
				int vertex = init_cost_nodes[j];
				InitCostLsa lsa;
				lsa.source = globalMyID;
				lsa.dest = vertex;
				lsa.init_cost = network[globalMyID][vertex].init_cost;
				lsa.seq_num = ++network[globalMyID][vertex].seq_num;
				network[vertex][globalMyID].seq_num++;
				convertHton(&lsa);
				// add message type and copy lsa into buffer to send
				char *msg_type = "hello";
				int buf_length = sizeof(InitCostLsa) + strlen(msg_type);
				void *buf = (void*) malloc(buf_length);
				memcpy(buf, msg_type, strlen(msg_type));
				memcpy((char*)buf+strlen(msg_type), &lsa, sizeof(InitCostLsa));
				// send lsa to neighbor
				sendto(globalSocketUDP, buf, buf_length, 0,
				  (struct sockaddr*)&globalNodeAddrs[dest_node], sizeof(globalNodeAddrs[dest_node]));
			}
		}
	}
	pthread_mutex_unlock(&init_costs_mutex);
	pthread_mutex_unlock(&network_mutex);
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

int minDistRouter(int dist[], bool visited[]) {
  int min = INT_MAX;
  int min_index = 0;
  for (int i = 0; i < num_routers; i++) {
    if (!visited[i] && dist[i] < min) {
      min = dist[i];
      min_index = i;
    }
  }
  return min_index;
}

int* Disjkstra() {
  int parent[num_routers];
  int dist[num_routers];
  bool visited[num_routers];

  // initialize arrays
  for (int i = 0; i < num_routers; i++) {
    dist[i] = INT_MAX;
    parent[i] = -1;
    visited[i] = false;
  }
  dist[globalMyID] = 0;

  // finding shortest path
  for (int i = 0; i < num_routers; i++) {
    int u = minDistRouter(dist, visited);
    visited[u] = true;
    for (int v = 0; v < num_routers; v++) {
      if (network[u][v].connected && !visited[v] && (dist[u] + network[u][v].init_cost) < dist[v]) {
        dist[v] = dist[u] + network[u][v].init_cost;
        parent[v] = u;
      }
    }
  }
  return parent;
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

			// this node can consider heardFrom to be directly connected to it; do any such logic now - mark heardFrom as a neighbor
			network[globalMyID][heardFrom].connected = true;
			network[heardFrom][globalMyID].connected = true;
			
			//record that we heard from heardFrom just now.
			pthread_mutex_lock(&lastHeartbeat_mutex);
			gettimeofday(&globalLastHeartbeat[heardFrom], 0);
			pthread_mutex_unlock(&lastHeartbeat_mutex);
		}
		
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
		// format: 'hello'<rest of InitCostLsa>
		else if (!strncmp(recvBuf, "hello", 5)) {
			InitCostLsa* lsa = ((void*)recvBuf + 5);
			convertNtoh(lsa);
			if ( lsa->seq_num > network[lsa->source][lsa->dest].seq_num ) {
				// update network
				network[lsa->source][lsa->dest].seq_num = lsa->seq_num;
				network[lsa->dest][lsa->source].seq_num = lsa->seq_num;
				network[lsa->source][lsa->dest].init_cost = lsa->init_cost;
				network[lsa->dest][lsa->source].init_cost = lsa->init_cost;
				network[lsa->source][lsa->dest].connected = true;
				network[lsa->dest][lsa->source].connected = true;
				// send lsa to all neighbors
				convertHton(lsa);
				for (int dest_node = 0; dest_node < num_routers; dest_node++) {
					if (dest_node == globalMyID || dest_node == heardFrom) { continue; } // avoid sending in a loop
					if (network[globalMyID][dest_node].connected) {
						sendto(globalSocketUDP, recvBuf, bytesRecvd, 0, (struct sockaddr*)&globalNodeAddrs[dest_node], sizeof(globalNodeAddrs[dest_node]));
					}
				}
			}
		}
	}
	//(should never reach here)
	close(globalSocketUDP);
}
