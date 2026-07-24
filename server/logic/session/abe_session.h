#ifndef ABE_LOGIC_SESSION_H
#define ABE_LOGIC_SESSION_H

#include <stdint.h>

namespace abe {
namespace logic {
namespace session {

enum SessionStatus {
    SESSION_STATUS_OK = 0,
    SESSION_STATUS_INVALID_ARG = -1,
    SESSION_STATUS_NOT_FOUND = -2,
    SESSION_STATUS_ALREADY_EXISTS = -3,
    SESSION_STATUS_NO_SLOT = -4,
    SESSION_STATUS_NO_HANDLER = -5,
    SESSION_STATUS_CLOSED = -6
};

enum SessionState {
    SESSION_STATE_CLOSED = 0,
    SESSION_STATE_CONNECTED = 1,
    SESSION_STATE_AUTHENTICATED = 2,
    SESSION_STATE_IN_ROOM = 3,
    SESSION_STATE_IN_GAME = 4
};

enum {
    SESSION_MAX_MESSAGE_HANDLERS = 64u
};

class Session;

struct SessionOpenRequest {
    uint64_t link_id;
    uint64_t conn_id;
    uint64_t now_ms;
    void* link_user_data;
};

struct SessionInfo {
    uint64_t link_id;
    uint64_t server_id;
    uint64_t conn_id;
    uint64_t uid;
    uint64_t room_id;
    SessionState state;
    uint64_t connected_at_ms;
    uint64_t last_recv_ms;
    uint64_t last_send_ms;
    uint32_t close_reason;
    void* link_user_data;
};

struct SessionMessage {
    uint32_t message_id;
    const void* data;
    uint32_t size;
    uint64_t recv_time_ms;
};

typedef int (*SessionMessageHandler)(
    Session* session,
    const SessionMessage* message,
    void* user_data);

typedef int (*SessionSendHandler)(
    Session* session,
    const void* data,
    uint32_t size,
    void* user_data);

class Session {
public:
    Session();

    int open(uint64_t server_id, const SessionOpenRequest& request);
    void close(uint32_t reason, uint64_t now_ms);
    void reset();

    int set_uid(uint64_t uid);
    int enter_room(uint64_t room_id);
    int enter_game();
    int leave_game();
    int leave_room();

    int set_message_handler(
        uint32_t message_id,
        SessionMessageHandler handler,
        void* user_data);
    int clear_message_handler(uint32_t message_id);
    void clear_message_handlers();
    int set_default_message_handler(SessionMessageHandler handler, void* user_data);
    void clear_default_message_handler();
    int handle_message(uint32_t message_id, const void* data, uint32_t size, uint64_t now_ms);

    void set_send_handler(SessionSendHandler handler, void* user_data);
    int send(const void* data, uint32_t size, uint64_t now_ms);

    int active() const;
    uint64_t link_id() const;
    uint64_t server_id() const;
    uint64_t conn_id() const;
    uint64_t uid() const;
    uint64_t room_id() const;
    SessionState state() const;
    uint64_t connected_at_ms() const;
    uint64_t last_recv_ms() const;
    uint64_t last_send_ms() const;
    uint32_t close_reason() const;
    void* link_user_data() const;

    void fill_info(SessionInfo* out_info) const;

private:
    struct HandlerEntry {
        HandlerEntry();

        uint32_t message_id;
        SessionMessageHandler handler;
        void* user_data;
    };

    HandlerEntry* find_handler(uint32_t message_id);

    HandlerEntry default_handler_;
    SessionSendHandler send_handler_;
    void* send_user_data_;
    HandlerEntry handlers_[SESSION_MAX_MESSAGE_HANDLERS];
    uint32_t handler_count_;
    SessionInfo info_;
    int active_;
};

} /* namespace session */
} /* namespace logic */
} /* namespace abe */

#endif /* ABE_LOGIC_SESSION_H */
