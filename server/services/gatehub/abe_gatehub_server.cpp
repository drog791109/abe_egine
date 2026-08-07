#include "abe_gatehub_server.h"

#include "abe_log.h"
#include "abe_time.h"
#include "protocol.pb.h"

#include <errno.h>
#include <fcntl.h>
#include <new>
#include <string.h>
#include <unistd.h>

namespace abe {
namespace service {
namespace gatehub {

namespace proto = ::abe::proto::client;

enum {
    ABE_GATEHUB_DEFAULT_MAX_SESSIONS = 4096u,
    ABE_GATEHUB_DEFAULT_RECONNECT_GRACE_S = 30u,
    ABE_GATEHUB_DEFAULT_SESSION_TTL_S = 86400u,
    ABE_GATEHUB_RANDOM_TOKEN_BYTES = 24u
};

struct GateHubRegistry::SessionSlot {
    int used;
    GateHubSessionInfo info;
};

static uint64_t add_deadline(uint64_t now_ms, uint64_t duration_ms)
{
    if (duration_ms > 0xffffffffffffffffull - now_ms) {
        return 0xffffffffffffffffull;
    }
    return now_ms + duration_ms;
}

static const char* gatehub_error_message(int status)
{
    switch (status) {
    case proto::ERROR_CODE_OK:
        return "ok";
    case proto::ERROR_CODE_COMMON_INVALID_ARGUMENT:
        return "invalid argument";
    case proto::ERROR_CODE_SESSION_NOT_FOUND:
        return "session not found";
    case proto::ERROR_CODE_SESSION_CLOSED:
        return "session closed";
    case proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE:
        return "service unavailable";
    default:
        return "gatehub request failed";
    }
}

static void fill_response_header(
    proto::PB_MESSAGE_HEADER* response_header,
    const proto::PB_MESSAGE_HEADER& request_header,
    proto::ProtocolId response_id)
{
    if (response_header == NULL) {
        return;
    }

    response_header->CopyFrom(request_header);
    response_header->set_protocol_id(response_id);
    response_header->set_server_time_ms(abe_time_real_ms());
}

static void set_result_status(proto::PB_RESULT* result, int status)
{
    if (result == NULL) {
        return;
    }

    result->set_error_code((proto::ErrorCode)status);
    result->set_message(gatehub_error_message(status));
}

static void fill_error_notify(
    proto::PB_SC_ERROR_NOTIFY* response,
    const proto::PB_MESSAGE_HEADER& request_header,
    int status)
{
    if (response == NULL) {
        return;
    }

    response->Clear();
    fill_response_header(
        response->mutable_header(),
        request_header,
        proto::SC_ERROR_NOTIFY);
    set_result_status(response->mutable_result(), status);
}

static int validate_handler_context(
    int initialized,
    uint64_t gateway_id,
    uint64_t connection_id,
    uint64_t now_ms)
{
    if (!initialized) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    if (gateway_id == 0u || connection_id == 0u || now_ms == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    return proto::ERROR_CODE_OK;
}

static int copy_session_token(
    char* out_token,
    uint32_t token_capacity,
    const char* token)
{
    size_t token_size;

    if (out_token == NULL || token_capacity == 0u) {
        return ABE_INVALID_ARG;
    }
    if (token == NULL || token[0] == '\0') {
        return ABE_NOT_FOUND;
    }

    token_size = strlen(token);
    if (token_size >= token_capacity) {
        return ABE_BUFFER_TOO_SMALL;
    }
    memcpy(out_token, token, token_size + 1u);
    return ABE_OK;
}

static int validate_online_session(
    const GateHubRegistry& registry,
    uint64_t gateway_id,
    uint64_t connection_id,
    uint64_t uid,
    GateHubSessionInfo* out_session)
{
    GateHubSessionInfo session;
    int rc;

    if (uid == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    rc = registry.find_session(uid, &session);
    if (rc != proto::ERROR_CODE_OK) {
        return rc;
    }
    if (session.gateway_id != gateway_id ||
        session.connection_id != connection_id ||
        session.state != GATEHUB_SESSION_ONLINE) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }

    if (out_session != NULL) {
        *out_session = session;
    }
    return proto::ERROR_CODE_OK;
}

static bool token_equal(const char* left, const char* right)
{
    size_t left_size;
    size_t right_size;
    size_t index;
    unsigned int difference;

    if (left == NULL || right == NULL) {
        return false;
    }

    left_size = strlen(left);
    right_size = strlen(right);
    if (left_size == 0u || left_size != right_size) {
        return false;
    }

    difference = 0u;
    index = 0u;
    while (index < left_size) {
        difference |= (unsigned int)((unsigned char)left[index] ^
            (unsigned char)right[index]);
        ++index;
    }
    return difference == 0u;
}

static int fill_random_bytes(unsigned char* bytes, uint32_t size)
{
    uint32_t offset;
    int file;

    if (bytes == NULL || size == 0u) {
        return ABE_INVALID_ARG;
    }

    file = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (file < 0) {
        return ABE_SYSTEM_ERROR;
    }

    offset = 0u;
    while (offset < size) {
        ssize_t count;

        count = read(file, bytes + offset, size - offset);
        if (count > 0) {
            offset += (uint32_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        close(file);
        return ABE_SYSTEM_ERROR;
    }

    close(file);
    return ABE_OK;
}

static int generate_session_token(
    char* out_token,
    uint32_t token_capacity)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char bytes[ABE_GATEHUB_RANDOM_TOKEN_BYTES];
    uint32_t index;
    int rc;

    if (out_token == NULL ||
        token_capacity < ABE_GATEHUB_RANDOM_TOKEN_BYTES * 2u + 1u) {
        return ABE_BUFFER_TOO_SMALL;
    }

    rc = fill_random_bytes(bytes, ABE_GATEHUB_RANDOM_TOKEN_BYTES);
    if (rc != ABE_OK) {
        return rc;
    }

    index = 0u;
    while (index < ABE_GATEHUB_RANDOM_TOKEN_BYTES) {
        out_token[index * 2u] = hex[(bytes[index] >> 4u) & 0x0fu];
        out_token[index * 2u + 1u] = hex[bytes[index] & 0x0fu];
        ++index;
    }
    out_token[ABE_GATEHUB_RANDOM_TOKEN_BYTES * 2u] = '\0';
    return ABE_OK;
}

void set_gatehub_defaults(GateHubConfig* config)
{
    if (config == NULL) {
        return;
    }

    config->max_sessions = ABE_GATEHUB_DEFAULT_MAX_SESSIONS;
    config->allow_reconnect = 1u;
    config->replace_duplicate_login = 1u;
    config->reconnect_grace_s = ABE_GATEHUB_DEFAULT_RECONNECT_GRACE_S;
    config->session_ttl_s = ABE_GATEHUB_DEFAULT_SESSION_TTL_S;
}

GateHubRegistry::GateHubRegistry()
    : id_generator_(NULL),
      slots_(NULL),
      active_count_(0u),
      initialized_(0)
{
    set_gatehub_defaults(&config_);
}

GateHubRegistry::~GateHubRegistry()
{
    close();
}

int GateHubRegistry::init(
    const GateHubConfig& config,
    abe_snowflake_t* id_generator)
{
    if (config.max_sessions == 0u ||
        config.allow_reconnect > 1u ||
        config.replace_duplicate_login > 1u ||
        config.session_ttl_s == 0u ||
        id_generator == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    close();
    slots_ = new (std::nothrow) SessionSlot[config.max_sessions];
    if (slots_ == NULL) {
        return proto::ERROR_CODE_COMMON_SERVER_BUSY;
    }

    memset(slots_, 0, sizeof(SessionSlot) * config.max_sessions);
    config_ = config;
    id_generator_ = id_generator;
    active_count_ = 0u;
    initialized_ = 1;
    return proto::ERROR_CODE_OK;
}

void GateHubRegistry::close()
{
    delete[] slots_;
    slots_ = NULL;
    id_generator_ = NULL;
    active_count_ = 0u;
    initialized_ = 0;
}

int GateHubRegistry::open_session(
    const GateHubOpenRequest& request,
    GateHubOpenResult* out_result)
{
    SessionSlot* slot;

    if (out_result != NULL) {
        memset(out_result, 0, sizeof(*out_result));
    }
    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    if (request.account_id == 0u ||
        request.uid == 0u ||
        request.gateway_id == 0u ||
        request.connection_id == 0u ||
        request.now_ms == 0u ||
        out_result == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    slot = find_slot_by_uid(request.uid);
    if (slot != NULL &&
        slot->info.expire_time_ms != 0u &&
        request.now_ms >= slot->info.expire_time_ms) {
        reset_slot(slot);
        slot = NULL;
    }
    if (slot != NULL &&
        slot->info.state == GATEHUB_SESSION_RECONNECTING &&
        slot->info.reconnect_deadline_ms != 0u &&
        request.now_ms > slot->info.reconnect_deadline_ms) {
        reset_slot(slot);
        slot = NULL;
    }

    if (slot != NULL &&
        request.session_token != NULL &&
        request.session_token[0] != '\0' &&
        token_equal(request.session_token, slot->info.session_token)) {
        if (!config_.allow_reconnect) {
            return proto::ERROR_CODE_AUTH_RECONNECT_FAILED;
        }

        slot->info.gateway_id = request.gateway_id;
        slot->info.connection_id = request.connection_id;
        slot->info.state = GATEHUB_SESSION_ONLINE;
        slot->info.reconnect_deadline_ms = 0u;
        slot->info.expire_time_ms = add_deadline(request.now_ms, config_.session_ttl_s * 1000u);
        out_result->session = slot->info;
        out_result->reconnected = 1u;
        return proto::ERROR_CODE_OK;
    }

    if (slot != NULL) {
        if (!config_.replace_duplicate_login) {
            return proto::ERROR_CODE_AUTH_DUPLICATE_LOGIN;
        }

        out_result->replaced_gateway_id = slot->info.gateway_id;
        out_result->replaced_connection_id = slot->info.connection_id;
        out_result->replaced = 1u;
        memset(&slot->info, 0, sizeof(slot->info));
        slot->used = 1;
        return start_session(slot, request, out_result);
    }

    slot = find_free_slot();
    if (slot == NULL) {
        return proto::ERROR_CODE_SESSION_NO_SLOT;
    }

    slot->used = 1;
    ++active_count_;
    return start_session(slot, request, out_result);
}

int GateHubRegistry::disconnect(
    uint64_t gateway_id,
    uint64_t connection_id,
    uint64_t now_ms)
{
    SessionSlot* slot;

    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    if (gateway_id == 0u || connection_id == 0u || now_ms == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    slot = find_slot_by_connection(gateway_id, connection_id);
    if (slot == NULL) {
        return proto::ERROR_CODE_SESSION_NOT_FOUND;
    }

    if (config_.allow_reconnect && config_.reconnect_grace_s != 0u) {
        slot->info.state = GATEHUB_SESSION_RECONNECTING;
        slot->info.reconnect_deadline_ms =
            add_deadline(now_ms, config_.reconnect_grace_s * 1000u);
        return proto::ERROR_CODE_OK;
    }

    reset_slot(slot);
    return proto::ERROR_CODE_OK;
}

int GateHubRegistry::close_session(
    uint64_t uid,
    const char* session_token)
{
    SessionSlot* slot;

    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    if (uid == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    slot = find_slot_by_uid(uid);
    if (slot == NULL) {
        return proto::ERROR_CODE_SESSION_NOT_FOUND;
    }
    if (session_token != NULL &&
        session_token[0] != '\0' &&
        !token_equal(session_token, slot->info.session_token)) {
        return proto::ERROR_CODE_AUTH_RECONNECT_FAILED;
    }

    reset_slot(slot);
    return proto::ERROR_CODE_OK;
}

int GateHubRegistry::update(
    uint64_t now_ms,
    uint32_t* out_closed_count)
{
    uint32_t index;
    uint32_t closed_count;

    if (out_closed_count != NULL) {
        *out_closed_count = 0u;
    }
    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }

    closed_count = 0u;
    index = 0u;
    while (index < config_.max_sessions) {
        SessionSlot* slot;
        bool expired;

        slot = &slots_[index];
        expired = false;
        if (slot->used &&
            slot->info.expire_time_ms != 0u &&
            now_ms >= slot->info.expire_time_ms) {
            expired = true;
        }
        if (slot->used &&
            slot->info.state == GATEHUB_SESSION_RECONNECTING &&
            slot->info.reconnect_deadline_ms != 0u &&
            now_ms > slot->info.reconnect_deadline_ms) {
            expired = true;
        }
        if (expired) {
            reset_slot(slot);
            ++closed_count;
        }
        ++index;
    }

    if (out_closed_count != NULL) {
        *out_closed_count = closed_count;
    }
    return proto::ERROR_CODE_OK;
}

int GateHubRegistry::find_session(
    uint64_t uid,
    GateHubSessionInfo* out_info) const
{
    const SessionSlot* slot;

    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    if (uid == 0u || out_info == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    slot = find_slot_by_uid(uid);
    if (slot == NULL) {
        return proto::ERROR_CODE_SESSION_NOT_FOUND;
    }

    *out_info = slot->info;
    return proto::ERROR_CODE_OK;
}

uint32_t GateHubRegistry::active_count() const
{
    return active_count_;
}

int GateHubRegistry::initialized() const
{
    return initialized_;
}

GateHubRegistry::SessionSlot* GateHubRegistry::find_slot_by_uid(uint64_t uid)
{
    uint32_t index;

    if (slots_ == NULL || uid == 0u) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_sessions) {
        if (slots_[index].used && slots_[index].info.uid == uid) {
            return &slots_[index];
        }
        ++index;
    }
    return NULL;
}

const GateHubRegistry::SessionSlot* GateHubRegistry::find_slot_by_uid(
    uint64_t uid) const
{
    uint32_t index;

    if (slots_ == NULL || uid == 0u) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_sessions) {
        if (slots_[index].used && slots_[index].info.uid == uid) {
            return &slots_[index];
        }
        ++index;
    }
    return NULL;
}

GateHubRegistry::SessionSlot* GateHubRegistry::find_slot_by_connection(
    uint64_t gateway_id,
    uint64_t connection_id)
{
    uint32_t index;

    if (slots_ == NULL || gateway_id == 0u || connection_id == 0u) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_sessions) {
        if (slots_[index].used &&
            slots_[index].info.gateway_id == gateway_id &&
            slots_[index].info.connection_id == connection_id) {
            return &slots_[index];
        }
        ++index;
    }
    return NULL;
}

GateHubRegistry::SessionSlot* GateHubRegistry::find_free_slot()
{
    uint32_t index;

    if (slots_ == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_sessions) {
        if (!slots_[index].used) {
            return &slots_[index];
        }
        ++index;
    }
    return NULL;
}

void GateHubRegistry::reset_slot(SessionSlot* slot)
{
    if (slot == NULL || !slot->used) {
        return;
    }

    memset(slot, 0, sizeof(*slot));
    if (active_count_ > 0u) {
        --active_count_;
    }
}

int GateHubRegistry::start_session(
    SessionSlot* slot,
    const GateHubOpenRequest& request,
    GateHubOpenResult* out_result)
{
    uint64_t session_id;
    int rc;

    if (slot == NULL || out_result == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    rc = abe_snowflake_next(id_generator_, &session_id);
    if (rc != ABE_OK) {
        ABE_LOG_ERROR(
            "gatehub session id generation failed status=%s",
            abe_status_name(rc));
        reset_slot(slot);
        return rc == ABE_LIMIT
            ? proto::ERROR_CODE_COMMON_SERVER_BUSY
            : proto::ERROR_CODE_SYSTEM_INTERNAL;
    }

    rc = copy_session_token(
        slot->info.session_token,
        ABE_GATEHUB_SESSION_TOKEN_CAPACITY,
        request.session_token);
    if (rc == ABE_NOT_FOUND) {
        rc = generate_session_token(
            slot->info.session_token,
            ABE_GATEHUB_SESSION_TOKEN_CAPACITY);
    }
    if (rc != ABE_OK) {
        ABE_LOG_ERROR(
            "gatehub session token setup failed status=%s",
            abe_status_name(rc));
        reset_slot(slot);
        return proto::ERROR_CODE_SYSTEM_INTERNAL;
    }

    slot->info.session_id = session_id;
    slot->info.account_id = request.account_id;
    slot->info.uid = request.uid;
    slot->info.gateway_id = request.gateway_id;
    slot->info.connection_id = request.connection_id;
    slot->info.expire_time_ms = add_deadline(request.now_ms, config_.session_ttl_s * 1000u);
    slot->info.reconnect_deadline_ms = 0u;
    slot->info.state = GATEHUB_SESSION_ONLINE;
    out_result->session = slot->info;
    return proto::ERROR_CODE_OK;
}

GateHubServer::GateHubServer()
    : initialized_(0)
{
    set_gatehub_defaults(&config_);
}

const char* GateHubServer::name() const
{
    return "gatehub";
}

const char* GateHubServer::config_path() const
{
    return "server/bin/gatehub.json";
}

void GateHubServer::defaults()
{
    set_gatehub_defaults(&config_);
}

int GateHubServer::load_config(const abe_config_t* config)
{
    uint64_t value;
    int bool_value;
    int rc;

    if (config == NULL) {
        return abe::service::common::SERVICE_STATUS_OK;
    }

    rc = abe_config_get_u64(config, "gatehub.max_sessions", &value);
    if (rc == ABE_CONFIG_OK && value >= 1u && value <= 1048576u) {
        config_.max_sessions = (uint32_t)value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gatehub config path=gatehub.max_sessions status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_bool(config, "gatehub.allow_reconnect", &bool_value);
    if (rc == ABE_CONFIG_OK) {
        config_.allow_reconnect = bool_value ? 1u : 0u;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gatehub config path=gatehub.allow_reconnect status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_bool(config, "gatehub.replace_duplicate_login", &bool_value);
    if (rc == ABE_CONFIG_OK) {
        config_.replace_duplicate_login = bool_value ? 1u : 0u;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gatehub config path=gatehub.replace_duplicate_login status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "gatehub.reconnect_grace_s", &value);
    if (rc == ABE_CONFIG_OK && value <= 3600u) {
        config_.reconnect_grace_s = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gatehub config path=gatehub.reconnect_grace_s status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, "gatehub.session_ttl_s", &value);
    if (rc == ABE_CONFIG_OK && value >= 1u && value <= 604800u) {
        config_.session_ttl_s = value;
    } else if (rc != ABE_CONFIG_NOT_FOUND) {
        ABE_LOG_ERROR("invalid gatehub config path=gatehub.session_ttl_s status=%s",
            abe_status_name(abe::service::common::SERVICE_STATUS_INVALID_ARG));
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    return abe::service::common::SERVICE_STATUS_OK;
}

int GateHubServer::init(abe::service::common::Context& context)
{
    int rc;

    if (context.id_generator == NULL) {
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    rc = registry_.init(config_, context.id_generator);
    if (rc != proto::ERROR_CODE_OK) {
        ABE_LOG_ERROR("gatehub registry init failed status=%d", rc);
        return rc == proto::ERROR_CODE_COMMON_INVALID_ARGUMENT
            ? abe::service::common::SERVICE_STATUS_INVALID_ARG
            : abe::service::common::SERVICE_STATUS_FAILED;
    }

    initialized_ = 1;
    ABE_LOG_INFO(
        "gatehub ready max_sessions=%u reconnect=%u replace_duplicate=%u",
        config_.max_sessions,
        config_.allow_reconnect,
        config_.replace_duplicate_login);
    return abe::service::common::SERVICE_STATUS_OK;
}

int GateHubServer::update(uint64_t now_ms)
{
    uint32_t closed_count;
    int rc;

    if (!initialized_) {
        return abe::service::common::SERVICE_STATUS_INVALID_ARG;
    }

    closed_count = 0u;
    rc = registry_.update(now_ms, &closed_count);
    if (rc != proto::ERROR_CODE_OK) {
        ABE_LOG_ERROR("gatehub registry update failed status=%d", rc);
        return abe::service::common::SERVICE_STATUS_FAILED;
    }
    if (closed_count != 0u) {
        ABE_LOG_DEBUG("gatehub expired sessions count=%u", closed_count);
    }
    return abe::service::common::SERVICE_STATUS_OK;
}

void GateHubServer::close(uint64_t now_ms)
{
    (void)now_ms;
    registry_.close();
    initialized_ = 0;
}

int GateHubServer::handle_enter_lobby(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_ENTER_LOBBY_REQ& request,
    proto::PB_SC_ENTER_LOBBY_RESP* out_response,
    uint64_t now_ms)
{
    GateHubSessionInfo session;
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    out_response->Clear();
    fill_response_header(
        out_response->mutable_header(),
        request.header(),
        proto::SC_ENTER_LOBBY_RESP);

    rc = validate_handler_context(initialized_, gateway_id, connection_id, now_ms);
    if (rc == proto::ERROR_CODE_OK && request.uid() == 0u) {
        rc = proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    if (rc == proto::ERROR_CODE_OK) {
        rc = registry_.find_session(request.uid(), &session);
    }
    if (rc == proto::ERROR_CODE_OK &&
        (session.gateway_id != gateway_id ||
         session.connection_id != connection_id ||
         session.state != GATEHUB_SESSION_ONLINE)) {
        rc = proto::ERROR_CODE_SESSION_CLOSED;
    }

    set_result_status(out_response->mutable_result(), rc);
    if (rc == proto::ERROR_CODE_OK) {
        out_response->mutable_player()->set_uid(request.uid());
        out_response->set_lobby_time_ms(now_ms);
    }
    return rc;
}

int GateHubServer::handle_room_list(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_ROOM_LIST_REQ& request,
    proto::PB_SC_ROOM_LIST_RESP* out_response,
    uint64_t now_ms)
{
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    out_response->Clear();
    fill_response_header(
        out_response->mutable_header(),
        request.header(),
        proto::SC_ROOM_LIST_RESP);

    rc = validate_handler_context(initialized_, gateway_id, connection_id, now_ms);
    if (rc == proto::ERROR_CODE_OK) {
        rc = proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    set_result_status(out_response->mutable_result(), rc);
    return rc;
}

int GateHubServer::handle_create_room(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_CREATE_ROOM_REQ& request,
    proto::PB_SC_CREATE_ROOM_RESP* out_response,
    uint64_t now_ms)
{
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    out_response->Clear();
    fill_response_header(
        out_response->mutable_header(),
        request.header(),
        proto::SC_CREATE_ROOM_RESP);

    rc = validate_handler_context(initialized_, gateway_id, connection_id, now_ms);
    if (rc == proto::ERROR_CODE_OK) {
        rc = proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    set_result_status(out_response->mutable_result(), rc);
    return rc;
}

int GateHubServer::handle_join_room(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_JOIN_ROOM_REQ& request,
    proto::PB_SC_JOIN_ROOM_RESP* out_response,
    uint64_t now_ms)
{
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    out_response->Clear();
    fill_response_header(
        out_response->mutable_header(),
        request.header(),
        proto::SC_JOIN_ROOM_RESP);

    rc = validate_handler_context(initialized_, gateway_id, connection_id, now_ms);
    if (rc == proto::ERROR_CODE_OK) {
        rc = proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    set_result_status(out_response->mutable_result(), rc);
    return rc;
}

int GateHubServer::handle_update_room_state(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_UPDATE_ROOM_STATE_REQ& request,
    proto::PB_SC_UPDATE_ROOM_STATE_RESP* out_response,
    uint64_t now_ms)
{
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    out_response->Clear();
    fill_response_header(
        out_response->mutable_header(),
        request.header(),
        proto::SC_UPDATE_ROOM_STATE_RESP);

    rc = validate_handler_context(initialized_, gateway_id, connection_id, now_ms);
    if (rc == proto::ERROR_CODE_OK) {
        rc = proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    set_result_status(out_response->mutable_result(), rc);
    return rc;
}

int GateHubServer::handle_fetch_room_archive(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_FETCH_ROOM_ARCHIVE_REQ& request,
    proto::PB_SC_FETCH_ROOM_ARCHIVE_RESP* out_response,
    uint64_t now_ms)
{
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    out_response->Clear();
    fill_response_header(
        out_response->mutable_header(),
        request.header(),
        proto::SC_FETCH_ROOM_ARCHIVE_RESP);

    rc = validate_handler_context(initialized_, gateway_id, connection_id, now_ms);
    if (rc == proto::ERROR_CODE_OK) {
        rc = proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    set_result_status(out_response->mutable_result(), rc);
    return rc;
}

int GateHubServer::handle_lobby_chat(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_LOBBY_CHAT_REQ& request,
    proto::PB_SC_ERROR_NOTIFY* out_response,
    uint64_t now_ms)
{
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    rc = validate_handler_context(initialized_, gateway_id, connection_id, now_ms);
    if (rc == proto::ERROR_CODE_OK) {
        rc = proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    fill_error_notify(out_response, request.header(), rc);
    return rc;
}

int GateHubServer::handle_enter_game(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_ENTER_GAME_REQ& request,
    proto::PB_SC_ENTER_GAME_RESP* out_response,
    uint64_t now_ms)
{
    GateHubSessionInfo session;
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    out_response->Clear();
    fill_response_header(
        out_response->mutable_header(),
        request.header(),
        proto::SC_ENTER_GAME_RESP);

    rc = validate_handler_context(initialized_, gateway_id, connection_id, now_ms);
    if (rc == proto::ERROR_CODE_OK) {
        rc = validate_online_session(
            registry_,
            gateway_id,
            connection_id,
            request.uid(),
            &session);
    }
    if (rc == proto::ERROR_CODE_OK && request.session_token().empty()) {
        rc = proto::ERROR_CODE_AUTH_FAILED;
    }
    if (rc == proto::ERROR_CODE_OK &&
        !token_equal(request.session_token().c_str(), session.session_token)) {
        rc = proto::ERROR_CODE_AUTH_FAILED;
    }
    if (rc == proto::ERROR_CODE_OK && request.room().room_id() == 0u) {
        rc = proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    set_result_status(out_response->mutable_result(), rc);
    if (rc == proto::ERROR_CODE_OK) {
        out_response->mutable_room()->CopyFrom(request.room());
        out_response->set_game_start_time_ms(now_ms);
        out_response->set_tick_rate(30u);
    }
    return rc;
}

int GateHubServer::handle_leave_game(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_LEAVE_GAME_REQ& request,
    proto::PB_SC_LEAVE_GAME_RESP* out_response,
    uint64_t now_ms)
{
    uint64_t uid;
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    out_response->Clear();
    fill_response_header(
        out_response->mutable_header(),
        request.header(),
        proto::SC_LEAVE_GAME_RESP);

    rc = validate_handler_context(initialized_, gateway_id, connection_id, now_ms);
    if (rc == proto::ERROR_CODE_OK) {
        uid = request.header().uid();
        rc = validate_online_session(
            registry_,
            gateway_id,
            connection_id,
            uid,
            NULL);
    }
    if (rc == proto::ERROR_CODE_OK && request.room().room_id() == 0u) {
        rc = proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    set_result_status(out_response->mutable_result(), rc);
    if (rc == proto::ERROR_CODE_OK) {
        out_response->mutable_room()->CopyFrom(request.room());
        out_response->set_reason(request.reason());
    }
    return rc;
}

int GateHubServer::handle_game_action(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_GAME_ACTION_REQ& request,
    proto::PB_SC_ERROR_NOTIFY* out_response,
    uint64_t now_ms)
{
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    rc = validate_handler_context(initialized_, gateway_id, connection_id, now_ms);
    if (rc == proto::ERROR_CODE_OK) {
        rc = proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    fill_error_notify(out_response, request.header(), rc);
    return rc;
}

int GateHubServer::handle_room_chat(
    uint64_t gateway_id,
    uint64_t connection_id,
    const proto::PB_CS_ROOM_CHAT_REQ& request,
    proto::PB_SC_ERROR_NOTIFY* out_response,
    uint64_t now_ms)
{
    int rc;

    if (out_response == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    rc = validate_handler_context(initialized_, gateway_id, connection_id, now_ms);
    if (rc == proto::ERROR_CODE_OK) {
        rc = proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    fill_error_notify(out_response, request.header(), rc);
    return rc;
}

GateHubRegistry* GateHubServer::registry()
{
    return &registry_;
}

const GateHubRegistry* GateHubServer::registry() const
{
    return &registry_;
}

int GateHubServer::initialized() const
{
    return initialized_;
}

} /* namespace gatehub */
} /* namespace service */
} /* namespace abe */
