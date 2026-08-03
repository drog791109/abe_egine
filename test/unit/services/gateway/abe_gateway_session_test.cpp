#include "abe_gateway_session.h"
#include "abe_gateway_backend.h"
#include "abe_protocol.h"
#include "abe_session_manager.h"
#include "abe_snowflake.h"
#include "protocol.pb.h"

#include "../../abe_test.h"

#include <string>
#include <stdint.h>
#include <string.h>

namespace gateway = abe::service::gateway;
namespace net = abe::adapter::net;
namespace proto = abe::proto::client;
namespace session = abe::service::session;
namespace svc = abe::service::common;

struct SendCapture {
    unsigned char data[ABE_MSG_HEADER_SIZE + 4096u];
    uint32_t size;
    uint32_t count;
};

class TestGatewaySession : public gateway::GatewaySession {
public:
    TestGatewaySession()
        : capture(NULL)
    {
    }

    void bind_capture(SendCapture* value)
    {
        capture = value;
    }

protected:
    virtual int send_packet(const void* data, uint32_t size)
    {
        TEST_REQUIRE(capture != NULL);
        TEST_REQUIRE(data != NULL);
        TEST_REQUIRE(size <= (uint32_t)sizeof(capture->data));

        memcpy(capture->data, data, size);
        capture->size = size;
        ++capture->count;
        return proto::ERROR_CODE_OK;
    }

private:
    SendCapture* capture;
};

static void reset_capture(SendCapture* capture)
{
    memset(capture, 0, sizeof(*capture));
}

template <typename Request>
static int send_gateway_request(
    TestGatewaySession* gateway_session,
    uint32_t message_id,
    const Request& request,
    uint64_t now_ms,
    int expected_status)
{
    std::string body;
    abe_msg_header_t header;
    unsigned char packet[ABE_MSG_HEADER_SIZE + 4096u];
    uint32_t written_size;

    TEST_REQUIRE(gateway_session != NULL);
    TEST_REQUIRE(request.SerializeToString(&body));

    abe_msg_header_init(&header);
    header.msg_id = message_id;
    written_size = 0u;
    TEST_REQUIRE(abe_msg_packet_encode(
        &header,
        body.empty() ? NULL : body.data(),
        (uint32_t)body.size(),
        packet,
        (uint32_t)sizeof(packet),
        &written_size) == ABE_PROTOCOL_OK);

    TEST_REQUIRE(gateway_session->handle_packet(packet, written_size, now_ms) ==
        expected_status);
    return ABE_TEST_STATUS_OK;
}

template <typename Response>
static int parse_captured_response(
    const SendCapture* capture,
    uint32_t message_id,
    Response* out_response)
{
    abe_msg_packet_view_t sent_packet;

    TEST_REQUIRE(capture != NULL);
    TEST_REQUIRE(out_response != NULL);
    TEST_REQUIRE(capture->count == 1u);
    TEST_REQUIRE(abe_msg_packet_decode(capture->data, capture->size, &sent_packet) ==
        ABE_PROTOCOL_OK);
    TEST_REQUIRE(sent_packet.header.msg_id == message_id);
    TEST_REQUIRE(out_response->ParseFromArray(
        sent_packet.body,
        (int)sent_packet.body_size));
    return ABE_TEST_STATUS_OK;
}

static int test_gateway_session_handles_ping(void)
{
    session::SessionManager server;
    session::SessionOpenRequest request;
    session::Session* service_session;
    session::Session* free_session;
    TestGatewaySession* gateway_session;
    net::TcpLink* link;
    proto::PB_CS_PING ping;
    proto::PB_SC_PONG response;
    proto::PB_CS_LOGIN_REQ login_request;
    std::string body;
    SendCapture capture;
    abe_msg_header_t header;
    unsigned char packet[ABE_MSG_HEADER_SIZE + 512u];
    abe_msg_packet_view_t sent_packet;
    uint32_t written_size;
    int status;

    memset(&capture, 0, sizeof(capture));
    TEST_REQUIRE(server.init<TestGatewaySession>(1u, 1u, 0u) ==
        proto::ERROR_CODE_OK);

    free_session = server.peek_free_session();
    TEST_REQUIRE(free_session != NULL);
    gateway_session = (TestGatewaySession*)free_session;
    link = gateway_session->tcp_link_slot();
    TEST_REQUIRE(link != NULL);

    memset(&request, 0, sizeof(request));
    request.conn_id = (uint64_t)(uintptr_t)link;
    request.now_ms = 100u;
    request.user_data = link;

    status = proto::ERROR_CODE_OK;
    service_session = server.open_session(request, &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(service_session != NULL);
    gateway_session = (TestGatewaySession*)service_session;

    TEST_REQUIRE(gateway_session->active());
    TEST_REQUIRE(gateway_session->link() == link);
    TEST_REQUIRE(gateway_session->conn_id() == request.conn_id);
    TEST_REQUIRE((session::Session*)gateway_session == service_session);
    gateway_session->bind_capture(&capture);

    ping.mutable_header()->set_protocol_id(proto::CS_PING);
    ping.set_client_send_time_ms(1234u);
    TEST_REQUIRE(ping.SerializeToString(&body));

    abe_msg_header_init(&header);
    header.msg_id = proto::CS_PING;
    written_size = 0u;
    TEST_REQUIRE(abe_msg_packet_encode(
        &header,
        body.data(),
        (uint32_t)body.size(),
        packet,
        (uint32_t)sizeof(packet),
        &written_size) == ABE_PROTOCOL_OK);

    TEST_REQUIRE(gateway_session->handle_packet(packet, written_size, 200u) ==
        proto::ERROR_CODE_OK);

    TEST_REQUIRE(capture.count == 1u);
    TEST_REQUIRE(abe_msg_packet_decode(capture.data, capture.size, &sent_packet) ==
        ABE_PROTOCOL_OK);
    TEST_REQUIRE(sent_packet.header.msg_id == proto::SC_PONG);
    TEST_REQUIRE(response.ParseFromArray(sent_packet.body, (int)sent_packet.body_size));
    TEST_REQUIRE(response.header().protocol_id() == proto::SC_PONG);
    TEST_REQUIRE(response.server_send_time_ms() != 0u);

    memset(&capture, 0, sizeof(capture));
    body.clear();
    login_request.mutable_header()->set_protocol_id(proto::CS_LOGIN_REQ);
    login_request.mutable_header()->set_seq(101u);
    login_request.set_account("alice01");
    login_request.set_nickname("Alice");
    TEST_REQUIRE(login_request.SerializeToString(&body));

    abe_msg_header_init(&header);
    header.msg_id = proto::CS_LOGIN_REQ;
    written_size = 0u;
    TEST_REQUIRE(abe_msg_packet_encode(
        &header,
        body.data(),
        (uint32_t)body.size(),
        packet,
        (uint32_t)sizeof(packet),
        &written_size) == ABE_PROTOCOL_OK);

    TEST_REQUIRE(gateway_session->handle_packet(packet, written_size, 210u) ==
        proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE);
    TEST_REQUIRE(capture.count == 0u);

    TEST_REQUIRE(server.close_session(request.conn_id, 0u, 300u) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(!gateway_session->active());
    TEST_REQUIRE(gateway_session->link() == NULL);
    TEST_REQUIRE(gateway_session->conn_id() == 0u);
    server.close(301u);
    return ABE_TEST_STATUS_OK;
}

static int test_gateway_session_routes_login_lobby_game_flow(void)
{
    session::SessionManager server;
    session::SessionOpenRequest request;
    session::Session* service_session;
    session::Session* free_session;
    TestGatewaySession* gateway_session;
    gateway::LocalGatewayBackend backend;
    abe_snowflake_t* id_generator;
    svc::Context context;
    net::TcpLink* link;
    proto::PB_CS_LOGIN_REQ login_request;
    proto::PB_SC_LOGIN_RESP login_response;
    proto::PB_CS_ENTER_LOBBY_REQ enter_lobby;
    proto::PB_SC_ENTER_LOBBY_RESP lobby_response;
    proto::PB_CS_ENTER_GAME_REQ enter_game;
    proto::PB_SC_ENTER_GAME_RESP enter_game_response;
    proto::PB_CS_LEAVE_GAME_REQ leave_game;
    proto::PB_SC_LEAVE_GAME_RESP leave_game_response;
    proto::PB_CS_PING ping;
    proto::PB_SC_PONG pong;
    SendCapture capture;
    uint64_t uid;
    std::string session_token;
    int status;

    id_generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(21u, &id_generator) == ABE_OK);
    memset(&context, 0, sizeof(context));
    context.id_generator = id_generator;
    TEST_REQUIRE(backend.init(context) == svc::SERVICE_STATUS_OK);

    reset_capture(&capture);
    TEST_REQUIRE(server.init<TestGatewaySession>(1u, 1u, 0u) ==
        proto::ERROR_CODE_OK);

    free_session = server.peek_free_session();
    TEST_REQUIRE(free_session != NULL);
    gateway_session = (TestGatewaySession*)free_session;
    link = gateway_session->tcp_link_slot();
    TEST_REQUIRE(link != NULL);

    memset(&request, 0, sizeof(request));
    request.conn_id = (uint64_t)(uintptr_t)link;
    request.now_ms = 1000u;
    request.user_data = link;

    status = proto::ERROR_CODE_OK;
    service_session = server.open_session(request, &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(service_session != NULL);

    gateway_session = (TestGatewaySession*)service_session;
    gateway_session->bind_capture(&capture);
    gateway_session->set_backend(&backend);

    login_request.mutable_header()->set_protocol_id(proto::CS_LOGIN_REQ);
    login_request.mutable_header()->set_seq(1001u);
    login_request.set_account("alice01");
    login_request.set_token("test-auth-token");
    login_request.set_device_id("unit-test-device");
    login_request.set_client_version("1.0.0");
    login_request.set_nickname("Alice");
    TEST_REQUIRE(send_gateway_request(
            gateway_session,
            proto::CS_LOGIN_REQ,
            login_request,
            1100u,
            proto::ERROR_CODE_OK) == ABE_TEST_STATUS_OK);
    TEST_REQUIRE(parse_captured_response(
            &capture,
            proto::SC_LOGIN_RESP,
            &login_response) == ABE_TEST_STATUS_OK);
    TEST_REQUIRE(login_response.header().protocol_id() == proto::SC_LOGIN_RESP);
    TEST_REQUIRE(login_response.header().seq() == 1001u);
    TEST_REQUIRE(login_response.result().error_code() == proto::ERROR_CODE_OK);
    TEST_REQUIRE(login_response.player().uid() != 0u);
    TEST_REQUIRE(!login_response.session_token().empty());
    uid = login_response.player().uid();
    session_token = login_response.session_token();
    TEST_REQUIRE(gateway_session->authenticated());
    TEST_REQUIRE(gateway_session->user_id() == uid);

    reset_capture(&capture);
    enter_lobby.mutable_header()->set_protocol_id(proto::CS_ENTER_LOBBY_REQ);
    enter_lobby.mutable_header()->set_seq(1002u);
    TEST_REQUIRE(send_gateway_request(
            gateway_session,
            proto::CS_ENTER_LOBBY_REQ,
            enter_lobby,
            1200u,
            proto::ERROR_CODE_OK) == ABE_TEST_STATUS_OK);
    TEST_REQUIRE(parse_captured_response(
            &capture,
            proto::SC_ENTER_LOBBY_RESP,
            &lobby_response) == ABE_TEST_STATUS_OK);
    TEST_REQUIRE(lobby_response.header().protocol_id() == proto::SC_ENTER_LOBBY_RESP);
    TEST_REQUIRE(lobby_response.header().seq() == 1002u);
    TEST_REQUIRE(lobby_response.result().error_code() == proto::ERROR_CODE_OK);
    TEST_REQUIRE(lobby_response.player().uid() == uid);
    TEST_REQUIRE(lobby_response.lobby_time_ms() == 1200u);

    reset_capture(&capture);
    enter_game.mutable_header()->set_protocol_id(proto::CS_ENTER_GAME_REQ);
    enter_game.mutable_header()->set_seq(1003u);
    enter_game.mutable_room()->set_room_id(7001u);
    enter_game.mutable_room()->set_room_version(1u);
    enter_game.set_session_token(session_token);
    enter_game.set_client_ready_version(1u);
    TEST_REQUIRE(send_gateway_request(
            gateway_session,
            proto::CS_ENTER_GAME_REQ,
            enter_game,
            1300u,
            proto::ERROR_CODE_OK) == ABE_TEST_STATUS_OK);
    TEST_REQUIRE(parse_captured_response(
            &capture,
            proto::SC_ENTER_GAME_RESP,
            &enter_game_response) == ABE_TEST_STATUS_OK);
    TEST_REQUIRE(enter_game_response.header().protocol_id() ==
        proto::SC_ENTER_GAME_RESP);
    TEST_REQUIRE(enter_game_response.header().seq() == 1003u);
    TEST_REQUIRE(enter_game_response.result().error_code() == proto::ERROR_CODE_OK);
    TEST_REQUIRE(enter_game_response.room().room_id() == 7001u);
    TEST_REQUIRE(enter_game_response.game_start_time_ms() == 1300u);
    TEST_REQUIRE(enter_game_response.tick_rate() == 30u);

    reset_capture(&capture);
    leave_game.mutable_header()->set_protocol_id(proto::CS_LEAVE_GAME_REQ);
    leave_game.mutable_header()->set_seq(1004u);
    leave_game.mutable_room()->set_room_id(7001u);
    leave_game.mutable_room()->set_room_version(1u);
    leave_game.set_reason(9u);
    TEST_REQUIRE(send_gateway_request(
            gateway_session,
            proto::CS_LEAVE_GAME_REQ,
            leave_game,
            1400u,
            proto::ERROR_CODE_OK) == ABE_TEST_STATUS_OK);
    TEST_REQUIRE(parse_captured_response(
            &capture,
            proto::SC_LEAVE_GAME_RESP,
            &leave_game_response) == ABE_TEST_STATUS_OK);
    TEST_REQUIRE(leave_game_response.header().protocol_id() ==
        proto::SC_LEAVE_GAME_RESP);
    TEST_REQUIRE(leave_game_response.header().seq() == 1004u);
    TEST_REQUIRE(leave_game_response.result().error_code() == proto::ERROR_CODE_OK);
    TEST_REQUIRE(leave_game_response.room().room_id() == 7001u);
    TEST_REQUIRE(leave_game_response.reason() == 9u);

    reset_capture(&capture);
    ping.mutable_header()->set_protocol_id(proto::CS_PING);
    ping.mutable_header()->set_seq(1005u);
    ping.set_client_send_time_ms(1500u);
    TEST_REQUIRE(send_gateway_request(
            gateway_session,
            proto::CS_PING,
            ping,
            1500u,
            proto::ERROR_CODE_OK) == ABE_TEST_STATUS_OK);
    TEST_REQUIRE(parse_captured_response(
            &capture,
            proto::SC_PONG,
            &pong) == ABE_TEST_STATUS_OK);
    TEST_REQUIRE(pong.header().protocol_id() == proto::SC_PONG);
    TEST_REQUIRE(pong.header().seq() == 1005u);
    TEST_REQUIRE(pong.server_send_time_ms() != 0u);

    server.close(1600u);
    backend.close(1601u);
    abe_snowflake_destroy(id_generator);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_gateway_session_handles_ping() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_gateway_session_routes_login_lobby_game_flow() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
