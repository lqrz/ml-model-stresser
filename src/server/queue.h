/**
 * @file queue.h
 * @brief Minimal linked-list queue for request_t elements.
 *
 * This module defines the types and functions for a simple FIFO queue
 * used to store `request_t` objects. Each node holds a pointer to a
 * `request_t`, which contains information about a client request.
 *
 * The queue supports:
 * - Creating a new queue with @ref create_queue
 * - Adding a request with @ref enqueue
 * - Removing the oldest request with @ref dequeue
 * 
 * Created by lautaro.quiroz on 4/6/25.
 */

#include <netinet/in.h>  // for struct sockaddr_in

/**
 * @struct request_t
 * @brief Represents a client request received by the server.
 *
 * Encapsulates the client socket address and the server socket that
 * received the request, so the worker thread can reply back.
 */
typedef struct {
   struct sockaddr_in client_addr;
   socklen_t addr_len;
   int server_socket;
} request_t;

/**
 * @struct node_t
 * @brief Single node in the queue linked list.
 *
 * Each node stores a pointer to a request and a pointer to the next node.
 */
typedef struct node {
    request_t *value;
    struct node *next;
} node_t;

/**
 * @struct queue_t
 * @brief FIFO queue implemented as a singly linked list.
 *
 * Maintains pointers to the head (oldest element) and tail (newest element).
 */
typedef struct {
    node_t *head;
    node_t *tail;
} queue_t;

/**
 * @brief Create a new empty queue.
 *
 * Allocates memory for a queue_t instance and initializes
 * head and tail to NULL.
 *
 * @return Pointer to a new queue_t, or NULL if allocation fails.
 */
queue_t* create_queue(void);

/**
 * @brief Enqueue a request at the tail of the queue.
 *
 * Allocates a new node and appends it to the end of the list.
 *
 * @param q Pointer to the queue.
 * @param request Pointer to the request to enqueue.
 */
void enqueue(queue_t *q, request_t *request);

/**
 * @brief Dequeue a request from the head of the queue.
 *
 * Removes the oldest node from the queue and returns the request.
 * If the queue is empty, returns NULL.
 *
 * @param q Pointer to the queue.
 * @return Pointer to the dequeued request, or NULL if empty.
 */
request_t* dequeue(queue_t* q);
