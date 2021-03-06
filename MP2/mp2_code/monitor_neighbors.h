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
#include <sys/time.h>

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
extern FILE *output_file;
const int num_routers = 256;
extern struct RouterEdge network[256][256];
extern int init_cost_nodes[256];
extern int num_init_cost_nodes;
extern int parent[256];
extern bool init_costs_updated;
extern bool og_pop;

// list of added functions
void convertHton(InitCostLsa* lsa);
void convertNtoh(InitCostLsa* lsa);
void* monitorNeighbors(void* unusedParam);
void* broadcastInitCosts(void* unusedParam);
void hackyBroadcast(const char* buf, int length);
int minDistRouter(int dist[], bool visited[]);
void runDijkstra();
void* announceToNeighbors(void* unusedParam);
void listenForNeighbors();

void convertHton(InitCostLsa* lsa) {
	lsa->source = htonl(lsa->source);
	lsa->dest = htonl(lsa->dest);
	lsa->init_cost = htonl(lsa->init_cost);
	lsa->seq_num = htonl(lsa->seq_num);
}

void convertNtoh(InitCostLsa* lsa) {
	lsa->source = ntohl(lsa->source);
	lsa->dest = ntohl(lsa->dest);
	lsa->init_cost = ntohl(lsa->init_cost);
	lsa->seq_num = ntohl(lsa->seq_num);
}

void* monitorNeighbors(void* unusedParam) {
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 500 * 1000 * 1000; //500 ms
	time_t timeout = 1; // 1 sec
	while(1) {
		for (int i = 0; i < num_routers; i++) {
			if (i == globalMyID) {
				continue;
			}
			// check if connection is dropped
			struct timeval currTime;
			gettimeofday(&currTime, 0);
			if (network[globalMyID][i].connected &&
				(currTime.tv_sec - globalLastHeartbeat[i].tv_sec > timeout)) {
				// connection is dropped
				network[globalMyID][i].connected = false;
				network[i][globalMyID].connected = false;
				// broadcast to neighbors
				for (int neighbor = 0; neighbor < num_routers; neighbor++) {
					if (neighbor == globalMyID || !network[neighbor][globalMyID].connected) { continue; }
					// create LSA to be sent
					int vertex = i;
					InitCostLsa lsa;
					lsa.source = globalMyID;
					lsa.dest = vertex;
					lsa.init_cost = -1;
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
					(struct sockaddr*)&globalNodeAddrs[neighbor], sizeof(globalNodeAddrs[neighbor]));
				}
			}
		}		
		nanosleep(&sleepFor, 0);
	}
}

// TODO: may have memory leaks from sending ocal_lsa[lsa_index]
void* broadcastInitCosts(void* unusedParam) {
	// broadcst only to neighbors
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 500 * 1000 * 1000; //500 ms
	while(1) {
		if (!init_costs_updated) { continue; }
		for (int dest_node = 0; dest_node < num_routers; dest_node++) {
			if (dest_node == globalMyID) { continue; }
			if (network[globalMyID][dest_node].connected) {
				// broadcast init_cost only if the init_node is a neighbor
				for (int j = 0; init_cost_nodes[j] != -1 && j < num_routers; j++) {
					if (!network[globalMyID][init_cost_nodes[j]].connected) { continue; }
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
		init_costs_updated = false;
		nanosleep(&sleepFor, 0);
	}
}

int minDistRouter(int dist[], bool visited[]) {
  int min = INT_MAX;
  int min_index = 0;
  for (int i = 0; i < num_routers; i++) {
    if (!og_pop && !visited[i] && dist[i] <= min) {
      min = dist[i];
      min_index = i;
    } else if (og_pop && !visited[i] && dist[i] < min) {
      min = dist[i];
      min_index = i;
    }
  }
  return min_index;
}

void runDijkstra() {
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
	if (!og_pop && dist[u] == INT_MAX) { continue; }
    visited[u] = true;
    for (int v = 0; v < num_routers; v++) {
      if (network[u][v].connected && (dist[u] + network[u][v].init_cost) < dist[v]) {
        dist[v] = dist[u] + network[u][v].init_cost;
        parent[v] = u;
      }
    }
  }
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
			if (network[globalMyID][heardFrom].connected == false) {
				init_cost_nodes[num_init_cost_nodes++] = heardFrom;
				init_costs_updated = true;
				network[globalMyID][heardFrom].connected = true;
				network[heardFrom][globalMyID].connected = true;
			}


			//record that we heard from heardFrom just now.
			gettimeofday(&globalLastHeartbeat[heardFrom], 0);
		}
		
		// format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
		if(!strncmp(recvBuf, "send", 4)) {
			// parse info from recvBuf
			unsigned short destID;
			memcpy(&destID, recvBuf+4, 2);
			destID = ntohs(destID);
			int msgLength = bytesRecvd - 4 - 2;
			char msg[100];
			memset(msg, '\0', 100);
			memcpy(msg, recvBuf+4+2, msgLength);
			// finding next router in shortest path
			int nextHOP = destID;
			runDijkstra();
			while(parent[nextHOP] != globalMyID && parent[nextHOP] != -1) {
				nextHOP = parent[nextHOP];
			}
			// appropriately forward / log messages
			char logLine[300];
			memset(logLine, '\0', 300);
			if (destID == globalMyID) { // reached dest. router
				sprintf(logLine, "receive packet message %s\n", msg); 
			} else if (parent[destID] == -1) { // unreachable router
				sprintf(logLine, "unreachable dest %d\n", destID); 
			} else { // forward following shortest path
				sendto(globalSocketUDP, recvBuf, bytesRecvd, 0, (struct sockaddr*)&globalNodeAddrs[nextHOP], sizeof(globalNodeAddrs[nextHOP]));
				if (strncmp(fromAddr, "10.0.0.10", 9) == 0) {
					// print send log
					sprintf(logLine, "sending packet dest %d nexthop %d message %s\n", destID, nextHOP, msg); 

				} else {
					// print forward log
					sprintf(logLine, "forward packet dest %d nexthop %d message %s\n", destID, nextHOP, msg); 
				}
			}
			if (output_file != NULL) {
				fwrite(logLine, 1, strlen(logLine), output_file);
				fflush(output_file);
			}
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
				if (lsa->init_cost != -1) {
					network[lsa->source][lsa->dest].init_cost = lsa->init_cost;
					network[lsa->dest][lsa->source].init_cost = lsa->init_cost;
				}
				if (lsa->init_cost == -1) {
					network[lsa->source][lsa->dest].connected = false;
					network[lsa->dest][lsa->source].connected = false;
				} else {
					network[lsa->source][lsa->dest].connected = true;
					network[lsa->dest][lsa->source].connected = true;
				}
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
