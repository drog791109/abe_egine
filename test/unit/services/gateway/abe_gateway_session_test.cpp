#include "abe_gateway_session.h"
#include "abe_session_server.h"
#include "protocol.pb.h"

#include "../../abe_test.h"

#include <stdint.h>
#include <string.h>

namespace gateway = abe::service::gateway;
namespace net = abe::adapter::net;
namespace proto = abe::proto::client;
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
    return proto::ERROR_CODE_OK;
}

static int test_gateway_session_routes_message(void)
{
    gateway::GatewaySession slots[1];
    session::SessionServer server;
    session::SessionServerConfig config;
    session::SessionOpenRequest request;
    session::Session* logic_session;
    net::TcpLink link;
    MessageCounter counter;
    const char body[] = "abc";
    int status;

    memset(&counter, 0, sizeof(counter));
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
    logic_session = server.open_session(request, &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(logic_session != NULL);
    TEST_REQUIRE(logic_session->set_message_handler(12004u, on_message, &counter) ==
        proto::ERROR_CODE_OK);

    TEST_REQUIRE(logic_session == &slots[0]);
    TEST_REQUIRE(slots[0].active() == 1);
    TEST_REQUIRE(slots[0].link() == &link);
    TEST_REQUIRE(slots[0].link_id() == request.link_id);
    TEST_REQUIRE((session::Session*)&slots[0] == logic_session);

    TEST_REQUIRE(slots[0].handle_message(12004u, body, 3u, 200u) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(counter.count == 1u);
    TEST_REQUIRE(counter.msg_id == 12004u);
    TEST_REQUIRE(counter.size == 3u);
    TEST_REQUIRE(counter.link_id == request.link_id);

    TEST_REQUIRE(server.close_session(request.link_id, 0u, 300u) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(slots[0].active() == 0);
    TEST_REQUIRE(slots[0].link() == NULL);
    TEST_REQUIRE(slots[0].link_id() == 0u);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_gateway_session_routes_message() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
