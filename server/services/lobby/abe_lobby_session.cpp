#include "abe_lobby_session.h"

#include "abe_log.h"
#include "abe_protocol.h"
#include "protocol.pb.h"

namespace abe {
namespace service {
namespace lobby {

namespace proto = ::abe::proto::client;
namespace service_session = ::abe::service::session;

std::unordered_map<uint32_t, LobbySession::MessageHandler> LobbySession::handlers_;
std::once_flag LobbySession::handlers_once_;

void LobbySession::register_handlers()
{
    /* Register lobby message handlers here.
     * e.g. handlers_[proto::CS_ENTER_LOBBY_REQ] = &LobbySession::handle_enter_lobby_req;
     */
    handlers_[proto::CS_ENTER_LOBBY_REQ]        = &LobbySession::handle_enter_lobby_req;
    handlers_[proto::CS_ROOM_LIST_REQ]          = &LobbySession::handle_room_list_req;
    handlers_[proto::CS_CREATE_ROOM_REQ]        = &LobbySession::handle_create_room_req;
    handlers_[proto::CS_JOIN_ROOM_REQ]          = &LobbySession::handle_join_room_req;
    handlers_[proto::CS_UPDATE_ROOM_STATE_REQ]  = &LobbySession::handle_update_room_state_req;
    handlers_[proto::CS_FETCH_ROOM_ARCHIVE_REQ] = &LobbySession::handle_fetch_room_archive_req;
    handlers_[proto::CS_LOBBY_CHAT_REQ]         = &LobbySession::handle_lobby_chat_req;
}

LobbySession::LobbySession()
    : gateway_id_(0u)
{
    std::call_once(handlers_once_, []() {
        register_handlers();
    });
}

uint64_t LobbySession::gateway_id() const
{
    return gateway_id_;
}

int LobbySession::on_connect(const service_session::SessionOpenRequest& request)
{
    gateway_id_ = 0u;
    (void)request;
    return proto::ERROR_CODE_OK;
}

void LobbySession::on_close(uint32_t reason, uint64_t now_ms)
{
    ABE_LOG_DEBUG("lobby session closed uid=%llu gateway_id=%llu conn_id=%llu reason=%u now_ms=%llu",
        (unsigned long long)user_id(),
        (unsigned long long)gateway_id_,
        (unsigned long long)conn_id(),
        reason,
        (unsigned long long)now_ms);
    gateway_id_ = 0u;
}

void LobbySession::on_reset()
{
    gateway_id_ = 0u;
}

int LobbySession::on_message(const void* data, uint32_t size, uint64_t now_ms)
{
    abe_msg_packet_view_t view;
    LobbyMessage message;
    int rc;

    rc = abe_msg_packet_decode(data, size, &view);
    if (rc != ABE_PROTOCOL_OK) {
        return proto::ERROR_CODE_COMMON_PROTOCOL_ERROR;
    }
    if (view.header.msg_id == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    message.message_id = view.header.msg_id;
    message.data = view.body;
    message.size = view.body_size;
    message.recv_time_ms = now_ms;
    return dispatch_message(message);
}

int LobbySession::dispatch_message(const LobbyMessage& message)
{
    std::unordered_map<uint32_t, MessageHandler>::const_iterator it;

    it = handlers_.find(message.message_id);
    if (it == handlers_.end()) {
        ABE_LOG_WARN("lobby session no handler msg_id=%u conn_id=%llu",
            message.message_id, (unsigned long long)conn_id());
        return proto::ERROR_CODE_SESSION_NO_HANDLER;
    }
    return (this->*it->second)(message);
}

int LobbySession::send_packet(const void* data, uint32_t size)
{
    (void)data;
    (void)size;
    return proto::ERROR_CODE_OK;
}

} /* namespace lobby */
} /* namespace service */
} /* namespace abe */
