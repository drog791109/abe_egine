#include "abe_login_server.h"

#include "abe_log.h"
#include "abe_time.h"

#include <string.h>

namespace abe {
namespace service {
namespace login {

namespace gatehub = ::abe::service::gatehub;
namespace proto = ::abe::proto::client;
namespace svc = ::abe::service::common;

static const char* login_error_message(int status)
{
    switch (status) {
    case proto::ERROR_CODE_OK:
        return "ok";
    case proto::ERROR_CODE_AUTH_FAILED:
        return "authentication failed";
    case proto::ERROR_CODE_AUTH_DUPLICATE_LOGIN:
        return "duplicate login";
    case proto::ERROR_CODE_AUTH_INVALID_ACCOUNT:
        return "invalid account";
    case proto::ERROR_CODE_AUTH_INVALID_NICKNAME:
        return "invalid nickname";
    case proto::ERROR_CODE_AUTH_ACCOUNT_EXISTS:
        return "account already exists";
    case proto::ERROR_CODE_AUTH_NICKNAME_EXISTS:
        return "nickname already exists";
    case proto::ERROR_CODE_AUTH_SQL_PATTERN:
        return "sql-like input rejected";
    case proto::ERROR_CODE_AUTH_DIRTY_WORD:
        return "dirty word rejected";
    case proto::ERROR_CODE_AUTH_RECONNECT_FAILED:
        return "reconnect failed";
    case proto::ERROR_CODE_SESSION_NO_SLOT:
        return "no session slot";
    default:
        return "login failed";
    }
}

static void set_login_response_status(
    proto::PB_SC_LOGIN_RESP* response,
    int status)
{
    proto::PB_RESULT* result;

    if (response == NULL) {
        return;
    }

    result = response->mutable_result();
    result->set_error_code((proto::ErrorCode)status);
    result->set_message(login_error_message(status));
}

static void fill_account_proto(
    const LoginAccountInfo& account,
    proto::PB_LOGIN_ACCOUNT_INFO* out_info)
{
    proto::PB_PLAYER_ID* player;

    if (out_info == NULL) {
        return;
    }

    out_info->Clear();
    player = out_info->mutable_player();
    player->set_uid(account.uid);
    player->set_open_id(account.account_name);
    out_info->set_account_name(account.account_name);
    out_info->set_nickname(account.nickname);
    out_info->set_sex(account.sex);
    out_info->set_avatar_id(account.avatar_id);
    out_info->set_level(account.level);
    out_info->set_region(account.region);
    out_info->set_device_id(account.device_id);
    out_info->set_client_version(account.client_version);
}

void set_login_server_defaults(LoginServerConfig* config)
{
    if (config == NULL) {
        return;
    }

    set_login_manager_defaults(&config->accounts);
    gatehub::set_gatehub_defaults(&config->sessions);
    config->default_region = "global";
}

LoginServer::LoginServer()
    : initialized_(0)
{
    set_login_server_defaults(&config_);
}

const char* LoginServer::name() const
{
    return "login";
}

const char* LoginServer::config_path() const
{
    return "bin/login.json";
}

void LoginServer::defaults()
{
    set_login_server_defaults(&config_);
}

int LoginServer::load_config(const abe_config_t* config)
{
    const char* text;
    uint64_t value;
    int bool_value;
    int rc;

    if (config == NULL) {
        return svc::SERVICE_STATUS_OK;
    }

    rc = abe_config_get_u64(config, "login.max_accounts", &value);
    if (rc == ABE_CONFIG_OK && value >= 1u && value <= 1048576u) {
        config_.accounts.max_accounts = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid login config path=login.max_accounts status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_bool(config, "login.allow_register", &bool_value);
    if (rc == ABE_CONFIG_OK) {
        config_.accounts.allow_register = bool_value ? 1u : 0u;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid login config path=login.allow_register status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_bool(config, "login.unique_nickname", &bool_value);
    if (rc == ABE_CONFIG_OK) {
        config_.accounts.unique_nickname = bool_value ? 1u : 0u;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid login config path=login.unique_nickname status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_bool(config, "login.require_auth_token", &bool_value);
    if (rc == ABE_CONFIG_OK) {
        config_.accounts.require_auth_token = bool_value ? 1u : 0u;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid login config path=login.require_auth_token status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_string(config, "login.dirty_words", &text);
    if (rc == ABE_CONFIG_OK) {
        config_.accounts.dirty_words = text;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid login config path=login.dirty_words status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_string(config, "login.default_region", &text);
    if (rc == ABE_CONFIG_OK) {
        config_.default_region = text;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid login config path=login.default_region status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "login.max_sessions", &value);
    if (rc == ABE_CONFIG_OK && value >= 1u && value <= 1048576u) {
        config_.sessions.max_sessions = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid login config path=login.max_sessions status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_bool(config, "login.allow_reconnect", &bool_value);
    if (rc == ABE_CONFIG_OK) {
        config_.sessions.allow_reconnect = bool_value ? 1u : 0u;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid login config path=login.allow_reconnect status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_bool(config, "login.replace_duplicate_login", &bool_value);
    if (rc == ABE_CONFIG_OK) {
        config_.sessions.replace_duplicate_login = bool_value ? 1u : 0u;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid login config path=login.replace_duplicate_login status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "login.reconnect_grace_ms", &value);
    if (rc == ABE_CONFIG_OK && value <= 3600000u) {
        config_.sessions.reconnect_grace_ms = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid login config path=login.reconnect_grace_ms status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "login.session_ttl_ms", &value);
    if (rc == ABE_CONFIG_OK && value >= 1000u && value <= 604800000u) {
        config_.sessions.session_ttl_ms = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid login config path=login.session_ttl_ms status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    return svc::SERVICE_STATUS_OK;
}

int LoginServer::init(svc::Context& context)
{
    int rc;

    if (context.id_generator == NULL ||
        config_.default_region == NULL ||
        config_.default_region[0] == '\0') {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = accounts_.init(config_.accounts, context.id_generator);
    if (rc != proto::ERROR_CODE_OK) {
        ABE_LOG_ERROR("login account manager init failed status=%d", rc);
        return rc == proto::ERROR_CODE_COMMON_INVALID_ARGUMENT
            ? svc::SERVICE_STATUS_INVALID_ARG
            : svc::SERVICE_STATUS_FAILED;
    }

    rc = sessions_.init(config_.sessions, context.id_generator);
    if (rc != proto::ERROR_CODE_OK) {
        ABE_LOG_ERROR("login session registry init failed status=%d", rc);
        accounts_.close();
        return rc == proto::ERROR_CODE_COMMON_INVALID_ARGUMENT
            ? svc::SERVICE_STATUS_INVALID_ARG
            : svc::SERVICE_STATUS_FAILED;
    }

    initialized_ = 1;
    ABE_LOG_INFO(
        "login ready max_accounts=%u reconnect=%u replace_duplicate=%u",
        config_.accounts.max_accounts,
        config_.sessions.allow_reconnect,
        config_.sessions.replace_duplicate_login);
    return svc::SERVICE_STATUS_OK;
}

int LoginServer::update(uint64_t now_ms)
{
    uint32_t closed_count;
    int rc;

    if (!initialized_) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    closed_count = 0u;
    rc = sessions_.update(now_ms, &closed_count);
    if (rc != proto::ERROR_CODE_OK) {
        ABE_LOG_ERROR("login session update failed status=%d", rc);
        return svc::SERVICE_STATUS_FAILED;
    }
    if (closed_count != 0u) {
        ABE_LOG_DEBUG("login expired sessions count=%u", closed_count);
    }
    return svc::SERVICE_STATUS_OK;
}

void LoginServer::close(uint64_t now_ms)
{
    (void)now_ms;
    sessions_.close();
    accounts_.close();
    initialized_ = 0;
}

int LoginServer::handle_login(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_LOGIN_REQ& request,
    proto::PB_SC_LOGIN_RESP* out_response,
    uint64_t now_ms)
{
    LoginAuthRequest auth_request;
    LoginAuthResult auth_result;
    gatehub::GateHubOpenRequest session_request;
    gatehub::GateHubOpenResult session_result;
    LoginAccountInfo account;
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    out_response->Clear();
    out_response->mutable_header()->CopyFrom(request.header());
    out_response->mutable_header()->set_server_time_ms(abe_time_real_ms());
    if (!initialized_ ||
        gateway_id == 0u ||
        connection_id == 0u ||
        now_ms == 0u) {
        rc = !initialized_
            ? proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE
            : proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
        set_login_response_status(out_response, rc);
        return rc;
    }

    memset(&auth_request, 0, sizeof(auth_request));
    auth_request.account = request.account().c_str();
    auth_request.nickname = request.nickname().c_str();
    auth_request.auth_token = request.token().c_str();
    auth_request.device_id = request.device_id().c_str();
    auth_request.client_version = request.client_version().c_str();
    auth_request.region = config_.default_region;
    auth_request.reconnect = request.session_token().empty() ? 0u : 1u;
    auth_request.now_ms = now_ms;

    rc = accounts_.authenticate(auth_request, &auth_result);
    if (rc != proto::ERROR_CODE_OK) {
        ABE_LOG_WARN(
            "login rejected account=%s status=%d",
            request.account().c_str(),
            rc);
        set_login_response_status(out_response, rc);
        return rc;
    }

    memset(&session_request, 0, sizeof(session_request));
    session_request.account_id = auth_result.account.account_id;
    session_request.uid = auth_result.account.uid;
    session_request.gateway_id = gateway_id;
    session_request.connection_id = connection_id;
    session_request.session_token = request.session_token().c_str();
    session_request.now_ms = now_ms;

    rc = sessions_.open_session(session_request, &session_result);
    if (rc != proto::ERROR_CODE_OK) {
        ABE_LOG_WARN(
            "login session rejected uid=%llu gateway_id=%llu connection_id=%llu status=%d",
            (unsigned long long)auth_result.account.uid,
            (unsigned long long)gateway_id,
            (unsigned long long)connection_id,
            rc);
        set_login_response_status(out_response, rc);
        return rc;
    }

    rc = accounts_.mark_login_success(
        auth_result.account.uid,
        auth_request,
        &account);
    if (rc != proto::ERROR_CODE_OK) {
        (void)sessions_.close_session(
            auth_result.account.uid,
            session_result.session.session_token);
        set_login_response_status(out_response, rc);
        return rc;
    }

    set_login_response_status(out_response, proto::ERROR_CODE_OK);
    out_response->mutable_player()->set_uid(account.uid);
    out_response->mutable_player()->set_open_id(account.account_name);
    out_response->set_session_token(session_result.session.session_token);
    out_response->set_session_expire_time_ms(session_result.session.expire_time_ms);
    fill_account_proto(account, out_response->mutable_account_info());

    ABE_LOG_INFO(
        "login accepted uid=%llu gateway_id=%llu connection_id=%llu created=%u reconnect=%u replaced=%u",
        (unsigned long long)account.uid,
        (unsigned long long)gateway_id,
        (unsigned long long)connection_id,
        auth_result.created,
        session_result.reconnected,
        session_result.replaced);
    return proto::ERROR_CODE_OK;
}

int LoginServer::handle_disconnect(
    uint64_t gateway_id,
    uint64_t connection_id,
    uint64_t now_ms)
{
    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    return sessions_.disconnect(gateway_id, connection_id, now_ms);
}

LoginManager* LoginServer::account_manager()
{
    return &accounts_;
}

const LoginManager* LoginServer::account_manager() const
{
    return &accounts_;
}

gatehub::GateHubRegistry* LoginServer::session_registry()
{
    return &sessions_;
}

const gatehub::GateHubRegistry* LoginServer::session_registry() const
{
    return &sessions_;
}

int LoginServer::initialized() const
{
    return initialized_;
}

} /* namespace login */
} /* namespace service */
} /* namespace abe */
