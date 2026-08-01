#include "abe_lobby_session.h"

#include "abe_log.h"
#include "abe_service_message_parse.h"
#include "protocol.pb.h"

namespace abe {
namespace service {
namespace lobby {

namespace proto   = ::abe::proto::client;
namespace svc_int = ::abe::service::internal;

/* ---------------------------------------------------------------------------
 * Lobby message handler implementations.
 * Add business logic below; handler declarations are in abe_lobby_session.h.
 * ------------------------------------------------------------------------- */

int LobbySession::handle_enter_lobby_req(const LobbyMessage& message)
{
    proto::PB_CS_ENTER_LOBBY_REQ request;
    int rc = svc_int::parse_message(message.data, message.size, &request);
    if (rc != proto::ERROR_CODE_OK) { return rc; }
    ABE_LOG_DEBUG("handle_enter_lobby_req uid=%llu conn_id=%llu",
        (unsigned long long)user_id(), (unsigned long long)conn_id());
    /* TODO */
    return proto::ERROR_CODE_OK;
}

int LobbySession::handle_room_list_req(const LobbyMessage& message)
{
    proto::PB_CS_ROOM_LIST_REQ request;
    int rc = svc_int::parse_message(message.data, message.size, &request);
    if (rc != proto::ERROR_CODE_OK) { return rc; }
    /* TODO */
    return proto::ERROR_CODE_OK;
}

int LobbySession::handle_create_room_req(const LobbyMessage& message)
{
    proto::PB_CS_CREATE_ROOM_REQ request;
    int rc = svc_int::parse_message(message.data, message.size, &request);
    if (rc != proto::ERROR_CODE_OK) { return rc; }
    /* TODO */
    return proto::ERROR_CODE_OK;
}

int LobbySession::handle_join_room_req(const LobbyMessage& message)
{
    proto::PB_CS_JOIN_ROOM_REQ request;
    int rc = svc_int::parse_message(message.data, message.size, &request);
    if (rc != proto::ERROR_CODE_OK) { return rc; }
    /* TODO */
    return proto::ERROR_CODE_OK;
}

int LobbySession::handle_update_room_state_req(const LobbyMessage& message)
{
    proto::PB_CS_UPDATE_ROOM_STATE_REQ request;
    int rc = svc_int::parse_message(message.data, message.size, &request);
    if (rc != proto::ERROR_CODE_OK) { return rc; }
    /* TODO */
    return proto::ERROR_CODE_OK;
}

int LobbySession::handle_fetch_room_archive_req(const LobbyMessage& message)
{
    proto::PB_CS_FETCH_ROOM_ARCHIVE_REQ request;
    int rc = svc_int::parse_message(message.data, message.size, &request);
    if (rc != proto::ERROR_CODE_OK) { return rc; }
    /* TODO */
    return proto::ERROR_CODE_OK;
}

int LobbySession::handle_lobby_chat_req(const LobbyMessage& message)
{
    proto::PB_CS_LOBBY_CHAT_REQ request;
    int rc = svc_int::parse_message(message.data, message.size, &request);
    if (rc != proto::ERROR_CODE_OK) { return rc; }
    /* TODO */
    return proto::ERROR_CODE_OK;
}

} /* namespace lobby */
} /* namespace service */
} /* namespace abe */
