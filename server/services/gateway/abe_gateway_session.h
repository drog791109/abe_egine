#ifndef ABE_SERVICE_GATEWAY_SESSION_H
#define ABE_SERVICE_GATEWAY_SESSION_H

#include "abe_net_link.h"
#include "abe_session.h"

#include <mutex>
#include <stdint.h>
#include <unordered_map>

namespace abe {
namespace service {
namespace gateway {

class GatewaySession : public abe::service::session::Session {
public:
    GatewaySession();

    using abe::service::session::Session::close;

    void close();

    int active() const;
    uint64_t link_id() const;
    abe::adapter::net::TcpLink* link() const;
    int handle_packet(const void* packet, uint32_t packet_size, uint64_t now_ms);

protected:
    virtual int on_open(const abe::service::session::SessionOpenRequest& request);
    virtual void on_close(uint32_t reason, uint64_t now_ms);
    virtual void on_reset();
    virtual int on_send(const void* data, uint32_t size);

private:
    typedef int (GatewaySession::*MessageHandler)(
        const abe::service::session::SessionMessage& message);

    static void register_handlers();

    int dispatch_message(
        const abe::service::session::SessionMessage& message);
    int handle_ping(
        const abe::service::session::SessionMessage& message);
    int handle_backend_message(
        const abe::service::session::SessionMessage& message);

    void clear_gateway_state();

    abe::adapter::net::TcpLink* link_;
    uint64_t link_id_;

    static std::unordered_map<uint32_t, MessageHandler> handlers_;
    static std::once_flag handlers_once_;
};

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GATEWAY_SESSION_H */
