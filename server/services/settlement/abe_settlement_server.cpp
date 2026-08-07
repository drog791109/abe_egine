#include "abe_settlement_server.h"

#include "abe_log.h"

namespace abe {
namespace service {
namespace settlement {

namespace svc = ::abe::service::common;

enum {
    ABE_SETTLEMENT_DEFAULT_MAX_PENDING_EVENTS = 8192u,
    ABE_SETTLEMENT_DEFAULT_SERVER_ID = 1u,
    ABE_SETTLEMENT_DEFAULT_IDLE_TIMEOUT_MS = 60000u
};

void set_settlement_server_defaults(SettlementServerConfig* config)
{
    if (config == NULL) {
        return;
    }

    config->max_pending_events = ABE_SETTLEMENT_DEFAULT_MAX_PENDING_EVENTS;
    config->server_id = ABE_SETTLEMENT_DEFAULT_SERVER_ID;
    config->idle_timeout_ms = ABE_SETTLEMENT_DEFAULT_IDLE_TIMEOUT_MS;
}

SettlementServer::SettlementServer()
    : initialized_(0)
{
    set_settlement_server_defaults(&config_);
}

const char* SettlementServer::name() const
{
    return "settlement";
}

const char* SettlementServer::config_path() const
{
    return "server/bin/settlement.json";
}

void SettlementServer::defaults()
{
    set_settlement_server_defaults(&config_);
}

int SettlementServer::load_config(const abe_config_t* config)
{
    uint64_t value;
    int rc;

    if (config == NULL) {
        return svc::SERVICE_STATUS_OK;
    }

    rc = abe_config_get_u64(config, "settlement.max_pending_events", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.max_pending_events = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR(
            "invalid settlement config path=settlement.max_pending_events status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "settlement.server_id", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.server_id = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid settlement config path=settlement.server_id status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "settlement.idle_timeout_ms", &value);
    if (rc == ABE_CONFIG_OK && value > 0u) {
        config_.idle_timeout_ms = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR(
            "invalid settlement config path=settlement.idle_timeout_ms status=%s",
            abe_status_name(svc::SERVICE_STATUS_INVALID_ARG));
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    return svc::SERVICE_STATUS_OK;
}

int SettlementServer::init(svc::Context& context)
{
    (void)context;

    if (config_.max_pending_events == 0u) {
        ABE_LOG_ERROR("settlement init failed: max_pending_events must be > 0");
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    initialized_ = 1;
    ABE_LOG_INFO("settlement server initialized max_pending_events=%u server_id=%llu",
        config_.max_pending_events,
        (unsigned long long)config_.server_id);
    return svc::SERVICE_STATUS_OK;
}

int SettlementServer::process_message(const svc::Message& message)
{
    if (!initialized_) {
        return svc::SERVICE_STATUS_FAILED;
    }
    (void)message;
    return svc::SERVICE_STATUS_OK;
}

int SettlementServer::update(uint64_t now_ms)
{
    (void)now_ms;

    if (!initialized_) {
        return svc::SERVICE_STATUS_OK;
    }
    return svc::SERVICE_STATUS_OK;
}

void SettlementServer::close(uint64_t now_ms)
{
    (void)now_ms;

    if (initialized_) {
        ABE_LOG_INFO("settlement server closed");
    }
    initialized_ = 0;
}

int SettlementServer::initialized() const
{
    return initialized_;
}

} /* namespace settlement */
} /* namespace service */
} /* namespace abe */
