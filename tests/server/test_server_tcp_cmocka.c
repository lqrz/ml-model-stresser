// tests/unit/test_server_tcp_cmocka.c

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "server/server_tcp.c"

void escape_json(const char *input, char *output);
void forward_to_worker(const char *message, char *response);
extern int current_worker;  // defined in server_tcp.c

// Ports and constants (mirror server_tcp.c)
#ifndef WORKER_BASE_PORT
#define WORKER_BASE_PORT 9001
#endif
#ifndef WORKER_COUNT
#define WORKER_COUNT 3
#endif

/* -------------------- Simple TCP worker stub -------------------- */

typedef struct {
    int port;
    const char* reply;   // JSON text to send back
    pthread_t tid;
    int sockfd;
    bool started;
} worker_stub_t;

static void* worker_stub_thread(void* arg) {
    worker_stub_t* ws = (worker_stub_t*)arg;

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    assert_true(srv >= 0);
    ws->sockfd = srv;

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ws->port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    assert_int_equal(bind(srv, (struct sockaddr*)&addr, sizeof(addr)), 0);
    assert_int_equal(listen(srv, 1), 0);

    ws->started = true;

    int cli = accept(srv, NULL, NULL);
    if (cli >= 0) {
        char buf[1024];
        (void)recv(cli, buf, sizeof(buf), 0);    // ignore content, just reply
        send(cli, ws->reply, (int)strlen(ws->reply), 0);
        close(cli);
    }

    close(srv);
    return NULL;
}

static void worker_stub_start(worker_stub_t* ws, int port, const char* reply) {
    memset(ws, 0, sizeof(*ws));
    ws->port = port;
    ws->reply = reply;
    int rc = pthread_create(&ws->tid, NULL, worker_stub_thread, ws);
    assert_int_equal(rc, 0);

    // Busy-wait briefly until the thread sets up listen()
    for (int i = 0; i < 100 && !ws->started; ++i) {
        usleep(1000 * 5); // 5ms
    }
    assert_true(ws->started);
}

static void worker_stub_join(worker_stub_t* ws) {
    pthread_join(ws->tid, NULL);
}

/* ----------------------------- Tests ---------------------------- */

void test_escape_json_basic(void **state) {
    (void)state;
    char out[256];

    escape_json("plain", out);
    assert_string_equal(out, "plain");

    escape_json("quote: \" ", out);
    assert_string_equal(out, "quote: \\\" ");

    escape_json("backslash: \\\\ end", out);
    assert_string_equal(out, "backslash: \\\\\\\\ end"); /* \\ -> \\\\ */

    escape_json("mix: \\ and \" ok", out);
    assert_string_equal(out, "mix: \\\\ and \\\" ok");
}

void test_forward_to_worker_round_robin(void **state) {
    (void)state;

    // Spin up 3 stub workers on the exact ports forward_to_worker expects.
    worker_stub_t w[WORKER_COUNT];
    worker_stub_start(&w[0], WORKER_BASE_PORT + 0, "{\"w\":9001}");
    worker_stub_start(&w[1], WORKER_BASE_PORT + 1, "{\"w\":9002}");
    worker_stub_start(&w[2], WORKER_BASE_PORT + 2, "{\"w\":9003}");

    // Reset scheduler
    current_worker = 0;

    char resp1[1024] = {0};
    char resp2[1024] = {0};
    char resp3[1024] = {0};

    forward_to_worker("hello\n", resp1);
    forward_to_worker("hello\n", resp2);
    forward_to_worker("hello\n", resp3);

    assert_string_equal(resp1, "{\"w\":9001}");
    assert_string_equal(resp2, "{\"w\":9002}");
    assert_string_equal(resp3, "{\"w\":9003}");

    // Join workers after one request each
    worker_stub_join(&w[0]);
    worker_stub_join(&w[1]);
    worker_stub_join(&w[2]);
}

void test_forward_to_worker_error_when_no_listener(void **state) {
    (void)state;

    // Ensure no one listens on port 9001
    current_worker = 0;

    char resp[1024] = {0};
    forward_to_worker("hey", resp);

    // server_tcp.c error string:
    assert_string_equal(resp, "{\"error\": \"Worker connection failed\"}");
}

/* --------------------------- Runner ----------------------------- */

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_escape_json_basic),
        cmocka_unit_test(test_forward_to_worker_round_robin),
        cmocka_unit_test(test_forward_to_worker_error_when_no_listener),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
