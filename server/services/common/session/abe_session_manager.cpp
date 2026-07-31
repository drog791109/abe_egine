#include "abe_session_manager.h"

#include "abe_log.h"
#include "protocol.pb.h"

#include <stddef.h>

namespace abe {
namespace service {
namespace session {

namespace proto = ::abe::proto::client;

SessionManager::SessionManager()
    : session_storage_(NULL),
      session_at_(NULL),
      session_at_const_(NULL),
      destroy_storage_(NULL),
      session_count_(0u),
      active_count_(0u),
      server_id_(0u),
      idle_timeout_ms_(0u),
      initialized_(0)
{
}

SessionManager::~SessionManager()
{
    close(0u);
}

void SessionManager::close(uint64_t now_ms)
{
    if (session_storage_ != NULL) {
        reset_sessions(now_ms);
        destroy_session_storage();
    }
    reset_state();
}

int SessionManager::update(uint64_t now_ms, uint32_t* out_closed_count)
{
    uint32_t index;
    uint32_t closed_count;

    if (out_closed_count != NULL) {
        *out_closed_count = 0u;
    }
    if (!initialized_ || session_storage_ == NULL) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }
    if (idle_timeout_ms_ == 0u) {
        return proto::ERROR_CODE_OK;
    }

    closed_count = 0u;
    index = 0u;
    while (index < session_count_) {
        Session* session;

        session = session_at(index);
        if (session != NULL && session->active()) {
            if (session->is_timeout(now_ms, idle_timeout_ms_)) {
                release_session_slot(index, session, 0u, now_ms);
                ++closed_count;
            }
        }
        ++index;
    }

    if (out_closed_count != NULL) {
        *out_closed_count = closed_count;
    }
    return proto::ERROR_CODE_OK;
}

Session* SessionManager::open_session(const SessionOpenRequest& request, int* out_status)
{
    Session* session;
    uint32_t index;
    int status;

    if (out_status != NULL) {
        *out_status = proto::ERROR_CODE_OK;
    }
    if (!initialized_ || session_storage_ == NULL) {
        if (out_status != NULL) {
            *out_status = proto::ERROR_CODE_SESSION_CLOSED;
        }
        return NULL;
    }
    if (request.conn_id == 0u) {
        if (out_status != NULL) {
            *out_status = proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
        }
        return NULL;
    }
    if (conn_index_.find(request.conn_id) != conn_index_.end()) {
        if (out_status != NULL) {
            *out_status = proto::ERROR_CODE_SESSION_ALREADY_EXISTS;
        }
        return NULL;
    }

    index = 0u;
    session = find_free_session(&index);
    if (session == NULL) {
        if (out_status != NULL) {
            *out_status = proto::ERROR_CODE_SESSION_NO_SLOT;
        }
        return NULL;
    }

    status = session->open(server_id_, request);
    if (status != proto::ERROR_CODE_OK) {
        if (out_status != NULL) {
            *out_status = status;
        }
        return NULL;
    }

    try {
        conn_index_[request.conn_id] = index;
    } catch (...) {
        session->close(0u, request.now_ms);
        session->reset();
        if (out_status != NULL) {
            *out_status = proto::ERROR_CODE_SYSTEM_INTERNAL;
        }
        return NULL;
    }

    free_slots_.pop_back();
    ++active_count_;
    return session;
}

int SessionManager::close_session(uint64_t conn_id, uint32_t reason, uint64_t now_ms)
{
    std::unordered_map<uint64_t, uint32_t>::iterator found;
    Session* session;
    uint32_t index;

    if (!initialized_ || session_storage_ == NULL) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }

    found = conn_index_.find(conn_id);
    if (found == conn_index_.end()) {
        return proto::ERROR_CODE_SESSION_NOT_FOUND;
    }

    index = found->second;
    session = find_session_by_index(index);
    if (session == NULL || session->conn_id() != conn_id) {
        conn_index_.erase(found);
        return proto::ERROR_CODE_SESSION_NOT_FOUND;
    }

    release_session_slot(index, session, reason, now_ms);
    return proto::ERROR_CODE_OK;
}

int SessionManager::handle_message(
    uint64_t conn_id,
    const void* data,
    uint32_t size,
    uint64_t now_ms)
{
    Session* session;

    if (!initialized_ || session_storage_ == NULL) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }

    session = find_session(conn_id);
    if (session == NULL) {
        return proto::ERROR_CODE_SESSION_NOT_FOUND;
    }

    return session->receive(data, size, now_ms);
}

Session* SessionManager::find_session(uint64_t conn_id)
{
    std::unordered_map<uint64_t, uint32_t>::iterator found;
    Session* session;

    if (conn_id == 0u || session_storage_ == NULL) {
        return NULL;
    }

    found = conn_index_.find(conn_id);
    if (found == conn_index_.end()) {
        return NULL;
    }
    session = find_session_by_index(found->second);
    if (session == NULL || session->conn_id() != conn_id) {
        conn_index_.erase(found);
        return NULL;
    }
    return session;
}

const Session* SessionManager::find_session(uint64_t conn_id) const
{
    std::unordered_map<uint64_t, uint32_t>::const_iterator found;
    const Session* session;

    if (conn_id == 0u || session_storage_ == NULL) {
        return NULL;
    }

    found = conn_index_.find(conn_id);
    if (found == conn_index_.end()) {
        return NULL;
    }
    session = find_session_by_index(found->second);
    if (session == NULL || session->conn_id() != conn_id) {
        return NULL;
    }
    return session;
}

Session* SessionManager::find_session_by_user_id(uint64_t user_id)
{
    std::unordered_map<uint64_t, uint32_t>::iterator found;
    Session* session;

    if (user_id == 0u || session_storage_ == NULL) {
        return NULL;
    }

    found = user_index_.find(user_id);
    if (found == user_index_.end()) {
        return NULL;
    }
    session = find_session_by_index(found->second);
    if (session == NULL || session->user_id() != user_id) {
        user_index_.erase(found);
        return NULL;
    }
    return session;
}

const Session* SessionManager::find_session_by_user_id(uint64_t user_id) const
{
    std::unordered_map<uint64_t, uint32_t>::const_iterator found;
    const Session* session;

    if (user_id == 0u || session_storage_ == NULL) {
        return NULL;
    }

    found = user_index_.find(user_id);
    if (found == user_index_.end()) {
        return NULL;
    }
    session = find_session_by_index(found->second);
    if (session == NULL || session->user_id() != user_id) {
        return NULL;
    }
    return session;
}

Session* SessionManager::peek_free_session()
{
    return find_free_session(NULL);
}

const Session* SessionManager::peek_free_session() const
{
    uint32_t index;
    const Session* session;

    if (session_storage_ == NULL || free_slots_.empty()) {
        return NULL;
    }

    index = free_slots_.back();
    session = session_at(index);
    if (session == NULL || session->active()) {
        return NULL;
    }
    return session;
}

uint64_t SessionManager::server_id() const
{
    return server_id_;
}

uint32_t SessionManager::active_count() const
{
    return active_count_;
}

uint32_t SessionManager::capacity() const
{
    return session_count_;
}

int SessionManager::initialized() const
{
    return initialized_;
}

int SessionManager::init_created_sessions(
    uint64_t server_id,
    uint32_t session_count,
    uint64_t idle_timeout_ms)
{
    uint32_t index;
    int rc;

    rc = validate_config(server_id, session_count);
    if (rc != proto::ERROR_CODE_OK) {
        return rc;
    }
    if (session_storage_ == NULL ||
        session_at_ == NULL ||
        session_at_const_ == NULL ||
        destroy_storage_ == NULL) {
        return proto::ERROR_CODE_SYSTEM_INTERNAL;
    }

    try {
        free_slots_.clear();
        conn_index_.clear();
        user_index_.clear();
        session_slot_index_.clear();
        free_slots_.reserve(session_count);
        conn_index_.reserve(session_count);
        user_index_.reserve(session_count);
        session_slot_index_.reserve(session_count);
    } catch (...) {
        return storage_status_to_proto(ABE_NO_MEMORY);
    }

    session_count_ = session_count;
    active_count_ = 0u;
    server_id_ = server_id;
    idle_timeout_ms_ = idle_timeout_ms;

    index = 0u;
    while (index < session_count_) {
        Session* session;

        session = session_at(index);
        if (session == NULL) {
            return proto::ERROR_CODE_SYSTEM_INTERNAL;
        }
        session->reset();
        session->set_auth_change_callback(
            SessionManager::on_session_auth_changed,
            this);
        try {
            session_slot_index_[session] = index;
        } catch (...) {
            return storage_status_to_proto(ABE_NO_MEMORY);
        }
        ++index;
    }

    index = session_count;
    while (index > 0u) {
        --index;
        try {
            free_slots_.push_back(index);
        } catch (...) {
            return storage_status_to_proto(ABE_NO_MEMORY);
        }
    }

    initialized_ = 1;
    return proto::ERROR_CODE_OK;
}

int SessionManager::validate_config(uint64_t server_id, uint32_t session_count) const
{
    if (server_id == 0u || session_count == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    return proto::ERROR_CODE_OK;
}

void SessionManager::reset_state()
{
    session_count_ = 0u;
    active_count_ = 0u;
    server_id_ = 0u;
    idle_timeout_ms_ = 0u;
    free_slots_.clear();
    conn_index_.clear();
    user_index_.clear();
    session_slot_index_.clear();
    initialized_ = 0;
}

void SessionManager::destroy_session_storage()
{
    if (session_storage_ != NULL && destroy_storage_ != NULL) {
        destroy_storage_(session_storage_);
    }
    session_storage_ = NULL;
    session_at_ = NULL;
    session_at_const_ = NULL;
    destroy_storage_ = NULL;
}

Session* SessionManager::session_at(uint32_t index)
{
    if (session_storage_ == NULL || session_at_ == NULL || index >= session_count_) {
        return NULL;
    }
    return session_at_(session_storage_, index);
}

const Session* SessionManager::session_at(uint32_t index) const
{
    if (session_storage_ == NULL ||
        session_at_const_ == NULL ||
        index >= session_count_) {
        return NULL;
    }
    return session_at_const_(session_storage_, index);
}

Session* SessionManager::find_session_by_index(uint32_t index)
{
    Session* session;

    session = session_at(index);
    if (session == NULL || !session->active()) {
        return NULL;
    }
    return session;
}

const Session* SessionManager::find_session_by_index(uint32_t index) const
{
    const Session* session;

    session = session_at(index);
    if (session == NULL || !session->active()) {
        return NULL;
    }
    return session;
}

Session* SessionManager::find_free_session(uint32_t* out_index)
{
    uint32_t index;
    Session* session;

    if (out_index != NULL) {
        *out_index = 0u;
    }
    if (session_storage_ == NULL || free_slots_.empty()) {
        return NULL;
    }

    index = free_slots_.back();
    session = session_at(index);
    if (session == NULL || session->active()) {
        return NULL;
    }

    if (out_index != NULL) {
        *out_index = index;
    }
    return session;
}

void SessionManager::release_session_slot(
    uint32_t index,
    Session* session,
    uint32_t reason,
    uint64_t now_ms)
{
    if (session == NULL || index >= session_count_) {
        return;
    }

    remove_session_indexes(index, session);
    if (session->active()) {
        session->close(reason, now_ms);
    }
    session->reset();
    if (active_count_ > 0u) {
        --active_count_;
    }
    free_slots_.push_back(index);
}

void SessionManager::remove_session_indexes(uint32_t index, Session* session)
{
    std::unordered_map<uint64_t, uint32_t>::iterator conn_it;
    std::unordered_map<uint64_t, uint32_t>::iterator user_it;

    if (session == NULL) {
        return;
    }

    if (session->conn_id() != 0u) {
        conn_it = conn_index_.find(session->conn_id());
        if (conn_it != conn_index_.end() && conn_it->second == index) {
            conn_index_.erase(conn_it);
        }
    }

    if (session->user_id() != 0u) {
        user_it = user_index_.find(session->user_id());
        if (user_it != user_index_.end() && user_it->second == index) {
            user_index_.erase(user_it);
        }
    }
}

void SessionManager::update_session_user_index(
    Session* session,
    uint64_t old_user_id,
    uint64_t new_user_id)
{
    std::unordered_map<Session*, uint32_t>::iterator slot_it;
    std::unordered_map<uint64_t, uint32_t>::iterator user_it;
    uint32_t index;

    if (session == NULL) {
        return;
    }

    slot_it = session_slot_index_.find(session);
    if (slot_it == session_slot_index_.end()) {
        return;
    }
    index = slot_it->second;

    if (old_user_id != 0u) {
        user_it = user_index_.find(old_user_id);
        if (user_it != user_index_.end() && user_it->second == index) {
            user_index_.erase(user_it);
        }
    }

    if (new_user_id != 0u && session->active()) {
        try {
            user_index_[new_user_id] = index;
        } catch (...) {
            ABE_LOG_ERROR(
                "session user index update failed user_id=%llu index=%u",
                (unsigned long long)new_user_id,
                index);
        }
    }
}

void SessionManager::reset_sessions(uint64_t now_ms)
{
    uint32_t index;

    conn_index_.clear();
    user_index_.clear();
    free_slots_.clear();

    index = 0u;
    while (index < session_count_) {
        Session* session;

        session = session_at(index);
        if (session != NULL) {
            if (session->active()) {
                session->close(0u, now_ms);
            }
            session->reset();
        }
        ++index;
    }
    index = session_count_;
    while (index > 0u) {
        --index;
        try {
            free_slots_.push_back(index);
        } catch (...) {
            ABE_LOG_ERROR("session free slot reset failed index=%u", index);
        }
    }
    active_count_ = 0u;
    initialized_ = 0;
}

void SessionManager::on_session_auth_changed(
    Session* session,
    uint64_t old_user_id,
    uint64_t new_user_id,
    void* user_data)
{
    SessionManager* manager;

    manager = (SessionManager*)user_data;
    if (manager != NULL) {
        manager->update_session_user_index(
            session,
            old_user_id,
            new_user_id);
    }
}

int SessionManager::storage_status_to_proto(int status)
{
    if (status == ABE_OK) {
        return proto::ERROR_CODE_OK;
    }
    if (status == ABE_INVALID_ARG ||
        status == ABE_TYPE_MISMATCH ||
        status == ABE_OUT_OF_RANGE) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    if (status == ABE_NO_SLOT) {
        return proto::ERROR_CODE_SESSION_NO_SLOT;
    }
    return proto::ERROR_CODE_SYSTEM_INTERNAL;
}

} /* namespace session */
} /* namespace service */
} /* namespace abe */
