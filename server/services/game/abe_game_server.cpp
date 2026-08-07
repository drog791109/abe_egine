#include "abe_game_server.h"

#include "abe_error.h"
#include "abe_log.h"
#include "abe_time.h"

#include <string.h>

namespace abe {
namespace service {
namespace game {

namespace svc = ::abe::service::common;

enum {
    ABE_GAME_DEFAULT_MAX_ROOMS = 1024u,
    ABE_GAME_DEFAULT_MAX_PLAYERS_PER_ROOM = 8u,
    ABE_GAME_DEFAULT_SERVER_ID = 1u,
    ABE_GAME_DEFAULT_IDLE_TIMEOUT_MS = 60000u
};

void set_game_server_defaults(GameServerConfig* config)
{
    if (config == NULL) {
        return;
    }

    config->max_rooms = ABE_GAME_DEFAULT_MAX_ROOMS;
    config->max_players_per_room = ABE_GAME_DEFAULT_MAX_PLAYERS_PER_ROOM;
    config->server_id = ABE_GAME_DEFAULT_SERVER_ID;
    config->idle_timeout_ms = ABE_GAME_DEFAULT_IDLE_TIMEOUT_MS;
}

GameServer::GameServer()
    : initialized_(0)
{
    set_game_server_defaults(&config_);
}

const char* GameServer::name() const
{
    return "game";
}

const char* GameServer::config_path() const
{
    return "server/bin/game.json";
}

void GameServer::defaults()
{
    set_game_server_defaults(&config_);
}

int GameServer::load_config(const abe_config_t* config)
{
    uint64_t value;
    int rc;

    if (config == NULL) {
        return svc::SERVICE_STATUS_OK;
    }

    rc = abe_config_get_u64(config, "game.max_rooms", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.max_rooms = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid game config path=game.max_rooms status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "game.max_players_per_room", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.max_players_per_room = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid game config path=game.max_players_per_room status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "game.server_id", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.server_id = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid game config path=game.server_id status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "game.idle_timeout_ms", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.idle_timeout_ms = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid game config path=game.idle_timeout_ms status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    return svc::SERVICE_STATUS_OK;
}

int GameServer::init(svc::Context& context)
{
    int rc;
    uint32_t max_sessions;

    (void)context;

    if (config_.max_rooms == 0u || config_.max_players_per_room == 0u) {
        ABE_LOG_ERROR("game init failed: max_rooms and max_players_per_room must be > 0");
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    max_sessions = config_.max_rooms * config_.max_players_per_room;
    rc = sessions_.init<GameSession>(
        config_.server_id,
        max_sessions,
        config_.idle_timeout_ms);
    if (rc != ABE_OK) {
        ABE_LOG_ERROR("game session manager init failed rc=%d", rc);
        return svc::SERVICE_STATUS_FAILED;
    }

    initialized_ = 1;
    ABE_LOG_INFO("game server initialized max_rooms=%u max_players_per_room=%u server_id=%llu",
        config_.max_rooms, config_.max_players_per_room,
        (unsigned long long)config_.server_id);
    return svc::SERVICE_STATUS_OK;
}

int GameServer::process_message(const svc::Message& message)
{
    if (!initialized_) {
        return svc::SERVICE_STATUS_FAILED;
    }
    (void)message;
    return svc::SERVICE_STATUS_OK;
}

int GameServer::update(uint64_t now_ms)
{
    if (!initialized_) {
        return svc::SERVICE_STATUS_OK;
    }
    sessions_.update(now_ms, NULL);
    return svc::SERVICE_STATUS_OK;
}

void GameServer::close(uint64_t now_ms)
{
    if (initialized_) {
        sessions_.close(now_ms);
    }
    initialized_ = 0;
    ABE_LOG_INFO("game server closed");
}

int GameServer::handle_disconnect(
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

abe::service::session::SessionManager* GameServer::session_manager()
{
    return &sessions_;
}

const abe::service::session::SessionManager* GameServer::session_manager() const
{
    return &sessions_;
}

int GameServer::initialized() const
{
    return initialized_;
}

} /* namespace game */
} /* namespace service */
} /* namespace abe */
