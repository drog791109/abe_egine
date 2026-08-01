#ifndef ABE_SERVICE_GAME_SESSION_H
#define ABE_SERVICE_GAME_SESSION_H

#include "abe_session.h"
#include "protocol.pb.h"

#include <mutex>
#include <stdint.h>
#include <string>
#include <unordered_map>

namespace abe {
namespace service {
namespace game {

class GameSession;

typedef int (*GameSessionSendCallback)(
    GameSession* session,
    const void* data,
    uint32_t size,
    void* user_data);

template <typename Message>
struct GameSessionProtocolId;

template <>
struct GameSessionProtocolId< ::abe::proto::client::PB_SC_ERROR_NOTIFY > {
    static uint32_t value() { return ::abe::proto::client::SC_ERROR_NOTIFY; }
};

template <>
struct GameSessionProtocolId< ::abe::proto::client::PB_SC_ENTER_GAME_RESP > {
    static uint32_t value() { return ::abe::proto::client::SC_ENTER_GAME_RESP; }
};

template <>
struct GameSessionProtocolId< ::abe::proto::client::PB_SC_LEAVE_GAME_RESP > {
    static uint32_t value() { return ::abe::proto::client::SC_LEAVE_GAME_RESP; }
};

template <>
struct GameSessionProtocolId< ::abe::proto::client::PB_SC_GAME_ACTION_BROADCAST > {
    static uint32_t value() { return ::abe::proto::client::SC_GAME_ACTION_BROADCAST; }
};

template <>
struct GameSessionProtocolId< ::abe::proto::client::PB_SC_ROOM_CHAT_NOTIFY > {
    static uint32_t value() { return ::abe::proto::client::SC_ROOM_CHAT_NOTIFY; }
};

template <>
struct GameSessionProtocolId< ::abe::proto::client::PB_SC_GAME_OVER_NOTIFY > {
    static uint32_t value() { return ::abe::proto::client::SC_GAME_OVER_NOTIFY; }
};

class GameSession : public abe::service::session::Session {
public:
    GameSession();

    uint64_t gateway_id() const;
    uint64_t room_id() const;
    void set_room_id(uint64_t room_id);
    void set_send_callback(GameSessionSendCallback callback, void* user_data);

    template <typename Message>
    int send_message(const Message& message, uint64_t now_ms)
    {
        Message output(message);
        std::string body;
        uint32_t message_id;

        message_id = GameSessionProtocolId<Message>::value();
        output.mutable_header()->set_protocol_id(
            (::abe::proto::client::ProtocolId)message_id);
        if (!output.SerializeToString(&body)) {
            return ::abe::proto::client::ERROR_CODE_SYSTEM_INTERNAL;
        }
        if (body.size() > 0xffffffffu) {
            return ::abe::proto::client::ERROR_CODE_COMMON_PROTOCOL_ERROR;
        }
        if (output.header().seq() > 0xffffffffu) {
            return ::abe::proto::client::ERROR_CODE_COMMON_INVALID_ARGUMENT;
        }

        return send_serialized_message(
            message_id,
            (uint32_t)output.header().seq(),
            body.empty() ? NULL : body.data(),
            (uint32_t)body.size(),
            now_ms);
    }

protected:
    virtual int on_connect(const abe::service::session::SessionOpenRequest& request);
    virtual void on_close(uint32_t reason, uint64_t now_ms);
    virtual void on_reset();
    virtual int on_message(const void* data, uint32_t size, uint64_t now_ms);
    virtual int send_packet(const void* data, uint32_t size);

private:
    struct GameMessage {
        uint32_t message_id;
        const void* data;
        uint32_t size;
        uint64_t recv_time_ms;
    };

    typedef int (GameSession::*MessageHandler)(const GameMessage& message);

    static void register_handlers();
    int dispatch_message(const GameMessage& message);
    int send_serialized_message(
        uint32_t message_id,
        uint32_t seq,
        const void* body,
        uint32_t body_size,
        uint64_t now_ms);

    int handle_enter_game_req(const GameMessage& message);
    int handle_leave_game_req(const GameMessage& message);
    int handle_game_action_req(const GameMessage& message);
    int handle_room_chat_req(const GameMessage& message);

    uint64_t gateway_id_;
    uint64_t room_id_;
    GameSessionSendCallback send_callback_;
    void* send_callback_user_data_;

    static std::unordered_map<uint32_t, MessageHandler> handlers_;
    static std::once_flag handlers_once_;
};

} /* namespace game */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GAME_SESSION_H */
