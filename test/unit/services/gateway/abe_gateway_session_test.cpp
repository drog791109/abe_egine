#include "abe_gateway_session.h"
#include "abe_protocol.h"
#include "abe_session_server.h"
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
    virtual int on_send(const void* data, uint32_t size)
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
    TestGatewaySession slots[1];
    session::SessionServer server;
    session::SessionServerConfig config;
    session::SessionOpenRequest request;
    session::Session* service_session;
    net::TcpLink link;
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
    memset(&config, 0, sizeof(config));
    config.server_id = 1u;
    config.sessions = slots;
    config.session_count = 1u;
    config.session_size = (uint32_t)sizeof(slots[0]);
    config.idle_timeout_ms = 0u;
    TEST_REQUIRE(server.init(config) == proto::ERROR_CODE_OK);

    memset(&request, 0, sizeof(request));
    request.link_id = (uint64_t)(uintptr_t)&link;
    request.conn_id = request.link_id;
    request.now_ms = 100u;
    request.link_user_data = &link;

    status = proto::ERROR_CODE_OK;
    service_session = server.open_session(request, &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(service_session != NULL);

    TEST_REQUIRE(service_session == &slots[0]);
    TEST_REQUIRE(slots[0].active() == 1);
    TEST_REQUIRE(slots[0].link() == &link);
    TEST_REQUIRE(slots[0].link_id() == request.link_id);
    TEST_REQUIRE((session::Session*)&slots[0] == service_session);
    slots[0].bind_capture(&capture);

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

    TEST_REQUIRE(slots[0].handle_packet(packet, written_size, 200u) ==
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

    TEST_REQUIRE(slots[0].handle_packet(packet, written_size, 210u) ==
        proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE);
    TEST_REQUIRE(capture.count == 0u);

    TEST_REQUIRE(server.close_session(request.link_id, 0u, 300u) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(slots[0].active() == 0);
    TEST_REQUIRE(slots[0].link() == NULL);
    TEST_REQUIRE(slots[0].link_id() == 0u);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_gateway_session_handles_ping() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
