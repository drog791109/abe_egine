#ifndef ABE_SERVICE_LOGIN_SERVER_H
#define ABE_SERVICE_LOGIN_SERVER_H

#include "abe_gatehub_server.h"
#include "abe_login_process.h"
#include "abe_service_runtime.h"
#include "protocol.pb.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace login {

struct LoginServerConfig {
    LoginProcessConfig accounts;
    abe::service::gatehub::GateHubConfig sessions;
    const char* default_region;
};

void set_login_server_defaults(LoginServerConfig* config);

class LoginServer : public abe::service::common::Service {
public:
    LoginServer();

    virtual const char* name() const;
    virtual const char* config_path() const;
    virtual void defaults();
    virtual int load_config(const abe_config_t* config);
    virtual int init(abe::service::common::Context& context);
    virtual int update(uint64_t now_ms);
    virtual void close(uint64_t now_ms);

    int handle_login(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_LOGIN_REQ& request,
        abe::proto::client::PB_SC_LOGIN_RESP* out_response,
        uint64_t now_ms);
    int handle_create_charactor(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_CREATE_CHARACTOR& request,
        abe::proto::client::PB_SC_CREATE_CHARACTOR* out_response,
        uint64_t now_ms);
    int handle_select_charactor(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_SELECT_CHARACTOR& request,
        abe::proto::client::PB_SC_SELECT_CHARACTOR* out_response,
        uint64_t now_ms);
    int handle_delete_charactor(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_DELETE_CHARACTOR& request,
        abe::proto::client::PB_SC_DELETE_CHARACTOR* out_response,
        uint64_t now_ms);
    int handle_disconnect(
        uint64_t gateway_id,
        uint64_t connection_id,
        uint64_t now_ms);

    LoginProcess* account_process();
    const LoginProcess* account_process() const;
    abe::service::gatehub::GateHubRegistry* session_registry();
    const abe::service::gatehub::GateHubRegistry* session_registry() const;
    int initialized() const;

private:
    LoginServer(const LoginServer&);
    LoginServer& operator=(const LoginServer&);

    LoginServerConfig config_;
    LoginProcess accounts_;
    abe::service::gatehub::GateHubRegistry sessions_;
    int initialized_;
};

} /* namespace login */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_LOGIN_SERVER_H */
