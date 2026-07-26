#include "abe_gateway_session.h"

#include "protocol.pb.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace gateway {

namespace proto = ::abe::proto::client;

GatewaySession::GatewaySession()
    : link_(NULL),
      link_id_(0u)
{
}

void GatewaySession::close()
{
    reset();
}

int GatewaySession::active() const
{
    if (!abe::logic::session::Session::active() || link_ == NULL || link_id_ == 0u) {
        return 0;
    }
    if (abe::logic::session::Session::link_id() != link_id_) {
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

int GatewaySession::on_open(const abe::logic::session::SessionOpenRequest& request)
{
    abe::adapter::net::TcpLink* link;

    link = (abe::adapter::net::TcpLink*)request.link_user_data;
    if (link == NULL || request.link_id != (uint64_t)(uintptr_t)link) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    link_ = link;
    link_id_ = request.link_id;
    set_send_handler(GatewaySession::on_send, this);
    return proto::ERROR_CODE_OK;
}

void GatewaySession::on_close(uint32_t reason, uint64_t now_ms)
{
    (void)reason;
    (void)now_ms;
    clear_gateway_state();
}

void GatewaySession::on_reset()
{
    clear_gateway_state();
}

int GatewaySession::on_send(
    abe::logic::session::Session* session,
    const void* data,
    uint32_t size,
    void* user_data)
{
    GatewaySession* gateway_session;

    gateway_session = (GatewaySession*)user_data;
    if (session == NULL || gateway_session == NULL ||
        session != (abe::logic::session::Session*)gateway_session ||
        !gateway_session->active() ||
        gateway_session->link_ == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    return gateway_session->link_->send(data, size);
}

void GatewaySession::clear_gateway_state()
{
    link_ = NULL;
    link_id_ = 0u;
}

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */
