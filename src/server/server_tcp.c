/**
 * @file server_tcp.c
 * @brief TCP front-end server for spawning Python worker processes.
 *
 * The server listens on SERVER_PORT, accepts client TCP connections,
 * and forwards each request to one of several Python workers. Each worker
 * listens on its own port (starting at WORKER_BASE_PORT), runs a model
 * inference (or other logic), and returns a JSON response.
 *
 * Workflow:
 * 1. Spawn WORKER_COUNT Python worker processes on sequential ports.
 * 2. Listen on SERVER_PORT for incoming client requests.
 * 3. For each request:
 *    - Round-robin select a worker.
 *    - Forward the request to the worker.
 *    - Send the worker's response back to the client.
 * 4. On termination, gracefully stop workers.
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
#include <signal.h>

#define SERVER_PORT 6160 /**< Port where this TCP server listens */
#define WORKER_COUNT 3 /**< Number of Python worker processes */
#define WORKER_BASE_PORT 9001 /**< Path to Python worker script */
#define WORKER_SCRIPT "src/worker/worker.py" /**< Path to Python worker script */

pid_t worker_pids[WORKER_COUNT]; /**< PIDs of spawned workers */
int current_worker = 0; /**< Index of next worker in round-robin */

/**
 * @brief Spawn Python workers on sequential ports.
 *
 * Forks the current process WORKER_COUNT times. Each child process
 * replaces itself with a Python worker (`execlp("python3", WORKER_SCRIPT, port)`).
 *
 * @note The worker ports are calculated as WORKER_BASE_PORT + i.
 *       Worker PIDs are stored in @ref worker_pids.
 */
void spawn_workers(void) {
    for (int i = 0; i < WORKER_COUNT; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char port_str[6];
            sprintf(port_str, "%d", WORKER_BASE_PORT + i);
            // execlp("python3", "python3", "worker.py", port_str, NULL);
            execlp("python3", "python3", WORKER_SCRIPT, port_str, NULL);
            perror("execlp failed");
            exit(1);
        } else {
            worker_pids[i] = pid;
        }
    }
}

/**
 * @brief Escape JSON-special characters in a string.
 *
 * Inserts backslashes before quotes (") and backslashes (\\) to make
 * the string JSON-safe.
 *
 * @param input Original string.
 * @param output Buffer to hold the escaped string (must be large enough).
 */
void escape_json(const char *input, char *output) {
    while (*input) {
        if (*input == '\"' || *input == '\\') {
            *output++ = '\\';
        }
        *output++ = *input++;
    }
    *output = '\0';
}

/**
 * @brief Forward a message to a worker and retrieve the response.
 *
 * Selects a worker port in round-robin fashion, connects to the worker,
 * sends the request as a JSON object, and waits for the response.
 *
 * @param message  The raw message string received from the client.
 * @param response Buffer to hold the worker's JSON response.
 */
void forward_to_worker(const char *message, char *response) {
    int worker_port = WORKER_BASE_PORT + current_worker;
    current_worker = (current_worker + 1) % WORKER_COUNT;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in worker_addr;
    worker_addr.sin_family = AF_INET;
    worker_addr.sin_port = htons(worker_port);
    inet_pton(AF_INET, "127.0.0.1", &worker_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&worker_addr, sizeof(worker_addr)) < 0) {
        perror("Worker connection failed");
        strcpy(response, "{\"error\": \"Worker connection failed\"}");
        return;
    }

    char escaped[1024];
    char request[1024];
    // char escaped[1024];
    // escape_json(message, escaped);
    sprintf(escaped, "%s", message);
    escaped[strcspn(escaped, "\n")] = '\0';
    sprintf(request, "{\"message\": \"%s\"}", escaped);
    send(sock, request, strlen(request), 0);
    recv(sock, response, 1024, 0);
    close(sock);
}

/**
 * @brief Main entry point.
 *
 * - Spawns worker processes
 * - Initializes TCP server socket
 * - Accepts client requests and forwards them to workers
 * - Cleans up workers on termination
 *
 * @return 0 on success, non-zero on error
 */
#ifndef UNIT_TEST
int main(void) {
    spawn_workers();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 10);

    printf("C server listening on port %d...\n", SERVER_PORT);

    while (1) {
        int client_socket = accept(server_fd, NULL, NULL);
        if (client_socket < 0) continue;

        char buffer[1024] = {0};
        read(client_socket, buffer, sizeof(buffer));

        char response[1024] = {0};
        forward_to_worker(buffer, response);

        send(client_socket, response, strlen(response), 0);
        close(client_socket);
    }

    // Clean up workers
    for (int i = 0; i < WORKER_COUNT; i++) {
        kill(worker_pids[i], SIGTERM);
        wait(NULL);
    }

    return 0;
}
#endif
