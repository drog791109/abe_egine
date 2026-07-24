#ifndef ABE_SERVICE_GATEWAY_APP_H
#define ABE_SERVICE_GATEWAY_APP_H

#include "abe_gateway_service.h"
#include "abe_net_server.h"
#include "abe_session.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace gateway {

enum {
    ABE_GATEWAY_MAX_CLIENTS = 1024u
};

struct GatewayMainConfig {
    const char* host;
    uint16_t port;
    uint32_t max_clients;
    int backlog;
    uint32_t max_packet_size;
    uint32_t tick_ms;
    uint64_t server_id;
    uint64_t idle_timeout_ms;
};

void gateway_main_config_set_defaults(GatewayMainConfig* config);

class GatewayApp {
public:
    GatewayApp();

    int init(const GatewayMainConfig& config);
    int run();
    void close();

private:
    GatewayApp(const GatewayApp&);
    GatewayApp& operator=(const GatewayApp&);

    static int on_default_message(
        abe::logic::session::Session* current,
        const abe::logic::session::SessionMessage* message,
        void* user_data);

    int init_service();
    int init_tcp_server();
    int update_once();

    GatewayMainConfig config_;
    abe::adapter::net::Loop loop_;
    abe::adapter::net::TcpServer tcp_server_;
    GatewayService service_;
    abe::logic::session::Session session_slots_[ABE_GATEWAY_MAX_CLIENTS];
    abe::adapter::net::TcpLink link_slots_[ABE_GATEWAY_MAX_CLIENTS];
    int loop_ready_;
    int service_ready_;
    int tcp_ready_;
};

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GATEWAY_APP_H */
