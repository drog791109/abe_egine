#include "abe_gateway_server.h"

#include "abe_log.h"
#include "abe_protocol.h"
#include "abe_time.h"
#include "protocol.pb.h"

#include <string.h>

namespace abe {
namespace service {
namespace gateway {

namespace proto = ::abe::proto::client;

enum {
    ABE_GATEWAY_DEFAULT_PORT = 7000u,
    ABE_GATEWAY_DEFAULT_BACKLOG = 128u,
    ABE_GATEWAY_DEFAULT_IDLE_TIMEOUT_MS = 60000u,
    ABE_GATEWAY_DEFAULT_SERVER_ID = 1u
};

static int protocol_status_to_session_status(int status)
{
    if (status == ABE_PROTOCOL_OK) {
        return proto::ERROR_CODE_OK;
    }
    return proto::ERROR_CODE_COMMON_PROTOCOL_ERROR;
}

void set_gateway_defaults(GatewayServerConfig* config)
{
    if (config == NULL) {
        return;
    }

    config->host = "0.0.0.0";
    config->port = ABE_GATEWAY_DEFAULT_PORT;
    config->max_clients = ABE_GATEWAY_MAX_CLIENTS;
    config->backlog = ABE_GATEWAY_DEFAULT_BACKLOG;
    config->max_packet_size = 0u;
    config->server_id = ABE_GATEWAY_DEFAULT_SERVER_ID;
    config->idle_timeout_ms = ABE_GATEWAY_DEFAULT_IDLE_TIMEOUT_MS;
}

GatewayServer::GatewayServer()
    : message_queue_(NULL),
      session_ready_(0),
      tcp_ready_(0)
{
    set_gateway_defaults(&config_);
}

const char* GatewayServer::name() const
{
    return "gateway";
}

const char* GatewayServer::config_path() const
{
    return "bin/gate.json";
}

void GatewayServer::defaults()
{
    set_gateway_defaults(&config_);
}

int GatewayServer::load_config(const abe_config_t* config)
{
    const char* text;
    uint64_t value;
    int rc;

    if (config == NULL) {
        return abe::service::common::SERVICE_STATUS_OK;
    }

    rc = abe_config_get_string(config, "gateway.host", &text);
    if (rc == ABE_CONFIG_OK) {
        config_.host = text;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gateway config path=gateway.host status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "gateway.port", &value);
    if (rc == ABE_CONFIG_OK && value >= 1u && value <= 65535u) {
        config_.port = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gateway config path=gateway.port status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "gateway.max_clients", &value);
    if (rc == ABE_CONFIG_OK && value >= 1u && value <= ABE_GATEWAY_MAX_CLIENTS) {
        config_.max_clients = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gateway config path=gateway.max_clients status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "gateway.backlog", &value);
    if (rc == ABE_CONFIG_OK && value >= 1u && value <= 65535u) {
        config_.backlog = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gateway config path=gateway.backlog status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "gateway.max_packet_size", &value);
    if (rc == ABE_CONFIG_OK && value <= 16777216u) {
        config_.max_packet_size = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gateway config path=gateway.max_packet_size status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "gateway.server_id", &value);
    if (rc == ABE_CONFIG_OK && value != 0u) {
        config_.server_id = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gateway config path=gateway.server_id status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "gateway.idle_ms", &value);
    if (rc == ABE_CONFIG_OK) {
        config_.idle_timeout_ms = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gateway config path=gateway.idle_ms status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    return abe::service::common::SERVICE_STATUS_OK;
}

int GatewayServer::init(abe::service::common::Context& context)
{
    int rc;

    if (context.loop == NULL ||
        context.message_queue == NULL ||
        config_.host == NULL ||
        config_.max_clients == 0u ||
        config_.max_clients > ABE_GATEWAY_MAX_CLIENTS ||
        config_.backlog == 0u ||
        config_.backlog > 65535u ||
        config_.max_packet_size > 16777216u ||
        config_.server_id == 0u ||
        config_.port == 0u ||
        config_.port > 65535u) {
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    message_queue_ = context.message_queue;

    rc = init_sessions();
    if (rc != abe::service::common::SERVICE_STATUS_OK) {
        close(abe_time_mono_ms());
        return rc;
    }

    rc = listen(context.loop);
    if (rc != abe::service::common::SERVICE_STATUS_OK) {
        close(abe_time_mono_ms());
        return rc;
    }

    ABE_LOG_INFO(
        "gateway listening on %s:%u server_id=%llu max_clients=%u",
        config_.host,
        (unsigned int)config_.port,
        (unsigned long long)config_.server_id,
        config_.max_clients);
    return abe::service::common::SERVICE_STATUS_OK;
}

int GatewayServer::update(uint64_t now_ms)
{
    uint32_t closed_count;
    int rc;

    if (!initialized()) {
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    closed_count = 0u;
    rc = sessions_.update(now_ms, &closed_count);
    if (rc != proto::ERROR_CODE_OK) {
        ABE_LOG_ERROR("gateway session update failed rc=%d", rc);
        return abe::service::common::SERVICE_STATUS_FAILED;
    }
    return abe::service::common::SERVICE_STATUS_OK;
}

void GatewayServer::close(uint64_t now_ms)
{
    if (tcp_ready_) {
        tcp_.close();
        tcp_ready_ = 0;
    }
    if (session_ready_) {
        sessions_.close(now_ms);
        session_ready_ = 0;
    }
    message_queue_ = NULL;
}

int GatewayServer::on_connect(abe::adapter::net::TcpLink* link, uint64_t now_ms)
{
    return open_session_for_link(link, now_ms);
}

int GatewayServer::on_receive(
    abe::adapter::net::TcpLink* link,
    const void* packet,
    uint32_t packet_size,
    uint64_t now_ms)
{
    int rc;

    if (!initialized() || link == NULL || message_queue_ == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    rc = message_queue_->push(
        GatewayServer::process_queued_message,
        this,
        link,
        link_id(link),
        packet,
        packet_size,
        now_ms);
    if (rc == abe::service::common::SERVICE_STATUS_NO_SLOT ||
        rc == ABE_NO_MEMORY) {
        ABE_LOG_WARN(
            "gateway message queue full or memory exhausted rc=%d link_id=%llu",
            rc,
            (unsigned long long)link_id(link));
        return proto::ERROR_CODE_COMMON_SERVER_BUSY;
    }
    if (rc != abe::service::common::SERVICE_STATUS_OK) {
        ABE_LOG_WARN(
            "gateway message enqueue failed rc=%d link_id=%llu",
            rc,
            (unsigned long long)link_id(link));
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    return proto::ERROR_CODE_OK;
}

int GatewayServer::on_disconnect(
    abe::adapter::net::TcpLink* link,
    int error_code,
    uint64_t now_ms)
{
    return close_session_for_link(link, (uint32_t)error_code, now_ms);
}

abe::logic::session::SessionServer* GatewayServer::session_server()
{
    return &sessions_;
}

const abe::logic::session::SessionServer* GatewayServer::session_server() const
{
    return &sessions_;
}

int GatewayServer::initialized() const
{
    return session_ready_ && tcp_ready_;
}

void GatewayServer::tcp_on_connect(
    abe::adapter::net::TcpServer* server,
    abe::adapter::net::TcpLink* link,
    void* user_data)
{
    GatewayServer* gateway;

    (void)server;
    gateway = (GatewayServer*)user_data;
    if (gateway != NULL) {
        (void)gateway->on_connect(link, abe_time_mono_ms());
    }
}

void GatewayServer::tcp_on_receive(
    abe::adapter::net::TcpLink* link,
    const void* packet,
    uint32_t packet_size,
    void* user_data)
{
    GatewayServer* gateway;

    gateway = (GatewayServer*)user_data;
    if (gateway != NULL) {
        (void)gateway->on_receive(link, packet, packet_size, abe_time_mono_ms());
    }
}

void GatewayServer::tcp_on_disconnect(
    abe::adapter::net::TcpLink* link,
    int error_code,
    void* user_data)
{
    GatewayServer* gateway;

    gateway = (GatewayServer*)user_data;
    if (gateway != NULL) {
        (void)gateway->on_disconnect(link, error_code, abe_time_mono_ms());
    }
}

int GatewayServer::process_queued_message(
    const abe::service::common::Message& message,
    void* user_data)
{
    GatewayServer* gateway;

    gateway = (GatewayServer*)user_data;
    if (gateway == NULL || message.source == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    return gateway->dispatch(
        (abe::adapter::net::TcpLink*)message.source,
        message.data,
        message.data_size,
        message.enqueue_time_ms);
}

int GatewayServer::init_sessions()
{
    abe::logic::session::SessionServerConfig session_config;
    int rc;

    memset(&session_config, 0, sizeof(session_config));
    session_config.server_id = config_.server_id;
    session_config.sessions = session_slots_;
    session_config.session_count = config_.max_clients;
    session_config.session_size = (uint32_t)sizeof(session_slots_[0]);
    session_config.idle_timeout_ms = config_.idle_timeout_ms;

    rc = sessions_.init(session_config);
    if (rc != proto::ERROR_CODE_OK) {
        ABE_LOG_ERROR("gateway session init failed rc=%d", rc);
        return abe::service::common::SERVICE_STATUS_FAILED;
    }
    session_ready_ = 1;
    return abe::service::common::SERVICE_STATUS_OK;
}

int GatewayServer::listen(abe::adapter::net::Loop* loop)
{
    abe::adapter::net::TcpServerConfig tcp_config;
    abe::adapter::net::TcpServerCallbacks callbacks;
    int rc;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_connect = GatewayServer::tcp_on_connect;
    callbacks.on_receive = GatewayServer::tcp_on_receive;
    callbacks.on_disconnect = GatewayServer::tcp_on_disconnect;
    callbacks.user_data = this;

    memset(&tcp_config, 0, sizeof(tcp_config));
    tcp_config.host = config_.host;
    tcp_config.port = (uint16_t)config_.port;
    tcp_config.max_packet_size = config_.max_packet_size;
    tcp_config.backlog = (int)config_.backlog;
    tcp_config.links = link_slots_;
    tcp_config.link_count = config_.max_clients;
    tcp_config.callbacks = callbacks;

    rc = tcp_.init(loop, &tcp_config);
    if (rc != ABE_NET_OK) {
        ABE_LOG_ERROR("gateway listen failed rc=%d host=%s port=%u",
            rc,
            config_.host == NULL ? "" : config_.host,
            config_.port);
        return abe::service::common::SERVICE_STATUS_FAILED;
    }
    tcp_ready_ = 1;
    return abe::service::common::SERVICE_STATUS_OK;
}

int GatewayServer::open_session_for_link(
    abe::adapter::net::TcpLink* link,
    uint64_t now_ms)
{
    abe::logic::session::SessionOpenRequest request;
    abe::logic::session::Session* session;
    GatewaySession* gateway_session;
    int status;

    if (!initialized() || link == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    request.link_id = link_id(link);
    request.conn_id = request.link_id;
    request.now_ms = now_ms;
    request.link_user_data = link;

    session = sessions_.open_session(request, &status);
    if (session == NULL || status != proto::ERROR_CODE_OK) {
        link->disconnect();
        return status;
    }

    gateway_session = (GatewaySession*)session;
    if (!gateway_session->active() || gateway_session->link() != link) {
        sessions_.close_session(request.link_id, 0u, now_ms);
        link->disconnect();
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    return proto::ERROR_CODE_OK;
}

int GatewayServer::close_session_for_link(
    abe::adapter::net::TcpLink* link,
    uint32_t reason,
    uint64_t now_ms)
{
    if (!initialized() || link == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    return sessions_.close_session(link_id(link), reason, now_ms);
}

int GatewayServer::dispatch(
    abe::adapter::net::TcpLink* link,
    const void* packet,
    uint32_t packet_size,
    uint64_t now_ms)
{
    abe_msg_packet_view_t view;
    GatewaySession* session;
    uint64_t id;
    int rc;

    if (!initialized() || link == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    id = link_id(link);
    session = find_session(id);
    if (session == NULL) {
        return proto::ERROR_CODE_SESSION_NOT_FOUND;
    }

    rc = abe_msg_packet_decode(packet, packet_size, &view);
    if (rc != ABE_PROTOCOL_OK) {
        return protocol_status_to_session_status(rc);
    }
    if (view.header.msg_id == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    return session->handle_message(
        view.header.msg_id,
        view.body,
        view.body_size,
        now_ms);
}

uint64_t GatewayServer::link_id(abe::adapter::net::TcpLink* link) const
{
    return (uint64_t)(uintptr_t)link;
}

GatewaySession* GatewayServer::find_session(uint64_t link_id)
{
    uint32_t index;

    if (link_id == 0u) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_clients) {
        if (session_slots_[index].active() &&
            session_slots_[index].link_id() == link_id) {
            return &session_slots_[index];
        }
        ++index;
    }
    return NULL;
}

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */
