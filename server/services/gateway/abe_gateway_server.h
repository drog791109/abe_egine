#ifndef ABE_SERVICE_GATEWAY_SERVER_H
#define ABE_SERVICE_GATEWAY_SERVER_H

#include "abe_gateway_session.h"
#include "abe_net_server.h"
#include "abe_service_runtime.h"
#include "abe_session_server.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace gateway {

enum {
    ABE_GATEWAY_MAX_CLIENTS = 1024u
};

struct GatewayServerConfig {
    const char* host;
    uint32_t port;
    uint32_t max_clients;
    uint32_t backlog;
    uint32_t max_packet_size;
    uint64_t server_id;
    uint64_t idle_timeout_ms;
};

void set_gateway_defaults(GatewayServerConfig* config);

class GatewayServer : public abe::service::common::Service {
public:
    GatewayServer();

    virtual const char* name() const;
    virtual const char* config_path() const;
    virtual void defaults();
    virtual int load_config(const abe_config_t* config);
    virtual int init(abe::service::common::Context& context);
    virtual int update(uint64_t now_ms);
    virtual void close(uint64_t now_ms);

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
    GatewayServer(const GatewayServer&);
    GatewayServer& operator=(const GatewayServer&);

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

    int init_sessions();
    int listen(abe::adapter::net::Loop* loop);
    int open_session_for_link(abe::adapter::net::TcpLink* link, uint64_t now_ms);
    int close_session_for_link(
        abe::adapter::net::TcpLink* link,
        uint32_t reason,
        uint64_t now_ms);

    int dispatch(
        abe::adapter::net::TcpLink* link,
        const void* packet,
        uint32_t packet_size,
        uint64_t now_ms);

    uint64_t link_id(abe::adapter::net::TcpLink* link) const;
    GatewaySession* find_session(uint64_t link_id);

    GatewayServerConfig config_;
    abe::adapter::net::TcpServer tcp_;
    abe::logic::session::SessionServer sessions_;
    GatewaySession session_slots_[ABE_GATEWAY_MAX_CLIENTS];
    abe::adapter::net::TcpLink link_slots_[ABE_GATEWAY_MAX_CLIENTS];
    int session_ready_;
    int tcp_ready_;
};

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GATEWAY_SERVER_H */
