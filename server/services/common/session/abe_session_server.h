#ifndef ABE_SERVICE_SESSION_SERVER_H
#define ABE_SERVICE_SESSION_SERVER_H

#include "abe_session.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace session {

struct SessionServerConfig {
    uint64_t server_id;
    Session* sessions;
    uint32_t session_count;
    /*
     * Size in bytes of one session slot. Leave as 0 for plain Session slots.
     * Set to sizeof(DerivedSession) when a service provides derived sessions.
     */
    uint32_t session_size;
    uint64_t idle_timeout_ms;
};

class SessionServer {
public:
    SessionServer();

    int init(const SessionServerConfig& config);
    void close(uint64_t now_ms);
    int update(uint64_t now_ms, uint32_t* out_closed_count);

    Session* open_session(const SessionOpenRequest& request, int* out_status);
    int close_session(uint64_t link_id, uint32_t reason, uint64_t now_ms);
    int handle_message(
        uint64_t link_id,
        uint32_t message_id,
        const void* data,
        uint32_t size,
        uint64_t now_ms);

    Session* find_session(uint64_t link_id);
    const Session* find_session(uint64_t link_id) const;
    Session* find_session_by_uid(uint64_t uid);
    const Session* find_session_by_uid(uint64_t uid) const;

    uint64_t server_id() const;
    uint32_t active_count() const;
    uint32_t capacity() const;
    int initialized() const;

private:
    SessionServer(const SessionServer&);
    SessionServer& operator=(const SessionServer&);

    Session* session_at(uint32_t index);
    const Session* session_at(uint32_t index) const;
    Session* find_free_session();

    Session* sessions_;
    uint32_t session_count_;
    uint32_t session_size_;
    uint32_t active_count_;
    uint64_t server_id_;
    uint64_t idle_timeout_ms_;
    int initialized_;
};

} /* namespace session */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_SESSION_SERVER_H */
