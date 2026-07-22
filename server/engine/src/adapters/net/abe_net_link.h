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

typedef void (*TcpMessageCallback)(
    TcpLink* link,
    const void* data,
    uint32_t size,
    void* user_data);

typedef void (*TcpEventCallback)(
    TcpLink* link,
    abe_net_tcp_event_t event,
    int error_code,
    void* user_data);

typedef void (*UdpMessageCallback)(
    UdpLink* link,
    const void* data,
    uint32_t size,
    const abe_net_addr_t* peer,
    void* user_data);

struct TcpCallbacks {
    TcpAcceptCallback on_accept;
    TcpMessageCallback on_message;
    TcpEventCallback on_event;
    void* user_data;
};

struct UdpCallbacks {
    UdpMessageCallback on_message;
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
    ~TcpLink();

    int connect(Loop* loop, const TcpConfig* config);
    int attach(abe_net_tcp_conn_t* conn, int close_on_destroy);
    void detach();
    void close();

    int send(const void* data, uint32_t size);
    int get_peer_addr(abe_net_addr_t* out_addr) const;

    int valid() const;
    int close_on_destroy() const;
    abe_net_tcp_conn_t* handle() const;

private:
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

    int bind(Loop* loop, const UdpConfig* config);
    void close();

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
