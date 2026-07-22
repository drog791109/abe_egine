#include "abe_net.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

struct runtime_state {
    abe_net_loop_t* loop;
    abe_net_tcp_listener_t* listener;
    abe_net_tcp_conn_t* client;
    abe_net_tcp_conn_t* server;
    abe_net_udp_endpoint_t* udp_a;
    abe_net_udp_endpoint_t* udp_b;
    int tcp_server_got;
    int tcp_client_got;
    int udp_got;
    int oversized_error_got;
    int failed_line;
};

static uint16_t test_port(unsigned int offset)
{
    return (uint16_t)(20000u + (((unsigned int)getpid() * 17u + offset) % 20000u));
}

static void fail_state(struct runtime_state* state, int line)
{
    if (state->failed_line == 0) {
        state->failed_line = line;
    }
    if (state->loop != NULL) {
        (void)abe_net_loop_stop(state->loop);
    }
}

#define FAIL_STATE(state) fail_state((state), __LINE__)

static void try_finish_runtime(struct runtime_state* state)
{
    if (state->tcp_server_got && state->tcp_client_got && state->udp_got) {
        (void)abe_net_loop_stop(state->loop);
    }
}

static void close_tcp_conn(abe_net_tcp_conn_t** conn)
{
    abe_net_tcp_conn_t* tmp;

    if (conn == NULL || *conn == NULL) {
        return;
    }

    tmp = *conn;
    *conn = NULL;
    abe_net_tcp_close(tmp);
}

static void close_udp_endpoint(abe_net_udp_endpoint_t** endpoint)
{
    abe_net_udp_endpoint_t* tmp;

    if (endpoint == NULL || *endpoint == NULL) {
        return;
    }

    tmp = *endpoint;
    *endpoint = NULL;
    abe_net_udp_close(tmp);
}

static void cleanup_runtime(struct runtime_state* state)
{
    if (state->listener != NULL) {
        abe_net_tcp_listener_close(state->listener);
        state->listener = NULL;
    }
    close_tcp_conn(&state->client);
    close_tcp_conn(&state->server);
    close_udp_endpoint(&state->udp_a);
    close_udp_endpoint(&state->udp_b);
    if (state->loop != NULL) {
        abe_net_loop_destroy(state->loop);
        state->loop = NULL;
    }
}

static void on_accept(
    abe_net_tcp_listener_t* listener,
    abe_net_tcp_conn_t* conn,
    const abe_net_addr_t* peer,
    void* user_data)
{
    struct runtime_state* state;

    (void)listener;
    (void)peer;
    state = (struct runtime_state*)user_data;
    state->server = conn;
}

static void on_tcp_message(
    abe_net_tcp_conn_t* conn,
    const void* data,
    uint32_t size,
    void* user_data)
{
    struct runtime_state* state;

    state = (struct runtime_state*)user_data;

    if (conn == state->server) {
        if (size != 4u || memcmp(data, "ping", 4u) != 0) {
            FAIL_STATE(state);
            return;
        }
        state->tcp_server_got = 1;
        if (abe_net_tcp_send(conn, "pong", 4u) != ABE_NET_OK) {
            FAIL_STATE(state);
            return;
        }
        try_finish_runtime(state);
        return;
    }

    if (conn == state->client) {
        if (size != 4u || memcmp(data, "pong", 4u) != 0) {
            FAIL_STATE(state);
            return;
        }
        state->tcp_client_got = 1;
        close_tcp_conn(&state->client);
        close_tcp_conn(&state->server);
        try_finish_runtime(state);
        return;
    }

    FAIL_STATE(state);
}

static void on_tcp_event(
    abe_net_tcp_conn_t* conn,
    abe_net_tcp_event_t event,
    int error_code,
    void* user_data)
{
    struct runtime_state* state;

    state = (struct runtime_state*)user_data;

    if (event == ABE_NET_TCP_EVENT_CONNECTED && conn == state->client) {
        if (abe_net_tcp_send(conn, "ping", 4u) != ABE_NET_OK) {
            FAIL_STATE(state);
        }
        return;
    }

    if (event == ABE_NET_TCP_EVENT_ERROR &&
        conn == state->server &&
        error_code == ABE_NET_PACKET_TOO_LARGE) {
        state->oversized_error_got = 1;
        state->server = NULL;
        (void)abe_net_loop_stop(state->loop);
        return;
    }

    if (event == ABE_NET_TCP_EVENT_CLOSED || event == ABE_NET_TCP_EVENT_ERROR) {
        if (conn == state->client) {
            state->client = NULL;
        }
        if (conn == state->server) {
            state->server = NULL;
        }
    }
}

static void on_udp_message(
    abe_net_udp_endpoint_t* endpoint,
    const void* data,
    uint32_t size,
    const abe_net_addr_t* peer,
    void* user_data)
{
    struct runtime_state* state;

    (void)peer;
    state = (struct runtime_state*)user_data;

    if (endpoint != state->udp_b || size != 3u || memcmp(data, "udp", 3u) != 0) {
        FAIL_STATE(state);
        return;
    }

    state->udp_got = 1;
    close_udp_endpoint(&state->udp_a);
    close_udp_endpoint(&state->udp_b);
    try_finish_runtime(state);
}

static int test_packet_helpers(void)
{
    unsigned char packet[ABE_NET_PACKET_HEADER_SIZE + 8u];
    unsigned char short_buffer[ABE_NET_PACKET_HEADER_SIZE - 1u];
    unsigned char too_large_header[ABE_NET_PACKET_HEADER_SIZE] = {0u, 1u, 0u, 0u};
    unsigned char too_large_payload[ABE_NET_DEFAULT_MAX_PACKET_SIZE + 1u];
    unsigned char too_large_packet[
        ABE_NET_DEFAULT_MAX_PACKET_SIZE + ABE_NET_PACKET_HEADER_SIZE + 1u];
    uint32_t packet_size;
    uint32_t payload_size;
    int rc;

    TEST_REQUIRE(ABE_NET_DEFAULT_MAX_PACKET_SIZE == 65535u);
    TEST_REQUIRE(ABE_NET_DEFAULT_MAX_PACKET_SIZE < 65536u);
    TEST_REQUIRE(abe_net_packet_max_payload_size(0u) == ABE_NET_DEFAULT_MAX_PACKET_SIZE);
    TEST_REQUIRE(abe_net_packet_max_payload_size(1u) == 1u);
    TEST_REQUIRE(abe_net_packet_max_payload_size(65536u) == ABE_NET_DEFAULT_MAX_PACKET_SIZE);

    packet_size = 0;
    rc = abe_net_packet_encode("hello", 5u, packet, sizeof(packet), &packet_size);
    TEST_REQUIRE(rc == ABE_NET_OK);
    TEST_REQUIRE(packet_size == ABE_NET_PACKET_HEADER_SIZE + 5u);
    TEST_REQUIRE(packet[0] == 0u);
    TEST_REQUIRE(packet[1] == 0u);
    TEST_REQUIRE(packet[2] == 0u);
    TEST_REQUIRE(packet[3] == 5u);
    TEST_REQUIRE(memcmp(packet + ABE_NET_PACKET_HEADER_SIZE, "hello", 5u) == 0);

    payload_size = 0;
    rc = abe_net_packet_peek_size(packet, packet_size, &payload_size);
    TEST_REQUIRE(rc == ABE_NET_OK);
    TEST_REQUIRE(payload_size == 5u);

    rc = abe_net_packet_peek_size(short_buffer, sizeof(short_buffer), &payload_size);
    TEST_REQUIRE(rc == ABE_NET_WOULD_BLOCK);

    rc = abe_net_packet_peek_size(too_large_header, sizeof(too_large_header), &payload_size);
    TEST_REQUIRE(rc == ABE_NET_PACKET_TOO_LARGE);

    rc = abe_net_packet_encode("x", 1u, packet, ABE_NET_PACKET_HEADER_SIZE, NULL);
    TEST_REQUIRE(rc == ABE_NET_INVALID_ARG);

    memset(too_large_payload, 0, sizeof(too_large_payload));

    rc = abe_net_packet_encode(
        too_large_payload,
        ABE_NET_DEFAULT_MAX_PACKET_SIZE + 1u,
        too_large_packet,
        ABE_NET_DEFAULT_MAX_PACKET_SIZE + ABE_NET_PACKET_HEADER_SIZE + 1u,
        &packet_size);
    TEST_REQUIRE(rc == ABE_NET_PACKET_TOO_LARGE);

    return 0;
}

static int start_listener(struct runtime_state* state, uint16_t port)
{
    abe_net_tcp_config_t config;

    memset(&config, 0, sizeof(config));
    config.host = "127.0.0.1";
    config.port = port;
    config.callbacks.on_accept = on_accept;
    config.callbacks.on_message = on_tcp_message;
    config.callbacks.on_event = on_tcp_event;
    config.callbacks.user_data = state;

    return abe_net_tcp_listen(state->loop, &config, &state->listener);
}

static int test_tcp_udp_runtime(void)
{
    struct runtime_state state;
    abe_net_tcp_config_t tcp_client_config;
    abe_net_udp_config_t udp_a_config;
    abe_net_udp_config_t udp_b_config;
    uint16_t tcp_port;
    uint16_t udp_a_port;
    uint16_t udp_b_port;
    int rc;

    memset(&state, 0, sizeof(state));
    TEST_REQUIRE(abe_net_loop_create(&state.loop) == ABE_NET_OK);

    tcp_port = test_port(1u);
    udp_a_port = test_port(101u);
    udp_b_port = test_port(201u);

    rc = start_listener(&state, tcp_port);
    if (rc != ABE_NET_OK) {
        cleanup_runtime(&state);
        fprintf(stderr, "failed to listen on tcp port %u\n", (unsigned int)tcp_port);
        return 1;
    }

    memset(&tcp_client_config, 0, sizeof(tcp_client_config));
    tcp_client_config.host = "127.0.0.1";
    tcp_client_config.port = tcp_port;
    tcp_client_config.callbacks.on_message = on_tcp_message;
    tcp_client_config.callbacks.on_event = on_tcp_event;
    tcp_client_config.callbacks.user_data = &state;
    TEST_REQUIRE(abe_net_tcp_connect(state.loop, &tcp_client_config, &state.client) == ABE_NET_OK);

    memset(&udp_a_config, 0, sizeof(udp_a_config));
    udp_a_config.host = "127.0.0.1";
    udp_a_config.port = udp_a_port;
    udp_a_config.callbacks.on_message = on_udp_message;
    udp_a_config.callbacks.user_data = &state;
    TEST_REQUIRE(abe_net_udp_bind(state.loop, &udp_a_config, &state.udp_a) == ABE_NET_OK);

    memset(&udp_b_config, 0, sizeof(udp_b_config));
    udp_b_config.host = "127.0.0.1";
    udp_b_config.port = udp_b_port;
    udp_b_config.callbacks.on_message = on_udp_message;
    udp_b_config.callbacks.user_data = &state;
    TEST_REQUIRE(abe_net_udp_bind(state.loop, &udp_b_config, &state.udp_b) == ABE_NET_OK);

    TEST_REQUIRE(abe_net_udp_send_to(state.udp_a, "127.0.0.1", udp_b_port, "udp", 3u) == ABE_NET_OK);
    TEST_REQUIRE(abe_net_loop_run(state.loop) == ABE_NET_OK);
    TEST_REQUIRE(state.failed_line == 0);
    TEST_REQUIRE(state.tcp_server_got);
    TEST_REQUIRE(state.tcp_client_got);
    TEST_REQUIRE(state.udp_got);

    cleanup_runtime(&state);
    return 0;
}

static int connect_raw_tcp(uint16_t port)
{
    struct sockaddr_in addr;
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int write_all(int fd, const unsigned char* data, size_t size)
{
    size_t written;

    written = 0u;
    while (written < size) {
        ssize_t rc;

        rc = write(fd, data + written, size - written);
        if (rc <= 0) {
            return ABE_NET_ERROR;
        }
        written += (size_t)rc;
    }

    return ABE_NET_OK;
}

static int test_tcp_oversized_packet(void)
{
    struct runtime_state state;
    unsigned char header[ABE_NET_PACKET_HEADER_SIZE] = {0u, 1u, 0u, 0u};
    uint16_t tcp_port;
    int fd;
    int rc;

    memset(&state, 0, sizeof(state));
    TEST_REQUIRE(abe_net_loop_create(&state.loop) == ABE_NET_OK);

    tcp_port = test_port(301u);
    rc = start_listener(&state, tcp_port);
    if (rc != ABE_NET_OK) {
        cleanup_runtime(&state);
        fprintf(stderr, "failed to listen on tcp port %u\n", (unsigned int)tcp_port);
        return 1;
    }

    fd = connect_raw_tcp(tcp_port);
    TEST_REQUIRE(fd >= 0);
    TEST_REQUIRE(write_all(fd, header, sizeof(header)) == ABE_NET_OK);
    TEST_REQUIRE(abe_net_loop_run(state.loop) == ABE_NET_OK);
    close(fd);

    TEST_REQUIRE(state.failed_line == 0);
    TEST_REQUIRE(state.oversized_error_got);

    cleanup_runtime(&state);
    return 0;
}

int main(void)
{
    signal(SIGALRM, SIG_DFL);
    alarm(10u);

    if (test_packet_helpers() != 0) {
        return 1;
    }
    if (test_tcp_udp_runtime() != 0) {
        return 1;
    }
    if (test_tcp_oversized_packet() != 0) {
        return 1;
    }

    return 0;
}
