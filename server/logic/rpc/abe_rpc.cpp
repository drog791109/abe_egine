#include "abe_rpc.h"

#include "abe_log.h"

#include <new>
#include <string.h>
#include <vector>

namespace abe {
namespace logic {
namespace rpc {

namespace {

enum {
    RPC_DEFAULT_TIMEOUT_MS = 5000u,
    RPC_MAX_HANDLER_COUNT = 256u
};

static void write_i32(unsigned char* out, int value)
{
    uint32_t data;

    data = (uint32_t)value;
    out[0] = (unsigned char)((data >> 24) & 0xffu);
    out[1] = (unsigned char)((data >> 16) & 0xffu);
    out[2] = (unsigned char)((data >> 8) & 0xffu);
    out[3] = (unsigned char)(data & 0xffu);
}

static int read_i32(const void* data, uint32_t size)
{
    const unsigned char* bytes;
    uint32_t value;

    if (data == NULL || size < 4u) {
        return ABE_ERROR;
    }

    bytes = (const unsigned char*)data;
    value = ((uint32_t)bytes[0] << 24) |
        ((uint32_t)bytes[1] << 16) |
        ((uint32_t)bytes[2] << 8) |
        (uint32_t)bytes[3];
    return (int)value;
}

} /* namespace */

struct RpcEndpoint::PendingCall {
    PendingCall()
        : used(0),
          completed(0),
          rpc_id(0u),
          expire_time_ms(0u),
          continuation(NULL),
          response()
    {
    }

    int used;
    int completed;
    uint32_t rpc_id;
    uint64_t expire_time_ms;
    std::coroutine_handle<> continuation;
    RpcResponse response;
};

struct RpcEndpoint::HandlerEntry {
    HandlerEntry()
        : msg_id(0u),
          handler(NULL),
          user_data(NULL)
    {
    }

    uint32_t msg_id;
    RpcHandler handler;
    void* user_data;
};

RpcResponse::RpcResponse()
    : status(ABE_ERROR),
      body()
{
    abe_msg_header_init(&header);
}

RpcCallAwaitable::RpcCallAwaitable()
    : endpoint_(NULL),
      rpc_id_(0u),
      immediate_response_()
{
    immediate_response_.status = ABE_CLOSED;
}

RpcCallAwaitable::RpcCallAwaitable(
    RpcEndpoint* endpoint,
    uint32_t rpc_id,
    const RpcResponse& immediate_response)
    : endpoint_(endpoint),
      rpc_id_(rpc_id),
      immediate_response_(immediate_response)
{
}

bool RpcCallAwaitable::await_ready() const
{
    if (endpoint_ == NULL || immediate_response_.status != ABE_WOULD_BLOCK) {
        return true;
    }
    return endpoint_->call_ready(rpc_id_);
}

bool RpcCallAwaitable::await_suspend(std::coroutine_handle<> continuation)
{
    if (endpoint_ == NULL) {
        return false;
    }
    return endpoint_->suspend_call(rpc_id_, continuation);
}

RpcResponse RpcCallAwaitable::await_resume()
{
    if (endpoint_ == NULL || immediate_response_.status != ABE_WOULD_BLOCK) {
        return immediate_response_;
    }
    return endpoint_->take_response(rpc_id_);
}

RpcEndpoint::RpcEndpoint()
    : pending_(NULL),
      handlers_(NULL),
      pending_count_(0u),
      handler_count_(0u),
      next_rpc_id_(1u),
      initialized_(0)
{
    memset(&config_, 0, sizeof(config_));
}

RpcEndpoint::~RpcEndpoint()
{
    close();
}

int RpcEndpoint::init(const RpcEndpointConfig& config)
{
    if (config.server_id == 0u ||
        config.max_pending == 0u ||
        config.max_packet_size < ABE_MSG_HEADER_SIZE ||
        config.sender.send == NULL) {
        return ABE_INVALID_ARG;
    }

    close();
    pending_ = new (std::nothrow) PendingCall[config.max_pending];
    if (pending_ == NULL) {
        return ABE_NO_MEMORY;
    }
    handlers_ = new (std::nothrow) HandlerEntry[RPC_MAX_HANDLER_COUNT];
    if (handlers_ == NULL) {
        delete[] pending_;
        pending_ = NULL;
        return ABE_NO_MEMORY;
    }

    config_ = config;
    if (config_.default_timeout_ms == 0u) {
        config_.default_timeout_ms = RPC_DEFAULT_TIMEOUT_MS;
    }
    pending_count_ = 0u;
    handler_count_ = 0u;
    next_rpc_id_ = 1u;
    initialized_ = 1;
    return ABE_OK;
}

void RpcEndpoint::close()
{
    delete[] pending_;
    delete[] handlers_;
    pending_ = NULL;
    handlers_ = NULL;
    pending_count_ = 0u;
    handler_count_ = 0u;
    next_rpc_id_ = 1u;
    initialized_ = 0;
    memset(&config_, 0, sizeof(config_));
}

int RpcEndpoint::set_handler(
    uint32_t msg_id,
    RpcHandler handler,
    void* user_data)
{
    HandlerEntry* entry;

    if (!initialized_ || msg_id == 0u || handler == NULL) {
        return ABE_INVALID_ARG;
    }

    entry = find_handler(msg_id);
    if (entry != NULL) {
        entry->handler = handler;
        entry->user_data = user_data;
        return ABE_OK;
    }
    if (handler_count_ >= RPC_MAX_HANDLER_COUNT) {
        return ABE_NO_SLOT;
    }

    handlers_[handler_count_].msg_id = msg_id;
    handlers_[handler_count_].handler = handler;
    handlers_[handler_count_].user_data = user_data;
    ++handler_count_;
    return ABE_OK;
}

int RpcEndpoint::clear_handler(uint32_t msg_id)
{
    uint32_t index;

    if (msg_id == 0u) {
        return ABE_INVALID_ARG;
    }

    index = 0u;
    while (index < handler_count_) {
        if (handlers_[index].msg_id == msg_id) {
            if (index + 1u < handler_count_) {
                handlers_[index] = handlers_[handler_count_ - 1u];
            }
            --handler_count_;
            handlers_[handler_count_] = HandlerEntry();
            return ABE_OK;
        }
        ++index;
    }
    return ABE_NOT_FOUND;
}

void RpcEndpoint::clear_handlers()
{
    uint32_t index;

    index = 0u;
    while (index < handler_count_) {
        handlers_[index] = HandlerEntry();
        ++index;
    }
    handler_count_ = 0u;
}

RpcCallAwaitable RpcEndpoint::call(const RpcCallRequest& request)
{
    RpcResponse immediate;
    PendingCall* pending;
    abe_msg_header_t header;
    uint32_t rpc_id;
    uint32_t index;
    uint64_t timeout_ms;
    int rc;

    immediate.status = ABE_WOULD_BLOCK;
    if (!initialized_) {
        immediate.status = ABE_CLOSED;
        return RpcCallAwaitable(NULL, 0u, immediate);
    }
    if (request.msg_id == 0u ||
        request.target_server == 0u ||
        (request.body_size != 0u && request.body == NULL)) {
        immediate.status = ABE_INVALID_ARG;
        return RpcCallAwaitable(NULL, 0u, immediate);
    }
    if (pending_count_ >= config_.max_pending) {
        immediate.status = ABE_NO_SLOT;
        return RpcCallAwaitable(NULL, 0u, immediate);
    }

    rpc_id = next_rpc_id();
    pending = NULL;
    index = 0u;
    while (index < config_.max_pending) {
        if (!pending_[index].used) {
            pending = &pending_[index];
            break;
        }
        ++index;
    }
    if (pending == NULL) {
        immediate.status = ABE_NO_SLOT;
        return RpcCallAwaitable(NULL, 0u, immediate);
    }

    timeout_ms = request.timeout_ms == 0u
        ? config_.default_timeout_ms
        : request.timeout_ms;
    *pending = PendingCall();
    pending->used = 1;
    pending->completed = 0;
    pending->rpc_id = rpc_id;
    pending->expire_time_ms = request.now_ms + timeout_ms;
    pending->response.status = ABE_WOULD_BLOCK;
    ++pending_count_;

    abe_msg_header_init(&header);
    header.msg_id = request.msg_id;
    header.rpc_id = rpc_id;
    header.session_id = request.session_id;
    header.role_id = request.role_id;
    header.player_id = request.player_id;
    header.trace_id = request.trace_id;
    header.source_server = config_.server_id;
    header.target_server = request.target_server;
    header.route_type = request.route_type;
    header.flags = RPC_FLAG_REQUEST;
    header.timestamp = request.now_ms;

    rc = send_packet(header, request.body, request.body_size);
    if (rc != ABE_OK) {
        *pending = PendingCall();
        --pending_count_;
        immediate.status = rc;
        return RpcCallAwaitable(NULL, 0u, immediate);
    }

    return RpcCallAwaitable(this, rpc_id, immediate);
}

int RpcEndpoint::notify(const RpcCallRequest& request)
{
    abe_msg_header_t header;

    if (!initialized_) {
        return ABE_CLOSED;
    }
    if (request.msg_id == 0u ||
        request.target_server == 0u ||
        (request.body_size != 0u && request.body == NULL)) {
        return ABE_INVALID_ARG;
    }

    abe_msg_header_init(&header);
    header.msg_id = request.msg_id;
    header.session_id = request.session_id;
    header.role_id = request.role_id;
    header.player_id = request.player_id;
    header.trace_id = request.trace_id;
    header.source_server = config_.server_id;
    header.target_server = request.target_server;
    header.route_type = request.route_type;
    header.flags = RPC_FLAG_REQUEST;
    header.timestamp = request.now_ms;
    return send_packet(header, request.body, request.body_size);
}

int RpcEndpoint::receive_packet(const void* packet, uint32_t packet_size)
{
    abe_msg_packet_view_t view;
    int rc;

    if (!initialized_) {
        return ABE_CLOSED;
    }

    rc = abe_msg_packet_decode(packet, packet_size, &view);
    if (rc != ABE_PROTOCOL_OK) {
        ABE_LOG_WARN("rpc packet decode failed status=%s", abe_status_name(rc));
        return rc;
    }
    if (view.header.target_server != 0u &&
        view.header.target_server != config_.server_id) {
        return ABE_NOT_FOUND;
    }

    if ((view.header.flags & RPC_FLAG_RESPONSE) != 0u) {
        return receive_response(view);
    }
    if ((view.header.flags & RPC_FLAG_REQUEST) != 0u) {
        return receive_request(view);
    }
    return ABE_INVALID_ARG;
}

int RpcEndpoint::send_response(
    const RpcRequest& request,
    const void* body,
    uint32_t body_size)
{
    abe_msg_header_t header;

    if (!initialized_) {
        return ABE_CLOSED;
    }
    if (request.header.rpc_id == 0u || (body_size != 0u && body == NULL)) {
        return ABE_INVALID_ARG;
    }

    abe_msg_header_init(&header);
    header.msg_id = request.header.msg_id;
    header.rpc_id = request.header.rpc_id;
    header.session_id = request.header.session_id;
    header.role_id = request.header.role_id;
    header.player_id = request.header.player_id;
    header.trace_id = request.header.trace_id;
    header.source_server = config_.server_id;
    header.target_server = request.header.source_server;
    header.route_type = request.header.route_type;
    header.flags = RPC_FLAG_RESPONSE;
    header.timestamp = request.header.timestamp;
    return send_packet(header, body, body_size);
}

int RpcEndpoint::send_error_response(const RpcRequest& request, int status)
{
    if (request.header.rpc_id == 0u) {
        return ABE_INVALID_ARG;
    }
    return send_error_packet(request.header, status);
}

int RpcEndpoint::update(uint64_t now_ms, uint32_t* out_timeout_count)
{
    std::vector<std::coroutine_handle<> > continuations;
    uint32_t index;
    uint32_t timeout_count;

    if (out_timeout_count != NULL) {
        *out_timeout_count = 0u;
    }
    if (!initialized_) {
        return ABE_CLOSED;
    }

    timeout_count = 0u;
    index = 0u;
    while (index < config_.max_pending) {
        PendingCall* pending;

        pending = &pending_[index];
        if (pending->used &&
            !pending->completed &&
            pending->expire_time_ms != 0u &&
            now_ms >= pending->expire_time_ms) {
            pending->completed = 1;
            pending->response.status = ABE_TIMEOUT;
            if (pending->continuation) {
                continuations.push_back(pending->continuation);
                pending->continuation = NULL;
            }
            ++timeout_count;
        }
        ++index;
    }

    if (out_timeout_count != NULL) {
        *out_timeout_count = timeout_count;
    }

    index = 0u;
    while (index < continuations.size()) {
        continuations[index].resume();
        ++index;
    }
    return ABE_OK;
}

uint32_t RpcEndpoint::pending_count() const
{
    return pending_count_;
}

uint32_t RpcEndpoint::server_id() const
{
    return config_.server_id;
}

int RpcEndpoint::initialized() const
{
    return initialized_;
}

bool RpcEndpoint::call_ready(uint32_t rpc_id) const
{
    const PendingCall* pending;

    pending = find_pending(rpc_id);
    return pending == NULL || pending->completed;
}

bool RpcEndpoint::suspend_call(
    uint32_t rpc_id,
    std::coroutine_handle<> continuation)
{
    PendingCall* pending;

    pending = find_pending(rpc_id);
    if (pending == NULL || pending->completed) {
        return false;
    }

    pending->continuation = continuation;
    return true;
}

RpcResponse RpcEndpoint::take_response(uint32_t rpc_id)
{
    RpcResponse response;
    PendingCall* pending;

    pending = find_pending(rpc_id);
    if (pending == NULL) {
        response.status = ABE_NOT_FOUND;
        return response;
    }

    response = pending->response;
    *pending = PendingCall();
    if (pending_count_ > 0u) {
        --pending_count_;
    }
    return response;
}

int RpcEndpoint::send_packet(
    const abe_msg_header_t& header,
    const void* body,
    uint32_t body_size)
{
    std::vector<unsigned char> packet;
    uint32_t packet_size;
    uint32_t written_size;
    int rc;

    if (body_size != 0u && body == NULL) {
        return ABE_INVALID_ARG;
    }

    rc = abe_msg_packet_get_size(body_size, &packet_size);
    if (rc != ABE_PROTOCOL_OK) {
        return rc;
    }
    if (packet_size > config_.max_packet_size) {
        return ABE_PACKET_TOO_LARGE;
    }

    packet.resize(packet_size);
    written_size = 0u;
    rc = abe_msg_packet_encode(
        &header,
        body,
        body_size,
        &packet[0],
        packet_size,
        &written_size);
    if (rc != ABE_PROTOCOL_OK) {
        return rc;
    }
    return config_.sender.send(&packet[0], written_size, config_.sender.user_data);
}

int RpcEndpoint::send_error_packet(
    const abe_msg_header_t& request_header,
    int status)
{
    abe_msg_header_t header;
    unsigned char body[4];

    abe_msg_header_init(&header);
    header.msg_id = request_header.msg_id;
    header.rpc_id = request_header.rpc_id;
    header.session_id = request_header.session_id;
    header.role_id = request_header.role_id;
    header.player_id = request_header.player_id;
    header.trace_id = request_header.trace_id;
    header.source_server = config_.server_id;
    header.target_server = request_header.source_server;
    header.route_type = request_header.route_type;
    header.flags = RPC_FLAG_RESPONSE | RPC_FLAG_ERROR;
    header.timestamp = request_header.timestamp;
    write_i32(body, status);
    return send_packet(header, body, sizeof(body));
}

int RpcEndpoint::receive_request(const abe_msg_packet_view_t& packet)
{
    RpcRequest request;
    HandlerEntry* entry;
    int rc;

    if (packet.header.msg_id == 0u) {
        return ABE_INVALID_ARG;
    }

    memset(&request, 0, sizeof(request));
    request.header = packet.header;
    request.body = packet.body;
    request.body_size = packet.body_size;

    entry = find_handler(packet.header.msg_id);
    if (entry == NULL || entry->handler == NULL) {
        return send_error_response(request, ABE_NOT_FOUND);
    }

    rc = entry->handler(this, request, entry->user_data);
    if (rc != ABE_OK && rc != ABE_WOULD_BLOCK) {
        return send_error_response(request, rc);
    }
    return ABE_OK;
}

int RpcEndpoint::receive_response(const abe_msg_packet_view_t& packet)
{
    int status;

    if (packet.header.rpc_id == 0u) {
        return ABE_INVALID_ARG;
    }

    status = ABE_OK;
    if ((packet.header.flags & RPC_FLAG_ERROR) != 0u) {
        status = read_i32(packet.body, packet.body_size);
    }
    return complete_call(
        packet.header.rpc_id,
        status,
        packet.header,
        packet.body,
        packet.body_size);
}

int RpcEndpoint::complete_call(
    uint32_t rpc_id,
    int status,
    const abe_msg_header_t& header,
    const void* body,
    uint32_t body_size)
{
    PendingCall* pending;
    std::coroutine_handle<> continuation;

    pending = find_pending(rpc_id);
    if (pending == NULL) {
        return ABE_NOT_FOUND;
    }

    pending->completed = 1;
    pending->response.status = status;
    pending->response.header = header;
    pending->response.body.clear();
    if (body != NULL && body_size != 0u) {
        pending->response.body.assign((const char*)body, (size_t)body_size);
    }

    continuation = pending->continuation;
    pending->continuation = NULL;
    if (continuation) {
        continuation.resume();
    }
    return ABE_OK;
}

uint32_t RpcEndpoint::next_rpc_id()
{
    uint32_t candidate;

    candidate = next_rpc_id_;
    do {
        if (candidate == 0u) {
            candidate = 1u;
        }
        next_rpc_id_ = candidate + 1u;
        if (find_pending(candidate) == NULL) {
            return candidate;
        }
        candidate = next_rpc_id_;
    } while (candidate != next_rpc_id_);

    return 0u;
}

RpcEndpoint::PendingCall* RpcEndpoint::find_pending(uint32_t rpc_id)
{
    uint32_t index;

    if (pending_ == NULL || rpc_id == 0u) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_pending) {
        if (pending_[index].used && pending_[index].rpc_id == rpc_id) {
            return &pending_[index];
        }
        ++index;
    }
    return NULL;
}

const RpcEndpoint::PendingCall* RpcEndpoint::find_pending(uint32_t rpc_id) const
{
    uint32_t index;

    if (pending_ == NULL || rpc_id == 0u) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_pending) {
        if (pending_[index].used && pending_[index].rpc_id == rpc_id) {
            return &pending_[index];
        }
        ++index;
    }
    return NULL;
}

RpcEndpoint::HandlerEntry* RpcEndpoint::find_handler(uint32_t msg_id)
{
    uint32_t index;

    if (handlers_ == NULL || msg_id == 0u) {
        return NULL;
    }

    index = 0u;
    while (index < handler_count_) {
        if (handlers_[index].msg_id == msg_id) {
            return &handlers_[index];
        }
        ++index;
    }
    return NULL;
}

} /* namespace rpc */
} /* namespace logic */
} /* namespace abe */
