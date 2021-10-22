#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>

#include "monitor_neighbors.h"

/**
 * NOTE: Remember to convert to htonl when sending and ntohl when reading
 * TODO: 
 * 		look into placement of network mutex - consider not using isNeighbor (?)
 * 		implement cost message
 */

int globalMyID = 0;
//last time you heard from each node. you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
struct sockaddr_in globalNodeAddrs[256];

char *output_filename;
FILE *output_file = NULL;
const int num_routers = 256;
struct RouterEdge network[num_routers][num_routers];
int init_cost_nodes[num_routers];

// mutexes for threads
pthread_mutex_t lastHeartbeat_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t network_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t init_costs_mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char** argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "Usage: %s mynodeid initialcostsfile logfile\n\n", argv[0]);
		exit(1);
	}
	
	
	//initialization: get this process's node ID, record what time it is, 
	//and set up our sockaddr_in's for sending to the other nodes.
	globalMyID = atoi(argv[1]);
	pthread_mutex_lock(&lastHeartbeat_mutex);
	for(int i=0;i<256;i++) {
		// initialize globalLastHeartbeat to 0
		// gettimeofday(&globalLastHeartbeat[i], 0);
		struct timeval zero_tv;
		zero_tv.tv_sec = 0; zero_tv.tv_usec = 0;
		globalLastHeartbeat[i] = zero_tv;

		char tempaddr[100];
		sprintf(tempaddr, "10.1.1.%d", i);
		memset(&globalNodeAddrs[i], 0, sizeof(globalNodeAddrs[i]));
		globalNodeAddrs[i].sin_family = AF_INET;
		globalNodeAddrs[i].sin_port = htons(7777);
		inet_pton(AF_INET, tempaddr, &globalNodeAddrs[i].sin_addr);
	}
	pthread_mutex_unlock(&lastHeartbeat_mutex);
	
	// read and parse initial costs file. default to cost 1 if no entry for a node. file may be empty.
	output_filename = argv[3];
	output_file = fopen(output_filename, "w");
	// initialize network
	pthread_mutex_lock(&network_mutex);
	pthread_mutex_lock(&init_costs_mutex);
	for (int i = 0; i < 256; i++) {
		for (int j = 0; j < 256; j++) {
			network[i][j].connected = false;
			network[i][j].init_cost = 1;
			network[i][j].seq_num = 0;
			init_cost_nodes[i] = -1;
		}
	}
	// assign costs based on inputted cost_file
	char *lineptr = NULL; ssize_t n = 0;
	FILE *costs_file = fopen(argv[2], "r");
	int index = 0;
	while (getline(&lineptr, &n, costs_file) != -1) {
		int node, cost;
		sscanf(lineptr, "%d %d", &node, &cost);
		network[globalMyID][node].init_cost = cost;
		network[node][globalMyID].init_cost = cost;
		init_cost_nodes[index] = node;
		index++;
		free(lineptr); n = 0;
	}
	free(lineptr); n = 0;
	fclose(costs_file);
	pthread_mutex_unlock(&init_costs_mutex);
	pthread_mutex_unlock(&network_mutex);
	
	//socket() and bind() our socket. We will do all sendto()ing and recvfrom()ing on this one.
	if((globalSocketUDP=socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}
	char myAddr[100];
	struct sockaddr_in bindAddr;
	sprintf(myAddr, "10.1.1.%d", globalMyID);	
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = htons(7777);
	inet_pton(AF_INET, myAddr, &bindAddr.sin_addr);
	if(bind(globalSocketUDP, (struct sockaddr*)&bindAddr, sizeof(struct sockaddr_in)) < 0)
	{
		perror("bind");
		close(globalSocketUDP);
		exit(1);
	}
	
	
	//start threads... feel free to add your own, and to remove the provided ones.
	pthread_t announcerThread;
	pthread_create(&announcerThread, 0, announceToNeighbors, (void*)0);

	// monitor neighbors to see if connection is dropped
	pthread_t monitorThread;
	pthread_create(&monitorThread, 0, monitorNeighbors, (void*)0);
	
	// broadcast local_lsa to neighbors. broadcasts intial costs
	pthread_t broadcastThread;
	pthread_create(&broadcastThread, 0, broadcastInitCosts, (void*)0);
	
	//good luck, have fun!
	listenForNeighbors();	
}
