#ifndef ABE_NET_H
#define ABE_NET_H

#include "abe_error.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABE_NET_DEFAULT_MAX_PACKET_SIZE 65535u
#define ABE_NET_PACKET_HEADER_SIZE 4u

/*
 * TCP message framing:
 *   uint32 big-endian payload length + payload bytes.
 *
 * max_packet_size is the maximum payload length. When it is 0, the default is
 * ABE_NET_DEFAULT_MAX_PACKET_SIZE, which is smaller than 64 KiB.
 *
 * Public handles are opaque. All buffers passed to callbacks are owned by the
 * network layer and are valid only during the callback.
 */

typedef struct abe_net_loop abe_net_loop_t;
typedef struct abe_net_tcp_listener abe_net_tcp_listener_t;
typedef struct abe_net_tcp_conn abe_net_tcp_conn_t;
typedef struct abe_net_udp_endpoint abe_net_udp_endpoint_t;

typedef enum abe_net_status {
    ABE_NET_OK = ABE_OK,
    ABE_NET_ERROR = ABE_ERROR,
    ABE_NET_INVALID_ARG = ABE_INVALID_ARG,
    ABE_NET_NO_MEMORY = ABE_NO_MEMORY,
    ABE_NET_PACKET_TOO_LARGE = ABE_PACKET_TOO_LARGE,
    ABE_NET_WOULD_BLOCK = ABE_WOULD_BLOCK
} abe_net_status_t;

typedef enum abe_net_tcp_event {
    ABE_NET_TCP_EVENT_CONNECTED = 1,
    ABE_NET_TCP_EVENT_CLOSED = 2,
    ABE_NET_TCP_EVENT_ERROR = 3
} abe_net_tcp_event_t;

typedef struct abe_net_addr {
    char host[64];
    uint16_t port;
} abe_net_addr_t;

/* The accepted TCP connection is owned by the network layer; close it with
 * abe_net_tcp_close when the session ends. The peer pointer is callback-local.
 */
typedef void (*abe_net_tcp_accept_cb)(
    abe_net_tcp_listener_t* listener,
    abe_net_tcp_conn_t* conn,
    const abe_net_addr_t* peer,
    void* user_data);

/* The data pointer is callback-local. Copy it if it must outlive the callback. */
typedef void (*abe_net_tcp_message_cb)(
    abe_net_tcp_conn_t* conn,
    const void* data,
    uint32_t size,
    void* user_data);

/* The conn pointer is callback-local for CLOSED/ERROR events. */
typedef void (*abe_net_tcp_event_cb)(
    abe_net_tcp_conn_t* conn,
    abe_net_tcp_event_t event,
    int error_code,
    void* user_data);

/* The data and peer pointers are callback-local. */
typedef void (*abe_net_udp_message_cb)(
    abe_net_udp_endpoint_t* endpoint,
    const void* data,
    uint32_t size,
    const abe_net_addr_t* peer,
    void* user_data);

typedef struct abe_net_tcp_callbacks {
    abe_net_tcp_accept_cb on_accept;
    abe_net_tcp_message_cb on_message;
    abe_net_tcp_event_cb on_event;
    void* user_data;
} abe_net_tcp_callbacks_t;

typedef struct abe_net_udp_callbacks {
    abe_net_udp_message_cb on_message;
    void* user_data;
} abe_net_udp_callbacks_t;

typedef struct abe_net_tcp_config {
    /* Numeric IPv4 address. NULL, empty, or "0.0.0.0" means bind-any for listen. */
    const char* host;
    uint16_t port;
    uint32_t max_packet_size;
    int backlog;
    abe_net_tcp_callbacks_t callbacks;
} abe_net_tcp_config_t;

typedef struct abe_net_udp_config {
    /* Numeric IPv4 address. NULL, empty, or "0.0.0.0" means bind-any. */
    const char* host;
    uint16_t port;
    uint32_t max_packet_size;
    abe_net_udp_callbacks_t callbacks;
} abe_net_udp_config_t;

int abe_net_loop_create(abe_net_loop_t** out_loop);
/* Close listeners, connections, and UDP endpoints before destroying the loop. */
void abe_net_loop_destroy(abe_net_loop_t* loop);
int abe_net_loop_run(abe_net_loop_t* loop);
int abe_net_loop_run_once(abe_net_loop_t* loop);
int abe_net_loop_update(abe_net_loop_t* loop);
int abe_net_loop_stop(abe_net_loop_t* loop);

int abe_net_tcp_listen(
    abe_net_loop_t* loop,
    const abe_net_tcp_config_t* config,
    abe_net_tcp_listener_t** out_listener);
void abe_net_tcp_listener_close(abe_net_tcp_listener_t* listener);

int abe_net_tcp_connect(
    abe_net_loop_t* loop,
    const abe_net_tcp_config_t* config,
    abe_net_tcp_conn_t** out_conn);
int abe_net_tcp_send(abe_net_tcp_conn_t* conn, const void* data, uint32_t size);
/* Safe to call from network callbacks. The conn pointer becomes invalid after close returns. */
void abe_net_tcp_close(abe_net_tcp_conn_t* conn);
void abe_net_tcp_set_user_data(abe_net_tcp_conn_t* conn, void* user_data);
void* abe_net_tcp_get_user_data(abe_net_tcp_conn_t* conn);
/* Replaces callbacks for an existing connection. Callback pointers are copied. */
void abe_net_tcp_set_callbacks(
    abe_net_tcp_conn_t* conn,
    const abe_net_tcp_callbacks_t* callbacks);
int abe_net_tcp_get_peer_addr(abe_net_tcp_conn_t* conn, abe_net_addr_t* out_addr);

int abe_net_udp_bind(
    abe_net_loop_t* loop,
    const abe_net_udp_config_t* config,
    abe_net_udp_endpoint_t** out_endpoint);
int abe_net_udp_send_to(
    abe_net_udp_endpoint_t* endpoint,
    const char* host,
    uint16_t port,
    const void* data,
    uint32_t size);
/* Safe to call from network callbacks. The endpoint pointer becomes invalid after close returns. */
void abe_net_udp_close(abe_net_udp_endpoint_t* endpoint);

uint32_t abe_net_packet_max_payload_size(uint32_t configured_size);
int abe_net_packet_encode(
    const void* payload,
    uint32_t payload_size,
    void* out_buffer,
    uint32_t out_buffer_size,
    uint32_t* out_packet_size);
int abe_net_packet_peek_size(
    const void* buffer,
    uint32_t buffer_size,
    uint32_t* out_payload_size);

#ifdef __cplusplus
}
#endif

#endif /* ABE_NET_H */
