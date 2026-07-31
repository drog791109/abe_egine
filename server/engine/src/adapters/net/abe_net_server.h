#ifndef ABE_NET_SERVER_H
#define ABE_NET_SERVER_H

#include "abe_net_link.h"

#include <stdint.h>

namespace abe {
namespace adapter {
namespace net {

class TcpServer;
class TcpClient;

typedef void (*ServerLinkCallback)(
    TcpServer* server,
    TcpLink* link,
    void* user_data);

typedef TcpLink* (*ServerAcquireLinkCallback)(
    TcpServer* server,
    void* user_data);

typedef void (*ClientLinkCallback)(
    TcpClient* client,
    TcpLink* link,
    void* user_data);

struct TcpServerCallbacks {
    ServerAcquireLinkCallback acquire_link;
    ServerLinkCallback on_connect;
    TcpReceiveCallback on_receive;
    TcpDisconnectCallback on_disconnect;
    void* user_data;
};

struct TcpServerConfig {
    const char* host;
    uint16_t port;
    uint32_t max_packet_size;
    int backlog;
    /*
     * links may be NULL when callbacks.acquire_link supplies slots.
     * link_count is still the maximum accepted connection count.
     */
    TcpLink* links;
    uint32_t link_count;
    TcpServerCallbacks callbacks;
};

struct TcpClientCallbacks {
    ClientLinkCallback on_connect;
    TcpReceiveCallback on_receive;
    TcpDisconnectCallback on_disconnect;
    void* user_data;
};

struct TcpClientConfig {
    /* host is kept by pointer and must outlive TcpClient::close(). */
    const char* host;
    uint16_t port;
    uint32_t max_packet_size;
    TcpLink* links;
    uint32_t link_count;
    TcpClientCallbacks callbacks;
};

class TcpServer {
public:
    TcpServer();
    ~TcpServer();

    /*
     * TcpServer owns one listener and uses caller-provided TcpLink slots for
     * accepted connections. Slots may come from config.links or from
     * callbacks.acquire_link. The slots must outlive close().
     */
    int init(Loop* loop, const TcpServerConfig* config);
    int update();
    void close();

    int send_to_all(const void* data, uint32_t size);
    uint32_t active_count() const;
    uint32_t capacity() const;
    int valid() const;

private:
    static void on_accept(
        TcpListener* listener,
        TcpLink* link,
        const abe_net_addr_t* peer,
        void* user_data);
    static void on_link_receive(
        TcpLink* link,
        const void* data,
        uint32_t size,
        void* user_data);
    static void on_link_disconnect(
        TcpLink* link,
        int error_code,
        void* user_data);

    TcpServer(const TcpServer&);
    TcpServer& operator=(const TcpServer&);

    TcpLink* find_free_link();
    TcpLink* find_link(TcpLink* link);

    Loop* loop_;
    TcpListener listener_;
    TcpLink* links_;
    uint32_t link_count_;
    uint32_t active_count_;
    uint32_t max_packet_size_;
    TcpServerCallbacks callbacks_;
};

class TcpClient {
public:
    TcpClient();
    ~TcpClient();

    /*
     * TcpClient uses caller-provided TcpLink storage for outbound
     * connections. Call connect_one() once for each connection to start.
     */
    int init(Loop* loop, const TcpClientConfig* config);
    int update();
    void close();

    int connect_one();
    int send_to_all(const void* data, uint32_t size);
    uint32_t active_count() const;
    uint32_t capacity() const;
    int valid() const;

private:
    static void on_link_connect(
        TcpLink* link,
        void* user_data);
    static void on_link_receive(
        TcpLink* link,
        const void* data,
        uint32_t size,
        void* user_data);
    static void on_link_disconnect(
        TcpLink* link,
        int error_code,
        void* user_data);

    TcpClient(const TcpClient&);
    TcpClient& operator=(const TcpClient&);

    TcpLink* find_free_link();
    TcpLink* find_link(TcpLink* link);

    Loop* loop_;
    const char* host_;
    uint16_t port_;
    uint32_t max_packet_size_;
    TcpLink* links_;
    uint32_t link_count_;
    TcpClientCallbacks callbacks_;
};

} /* namespace net */
} /* namespace adapter */
} /* namespace abe */

#endif /* ABE_NET_SERVER_H */
