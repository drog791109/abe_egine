#include "abe_gateway_session.h"
#include "abe_protocol.h"
#include "abe_session_manager.h"
#include "protocol.pb.h"

#include "../../abe_test.h"

#include <string>
#include <stdint.h>
#include <string.h>

namespace gateway = abe::service::gateway;
namespace net = abe::adapter::net;
namespace proto = abe::proto::client;
namespace session = abe::service::session;

struct SendCapture {
    unsigned char data[ABE_MSG_HEADER_SIZE + 512u];
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

int main()
{
    if (test_gateway_session_handles_ping() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
