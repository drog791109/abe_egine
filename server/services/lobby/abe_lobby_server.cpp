#include "abe_lobby_server.h"

#include "abe_error.h"
#include "abe_log.h"
#include "abe_time.h"

#include <string.h>

namespace abe {
namespace service {
namespace lobby {

namespace svc = ::abe::service::common;

enum {
    ABE_LOBBY_DEFAULT_MAX_PLAYERS = 4096u,
    ABE_LOBBY_DEFAULT_SERVER_ID = 1u,
    ABE_LOBBY_DEFAULT_IDLE_TIMEOUT_MS = 60000u
};

void set_lobby_server_defaults(LobbyServerConfig* config)
{
    if (config == NULL) {
        return;
    }

    config->max_players = ABE_LOBBY_DEFAULT_MAX_PLAYERS;
    config->server_id = ABE_LOBBY_DEFAULT_SERVER_ID;
    config->idle_timeout_ms = ABE_LOBBY_DEFAULT_IDLE_TIMEOUT_MS;
}

LobbyServer::LobbyServer()
    : initialized_(0)
{
    set_lobby_server_defaults(&config_);
}

const char* LobbyServer::name() const
{
    return "lobby";
}

const char* LobbyServer::config_path() const
{
    return "server/bin/lobby.json";
}

void LobbyServer::defaults()
{
    set_lobby_server_defaults(&config_);
}

int LobbyServer::load_config(const abe_config_t* config)
{
    uint64_t value;
    int rc;

    if (config == NULL) {
        return svc::SERVICE_STATUS_OK;
    }

    rc = abe_config_get_u64(config, "lobby.max_players", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.max_players = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid lobby config path=lobby.max_players status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "lobby.server_id", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.server_id = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid lobby config path=lobby.server_id status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "lobby.idle_timeout_ms", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.idle_timeout_ms = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid lobby config path=lobby.idle_timeout_ms status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    return svc::SERVICE_STATUS_OK;
}

int LobbyServer::init(svc::Context& context)
{
    int rc;

    (void)context;

    if (config_.max_players == 0u) {
        ABE_LOG_ERROR("lobby init failed: max_players must be > 0");
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = sessions_.init<LobbySession>(
        config_.server_id,
        config_.max_players,
        config_.idle_timeout_ms);
    if (rc != ABE_OK) {
        ABE_LOG_ERROR("lobby session manager init failed rc=%d", rc);
        return svc::SERVICE_STATUS_FAILED;
    }

    initialized_ = 1;
    ABE_LOG_INFO("lobby server initialized max_players=%u server_id=%llu",
        config_.max_players, (unsigned long long)config_.server_id);
    return svc::SERVICE_STATUS_OK;
}

int LobbyServer::process_message(const svc::Message& message)
{
    if (!initialized_) {
        return svc::SERVICE_STATUS_FAILED;
    }
    (void)message;
    return svc::SERVICE_STATUS_OK;
}

int LobbyServer::update(uint64_t now_ms)
{
    if (!initialized_) {
        return svc::SERVICE_STATUS_OK;
    }
    sessions_.update(now_ms, NULL);
    return svc::SERVICE_STATUS_OK;
}

void LobbyServer::close(uint64_t now_ms)
{
    if (initialized_) {
        sessions_.close(now_ms);
    }
    initialized_ = 0;
    ABE_LOG_INFO("lobby server closed");
}

int LobbyServer::handle_disconnect(
    uint64_t gateway_id,
    uint64_t connection_id,
    uint64_t now_ms)
{
    abe::service::session::Session* session;

    if (!initialized_) {
        return ABE_ERROR;
    }
    session = sessions_.find_session(connection_id);
    if (session == NULL) {
        return ABE_NOT_FOUND;
    }
    (void)gateway_id;
    return sessions_.close_session(connection_id, 0u, now_ms);
}

abe::service::session::SessionManager* LobbyServer::session_manager()
{
    return &sessions_;
}

const abe::service::session::SessionManager* LobbyServer::session_manager() const
{
    return &sessions_;
}

int LobbyServer::initialized() const
{
    return initialized_;
}

} /* namespace lobby */
} /* namespace service */
} /* namespace abe */
