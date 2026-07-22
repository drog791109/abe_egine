#include "abe_net_link.h"
#include "abe_net_server.h"

#include <signal.h>
#include <stddef.h>
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
    net::TcpLink server_link;
    net::UdpLink udp_a;
    net::UdpLink udp_b;
    int tcp_accept_got;
    int tcp_connect_got;
    int tcp_server_got;
    int tcp_client_got;
    int tcp_disconnect_got;
    int udp_got;
    int failed_line;
};

struct server_client_state {
    net::Loop loop;
    net::TcpServer server;
    net::TcpClient client;
    net::TcpLink server_links[4];
    net::TcpLink client_links[4];
    int server_connect_got;
    int client_connect_got;
    int server_receive_got;
    int client_receive_got;
    int disconnect_got;
    int failed_line;
};

static void on_tcp_receive(
    net::TcpLink* link,
    const void* data,
    uint32_t size,
    void* user_data);
static void on_tcp_disconnect(
    net::TcpLink* link,
    int error_code,
    void* user_data);

static uint16_t test_port(unsigned int offset)
{
    return (uint16_t)(20000u + (((unsigned int)getpid() * 17u + offset) % 20000u));
}

static void clear_runtime_flags(runtime_state* state)
{
    state->tcp_accept_got = 0;
    state->tcp_connect_got = 0;
    state->tcp_server_got = 0;
    state->tcp_client_got = 0;
    state->tcp_disconnect_got = 0;
    state->udp_got = 0;
    state->failed_line = 0;
}

static void clear_server_client_flags(server_client_state* state)
{
    state->server_connect_got = 0;
    state->client_connect_got = 0;
    state->server_receive_got = 0;
    state->client_receive_got = 0;
    state->disconnect_got = 0;
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

static void fail_server_client_state(server_client_state* state, int line)
{
    if (state->failed_line == 0) {
        state->failed_line = line;
    }
}

#define FAIL_SERVER_CLIENT_STATE(state) fail_server_client_state((state), __LINE__)

static void try_finish_runtime(runtime_state* state)
{
    if (state->tcp_accept_got &&
        state->tcp_connect_got &&
        state->tcp_server_got &&
        state->tcp_client_got &&
        state->tcp_disconnect_got &&
        state->udp_got) {
        (void)state->loop.stop();
    }
}

static void cleanup_runtime(runtime_state* state)
{
    state->listener.close();
    state->client.disconnect();
    state->server_link.disconnect();
    state->udp_a.unbind();
    state->udp_b.unbind();
    state->loop.destroy();
}

static void cleanup_server_client(server_client_state* state)
{
    state->client.close();
    state->server.close();
    state->loop.destroy();
}

static void on_tcp_connect(
    net::TcpLink* link,
    void* user_data)
{
    runtime_state* state;

    state = (runtime_state*)user_data;
    if (link == NULL || !link->valid()) {
        FAIL_STATE(state);
        return;
    }
    state->tcp_connect_got = 1;
    if (link->send("ping", 4u) != ABE_NET_OK) {
        FAIL_STATE(state);
    }
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
    if (state->server_link.attach(link, 1) != ABE_NET_OK) {
        FAIL_STATE(state);
        return;
    }
    state->server_link.set_user_data(state);
    state->server_link.on_receive(on_tcp_receive);
    state->server_link.on_disconnect(on_tcp_disconnect);
    state->tcp_accept_got = 1;
}

static void on_tcp_receive(
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

    if (state->server_link.valid() && link->handle() == state->server_link.handle()) {
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
        state->client.disconnect();
        state->server_link.disconnect();
        try_finish_runtime(state);
        return;
    }

    FAIL_STATE(state);
}

static void on_tcp_disconnect(
    net::TcpLink* link,
    int error_code,
    void* user_data)
{
    runtime_state* state;

    (void)error_code;
    state = (runtime_state*)user_data;
    if (link == NULL) {
        FAIL_STATE(state);
        return;
    }
    state->tcp_disconnect_got = 1;
    try_finish_runtime(state);
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
    (void)event;
}

static void on_udp_receive(
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
    state->udp_a.unbind();
    state->udp_b.unbind();
    try_finish_runtime(state);
}

static void on_server_connect(
    net::TcpServer* server,
    net::TcpLink* link,
    void* user_data)
{
    server_client_state* state;

    (void)server;
    state = (server_client_state*)user_data;
    if (link == NULL || !link->valid()) {
        FAIL_SERVER_CLIENT_STATE(state);
        return;
    }
    state->server_connect_got = 1;
}

static void on_client_connect(
    net::TcpClient* client,
    net::TcpLink* link,
    void* user_data)
{
    server_client_state* state;

    (void)client;
    state = (server_client_state*)user_data;
    if (link == NULL || !link->valid()) {
        FAIL_SERVER_CLIENT_STATE(state);
        return;
    }
    state->client_connect_got = 1;
    if (link->send("ping", 4u) != ABE_NET_OK) {
        FAIL_SERVER_CLIENT_STATE(state);
    }
}

static void on_server_client_receive(
    net::TcpLink* link,
    const void* data,
    uint32_t size,
    void* user_data)
{
    server_client_state* state;

    state = (server_client_state*)user_data;
    if (link == NULL || !link->valid()) {
        FAIL_SERVER_CLIENT_STATE(state);
        return;
    }

    if (size == 4u && memcmp(data, "ping", 4u) == 0) {
        state->server_receive_got = 1;
        if (link->send("pong", 4u) != ABE_NET_OK) {
            FAIL_SERVER_CLIENT_STATE(state);
        }
        return;
    }

    if (size == 4u && memcmp(data, "pong", 4u) == 0) {
        state->client_receive_got = 1;
        state->client.close();
        state->server.close();
        return;
    }

    FAIL_SERVER_CLIENT_STATE(state);
}

static void on_server_client_disconnect(
    net::TcpLink* link,
    int error_code,
    void* user_data)
{
    server_client_state* state;

    (void)link;
    (void)error_code;
    state = (server_client_state*)user_data;
    state->disconnect_got = 1;
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
    config.callbacks.on_event = on_tcp_event;
    config.callbacks.user_data = state;

    return state->listener.listen(&state->loop, &config);
}

static int test_tcp_udp_runtime(void)
{
    runtime_state state;
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

    state.client.set_user_data(&state);
    state.client.on_connect(on_tcp_connect);
    state.client.on_receive(on_tcp_receive);
    state.client.on_disconnect(on_tcp_disconnect);
    state.client.on_event(on_tcp_event);
    TEST_REQUIRE(state.client.connect(&state.loop, "127.0.0.1", tcp_port) == ABE_NET_OK);

    state.udp_a.set_user_data(&state);
    state.udp_a.on_receive(on_udp_receive);
    TEST_REQUIRE(state.udp_a.bind(&state.loop, "127.0.0.1", udp_a_port) == ABE_NET_OK);

    state.udp_b.set_user_data(&state);
    state.udp_b.on_receive(on_udp_receive);
    TEST_REQUIRE(state.udp_b.bind(&state.loop, "127.0.0.1", udp_b_port) == ABE_NET_OK);

    TEST_REQUIRE(state.udp_a.send_to("127.0.0.1", udp_b_port, "udp", 3u) == ABE_NET_OK);
    TEST_REQUIRE(state.loop.run() == ABE_NET_OK);
    TEST_REQUIRE(state.failed_line == 0);
    TEST_REQUIRE(state.tcp_accept_got);
    TEST_REQUIRE(state.tcp_connect_got);
    TEST_REQUIRE(state.tcp_server_got);
    TEST_REQUIRE(state.tcp_client_got);
    TEST_REQUIRE(state.tcp_disconnect_got);
    TEST_REQUIRE(state.udp_got);

    cleanup_runtime(&state);
    return 0;
}

static int test_tcp_server_client_runtime(void)
{
    server_client_state state;
    net::TcpServerConfig server_config;
    net::TcpClientConfig client_config;
    uint16_t tcp_port;
    uint32_t index;
    int rc;

    clear_server_client_flags(&state);
    TEST_REQUIRE(state.loop.create() == ABE_NET_OK);

    tcp_port = test_port(401u);

    memset(&server_config, 0, sizeof(server_config));
    server_config.host = "127.0.0.1";
    server_config.port = tcp_port;
    server_config.links = state.server_links;
    server_config.link_count = 4u;
    server_config.callbacks.on_connect = on_server_connect;
    server_config.callbacks.on_receive = on_server_client_receive;
    server_config.callbacks.on_disconnect = on_server_client_disconnect;
    server_config.callbacks.user_data = &state;
    rc = state.server.init(&state.loop, &server_config);
    if (rc != ABE_NET_OK) {
        cleanup_server_client(&state);
        fprintf(stderr, "failed to init tcp server on port %u\n", (unsigned int)tcp_port);
        return 1;
    }

    memset(&client_config, 0, sizeof(client_config));
    client_config.host = "127.0.0.1";
    client_config.port = tcp_port;
    client_config.links = state.client_links;
    client_config.link_count = 4u;
    client_config.callbacks.on_connect = on_client_connect;
    client_config.callbacks.on_receive = on_server_client_receive;
    client_config.callbacks.on_disconnect = on_server_client_disconnect;
    client_config.callbacks.user_data = &state;
    TEST_REQUIRE(state.client.init(&state.loop, &client_config) == ABE_NET_OK);
    TEST_REQUIRE(state.client.connect_one() == ABE_NET_OK);

    for (index = 0u; index < 1000u; ++index) {
        TEST_REQUIRE(state.server.update() == ABE_NET_OK);
        if (state.failed_line != 0 ||
            (state.server_connect_got &&
             state.client_connect_got &&
             state.server_receive_got &&
             state.client_receive_got &&
             state.disconnect_got)) {
            break;
        }
    }

    TEST_REQUIRE(state.failed_line == 0);
    TEST_REQUIRE(state.server_connect_got);
    TEST_REQUIRE(state.client_connect_got);
    TEST_REQUIRE(state.server_receive_got);
    TEST_REQUIRE(state.client_receive_got);
    TEST_REQUIRE(state.disconnect_got);

    cleanup_server_client(&state);
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
    if (test_tcp_server_client_runtime() != 0) {
        return 1;
    }

    return 0;
}
