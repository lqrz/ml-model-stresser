//
//  queue.h
//  queue
//
//  Created by lautaro.quiroz on 4/6/25.
//

#include <netinet/in.h>

typedef struct {
   struct sockaddr_in client_addr;
   socklen_t addr_len;
   int server_socket;
} request_t;

typedef struct node {
    request_t *value;
    struct node *next;
} node_t;

typedef struct {
    node_t *head;
    node_t *tail;
} queue_t;

queue_t* create_queue(void);
void enqueue(queue_t *q, request_t *request);
request_t* dequeue(queue_t* q);
