#include "abe_gateway_backend.h"

#include "abe_log.h"

#include <string.h>

namespace abe {
namespace service {
namespace gateway {

namespace gatehub = ::abe::service::gatehub;
namespace login = ::abe::service::login;
namespace proto = ::abe::proto::client;
namespace svc = ::abe::service::common;

LocalGatewayBackend::LocalGatewayBackend()
    : initialized_(0)
{
}

LocalGatewayBackend::~LocalGatewayBackend()
{
    close(0u);
}

int LocalGatewayBackend::init(svc::Context& context)
{
    int rc;

    close(0u);

    login_.defaults();
    rc = login_.load_config(context.config);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = login_.init(context);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }

    gatehub_.defaults();
    rc = gatehub_.load_config(context.config);
    if (rc != svc::SERVICE_STATUS_OK) {
        login_.close(0u);
        return rc;
    }
    rc = gatehub_.init(context);
    if (rc != svc::SERVICE_STATUS_OK) {
        login_.close(0u);
        return rc;
    }

    initialized_ = 1;
    return svc::SERVICE_STATUS_OK;
}

int LocalGatewayBackend::update(uint64_t now_ms)
{
    int rc;

    if (!initialized_) {
        return svc::SERVICE_STATUS_OK;
    }

    rc = login_.update(now_ms);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = gatehub_.update(now_ms);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return svc::SERVICE_STATUS_OK;
}

void LocalGatewayBackend::close(uint64_t now_ms)
{
    if (initialized_) {
        gatehub_.close(now_ms);
        login_.close(now_ms);
    }
    initialized_ = 0;
}

int LocalGatewayBackend::handle_login(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_LOGIN_REQ& request,
    proto::PB_SC_LOGIN_RESP* out_response,
    uint64_t now_ms)
{
    login::LoginAccountData account;
    gatehub::GateHubOpenRequest open_request;
    gatehub::GateHubOpenResult open_result;
    int rc;

    rc = login_.handle_login(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
    if (rc != proto::ERROR_CODE_OK ||
        out_response == NULL ||
        out_response->result().error_code() != proto::ERROR_CODE_OK) {
        return rc;
    }

    memset(&account, 0, sizeof(account));
    rc = login_.account_process()->find_account_by_uid(
        out_response->player().uid(),
        &account);
    if (rc != proto::ERROR_CODE_OK) {
        return rc;
    }

    memset(&open_request, 0, sizeof(open_request));
    open_request.account_id = account.account_id;
    open_request.uid = out_response->player().uid();
    open_request.gateway_id = gateway_id;
    open_request.connection_id = connection_id;
    open_request.session_token = out_response->session_token().c_str();
    open_request.now_ms = now_ms;
    rc = gatehub_.registry()->open_session(open_request, &open_result);
    if (rc != proto::ERROR_CODE_OK) {
        if (out_response != NULL) {
            out_response->mutable_result()->set_error_code((proto::ErrorCode)rc);
            out_response->mutable_result()->set_message("gatehub session failed");
        }
        return rc;
    }

    return proto::ERROR_CODE_OK;
}

int LocalGatewayBackend::handle_create_charactor(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_CREATE_CHARACTOR& request,
    proto::PB_SC_CREATE_CHARACTOR* out_response,
    uint64_t now_ms)
{
    return login_.handle_create_charactor(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_select_charactor(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_SELECT_CHARACTOR& request,
    proto::PB_SC_SELECT_CHARACTOR* out_response,
    uint64_t now_ms)
{
    return login_.handle_select_charactor(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_delete_charactor(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_DELETE_CHARACTOR& request,
    proto::PB_SC_DELETE_CHARACTOR* out_response,
    uint64_t now_ms)
{
    return login_.handle_delete_charactor(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_enter_lobby(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_ENTER_LOBBY_REQ& request,
    proto::PB_SC_ENTER_LOBBY_RESP* out_response,
    uint64_t now_ms)
{
    return gatehub_.handle_enter_lobby(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_room_list(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_ROOM_LIST_REQ& request,
    proto::PB_SC_ROOM_LIST_RESP* out_response,
    uint64_t now_ms)
{
    return gatehub_.handle_room_list(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_create_room(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_CREATE_ROOM_REQ& request,
    proto::PB_SC_CREATE_ROOM_RESP* out_response,
    uint64_t now_ms)
{
    return gatehub_.handle_create_room(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_join_room(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_JOIN_ROOM_REQ& request,
    proto::PB_SC_JOIN_ROOM_RESP* out_response,
    uint64_t now_ms)
{
    return gatehub_.handle_join_room(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_update_room_state(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_UPDATE_ROOM_STATE_REQ& request,
    proto::PB_SC_UPDATE_ROOM_STATE_RESP* out_response,
    uint64_t now_ms)
{
    return gatehub_.handle_update_room_state(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_fetch_room_archive(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_FETCH_ROOM_ARCHIVE_REQ& request,
    proto::PB_SC_FETCH_ROOM_ARCHIVE_RESP* out_response,
    uint64_t now_ms)
{
    return gatehub_.handle_fetch_room_archive(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_lobby_chat(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_LOBBY_CHAT_REQ& request,
    proto::PB_SC_ERROR_NOTIFY* out_response,
    uint64_t now_ms)
{
    return gatehub_.handle_lobby_chat(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_enter_game(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_ENTER_GAME_REQ& request,
    proto::PB_SC_ENTER_GAME_RESP* out_response,
    uint64_t now_ms)
{
    return gatehub_.handle_enter_game(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_leave_game(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_LEAVE_GAME_REQ& request,
    proto::PB_SC_LEAVE_GAME_RESP* out_response,
    uint64_t now_ms)
{
    return gatehub_.handle_leave_game(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_game_action(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_GAME_ACTION_REQ& request,
    proto::PB_SC_ERROR_NOTIFY* out_response,
    uint64_t now_ms)
{
    return gatehub_.handle_game_action(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_room_chat(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_ROOM_CHAT_REQ& request,
    proto::PB_SC_ERROR_NOTIFY* out_response,
    uint64_t now_ms)
{
    return gatehub_.handle_room_chat(
        gateway_id,
        connection_id,
        request,
        out_response,
        now_ms);
}

int LocalGatewayBackend::handle_disconnect(
    uint64_t gateway_id,
    uint64_t connection_id,
    uint64_t now_ms)
{
    int rc;

    if (!initialized_) {
        return proto::ERROR_CODE_OK;
    }

    rc = login_.handle_disconnect(gateway_id, connection_id, now_ms);
    if (rc != proto::ERROR_CODE_OK &&
        rc != proto::ERROR_CODE_SESSION_NOT_FOUND) {
        ABE_LOG_WARN(
            "local backend login disconnect failed gateway_id=%llu conn_id=%llu rc=%d",
            (unsigned long long)gateway_id,
            (unsigned long long)connection_id,
            rc);
    }

    rc = gatehub_.registry()->disconnect(gateway_id, connection_id, now_ms);
    if (rc != proto::ERROR_CODE_OK &&
        rc != proto::ERROR_CODE_SESSION_NOT_FOUND) {
        ABE_LOG_WARN(
            "local backend gatehub disconnect failed gateway_id=%llu conn_id=%llu rc=%d",
            (unsigned long long)gateway_id,
            (unsigned long long)connection_id,
            rc);
    }
    return proto::ERROR_CODE_OK;
}

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */
