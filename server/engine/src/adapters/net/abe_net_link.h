#ifndef ABE_NET_LINK_H
#define ABE_NET_LINK_H

#include "abe_net.h"

#include <stdint.h>

namespace abe {
namespace adapter {
namespace net {

class Loop;
class TcpLink;
class TcpListener;
class UdpLink;

typedef void (*TcpAcceptCallback)(
    TcpListener* listener,
    TcpLink* link,
    const abe_net_addr_t* peer,
    void* user_data);

typedef void (*TcpReceiveCallback)(
    TcpLink* link,
    const void* data,
    uint32_t size,
    void* user_data);

typedef void (*TcpConnectCallback)(
    TcpLink* link,
    void* user_data);

typedef void (*TcpDisconnectCallback)(
    TcpLink* link,
    int error_code,
    void* user_data);

typedef void (*TcpEventCallback)(
    TcpLink* link,
    abe_net_tcp_event_t event,
    int error_code,
    void* user_data);

typedef void (*UdpReceiveCallback)(
    UdpLink* link,
    const void* data,
    uint32_t size,
    const abe_net_addr_t* peer,
    void* user_data);

struct TcpCallbacks {
    TcpAcceptCallback on_accept;
    TcpConnectCallback on_connect;
    TcpReceiveCallback on_receive;
    TcpDisconnectCallback on_disconnect;
    TcpEventCallback on_event;
    void* user_data;
};

struct UdpCallbacks {
    UdpReceiveCallback on_receive;
    void* user_data;
};

struct TcpConfig {
    const char* host;
    uint16_t port;
    uint32_t max_packet_size;
    int backlog;
    TcpCallbacks callbacks;
};

struct UdpConfig {
    const char* host;
    uint16_t port;
    uint32_t max_packet_size;
    UdpCallbacks callbacks;
};

class Loop {
public:
    Loop();
    ~Loop();

    int create();
    void destroy();
    int run();
    int run_once();
    int update();
    int stop();

    int valid() const;
    abe_net_loop_t* handle() const;

private:
    Loop(const Loop&);
    Loop& operator=(const Loop&);

    abe_net_loop_t* loop_;
};

class TcpLink {
public:
    TcpLink();
    TcpLink(TcpLink&& other);
    ~TcpLink();
    TcpLink& operator=(TcpLink&& other);

    /*
     * TcpLink wraps one TCP connection. connect() creates an outbound
     * connection. Incoming bytes are delivered through on_receive.
     * Disconnects are delivered through on_disconnect.
     * attach() takes over a callback-local accepted link.
     */
    int connect(Loop* loop, const TcpConfig* config);
    int connect(Loop* loop, const char* host, uint16_t port);
    int attach(TcpLink* link, int close_on_destroy);
    void detach();
    void disconnect();
    void close();

    void set_callbacks(const TcpCallbacks* callbacks);
    void set_user_data(void* user_data);
    void on_connect(TcpConnectCallback callback);
    void on_receive(TcpReceiveCallback callback);
    void on_disconnect(TcpDisconnectCallback callback);
    void on_event(TcpEventCallback callback);

    int send(const void* data, uint32_t size);
    int get_peer_addr(abe_net_addr_t* out_addr) const;

    int valid() const;
    int close_on_destroy() const;
    abe_net_tcp_conn_t* handle() const;

private:
    friend class TcpListener;

    int attach_handle(
        abe_net_tcp_conn_t* conn,
        int close_on_destroy,
        const TcpCallbacks* callbacks,
        int install_callbacks);

    static void fill_raw_callbacks(
        abe_net_tcp_callbacks_t* raw_callbacks,
        void* user_data);
    static void raw_message_callback(
        abe_net_tcp_conn_t* conn,
        const void* data,
        uint32_t size,
        void* user_data);
    static void raw_event_callback(
        abe_net_tcp_conn_t* conn,
        abe_net_tcp_event_t event,
        int error_code,
        void* user_data);

    TcpLink(const TcpLink&);
    TcpLink& operator=(const TcpLink&);

    abe_net_tcp_conn_t* conn_;
    int close_on_destroy_;
    TcpCallbacks callbacks_;
};

class TcpListener {
public:
    TcpListener();
    ~TcpListener();

    /*
     * TcpListener owns the listen socket. Each accepted connection is exposed
     * as a callback-local TcpLink.
     */
    int listen(Loop* loop, const TcpConfig* config);
    void close();

    int valid() const;
    abe_net_tcp_listener_t* handle() const;

private:
    static void raw_accept_callback(
        abe_net_tcp_listener_t* listener,
        abe_net_tcp_conn_t* conn,
        const abe_net_addr_t* peer,
        void* user_data);
    static void raw_message_callback(
        abe_net_tcp_conn_t* conn,
        const void* data,
        uint32_t size,
        void* user_data);
    static void raw_event_callback(
        abe_net_tcp_conn_t* conn,
        abe_net_tcp_event_t event,
        int error_code,
        void* user_data);

    TcpListener(const TcpListener&);
    TcpListener& operator=(const TcpListener&);

    abe_net_tcp_listener_t* listener_;
    TcpCallbacks callbacks_;
};

class UdpLink {
public:
    UdpLink();
    ~UdpLink();

    /*
     * UdpLink wraps one UDP endpoint. bind() opens the local endpoint and
     * datagrams are delivered through on_receive.
     */
    int bind(Loop* loop, const UdpConfig* config);
    int bind(Loop* loop, const char* host, uint16_t port);
    void unbind();
    void close();

    void set_callbacks(const UdpCallbacks* callbacks);
    void set_user_data(void* user_data);
    void on_receive(UdpReceiveCallback callback);

    int send_to(const char* host, uint16_t port, const void* data, uint32_t size);

    int valid() const;
    abe_net_udp_endpoint_t* handle() const;

private:
    static void raw_message_callback(
        abe_net_udp_endpoint_t* endpoint,
        const void* data,
        uint32_t size,
        const abe_net_addr_t* peer,
        void* user_data);

    UdpLink(const UdpLink&);
    UdpLink& operator=(const UdpLink&);

    abe_net_udp_endpoint_t* endpoint_;
    UdpCallbacks callbacks_;
};

} /* namespace net */
} /* namespace adapter */
} /* namespace abe */

#endif /* ABE_NET_LINK_H */
