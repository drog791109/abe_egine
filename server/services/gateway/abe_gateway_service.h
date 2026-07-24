#ifndef ABE_SERVICE_GATEWAY_SERVICE_H
#define ABE_SERVICE_GATEWAY_SERVICE_H

#include "abe_gateway_session.h"
#include "abe_net_server.h"
#include "abe_session_server.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace gateway {

enum {
    GATEWAY_MAX_PROTOCOL_HANDLERS = 64u,
    GATEWAY_MAX_LINK_SESSIONS = 1024u
};

/*
 * Packet decoders should be implemented from the protocol source in
 * server/share/proto. The gateway needs the fixed header msg id and the body
 * slice before dispatching to a session handler.
 */
typedef int (*GatewayPacketDecodeFn)(
    const void* packet,
    uint32_t packet_size,
    uint32_t* out_msg_id,
    const void** out_payload,
    uint32_t* out_payload_size,
    void* user_data);

struct GatewayHandlerEntry {
    uint32_t msg_id;
    abe::logic::session::SessionMessageHandler handler;
    void* user_data;
};

struct GatewayServiceConfig {
    abe::logic::session::SessionServerConfig session_config;
    GatewayPacketDecodeFn decode_packet;
    void* decode_user_data;
    const GatewayHandlerEntry* handlers;
    uint32_t handler_count;
    abe::logic::session::SessionMessageHandler default_handler;
    void* default_handler_user_data;
};

class GatewayService {
public:
    GatewayService();

    int init(const GatewayServiceConfig& config);
    void close(uint64_t now_ms);
    int update(uint64_t now_ms);

    void fill_tcp_callbacks(abe::adapter::net::TcpServerCallbacks* out_callbacks);

    int on_connect(abe::adapter::net::TcpLink* link, uint64_t now_ms);
    int on_receive(
        abe::adapter::net::TcpLink* link,
        const void* packet,
        uint32_t packet_size,
        uint64_t now_ms);
    int on_disconnect(
        abe::adapter::net::TcpLink* link,
        int error_code,
        uint64_t now_ms);

    abe::logic::session::SessionServer* session_server();
    const abe::logic::session::SessionServer* session_server() const;
    int initialized() const;

private:
    static void tcp_on_connect(
        abe::adapter::net::TcpServer* server,
        abe::adapter::net::TcpLink* link,
        void* user_data);
    static void tcp_on_receive(
        abe::adapter::net::TcpLink* link,
        const void* packet,
        uint32_t packet_size,
        void* user_data);
    static void tcp_on_disconnect(
        abe::adapter::net::TcpLink* link,
        int error_code,
        void* user_data);

    int install_handlers(abe::logic::session::Session* session);
    int open_session_for_link(abe::adapter::net::TcpLink* link, uint64_t now_ms);
    int close_session_for_link(
        abe::adapter::net::TcpLink* link,
        uint32_t reason,
        uint64_t now_ms);
    int dispatch_packet(
        abe::adapter::net::TcpLink* link,
        const void* packet,
        uint32_t packet_size,
        uint64_t now_ms);
    uint64_t link_id_from_link(abe::adapter::net::TcpLink* link) const;
    GatewaySession* find_free_gateway_session();
    GatewaySession* find_gateway_session(uint64_t link_id);
    void clear_gateway_sessions();
    void sync_gateway_sessions();

    GatewayServiceConfig config_;
    GatewayHandlerEntry handlers_[GATEWAY_MAX_PROTOCOL_HANDLERS];
    GatewaySession gateway_sessions_[GATEWAY_MAX_LINK_SESSIONS];
    uint32_t handler_count_;
    int initialized_;
    abe::logic::session::SessionServer session_server_;
};

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GATEWAY_SERVICE_H */
