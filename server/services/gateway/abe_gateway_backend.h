#ifndef ABE_SERVICE_GATEWAY_BACKEND_H
#define ABE_SERVICE_GATEWAY_BACKEND_H

#include "abe_gatehub_server.h"
#include "abe_login_server.h"
#include "abe_service_runtime.h"
#include "protocol.pb.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace gateway {

/*
 * Gateway owns the client connection, while this interface owns the business
 * routing decision.  The current runtime uses the local implementation below;
 * a network RPC implementation can replace it without changing session code.
 */
class GatewayBackend {
public:
    virtual ~GatewayBackend() {}

    virtual int init(abe::service::common::Context& context) = 0;
    virtual int update(uint64_t now_ms) = 0;
    virtual void close(uint64_t now_ms) = 0;

    virtual int handle_login(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_LOGIN_REQ& request,
        abe::proto::client::PB_SC_LOGIN_RESP* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_create_charactor(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_CREATE_CHARACTOR& request,
        abe::proto::client::PB_SC_CREATE_CHARACTOR* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_select_charactor(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_SELECT_CHARACTOR& request,
        abe::proto::client::PB_SC_SELECT_CHARACTOR* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_delete_charactor(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_DELETE_CHARACTOR& request,
        abe::proto::client::PB_SC_DELETE_CHARACTOR* out_response,
        uint64_t now_ms) = 0;

    virtual int handle_enter_lobby(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_ENTER_LOBBY_REQ& request,
        abe::proto::client::PB_SC_ENTER_LOBBY_RESP* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_room_list(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_ROOM_LIST_REQ& request,
        abe::proto::client::PB_SC_ROOM_LIST_RESP* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_create_room(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_CREATE_ROOM_REQ& request,
        abe::proto::client::PB_SC_CREATE_ROOM_RESP* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_join_room(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_JOIN_ROOM_REQ& request,
        abe::proto::client::PB_SC_JOIN_ROOM_RESP* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_update_room_state(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_UPDATE_ROOM_STATE_REQ& request,
        abe::proto::client::PB_SC_UPDATE_ROOM_STATE_RESP* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_fetch_room_archive(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_FETCH_ROOM_ARCHIVE_REQ& request,
        abe::proto::client::PB_SC_FETCH_ROOM_ARCHIVE_RESP* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_lobby_chat(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_LOBBY_CHAT_REQ& request,
        abe::proto::client::PB_SC_ERROR_NOTIFY* out_response,
        uint64_t now_ms) = 0;

    virtual int handle_enter_game(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_ENTER_GAME_REQ& request,
        abe::proto::client::PB_SC_ENTER_GAME_RESP* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_leave_game(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_LEAVE_GAME_REQ& request,
        abe::proto::client::PB_SC_LEAVE_GAME_RESP* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_game_action(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_GAME_ACTION_REQ& request,
        abe::proto::client::PB_SC_ERROR_NOTIFY* out_response,
        uint64_t now_ms) = 0;
    virtual int handle_room_chat(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_ROOM_CHAT_REQ& request,
        abe::proto::client::PB_SC_ERROR_NOTIFY* out_response,
        uint64_t now_ms) = 0;

    virtual int handle_disconnect(
        uint64_t gateway_id,
        uint64_t connection_id,
        uint64_t now_ms) = 0;
};

/*
 * The repository does not have an internal RPC listener yet.  This backend
 * assembles the existing login and gatehub handlers in the gateway process so
 * the client protocol is usable today.  Its interface is deliberately the
 * same one a future RPC-backed router will implement.
 */
class LocalGatewayBackend : public GatewayBackend {
public:
    LocalGatewayBackend();
    virtual ~LocalGatewayBackend();

    virtual int init(abe::service::common::Context& context);
    virtual int update(uint64_t now_ms);
    virtual void close(uint64_t now_ms);

    virtual int handle_login(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_LOGIN_REQ& request,
        abe::proto::client::PB_SC_LOGIN_RESP* out_response,
        uint64_t now_ms);
    virtual int handle_create_charactor(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_CREATE_CHARACTOR& request,
        abe::proto::client::PB_SC_CREATE_CHARACTOR* out_response,
        uint64_t now_ms);
    virtual int handle_select_charactor(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_SELECT_CHARACTOR& request,
        abe::proto::client::PB_SC_SELECT_CHARACTOR* out_response,
        uint64_t now_ms);
    virtual int handle_delete_charactor(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_DELETE_CHARACTOR& request,
        abe::proto::client::PB_SC_DELETE_CHARACTOR* out_response,
        uint64_t now_ms);

    virtual int handle_enter_lobby(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_ENTER_LOBBY_REQ& request,
        abe::proto::client::PB_SC_ENTER_LOBBY_RESP* out_response,
        uint64_t now_ms);
    virtual int handle_room_list(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_ROOM_LIST_REQ& request,
        abe::proto::client::PB_SC_ROOM_LIST_RESP* out_response,
        uint64_t now_ms);
    virtual int handle_create_room(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_CREATE_ROOM_REQ& request,
        abe::proto::client::PB_SC_CREATE_ROOM_RESP* out_response,
        uint64_t now_ms);
    virtual int handle_join_room(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_JOIN_ROOM_REQ& request,
        abe::proto::client::PB_SC_JOIN_ROOM_RESP* out_response,
        uint64_t now_ms);
    virtual int handle_update_room_state(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_UPDATE_ROOM_STATE_REQ& request,
        abe::proto::client::PB_SC_UPDATE_ROOM_STATE_RESP* out_response,
        uint64_t now_ms);
    virtual int handle_fetch_room_archive(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_FETCH_ROOM_ARCHIVE_REQ& request,
        abe::proto::client::PB_SC_FETCH_ROOM_ARCHIVE_RESP* out_response,
        uint64_t now_ms);
    virtual int handle_lobby_chat(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_LOBBY_CHAT_REQ& request,
        abe::proto::client::PB_SC_ERROR_NOTIFY* out_response,
        uint64_t now_ms);

    virtual int handle_enter_game(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_ENTER_GAME_REQ& request,
        abe::proto::client::PB_SC_ENTER_GAME_RESP* out_response,
        uint64_t now_ms);
    virtual int handle_leave_game(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_LEAVE_GAME_REQ& request,
        abe::proto::client::PB_SC_LEAVE_GAME_RESP* out_response,
        uint64_t now_ms);
    virtual int handle_game_action(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_GAME_ACTION_REQ& request,
        abe::proto::client::PB_SC_ERROR_NOTIFY* out_response,
        uint64_t now_ms);
    virtual int handle_room_chat(
        uint64_t gateway_id,
        uint64_t connection_id,
        const abe::proto::client::PB_CS_ROOM_CHAT_REQ& request,
        abe::proto::client::PB_SC_ERROR_NOTIFY* out_response,
        uint64_t now_ms);

    virtual int handle_disconnect(
        uint64_t gateway_id,
        uint64_t connection_id,
        uint64_t now_ms);

private:
    LocalGatewayBackend(const LocalGatewayBackend&);
    LocalGatewayBackend& operator=(const LocalGatewayBackend&);

    abe::service::login::LoginServer login_;
    abe::service::gatehub::GateHubServer gatehub_;
    int initialized_;
};

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GATEWAY_BACKEND_H */
