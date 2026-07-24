#include "abe_gateway_service.h"
#include "abe_gateway_protocol.h"

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
namespace session = abe::logic::session;
namespace net = abe::adapter::net;

struct FakePacket {
    uint32_t msg_id;
    const void* payload;
    uint32_t payload_size;
};

struct MessageCounter {
    uint32_t count;
    uint32_t msg_id;
    uint32_t payload_size;
    uint64_t link_id;
};

static int decode_fake_packet(
    const void* packet,
    uint32_t packet_size,
    uint32_t* out_msg_id,
    const void** out_payload,
    uint32_t* out_payload_size,
    void* user_data)
{
    const FakePacket* fake;

    (void)user_data;
    if (packet == NULL || packet_size != sizeof(FakePacket) ||
        out_msg_id == NULL || out_payload == NULL || out_payload_size == NULL) {
        return session::SESSION_STATUS_INVALID_ARG;
    }

    fake = (const FakePacket*)packet;
    *out_msg_id = fake->msg_id;
    *out_payload = fake->payload;
    *out_payload_size = fake->payload_size;
    return session::SESSION_STATUS_OK;
}

static int on_message(
    session::Session* current,
    const session::SessionMessage* message,
    void* user_data)
{
    MessageCounter* counter;

    counter = (MessageCounter*)user_data;
    TEST_REQUIRE(current != NULL);
    TEST_REQUIRE(message != NULL);
    TEST_REQUIRE(counter != NULL);

    ++counter->count;
    counter->msg_id = message->message_id;
    counter->payload_size = message->size;
    counter->link_id = current->link_id();
    return session::SESSION_STATUS_OK;
}

static int test_gateway_dispatch(void)
{
    session::Session slots[1];
    MessageCounter counter;
    gateway::GatewayHandlerEntry handlers[1];
    gateway::GatewayServiceConfig config;
    gateway::GatewayService service;
    net::TcpServerCallbacks callbacks;
    net::TcpLink link;
    const char payload[] = "abc";
    FakePacket packet;
    uint64_t expected_link_id;

    memset(&counter, 0, sizeof(counter));
    memset(handlers, 0, sizeof(handlers));
    memset(&config, 0, sizeof(config));
    handlers[0].msg_id = 12004u;
    handlers[0].handler = on_message;
    handlers[0].user_data = &counter;

    config.session_config.server_id = 7u;
    config.session_config.sessions = slots;
    config.session_config.session_count = 1u;
    config.session_config.idle_timeout_ms = 0u;
    config.decode_packet = decode_fake_packet;
    config.decode_user_data = NULL;
    config.handlers = handlers;
    config.handler_count = 1u;

    TEST_REQUIRE(service.init(config) == session::SESSION_STATUS_OK);
    TEST_REQUIRE(service.initialized() == 1);
    TEST_REQUIRE(service.session_server()->active_count() == 0u);

    memset(&callbacks, 0, sizeof(callbacks));
    service.fill_tcp_callbacks(&callbacks);
    TEST_REQUIRE(callbacks.on_connect != NULL);
    TEST_REQUIRE(callbacks.on_receive != NULL);
    TEST_REQUIRE(callbacks.on_disconnect != NULL);
    TEST_REQUIRE(callbacks.user_data == &service);

    expected_link_id = (uint64_t)(uintptr_t)&link;
    callbacks.on_connect(NULL, &link, callbacks.user_data);
    TEST_REQUIRE(service.session_server()->active_count() == 1u);
    TEST_REQUIRE(service.session_server()->find_session(expected_link_id) != NULL);

    packet.msg_id = 12004u;
    packet.payload = payload;
    packet.payload_size = 3u;
    callbacks.on_receive(&link, &packet, sizeof(packet), callbacks.user_data);
    TEST_REQUIRE(counter.count == 1u);
    TEST_REQUIRE(counter.msg_id == 12004u);
    TEST_REQUIRE(counter.payload_size == 3u);
    TEST_REQUIRE(counter.link_id == expected_link_id);

    callbacks.on_disconnect(&link, 0, callbacks.user_data);
    TEST_REQUIRE(service.session_server()->active_count() == 0u);

    service.close(2000u);
    TEST_REQUIRE(service.initialized() == 0);
    return 0;
}

static int test_client_msg_packet_codec(void)
{
    abe_msg_header_t header;
    unsigned char packet[128];
    const unsigned char body[4] = { 10u, 20u, 30u, 40u };
    uint32_t packet_size;
    uint32_t msg_id;
    const void* payload;
    uint32_t payload_size;

    abe_msg_header_init(&header);
    header.msg_id = 12004u;
    header.session_id = 1001u;
    header.player_id = 9001u;
    header.seq = 7u;

    packet_size = 0u;
    TEST_REQUIRE(gateway::encode_client_msg_packet(
        &header,
        body,
        sizeof(body),
        packet,
        sizeof(packet),
        &packet_size) == session::SESSION_STATUS_OK);

    msg_id = 0u;
    payload = NULL;
    payload_size = 0u;
    TEST_REQUIRE(gateway::decode_client_msg_packet(
        packet,
        packet_size,
        &msg_id,
        &payload,
        &payload_size,
        NULL) == session::SESSION_STATUS_OK);
    TEST_REQUIRE(msg_id == 12004u);
    TEST_REQUIRE(payload_size == sizeof(body));
    TEST_REQUIRE(memcmp(payload, body, sizeof(body)) == 0);
    return 0;
}

int main()
{
    if (test_client_msg_packet_codec() != 0) {
        return 1;
    }
    if (test_gateway_dispatch() != 0) {
        return 1;
    }
    return 0;
}
