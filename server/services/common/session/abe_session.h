#ifndef ABE_SERVICE_SESSION_H
#define ABE_SERVICE_SESSION_H

#include <stdint.h>

namespace abe {
namespace service {
namespace session {

class Session;

typedef void (*SessionAuthChangeCallback)(
    Session* session,
    uint64_t old_user_id,
    uint64_t new_user_id,
    void* user_data);

struct SessionOpenRequest {
    uint64_t conn_id;
    uint64_t now_ms;
    void* user_data;
};

class Session {
public:
    Session();
    virtual ~Session();

    int open(uint64_t server_id, const SessionOpenRequest& request);
    void close(uint32_t reason, uint64_t now_ms);
    void reset();

    int receive(const void* data, uint32_t size, uint64_t now_ms);
    int send(const void* data, uint32_t size, uint64_t now_ms);

    int mark_authenticated(uint64_t user_id);
    void clear_authenticated();
    void set_auth_change_callback(
        SessionAuthChangeCallback callback,
        void* user_data);
    virtual void on_frame_update(uint64_t delta_ms);
    bool is_timeout(uint64_t now_ms, uint64_t timeout_ms) const;

    bool active() const;
    uint64_t server_id() const;
    uint64_t conn_id() const;
    uint64_t user_id() const;
    bool authenticated() const;
    uint64_t last_active_ms() const;
    uint32_t close_reason() const;
    void* user_data() const;

protected:
    virtual int on_connect(const SessionOpenRequest& request);
    virtual void on_close(uint32_t reason, uint64_t now_ms);
    virtual void on_reset();
    virtual int on_message(const void* data, uint32_t size, uint64_t now_ms) = 0;
    virtual int send_packet(const void* data, uint32_t size) = 0;

    void update_activity(uint64_t now_ms);

private:
    void notify_auth_changed(uint64_t old_user_id, uint64_t new_user_id);

    uint64_t server_id_;
    uint64_t conn_id_;
    uint64_t user_id_;
    uint64_t last_active_ms_;
    uint32_t close_reason_;
    void* user_data_;
    SessionAuthChangeCallback auth_change_callback_;
    void* auth_change_user_data_;
    bool authenticated_;
    bool active_;
};

} /* namespace session */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_SESSION_H */
