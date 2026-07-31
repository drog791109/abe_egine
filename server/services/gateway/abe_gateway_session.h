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

    abe::adapter::net::TcpLink* link() const;
    abe::adapter::net::TcpLink* tcp_link_slot();
    int handle_packet(const void* packet, uint32_t packet_size, uint64_t now_ms);

protected:
    virtual int on_connect(const abe::service::session::SessionOpenRequest& request);
    virtual void on_close(uint32_t reason, uint64_t now_ms);
    virtual void on_reset();
    virtual int on_message(const void* data, uint32_t size, uint64_t now_ms);
    virtual int send_packet(const void* data, uint32_t size);

private:
    struct GatewayMessage {
        uint32_t message_id;
        const void* data;
        uint32_t size;
        uint64_t recv_time_ms;
    };

    typedef int (GatewaySession::*MessageHandler)(const GatewayMessage& message);

    static void register_handlers();

    int dispatch_message(const GatewayMessage& message);
    int handle_ping(const GatewayMessage& message);
    int handle_backend_message(const GatewayMessage& message);

    abe::adapter::net::TcpLink tcp_link_;

    static std::unordered_map<uint32_t, MessageHandler> handlers_;
    static std::once_flag handlers_once_;
};

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GATEWAY_SESSION_H */
