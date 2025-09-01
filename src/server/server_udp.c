/**
 * @file server_udp.c
 * @brief UDP server with thread pool dispatching to Python workers.
 *
 * This server implements a multi-threaded model:
 * - A UDP socket listens on SERVER_PORT.
 * - Each incoming datagram is wrapped into a @ref request_t and enqueued.
 * - A fixed-size thread pool waits on the queue.
 * - Each thread spawns a dedicated Python worker (bound to a unique port).
 * - Threads dequeue requests and forward them to their worker, then send
 *   the worker’s response back to the client via UDP.
 *
 * Synchronization is provided with a global mutex and condition variable.
 *
 * @note This implementation uses fork()/execlp() to start Python processes
 *       and simple round-robin scheduling for load balancing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <pthread.h>
#include "queue.h"

#define SERVER_PORT 6160 /**< UDP server listening port */
#define WORKER_BASE_PORT 9001 /**< Base port where Python workers start */
#define THREAD_POOL_SIZE 3 /**< Number of threads in the pool */
#define BUFFER_SIZE 1024 /**< Buffer size for requests/responses */

/**
 * @struct thread_arg_t
 * @brief Arguments passed to each worker thread.
 *
 * Contains a reference to the shared request queue and the unique
 * port number where this thread’s Python worker is listening.
 */
typedef struct {
    queue_t *queue; /**< Shared request queue */
    int *python_worker_port; /**< Port number assigned to this thread's worker */
} thread_arg_t;

/* --- Global thread pool state --- */
pthread_t THREAD_POOL[THREAD_POOL_SIZE]; /**< Array of thread IDs */
thread_arg_t *THREAD_ARGS[THREAD_POOL_SIZE]; /**< Array of per-thread args */
pthread_mutex_t QUEUE_MUTEX= PTHREAD_MUTEX_INITIALIZER; /**< Protects queue access */
pthread_cond_t IS_NOT_EMPTY_QUEUE = PTHREAD_COND_INITIALIZER; /**< Signals non-empty */

/**
 * @brief Spawn a Python worker process on the given port.
 *
 * Forks the process and execs the Python worker script, binding
 * it to the port provided via *worker_port.
 *
 * @param worker_port Pointer to port number for the worker.
 * @return PID of the spawned process (parent context only).
 */
pid_t spawn_worker(int *worker_port){
    pid_t pid = fork();
    if(pid == 0){
        char *worker_port_str;
        sprintf(worker_port_str, "%d", *worker_port);
        printf("Spanning python worker on port %s\n", worker_port_str);
        // execlp("python3", "python3", "worker.py", worker_port_str, NULL);
        execlp("python3", "python3", "worker_predictor.py", worker_port_str, NULL);
        perror("execlp failed");
        exit(1);
    }
    return pid;
}

/**
 * @brief Forward a request to a Python worker and receive a response.
 *
 * Creates a UDP socket, sends the given message to the worker, and waits
 * for the response.
 *
 * @param message   Request payload to send (string).
 * @param response  Buffer to hold the worker’s response.
 * @param worker_port Pointer to the port number of the worker.
 */
void forward_to_worker(const char *message, char *response, int *worker_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("UDP socket failed");
        strcpy(response, "{\"error\": \"Socket creation failed\"}");
        return;
    }
    
    struct sockaddr_in worker_addr = {0};
    struct sockaddr_in recv_addr = {0};
    
    socklen_t recv_addr_len = sizeof(recv_addr);
    socklen_t worker_addr_len = sizeof(worker_addr);
    
    worker_addr.sin_family = AF_INET;
    worker_addr.sin_port = htons(*worker_port);
    inet_pton(AF_INET, "127.0.0.1", &worker_addr.sin_addr);

//     current_worker = (current_worker + 1) % WORKER_COUNT;

//     char escaped[1024] = {0};
    char request[BUFFER_SIZE] = {0};
//     sprintf(escaped, "%s", message);
//     escaped[strcspn(escaped, "\n")] = '\0';
    sprintf(request, "{\"message\": \"%s\"}", message);

    sendto(sock, request, BUFFER_SIZE, 0, (struct sockaddr *)&worker_addr, worker_addr_len);

    recvfrom(sock, response, 1024, 0, (struct sockaddr *)&recv_addr, &recv_addr_len);

    // printf("Response from client %s\n", response);
    
    close(sock);
}

/**
 * @brief Handle a single client connection by forwarding to a worker.
 *
 * @param r           Pointer to the request structure with client info.
 * @param worker_port Pointer to the Python worker’s port number.
 * @return Always NULL (to match pthread signature).
 */
void * handle_connection(request_t *r, int *worker_port){
    
    // printf("Replying to client\n");
    
    char response[BUFFER_SIZE] = {0};

    char *message = "Hello";
    forward_to_worker(message, response, worker_port);
    
    // printf("Responding to client: %s len: %d\n", response, strlen(response));
    sendto(r->server_socket, response, strlen(response), 0, (struct sockaddr *)&(r->client_addr), r->addr_len);
}

/**
 * @brief Thread entry point: spawn worker and process requests.
 *
 * Each thread:
 * - Spawns its own Python worker.
 * - Waits for requests in the shared queue.
 * - Processes requests as they arrive by calling handle_connection().
 *
 * @param arg Pointer to a thread_arg_t struct.
 * @return Always NULL.
 */
void * thread_handler(void *arg){
    thread_arg_t *thread_arg = (thread_arg_t *)arg;
    printf("im inside thread_handler init\n");
    
    pid_t python_worker_pid = spawn_worker(thread_arg->python_worker_port);
    
    while(1){
        request_t *r;
        pthread_mutex_lock(&QUEUE_MUTEX);
        if((r = dequeue(thread_arg->queue)) == NULL){
            // printf("No requests in queue. Going to sleep...\n");
            pthread_cond_wait(&IS_NOT_EMPTY_QUEUE, &QUEUE_MUTEX);
        }
        pthread_mutex_unlock(&QUEUE_MUTEX);
        if(r != NULL){
            handle_connection(r, thread_arg->python_worker_port);
        }
    }
}

/**
 * @brief Main entry point for the UDP server.
 *
 * - Initializes the request queue.
 * - Creates THREAD_POOL_SIZE worker threads, each with its own worker port.
 * - Binds a UDP socket on SERVER_PORT.
 * - Receives datagrams, wraps them into request_t structs, and enqueues them.
 *
 * @return 0 on normal termination (never reached in typical run).
 */
#ifndef UNIT_TEST
int main() {
    queue_t *q = create_queue();

    // instantiate threadpool
    for(int i=0; i < THREAD_POOL_SIZE; i++){
        int *python_worker_port = malloc(sizeof(int));
        *python_worker_port = WORKER_BASE_PORT + i;
        THREAD_ARGS[i] = malloc(sizeof(thread_arg_t));
        THREAD_ARGS[i]->queue = q;
        THREAD_ARGS[i]->python_worker_port = python_worker_port;
        pthread_create(&THREAD_POOL[i], NULL, thread_handler, (void *)THREAD_ARGS[i]);
    }

    //spawn_workers();

    //SOCK_DGRAM: indicates its is udp datagram.
    int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket< 0) {
        perror("UDP server socket failed");
        exit(1);
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    socklen_t server_addr_len = sizeof(server_addr);

    if (bind(server_socket, (struct sockaddr *)&server_addr, server_addr_len) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(1);
    }

    printf("UDP C server listening on port %d...\n", SERVER_PORT);

    struct sockaddr_in client_addr = {0};
    socklen_t client_addr_len = sizeof(client_addr);
    
    while (1) {
        
        char buffer[BUFFER_SIZE] = {0};
        ssize_t recv_len = recvfrom(
            server_socket,
            buffer,
            BUFFER_SIZE,
            0,
            (struct sockaddr*)&client_addr,
            &client_addr_len
        );
        
        if(recv_len < 0){
            perror("Recvfrom failed");
            continue;
        }
        
        // printf("Received message from socket\n");
        
        // buffer[recv_len] = "\0";
        
        // enqueue
        request_t *r = malloc(sizeof(request_t));
        r->client_addr = client_addr;
        r->addr_len = client_addr_len;
        r->server_socket = server_socket;
        
        pthread_mutex_lock(&QUEUE_MUTEX);
        enqueue(q, r);
        pthread_cond_signal(&IS_NOT_EMPTY_QUEUE);
        pthread_mutex_unlock(&QUEUE_MUTEX);
   }

//     // Clean up workers
//     for (int i = 0; i < WORKER_COUNT; i++) {
//         kill(worker_pids[i], SIGTERM);
//         wait(NULL);
//     }

    return 0;
}
#endif
