#ifndef ABE_SESSION_MANAGER_H
#define ABE_SESSION_MANAGER_H

#include <stdint.h>

#include <map>
#include <string>

namespace abe {
namespace logic {
namespace session {

enum SessionStatus {
    SESSION_STATUS_OK = 0,
    SESSION_STATUS_INVALID_ARG = -1,
    SESSION_STATUS_NOT_FOUND = -2,
    SESSION_STATUS_TOKEN_MISMATCH = -3,
    SESSION_STATUS_EXPIRED = -4,
    SESSION_STATUS_NO_MEMORY = -5
};

enum SessionState {
    SESSION_STATE_OFFLINE = 0,
    SESSION_STATE_ONLINE = 1,
    SESSION_STATE_RECONNECTING = 2
};

struct SessionConfig {
    uint64_t session_ttl_ms;
    uint64_t reconnect_ttl_ms;
};

struct LoginRequest {
    uint64_t uid;
    uint64_t gateway_id;
    uint64_t conn_id;
    const char* login_token;
    const char* device_id;
    uint64_t now_ms;
};

struct SessionInfo {
    uint64_t uid;
    uint64_t session_id;
    uint64_t gateway_id;
    uint64_t conn_id;
    uint64_t room_id;
    SessionState state;
    uint64_t login_time_ms;
    uint64_t expire_time_ms;
    uint64_t last_heartbeat_ms;
    uint64_t reconnect_deadline_ms;
    std::string session_token;
    std::string login_token;
    std::string device_id;
};

class SessionManager {
public:
    SessionManager();

    void init(const SessionConfig& config);
    void clear();

    int login(const LoginRequest& request, SessionInfo* out_session, SessionInfo* out_kicked);
    int heartbeat(uint64_t uid, const char* session_token, uint64_t now_ms, SessionInfo* out_session);
    int disconnect(uint64_t uid, uint64_t conn_id, uint64_t now_ms, SessionInfo* out_session);
    int reconnect(const LoginRequest& request, SessionInfo* out_session);
    int kick(uint64_t uid, uint64_t now_ms, SessionInfo* out_session);

    int bind_room(uint64_t uid, uint64_t room_id, SessionInfo* out_session);
    int clear_room(uint64_t uid, uint64_t room_id, SessionInfo* out_session);
    int find(uint64_t uid, SessionInfo* out_session) const;
    int remove_expired(uint64_t now_ms);

    uint32_t count() const;

private:
    struct Record {
        uint64_t uid;
        uint64_t session_id;
        uint64_t gateway_id;
        uint64_t conn_id;
        uint64_t room_id;
        SessionState state;
        uint64_t login_time_ms;
        uint64_t expire_time_ms;
        uint64_t last_heartbeat_ms;
        uint64_t reconnect_deadline_ms;
        std::string session_token;
        std::string login_token;
        std::string device_id;
    };

    typedef std::map<uint64_t, Record> SessionMap;

    int validate_token(const Record& record, const char* session_token, uint64_t now_ms) const;
    void fill_info(const Record& record, SessionInfo* out_session) const;
    void make_session_token(const LoginRequest& request, uint64_t session_id, std::string* out_token) const;
    uint64_t next_session_id();

    SessionConfig config_;
    uint64_t next_session_id_;
    SessionMap sessions_;
};

} /* namespace session */
} /* namespace logic */
} /* namespace abe */

#endif /* ABE_SESSION_MANAGER_H */
