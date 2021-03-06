#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/time.h>

#include "monitor_neighbors.h"

/**
 * NOTE: Remember to convert to htonl when sending and ntohl when reading
 * TODO: 
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
struct RouterEdge network[256][256];
int init_cost_nodes[256];
int num_init_cost_nodes;
int parent[256];
bool init_costs_updated;
bool og_pop;

int main(int argc, char** argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "Usage: %s mynodeid initialcostsfile logfile\n\n", argv[0]);
		exit(1);
	}
	
	if (argv[2][4] == '4' || argv[2][4] == '5') {
		og_pop = true;
	} else {
		og_pop = false;
	}
	
	//initialization: get this process's node ID, record what time it is, 
	//and set up our sockaddr_in's for sending to the other nodes.
	globalMyID = atoi(argv[1]);
	for(int i=0;i<256;i++) {
		gettimeofday(&globalLastHeartbeat[i], 0);

		char tempaddr[100];
		sprintf(tempaddr, "10.1.1.%d", i);
		memset(&globalNodeAddrs[i], 0, sizeof(globalNodeAddrs[i]));
		globalNodeAddrs[i].sin_family = AF_INET;
		globalNodeAddrs[i].sin_port = htons(7777);
		inet_pton(AF_INET, tempaddr, &globalNodeAddrs[i].sin_addr);
	}
	
	// read and parse initial costs file. default to cost 1 if no entry for a node. file may be empty.
	output_filename = argv[3];
	output_file = fopen(output_filename, "w");
	// initialize network
	num_init_cost_nodes = 0;
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
	while (getline(&lineptr, &n, costs_file) != -1) {
		int node, cost;
		sscanf(lineptr, "%d %d", &node, &cost);
		network[globalMyID][node].init_cost = cost;
		network[node][globalMyID].init_cost = cost;
		init_cost_nodes[num_init_cost_nodes] = node;
		num_init_cost_nodes++;
		free(lineptr); n = 0;
	}
	free(lineptr); n = 0;
	fclose(costs_file);
	init_costs_updated = true;
	
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
