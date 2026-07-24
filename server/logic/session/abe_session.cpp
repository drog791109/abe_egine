#include "abe_session.h"

#include <stddef.h>

namespace abe {
namespace logic {
namespace session {

Session::HandlerEntry::HandlerEntry()
    : message_id(0u),
      handler(NULL),
      user_data(NULL)
{
}

Session::Session()
    : send_handler_(NULL),
      send_user_data_(NULL),
      handler_count_(0u),
      active_(0)
{
    reset();
}

int Session::open(uint64_t server_id, const SessionOpenRequest& request)
{
    if (server_id == 0u || request.link_id == 0u || request.conn_id == 0u) {
        return SESSION_STATUS_INVALID_ARG;
    }

    reset();
    active_ = 1;
    info_.link_id = request.link_id;
    info_.server_id = server_id;
    info_.conn_id = request.conn_id;
    info_.state = SESSION_STATE_CONNECTED;
    info_.connected_at_ms = request.now_ms;
    info_.last_recv_ms = request.now_ms;
    info_.last_send_ms = request.now_ms;
    info_.link_user_data = request.link_user_data;
    return SESSION_STATUS_OK;
}

void Session::close(uint32_t reason, uint64_t now_ms)
{
    if (!active_) {
        return;
    }

    info_.state = SESSION_STATE_CLOSED;
    info_.close_reason = reason;
    info_.last_recv_ms = now_ms;
    active_ = 0;
    handler_count_ = 0u;
    default_handler_ = HandlerEntry();
    send_handler_ = NULL;
    send_user_data_ = NULL;
}

void Session::reset()
{
    info_.link_id = 0u;
    info_.server_id = 0u;
    info_.conn_id = 0u;
    info_.uid = 0u;
    info_.room_id = 0u;
    info_.state = SESSION_STATE_CLOSED;
    info_.connected_at_ms = 0u;
    info_.last_recv_ms = 0u;
    info_.last_send_ms = 0u;
    info_.close_reason = 0u;
    info_.link_user_data = NULL;
    active_ = 0;
    handler_count_ = 0u;
    default_handler_ = HandlerEntry();
    send_handler_ = NULL;
    send_user_data_ = NULL;
}

int Session::set_uid(uint64_t uid)
{
    if (!active_) {
        return SESSION_STATUS_CLOSED;
    }
    if (uid == 0u) {
        return SESSION_STATUS_INVALID_ARG;
    }

    info_.uid = uid;
    if (info_.state == SESSION_STATE_CONNECTED) {
        info_.state = SESSION_STATE_AUTHENTICATED;
    }
    return SESSION_STATUS_OK;
}

int Session::enter_room(uint64_t room_id)
{
    if (!active_) {
        return SESSION_STATUS_CLOSED;
    }
    if (room_id == 0u) {
        return SESSION_STATUS_INVALID_ARG;
    }

    info_.room_id = room_id;
    if (info_.state != SESSION_STATE_IN_GAME) {
        info_.state = SESSION_STATE_IN_ROOM;
    }
    return SESSION_STATUS_OK;
}

int Session::enter_game()
{
    if (!active_) {
        return SESSION_STATUS_CLOSED;
    }
    if (info_.room_id == 0u) {
        return SESSION_STATUS_INVALID_ARG;
    }

    info_.state = SESSION_STATE_IN_GAME;
    return SESSION_STATUS_OK;
}

int Session::leave_game()
{
    if (!active_) {
        return SESSION_STATUS_CLOSED;
    }
    if (info_.state == SESSION_STATE_IN_GAME) {
        info_.state = info_.room_id == 0u ? SESSION_STATE_AUTHENTICATED : SESSION_STATE_IN_ROOM;
    }
    return SESSION_STATUS_OK;
}

int Session::leave_room()
{
    if (!active_) {
        return SESSION_STATUS_CLOSED;
    }

    info_.room_id = 0u;
    if (info_.uid != 0u) {
        info_.state = SESSION_STATE_AUTHENTICATED;
    } else {
        info_.state = SESSION_STATE_CONNECTED;
    }
    return SESSION_STATUS_OK;
}

int Session::set_message_handler(
    uint32_t message_id,
    SessionMessageHandler handler,
    void* user_data)
{
    HandlerEntry entry;
    HandlerEntry* current;

    if (message_id == 0u || handler == NULL) {
        return SESSION_STATUS_INVALID_ARG;
    }

    current = find_handler(message_id);
    if (current != NULL) {
        current->handler = handler;
        current->user_data = user_data;
        return SESSION_STATUS_OK;
    }
    if (handler_count_ >= SESSION_MAX_MESSAGE_HANDLERS) {
        return SESSION_STATUS_NO_SLOT;
    }

    entry.message_id = message_id;
    entry.handler = handler;
    entry.user_data = user_data;
    handlers_[handler_count_] = entry;
    ++handler_count_;
    return SESSION_STATUS_OK;
}

int Session::clear_message_handler(uint32_t message_id)
{
    uint32_t index;

    index = 0u;
    while (index < handler_count_) {
        if (handlers_[index].message_id == message_id) {
            if (index + 1u < handler_count_) {
                handlers_[index] = handlers_[handler_count_ - 1u];
            }
            --handler_count_;
            handlers_[handler_count_] = HandlerEntry();
            return SESSION_STATUS_OK;
        }
        ++index;
    }

    return SESSION_STATUS_NOT_FOUND;
}

void Session::clear_message_handlers()
{
    handler_count_ = 0u;
}

int Session::set_default_message_handler(SessionMessageHandler handler, void* user_data)
{
    if (handler == NULL) {
        return SESSION_STATUS_INVALID_ARG;
    }

    default_handler_.handler = handler;
    default_handler_.user_data = user_data;
    return SESSION_STATUS_OK;
}

void Session::clear_default_message_handler()
{
    default_handler_ = HandlerEntry();
}

int Session::handle_message(uint32_t message_id, const void* data, uint32_t size, uint64_t now_ms)
{
    SessionMessage message;
    HandlerEntry* entry;

    if (!active_) {
        return SESSION_STATUS_CLOSED;
    }
    if (message_id == 0u || (data == NULL && size != 0u)) {
        return SESSION_STATUS_INVALID_ARG;
    }

    info_.last_recv_ms = now_ms;
    message.message_id = message_id;
    message.data = data;
    message.size = size;
    message.recv_time_ms = now_ms;

    entry = find_handler(message_id);
    if (entry == NULL && default_handler_.handler != NULL) {
        entry = &default_handler_;
    }

    if (entry == NULL || entry->handler == NULL) {
        return SESSION_STATUS_NO_HANDLER;
    }
    return entry->handler(this, &message, entry->user_data);
}

void Session::set_send_handler(SessionSendHandler handler, void* user_data)
{
    send_handler_ = handler;
    send_user_data_ = user_data;
}

int Session::send(const void* data, uint32_t size, uint64_t now_ms)
{
    int result;

    if (!active_) {
        return SESSION_STATUS_CLOSED;
    }
    if (data == NULL && size != 0u) {
        return SESSION_STATUS_INVALID_ARG;
    }
    if (send_handler_ == NULL) {
        return SESSION_STATUS_NO_HANDLER;
    }

    result = send_handler_(this, data, size, send_user_data_);
    if (result == SESSION_STATUS_OK) {
        info_.last_send_ms = now_ms;
    }
    return result;
}

int Session::active() const
{
    return active_;
}

uint64_t Session::link_id() const
{
    return info_.link_id;
}

uint64_t Session::server_id() const
{
    return info_.server_id;
}

uint64_t Session::conn_id() const
{
    return info_.conn_id;
}

uint64_t Session::uid() const
{
    return info_.uid;
}

uint64_t Session::room_id() const
{
    return info_.room_id;
}

SessionState Session::state() const
{
    return info_.state;
}

uint64_t Session::connected_at_ms() const
{
    return info_.connected_at_ms;
}

uint64_t Session::last_recv_ms() const
{
    return info_.last_recv_ms;
}

uint64_t Session::last_send_ms() const
{
    return info_.last_send_ms;
}

uint32_t Session::close_reason() const
{
    return info_.close_reason;
}

void* Session::link_user_data() const
{
    return info_.link_user_data;
}

void Session::fill_info(SessionInfo* out_info) const
{
    if (out_info == NULL) {
        return;
    }

    *out_info = info_;
}

Session::HandlerEntry* Session::find_handler(uint32_t message_id)
{
    uint32_t index;

    index = 0u;
    while (index < handler_count_) {
        if (handlers_[index].message_id == message_id) {
            return &handlers_[index];
        }
        ++index;
    }
    return NULL;
}

} /* namespace session */
} /* namespace logic */
} /* namespace abe */
