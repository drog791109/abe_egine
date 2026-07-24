#include "abe_gateway_session.h"
#include "abe_session_server.h"

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
namespace net = abe::adapter::net;
namespace session = abe::logic::session;

struct MessageCounter {
    uint32_t count;
    uint32_t msg_id;
    uint32_t size;
    uint64_t link_id;
};

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
    counter->size = message->size;
    counter->link_id = current->link_id();
    return session::SESSION_STATUS_OK;
}

static int test_gateway_session_routes_message(void)
{
    session::Session slots[1];
    session::SessionServer server;
    session::SessionServerConfig config;
    session::SessionOpenRequest request;
    session::Session* logic_session;
    gateway::GatewaySession gateway_session;
    net::TcpLink link;
    MessageCounter counter;
    const char body[] = "abc";
    int status;

    memset(&counter, 0, sizeof(counter));
    memset(&config, 0, sizeof(config));
    config.server_id = 1u;
    config.sessions = slots;
    config.session_count = 1u;
    config.idle_timeout_ms = 0u;
    TEST_REQUIRE(server.init(config) == session::SESSION_STATUS_OK);

    memset(&request, 0, sizeof(request));
    request.link_id = (uint64_t)(uintptr_t)&link;
    request.conn_id = request.link_id;
    request.now_ms = 100u;
    request.link_user_data = &link;

    status = session::SESSION_STATUS_OK;
    logic_session = server.open_session(request, &status);
    TEST_REQUIRE(status == session::SESSION_STATUS_OK);
    TEST_REQUIRE(logic_session != NULL);
    TEST_REQUIRE(logic_session->set_message_handler(12004u, on_message, &counter) ==
        session::SESSION_STATUS_OK);

    TEST_REQUIRE(gateway_session.open(&link, logic_session) == session::SESSION_STATUS_OK);
    TEST_REQUIRE(gateway_session.active() == 1);
    TEST_REQUIRE(gateway_session.link_id() == request.link_id);
    TEST_REQUIRE(gateway_session.logic_session() == logic_session);

    TEST_REQUIRE(gateway_session.handle_message(12004u, body, 3u, 200u) ==
        session::SESSION_STATUS_OK);
    TEST_REQUIRE(counter.count == 1u);
    TEST_REQUIRE(counter.msg_id == 12004u);
    TEST_REQUIRE(counter.size == 3u);
    TEST_REQUIRE(counter.link_id == request.link_id);

    TEST_REQUIRE(server.close_session(request.link_id, 0u, 300u) ==
        session::SESSION_STATUS_OK);
    TEST_REQUIRE(gateway_session.active() == 0);
    gateway_session.close();
    TEST_REQUIRE(gateway_session.link_id() == 0u);
    return 0;
}

int main()
{
    if (test_gateway_session_routes_message() != 0) {
        return 1;
    }
    return 0;
}
