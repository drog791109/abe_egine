#include "abe_net_link.h"

#include <stddef.h>
#include <string.h>

namespace abe {
namespace adapter {
namespace net {

static void tcp_callbacks_clear(TcpCallbacks* callbacks)
{
    if (callbacks != NULL) {
        memset(callbacks, 0, sizeof(*callbacks));
    }
}

static void udp_callbacks_clear(UdpCallbacks* callbacks)
{
    if (callbacks != NULL) {
        memset(callbacks, 0, sizeof(*callbacks));
    }
}

void TcpLink::fill_raw_callbacks(
    abe_net_tcp_callbacks_t* raw_callbacks,
    void* user_data)
{
    if (raw_callbacks == NULL) {
        return;
    }

    memset(raw_callbacks, 0, sizeof(*raw_callbacks));
    raw_callbacks->on_message = TcpLink::raw_message_callback;
    raw_callbacks->on_event = TcpLink::raw_event_callback;
    raw_callbacks->user_data = user_data;
}

void TcpLink::raw_message_callback(
    abe_net_tcp_conn_t* conn,
    const void* data,
    uint32_t size,
    void* user_data)
{
    TcpLink* link;

    (void)conn;
    link = (TcpLink*)user_data;
    if (link != NULL && link->valid()) {
        TcpCallbacks callbacks;

        callbacks = link->callbacks_;
        if (callbacks.on_receive != NULL) {
            callbacks.on_receive(link, data, size, callbacks.user_data);
        }
    }
}

void TcpLink::raw_event_callback(
    abe_net_tcp_conn_t* conn,
    abe_net_tcp_event_t event,
    int error_code,
    void* user_data)
{
    TcpLink* link;

    (void)conn;
    link = (TcpLink*)user_data;
    if (link != NULL && link->valid()) {
        TcpCallbacks callbacks;

        callbacks = link->callbacks_;
        if (event == ABE_NET_TCP_EVENT_CONNECTED &&
            callbacks.on_connect != NULL) {
            callbacks.on_connect(link, callbacks.user_data);
        }
        if (callbacks.on_event != NULL) {
            callbacks.on_event(link, event, error_code, callbacks.user_data);
        }
        if (event == ABE_NET_TCP_EVENT_CLOSED ||
            event == ABE_NET_TCP_EVENT_ERROR) {
            if (callbacks.on_disconnect != NULL) {
                callbacks.on_disconnect(link, error_code, callbacks.user_data);
            }
            link->detach();
        }
    }
}

void TcpListener::raw_accept_callback(
    abe_net_tcp_listener_t* listener,
    abe_net_tcp_conn_t* conn,
    const abe_net_addr_t* peer,
    void* user_data)
{
    TcpListener* tcp_listener;

    (void)listener;
    tcp_listener = (TcpListener*)user_data;
    if (tcp_listener != NULL && tcp_listener->callbacks_.on_accept != NULL) {
        TcpLink link;
        TcpCallbacks callbacks;

        callbacks = tcp_listener->callbacks_;
        (void)link.attach_handle(conn, 0, &callbacks, 0);
        callbacks.on_accept(
            tcp_listener,
            &link,
            peer,
            callbacks.user_data);
    }
}

void TcpListener::raw_message_callback(
    abe_net_tcp_conn_t* conn,
    const void* data,
    uint32_t size,
    void* user_data)
{
    TcpListener* listener;

    listener = (TcpListener*)user_data;
    if (listener != NULL && listener->callbacks_.on_receive != NULL) {
        TcpLink link;
        TcpCallbacks callbacks;

        callbacks = listener->callbacks_;
        (void)link.attach_handle(conn, 0, &callbacks, 0);
        callbacks.on_receive(
            &link,
            data,
            size,
            callbacks.user_data);
    }
}

void TcpListener::raw_event_callback(
    abe_net_tcp_conn_t* conn,
    abe_net_tcp_event_t event,
    int error_code,
    void* user_data)
{
    TcpListener* listener;

    listener = (TcpListener*)user_data;
    if (listener != NULL && listener->callbacks_.on_event != NULL) {
        TcpLink link;
        TcpCallbacks callbacks;

        callbacks = listener->callbacks_;
        (void)link.attach_handle(conn, 0, &callbacks, 0);
        callbacks.on_event(
            &link,
            event,
            error_code,
            callbacks.user_data);
        if ((event == ABE_NET_TCP_EVENT_CLOSED ||
             event == ABE_NET_TCP_EVENT_ERROR) &&
            callbacks.on_disconnect != NULL) {
            callbacks.on_disconnect(&link, error_code, callbacks.user_data);
        }
    } else if (listener != NULL && listener->callbacks_.on_disconnect != NULL) {
        TcpLink link;
        TcpCallbacks callbacks;

        callbacks = listener->callbacks_;
        (void)link.attach_handle(conn, 0, &callbacks, 0);
        if (event == ABE_NET_TCP_EVENT_CLOSED ||
            event == ABE_NET_TCP_EVENT_ERROR) {
            callbacks.on_disconnect(&link, error_code, callbacks.user_data);
        }
    }
}

void UdpLink::raw_message_callback(
    abe_net_udp_endpoint_t* endpoint,
    const void* data,
    uint32_t size,
    const abe_net_addr_t* peer,
    void* user_data)
{
    UdpLink* link;

    (void)endpoint;
    link = (UdpLink*)user_data;
    if (link != NULL && link->valid()) {
        UdpCallbacks callbacks;

        callbacks = link->callbacks_;
        if (callbacks.on_receive != NULL) {
            callbacks.on_receive(link, data, size, peer, callbacks.user_data);
        }
    }
}

Loop::Loop() : loop_(NULL)
{
}

Loop::~Loop()
{
    destroy();
}

int Loop::create()
{
    destroy();
    return abe_net_loop_create(&loop_);
}

void Loop::destroy()
{
    if (loop_ != NULL) {
        abe_net_loop_destroy(loop_);
        loop_ = NULL;
    }
}

int Loop::run()
{
    return abe_net_loop_run(loop_);
}

int Loop::run_once()
{
    return abe_net_loop_run_once(loop_);
}

int Loop::update()
{
    return abe_net_loop_update(loop_);
}

int Loop::stop()
{
    return abe_net_loop_stop(loop_);
}

int Loop::valid() const
{
    return loop_ != NULL;
}

abe_net_loop_t* Loop::handle() const
{
    return loop_;
}

TcpLink::TcpLink() : conn_(NULL), close_on_destroy_(0)
{
    tcp_callbacks_clear(&callbacks_);
}

TcpLink::TcpLink(TcpLink&& other)
    : conn_(other.conn_),
      close_on_destroy_(other.close_on_destroy_),
      callbacks_(other.callbacks_)
{
    abe_net_tcp_callbacks_t raw_callbacks;

    other.conn_ = NULL;
    other.close_on_destroy_ = 0;
    tcp_callbacks_clear(&other.callbacks_);

    if (conn_ != NULL) {
        TcpLink::fill_raw_callbacks(&raw_callbacks, this);
        abe_net_tcp_set_callbacks(conn_, &raw_callbacks);
    }
}

TcpLink::~TcpLink()
{
    if (conn_ != NULL && close_on_destroy_ != 0) {
        abe_net_tcp_close(conn_);
    }
    conn_ = NULL;
    close_on_destroy_ = 0;
}

TcpLink& TcpLink::operator=(TcpLink&& other)
{
    abe_net_tcp_callbacks_t raw_callbacks;

    if (this == &other) {
        return *this;
    }

    close();
    conn_ = other.conn_;
    close_on_destroy_ = other.close_on_destroy_;
    callbacks_ = other.callbacks_;

    other.conn_ = NULL;
    other.close_on_destroy_ = 0;
    tcp_callbacks_clear(&other.callbacks_);

    if (conn_ != NULL) {
        TcpLink::fill_raw_callbacks(&raw_callbacks, this);
        abe_net_tcp_set_callbacks(conn_, &raw_callbacks);
    }
    return *this;
}

int TcpLink::connect(Loop* loop, const TcpConfig* config)
{
    abe_net_tcp_config_t raw_config;
    abe_net_tcp_conn_t* raw_conn;
    int rc;

    if (loop == NULL || !loop->valid() || config == NULL) {
        return ABE_NET_INVALID_ARG;
    }

    close();
    callbacks_ = config->callbacks;

    memset(&raw_config, 0, sizeof(raw_config));
    raw_config.host = config->host;
    raw_config.port = config->port;
    raw_config.max_packet_size = config->max_packet_size;
    raw_config.backlog = config->backlog;
    TcpLink::fill_raw_callbacks(&raw_config.callbacks, this);

    raw_conn = NULL;
    rc = abe_net_tcp_connect(loop->handle(), &raw_config, &raw_conn);
    if (rc != ABE_NET_OK) {
        tcp_callbacks_clear(&callbacks_);
        return rc;
    }

    conn_ = raw_conn;
    close_on_destroy_ = 1;
    return ABE_NET_OK;
}

int TcpLink::connect(Loop* loop, const char* host, uint16_t port)
{
    TcpConfig config;

    memset(&config, 0, sizeof(config));
    config.host = host;
    config.port = port;
    config.callbacks = callbacks_;
    return connect(loop, &config);
}

int TcpLink::attach(TcpLink* link, int close_on_destroy)
{
    int rc;

    if (link == NULL || !link->valid()) {
        return ABE_NET_INVALID_ARG;
    }
    if (link == this) {
        return ABE_NET_INVALID_ARG;
    }

    rc = attach_handle(link->handle(), close_on_destroy, &link->callbacks_, 1);
    if (rc == ABE_NET_OK) {
        link->detach();
    }
    return rc;
}

void TcpLink::set_callbacks(const TcpCallbacks* callbacks)
{
    abe_net_tcp_callbacks_t raw_callbacks;

    if (callbacks == NULL) {
        tcp_callbacks_clear(&callbacks_);
    } else {
        callbacks_ = *callbacks;
    }

    if (conn_ != NULL) {
        TcpLink::fill_raw_callbacks(&raw_callbacks, this);
        abe_net_tcp_set_callbacks(conn_, &raw_callbacks);
    }
}

void TcpLink::set_user_data(void* user_data)
{
    callbacks_.user_data = user_data;
}

void TcpLink::on_connect(TcpConnectCallback callback)
{
    callbacks_.on_connect = callback;
}

void TcpLink::on_receive(TcpReceiveCallback callback)
{
    callbacks_.on_receive = callback;
}

void TcpLink::on_disconnect(TcpDisconnectCallback callback)
{
    callbacks_.on_disconnect = callback;
}

void TcpLink::on_event(TcpEventCallback callback)
{
    callbacks_.on_event = callback;
}

int TcpLink::attach_handle(
    abe_net_tcp_conn_t* conn,
    int close_on_destroy,
    const TcpCallbacks* callbacks,
    int install_callbacks)
{
    abe_net_tcp_callbacks_t raw_callbacks;

    if (conn == NULL) {
        return ABE_NET_INVALID_ARG;
    }
    if (conn_ != NULL && close_on_destroy_ != 0) {
        abe_net_tcp_close(conn_);
    }
    conn_ = conn;
    close_on_destroy_ = close_on_destroy != 0 ? 1 : 0;
    if (callbacks != NULL) {
        callbacks_ = *callbacks;
    }

    if (install_callbacks != 0) {
        TcpLink::fill_raw_callbacks(&raw_callbacks, this);
        abe_net_tcp_set_callbacks(conn_, &raw_callbacks);
    }
    return ABE_NET_OK;
}

void TcpLink::detach()
{
    conn_ = NULL;
    close_on_destroy_ = 0;
}

void TcpLink::disconnect()
{
    close();
}

void TcpLink::close()
{
    if (conn_ != NULL) {
        abe_net_tcp_close(conn_);
    }
    conn_ = NULL;
    close_on_destroy_ = 0;
}

int TcpLink::send(const void* data, uint32_t size)
{
    return abe_net_tcp_send(conn_, data, size);
}

int TcpLink::get_peer_addr(abe_net_addr_t* out_addr) const
{
    return abe_net_tcp_get_peer_addr(conn_, out_addr);
}

int TcpLink::valid() const
{
    return conn_ != NULL;
}

int TcpLink::close_on_destroy() const
{
    return close_on_destroy_;
}

abe_net_tcp_conn_t* TcpLink::handle() const
{
    return conn_;
}

TcpListener::TcpListener() : listener_(NULL)
{
    tcp_callbacks_clear(&callbacks_);
}

TcpListener::~TcpListener()
{
    close();
}

int TcpListener::listen(Loop* loop, const TcpConfig* config)
{
    abe_net_tcp_config_t raw_config;
    abe_net_tcp_listener_t* raw_listener;
    int rc;

    if (loop == NULL || !loop->valid() || config == NULL) {
        return ABE_NET_INVALID_ARG;
    }

    close();
    callbacks_ = config->callbacks;

    memset(&raw_config, 0, sizeof(raw_config));
    raw_config.host = config->host;
    raw_config.port = config->port;
    raw_config.max_packet_size = config->max_packet_size;
    raw_config.backlog = config->backlog;
    raw_config.callbacks.on_accept = TcpListener::raw_accept_callback;
    raw_config.callbacks.on_message = TcpListener::raw_message_callback;
    raw_config.callbacks.on_event = TcpListener::raw_event_callback;
    raw_config.callbacks.user_data = this;

    raw_listener = NULL;
    rc = abe_net_tcp_listen(loop->handle(), &raw_config, &raw_listener);
    if (rc != ABE_NET_OK) {
        tcp_callbacks_clear(&callbacks_);
        return rc;
    }

    listener_ = raw_listener;
    return ABE_NET_OK;
}

void TcpListener::close()
{
    if (listener_ != NULL) {
        abe_net_tcp_listener_close(listener_);
        listener_ = NULL;
    }
}

int TcpListener::valid() const
{
    return listener_ != NULL;
}

abe_net_tcp_listener_t* TcpListener::handle() const
{
    return listener_;
}

UdpLink::UdpLink() : endpoint_(NULL)
{
    udp_callbacks_clear(&callbacks_);
}

UdpLink::~UdpLink()
{
    close();
}

int UdpLink::bind(Loop* loop, const UdpConfig* config)
{
    abe_net_udp_config_t raw_config;
    abe_net_udp_endpoint_t* raw_endpoint;
    int rc;

    if (loop == NULL || !loop->valid() || config == NULL) {
        return ABE_NET_INVALID_ARG;
    }

    close();
    callbacks_ = config->callbacks;

    memset(&raw_config, 0, sizeof(raw_config));
    raw_config.host = config->host;
    raw_config.port = config->port;
    raw_config.max_packet_size = config->max_packet_size;
    raw_config.callbacks.on_message = UdpLink::raw_message_callback;
    raw_config.callbacks.user_data = this;

    raw_endpoint = NULL;
    rc = abe_net_udp_bind(loop->handle(), &raw_config, &raw_endpoint);
    if (rc != ABE_NET_OK) {
        udp_callbacks_clear(&callbacks_);
        return rc;
    }

    endpoint_ = raw_endpoint;
    return ABE_NET_OK;
}

int UdpLink::bind(Loop* loop, const char* host, uint16_t port)
{
    UdpConfig config;

    memset(&config, 0, sizeof(config));
    config.host = host;
    config.port = port;
    config.callbacks = callbacks_;
    return bind(loop, &config);
}

void UdpLink::set_callbacks(const UdpCallbacks* callbacks)
{
    if (callbacks == NULL) {
        udp_callbacks_clear(&callbacks_);
    } else {
        callbacks_ = *callbacks;
    }
}

void UdpLink::set_user_data(void* user_data)
{
    callbacks_.user_data = user_data;
}

void UdpLink::on_receive(UdpReceiveCallback callback)
{
    callbacks_.on_receive = callback;
}

void UdpLink::unbind()
{
    close();
}

void UdpLink::close()
{
    if (endpoint_ != NULL) {
        abe_net_udp_close(endpoint_);
        endpoint_ = NULL;
    }
}

int UdpLink::send_to(const char* host, uint16_t port, const void* data, uint32_t size)
{
    return abe_net_udp_send_to(endpoint_, host, port, data, size);
}

int UdpLink::valid() const
{
    return endpoint_ != NULL;
}

abe_net_udp_endpoint_t* UdpLink::handle() const
{
    return endpoint_;
}

} /* namespace net */
} /* namespace adapter */
} /* namespace abe */
