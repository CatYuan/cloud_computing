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

typedef struct RouterEdge {
  bool connected;
  int init_cost;
  int seq_num;
} RouterEdge;

int globalMyID = 0;
int num_routers = 256;
struct RouterEdge network[num_routers][num_routers];

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

int[] disjkstra() {
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
  for (int i = 0; i < num_routers, i++) {
    int u = minDistRouter(dist);
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

