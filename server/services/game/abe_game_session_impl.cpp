#include "abe_game_session.h"

#include "abe_log.h"
#include "abe_service_message_parse.h"
#include "protocol.pb.h"

namespace abe {
namespace service {
namespace game {

namespace proto   = ::abe::proto::client;
namespace svc_int = ::abe::service::internal;

/* ---------------------------------------------------------------------------
 * Game message handler implementations.
 * Add business logic below; handler declarations are in abe_game_session.h.
 * ------------------------------------------------------------------------- */

int GameSession::handle_enter_game_req(const GameMessage& message)
{
    proto::PB_CS_ENTER_GAME_REQ request;
    int rc = svc_int::parse_message(message.data, message.size, &request);
    if (rc != proto::ERROR_CODE_OK) { return rc; }
    ABE_LOG_DEBUG("handle_enter_game_req uid=%llu conn_id=%llu room_id=%llu",
        (unsigned long long)user_id(),
        (unsigned long long)conn_id(),
        (unsigned long long)room_id_);
    /* TODO */
    return proto::ERROR_CODE_OK;
}

int GameSession::handle_leave_game_req(const GameMessage& message)
{
    proto::PB_CS_LEAVE_GAME_REQ request;
    int rc = svc_int::parse_message(message.data, message.size, &request);
    if (rc != proto::ERROR_CODE_OK) { return rc; }
    /* TODO */
    return proto::ERROR_CODE_OK;
}

int GameSession::handle_game_action_req(const GameMessage& message)
{
    proto::PB_CS_GAME_ACTION_REQ request;
    int rc = svc_int::parse_message(message.data, message.size, &request);
    if (rc != proto::ERROR_CODE_OK) { return rc; }
    /* TODO */
    return proto::ERROR_CODE_OK;
}

int GameSession::handle_room_chat_req(const GameMessage& message)
{
    proto::PB_CS_ROOM_CHAT_REQ request;
    int rc = svc_int::parse_message(message.data, message.size, &request);
    if (rc != proto::ERROR_CODE_OK) { return rc; }
    /* TODO */
    return proto::ERROR_CODE_OK;
}

} /* namespace game */
} /* namespace service */
} /* namespace abe */
