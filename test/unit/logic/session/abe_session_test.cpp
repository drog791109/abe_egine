#include "abe_session_server.h"

#include "protocol.pb.h"

#include "../../abe_test.h"

#include <string.h>

namespace session = abe::logic::session;
namespace proto = abe::proto::client;

struct MessageCounter {
    uint32_t count;
    uint32_t last_message_id;
    uint32_t last_size;
    uint64_t last_link_id;
};

struct SendCounter {
    uint32_t count;
    uint32_t last_size;
    uint64_t last_link_id;
};

class TestServerSession : public session::Session {
public:
    TestServerSession()
        : open_count(0u),
          close_count(0u),
          reset_count(0u),
          last_link_user_data(NULL)
    {
    }

    uint32_t open_count;
    uint32_t close_count;
    uint32_t reset_count;
    void* last_link_user_data;

protected:
    virtual int on_open(const session::SessionOpenRequest& request)
    {
        ++open_count;
        last_link_user_data = request.link_user_data;
        return proto::ERROR_CODE_OK;
    }

    virtual void on_close(uint32_t reason, uint64_t now_ms)
    {
        (void)reason;
        (void)now_ms;
        ++close_count;
    }

    virtual void on_reset()
    {
        ++reset_count;
        last_link_user_data = NULL;
    }
};

static session::SessionOpenRequest make_open_request(uint64_t link_id, uint64_t conn_id, uint64_t now_ms)
{
    session::SessionOpenRequest request;

    request.link_id = link_id;
    request.conn_id = conn_id;
    request.now_ms = now_ms;
    request.link_user_data = NULL;
    return request;
}

static int count_message(
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
    counter->last_message_id = message->message_id;
    counter->last_size = message->size;
    counter->last_link_id = current->link_id();
    return proto::ERROR_CODE_OK;
}

static int count_send(
    session::Session* current,
    const void* data,
    uint32_t size,
    void* user_data)
{
    SendCounter* counter;

    (void)data;
    counter = (SendCounter*)user_data;
    TEST_REQUIRE(current != NULL);
    TEST_REQUIRE(counter != NULL);

    ++counter->count;
    counter->last_size = size;
    counter->last_link_id = current->link_id();
    return proto::ERROR_CODE_OK;
}

static int test_server_accepts_derived_sessions(void)
{
    TestServerSession slots[2];
    session::SessionServer server;
    session::SessionServerConfig config;
    session::SessionOpenRequest request;
    session::Session* current;
    int link_user_data;
    int status;

    memset(&config, 0, sizeof(config));
    config.server_id = 8u;
    config.sessions = slots;
    config.session_count = 2u;
    config.session_size = (uint32_t)sizeof(slots[0]);
    config.idle_timeout_ms = 0u;

    TEST_REQUIRE(server.init(config) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(slots[0].reset_count == 1u);
    TEST_REQUIRE(slots[1].reset_count == 1u);

    request = make_open_request(901u, 11u, 100u);
    request.link_user_data = &link_user_data;
    current = server.open_session(request, &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current == &slots[0]);
    TEST_REQUIRE(slots[0].open_count == 1u);
    TEST_REQUIRE(slots[0].last_link_user_data == &link_user_data);
    TEST_REQUIRE(server.find_session(901u) == &slots[0]);

    TEST_REQUIRE(server.close_session(901u, 3u, 200u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(slots[0].close_count == 1u);
    TEST_REQUIRE(slots[0].reset_count == 3u);
    TEST_REQUIRE(slots[0].last_link_user_data == NULL);
    return ABE_TEST_STATUS_OK;
}

static int test_server_owns_link_sessions(void)
{
    session::Session slots[2];
    session::SessionServer server;
    session::SessionServerConfig config;
    session::Session* first;
    session::Session* second;
    int status;

    memset(&config, 0, sizeof(config));
    config.server_id = 9u;
    config.sessions = slots;
    config.session_count = 2u;
    config.idle_timeout_ms = 0u;

    TEST_REQUIRE(server.init(config) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(server.capacity() == 2u);
    TEST_REQUIRE(server.active_count() == 0u);

    first = server.open_session(make_open_request(1001u, 1u, 100u), &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(first != NULL);
    TEST_REQUIRE(first->link_id() == 1001u);
    TEST_REQUIRE(first->server_id() == 9u);
    TEST_REQUIRE(first->state() == session::SESSION_STATE_CONNECTED);
    TEST_REQUIRE(server.active_count() == 1u);

    TEST_REQUIRE(server.open_session(make_open_request(1001u, 2u, 101u), &status) == NULL);
    TEST_REQUIRE(status == proto::ERROR_CODE_SESSION_ALREADY_EXISTS);
    TEST_REQUIRE(server.active_count() == 1u);

    second = server.open_session(make_open_request(1002u, 2u, 102u), &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(second != NULL);
    TEST_REQUIRE(server.active_count() == 2u);

    TEST_REQUIRE(server.open_session(make_open_request(1003u, 3u, 103u), &status) == NULL);
    TEST_REQUIRE(status == proto::ERROR_CODE_SESSION_NO_SLOT);
    TEST_REQUIRE(server.find_session(1001u) == first);
    TEST_REQUIRE(server.find_session(1002u) == second);

    TEST_REQUIRE(server.close_session(1001u, 7u, 200u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(server.active_count() == 1u);
    TEST_REQUIRE(server.find_session(1001u) == NULL);

    first = server.open_session(make_open_request(1003u, 3u, 201u), &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(first != NULL);
    TEST_REQUIRE(first->link_id() == 1003u);
    TEST_REQUIRE(server.active_count() == 2u);
    return ABE_TEST_STATUS_OK;
}

static int test_session_message_handlers(void)
{
    session::Session slots[1];
    session::SessionServer server;
    session::SessionServerConfig config;
    session::Session* current;
    MessageCounter move_counter;
    MessageCounter default_counter;
    SendCounter send_counter;
    const char payload[] = "abc";
    int status;

    memset(&move_counter, 0, sizeof(move_counter));
    memset(&default_counter, 0, sizeof(default_counter));
    memset(&send_counter, 0, sizeof(send_counter));
    memset(&config, 0, sizeof(config));

    config.server_id = 10u;
    config.sessions = slots;
    config.session_count = 1u;
    config.idle_timeout_ms = 0u;

    TEST_REQUIRE(server.init(config) == proto::ERROR_CODE_OK);
    current = server.open_session(make_open_request(2001u, 1u, 1000u), &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current != NULL);

    TEST_REQUIRE(current->set_message_handler(3001u, count_message, &move_counter) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(current->set_default_message_handler(count_message, &default_counter) ==
        proto::ERROR_CODE_OK);

    TEST_REQUIRE(server.handle_message(2001u, 3001u, payload, 3u, 1100u) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(move_counter.count == 1u);
    TEST_REQUIRE(move_counter.last_message_id == 3001u);
    TEST_REQUIRE(move_counter.last_size == 3u);
    TEST_REQUIRE(move_counter.last_link_id == 2001u);
    TEST_REQUIRE(current->last_recv_ms() == 1100u);

    TEST_REQUIRE(server.handle_message(2001u, 3999u, NULL, 0u, 1200u) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(default_counter.count == 1u);
    TEST_REQUIRE(default_counter.last_message_id == 3999u);
    TEST_REQUIRE(current->last_recv_ms() == 1200u);

    TEST_REQUIRE(current->clear_message_handler(3001u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(server.handle_message(2001u, 3001u, NULL, 0u, 1300u) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(default_counter.count == 2u);

    current->clear_default_message_handler();
    TEST_REQUIRE(server.handle_message(2001u, 3999u, NULL, 0u, 1400u) ==
        proto::ERROR_CODE_SESSION_NO_HANDLER);

    current->set_send_handler(count_send, &send_counter);
    TEST_REQUIRE(current->send(payload, 3u, 1500u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(send_counter.count == 1u);
    TEST_REQUIRE(send_counter.last_size == 3u);
    TEST_REQUIRE(send_counter.last_link_id == 2001u);
    TEST_REQUIRE(current->last_send_ms() == 1500u);
    return ABE_TEST_STATUS_OK;
}

static int test_session_state_and_idle_update(void)
{
    session::Session slots[2];
    session::SessionServer server;
    session::SessionServerConfig config;
    session::Session* current;
    uint32_t closed_count;
    int status;

    memset(&config, 0, sizeof(config));
    config.server_id = 11u;
    config.sessions = slots;
    config.session_count = 2u;
    config.idle_timeout_ms = 100u;

    TEST_REQUIRE(server.init(config) == proto::ERROR_CODE_OK);
    current = server.open_session(make_open_request(3001u, 1u, 1000u), &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current != NULL);

    TEST_REQUIRE(current->set_uid(8001u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current->state() == session::SESSION_STATE_AUTHENTICATED);
    TEST_REQUIRE(server.find_session_by_uid(8001u) == current);

    TEST_REQUIRE(current->enter_room(9001u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current->state() == session::SESSION_STATE_IN_ROOM);
    TEST_REQUIRE(current->enter_game() == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current->state() == session::SESSION_STATE_IN_GAME);
    TEST_REQUIRE(current->leave_game() == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current->state() == session::SESSION_STATE_IN_ROOM);
    TEST_REQUIRE(current->leave_room() == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current->state() == session::SESSION_STATE_AUTHENTICATED);

    closed_count = 999u;
    TEST_REQUIRE(server.update(1099u, &closed_count) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(closed_count == 0u);
    TEST_REQUIRE(server.active_count() == 1u);
    TEST_REQUIRE(server.update(1101u, &closed_count) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(closed_count == 1u);
    TEST_REQUIRE(server.active_count() == 0u);
    TEST_REQUIRE(server.find_session_by_uid(8001u) == NULL);

    current = server.open_session(make_open_request(3002u, 2u, 1200u), &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current != NULL);
    server.close(1300u);
    TEST_REQUIRE(server.initialized() == 0);
    TEST_REQUIRE(server.active_count() == 0u);
    TEST_REQUIRE(server.open_session(make_open_request(3003u, 3u, 1301u), &status) == NULL);
    TEST_REQUIRE(status == proto::ERROR_CODE_SESSION_CLOSED);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_server_accepts_derived_sessions() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_server_owns_link_sessions() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_session_message_handlers() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_session_state_and_idle_update() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
