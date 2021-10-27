#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "monitor_neighbors.h"


void listenForNeighbors();
void* announceToNeighbors(void* unusedParam);


int globalMyID = 0;
//last time you heard from each node. monitor this
//in order to realize when a neighbor has gotten cut off from you.
struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
struct sockaddr_in globalNodeAddrs[256];

int num_routers = 256;
char *output_filename;
int init_cost[256];
int neighbor[256];
int network[256][256];
static int forwarding_table[256];
 
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
	int i;
	for(i=0;i<256;i++)
	{
		gettimeofday(&globalLastHeartbeat[i], 0);
		
		char tempaddr[100];
		sprintf(tempaddr, "10.1.1.%d", i);
		memset(&globalNodeAddrs[i], 0, sizeof(globalNodeAddrs[i]));
		globalNodeAddrs[i].sin_family = AF_INET;
		globalNodeAddrs[i].sin_port = htons(7777);
		inet_pton(AF_INET, tempaddr, &globalNodeAddrs[i].sin_addr);
	}
	
	
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

	// initialize global variables
	for (int i = 0; i < 256; i++) {
		forwarding_table[i] = INT_MIN;
		neighbor[i] = -1;
		init_cost[i] = 1;
		if (i == globalMyID) init_cost[i] = 0;
		for (int j = 0; j < 256; j++) {
			network[i][j] = 0;
		}
	}
	// read and parse initial costs file. default to cost 1 if no entry for a node. file may be empty.
	output_filename = argv[3];
	FILE *costs_file = fopen(argv[2], "r");
	int n = 0, bytes_read = 0; char *lineptr = NULL;
	while ((bytes_read = getline(&lineptr, &n, costs_file)) != -1) {
		// TODO: if lineptr[read - 1] == '\n' then do this
		lineptr[bytes_read - 1] = '\0'; 
		int neighbor_id, cost;
		sscanf(lineptr, "%d %d", &neighbor_id, &cost);
		init_cost[neighbor_id] = cost;
		free(lineptr); lineptr = NULL; n = 0;
	}
	free(lineptr); lineptr = NULL; n = 0;
	fclose(costs_file);
	constructEdgeCosts();
	neighbors[globalMyID] = -1;
	
	//start threads... feel free to add your own, and to remove the provided ones.
	pthread_t announcerThread;
	pthread_create(&announcerThread, 0, announceToNeighbors, (void*)0);
	
	// flood network
	pthread_t broadcastThread;
	pthread_create(&broadcastThread, 0, broadcastEdgeCosts, (void*)0);
	
	//good luck, have fun!
	listenForNeighbors();
}
