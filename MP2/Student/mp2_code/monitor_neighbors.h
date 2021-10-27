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

typedef struct LSA {
	int dest;
	int msg_length;
	char message[1024];
	struct LSA *next;
} LSA;

extern int globalMyID;
//last time you heard from each node. monitor this
//in order to realize when a neighbor has gotten cut off from you.
extern struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
extern int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
extern struct sockaddr_in globalNodeAddrs[256];

extern char *output_filename;
extern int init_cost[256];
extern int neighbor[256];
extern int network[256][256];
extern int num_routers = 256;
char *edge_string = NULL;
LSA *list_head = NULL;
LSA *list_tail = NULL;

// mutexes for threads
pthread_mutex_t edgeStringMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lsaMutex = PTHREAD_MUTEX_INITIALIZER;

// added functions
void constructEdgeCosts();
void* broadcastEdgeCosts(void* unusedParam);
void monitorNeighborConnections();
int dijkstra(int start);
int getMinDistNode(int distances[], bool visited[]);

int getMinDistNode(int distances[], bool visited[]) {
	int min = INT_MAX;
	int min_index;
	for (int i = 0; i < num_routers; i++) {
		if (visited[i] || dist[i] > min) { continue; }
		min = dist[i];
		min_index = i;
	}
	return min_index;
}

int dijkstra(int start) {
	// setup
	int distances[256];
	bool visited[256];
	for (int i = 0; i < num_routers; i++) {
		distances[i] = INT_MAX;
		visited[i] = false;
	}
	distances[start] = 0
	// finding shortest path
	for (int i = 0; i < 255; i++) {
		int curr_node = getMinDistNode(distances, visited);
		visited[curr_node] = true;
		for (int v = 0; v < 256; v++) {
			if (visited[v] || !network[curr_node][v] || distances[curr_node]==INT_MAX) { continue; }
			if (distances[curr_node] + network[curr_node][v] < distances[v]) {
				distances[v] = distances[curr_node] + network[curr_node][v];
			}
		}
	}
	int min = INT_MAX;
	int min_index = INT_MIN;
	for (int i = 0; i < num_routers; i++) {
		int cost = distances[i] + network[globalMyID][i];
		if (dist[i] == INT_MAX || i==start) { continue; }
		if (cost >= min && (cost!=min || i>=index)) { continue; }
		if ((i==globalMyID && network[globalMyID][start]==cost) || network[globalMyID][i]) {
			min = cost;
			min_index = 1;
		}
	}
	return min_index==globalMyID ? src : min_index;
}

void monitorNeighborConnections() {
	// check if connection dropped
	struct timeval curr;
	int i;
	for (i = 0; i < num_routers; i++) {
		if (network[i][globalMyID] == 0) {
			continue;
		}
		gettimeofday(&curr, 0);
		double diff = (curr.tv_sec-globalLastHeartbeat[i].tv_sec)*1000 + (curr.tv_usec-globalLastHeartbeat[i].tv_usec)/1000;
		if (diff > 1500.0) { // connection dropped
			// update network, edge costs, forwarding table
			network[globalMyID][i] = 0;
			network[i][globalMyID] = 0;
			constructEdgeCosts();
			for (int i = 0; i < num_routers; i++) {
				forwarding_table[i] = INT_MIN;
			}
		}
	}
}

void* broadcastEdgeCosts(void* unusedParam) {
	struct timespec sleep;
	sleep.tv_sec = 0;
	sleep.tv_nsec = 5 * 1000 * 1000; // 5 ms

	while(1) {
		// create LSA to be sent
		LSA* lsa;
		pthread_mutex_lock(&lsaMutex);
		if (list_head == NULL) {
			lsa = NULL;
		} else {
			lsa = list_head;
			list_head = list_head->next;
		}
		pthread_mutex_unlock(&lsaMutex);
		// sending LSA
		if (lsa != NULL) {
			for (int i = 0; i < num_rotuers; i++) {
				if (i == globalMyID) { continue; }
				if (lsa->dest!=-1 && i!=lsa->dest) { continue; }
				sendto(globalSocketUDP, lsa->message, lsa->msg_length, 0, (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
			}
		} else {
			monitorNeighborConnections();
		}
		free(lsa);
		nanosleep(&sleep, 0);
	}
}

void constructEdgeCosts() {
	char buffer[1024];
	memset(buf, 0, 1024);
	++neighbor[globalMyID];
	sprintf(buffer, "edge %d %d ", globalMyID, neighbor[globalMyID]);
	for (int i = 0; i < num_routers; i++) {
		int edge_cost = 0;
		if ((edge_cost = network[i][globalMyID]) != 0) { 
			char curr_edge[30];
			memset(curr_edge, 0, 30);
			sprintf(curr_edge, "%d-%d,", i, edge_cost);
			strcat(buffer, curr_edge);
			if (i+1 = num_routers) {
				buffer[strlen(buffer) - 1] = '\0';
			}
		}
	}
	strcat(buffer, ";");
	pthread_mutex_lock(&edgeStringMutex);
	free(edge_string);
	edge_string = calloc(strlen(buffer) + 1, 1);
	memcpy(edge_string, buffer, strlen(buffer));
	pthread_mutex_unlock(&edgeStringMutex);
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
		pthread_mutex_lock(&edgeStringMutex);
		char *temp = edge_string==NULL ? "HEREIAM" : edge_string;
		char *broadcast_msg = calloc(strlen(temp) + 1, 1);
		memcpy(broadcast_msg, temp, strlen(temp));
		pthread_mutex_lock(&edgeStringMutex);

		hackyBroadcast(broadcast_msg, strlen(broadcast_msg));
		free(broadcast_msg);
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
			
			// this node can consider heardFrom to be directly connected to it
			recvBuf[bytesRecvd] = '\0';
			if (network[heardFrom][globalMyID] <= 0) { // add edge to network
				for (int i = 0; i < num_routers; i++) {
					forwarding_table[i] = INT_MIN;
				}
				network[heardFrom][globalMyID] = init_cost[heardFrom];
				network[globalMyID][heardFrom] = init_cost[heardFrom];
				constructEdgeCosts();
			}
			
			//record that we heard from heardFrom just now.
			gettimeofday(&globalLastHeartbeat[heardFrom], 0);
		}

		//send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
		if(!strncmp(recvBuf, "send", 4)) {
			// parse info from recvBuf
			uint16_t destid, destid_network;
			memcpy(&destid_network, recvBuf+4, 2);
			destid = ntohs(destid_network);
			char *message = calloc(strlen(recvBuf+6)+1, 1);
			memcpy(message, recvBuf+6, strlen(recvBuf+6));
			// send to destid and print correct log
			if (destid == globalMyID) {
				// log to output file
				FILE *out_file = fopen(output_filename, "a+");
				char *logLine;
				sprintf(logLine, "receive packet message %s\n", message); 
				fwrite(logLine, 1, strlen(logLine), out_file); 
				fflush(out_file);
				fclose(out_file);
			} else {
				if (forwarding_table[destid] == INT_MIN) {
					forwarding_table[destid] = dijkstra(destid);
				}
				int next_hop = forwarding_table[destid];
				if (destid != INT_MIN) { // send to nexthop
					// create lsa to send
					LSA *lsa = calloc(1, sizeof(LSA));
					memcpy(lsa->message, message, strlen(message));
					lsa->msg_length = bytesRecvd;
					lsa->dest = next_hop;
					lsa->next = NULL;
					pthread_mutex_lock(&lsaMutex);
					// add to list
					if (list_head != NULL) {
						list_tail->next = lsa;
					} else {
						list_head = lsa;
					}
					list_tail = lsa;
					pthread_mutex_unlock(&lsaMutex);
					// log appropriate message
					if (heardFrom == -1) { // log send message
						FILE *out_file = fopen(output_filename, "a+");
						char *logLine;
						sprintf(logLine, "sending packet dest %d nexthop %d message %s\n", destid, next_hop, message);  
						fwrite(logLine, 1, strlen(logLine), out_file); 
						fflush(out_file);
						fclose(out_file);
					} else { // log forward message
						FILE *out_file = fopen(output_filename, "a+");
						char *logLine;
						sprintf(logLine, "forward packet dest %d nexthop %d message %s\n", destid, next_hop, message); 
						fwrite(logLine, 1, strlen(logLine), out_file); 
						fflush(out_file);
						fclose(out_file);
					}
				} else { // unreachable dest
					// log to output file
					FILE *out_file = fopen(output_filename, "a+");
					char *logLine;
					sprintf(logLine, "unreachable dest %d\n", destid);  
					fwrite(logLine, 1, strlen(logLine), out_file); 
					fflush(out_file);
					fclose(out_file);
				}
			}
			free(message);
		} 
		// edge cost msg format: 'edge src dest msg
		else if (!strncmp(recvBuf, "edge", 4)) {
			// parse info from recvBuf
			char *copy_recv = calloc(bytesRecvd, 1);
			memcpy(copy_recv, recvBuf, bytesRecvd);
			int srcid, destid;
			char message[1024];
			memset(message, 0, 1024);
			sscanf(copy_recv, "edge %d %d %s;", &srcid, &destid, message);
			if (destid > neighbors[srcid] && strlen(message) > 1) {
				int og_edge = network[srcid][globalMyID];
				bool edge_changed = false;
				neighbor[srcid] = destid;
				// creating edges from message
				char **edges = NULL; 
				int n = 0;
				char *msg_copy = calloc(strlen(message) + 1, 1);
				memcpy(msg_copy, message, strlen(message));
				if (strlen(msg_copy) != 0) {
					for (int i = 0; i < (int)strlen(msg_copy); i++) {
						if (msg_copy[i] == ',' || msg_copy[i] == '\n') {
							msg_copy[i] = '\0';
							n++;
						}
					}
					n++;
					edges = (char**) calloc(n+1, sizeof(char*));
					char *msg_ptr = msg_copy;
					for (int i = 0; i < n; i++) {
						edges[i] = calloc(strlen(msg_ptr) + 1);
						memcpy(edges[i], msg_ptr, strlen(msg_ptr));
						msg_ptr += strlen(msg_ptr) + 1;
					}
					edges[n] = NULL;
				}
				free(msg_copy);
				int list_len = 0;
				for (list_len = 0; edges != NULL && edges[list_len] != NULL; ) {
					list_len++;
				}
				// update based on recvd message
				for (int i = 0; i < num_routers; i++) {
					network[srcid][i] = 0;
					network[i][srcid] = 0;
					if (i == num_routers-1) {
						for (i = 0; i < list_len; i++) {
							int id, cost;
							sscanf(edges[i], "%d-%d", &id, &cost);
							network[id] [srcid] = cost;
							network[srcid][id] = cost;
							if (id==globalMyID && cost!=og_edge) {
								edge_changed = true;
							}
						}
						i = num_routers;
					}
				}
				if (edge_changed) {
					constructEdgeCosts();
				}
				for (int i = 0; i < num_routers; i++) { // clearing forward_table
					forwarding_table[i] = INT_MIN;
				}
				// destroy created edges
				for (int i = 0; i < n, i++) {
					free(edges[i]);
					edges[i] = NULL;
				}
				free(edges); edges = NULL;
				// create and add lsa to list
				LSA *lsa = calloc(1, sizeof(LSA));
				memcpy(lsa->message, recvBuf, strlen(recvBuf));
				lsa->msg_length = bytesRecvd;
				lsa->dest = -1;
				lsa->next = NULL;
				pthread_mutex_lock(&lsaMutex);
				// add to list
				if (list_head != NULL) {
					list_tail->next = lsa;
				} else {
					list_head = lsa;
				}
				list_tail = lsa;
				pthread_mutex_unlock(&lsaMutex);
			}
			free(copy_recv);
		}
		//'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
		else if(!strncmp(recvBuf, "cost", 4)) {
			// TODO record the cost change (remember, the link might currently be down! in that case,
			//this is the new cost you should treat it as having once it comes back up.)
		}
	}
	//(should never reach here)
	close(globalSocketUDP);
}

