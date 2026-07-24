#include "abe_gateway_app.h"

#include "abe_gateway_protocol.h"
#include "abe_time.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace abe {
namespace service {
namespace gateway {

enum {
    ABE_GATEWAY_DEFAULT_PORT = 7000u,
    ABE_GATEWAY_DEFAULT_BACKLOG = 128u,
    ABE_GATEWAY_DEFAULT_TICK_MS = 10u,
    ABE_GATEWAY_DEFAULT_IDLE_TIMEOUT_MS = 60000u,
    ABE_GATEWAY_DEFAULT_SERVER_ID = 1u
};

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_signal(int value)
{
    (void)value;
    g_stop_requested = 1;
}

void gateway_main_config_set_defaults(GatewayMainConfig* config)
{
    if (config == NULL) {
        return;
    }

    config->host = "0.0.0.0";
    config->port = (uint16_t)ABE_GATEWAY_DEFAULT_PORT;
    config->max_clients = ABE_GATEWAY_MAX_CLIENTS;
    config->backlog = (int)ABE_GATEWAY_DEFAULT_BACKLOG;
    config->max_packet_size = 0u;
    config->tick_ms = ABE_GATEWAY_DEFAULT_TICK_MS;
    config->server_id = ABE_GATEWAY_DEFAULT_SERVER_ID;
    config->idle_timeout_ms = ABE_GATEWAY_DEFAULT_IDLE_TIMEOUT_MS;
}

GatewayApp::GatewayApp()
    : loop_ready_(0),
      service_ready_(0),
      tcp_ready_(0)
{
    gateway_main_config_set_defaults(&config_);
}

int GatewayApp::init(const GatewayMainConfig& config)
{
    int rc;

    config_ = config;
    rc = loop_.create();
    if (rc != ABE_NET_OK) {
        fprintf(stderr, "gateway loop create failed: %d\n", rc);
        return 1;
    }
    loop_ready_ = 1;

    rc = init_service();
    if (rc != 0) {
        close();
        return rc;
    }

    rc = init_tcp_server();
    if (rc != 0) {
        close();
        return rc;
    }
    return 0;
}

int GatewayApp::run()
{
    int rc;

    g_stop_requested = 0;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf(
        "gateway listening on %s:%u server_id=%llu max_clients=%u\n",
        config_.host,
        (unsigned int)config_.port,
        (unsigned long long)config_.server_id,
        config_.max_clients);
    fflush(stdout);

    rc = 0;
    while (g_stop_requested == 0) {
        if (update_once() != 0) {
            rc = 1;
            break;
        }
    }

    printf("gateway stopping\n");
    close();
    return rc;
}

void GatewayApp::close()
{
    if (tcp_ready_) {
        tcp_server_.close();
        tcp_ready_ = 0;
    }
    if (service_ready_) {
        service_.close(abe_time_mono_ms());
        service_ready_ = 0;
    }
    if (loop_ready_) {
        loop_.destroy();
        loop_ready_ = 0;
    }
}

int GatewayApp::init_service()
{
    GatewayServiceConfig service_config;
    int rc;

    memset(&service_config, 0, sizeof(service_config));
    service_config.session_config.server_id = config_.server_id;
    service_config.session_config.sessions = session_slots_;
    service_config.session_config.session_count = config_.max_clients;
    service_config.session_config.idle_timeout_ms = config_.idle_timeout_ms;
    service_config.decode_packet = decode_client_msg_packet;
    service_config.decode_user_data = NULL;
    service_config.handlers = NULL;
    service_config.handler_count = 0u;
    service_config.default_handler = GatewayApp::on_default_message;
    service_config.default_handler_user_data = NULL;

    rc = service_.init(service_config);
    if (rc != abe::logic::session::SESSION_STATUS_OK) {
        fprintf(stderr, "gateway service init failed: %d\n", rc);
        return 1;
    }
    service_ready_ = 1;
    return 0;
}

int GatewayApp::init_tcp_server()
{
    abe::adapter::net::TcpServerConfig tcp_config;
    abe::adapter::net::TcpServerCallbacks tcp_callbacks;
    int rc;

    memset(&tcp_callbacks, 0, sizeof(tcp_callbacks));
    service_.fill_tcp_callbacks(&tcp_callbacks);

    memset(&tcp_config, 0, sizeof(tcp_config));
    tcp_config.host = config_.host;
    tcp_config.port = config_.port;
    tcp_config.max_packet_size = config_.max_packet_size;
    tcp_config.backlog = config_.backlog;
    tcp_config.links = link_slots_;
    tcp_config.link_count = config_.max_clients;
    tcp_config.callbacks = tcp_callbacks;

    rc = tcp_server_.init(&loop_, &tcp_config);
    if (rc != ABE_NET_OK) {
        fprintf(stderr, "gateway listen failed: %d\n", rc);
        return 1;
    }
    tcp_ready_ = 1;
    return 0;
}

int GatewayApp::update_once()
{
    uint64_t now_ms;
    int rc;

    now_ms = abe_time_mono_ms();
    rc = tcp_server_.update();
    if (rc != ABE_NET_OK) {
        fprintf(stderr, "gateway net update failed: %d\n", rc);
        return 1;
    }

    rc = service_.update(now_ms);
    if (rc < 0) {
        fprintf(stderr, "gateway service update failed: %d\n", rc);
        return 1;
    }

    if (config_.tick_ms != 0u) {
        usleep((useconds_t)config_.tick_ms * 1000u);
    }
    return 0;
}

int GatewayApp::on_default_message(
    abe::logic::session::Session* current,
    const abe::logic::session::SessionMessage* message,
    void* user_data)
{
    (void)user_data;
    if (current == NULL || message == NULL) {
        return abe::logic::session::SESSION_STATUS_INVALID_ARG;
    }

    printf(
        "gateway recv link=%llu uid=%llu msg_id=%u size=%u\n",
        (unsigned long long)current->link_id(),
        (unsigned long long)current->uid(),
        message->message_id,
        message->size);
    fflush(stdout);
    return abe::logic::session::SESSION_STATUS_OK;
}

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */
