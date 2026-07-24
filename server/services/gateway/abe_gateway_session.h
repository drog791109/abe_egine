#ifndef ABE_SERVICE_GATEWAY_SESSION_H
#define ABE_SERVICE_GATEWAY_SESSION_H

#include "abe_net_link.h"
#include "abe_session.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace gateway {

class GatewaySession {
public:
    GatewaySession();

    int open(
        abe::adapter::net::TcpLink* link,
        abe::logic::session::Session* logic_session);
    void close();

    int handle_message(
        uint32_t msg_id,
        const void* data,
        uint32_t size,
        uint64_t now_ms);

    int active() const;
    uint64_t link_id() const;
    abe::adapter::net::TcpLink* link() const;
    abe::logic::session::Session* logic_session() const;

private:
    static int on_send(
        abe::logic::session::Session* session,
        const void* data,
        uint32_t size,
        void* user_data);

    uint64_t current_link_id() const;

    abe::adapter::net::TcpLink* link_;
    abe::logic::session::Session* logic_session_;
    uint64_t link_id_;
};

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GATEWAY_SESSION_H */
