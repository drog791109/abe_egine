#include "abe_net_link.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

namespace net = abe::adapter::net;

struct runtime_state {
    net::Loop loop;
    net::TcpListener listener;
    net::TcpLink client;
    net::TcpLink server;
    net::UdpLink udp_a;
    net::UdpLink udp_b;
    int tcp_accept_got;
    int tcp_server_got;
    int tcp_client_got;
    int udp_got;
    int failed_line;
};

static uint16_t test_port(unsigned int offset)
{
    return (uint16_t)(20000u + (((unsigned int)getpid() * 17u + offset) % 20000u));
}

static void clear_runtime_flags(runtime_state* state)
{
    state->tcp_accept_got = 0;
    state->tcp_server_got = 0;
    state->tcp_client_got = 0;
    state->udp_got = 0;
    state->failed_line = 0;
}

static void fail_state(runtime_state* state, int line)
{
    if (state->failed_line == 0) {
        state->failed_line = line;
    }
    if (state->loop.valid()) {
        (void)state->loop.stop();
    }
}

#define FAIL_STATE(state) fail_state((state), __LINE__)

static void try_finish_runtime(runtime_state* state)
{
    if (state->tcp_accept_got &&
        state->tcp_server_got &&
        state->tcp_client_got &&
        state->udp_got) {
        (void)state->loop.stop();
    }
}

static void cleanup_runtime(runtime_state* state)
{
    state->listener.close();
    state->client.close();
    state->server.close();
    state->udp_a.close();
    state->udp_b.close();
    state->loop.destroy();
}

static void on_accept(
    net::TcpListener* listener,
    net::TcpLink* link,
    const abe_net_addr_t* peer,
    void* user_data)
{
    runtime_state* state;

    (void)listener;
    state = (runtime_state*)user_data;
    if (link == NULL || !link->valid() || peer == NULL) {
        FAIL_STATE(state);
        return;
    }
    if (state->server.attach(link->handle(), 1) != ABE_NET_OK) {
        FAIL_STATE(state);
        return;
    }
    state->tcp_accept_got = 1;
}

static void on_tcp_message(
    net::TcpLink* link,
    const void* data,
    uint32_t size,
    void* user_data)
{
    runtime_state* state;

    state = (runtime_state*)user_data;
    if (link == NULL || !link->valid()) {
        FAIL_STATE(state);
        return;
    }

    if (state->server.valid() && link->handle() == state->server.handle()) {
        if (size != 4u || memcmp(data, "ping", 4u) != 0) {
            FAIL_STATE(state);
            return;
        }
        state->tcp_server_got = 1;
        if (link->send("pong", 4u) != ABE_NET_OK) {
            FAIL_STATE(state);
            return;
        }
        try_finish_runtime(state);
        return;
    }

    if (state->client.valid() && link->handle() == state->client.handle()) {
        if (size != 4u || memcmp(data, "pong", 4u) != 0) {
            FAIL_STATE(state);
            return;
        }
        state->tcp_client_got = 1;
        state->client.close();
        state->server.close();
        try_finish_runtime(state);
        return;
    }

    FAIL_STATE(state);
}

static void on_tcp_event(
    net::TcpLink* link,
    abe_net_tcp_event_t event,
    int error_code,
    void* user_data)
{
    runtime_state* state;

    (void)error_code;
    state = (runtime_state*)user_data;
    if (link == NULL || !link->valid()) {
        return;
    }
    if (event == ABE_NET_TCP_EVENT_CONNECTED &&
        state->client.valid() &&
        link->handle() == state->client.handle()) {
        if (link->send("ping", 4u) != ABE_NET_OK) {
            FAIL_STATE(state);
        }
    }
}

static void on_udp_message(
    net::UdpLink* link,
    const void* data,
    uint32_t size,
    const abe_net_addr_t* peer,
    void* user_data)
{
    runtime_state* state;

    (void)peer;
    state = (runtime_state*)user_data;
    if (link == NULL ||
        !link->valid() ||
        link->handle() != state->udp_b.handle() ||
        size != 3u ||
        memcmp(data, "udp", 3u) != 0) {
        FAIL_STATE(state);
        return;
    }

    state->udp_got = 1;
    state->udp_a.close();
    state->udp_b.close();
    try_finish_runtime(state);
}

static int test_invalid_args(void)
{
    net::Loop loop;
    net::TcpLink tcp;
    net::TcpListener listener;
    net::UdpLink udp;
    net::TcpConfig tcp_config;
    net::UdpConfig udp_config;

    memset(&tcp_config, 0, sizeof(tcp_config));
    memset(&udp_config, 0, sizeof(udp_config));

    TEST_REQUIRE(tcp.attach(NULL, 1) == ABE_NET_INVALID_ARG);
    TEST_REQUIRE(tcp.connect(NULL, &tcp_config) == ABE_NET_INVALID_ARG);
    TEST_REQUIRE(listener.listen(NULL, &tcp_config) == ABE_NET_INVALID_ARG);
    TEST_REQUIRE(udp.bind(NULL, &udp_config) == ABE_NET_INVALID_ARG);

    TEST_REQUIRE(loop.create() == ABE_NET_OK);
    TEST_REQUIRE(tcp.connect(&loop, NULL) == ABE_NET_INVALID_ARG);
    TEST_REQUIRE(listener.listen(&loop, NULL) == ABE_NET_INVALID_ARG);
    TEST_REQUIRE(udp.bind(&loop, NULL) == ABE_NET_INVALID_ARG);
    loop.destroy();

    return 0;
}

static int start_listener(runtime_state* state, uint16_t port)
{
    net::TcpConfig config;

    memset(&config, 0, sizeof(config));
    config.host = "127.0.0.1";
    config.port = port;
    config.backlog = 16;
    config.callbacks.on_accept = on_accept;
    config.callbacks.on_message = on_tcp_message;
    config.callbacks.on_event = on_tcp_event;
    config.callbacks.user_data = state;

    return state->listener.listen(&state->loop, &config);
}

static int test_tcp_udp_runtime(void)
{
    runtime_state state;
    net::TcpConfig tcp_client_config;
    net::UdpConfig udp_a_config;
    net::UdpConfig udp_b_config;
    uint16_t tcp_port;
    uint16_t udp_a_port;
    uint16_t udp_b_port;
    int rc;

    clear_runtime_flags(&state);
    TEST_REQUIRE(state.loop.create() == ABE_NET_OK);

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
    TEST_REQUIRE(state.client.connect(&state.loop, &tcp_client_config) == ABE_NET_OK);

    memset(&udp_a_config, 0, sizeof(udp_a_config));
    udp_a_config.host = "127.0.0.1";
    udp_a_config.port = udp_a_port;
    udp_a_config.callbacks.on_message = on_udp_message;
    udp_a_config.callbacks.user_data = &state;
    TEST_REQUIRE(state.udp_a.bind(&state.loop, &udp_a_config) == ABE_NET_OK);

    memset(&udp_b_config, 0, sizeof(udp_b_config));
    udp_b_config.host = "127.0.0.1";
    udp_b_config.port = udp_b_port;
    udp_b_config.callbacks.on_message = on_udp_message;
    udp_b_config.callbacks.user_data = &state;
    TEST_REQUIRE(state.udp_b.bind(&state.loop, &udp_b_config) == ABE_NET_OK);

    TEST_REQUIRE(state.udp_a.send_to("127.0.0.1", udp_b_port, "udp", 3u) == ABE_NET_OK);
    TEST_REQUIRE(state.loop.run() == ABE_NET_OK);
    TEST_REQUIRE(state.failed_line == 0);
    TEST_REQUIRE(state.tcp_accept_got);
    TEST_REQUIRE(state.tcp_server_got);
    TEST_REQUIRE(state.tcp_client_got);
    TEST_REQUIRE(state.udp_got);

    cleanup_runtime(&state);
    return 0;
}

int main(void)
{
    signal(SIGALRM, SIG_DFL);
    alarm(10u);

    if (test_invalid_args() != 0) {
        return 1;
    }
    if (test_tcp_udp_runtime() != 0) {
        return 1;
    }

    return 0;
}
