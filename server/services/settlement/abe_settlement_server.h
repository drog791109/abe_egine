#ifndef ABE_SERVICE_SETTLEMENT_SERVER_H
#define ABE_SERVICE_SETTLEMENT_SERVER_H

#include "abe_service_runtime.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace settlement {

struct SettlementServerConfig {
    uint32_t max_pending_events;
    uint64_t server_id;
    uint64_t idle_timeout_ms;
};

void set_settlement_server_defaults(SettlementServerConfig* config);

class SettlementServer : public abe::service::common::Service {
public:
    SettlementServer();

    virtual const char* name() const;
    virtual const char* config_path() const;
    virtual void defaults();
    virtual int load_config(const abe_config_t* config);
    virtual int init(abe::service::common::Context& context);
    virtual int process_message(const abe::service::common::Message& message);
    virtual int update(uint64_t now_ms);
    virtual void close(uint64_t now_ms);

    int initialized() const;

private:
    SettlementServer(const SettlementServer&);
    SettlementServer& operator=(const SettlementServer&);

    SettlementServerConfig config_;
    int initialized_;
};

} /* namespace settlement */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_SETTLEMENT_SERVER_H */
