#include "abe_gateway_session.h"

#include "abe_log.h"
#include "abe_protocol.h"
#include "abe_time.h"
#include "protocol.pb.h"

#include <new>
#include <stdint.h>
#include <string>

#include <google/protobuf/message_lite.h>

namespace abe {
namespace service {
namespace gateway {

namespace proto = ::abe::proto::client;
namespace service_session = ::abe::service::session;

std::unordered_map<uint32_t, GatewaySession::MessageHandler> GatewaySession::handlers_;
std::once_flag GatewaySession::handlers_once_;

enum {
    GATEWAY_LOGIN_MESSAGE_BEGIN = 11000u,
    GATEWAY_LOGIN_MESSAGE_END = 11999u,
    GATEWAY_LOBBY_MESSAGE_BEGIN = 12000u,
    GATEWAY_LOBBY_MESSAGE_END = 12999u,
    GATEWAY_GAME_MESSAGE_BEGIN = 13000u,
    GATEWAY_GAME_MESSAGE_END = 13999u
};

static bool is_backend_message(uint32_t message_id)
{
    return (message_id >= GATEWAY_LOGIN_MESSAGE_BEGIN &&
            message_id <= GATEWAY_LOGIN_MESSAGE_END) ||
        (message_id >= GATEWAY_LOBBY_MESSAGE_BEGIN &&
         message_id <= GATEWAY_LOBBY_MESSAGE_END) ||
        (message_id >= GATEWAY_GAME_MESSAGE_BEGIN &&
         message_id <= GATEWAY_GAME_MESSAGE_END);
}

static int protocol_status_to_session_status(int status)
{
    if (status == ABE_PROTOCOL_OK) {
        return proto::ERROR_CODE_OK;
    }
    return proto::ERROR_CODE_COMMON_PROTOCOL_ERROR;
}

static int send_proto_message(
    GatewaySession* session,
    uint64_t recv_time_ms,
    uint32_t response_id,
    const google::protobuf::MessageLite& response)
{
    abe_msg_header_t header;
    std::string body;
    uint32_t packet_size;
    unsigned char* packet;
    uint32_t written_size;
    int rc;

    if (session == NULL || response_id == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    if (!response.SerializeToString(&body)) {
        return proto::ERROR_CODE_SYSTEM_INTERNAL;
    }

    abe_msg_header_init(&header);
    header.msg_id = response_id;
    header.session_id = session->conn_id();
    header.timestamp = recv_time_ms;

    rc = abe_msg_packet_get_size((uint32_t)body.size(), &packet_size);
    if (rc != ABE_PROTOCOL_OK) {
        return proto::ERROR_CODE_COMMON_PROTOCOL_ERROR;
    }

    packet = new (std::nothrow) unsigned char[packet_size];
    if (packet == NULL) {
        return proto::ERROR_CODE_COMMON_SERVER_BUSY;
    }

    written_size = 0u;
    rc = abe_msg_packet_encode(
        &header,
        body.empty() ? NULL : body.data(),
        (uint32_t)body.size(),
        packet,
        packet_size,
        &written_size);
    if (rc == ABE_PROTOCOL_OK) {
        rc = session->send(packet, written_size, recv_time_ms);
    }
    delete[] packet;

    if (rc != proto::ERROR_CODE_OK) {
        return proto::ERROR_CODE_COMMON_PROTOCOL_ERROR;
    }

    return proto::ERROR_CODE_OK;
}

GatewaySession::GatewaySession()
{
    std::call_once(handlers_once_, []() {
        register_handlers();
    });
}

void GatewaySession::register_handlers()
{
    handlers_[proto::CS_PING] = &GatewaySession::handle_ping;
}

abe::adapter::net::TcpLink* GatewaySession::link() const
{
    if (!active()) {
        return NULL;
    }
    return (abe::adapter::net::TcpLink*)&tcp_link_;
}

abe::adapter::net::TcpLink* GatewaySession::tcp_link_slot()
{
    return &tcp_link_;
}

int GatewaySession::handle_packet(
    const void* packet,
    uint32_t packet_size,
    uint64_t now_ms)
{
    return receive(packet, packet_size, now_ms);
}

int GatewaySession::on_message(
    const void* packet,
    uint32_t packet_size,
    uint64_t now_ms)
{
    abe_msg_packet_view_t view;
    GatewayMessage message;
    int rc;

    rc = abe_msg_packet_decode(packet, packet_size, &view);
    if (rc != ABE_PROTOCOL_OK) {
        return protocol_status_to_session_status(rc);
    }
    if (view.header.msg_id == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    message.message_id = view.header.msg_id;
    message.data = view.body;
    message.size = view.body_size;
    message.recv_time_ms = now_ms;
    return dispatch_message(message);
}

int GatewaySession::dispatch_message(
    const GatewayMessage& message)
{
    std::unordered_map<uint32_t, MessageHandler>::const_iterator it;

    if (is_backend_message(message.message_id)) {
        return handle_backend_message(message);
    }

    it = handlers_.find(message.message_id);
    if (it == handlers_.end()) {
        return proto::ERROR_CODE_SESSION_NO_HANDLER;
    }
    return (this->*it->second)(message);
}

int GatewaySession::handle_ping(
    const GatewayMessage& message)
{
    proto::PB_CS_PING request;
    proto::PB_SC_PONG response;
    const void* data;
    int rc;

    data = message.data;
    if (data == NULL && message.size == 0u) {
        data = "";
    }
    if (data == NULL ||
        !request.ParseFromArray(data, (int)message.size)) {
        return proto::ERROR_CODE_COMMON_PROTOCOL_ERROR;
    }

    response.mutable_header()->CopyFrom(request.header());
    response.mutable_header()->set_protocol_id(proto::SC_PONG);
    response.mutable_header()->set_server_time_ms(abe_time_real_ms());
    response.set_server_send_time_ms(abe_time_real_ms());

    rc = send_proto_message(
        this,
        message.recv_time_ms,
        proto::SC_PONG,
        response);
    return rc;
}

int GatewaySession::handle_backend_message(
    const GatewayMessage& message)
{
    ABE_LOG_DEBUG(
        "gateway backend route is not connected msg_id=%u conn_id=%llu body_size=%u",
        message.message_id,
        (unsigned long long)conn_id(),
        message.size);
    return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
}

int GatewaySession::on_connect(
    const service_session::SessionOpenRequest& request)
{
    abe::adapter::net::TcpLink* link;

    link = (abe::adapter::net::TcpLink*)request.user_data;
    if (link == NULL ||
        link != &tcp_link_ ||
        request.conn_id != (uint64_t)(uintptr_t)&tcp_link_) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    return proto::ERROR_CODE_OK;
}

void GatewaySession::on_close(uint32_t reason, uint64_t now_ms)
{
    (void)reason;
    (void)now_ms;
    tcp_link_.disconnect();
}

void GatewaySession::on_reset()
{
}

int GatewaySession::send_packet(const void* data, uint32_t size)
{
    if (!active() || !tcp_link_.valid()) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    return tcp_link_.send(data, size);
}

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */
