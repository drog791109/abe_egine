#include "abe_protocol.h"

#include <stdio.h>
#include <string.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

static int test_packet_encode_decode(void)
{
    abe_msg_header_t header;
    abe_msg_packet_view_t packet;
    unsigned char buffer[128];
    const unsigned char body[3] = { 1u, 2u, 3u };
    uint32_t written_size;

    abe_msg_header_init(&header);
    header.msg_id = 12004u;
    header.session_id = 1001u;
    header.role_id = 2002u;
    header.player_id = 3003u;
    header.seq = 11u;
    header.rpc_id = 22u;
    header.trace_id = 33u;
    header.source_server = 44u;
    header.target_server = 55u;
    header.route_type = 66u;
    header.flags = 77u;
    header.timestamp = 88u;

    memset(buffer, 0, sizeof(buffer));
    written_size = 0u;
    TEST_REQUIRE(abe_msg_packet_encode(
        &header,
        body,
        sizeof(body),
        buffer,
        sizeof(buffer),
        &written_size) == ABE_PROTOCOL_OK);
    TEST_REQUIRE(written_size == ABE_MSG_HEADER_SIZE + sizeof(body));

    TEST_REQUIRE(abe_msg_packet_decode(buffer, written_size, &packet) == ABE_PROTOCOL_OK);
    TEST_REQUIRE(packet.header.magic == ABE_MSG_MAGIC);
    TEST_REQUIRE(packet.header.version == ABE_MSG_VERSION);
    TEST_REQUIRE(packet.header.packet_length == written_size);
    TEST_REQUIRE(packet.header.msg_id == 12004u);
    TEST_REQUIRE(packet.header.session_id == 1001u);
    TEST_REQUIRE(packet.header.role_id == 2002u);
    TEST_REQUIRE(packet.header.player_id == 3003u);
    TEST_REQUIRE(packet.header.seq == 11u);
    TEST_REQUIRE(packet.header.rpc_id == 22u);
    TEST_REQUIRE(packet.header.trace_id == 33u);
    TEST_REQUIRE(packet.header.source_server == 44u);
    TEST_REQUIRE(packet.header.target_server == 55u);
    TEST_REQUIRE(packet.header.route_type == 66u);
    TEST_REQUIRE(packet.header.flags == 77u);
    TEST_REQUIRE(packet.header.timestamp == 88u);
    TEST_REQUIRE(packet.header.body_length == sizeof(body));
    TEST_REQUIRE(packet.body_size == sizeof(body));
    TEST_REQUIRE(memcmp(packet.body, body, sizeof(body)) == 0);
    return 0;
}

static int test_invalid_packet(void)
{
    abe_msg_header_t header;
    abe_msg_packet_view_t packet;
    unsigned char buffer[ABE_MSG_HEADER_SIZE];
    uint32_t written_size;

    abe_msg_header_init(&header);
    header.msg_id = 1u;
    written_size = 0u;
    TEST_REQUIRE(abe_msg_packet_encode(
        &header,
        NULL,
        0u,
        buffer,
        sizeof(buffer),
        &written_size) == ABE_PROTOCOL_OK);
    TEST_REQUIRE(written_size == ABE_MSG_HEADER_SIZE);

    buffer[0] = 0u;
    TEST_REQUIRE(abe_msg_packet_decode(buffer, written_size, &packet) ==
        ABE_PROTOCOL_INVALID_MAGIC);
    return 0;
}

int main(void)
{
    if (test_packet_encode_decode() != 0) {
        return 1;
    }
    if (test_invalid_packet() != 0) {
        return 1;
    }
    return 0;
}
