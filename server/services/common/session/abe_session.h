#ifndef ABE_SERVICE_SESSION_H
#define ABE_SERVICE_SESSION_H

#include <stdint.h>

namespace abe {
namespace service {
namespace session {

enum SessionState {
    SESSION_STATE_CLOSED = 0,
    SESSION_STATE_CONNECTED = 1,
    SESSION_STATE_AUTHENTICATED = 2,
    SESSION_STATE_IN_ROOM = 3,
    SESSION_STATE_IN_GAME = 4
};

enum {
    SESSION_MAX_MESSAGE_ID = 65535u
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

struct SessionHandlerEntry {
    SessionHandlerEntry();

    SessionMessageHandler handler;
    void* user_data;
};

struct SessionHandlerTable {
    SessionHandlerTable();

    SessionHandlerEntry* handlers;
    uint32_t handler_count;
    SessionHandlerEntry default_handler;
};

class Session {
public:
    Session();
    virtual ~Session();

    int open(uint64_t server_id, const SessionOpenRequest& request);
    void close(uint32_t reason, uint64_t now_ms);
    void reset();

    int set_uid(uint64_t uid);
    int enter_room(uint64_t room_id);
    int enter_game();
    int leave_game();
    int leave_room();

    static int set_message_handler(
        SessionHandlerTable* table,
        uint32_t message_id,
        SessionMessageHandler handler,
        void* user_data);
    static int clear_message_handler(SessionHandlerTable* table, uint32_t message_id);
    static void clear_message_handlers(SessionHandlerTable* table);
    static int set_default_message_handler(
        SessionHandlerTable* table,
        SessionMessageHandler handler,
        void* user_data);
    static void clear_default_message_handler(SessionHandlerTable* table);
    static void init_handler_table(
        SessionHandlerTable* table,
        SessionHandlerEntry* handlers,
        uint32_t handler_count);
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

protected:
    virtual int on_open(const SessionOpenRequest& request);
    virtual void on_close(uint32_t reason, uint64_t now_ms);
    virtual void on_reset();
    virtual int on_send(const void* data, uint32_t size);
    void mark_received(uint64_t now_ms);
    void set_handler_table(SessionHandlerTable* table);

private:
    static SessionHandlerEntry* find_handler(SessionHandlerTable* table, uint32_t message_id);

    SessionHandlerTable* handler_table_;
    SessionSendHandler send_handler_;
    void* send_user_data_;
    SessionInfo info_;
    int active_;
};

} /* namespace session */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_SESSION_H */
