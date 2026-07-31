#ifndef ABE_SERVICE_SESSION_MANAGER_H
#define ABE_SERVICE_SESSION_MANAGER_H

#include "abe_error.h"
#include "abe_session.h"

#include <new>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace abe {
namespace service {
namespace session {

class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    template <typename T>
    int init(
        uint64_t server_id,
        uint32_t session_count,
        uint64_t idle_timeout_ms);
    void close(uint64_t now_ms);
    int update(uint64_t now_ms, uint32_t* out_closed_count);

    Session* open_session(const SessionOpenRequest& request, int* out_status);
    int close_session(uint64_t conn_id, uint32_t reason, uint64_t now_ms);
    int handle_message(
        uint64_t conn_id,
        const void* data,
        uint32_t size,
        uint64_t now_ms);

    Session* find_session(uint64_t conn_id);
    const Session* find_session(uint64_t conn_id) const;
    Session* find_session_by_user_id(uint64_t user_id);
    const Session* find_session_by_user_id(uint64_t user_id) const;
    Session* peek_free_session();
    const Session* peek_free_session() const;

    uint64_t server_id() const;
    uint32_t active_count() const;
    uint32_t capacity() const;
    int initialized() const;

private:
    SessionManager(const SessionManager&);
    SessionManager& operator=(const SessionManager&);

    int init_created_sessions(
        uint64_t server_id,
        uint32_t session_count,
        uint64_t idle_timeout_ms);
    int validate_config(uint64_t server_id, uint32_t session_count) const;
    void reset_state();
    void destroy_session_storage();
    Session* session_at(uint32_t index);
    const Session* session_at(uint32_t index) const;
    Session* find_session_by_index(uint32_t index);
    const Session* find_session_by_index(uint32_t index) const;
    Session* find_free_session(uint32_t* out_index);
    void release_session_slot(
        uint32_t index,
        Session* session,
        uint32_t reason,
        uint64_t now_ms);
    void remove_session_indexes(uint32_t index, Session* session);
    void update_session_user_index(
        Session* session,
        uint64_t old_user_id,
        uint64_t new_user_id);
    void reset_sessions(uint64_t now_ms);
    static int storage_status_to_proto(int status);
    static void on_session_auth_changed(
        Session* session,
        uint64_t old_user_id,
        uint64_t new_user_id,
        void* user_data);

    typedef Session* (*SessionAtFn)(void* storage, uint32_t index);
    typedef const Session* (*ConstSessionAtFn)(const void* storage, uint32_t index);
    typedef void (*SessionStorageDestroyFn)(void* storage);

    template <typename T>
    static Session* session_vector_at(void* storage, uint32_t index);
    template <typename T>
    static const Session* session_vector_at_const(const void* storage, uint32_t index);
    template <typename T>
    static void destroy_session_vector(void* storage);

    void* session_storage_;
    SessionAtFn session_at_;
    ConstSessionAtFn session_at_const_;
    SessionStorageDestroyFn destroy_storage_;
    uint32_t session_count_;
    uint32_t active_count_;
    uint64_t server_id_;
    uint64_t idle_timeout_ms_;
    std::vector<uint32_t> free_slots_;
    std::unordered_map<uint64_t, uint32_t> conn_index_;
    std::unordered_map<uint64_t, uint32_t> user_index_;
    std::unordered_map<Session*, uint32_t> session_slot_index_;
    int initialized_;
};

template <typename T>
int SessionManager::init(
    uint64_t server_id,
    uint32_t session_count,
    uint64_t idle_timeout_ms)
{
    typedef std::vector<T> SessionVector;
    SessionVector* sessions;
    int rc;

    if (!std::is_base_of<Session, T>::value) {
        return storage_status_to_proto(ABE_INVALID_ARG);
    }

    rc = validate_config(server_id, session_count);
    if (rc != 0) {
        return rc;
    }

    if (initialized_ || session_storage_ != NULL) {
        close(0u);
    }

    sessions = new (std::nothrow) SessionVector();
    if (sessions == NULL) {
        return storage_status_to_proto(ABE_NO_MEMORY);
    }

    try {
        sessions->resize((size_t)session_count);
    } catch (const std::bad_alloc&) {
        delete sessions;
        return storage_status_to_proto(ABE_NO_MEMORY);
    } catch (...) {
        delete sessions;
        return storage_status_to_proto(ABE_ERROR);
    }

    session_storage_ = sessions;
    session_at_ = &SessionManager::session_vector_at<T>;
    session_at_const_ = &SessionManager::session_vector_at_const<T>;
    destroy_storage_ = &SessionManager::destroy_session_vector<T>;

    rc = init_created_sessions(
        server_id,
        session_count,
        idle_timeout_ms);
    if (rc != 0) {
        destroy_session_storage();
        reset_state();
    }
    return rc;
}

template <typename T>
Session* SessionManager::session_vector_at(void* storage, uint32_t index)
{
    std::vector<T>* sessions;

    sessions = (std::vector<T>*)storage;
    if (sessions == NULL || index >= (uint32_t)sessions->size()) {
        return NULL;
    }
    return &(*sessions)[index];
}

template <typename T>
const Session* SessionManager::session_vector_at_const(
    const void* storage,
    uint32_t index)
{
    const std::vector<T>* sessions;

    sessions = (const std::vector<T>*)storage;
    if (sessions == NULL || index >= (uint32_t)sessions->size()) {
        return NULL;
    }
    return &(*sessions)[index];
}

template <typename T>
void SessionManager::destroy_session_vector(void* storage)
{
    delete (std::vector<T>*)storage;
}

} /* namespace session */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_SESSION_MANAGER_H */
