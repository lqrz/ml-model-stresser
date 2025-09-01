/**
 * @file queue.c
 * @brief Linked-list based queue for request_t elements.
 *
 * This module implements FIFO queue where each node stores a
 * pointer to a `request_t`. It supports enqueue and dequeue operations.
 *
 *
 * Typical usage:
 * @code
 *   queue_t* q = create_queue();
 *   enqueue(q, req1);
 *   enqueue(q, req2);
 *   request_t* r = dequeue(q);
 *   // ...
 * @endcode
 *
 * @note Memory for nodes is allocated with malloc() and freed on dequeue.
 *       The caller is responsible for freeing the actual `request_t` objects
 *       once done with them.
 * 
 * Created by lautaro.quiroz on 4/6/25.
 */

#include <stdlib.h>
#include "queue.h"

/** @copydoc create_queue */
queue_t* create_queue(void){
    queue_t *q = malloc(sizeof(queue_t));
    q->head = NULL;
    q->tail = NULL;
    return q;
}

/** @copydoc enqueue */
void enqueue(queue_t *q, request_t *request){
    // create new node
    node_t *new_node = malloc(sizeof(node_t));
    new_node->value = request;
    new_node->next = NULL;
    if(q->tail == NULL){
        // empty queue; track head null tail
        q->head = new_node;
    }else{
        // has elements; add to tail
        q->tail->next = new_node;
    }
    q->tail = new_node;
}

/** @copydoc dequeue */
request_t* dequeue(queue_t *q){
    if(q->head == NULL){
        // no elements in the queue
        return NULL;
    }
    // take out element
    node_t *node = q->head; // head is a pointer
    request_t *request = node->value;
    q->head = q->head->next;
    if(q->head == NULL){
        q->tail = NULL;
    }
    free(node);
    return request;
}
