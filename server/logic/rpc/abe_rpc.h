#ifndef ABE_LOGIC_RPC_H
#define ABE_LOGIC_RPC_H

#include "abe_error.h"
#include "abe_protocol.h"

#include <coroutine>
#include <stdint.h>
#include <string>

namespace abe {
namespace logic {
namespace rpc {

enum RpcFlag {
    RPC_FLAG_REQUEST = 0x01u,
    RPC_FLAG_RESPONSE = 0x02u,
    RPC_FLAG_ERROR = 0x04u
};

struct RpcPacketSender {
    /*
     * The packet buffer is owned by RpcEndpoint and is valid only during this
     * callback. A transport that needs to send later must copy the packet.
     */
    int (*send)(const void* packet, uint32_t packet_size, void* user_data);
    void* user_data;
};

struct RpcEndpointConfig {
    uint32_t server_id;
    uint32_t max_pending;
    uint32_t max_packet_size;
    uint64_t default_timeout_ms;
    RpcPacketSender sender;
};

struct RpcCallRequest {
    uint32_t msg_id;
    uint32_t target_server;
    uint32_t route_type;
    uint64_t session_id;
    uint64_t role_id;
    uint64_t player_id;
    uint64_t trace_id;
    uint64_t now_ms;
    uint64_t timeout_ms;
    const void* body;
    uint32_t body_size;
};

struct RpcRequest {
    abe_msg_header_t header;
    const void* body;
    uint32_t body_size;
};

struct RpcResponse {
    RpcResponse();

    int status;
    abe_msg_header_t header;
    std::string body;
};

class RpcEndpoint;

typedef int (*RpcHandler)(
    RpcEndpoint* endpoint,
    const RpcRequest& request,
    void* user_data);

class RpcCallAwaitable {
public:
    RpcCallAwaitable();

    bool await_ready() const;
    bool await_suspend(std::coroutine_handle<> continuation);
    RpcResponse await_resume();

private:
    friend class RpcEndpoint;

    RpcCallAwaitable(
        RpcEndpoint* endpoint,
        uint32_t rpc_id,
        const RpcResponse& immediate_response);

    RpcEndpoint* endpoint_;
    uint32_t rpc_id_;
    RpcResponse immediate_response_;
};

template <typename T>
class RpcTask {
public:
    struct promise_type {
        promise_type()
            : status(ABE_WOULD_BLOCK),
              value()
        {
        }

        RpcTask get_return_object()
        {
            return RpcTask(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_never initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_always final_suspend() noexcept
        {
            return {};
        }

        void unhandled_exception()
        {
            status = ABE_ERROR;
        }

        void return_value(const T& result)
        {
            value = result;
            status = ABE_OK;
        }

        int status;
        T value;
    };

    RpcTask()
        : handle_(NULL)
    {
    }

    explicit RpcTask(std::coroutine_handle<promise_type> handle)
        : handle_(handle)
    {
    }

    RpcTask(RpcTask&& other) noexcept
        : handle_(other.handle_)
    {
        other.handle_ = NULL;
    }

    ~RpcTask()
    {
        if (handle_) {
            handle_.destroy();
        }
    }

    RpcTask& operator=(RpcTask&& other) noexcept
    {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = NULL;
        }
        return *this;
    }

    bool done() const
    {
        return handle_ != NULL && handle_.done();
    }

    int status() const
    {
        return handle_ ? handle_.promise().status : ABE_CLOSED;
    }

    T result() const
    {
        return handle_ ? handle_.promise().value : T();
    }

private:
    RpcTask(const RpcTask&);
    RpcTask& operator=(const RpcTask&);

    std::coroutine_handle<promise_type> handle_;
};

class RpcEndpoint {
public:
    RpcEndpoint();
    ~RpcEndpoint();

    int init(const RpcEndpointConfig& config);
    void close();

    int set_handler(uint32_t msg_id, RpcHandler handler, void* user_data);
    int clear_handler(uint32_t msg_id);
    void clear_handlers();

    RpcCallAwaitable call(const RpcCallRequest& request);
    int notify(const RpcCallRequest& request);
    int receive_packet(const void* packet, uint32_t packet_size);
    int send_response(
        const RpcRequest& request,
        const void* body,
        uint32_t body_size);
    int send_error_response(const RpcRequest& request, int status);
    int update(uint64_t now_ms, uint32_t* out_timeout_count);

    uint32_t pending_count() const;
    uint32_t server_id() const;
    int initialized() const;

private:
    friend class RpcCallAwaitable;

    struct PendingCall;
    struct HandlerEntry;

    RpcEndpoint(const RpcEndpoint&);
    RpcEndpoint& operator=(const RpcEndpoint&);

    bool call_ready(uint32_t rpc_id) const;
    bool suspend_call(uint32_t rpc_id, std::coroutine_handle<> continuation);
    RpcResponse take_response(uint32_t rpc_id);

    int send_packet(
        const abe_msg_header_t& header,
        const void* body,
        uint32_t body_size);
    int send_error_packet(const abe_msg_header_t& request_header, int status);
    int receive_request(const abe_msg_packet_view_t& packet);
    int receive_response(const abe_msg_packet_view_t& packet);
    int complete_call(
        uint32_t rpc_id,
        int status,
        const abe_msg_header_t& header,
        const void* body,
        uint32_t body_size);
    uint32_t next_rpc_id();
    PendingCall* find_pending(uint32_t rpc_id);
    const PendingCall* find_pending(uint32_t rpc_id) const;
    HandlerEntry* find_handler(uint32_t msg_id);

    RpcEndpointConfig config_;
    PendingCall* pending_;
    HandlerEntry* handlers_;
    uint32_t pending_count_;
    uint32_t handler_count_;
    uint32_t next_rpc_id_;
    int initialized_;
};

} /* namespace rpc */
} /* namespace logic */
} /* namespace abe */

#endif /* ABE_LOGIC_RPC_H */
