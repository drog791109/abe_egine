#include "abe_session_manager.h"

#include <stdio.h>

namespace abe {
namespace logic {
namespace session {

SessionManager::SessionManager()
    : next_session_id_(1u)
{
    config_.session_ttl_ms = 30u * 60u * 1000u;
    config_.reconnect_ttl_ms = 60u * 1000u;
}

void SessionManager::init(const SessionConfig& config)
{
    config_ = config;
    if (config_.session_ttl_ms == 0u) {
        config_.session_ttl_ms = 30u * 60u * 1000u;
    }
    if (config_.reconnect_ttl_ms == 0u) {
        config_.reconnect_ttl_ms = 60u * 1000u;
    }
}

void SessionManager::clear()
{
    sessions_.clear();
    next_session_id_ = 1u;
}

int SessionManager::login(const LoginRequest& request, SessionInfo* out_session, SessionInfo* out_kicked)
{
    Record record;
    SessionMap::iterator it;

    if (out_session != NULL) {
        *out_session = SessionInfo();
    }
    if (out_kicked != NULL) {
        *out_kicked = SessionInfo();
    }
    if (request.uid == 0u || request.gateway_id == 0u || request.conn_id == 0u) {
        return SESSION_STATUS_INVALID_ARG;
    }

    it = sessions_.find(request.uid);
    if (it != sessions_.end()) {
        fill_info(it->second, out_kicked);
    }

    record.uid = request.uid;
    record.session_id = next_session_id();
    record.gateway_id = request.gateway_id;
    record.conn_id = request.conn_id;
    record.room_id = it == sessions_.end() ? 0u : it->second.room_id;
    record.state = SESSION_STATE_ONLINE;
    record.login_time_ms = request.now_ms;
    record.expire_time_ms = request.now_ms + config_.session_ttl_ms;
    record.last_heartbeat_ms = request.now_ms;
    record.reconnect_deadline_ms = 0u;
    record.login_token = request.login_token == NULL ? "" : request.login_token;
    record.device_id = request.device_id == NULL ? "" : request.device_id;
    make_session_token(request, record.session_id, &record.session_token);

    sessions_[request.uid] = record;
    fill_info(sessions_[request.uid], out_session);
    return SESSION_STATUS_OK;
}

int SessionManager::heartbeat(
    uint64_t uid,
    const char* session_token,
    uint64_t now_ms,
    SessionInfo* out_session)
{
    SessionMap::iterator it;
    int status;

    it = sessions_.find(uid);
    if (it == sessions_.end()) {
        return SESSION_STATUS_NOT_FOUND;
    }

    status = validate_token(it->second, session_token, now_ms);
    if (status != SESSION_STATUS_OK) {
        return status;
    }

    it->second.state = SESSION_STATE_ONLINE;
    it->second.last_heartbeat_ms = now_ms;
    it->second.expire_time_ms = now_ms + config_.session_ttl_ms;
    it->second.reconnect_deadline_ms = 0u;
    fill_info(it->second, out_session);
    return SESSION_STATUS_OK;
}

int SessionManager::disconnect(
    uint64_t uid,
    uint64_t conn_id,
    uint64_t now_ms,
    SessionInfo* out_session)
{
    SessionMap::iterator it;

    it = sessions_.find(uid);
    if (it == sessions_.end()) {
        return SESSION_STATUS_NOT_FOUND;
    }
    if (conn_id != 0u && it->second.conn_id != conn_id) {
        return SESSION_STATUS_INVALID_ARG;
    }

    it->second.state = SESSION_STATE_RECONNECTING;
    it->second.gateway_id = 0u;
    it->second.conn_id = 0u;
    it->second.reconnect_deadline_ms = now_ms + config_.reconnect_ttl_ms;
    fill_info(it->second, out_session);
    return SESSION_STATUS_OK;
}

int SessionManager::reconnect(const LoginRequest& request, SessionInfo* out_session)
{
    SessionMap::iterator it;

    if (request.uid == 0u || request.gateway_id == 0u || request.conn_id == 0u) {
        return SESSION_STATUS_INVALID_ARG;
    }

    it = sessions_.find(request.uid);
    if (it == sessions_.end()) {
        return login(request, out_session, NULL);
    }
    if (it->second.state == SESSION_STATE_RECONNECTING &&
        request.now_ms > it->second.reconnect_deadline_ms) {
        sessions_.erase(it);
        return SESSION_STATUS_EXPIRED;
    }

    it->second.session_id = next_session_id();
    it->second.gateway_id = request.gateway_id;
    it->second.conn_id = request.conn_id;
    it->second.state = SESSION_STATE_ONLINE;
    it->second.last_heartbeat_ms = request.now_ms;
    it->second.expire_time_ms = request.now_ms + config_.session_ttl_ms;
    it->second.reconnect_deadline_ms = 0u;
    if (request.login_token != NULL) {
        it->second.login_token = request.login_token;
    }
    if (request.device_id != NULL) {
        it->second.device_id = request.device_id;
    }
    make_session_token(request, it->second.session_id, &it->second.session_token);
    fill_info(it->second, out_session);
    return SESSION_STATUS_OK;
}

int SessionManager::kick(uint64_t uid, uint64_t now_ms, SessionInfo* out_session)
{
    SessionMap::iterator it;

    (void)now_ms;
    it = sessions_.find(uid);
    if (it == sessions_.end()) {
        return SESSION_STATUS_NOT_FOUND;
    }

    fill_info(it->second, out_session);
    sessions_.erase(it);
    return SESSION_STATUS_OK;
}

int SessionManager::bind_room(uint64_t uid, uint64_t room_id, SessionInfo* out_session)
{
    SessionMap::iterator it;

    if (uid == 0u || room_id == 0u) {
        return SESSION_STATUS_INVALID_ARG;
    }
    it = sessions_.find(uid);
    if (it == sessions_.end()) {
        return SESSION_STATUS_NOT_FOUND;
    }
    it->second.room_id = room_id;
    fill_info(it->second, out_session);
    return SESSION_STATUS_OK;
}

int SessionManager::clear_room(uint64_t uid, uint64_t room_id, SessionInfo* out_session)
{
    SessionMap::iterator it;

    it = sessions_.find(uid);
    if (it == sessions_.end()) {
        return SESSION_STATUS_NOT_FOUND;
    }
    if (room_id != 0u && it->second.room_id != room_id) {
        return SESSION_STATUS_INVALID_ARG;
    }
    it->second.room_id = 0u;
    fill_info(it->second, out_session);
    return SESSION_STATUS_OK;
}

int SessionManager::find(uint64_t uid, SessionInfo* out_session) const
{
    SessionMap::const_iterator it;

    it = sessions_.find(uid);
    if (it == sessions_.end()) {
        return SESSION_STATUS_NOT_FOUND;
    }
    fill_info(it->second, out_session);
    return SESSION_STATUS_OK;
}

int SessionManager::remove_expired(uint64_t now_ms)
{
    SessionMap::iterator it;
    int removed;

    removed = 0;
    it = sessions_.begin();
    while (it != sessions_.end()) {
        if (now_ms > it->second.expire_time_ms ||
            (it->second.state == SESSION_STATE_RECONNECTING &&
             now_ms > it->second.reconnect_deadline_ms)) {
            SessionMap::iterator current;

            current = it;
            ++it;
            sessions_.erase(current);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

uint32_t SessionManager::count() const
{
    return (uint32_t)sessions_.size();
}

int SessionManager::validate_token(const Record& record, const char* session_token, uint64_t now_ms) const
{
    if (session_token == NULL || record.session_token != session_token) {
        return SESSION_STATUS_TOKEN_MISMATCH;
    }
    if (now_ms > record.expire_time_ms) {
        return SESSION_STATUS_EXPIRED;
    }
    return SESSION_STATUS_OK;
}

void SessionManager::fill_info(const Record& record, SessionInfo* out_session) const
{
    if (out_session == NULL) {
        return;
    }

    out_session->uid = record.uid;
    out_session->session_id = record.session_id;
    out_session->gateway_id = record.gateway_id;
    out_session->conn_id = record.conn_id;
    out_session->room_id = record.room_id;
    out_session->state = record.state;
    out_session->login_time_ms = record.login_time_ms;
    out_session->expire_time_ms = record.expire_time_ms;
    out_session->last_heartbeat_ms = record.last_heartbeat_ms;
    out_session->reconnect_deadline_ms = record.reconnect_deadline_ms;
    out_session->session_token = record.session_token;
    out_session->login_token = record.login_token;
    out_session->device_id = record.device_id;
}

void SessionManager::make_session_token(
    const LoginRequest& request,
    uint64_t session_id,
    std::string* out_token) const
{
    char buffer[128];

    if (out_token == NULL) {
        return;
    }

    (void)snprintf(
        buffer,
        sizeof(buffer),
        "s-%llu-%llu-%llu",
        (unsigned long long)request.uid,
        (unsigned long long)session_id,
        (unsigned long long)request.now_ms);
    *out_token = buffer;
}

uint64_t SessionManager::next_session_id()
{
    uint64_t value;

    value = next_session_id_;
    ++next_session_id_;
    if (next_session_id_ == 0u) {
        next_session_id_ = 1u;
    }
    return value;
}

} /* namespace session */
} /* namespace logic */
} /* namespace abe */
