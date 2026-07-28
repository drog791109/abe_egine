#ifndef ABE_SERVICE_GATEHUB_SERVER_H
#define ABE_SERVICE_GATEHUB_SERVER_H

#include "abe_service_runtime.h"
#include "abe_snowflake.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace gatehub {

enum {
    ABE_GATEHUB_SESSION_TOKEN_CAPACITY = 65u
};

enum GateHubSessionState {
    GATEHUB_SESSION_OFFLINE = 0,
    GATEHUB_SESSION_ONLINE = 1,
    GATEHUB_SESSION_RECONNECTING = 2
};

struct GateHubConfig {
    uint32_t max_sessions;
    uint32_t allow_reconnect;
    uint32_t replace_duplicate_login;
    uint64_t reconnect_grace_ms;
    uint64_t session_ttl_ms;
};

struct GateHubOpenRequest {
    uint64_t account_id;
    uint64_t uid;
    uint64_t gateway_id;
    uint64_t connection_id;
    const char* session_token;
    uint64_t now_ms;
};

struct GateHubSessionInfo {
    uint64_t session_id;
    uint64_t account_id;
    uint64_t uid;
    uint64_t gateway_id;
    uint64_t connection_id;
    uint64_t expire_time_ms;
    uint64_t reconnect_deadline_ms;
    GateHubSessionState state;
    char session_token[ABE_GATEHUB_SESSION_TOKEN_CAPACITY];
};

struct GateHubOpenResult {
    GateHubSessionInfo session;
    uint64_t replaced_gateway_id;
    uint64_t replaced_connection_id;
    uint32_t reconnected;
    uint32_t replaced;
};

void set_gatehub_defaults(GateHubConfig* config);

class GateHubRegistry {
public:
    GateHubRegistry();
    ~GateHubRegistry();

    int init(const GateHubConfig& config, abe_snowflake_t* id_generator);
    void close();

    int open_session(
        const GateHubOpenRequest& request,
        GateHubOpenResult* out_result);
    int disconnect(
        uint64_t gateway_id,
        uint64_t connection_id,
        uint64_t now_ms);
    int close_session(uint64_t uid, const char* session_token);
    int update(uint64_t now_ms, uint32_t* out_closed_count);
    int find_session(uint64_t uid, GateHubSessionInfo* out_info) const;

    uint32_t active_count() const;
    int initialized() const;

private:
    GateHubRegistry(const GateHubRegistry&);
    GateHubRegistry& operator=(const GateHubRegistry&);

    struct SessionSlot;

    SessionSlot* find_slot_by_uid(uint64_t uid);
    const SessionSlot* find_slot_by_uid(uint64_t uid) const;
    SessionSlot* find_slot_by_connection(
        uint64_t gateway_id,
        uint64_t connection_id);
    SessionSlot* find_free_slot();
    void reset_slot(SessionSlot* slot);
    int start_session(
        SessionSlot* slot,
        const GateHubOpenRequest& request,
        GateHubOpenResult* out_result);

    GateHubConfig config_;
    abe_snowflake_t* id_generator_;
    SessionSlot* slots_;
    uint32_t active_count_;
    int initialized_;
};

class GateHubServer : public abe::service::common::Service {
public:
    GateHubServer();

    virtual const char* name() const;
    virtual const char* config_path() const;
    virtual void defaults();
    virtual int load_config(const abe_config_t* config);
    virtual int init(abe::service::common::Context& context);
    virtual int update(uint64_t now_ms);
    virtual void close(uint64_t now_ms);

    GateHubRegistry* registry();
    const GateHubRegistry* registry() const;
    int initialized() const;

private:
    GateHubServer(const GateHubServer&);
    GateHubServer& operator=(const GateHubServer&);

    GateHubConfig config_;
    GateHubRegistry registry_;
    int initialized_;
};

} /* namespace gatehub */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GATEHUB_SERVER_H */
