#include "abe_session_server.h"

#include <stddef.h>

namespace abe {
namespace logic {
namespace session {

SessionServer::SessionServer()
    : sessions_(NULL),
      session_count_(0u),
      active_count_(0u),
      server_id_(0u),
      idle_timeout_ms_(0u),
      initialized_(0)
{
}

int SessionServer::init(const SessionServerConfig& config)
{
    uint32_t index;

    if (config.server_id == 0u || config.sessions == NULL || config.session_count == 0u) {
        return SESSION_STATUS_INVALID_ARG;
    }

    sessions_ = config.sessions;
    session_count_ = config.session_count;
    active_count_ = 0u;
    server_id_ = config.server_id;
    idle_timeout_ms_ = config.idle_timeout_ms;
    initialized_ = 1;

    index = 0u;
    while (index < session_count_) {
        sessions_[index].reset();
        ++index;
    }
    return SESSION_STATUS_OK;
}

void SessionServer::close(uint64_t now_ms)
{
    uint32_t index;

    index = 0u;
    while (index < session_count_) {
        if (sessions_[index].active()) {
            sessions_[index].close(0u, now_ms);
        }
        sessions_[index].reset();
        ++index;
    }

    active_count_ = 0u;
    initialized_ = 0;
    sessions_ = NULL;
    session_count_ = 0u;
    server_id_ = 0u;
    idle_timeout_ms_ = 0u;
}

int SessionServer::update(uint64_t now_ms)
{
    uint32_t index;
    uint32_t closed_count;

    if (!initialized_) {
        return SESSION_STATUS_CLOSED;
    }
    if (idle_timeout_ms_ == 0u) {
        return 0;
    }

    closed_count = 0u;
    index = 0u;
    while (index < session_count_) {
        if (sessions_[index].active()) {
            uint64_t last_time;

            last_time = sessions_[index].last_recv_ms();
            if (sessions_[index].last_send_ms() > last_time) {
                last_time = sessions_[index].last_send_ms();
            }
            if (sessions_[index].connected_at_ms() > last_time) {
                last_time = sessions_[index].connected_at_ms();
            }

            if (now_ms > last_time && now_ms - last_time > idle_timeout_ms_) {
                sessions_[index].close(0u, now_ms);
                sessions_[index].reset();
                if (active_count_ > 0u) {
                    --active_count_;
                }
                ++closed_count;
            }
        }
        ++index;
    }

    return (int)closed_count;
}

Session* SessionServer::open_session(const SessionOpenRequest& request, int* out_status)
{
    Session* session;
    int status;

    if (out_status != NULL) {
        *out_status = SESSION_STATUS_OK;
    }
    if (!initialized_) {
        if (out_status != NULL) {
            *out_status = SESSION_STATUS_CLOSED;
        }
        return NULL;
    }
    if (request.link_id == 0u || request.conn_id == 0u) {
        if (out_status != NULL) {
            *out_status = SESSION_STATUS_INVALID_ARG;
        }
        return NULL;
    }
    if (find_session(request.link_id) != NULL) {
        if (out_status != NULL) {
            *out_status = SESSION_STATUS_ALREADY_EXISTS;
        }
        return NULL;
    }

    session = find_free_session();
    if (session == NULL) {
        if (out_status != NULL) {
            *out_status = SESSION_STATUS_NO_SLOT;
        }
        return NULL;
    }

    status = session->open(server_id_, request);
    if (status != SESSION_STATUS_OK) {
        if (out_status != NULL) {
            *out_status = status;
        }
        return NULL;
    }

    ++active_count_;
    return session;
}

int SessionServer::close_session(uint64_t link_id, uint32_t reason, uint64_t now_ms)
{
    Session* session;

    if (!initialized_) {
        return SESSION_STATUS_CLOSED;
    }

    session = find_session(link_id);
    if (session == NULL) {
        return SESSION_STATUS_NOT_FOUND;
    }

    session->close(reason, now_ms);
    session->reset();
    if (active_count_ > 0u) {
        --active_count_;
    }
    return SESSION_STATUS_OK;
}

int SessionServer::handle_message(
    uint64_t link_id,
    uint32_t message_id,
    const void* data,
    uint32_t size,
    uint64_t now_ms)
{
    Session* session;

    if (!initialized_) {
        return SESSION_STATUS_CLOSED;
    }

    session = find_session(link_id);
    if (session == NULL) {
        return SESSION_STATUS_NOT_FOUND;
    }

    return session->handle_message(message_id, data, size, now_ms);
}

Session* SessionServer::find_session(uint64_t link_id)
{
    uint32_t index;

    if (link_id == 0u || sessions_ == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < session_count_) {
        if (sessions_[index].active() && sessions_[index].link_id() == link_id) {
            return &sessions_[index];
        }
        ++index;
    }
    return NULL;
}

const Session* SessionServer::find_session(uint64_t link_id) const
{
    uint32_t index;

    if (link_id == 0u || sessions_ == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < session_count_) {
        if (sessions_[index].active() && sessions_[index].link_id() == link_id) {
            return &sessions_[index];
        }
        ++index;
    }
    return NULL;
}

Session* SessionServer::find_session_by_uid(uint64_t uid)
{
    uint32_t index;

    if (uid == 0u || sessions_ == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < session_count_) {
        if (sessions_[index].active() && sessions_[index].uid() == uid) {
            return &sessions_[index];
        }
        ++index;
    }
    return NULL;
}

const Session* SessionServer::find_session_by_uid(uint64_t uid) const
{
    uint32_t index;

    if (uid == 0u || sessions_ == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < session_count_) {
        if (sessions_[index].active() && sessions_[index].uid() == uid) {
            return &sessions_[index];
        }
        ++index;
    }
    return NULL;
}

uint64_t SessionServer::server_id() const
{
    return server_id_;
}

uint32_t SessionServer::active_count() const
{
    return active_count_;
}

uint32_t SessionServer::capacity() const
{
    return session_count_;
}

int SessionServer::initialized() const
{
    return initialized_;
}

Session* SessionServer::find_free_session()
{
    uint32_t index;

    if (sessions_ == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < session_count_) {
        if (!sessions_[index].active()) {
            return &sessions_[index];
        }
        ++index;
    }
    return NULL;
}

} /* namespace session */
} /* namespace logic */
} /* namespace abe */
