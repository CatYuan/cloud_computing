/**
 * http_client.c - implements a simple HTTP client
 */

#define _GNU_SOURCE
#define CHUNK_SIZE 1024

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

typedef struct _host_info {
  char *protocol;
  char *hostname;
  char *resource;
} host_info;

/**
 * Parses the URI/URL
 * Allocates memory for host_info struct
 */
host_info *get_info(char *uri);

/**
 * Frees the memory from a host_info struct
 */
void free_info(host_info *info);

host_info *get_info(char *uri) {
  host_info *info = (host_info*) malloc(sizeof(host_info));
  char *protocol = strtok(uri, ":");
  char *domain = strtok(NULL, "")+2;
  char *hostname = strtok(domain, "/");
  char *resource = strtok(NULL, "");
  if (resource == NULL) {
    resource = "/";
  }
  // store values into info
  info->protocol = (char*) calloc(strlen(protocol) + 1, sizeof(char));
  strcpy(info->protocol, protocol);
  info->hostname = (char*) calloc(strlen(hostname) + 1, sizeof(char));
  strcpy(info->hostname, hostname);
  info->resource = (char*) calloc(strlen(resource) + 1, sizeof(char));
  strcpy(info->resource, resource);
  return info;
}

void free_info(host_info *info) {
  free(info->protocol);
  free(info->hostname);
  free(info->resource);
  free(info);
}

int main(int argc, char *argv[]) {
  // incorrect inputs
  if(argc != 2) {
    fprintf(stderr, "Usage: %s http://hostname[:port]/path_to_file\n", *argv);
    return 1;
  }
  // setup
  char *uri = argv[1];
  // FILE *output_fp = fopen("output", "w");
  host_info *info = get_info(uri);
  // invalid protocol
  if (strcmp(info->protocol, "http") != 0) {
    FILE *output_fp = fopen("output", "w");
    fprintf(output_fp, "INVALIDPROTOCOL");
    free_info(info);
    fclose(output_fp);
    return 1;
  }
  // attempt to connect to server
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  char *host = (char*) calloc(strlen(info->hostname) + 1, sizeof(char));
  strcpy(host, info->hostname);
  strtok(host, ":");
  char *port = strtok(NULL, "");
  if (port == NULL) {
    port = "8000";
  }
  if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
    FILE *output_fp = fopen("output", "w");
    fprintf(output_fp, "NOCONNECTION");
    free(host);
    free_info(info);
    fclose(output_fp);
    return 1;
  }
  free(host);
  // loop through all the results and connect to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("client: socket");
      continue;
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("client: connect");
      continue;
    }
    break;
  }
  // no connection
  if (p == NULL) {
    FILE *output_fp = fopen("output", "w");
    fprintf(output_fp, "NOCONNECTION");
    free_info(info);
    fclose(output_fp);
    return 1;
  }
  // send GET request to server
  char *buffer;
  asprintf(&buffer,
    "GET %s HTTP/1.0\r\n"
    "Connection: close\r\n"
    "User-Agent: Wget/1.15 (linux-gnu)\r\n"
    "Host: %s"
    "Accept: */*\r\n\r\n",
    info->resource, info->hostname);
  write(sockfd, buffer, strlen(buffer));
  free(buffer);
  // read response from server
  FILE *output_fp = fopen("output", "w");
  int size_recv = 0, total_bytes = 0, reading_message = 0, bytes_read = 0;
  char *length = NULL;
  char chunk[CHUNK_SIZE];
  while (1) {
    memset(chunk, 0, CHUNK_SIZE);
    if ((size_recv = recv(sockfd, chunk, CHUNK_SIZE, 0)) < 0) {
      break;
    } else {
      if (bytes_read == total_bytes && total_bytes != 0) { // reached EOF
        break;
      }
      if (strstr(chunk, "404 File not found") != NULL) {
        // check server returns a file
        fprintf(output_fp, "FILENOTFOUND");
        fclose(output_fp);
        output_fp = NULL;
        break;
      }
      if (total_bytes == 0 && (length = strstr(chunk, "Content-Length: ")) != NULL) {
          // check content length
          total_bytes = atoi(length + strlen("Content-Length: "));
      }
      char *message = chunk;
      if (reading_message == 0) {
        message = strstr(chunk, "\r\n\r\n");
        if (message != NULL) {
          message = message + 4;
          reading_message = 1;
        }
      }
      if (reading_message = 1) {
        int header_length = message - chunk;
        bytes_read = size_recv - header_length;
        fwrite(message, bytes_read, 1, output_fp);
      }
    }
  }
  // teardown
  if (output_fp != NULL) {
    fclose(output_fp);
  }
  close(sockfd);
  freeaddrinfo(servinfo);
  free_info(info);
  return 0;
}
