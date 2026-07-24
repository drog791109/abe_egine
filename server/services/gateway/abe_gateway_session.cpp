#include "abe_gateway_session.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace gateway {

GatewaySession::GatewaySession()
    : link_(NULL),
      logic_session_(NULL),
      link_id_(0u)
{
}

int GatewaySession::open(
    abe::adapter::net::TcpLink* link,
    abe::logic::session::Session* logic_session)
{
    if (link == NULL || logic_session == NULL) {
        return abe::logic::session::SESSION_STATUS_INVALID_ARG;
    }

    link_ = link;
    logic_session_ = logic_session;
    link_id_ = current_link_id();
    logic_session_->set_send_handler(GatewaySession::on_send, this);
    return abe::logic::session::SESSION_STATUS_OK;
}

void GatewaySession::close()
{
    link_ = NULL;
    logic_session_ = NULL;
    link_id_ = 0u;
}

int GatewaySession::handle_message(
    uint32_t msg_id,
    const void* data,
    uint32_t size,
    uint64_t now_ms)
{
    if (!active()) {
        return abe::logic::session::SESSION_STATUS_NOT_FOUND;
    }
    return logic_session_->handle_message(msg_id, data, size, now_ms);
}

int GatewaySession::active() const
{
    if (link_ == NULL || logic_session_ == NULL || link_id_ == 0u) {
        return 0;
    }
    if (!logic_session_->active()) {
        return 0;
    }
    if (logic_session_->link_id() != link_id_) {
        return 0;
    }
    return 1;
}

uint64_t GatewaySession::link_id() const
{
    return link_id_;
}

abe::adapter::net::TcpLink* GatewaySession::link() const
{
    return link_;
}

abe::logic::session::Session* GatewaySession::logic_session() const
{
    return logic_session_;
}

int GatewaySession::on_send(
    abe::logic::session::Session* session,
    const void* data,
    uint32_t size,
    void* user_data)
{
    GatewaySession* gateway_session;

    gateway_session = (GatewaySession*)user_data;
    if (session == NULL || gateway_session == NULL || gateway_session->link_ == NULL) {
        return abe::logic::session::SESSION_STATUS_INVALID_ARG;
    }

    return gateway_session->link_->send(data, size);
}

uint64_t GatewaySession::current_link_id() const
{
    return (uint64_t)(uintptr_t)link_;
}

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */
