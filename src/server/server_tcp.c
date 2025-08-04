#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#define SERVER_PORT 6160
#define WORKER_COUNT 3
#define WORKER_BASE_PORT 9001

pid_t worker_pids[WORKER_COUNT];
int current_worker = 0;

// Spawn Python workers on different ports
void spawn_workers() {
    for (int i = 0; i < WORKER_COUNT; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char port_str[6];
            sprintf(port_str, "%d", WORKER_BASE_PORT + i);
            // execlp("python3", "python3", "worker.py", port_str, NULL);
            execlp("python3", "python3", "worker_predictor.py", port_str, NULL);
            perror("execlp failed");
            exit(1);
        } else {
            worker_pids[i] = pid;
        }
    }
}

void escape_json(const char *input, char *output) {
    while (*input) {
        if (*input == '\"' || *input == '\\') {
            *output++ = '\\';
        }
        *output++ = *input++;
    }
    *output = '\0';
}

// Forward request to a worker and get response
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

int main() {
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

    // Clean up workers (optional)
    for (int i = 0; i < WORKER_COUNT; i++) {
        kill(worker_pids[i], SIGTERM);
        wait(NULL);
    }

    return 0;
}
