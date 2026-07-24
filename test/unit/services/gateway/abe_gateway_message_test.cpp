#include "abe_gateway_message.h"
#include "protocol.pb.h"

#include <google/protobuf/arena.h>
#include <google/protobuf/message.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

namespace gateway = abe::service::gateway;
namespace client = abe::proto::client;

static int test_message_mapping(void)
{
    google::protobuf::Arena arena;
    google::protobuf::Message* message;
    uint32_t msg_id;
    const char* message_name;

    msg_id = 0u;
    TEST_REQUIRE(gateway::get_client_msg_id_from_name("CS_GAME_ACTION_REQ", &msg_id) == 0);
    TEST_REQUIRE(msg_id == 12004u);

    message_name = gateway::get_client_message_name(12004u);
    TEST_REQUIRE(message_name != NULL);
    TEST_REQUIRE(strcmp(message_name, "abe.proto.client.PB_CS_GAME_ACTION_REQ") == 0);

    message = gateway::create_client_message(12004u, &arena);
    TEST_REQUIRE(message != NULL);
    TEST_REQUIRE(strcmp(message->GetDescriptor()->name().c_str(), "PB_CS_GAME_ACTION_REQ") == 0);

    msg_id = 0u;
    TEST_REQUIRE(gateway::get_client_msg_id_from_message(*message, &msg_id) == 0);
    TEST_REQUIRE(msg_id == 12004u);
    return 0;
}

static int test_parse_message_body(void)
{
    google::protobuf::Arena arena;
    client::PB_CS_GAME_ACTION_REQ request;
    google::protobuf::Message* parsed;
    uint32_t msg_id;
    char body[256];
    int body_size;

    request.mutable_header()->set_protocol_id(client::CS_GAME_ACTION_REQ);
    request.mutable_room()->set_room_id(1001u);
    request.set_seq(7u);
    request.set_client_time_ms(123456u);
    request.set_payload("abc", 3u);

    body_size = (int)request.ByteSizeLong();
    TEST_REQUIRE(body_size > 0);
    TEST_REQUIRE(body_size <= (int)sizeof(body));
    TEST_REQUIRE(request.SerializeToArray(body, body_size));

    msg_id = 0u;
    TEST_REQUIRE(gateway::get_client_msg_id_from_message(request, &msg_id) == 0);
    TEST_REQUIRE(msg_id == 12004u);

    parsed = NULL;
    TEST_REQUIRE(gateway::parse_client_message_body(
        msg_id,
        body,
        (uint32_t)body_size,
        &arena,
        &parsed) == 0);
    TEST_REQUIRE(parsed != NULL);
    TEST_REQUIRE(strcmp(parsed->GetDescriptor()->name().c_str(), "PB_CS_GAME_ACTION_REQ") == 0);
    return 0;
}

static int test_message_packet_codec(void)
{
    google::protobuf::Arena arena;
    client::PB_CS_GAME_ACTION_REQ request;
    google::protobuf::Message* parsed;
    abe_msg_header_t header;
    abe_msg_header_t decoded_header;
    unsigned char packet[512];
    uint32_t packet_size;

    request.mutable_header()->set_protocol_id(client::CS_GAME_ACTION_REQ);
    request.mutable_room()->set_room_id(2001u);
    request.set_seq(9u);
    request.set_client_time_ms(222u);
    request.set_payload("move", 4u);

    abe_msg_header_init(&header);
    header.session_id = 111u;
    header.role_id = 222u;
    header.player_id = 333u;
    header.seq = 9u;
    header.trace_id = 444u;

    packet_size = 0u;
    TEST_REQUIRE(gateway::encode_client_message_packet(
        &header,
        request,
        packet,
        sizeof(packet),
        &packet_size) == 0);
    TEST_REQUIRE(packet_size > ABE_MSG_HEADER_SIZE);

    parsed = NULL;
    memset(&decoded_header, 0, sizeof(decoded_header));
    TEST_REQUIRE(gateway::decode_client_message_packet(
        packet,
        packet_size,
        &arena,
        &decoded_header,
        &parsed) == 0);
    TEST_REQUIRE(parsed != NULL);
    TEST_REQUIRE(decoded_header.msg_id == 12004u);
    TEST_REQUIRE(decoded_header.session_id == 111u);
    TEST_REQUIRE(decoded_header.role_id == 222u);
    TEST_REQUIRE(decoded_header.player_id == 333u);
    TEST_REQUIRE(strcmp(parsed->GetDescriptor()->name().c_str(), "PB_CS_GAME_ACTION_REQ") == 0);
    return 0;
}

int main()
{
    if (test_message_mapping() != 0) {
        return 1;
    }
    if (test_parse_message_body() != 0) {
        return 1;
    }
    if (test_message_packet_codec() != 0) {
        return 1;
    }
    return 0;
}
