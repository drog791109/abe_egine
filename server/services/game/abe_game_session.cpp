#include "abe_game_session.h"

#include "abe_log.h"
#include "abe_protocol.h"
#include "protocol.pb.h"

#include <string>

namespace abe {
namespace service {
namespace game {

namespace proto = ::abe::proto::client;
namespace service_session = ::abe::service::session;

std::unordered_map<uint32_t, GameSession::MessageHandler> GameSession::handlers_;
std::once_flag GameSession::handlers_once_;

void GameSession::register_handlers()
{
    /* Register game message handlers here.
     * e.g. handlers_[proto::CS_ENTER_GAME_REQ] = &GameSession::handle_enter_game_req;
     */
    handlers_[proto::CS_ENTER_GAME_REQ]  = &GameSession::handle_enter_game_req;
    handlers_[proto::CS_LEAVE_GAME_REQ]  = &GameSession::handle_leave_game_req;
    handlers_[proto::CS_GAME_ACTION_REQ] = &GameSession::handle_game_action_req;
    handlers_[proto::CS_ROOM_CHAT_REQ]   = &GameSession::handle_room_chat_req;
}

GameSession::GameSession()
    : gateway_id_(0u),
      room_id_(0u),
      send_callback_(NULL),
      send_callback_user_data_(NULL)
{
    std::call_once(handlers_once_, []() {
        register_handlers();
    });
}

uint64_t GameSession::gateway_id() const
{
    return gateway_id_;
}

uint64_t GameSession::room_id() const
{
    return room_id_;
}

void GameSession::set_room_id(uint64_t room_id)
{
    room_id_ = room_id;
}

void GameSession::set_send_callback(
    GameSessionSendCallback callback,
    void* user_data)
{
    send_callback_ = callback;
    send_callback_user_data_ = user_data;
}

int GameSession::on_connect(const service_session::SessionOpenRequest& request)
{
    gateway_id_ = 0u;
    room_id_ = 0u;
    (void)request;
    return proto::ERROR_CODE_OK;
}

void GameSession::on_close(uint32_t reason, uint64_t now_ms)
{
    ABE_LOG_DEBUG("game session closed uid=%llu gateway_id=%llu conn_id=%llu room_id=%llu reason=%u now_ms=%llu",
        (unsigned long long)user_id(),
        (unsigned long long)gateway_id_,
        (unsigned long long)conn_id(),
        (unsigned long long)room_id_,
        reason,
        (unsigned long long)now_ms);
    gateway_id_ = 0u;
    room_id_ = 0u;
    send_callback_ = NULL;
    send_callback_user_data_ = NULL;
}

void GameSession::on_reset()
{
    gateway_id_ = 0u;
    room_id_ = 0u;
    send_callback_ = NULL;
    send_callback_user_data_ = NULL;
}

int GameSession::on_message(const void* data, uint32_t size, uint64_t now_ms)
{
    abe_msg_packet_view_t view;
    GameMessage message;
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

int GameSession::dispatch_message(const GameMessage& message)
{
    std::unordered_map<uint32_t, MessageHandler>::const_iterator it;

    it = handlers_.find(message.message_id);
    if (it == handlers_.end()) {
        ABE_LOG_WARN("game session no handler msg_id=%u conn_id=%llu",
            message.message_id, (unsigned long long)conn_id());
        return proto::ERROR_CODE_SESSION_NO_HANDLER;
    }
    return (this->*it->second)(message);
}

int GameSession::send_serialized_message(
    uint32_t message_id,
    uint32_t seq,
    const void* body,
    uint32_t body_size,
    uint64_t now_ms)
{
    abe_msg_header_t header;
    std::string packet;
    uint32_t packet_size;
    uint32_t written_size;
    int rc;

    if (message_id == 0u || (body == NULL && body_size != 0u)) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    rc = abe_msg_packet_get_size(body_size, &packet_size);
    if (rc != ABE_PROTOCOL_OK) {
        return proto::ERROR_CODE_COMMON_PROTOCOL_ERROR;
    }

    packet.resize(packet_size);
    abe_msg_header_init(&header);
    header.msg_id = message_id;
    header.session_id = conn_id();
    header.role_id = room_id_;
    header.player_id = user_id();
    header.seq = seq;
    header.timestamp = now_ms;

    written_size = 0u;
    rc = abe_msg_packet_encode(
        &header,
        body,
        body_size,
        &packet[0],
        packet_size,
        &written_size);
    if (rc != ABE_PROTOCOL_OK) {
        return proto::ERROR_CODE_COMMON_PROTOCOL_ERROR;
    }

    return send(packet.data(), written_size, now_ms);
}

int GameSession::send_packet(const void* data, uint32_t size)
{
    int rc;

    if (!active()) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }
    if (data == NULL && size != 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    if (send_callback_ == NULL) {
        ABE_LOG_WARN(
            "game session send target is not connected uid=%llu conn_id=%llu room_id=%llu size=%u",
            (unsigned long long)user_id(),
            (unsigned long long)conn_id(),
            (unsigned long long)room_id_,
            size);
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }

    rc = send_callback_(this, data, size, send_callback_user_data_);
    if (rc != proto::ERROR_CODE_OK) {
        ABE_LOG_WARN(
            "game session send failed uid=%llu conn_id=%llu room_id=%llu size=%u rc=%d",
            (unsigned long long)user_id(),
            (unsigned long long)conn_id(),
            (unsigned long long)room_id_,
            size,
            rc);
    }
    return rc;
}

} /* namespace game */
} /* namespace service */
} /* namespace abe */
