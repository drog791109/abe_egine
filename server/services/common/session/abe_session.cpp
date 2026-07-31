#include "abe_session.h"

#include "protocol.pb.h"

#include <stddef.h>

namespace abe {
namespace service {
namespace session {

namespace proto = ::abe::proto::client;

Session::Session()
    : server_id_(0u),
      conn_id_(0u),
      user_id_(0u),
      last_active_ms_(0u),
      close_reason_(0u),
      user_data_(NULL),
      auth_change_callback_(NULL),
      auth_change_user_data_(NULL),
      authenticated_(false),
      active_(false)
{
}

Session::~Session()
{
}

int Session::open(uint64_t server_id, const SessionOpenRequest& request)
{
    int rc;

    if (server_id == 0u || request.conn_id == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    reset();
    server_id_ = server_id;
    conn_id_ = request.conn_id;
    last_active_ms_ = request.now_ms;
    user_data_ = request.user_data;
    active_ = true;

    rc = on_connect(request);
    if (rc != proto::ERROR_CODE_OK) {
        reset();
        return rc;
    }
    return proto::ERROR_CODE_OK;
}

void Session::close(uint32_t reason, uint64_t now_ms)
{
    if (!active_) {
        return;
    }

    close_reason_ = reason;
    update_activity(now_ms);
    on_close(reason, now_ms);
    active_ = false;
    clear_authenticated();
}

void Session::reset()
{
    clear_authenticated();
    server_id_ = 0u;
    conn_id_ = 0u;
    user_id_ = 0u;
    last_active_ms_ = 0u;
    close_reason_ = 0u;
    user_data_ = NULL;
    authenticated_ = false;
    active_ = false;
    on_reset();
}

int Session::receive(const void* data, uint32_t size, uint64_t now_ms)
{
    if (!active_) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }
    if (data == NULL && size != 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    update_activity(now_ms);
    return on_message(data, size, now_ms);
}

int Session::send(const void* data, uint32_t size, uint64_t now_ms)
{
    int rc;

    if (!active_) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }
    if (data == NULL && size != 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    rc = send_packet(data, size);
    if (rc == proto::ERROR_CODE_OK) {
        update_activity(now_ms);
    }
    return rc;
}

int Session::mark_authenticated(uint64_t user_id)
{
    uint64_t old_user_id;

    if (!active_) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }
    if (user_id == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    old_user_id = user_id_;
    user_id_ = user_id;
    authenticated_ = true;
    notify_auth_changed(old_user_id, user_id_);
    return proto::ERROR_CODE_OK;
}

void Session::clear_authenticated()
{
    uint64_t old_user_id;

    old_user_id = user_id_;
    user_id_ = 0u;
    authenticated_ = false;
    notify_auth_changed(old_user_id, 0u);
}

void Session::set_auth_change_callback(
    SessionAuthChangeCallback callback,
    void* user_data)
{
    auth_change_callback_ = callback;
    auth_change_user_data_ = user_data;
}

void Session::on_frame_update(uint64_t delta_ms)
{
    (void)delta_ms;
}

bool Session::is_timeout(uint64_t now_ms, uint64_t timeout_ms) const
{
    if (!active_ || timeout_ms == 0u || now_ms <= last_active_ms_) {
        return false;
    }
    return now_ms - last_active_ms_ > timeout_ms;
}

bool Session::active() const
{
    return active_;
}

uint64_t Session::server_id() const
{
    return server_id_;
}

uint64_t Session::conn_id() const
{
    return conn_id_;
}

uint64_t Session::user_id() const
{
    return user_id_;
}

bool Session::authenticated() const
{
    return authenticated_;
}

uint64_t Session::last_active_ms() const
{
    return last_active_ms_;
}

uint32_t Session::close_reason() const
{
    return close_reason_;
}

void* Session::user_data() const
{
    return user_data_;
}

void Session::update_activity(uint64_t now_ms)
{
    last_active_ms_ = now_ms;
}

void Session::notify_auth_changed(uint64_t old_user_id, uint64_t new_user_id)
{
    if (old_user_id == new_user_id) {
        return;
    }
    if (auth_change_callback_ != NULL) {
        auth_change_callback_(
            this,
            old_user_id,
            new_user_id,
            auth_change_user_data_);
    }
}

int Session::on_connect(const SessionOpenRequest& request)
{
    (void)request;
    return proto::ERROR_CODE_OK;
}

void Session::on_close(uint32_t reason, uint64_t now_ms)
{
    (void)reason;
    (void)now_ms;
}

void Session::on_reset()
{
}

} /* namespace session */
} /* namespace service */
} /* namespace abe */
