#include "abe_gateway_service.h"

#include "abe_time.h"

#include <stddef.h>
#include <string.h>

namespace abe {
namespace service {
namespace gateway {

GatewayService::GatewayService()
    : handler_count_(0u),
      initialized_(0)
{
    memset(&config_, 0, sizeof(config_));
    memset(handlers_, 0, sizeof(handlers_));
}

int GatewayService::init(const GatewayServiceConfig& config)
{
    uint32_t index;
    int status;

    if (config.session_config.sessions == NULL ||
        config.session_config.session_count == 0u ||
        config.session_config.server_id == 0u ||
        config.decode_packet == NULL ||
        (config.handler_count != 0u && config.handlers == NULL) ||
        (config.default_handler == NULL && config.default_handler_user_data != NULL) ||
        config.session_config.session_count > GATEWAY_MAX_LINK_SESSIONS ||
        config.handler_count > GATEWAY_MAX_PROTOCOL_HANDLERS) {
        return abe::logic::session::SESSION_STATUS_INVALID_ARG;
    }

    memset(&config_, 0, sizeof(config_));
    memset(handlers_, 0, sizeof(handlers_));
    clear_gateway_sessions();
    config_ = config;
    handler_count_ = 0u;

    index = 0u;
    while (index < config.handler_count) {
        const GatewayHandlerEntry* src;

        src = &config.handlers[index];
        if (src->msg_id == 0u || src->handler == NULL) {
            memset(&config_, 0, sizeof(config_));
            memset(handlers_, 0, sizeof(handlers_));
            return abe::logic::session::SESSION_STATUS_INVALID_ARG;
        }
        handlers_[index] = *src;
        ++index;
    }
    handler_count_ = config.handler_count;

    status = session_server_.init(config.session_config);
    if (status != abe::logic::session::SESSION_STATUS_OK) {
        memset(&config_, 0, sizeof(config_));
        memset(handlers_, 0, sizeof(handlers_));
        handler_count_ = 0u;
        return status;
    }

    initialized_ = 1;
    return abe::logic::session::SESSION_STATUS_OK;
}

void GatewayService::close(uint64_t now_ms)
{
    session_server_.close(now_ms);
    clear_gateway_sessions();
    memset(&config_, 0, sizeof(config_));
    memset(handlers_, 0, sizeof(handlers_));
    handler_count_ = 0u;
    initialized_ = 0;
}

int GatewayService::update(uint64_t now_ms)
{
    int rc;

    if (!initialized_) {
        return abe::logic::session::SESSION_STATUS_CLOSED;
    }
    rc = session_server_.update(now_ms);
    if (rc >= 0) {
        sync_gateway_sessions();
    }
    return rc;
}

void GatewayService::fill_tcp_callbacks(abe::adapter::net::TcpServerCallbacks* out_callbacks)
{
    if (out_callbacks == NULL) {
        return;
    }

    memset(out_callbacks, 0, sizeof(*out_callbacks));
    out_callbacks->on_connect = GatewayService::tcp_on_connect;
    out_callbacks->on_receive = GatewayService::tcp_on_receive;
    out_callbacks->on_disconnect = GatewayService::tcp_on_disconnect;
    out_callbacks->user_data = this;
}

int GatewayService::on_connect(abe::adapter::net::TcpLink* link, uint64_t now_ms)
{
    return open_session_for_link(link, now_ms);
}

int GatewayService::on_receive(
    abe::adapter::net::TcpLink* link,
    const void* packet,
    uint32_t packet_size,
    uint64_t now_ms)
{
    return dispatch_packet(link, packet, packet_size, now_ms);
}

int GatewayService::on_disconnect(
    abe::adapter::net::TcpLink* link,
    int error_code,
    uint64_t now_ms)
{
    return close_session_for_link(link, (uint32_t)error_code, now_ms);
}

abe::logic::session::SessionServer* GatewayService::session_server()
{
    return &session_server_;
}

const abe::logic::session::SessionServer* GatewayService::session_server() const
{
    return &session_server_;
}

int GatewayService::initialized() const
{
    return initialized_;
}

void GatewayService::tcp_on_connect(
    abe::adapter::net::TcpServer* server,
    abe::adapter::net::TcpLink* link,
    void* user_data)
{
    GatewayService* gateway;

    (void)server;
    gateway = (GatewayService*)user_data;
    if (gateway != NULL) {
        (void)gateway->on_connect(link, abe_time_mono_ms());
    }
}

void GatewayService::tcp_on_receive(
    abe::adapter::net::TcpLink* link,
    const void* packet,
    uint32_t packet_size,
    void* user_data)
{
    GatewayService* gateway;

    gateway = (GatewayService*)user_data;
    if (gateway != NULL) {
        (void)gateway->on_receive(link, packet, packet_size, abe_time_mono_ms());
    }
}

void GatewayService::tcp_on_disconnect(
    abe::adapter::net::TcpLink* link,
    int error_code,
    void* user_data)
{
    GatewayService* gateway;

    gateway = (GatewayService*)user_data;
    if (gateway != NULL) {
        (void)gateway->on_disconnect(link, error_code, abe_time_mono_ms());
    }
}

int GatewayService::install_handlers(abe::logic::session::Session* session)
{
    uint32_t index;
    int rc;

    if (session == NULL) {
        return abe::logic::session::SESSION_STATUS_INVALID_ARG;
    }

    index = 0u;
    while (index < handler_count_) {
        rc = session->set_message_handler(
            handlers_[index].msg_id,
            handlers_[index].handler,
            handlers_[index].user_data);
        if (rc != abe::logic::session::SESSION_STATUS_OK) {
            return rc;
        }
        ++index;
    }
    if (config_.default_handler != NULL) {
        rc = session->set_default_message_handler(
            config_.default_handler,
            config_.default_handler_user_data);
        if (rc != abe::logic::session::SESSION_STATUS_OK) {
            return rc;
        }
    }
    return abe::logic::session::SESSION_STATUS_OK;
}

int GatewayService::open_session_for_link(
    abe::adapter::net::TcpLink* link,
    uint64_t now_ms)
{
    abe::logic::session::SessionOpenRequest request;
    abe::logic::session::Session* session;
    GatewaySession* gateway_session;
    int status;

    if (!initialized_ || link == NULL) {
        return abe::logic::session::SESSION_STATUS_INVALID_ARG;
    }

    request.link_id = link_id_from_link(link);
    request.conn_id = request.link_id;
    request.now_ms = now_ms;
    request.link_user_data = link;

    session = session_server_.open_session(request, &status);
    if (session == NULL || status != abe::logic::session::SESSION_STATUS_OK) {
        if (link != NULL) {
            link->disconnect();
        }
        return status;
    }

    gateway_session = find_free_gateway_session();
    if (gateway_session == NULL) {
        session_server_.close_session(request.link_id, 0u, now_ms);
        if (link != NULL) {
            link->disconnect();
        }
        return abe::logic::session::SESSION_STATUS_NO_SLOT;
    }
    status = gateway_session->open(link, session);
    if (status != abe::logic::session::SESSION_STATUS_OK) {
        session_server_.close_session(request.link_id, 0u, now_ms);
        if (link != NULL) {
            link->disconnect();
        }
        return status;
    }

    status = install_handlers(session);
    if (status != abe::logic::session::SESSION_STATUS_OK) {
        session_server_.close_session(request.link_id, 0u, now_ms);
        gateway_session->close();
        if (link != NULL) {
            link->disconnect();
        }
        return status;
    }
    return abe::logic::session::SESSION_STATUS_OK;
}

int GatewayService::close_session_for_link(
    abe::adapter::net::TcpLink* link,
    uint32_t reason,
    uint64_t now_ms)
{
    uint64_t link_id;
    GatewaySession* gateway_session;
    int rc;

    if (!initialized_ || link == NULL) {
        return abe::logic::session::SESSION_STATUS_INVALID_ARG;
    }

    link_id = link_id_from_link(link);
    gateway_session = find_gateway_session(link_id);
    rc = session_server_.close_session(link_id, reason, now_ms);
    if (gateway_session != NULL) {
        gateway_session->close();
    }
    return rc;
}

int GatewayService::dispatch_packet(
    abe::adapter::net::TcpLink* link,
    const void* packet,
    uint32_t packet_size,
    uint64_t now_ms)
{
    uint32_t msg_id;
    const void* payload;
    uint32_t payload_size;
    uint64_t link_id;
    GatewaySession* gateway_session;
    int rc;

    if (!initialized_ || link == NULL) {
        return abe::logic::session::SESSION_STATUS_INVALID_ARG;
    }

    link_id = link_id_from_link(link);
    gateway_session = find_gateway_session(link_id);
    if (gateway_session == NULL) {
        return abe::logic::session::SESSION_STATUS_NOT_FOUND;
    }

    rc = config_.decode_packet(
        packet,
        packet_size,
        &msg_id,
        &payload,
        &payload_size,
        config_.decode_user_data);
    if (rc != abe::logic::session::SESSION_STATUS_OK) {
        return rc;
    }

    return gateway_session->handle_message(msg_id, payload, payload_size, now_ms);
}

uint64_t GatewayService::link_id_from_link(abe::adapter::net::TcpLink* link) const
{
    return (uint64_t)(uintptr_t)link;
}

GatewaySession* GatewayService::find_free_gateway_session()
{
    uint32_t index;

    index = 0u;
    while (index < GATEWAY_MAX_LINK_SESSIONS) {
        if (!gateway_sessions_[index].active()) {
            gateway_sessions_[index].close();
            return &gateway_sessions_[index];
        }
        ++index;
    }
    return NULL;
}

GatewaySession* GatewayService::find_gateway_session(uint64_t link_id)
{
    uint32_t index;

    if (link_id == 0u) {
        return NULL;
    }

    index = 0u;
    while (index < GATEWAY_MAX_LINK_SESSIONS) {
        if (gateway_sessions_[index].active() &&
            gateway_sessions_[index].link_id() == link_id) {
            return &gateway_sessions_[index];
        }
        ++index;
    }
    return NULL;
}

void GatewayService::clear_gateway_sessions()
{
    uint32_t index;

    index = 0u;
    while (index < GATEWAY_MAX_LINK_SESSIONS) {
        gateway_sessions_[index].close();
        ++index;
    }
}

void GatewayService::sync_gateway_sessions()
{
    uint32_t index;

    index = 0u;
    while (index < GATEWAY_MAX_LINK_SESSIONS) {
        if (!gateway_sessions_[index].active()) {
            gateway_sessions_[index].close();
        }
        ++index;
    }
}

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */
