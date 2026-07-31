#include "abe_net_server.h"

#include <stddef.h>
#include <string.h>

namespace abe {
namespace adapter {
namespace net {

TcpServer::TcpServer()
    : loop_(NULL),
      links_(NULL),
      link_count_(0),
      active_count_(0u),
      max_packet_size_(0)
{
    memset(&callbacks_, 0, sizeof(callbacks_));
}

TcpServer::~TcpServer()
{
    close();
}

int TcpServer::init(Loop* loop, const TcpServerConfig* config)
{
    TcpConfig listen_config;
    int rc;

    if (loop == NULL || !loop->valid() ||
        config == NULL ||
        config->link_count == 0u ||
        (config->links == NULL &&
         config->callbacks.acquire_link == NULL)) {
        return ABE_NET_INVALID_ARG;
    }

    close();
    loop_ = loop;
    links_ = config->links;
    link_count_ = config->link_count;
    active_count_ = 0u;
    max_packet_size_ = config->max_packet_size;
    callbacks_ = config->callbacks;

    memset(&listen_config, 0, sizeof(listen_config));
    listen_config.host = config->host;
    listen_config.port = config->port;
    listen_config.max_packet_size = config->max_packet_size;
    listen_config.backlog = config->backlog;
    listen_config.callbacks.on_accept = TcpServer::on_accept;
    listen_config.callbacks.user_data = this;
    rc = listener_.listen(loop, &listen_config);
    if (rc != ABE_NET_OK) {
        close();
        return rc;
    }
    return ABE_NET_OK;
}

int TcpServer::update()
{
    if (loop_ == NULL || !loop_->valid()) {
        return ABE_NET_INVALID_ARG;
    }
    return loop_->update();
}

void TcpServer::close()
{
    uint32_t index;

    listener_.close();
    if (links_ != NULL) {
        for (index = 0u; index < link_count_; ++index) {
            links_[index].disconnect();
        }
    }
    loop_ = NULL;
    links_ = NULL;
    link_count_ = 0u;
    active_count_ = 0u;
    max_packet_size_ = 0u;
    memset(&callbacks_, 0, sizeof(callbacks_));
}

int TcpServer::send_to_all(const void* data, uint32_t size)
{
    uint32_t index;
    int rc;

    if (links_ == NULL) {
        return ABE_NET_INVALID_ARG;
    }

    for (index = 0u; index < link_count_; ++index) {
        if (links_[index].valid()) {
            rc = links_[index].send(data, size);
            if (rc != ABE_NET_OK) {
                return rc;
            }
        }
    }
    return ABE_NET_OK;
}

uint32_t TcpServer::active_count() const
{
    uint32_t index;
    uint32_t count;

    count = 0u;
    if (links_ == NULL) {
        return active_count_;
    }
    for (index = 0u; index < link_count_; ++index) {
        if (links_[index].valid()) {
            count += 1u;
        }
    }
    return count;
}

uint32_t TcpServer::capacity() const
{
    return link_count_;
}

int TcpServer::valid() const
{
    return loop_ != NULL && listener_.valid() && links_ != NULL;
}

TcpLink* TcpServer::find_free_link()
{
    uint32_t index;

    if (links_ == NULL) {
        if (callbacks_.acquire_link == NULL ||
            active_count_ >= link_count_) {
            return NULL;
        }
        return callbacks_.acquire_link(this, callbacks_.user_data);
    }
    for (index = 0u; index < link_count_; ++index) {
        if (!links_[index].valid()) {
            return &links_[index];
        }
    }
    return NULL;
}

TcpLink* TcpServer::find_link(TcpLink* link)
{
    uint32_t index;

    if (link == NULL) {
        return NULL;
    }
    if (links_ == NULL) {
        return link;
    }
    for (index = 0u; index < link_count_; ++index) {
        if (&links_[index] == link) {
            return &links_[index];
        }
    }
    return NULL;
}

void TcpServer::on_accept(
    TcpListener* listener,
    TcpLink* link,
    const abe_net_addr_t* peer,
    void* user_data)
{
    TcpServer* server;
    TcpLink* slot;

    (void)listener;
    (void)peer;
    server = (TcpServer*)user_data;
    if (server == NULL || link == NULL) {
        return;
    }

    slot = server->find_free_link();
    if (slot == NULL) {
        link->disconnect();
        return;
    }

    if (slot->attach(link, 1) != ABE_NET_OK) {
        link->disconnect();
        return;
    }

    slot->set_user_data(server);
    slot->on_receive(TcpServer::on_link_receive);
    slot->on_disconnect(TcpServer::on_link_disconnect);
    ++server->active_count_;
    if (server->callbacks_.on_connect != NULL) {
        server->callbacks_.on_connect(server, slot, server->callbacks_.user_data);
    }
}

void TcpServer::on_link_receive(
    TcpLink* link,
    const void* data,
    uint32_t size,
    void* user_data)
{
    TcpServer* server;

    server = (TcpServer*)user_data;
    if (server != NULL &&
        server->find_link(link) != NULL &&
        server->callbacks_.on_receive != NULL) {
        server->callbacks_.on_receive(link, data, size, server->callbacks_.user_data);
    }
}

void TcpServer::on_link_disconnect(
    TcpLink* link,
    int error_code,
    void* user_data)
{
    TcpServer* server;

    server = (TcpServer*)user_data;
    if (server != NULL && server->find_link(link) != NULL) {
        if (server->callbacks_.on_disconnect != NULL) {
            server->callbacks_.on_disconnect(link, error_code, server->callbacks_.user_data);
        }
        if (link != NULL) {
            link->detach();
        }
        if (server->active_count_ > 0u) {
            --server->active_count_;
        }
    }
}

TcpClient::TcpClient()
    : loop_(NULL),
      host_(NULL),
      port_(0),
      max_packet_size_(0),
      links_(NULL),
      link_count_(0)
{
    memset(&callbacks_, 0, sizeof(callbacks_));
}

TcpClient::~TcpClient()
{
    close();
}

int TcpClient::init(Loop* loop, const TcpClientConfig* config)
{
    if (loop == NULL || !loop->valid() ||
        config == NULL || config->host == NULL ||
        config->links == NULL || config->link_count == 0u) {
        return ABE_NET_INVALID_ARG;
    }

    close();
    loop_ = loop;
    host_ = config->host;
    port_ = config->port;
    max_packet_size_ = config->max_packet_size;
    links_ = config->links;
    link_count_ = config->link_count;
    callbacks_ = config->callbacks;
    return ABE_NET_OK;
}

int TcpClient::update()
{
    if (loop_ == NULL || !loop_->valid()) {
        return ABE_NET_INVALID_ARG;
    }
    return loop_->update();
}

void TcpClient::close()
{
    uint32_t index;

    if (links_ != NULL) {
        for (index = 0u; index < link_count_; ++index) {
            links_[index].disconnect();
        }
    }
    loop_ = NULL;
    host_ = NULL;
    port_ = 0;
    max_packet_size_ = 0u;
    links_ = NULL;
    link_count_ = 0u;
    memset(&callbacks_, 0, sizeof(callbacks_));
}

int TcpClient::connect_one()
{
    TcpConfig config;
    TcpLink* slot;

    if (loop_ == NULL || host_ == NULL) {
        return ABE_NET_INVALID_ARG;
    }

    slot = find_free_link();
    if (slot == NULL) {
        return ABE_NET_ERROR;
    }

    memset(&config, 0, sizeof(config));
    config.host = host_;
    config.port = port_;
    config.max_packet_size = max_packet_size_;
    config.callbacks.on_connect = TcpClient::on_link_connect;
    config.callbacks.on_receive = TcpClient::on_link_receive;
    config.callbacks.on_disconnect = TcpClient::on_link_disconnect;
    config.callbacks.user_data = this;
    return slot->connect(loop_, &config);
}

int TcpClient::send_to_all(const void* data, uint32_t size)
{
    uint32_t index;
    int rc;

    if (links_ == NULL) {
        return ABE_NET_INVALID_ARG;
    }

    for (index = 0u; index < link_count_; ++index) {
        if (links_[index].valid()) {
            rc = links_[index].send(data, size);
            if (rc != ABE_NET_OK) {
                return rc;
            }
        }
    }
    return ABE_NET_OK;
}

uint32_t TcpClient::active_count() const
{
    uint32_t index;
    uint32_t count;

    count = 0u;
    if (links_ == NULL) {
        return 0u;
    }
    for (index = 0u; index < link_count_; ++index) {
        if (links_[index].valid()) {
            count += 1u;
        }
    }
    return count;
}

uint32_t TcpClient::capacity() const
{
    return link_count_;
}

int TcpClient::valid() const
{
    return loop_ != NULL && host_ != NULL && links_ != NULL;
}

TcpLink* TcpClient::find_free_link()
{
    uint32_t index;

    if (links_ == NULL) {
        return NULL;
    }
    for (index = 0u; index < link_count_; ++index) {
        if (!links_[index].valid()) {
            return &links_[index];
        }
    }
    return NULL;
}

TcpLink* TcpClient::find_link(TcpLink* link)
{
    uint32_t index;

    if (links_ == NULL || link == NULL) {
        return NULL;
    }
    for (index = 0u; index < link_count_; ++index) {
        if (&links_[index] == link) {
            return &links_[index];
        }
    }
    return NULL;
}

void TcpClient::on_link_connect(TcpLink* link, void* user_data)
{
    TcpClient* client;

    client = (TcpClient*)user_data;
    if (client != NULL &&
        client->find_link(link) != NULL &&
        client->callbacks_.on_connect != NULL) {
        client->callbacks_.on_connect(client, link, client->callbacks_.user_data);
    }
}

void TcpClient::on_link_receive(
    TcpLink* link,
    const void* data,
    uint32_t size,
    void* user_data)
{
    TcpClient* client;

    client = (TcpClient*)user_data;
    if (client != NULL &&
        client->find_link(link) != NULL &&
        client->callbacks_.on_receive != NULL) {
        client->callbacks_.on_receive(link, data, size, client->callbacks_.user_data);
    }
}

void TcpClient::on_link_disconnect(
    TcpLink* link,
    int error_code,
    void* user_data)
{
    TcpClient* client;

    client = (TcpClient*)user_data;
    if (client != NULL && client->find_link(link) != NULL) {
        if (client->callbacks_.on_disconnect != NULL) {
            client->callbacks_.on_disconnect(link, error_code, client->callbacks_.user_data);
        }
        if (link != NULL) {
            link->detach();
        }
    }
}

} /* namespace net */
} /* namespace adapter */
} /* namespace abe */
