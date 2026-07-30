#ifndef ABE_SERVICE_GATEWAY_SESSION_H
#define ABE_SERVICE_GATEWAY_SESSION_H

#include "abe_net_link.h"
#include "abe_session.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace gateway {

class GatewaySession : public abe::logic::session::Session {
public:
    GatewaySession();

    using abe::logic::session::Session::close;

    void close();

    int active() const;
    uint64_t link_id() const;
    abe::adapter::net::TcpLink* link() const;
    int handle_packet(const void* packet, uint32_t packet_size, uint64_t now_ms);

protected:
    virtual int on_open(const abe::logic::session::SessionOpenRequest& request);
    virtual void on_close(uint32_t reason, uint64_t now_ms);
    virtual void on_reset();

private:
    static int on_send(
        abe::logic::session::Session* session,
        const void* data,
        uint32_t size,
        void* user_data);

    void clear_gateway_state();

    abe::adapter::net::TcpLink* link_;
    uint64_t link_id_;
};

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GATEWAY_SESSION_H */
