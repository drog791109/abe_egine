#include "abe_session_server.h"

#include "protocol.pb.h"

#include <stddef.h>

namespace abe {
namespace logic {
namespace session {

namespace proto = ::abe::proto::client;

SessionServer::SessionServer()
    : sessions_(NULL),
      session_count_(0u),
      session_size_(0u),
      active_count_(0u),
      server_id_(0u),
      idle_timeout_ms_(0u),
      initialized_(0)
{
}

int SessionServer::init(const SessionServerConfig& config)
{
    uint32_t index;

    if (config.server_id == 0u || config.sessions == NULL || config.session_count == 0u ||
        (config.session_size != 0u && config.session_size < (uint32_t)sizeof(Session))) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    sessions_ = config.sessions;
    session_count_ = config.session_count;
    session_size_ = config.session_size == 0u ? (uint32_t)sizeof(Session) : config.session_size;
    active_count_ = 0u;
    server_id_ = config.server_id;
    idle_timeout_ms_ = config.idle_timeout_ms;
    initialized_ = 1;

    index = 0u;
    while (index < session_count_) {
        session_at(index)->reset();
        ++index;
    }
    return proto::ERROR_CODE_OK;
}

void SessionServer::close(uint64_t now_ms)
{
    uint32_t index;

    index = 0u;
    while (index < session_count_) {
        Session* session;

        session = session_at(index);
        if (session->active()) {
            session->close(0u, now_ms);
        }
        session->reset();
        ++index;
    }

    active_count_ = 0u;
    initialized_ = 0;
    sessions_ = NULL;
    session_count_ = 0u;
    session_size_ = 0u;
    server_id_ = 0u;
    idle_timeout_ms_ = 0u;
}

int SessionServer::update(uint64_t now_ms, uint32_t* out_closed_count)
{
    uint32_t index;
    uint32_t closed_count;

    if (out_closed_count != NULL) {
        *out_closed_count = 0u;
    }
    if (!initialized_) {
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
        if (session->active()) {
            uint64_t last_time;

            last_time = session->last_recv_ms();
            if (session->last_send_ms() > last_time) {
                last_time = session->last_send_ms();
            }
            if (session->connected_at_ms() > last_time) {
                last_time = session->connected_at_ms();
            }

            if (now_ms > last_time && now_ms - last_time > idle_timeout_ms_) {
                session->close(0u, now_ms);
                session->reset();
                if (active_count_ > 0u) {
                    --active_count_;
                }
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

Session* SessionServer::open_session(const SessionOpenRequest& request, int* out_status)
{
    Session* session;
    int status;

    if (out_status != NULL) {
        *out_status = proto::ERROR_CODE_OK;
    }
    if (!initialized_) {
        if (out_status != NULL) {
            *out_status = proto::ERROR_CODE_SESSION_CLOSED;
        }
        return NULL;
    }
    if (request.link_id == 0u || request.conn_id == 0u) {
        if (out_status != NULL) {
            *out_status = proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
        }
        return NULL;
    }
    if (find_session(request.link_id) != NULL) {
        if (out_status != NULL) {
            *out_status = proto::ERROR_CODE_SESSION_ALREADY_EXISTS;
        }
        return NULL;
    }

    session = find_free_session();
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

    ++active_count_;
    return session;
}

int SessionServer::close_session(uint64_t link_id, uint32_t reason, uint64_t now_ms)
{
    Session* session;

    if (!initialized_) {
        return proto::ERROR_CODE_SESSION_CLOSED;
    }

    session = find_session(link_id);
    if (session == NULL) {
        return proto::ERROR_CODE_SESSION_NOT_FOUND;
    }

    session->close(reason, now_ms);
    session->reset();
    if (active_count_ > 0u) {
        --active_count_;
    }
    return proto::ERROR_CODE_OK;
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
        return proto::ERROR_CODE_SESSION_CLOSED;
    }

    session = find_session(link_id);
    if (session == NULL) {
        return proto::ERROR_CODE_SESSION_NOT_FOUND;
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
        Session* session;

        session = session_at(index);
        if (session->active() && session->link_id() == link_id) {
            return session;
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
        const Session* session;

        session = session_at(index);
        if (session->active() && session->link_id() == link_id) {
            return session;
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
        Session* session;

        session = session_at(index);
        if (session->active() && session->uid() == uid) {
            return session;
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
        const Session* session;

        session = session_at(index);
        if (session->active() && session->uid() == uid) {
            return session;
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

Session* SessionServer::session_at(uint32_t index)
{
    return (Session*)((char*)sessions_ + (uint64_t)index * (uint64_t)session_size_);
}

const Session* SessionServer::session_at(uint32_t index) const
{
    return (const Session*)((const char*)sessions_ + (uint64_t)index * (uint64_t)session_size_);
}

Session* SessionServer::find_free_session()
{
    uint32_t index;

    if (sessions_ == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < session_count_) {
        Session* session;

        session = session_at(index);
        if (!session->active()) {
            return session;
        }
        ++index;
    }
    return NULL;
}

} /* namespace session */
} /* namespace logic */
} /* namespace abe */
