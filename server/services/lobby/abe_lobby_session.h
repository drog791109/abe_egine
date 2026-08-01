#ifndef ABE_SERVICE_LOBBY_SESSION_H
#define ABE_SERVICE_LOBBY_SESSION_H

#include "abe_session.h"

#include <mutex>
#include <stdint.h>
#include <unordered_map>

namespace abe {
namespace service {
namespace lobby {

class LobbySession : public abe::service::session::Session {
public:
    LobbySession();

    uint64_t gateway_id() const;

protected:
    virtual int on_connect(const abe::service::session::SessionOpenRequest& request);
    virtual void on_close(uint32_t reason, uint64_t now_ms);
    virtual void on_reset();
    virtual int on_message(const void* data, uint32_t size, uint64_t now_ms);
    virtual int send_packet(const void* data, uint32_t size);

private:
    struct LobbyMessage {
        uint32_t message_id;
        const void* data;
        uint32_t size;
        uint64_t recv_time_ms;
    };

    typedef int (LobbySession::*MessageHandler)(const LobbyMessage& message);

    static void register_handlers();
    int dispatch_message(const LobbyMessage& message);

    int handle_enter_lobby_req(const LobbyMessage& message);
    int handle_room_list_req(const LobbyMessage& message);
    int handle_create_room_req(const LobbyMessage& message);
    int handle_join_room_req(const LobbyMessage& message);
    int handle_update_room_state_req(const LobbyMessage& message);
    int handle_fetch_room_archive_req(const LobbyMessage& message);
    int handle_lobby_chat_req(const LobbyMessage& message);

    uint64_t gateway_id_;

    static std::unordered_map<uint32_t, MessageHandler> handlers_;
    static std::once_flag handlers_once_;
};

} /* namespace lobby */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_LOBBY_SESSION_H */
