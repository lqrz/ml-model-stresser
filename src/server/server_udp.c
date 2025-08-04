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

#define SERVER_PORT 6160
#define WORKER_BASE_PORT 9001
#define THREAD_POOL_SIZE 3
#define BUFFER_SIZE 1024

typedef struct {
    queue_t *queue;
    int *python_worker_port;
} thread_arg_t;

pthread_t THREAD_POOL[THREAD_POOL_SIZE];
thread_arg_t *THREAD_ARGS[THREAD_POOL_SIZE];
pthread_mutex_t QUEUE_MUTEX= PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t IS_NOT_EMPTY_QUEUE = PTHREAD_COND_INITIALIZER;

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

// forward request to a worker and get response 
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

void * handle_connection(request_t *r, int *worker_port){
    
    // printf("Replying to client\n");
    
    char response[BUFFER_SIZE] = {0};

    char *message = "Hello";
    forward_to_worker(message, response, worker_port);
    
    // printf("Responding to client: %s len: %d\n", response, strlen(response));
    sendto(r->server_socket, response, strlen(response), 0, (struct sockaddr *)&(r->client_addr), r->addr_len);
}

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
