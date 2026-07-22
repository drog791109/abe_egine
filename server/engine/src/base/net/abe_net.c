#include "abe_net.h"
#include "abe_mem_pool.h"

#include <arpa/inet.h>
#include <errno.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define ABE_NET_MEMORY_POOL_CAPACITY (16u * 1024u * 1024u)

struct abe_net_loop {
    struct event_base* base;
    abe_mem_pool_t* mem_pool;
};

struct abe_net_tcp_listener {
    abe_net_loop_t* loop;
    struct evconnlistener* listener;
    abe_net_tcp_callbacks_t callbacks;
    uint32_t max_packet_size;
};

struct abe_net_tcp_conn {
    abe_net_loop_t* loop;
    struct bufferevent* bev;
    abe_net_tcp_callbacks_t callbacks;
    uint32_t max_packet_size;
    int closing;
    int ref_count;
    int closed_event_emitted;
};

struct abe_net_udp_endpoint {
    abe_net_loop_t* loop;
    evutil_socket_t fd;
    struct event* read_event;
    abe_net_udp_callbacks_t callbacks;
    uint32_t max_packet_size;
    int closing;
    int ref_count;
};

static uint32_t abe_net_default_max_packet(uint32_t configured_size)
{
    if (configured_size == 0) {
        return ABE_NET_DEFAULT_MAX_PACKET_SIZE;
    }
    if (configured_size > ABE_NET_DEFAULT_MAX_PACKET_SIZE) {
        return ABE_NET_DEFAULT_MAX_PACKET_SIZE;
    }
    return configured_size;
}

static void abe_net_u32_to_be(uint32_t value, unsigned char out[4])
{
    out[0] = (unsigned char)((value >> 24) & 0xffu);
    out[1] = (unsigned char)((value >> 16) & 0xffu);
    out[2] = (unsigned char)((value >> 8) & 0xffu);
    out[3] = (unsigned char)(value & 0xffu);
}

static uint32_t abe_net_u32_from_be(const unsigned char in[4])
{
    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
}

static int abe_net_make_sockaddr(
    const char* host,
    uint16_t port,
    struct sockaddr_in* out_addr,
    int allow_any)
{
    const char* bind_host;

    if (out_addr == NULL) {
        return ABE_NET_INVALID_ARG;
    }

    memset(out_addr, 0, sizeof(*out_addr));
    out_addr->sin_family = AF_INET;
    out_addr->sin_port = htons(port);

    bind_host = host;
    if (bind_host == NULL || bind_host[0] == '\0' || strcmp(bind_host, "0.0.0.0") == 0) {
        if (!allow_any) {
            return ABE_NET_INVALID_ARG;
        }
        out_addr->sin_addr.s_addr = htonl(INADDR_ANY);
        return ABE_NET_OK;
    }

    if (evutil_inet_pton(AF_INET, bind_host, &out_addr->sin_addr) != 1) {
        return ABE_NET_INVALID_ARG;
    }

    return ABE_NET_OK;
}

static void abe_net_tcp_ref(abe_net_tcp_conn_t* conn)
{
    if (conn != NULL) {
        ++conn->ref_count;
    }
}

static void* abe_net_alloc(abe_net_loop_t* loop, uint64_t size)
{
    if (loop == NULL || loop->mem_pool == NULL) {
        return NULL;
    }
    return abe_mem_pool_alloc(loop->mem_pool, size);
}

static void* abe_net_calloc(abe_net_loop_t* loop, uint64_t count, uint64_t size)
{
    if (loop == NULL || loop->mem_pool == NULL) {
        return NULL;
    }
    return abe_mem_pool_calloc(loop->mem_pool, count, size);
}

static void abe_net_release(abe_net_loop_t* loop, void* ptr)
{
    if (loop != NULL && loop->mem_pool != NULL && ptr != NULL) {
        (void)abe_mem_pool_free(loop->mem_pool, ptr);
    }
}

static void abe_net_tcp_free(abe_net_tcp_conn_t* conn)
{
    abe_net_loop_t* loop;

    if (conn == NULL) {
        return;
    }

    loop = conn->loop;
    if (conn->bev != NULL) {
        bufferevent_free(conn->bev);
        conn->bev = NULL;
    }
    abe_net_release(loop, conn);
}

static void abe_net_tcp_maybe_free(abe_net_tcp_conn_t* conn)
{
    if (conn != NULL && conn->closing && conn->ref_count <= 0) {
        abe_net_tcp_free(conn);
    }
}

static void abe_net_tcp_unref(abe_net_tcp_conn_t* conn)
{
    if (conn == NULL) {
        return;
    }
    --conn->ref_count;
    abe_net_tcp_maybe_free(conn);
}

static void abe_net_udp_ref(abe_net_udp_endpoint_t* endpoint)
{
    if (endpoint != NULL) {
        ++endpoint->ref_count;
    }
}

static void abe_net_udp_maybe_free(abe_net_udp_endpoint_t* endpoint)
{
    abe_net_loop_t* loop;

    if (endpoint != NULL && endpoint->closing && endpoint->ref_count <= 0) {
        loop = endpoint->loop;
        if (endpoint->read_event != NULL) {
            event_free(endpoint->read_event);
            endpoint->read_event = NULL;
        }
        if (endpoint->fd >= 0) {
            evutil_closesocket(endpoint->fd);
            endpoint->fd = -1;
        }
        abe_net_release(loop, endpoint);
    }
}

static void abe_net_udp_unref(abe_net_udp_endpoint_t* endpoint)
{
    if (endpoint == NULL) {
        return;
    }
    --endpoint->ref_count;
    abe_net_udp_maybe_free(endpoint);
}

static void abe_net_udp_begin_close(abe_net_udp_endpoint_t* endpoint)
{
    if (endpoint == NULL) {
        return;
    }
    if (!endpoint->closing) {
        endpoint->closing = 1;
        if (endpoint->read_event != NULL) {
            event_del(endpoint->read_event);
        }
    }
    abe_net_udp_maybe_free(endpoint);
}

static void abe_net_fill_addr(const struct sockaddr* sa, abe_net_addr_t* out_addr)
{
    const struct sockaddr_in* sin;

    if (out_addr == NULL) {
        return;
    }

    memset(out_addr, 0, sizeof(*out_addr));
    if (sa == NULL || sa->sa_family != AF_INET) {
        return;
    }

    sin = (const struct sockaddr_in*)sa;
    evutil_inet_ntop(AF_INET, &sin->sin_addr, out_addr->host, sizeof(out_addr->host));
    out_addr->port = ntohs(sin->sin_port);
}

static void abe_net_tcp_emit_event(
    abe_net_tcp_conn_t* conn,
    abe_net_tcp_event_t event,
    int error_code)
{
    if (conn != NULL && conn->callbacks.on_event != NULL) {
        abe_net_tcp_ref(conn);
        conn->callbacks.on_event(conn, event, error_code, conn->callbacks.user_data);
        --conn->ref_count;
    }
}

static void abe_net_tcp_emit_message(
    abe_net_tcp_conn_t* conn,
    const void* payload,
    uint32_t payload_size)
{
    if (conn != NULL && conn->callbacks.on_message != NULL) {
        abe_net_tcp_ref(conn);
        conn->callbacks.on_message(
            conn,
            payload,
            payload_size,
            conn->callbacks.user_data);
        --conn->ref_count;
    }
}

static void abe_net_tcp_begin_close(abe_net_tcp_conn_t* conn, int emit_closed_event)
{
    if (conn == NULL) {
        return;
    }

    if (!conn->closing) {
        conn->closing = 1;
        if (conn->bev != NULL) {
            bufferevent_disable(conn->bev, EV_READ | EV_WRITE);
            bufferevent_setcb(conn->bev, NULL, NULL, NULL, NULL);
        }
    }

    if (emit_closed_event && !conn->closed_event_emitted) {
        conn->closed_event_emitted = 1;
        abe_net_tcp_emit_event(conn, ABE_NET_TCP_EVENT_CLOSED, 0);
    }

    abe_net_tcp_maybe_free(conn);
}

static void abe_net_tcp_read_cb(struct bufferevent* bev, void* ctx)
{
    abe_net_tcp_conn_t* conn;
    struct evbuffer* input;
    size_t available;

    (void)bev;
    conn = (abe_net_tcp_conn_t*)ctx;
    if (conn == NULL || conn->closing) {
        return;
    }

    abe_net_tcp_ref(conn);
    input = bufferevent_get_input(conn->bev);
    for (;;) {
        unsigned char header[ABE_NET_PACKET_HEADER_SIZE];
        uint32_t payload_size;
        unsigned char* payload;

        available = evbuffer_get_length(input);
        if (available < ABE_NET_PACKET_HEADER_SIZE) {
            break;
        }

        if (evbuffer_copyout(input, header, ABE_NET_PACKET_HEADER_SIZE) !=
            (ssize_t)ABE_NET_PACKET_HEADER_SIZE) {
            break;
        }

        payload_size = abe_net_u32_from_be(header);
        if (payload_size > conn->max_packet_size) {
            abe_net_tcp_emit_event(conn, ABE_NET_TCP_EVENT_ERROR, ABE_NET_PACKET_TOO_LARGE);
            abe_net_tcp_begin_close(conn, 1);
            break;
        }

        if (available < (size_t)ABE_NET_PACKET_HEADER_SIZE + payload_size) {
            break;
        }

        evbuffer_drain(input, ABE_NET_PACKET_HEADER_SIZE);
        payload = NULL;
        if (payload_size > 0) {
            payload = (unsigned char*)abe_net_alloc(conn->loop, payload_size);
            if (payload == NULL) {
                abe_net_tcp_emit_event(conn, ABE_NET_TCP_EVENT_ERROR, ABE_NET_NO_MEMORY);
                abe_net_tcp_begin_close(conn, 1);
                break;
            }
            if (evbuffer_remove(input, payload, payload_size) != (int)payload_size) {
                abe_net_release(conn->loop, payload);
                abe_net_tcp_emit_event(conn, ABE_NET_TCP_EVENT_ERROR, ABE_NET_ERROR);
                abe_net_tcp_begin_close(conn, 1);
                break;
            }
        }

        abe_net_tcp_emit_message(conn, payload, payload_size);
        abe_net_release(conn->loop, payload);
        if (conn->closing) {
            break;
        }
    }

    abe_net_tcp_unref(conn);
}

static void abe_net_tcp_on_event(struct bufferevent* bev, short events, void* ctx)
{
    abe_net_tcp_conn_t* conn;
    int error_code;

    (void)bev;
    conn = (abe_net_tcp_conn_t*)ctx;
    if (conn == NULL) {
        return;
    }

    abe_net_tcp_ref(conn);
    error_code = EVUTIL_SOCKET_ERROR();
    if ((events & BEV_EVENT_CONNECTED) != 0) {
        abe_net_tcp_emit_event(conn, ABE_NET_TCP_EVENT_CONNECTED, 0);
        abe_net_tcp_unref(conn);
        return;
    }

    if ((events & (BEV_EVENT_ERROR | BEV_EVENT_EOF | BEV_EVENT_TIMEOUT)) != 0) {
        if ((events & BEV_EVENT_ERROR) != 0) {
            abe_net_tcp_emit_event(conn, ABE_NET_TCP_EVENT_ERROR, error_code);
        }
        abe_net_tcp_begin_close(conn, 1);
    }
    abe_net_tcp_unref(conn);
}

static void abe_net_tcp_on_accept(
    struct evconnlistener* listener,
    evutil_socket_t fd,
    struct sockaddr* addr,
    int socklen,
    void* ctx)
{
    abe_net_tcp_listener_t* tcp_listener;
    abe_net_tcp_conn_t* conn;
    struct bufferevent* bev;
    abe_net_addr_t peer;

    (void)listener;
    (void)socklen;
    tcp_listener = (abe_net_tcp_listener_t*)ctx;
    if (tcp_listener == NULL || tcp_listener->loop == NULL) {
        evutil_closesocket(fd);
        return;
    }

    conn = (abe_net_tcp_conn_t*)abe_net_calloc(tcp_listener->loop, 1u, sizeof(*conn));
    if (conn == NULL) {
        evutil_closesocket(fd);
        return;
    }
    conn->loop = tcp_listener->loop;

    bev = bufferevent_socket_new(
        tcp_listener->loop->base,
        fd,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
    if (bev == NULL) {
        abe_net_release(tcp_listener->loop, conn);
        evutil_closesocket(fd);
        return;
    }

    conn->bev = bev;
    conn->callbacks = tcp_listener->callbacks;
    conn->max_packet_size = tcp_listener->max_packet_size;

    bufferevent_setcb(bev, abe_net_tcp_read_cb, NULL, abe_net_tcp_on_event, conn);
    bufferevent_enable(bev, EV_READ | EV_WRITE);

    abe_net_fill_addr(addr, &peer);
    if (tcp_listener->callbacks.on_accept != NULL) {
        tcp_listener->callbacks.on_accept(
            tcp_listener,
            conn,
            &peer,
            tcp_listener->callbacks.user_data);
    }
}

static void abe_net_udp_read_cb(evutil_socket_t fd, short events, void* ctx)
{
    abe_net_udp_endpoint_t* endpoint;
    unsigned char* buffer;
    struct sockaddr_in peer_addr;
    socklen_t peer_len;
    ssize_t received;
    abe_net_addr_t peer;

    (void)events;
    endpoint = (abe_net_udp_endpoint_t*)ctx;
    if (endpoint == NULL || endpoint->closing) {
        return;
    }

    abe_net_udp_ref(endpoint);
    buffer = (unsigned char*)abe_net_alloc(endpoint->loop, (uint64_t)endpoint->max_packet_size + 1u);
    if (buffer == NULL) {
        abe_net_udp_unref(endpoint);
        return;
    }

    while (!endpoint->closing) {
        memset(&peer_addr, 0, sizeof(peer_addr));
        peer_len = sizeof(peer_addr);
        received = recvfrom(
            fd,
            buffer,
            (size_t)endpoint->max_packet_size + 1u,
            0,
            (struct sockaddr*)&peer_addr,
            &peer_len);

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                break;
            }
            break;
        }

        if ((uint32_t)received > endpoint->max_packet_size) {
            continue;
        }

        abe_net_fill_addr((const struct sockaddr*)&peer_addr, &peer);
        if (endpoint->callbacks.on_message != NULL) {
            endpoint->callbacks.on_message(
                endpoint,
                buffer,
                (uint32_t)received,
                &peer,
                endpoint->callbacks.user_data);
        }
    }

    abe_net_release(endpoint->loop, buffer);
    abe_net_udp_unref(endpoint);
}

uint32_t abe_net_packet_max_payload_size(uint32_t configured_size)
{
    return abe_net_default_max_packet(configured_size);
}

int abe_net_packet_encode(
    const void* payload,
    uint32_t payload_size,
    void* out_buffer,
    uint32_t out_buffer_size,
    uint32_t* out_packet_size)
{
    unsigned char* out;

    if ((payload_size > 0 && payload == NULL) || out_buffer == NULL) {
        return ABE_NET_INVALID_ARG;
    }
    if (payload_size > ABE_NET_DEFAULT_MAX_PACKET_SIZE) {
        return ABE_NET_PACKET_TOO_LARGE;
    }
    if (out_buffer_size < payload_size + ABE_NET_PACKET_HEADER_SIZE) {
        return ABE_NET_INVALID_ARG;
    }

    out = (unsigned char*)out_buffer;
    abe_net_u32_to_be(payload_size, out);
    if (payload_size > 0) {
        memcpy(out + ABE_NET_PACKET_HEADER_SIZE, payload, payload_size);
    }
    if (out_packet_size != NULL) {
        *out_packet_size = payload_size + ABE_NET_PACKET_HEADER_SIZE;
    }

    return ABE_NET_OK;
}

int abe_net_packet_peek_size(
    const void* buffer,
    uint32_t buffer_size,
    uint32_t* out_payload_size)
{
    uint32_t payload_size;

    if (buffer == NULL || out_payload_size == NULL) {
        return ABE_NET_INVALID_ARG;
    }
    if (buffer_size < ABE_NET_PACKET_HEADER_SIZE) {
        return ABE_NET_WOULD_BLOCK;
    }

    payload_size = abe_net_u32_from_be((const unsigned char*)buffer);
    if (payload_size > ABE_NET_DEFAULT_MAX_PACKET_SIZE) {
        return ABE_NET_PACKET_TOO_LARGE;
    }
    *out_payload_size = payload_size;

    return ABE_NET_OK;
}

int abe_net_loop_create(abe_net_loop_t** out_loop)
{
    abe_net_loop_t* loop;
    abe_mem_pool_t* mem_pool;
    abe_mem_pool_config_t mem_config;
    int rc;

    if (out_loop == NULL) {
        return ABE_NET_INVALID_ARG;
    }
    *out_loop = NULL;

    memset(&mem_config, 0, sizeof(mem_config));
    mem_config.capacity = ABE_NET_MEMORY_POOL_CAPACITY;
    mem_config.name = "abe_net_loop";
    mem_pool = NULL;
    rc = abe_mem_pool_create(&mem_config, &mem_pool);
    if (rc != ABE_MEM_POOL_OK) {
        return ABE_NET_NO_MEMORY;
    }

    loop = (abe_net_loop_t*)abe_mem_pool_calloc(mem_pool, 1u, sizeof(*loop));
    if (loop == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_NET_NO_MEMORY;
    }
    loop->mem_pool = mem_pool;

    loop->base = event_base_new();
    if (loop->base == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_NET_ERROR;
    }

    *out_loop = loop;
    return ABE_NET_OK;
}

void abe_net_loop_destroy(abe_net_loop_t* loop)
{
    abe_mem_pool_t* mem_pool;

    if (loop == NULL) {
        return;
    }
    mem_pool = loop->mem_pool;
    if (loop->base != NULL) {
        event_base_free(loop->base);
        loop->base = NULL;
    }
    abe_mem_pool_destroy(mem_pool);
}

int abe_net_loop_run(abe_net_loop_t* loop)
{
    if (loop == NULL || loop->base == NULL) {
        return ABE_NET_INVALID_ARG;
    }
    return event_base_dispatch(loop->base) >= 0 ? ABE_NET_OK : ABE_NET_ERROR;
}

int abe_net_loop_run_once(abe_net_loop_t* loop)
{
    if (loop == NULL || loop->base == NULL) {
        return ABE_NET_INVALID_ARG;
    }
    return event_base_loop(loop->base, EVLOOP_ONCE) >= 0 ? ABE_NET_OK : ABE_NET_ERROR;
}

int abe_net_loop_stop(abe_net_loop_t* loop)
{
    if (loop == NULL || loop->base == NULL) {
        return ABE_NET_INVALID_ARG;
    }
    return event_base_loopbreak(loop->base) == 0 ? ABE_NET_OK : ABE_NET_ERROR;
}

int abe_net_tcp_listen(
    abe_net_loop_t* loop,
    const abe_net_tcp_config_t* config,
    abe_net_tcp_listener_t** out_listener)
{
    abe_net_tcp_listener_t* listener;
    struct sockaddr_in addr;
    int backlog;
    int rc;

    if (loop == NULL || loop->base == NULL || config == NULL || out_listener == NULL) {
        return ABE_NET_INVALID_ARG;
    }

    rc = abe_net_make_sockaddr(config->host, config->port, &addr, 1);
    if (rc != ABE_NET_OK) {
        return rc;
    }

    listener = (abe_net_tcp_listener_t*)abe_net_calloc(loop, 1u, sizeof(*listener));
    if (listener == NULL) {
        return ABE_NET_NO_MEMORY;
    }

    listener->loop = loop;
    listener->callbacks = config->callbacks;
    listener->max_packet_size = abe_net_default_max_packet(config->max_packet_size);

    backlog = config->backlog > 0 ? config->backlog : 128;
    listener->listener = evconnlistener_new_bind(
        loop->base,
        abe_net_tcp_on_accept,
        listener,
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
        backlog,
        (struct sockaddr*)&addr,
        sizeof(addr));
    if (listener->listener == NULL) {
        abe_net_release(loop, listener);
        return ABE_NET_ERROR;
    }

    *out_listener = listener;
    return ABE_NET_OK;
}

void abe_net_tcp_listener_close(abe_net_tcp_listener_t* listener)
{
    if (listener == NULL) {
        return;
    }
    if (listener->listener != NULL) {
        evconnlistener_free(listener->listener);
        listener->listener = NULL;
    }
    abe_net_release(listener->loop, listener);
}

int abe_net_tcp_connect(
    abe_net_loop_t* loop,
    const abe_net_tcp_config_t* config,
    abe_net_tcp_conn_t** out_conn)
{
    abe_net_tcp_conn_t* conn;
    struct sockaddr_in addr;
    int rc;

    if (loop == NULL || loop->base == NULL || config == NULL || out_conn == NULL) {
        return ABE_NET_INVALID_ARG;
    }

    rc = abe_net_make_sockaddr(config->host, config->port, &addr, 0);
    if (rc != ABE_NET_OK) {
        return rc;
    }

    conn = (abe_net_tcp_conn_t*)abe_net_calloc(loop, 1u, sizeof(*conn));
    if (conn == NULL) {
        return ABE_NET_NO_MEMORY;
    }

    conn->loop = loop;
    conn->callbacks = config->callbacks;
    conn->max_packet_size = abe_net_default_max_packet(config->max_packet_size);
    conn->bev = bufferevent_socket_new(
        loop->base,
        -1,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
    if (conn->bev == NULL) {
        abe_net_release(loop, conn);
        return ABE_NET_ERROR;
    }

    bufferevent_setcb(conn->bev, abe_net_tcp_read_cb, NULL, abe_net_tcp_on_event, conn);
    bufferevent_enable(conn->bev, EV_READ | EV_WRITE);

    if (bufferevent_socket_connect(
            conn->bev,
            (struct sockaddr*)&addr,
            sizeof(addr)) != 0) {
        abe_net_tcp_free(conn);
        return ABE_NET_ERROR;
    }

    *out_conn = conn;
    return ABE_NET_OK;
}

int abe_net_tcp_send(abe_net_tcp_conn_t* conn, const void* data, uint32_t size)
{
    unsigned char header[ABE_NET_PACKET_HEADER_SIZE];

    if (conn == NULL || conn->bev == NULL || (size > 0 && data == NULL)) {
        return ABE_NET_INVALID_ARG;
    }
    if (size > conn->max_packet_size) {
        return ABE_NET_PACKET_TOO_LARGE;
    }

    abe_net_u32_to_be(size, header);
    if (bufferevent_write(conn->bev, header, sizeof(header)) != 0) {
        return ABE_NET_ERROR;
    }
    if (size > 0 && bufferevent_write(conn->bev, data, size) != 0) {
        return ABE_NET_ERROR;
    }

    return ABE_NET_OK;
}

void abe_net_tcp_close(abe_net_tcp_conn_t* conn)
{
    abe_net_tcp_begin_close(conn, 1);
}

void abe_net_tcp_set_user_data(abe_net_tcp_conn_t* conn, void* user_data)
{
    if (conn != NULL) {
        conn->callbacks.user_data = user_data;
    }
}

void* abe_net_tcp_get_user_data(abe_net_tcp_conn_t* conn)
{
    if (conn == NULL) {
        return NULL;
    }
    return conn->callbacks.user_data;
}

int abe_net_tcp_get_peer_addr(abe_net_tcp_conn_t* conn, abe_net_addr_t* out_addr)
{
    struct sockaddr_storage addr;
    socklen_t len;
    evutil_socket_t fd;

    if (conn == NULL || conn->bev == NULL || out_addr == NULL) {
        return ABE_NET_INVALID_ARG;
    }

    fd = bufferevent_getfd(conn->bev);
    if (fd < 0) {
        return ABE_NET_ERROR;
    }

    memset(&addr, 0, sizeof(addr));
    len = sizeof(addr);
    if (getpeername(fd, (struct sockaddr*)&addr, &len) != 0) {
        return ABE_NET_ERROR;
    }

    abe_net_fill_addr((const struct sockaddr*)&addr, out_addr);
    return ABE_NET_OK;
}

int abe_net_udp_bind(
    abe_net_loop_t* loop,
    const abe_net_udp_config_t* config,
    abe_net_udp_endpoint_t** out_endpoint)
{
    abe_net_udp_endpoint_t* endpoint;
    struct sockaddr_in addr;
    evutil_socket_t fd;
    int rc;
    int reuse;

    if (loop == NULL || loop->base == NULL || config == NULL || out_endpoint == NULL) {
        return ABE_NET_INVALID_ARG;
    }

    rc = abe_net_make_sockaddr(config->host, config->port, &addr, 1);
    if (rc != ABE_NET_OK) {
        return rc;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return ABE_NET_ERROR;
    }

    reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (evutil_make_socket_nonblocking(fd) != 0) {
        evutil_closesocket(fd);
        return ABE_NET_ERROR;
    }

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        evutil_closesocket(fd);
        return ABE_NET_ERROR;
    }

    endpoint = (abe_net_udp_endpoint_t*)abe_net_calloc(loop, 1u, sizeof(*endpoint));
    if (endpoint == NULL) {
        evutil_closesocket(fd);
        return ABE_NET_NO_MEMORY;
    }

    endpoint->loop = loop;
    endpoint->fd = fd;
    endpoint->callbacks = config->callbacks;
    endpoint->max_packet_size = abe_net_default_max_packet(config->max_packet_size);
    endpoint->read_event = event_new(loop->base, fd, EV_READ | EV_PERSIST, abe_net_udp_read_cb, endpoint);
    if (endpoint->read_event == NULL) {
        evutil_closesocket(fd);
        abe_net_release(loop, endpoint);
        return ABE_NET_ERROR;
    }

    if (event_add(endpoint->read_event, NULL) != 0) {
        event_free(endpoint->read_event);
        evutil_closesocket(fd);
        abe_net_release(loop, endpoint);
        return ABE_NET_ERROR;
    }

    *out_endpoint = endpoint;
    return ABE_NET_OK;
}

int abe_net_udp_send_to(
    abe_net_udp_endpoint_t* endpoint,
    const char* host,
    uint16_t port,
    const void* data,
    uint32_t size)
{
    struct sockaddr_in addr;
    ssize_t sent;
    int rc;

    if (endpoint == NULL || endpoint->closing || endpoint->fd < 0 ||
        (size > 0 && data == NULL)) {
        return ABE_NET_INVALID_ARG;
    }
    if (size > endpoint->max_packet_size) {
        return ABE_NET_PACKET_TOO_LARGE;
    }

    rc = abe_net_make_sockaddr(host, port, &addr, 0);
    if (rc != ABE_NET_OK) {
        return rc;
    }

    sent = sendto(
        endpoint->fd,
        data,
        size,
        0,
        (struct sockaddr*)&addr,
        sizeof(addr));
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return ABE_NET_WOULD_BLOCK;
        }
        return ABE_NET_ERROR;
    }
    if ((uint32_t)sent != size) {
        return ABE_NET_ERROR;
    }

    return ABE_NET_OK;
}

void abe_net_udp_close(abe_net_udp_endpoint_t* endpoint)
{
    abe_net_udp_begin_close(endpoint);
}
