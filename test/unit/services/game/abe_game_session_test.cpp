#include "abe_game_session.h"
#include "abe_protocol.h"
#include "abe_session_manager.h"
#include "protocol.pb.h"

#include "../../abe_test.h"

#include <stdint.h>
#include <string.h>

namespace game = abe::service::game;
namespace proto = abe::proto::client;
namespace session = abe::service::session;

struct SendCapture {
    unsigned char data[ABE_MSG_HEADER_SIZE + 512u];
    uint32_t size;
    uint32_t count;
};

static int capture_send(
    game::GameSession* session,
    const void* data,
    uint32_t size,
    void* user_data)
{
    SendCapture* capture;

    (void)session;
    capture = (SendCapture*)user_data;
    TEST_REQUIRE(capture != NULL);
    TEST_REQUIRE(data != NULL);
    TEST_REQUIRE(size <= (uint32_t)sizeof(capture->data));

    memcpy(capture->data, data, size);
    capture->size = size;
    ++capture->count;
    return proto::ERROR_CODE_OK;
}

static int test_game_session_sends_proto_message(void)
{
    session::SessionManager server;
    session::SessionOpenRequest request;
    session::Session* service_session;
    game::GameSession* game_session;
    proto::PB_SC_ENTER_GAME_RESP response;
    proto::PB_SC_ENTER_GAME_RESP sent_response;
    abe_msg_packet_view_t sent_packet;
    SendCapture capture;
    int status;

    memset(&capture, 0, sizeof(capture));
    TEST_REQUIRE(server.init<game::GameSession>(9u, 1u, 0u) ==
        proto::ERROR_CODE_OK);

    memset(&request, 0, sizeof(request));
    request.conn_id = 77u;
    request.now_ms = 100u;

    status = proto::ERROR_CODE_OK;
    service_session = server.open_session(request, &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(service_session != NULL);

    game_session = (game::GameSession*)service_session;
    TEST_REQUIRE(game_session->mark_authenticated(5001u) == proto::ERROR_CODE_OK);
    game_session->set_room_id(3001u);
    game_session->set_send_callback(capture_send, &capture);

    response.mutable_header()->set_seq(101u);
    TEST_REQUIRE(game_session->send_message(response, 200u) == proto::ERROR_CODE_OK);

    TEST_REQUIRE(capture.count == 1u);
    TEST_REQUIRE(abe_msg_packet_decode(capture.data, capture.size, &sent_packet) ==
        ABE_PROTOCOL_OK);
    TEST_REQUIRE(sent_packet.header.msg_id == proto::SC_ENTER_GAME_RESP);
    TEST_REQUIRE(sent_packet.header.session_id == 77u);
    TEST_REQUIRE(sent_packet.header.role_id == 3001u);
    TEST_REQUIRE(sent_packet.header.player_id == 5001u);
    TEST_REQUIRE(sent_packet.header.seq == 101u);
    TEST_REQUIRE(sent_packet.header.timestamp == 200u);
    TEST_REQUIRE(sent_response.ParseFromArray(
        sent_packet.body,
        (int)sent_packet.body_size));
    TEST_REQUIRE(sent_response.header().protocol_id() == proto::SC_ENTER_GAME_RESP);
    TEST_REQUIRE(sent_response.header().seq() == 101u);

    server.close(300u);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_game_session_sends_proto_message() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
