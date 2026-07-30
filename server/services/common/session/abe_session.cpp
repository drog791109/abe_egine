#include "abe_session.h"

#include "protocol.pb.h"

#include <stddef.h>

namespace abe {
namespace service {
namespace session {

namespace proto = ::abe::proto::client;

SessionHandlerEntry::SessionHandlerEntry()
    : handler(NULL),
      user_data(NULL)
{
}

SessionHandlerTable::SessionHandlerTable()
    : handlers(NULL),
      handler_count(0u),
      default_handler()
{
}

Session::Session()
    : handler_table_(NULL),
      send_handler_(NULL),
      send_user_data_(NULL),
      active_(0)
{
    reset();
}

Session::~Session()
{
}

int Session::open(uint64_t server_id, const SessionOpenRequest& request)
{
    int rc;

    if (server_id == 0u || request.link_id == 0u || request.conn_id == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
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

    rc = on_open(request);
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

    info_.state = SESSION_STATE_CLOSED;
    info_.close_reason = reason;
    info_.last_recv_ms = now_ms;
    on_close(reason, now_ms);
    active_ = 0;
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
    handler_table_ = NULL;
    send_handler_ = NULL;
    send_user_data_ = NULL;
    on_reset();
}

int Session::set_uid(uint64_t uid)
{
    if (!active_) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }
    if (uid == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    info_.uid = uid;
    if (info_.state == SESSION_STATE_CONNECTED) {
        info_.state = SESSION_STATE_AUTHENTICATED;
    }
    return proto::ERROR_CODE_OK;
}

int Session::enter_room(uint64_t room_id)
{
    if (!active_) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }
    if (room_id == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    info_.room_id = room_id;
    if (info_.state != SESSION_STATE_IN_GAME) {
        info_.state = SESSION_STATE_IN_ROOM;
    }
    return proto::ERROR_CODE_OK;
}

int Session::enter_game()
{
    if (!active_) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }
    if (info_.room_id == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    info_.state = SESSION_STATE_IN_GAME;
    return proto::ERROR_CODE_OK;
}

int Session::leave_game()
{
    if (!active_) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }
    if (info_.state == SESSION_STATE_IN_GAME) {
        info_.state = info_.room_id == 0u ? SESSION_STATE_AUTHENTICATED : SESSION_STATE_IN_ROOM;
    }
    return proto::ERROR_CODE_OK;
}

int Session::leave_room()
{
    if (!active_) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }

    info_.room_id = 0u;
    if (info_.uid != 0u) {
        info_.state = SESSION_STATE_AUTHENTICATED;
    } else {
        info_.state = SESSION_STATE_CONNECTED;
    }
    return proto::ERROR_CODE_OK;
}

int Session::set_message_handler(
    SessionHandlerTable* table,
    uint32_t message_id,
    SessionMessageHandler handler,
    void* user_data)
{
    if (table == NULL ||
        table->handlers == NULL ||
        table->handler_count == 0u ||
        message_id == 0u ||
        message_id >= table->handler_count ||
        handler == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    table->handlers[message_id].handler = handler;
    table->handlers[message_id].user_data = user_data;
    return proto::ERROR_CODE_OK;
}

int Session::clear_message_handler(SessionHandlerTable* table, uint32_t message_id)
{
    if (table == NULL ||
        table->handlers == NULL ||
        table->handler_count == 0u ||
        message_id == 0u ||
        message_id >= table->handler_count) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    if (table->handlers[message_id].handler == NULL) {
        return proto::ERROR_CODE_SESSION_NOT_FOUND;
    }
    table->handlers[message_id] = SessionHandlerEntry();
    return proto::ERROR_CODE_OK;
}

void Session::clear_message_handlers(SessionHandlerTable* table)
{
    uint32_t index;

    if (table == NULL || table->handlers == NULL) {
        return;
    }

    index = 0u;
    while (index < table->handler_count) {
        table->handlers[index] = SessionHandlerEntry();
        ++index;
    }
}

int Session::set_default_message_handler(
    SessionHandlerTable* table,
    SessionMessageHandler handler,
    void* user_data)
{
    if (table == NULL || handler == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    table->default_handler.handler = handler;
    table->default_handler.user_data = user_data;
    return proto::ERROR_CODE_OK;
}

void Session::clear_default_message_handler(SessionHandlerTable* table)
{
    if (table != NULL) {
        table->default_handler = SessionHandlerEntry();
    }
}

void Session::init_handler_table(
    SessionHandlerTable* table,
    SessionHandlerEntry* handlers,
    uint32_t handler_count)
{
    if (table == NULL) {
        return;
    }

    table->handlers = handlers;
    table->handler_count = handler_count;
    table->default_handler = SessionHandlerEntry();
    clear_message_handlers(table);
}

int Session::handle_message(uint32_t message_id, const void* data, uint32_t size, uint64_t now_ms)
{
    SessionMessage message;
    SessionHandlerEntry* entry;

    if (!active_) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }
    if (message_id == 0u ||
        handler_table_ == NULL ||
        handler_table_->handlers == NULL ||
        message_id >= handler_table_->handler_count ||
        (data == NULL && size != 0u)) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    info_.last_recv_ms = now_ms;
    message.message_id = message_id;
    message.data = data;
    message.size = size;
    message.recv_time_ms = now_ms;

    entry = find_handler(handler_table_, message_id);
    if (entry == NULL && handler_table_->default_handler.handler != NULL) {
        entry = &handler_table_->default_handler;
    }

    if (entry == NULL || entry->handler == NULL) {
        return proto::ERROR_CODE_SESSION_NO_HANDLER;
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
        return proto::ERROR_CODE_SESSION_CLOSED;
    }
    if (data == NULL && size != 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    result = on_send(data, size);
    if (result == proto::ERROR_CODE_OK) {
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

void Session::mark_received(uint64_t now_ms)
{
    info_.last_recv_ms = now_ms;
}

void Session::set_handler_table(SessionHandlerTable* table)
{
    handler_table_ = table;
}

SessionHandlerEntry* Session::find_handler(SessionHandlerTable* table, uint32_t message_id)
{
    if (table == NULL ||
        table->handlers == NULL ||
        message_id == 0u ||
        message_id >= table->handler_count) {
        return NULL;
    }

    if (table->handlers[message_id].handler == NULL) {
        return NULL;
    }
    return &table->handlers[message_id];
}

int Session::on_open(const SessionOpenRequest& request)
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

int Session::on_send(const void* data, uint32_t size)
{
    if (send_handler_ == NULL) {
        return proto::ERROR_CODE_SESSION_NO_HANDLER;
    }
    return send_handler_(this, data, size, send_user_data_);
}

} /* namespace session */
} /* namespace service */
} /* namespace abe */
