#include "abe_match_server.h"

#include "abe_log.h"

namespace abe {
namespace service {
namespace match {

namespace svc = ::abe::service::common;

enum {
    ABE_MATCH_DEFAULT_MAX_QUEUES = 1024u,
    ABE_MATCH_DEFAULT_MAX_PLAYERS_PER_QUEUE = 64u,
    ABE_MATCH_DEFAULT_SERVER_ID = 1u,
    ABE_MATCH_DEFAULT_IDLE_TIMEOUT_MS = 60000u
};

void set_match_server_defaults(MatchServerConfig* config)
{
    if (config == NULL) {
        return;
    }

    config->max_queues = ABE_MATCH_DEFAULT_MAX_QUEUES;
    config->max_players_per_queue = ABE_MATCH_DEFAULT_MAX_PLAYERS_PER_QUEUE;
    config->server_id = ABE_MATCH_DEFAULT_SERVER_ID;
    config->idle_timeout_ms = ABE_MATCH_DEFAULT_IDLE_TIMEOUT_MS;
}

MatchServer::MatchServer()
    : initialized_(0)
{
    set_match_server_defaults(&config_);
}

const char* MatchServer::name() const
{
    return "match";
}

const char* MatchServer::config_path() const
{
    return "server/bin/match.json";
}

void MatchServer::defaults()
{
    set_match_server_defaults(&config_);
}

int MatchServer::load_config(const abe_config_t* config)
{
    uint64_t value;
    int rc;

    if (config == NULL) {
        return svc::SERVICE_STATUS_OK;
    }

    rc = abe_config_get_u64(config, "match.max_queues", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.max_queues = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid match config path=match.max_queues status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "match.max_players_per_queue", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.max_players_per_queue = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid match config path=match.max_players_per_queue status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "match.server_id", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.server_id = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid match config path=match.server_id status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "match.idle_timeout_ms", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.idle_timeout_ms = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid match config path=match.idle_timeout_ms status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    return svc::SERVICE_STATUS_OK;
}

int MatchServer::init(svc::Context& context)
{
    (void)context;

    if (config_.max_queues == 0u || config_.max_players_per_queue == 0u) {
        ABE_LOG_ERROR("match init failed: max_queues and max_players_per_queue must be > 0");
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    initialized_ = 1;
    ABE_LOG_INFO("match server initialized max_queues=%u max_players_per_queue=%u server_id=%llu",
        config_.max_queues,
        config_.max_players_per_queue,
        (unsigned long long)config_.server_id);
    return svc::SERVICE_STATUS_OK;
}

int MatchServer::process_message(const svc::Message& message)
{
    if (!initialized_) {
        return svc::SERVICE_STATUS_FAILED;
    }
    (void)message;
    return svc::SERVICE_STATUS_OK;
}

int MatchServer::update(uint64_t now_ms)
{
    (void)now_ms;

    if (!initialized_) {
        return svc::SERVICE_STATUS_OK;
    }
    return svc::SERVICE_STATUS_OK;
}

void MatchServer::close(uint64_t now_ms)
{
    (void)now_ms;

    if (initialized_) {
        ABE_LOG_INFO("match server closed");
    }
    initialized_ = 0;
}

int MatchServer::initialized() const
{
    return initialized_;
}

} /* namespace match */
} /* namespace service */
} /* namespace abe */
