#include "abe_session_manager.h"

#include "protocol.pb.h"

#include "../../../abe_test.h"

#include <stdint.h>
#include <string.h>

namespace session = abe::service::session;
namespace proto = abe::proto::client;

class TestSession : public session::Session {
public:
    TestSession()
        : connect_count(0u),
          close_count(0u),
          reset_count(0u),
          message_count(0u),
          send_count(0u),
          frame_count(0u),
          last_user_data(NULL),
          last_message_size(0u),
          last_send_size(0u),
          last_delta_ms(0u),
          fail_connect(false),
          fail_send(false)
    {
    }

    uint32_t connect_count;
    uint32_t close_count;
    uint32_t reset_count;
    uint32_t message_count;
    uint32_t send_count;
    uint32_t frame_count;
    void* last_user_data;
    uint32_t last_message_size;
    uint32_t last_send_size;
    uint64_t last_delta_ms;
    bool fail_connect;
    bool fail_send;

protected:
    virtual int on_connect(const session::SessionOpenRequest& request)
    {
        ++connect_count;
        last_user_data = request.user_data;
        if (fail_connect) {
            return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
        }
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
        last_user_data = NULL;
    }

    virtual int on_message(const void* data, uint32_t size, uint64_t now_ms)
    {
        (void)data;
        (void)now_ms;
        ++message_count;
        last_message_size = size;
        return proto::ERROR_CODE_OK;
    }

    virtual int send_packet(const void* data, uint32_t size)
    {
        (void)data;
        if (fail_send) {
            return proto::ERROR_CODE_COMMON_PROTOCOL_ERROR;
        }
        ++send_count;
        last_send_size = size;
        return proto::ERROR_CODE_OK;
    }

    virtual void on_frame_update(uint64_t delta_ms)
    {
        ++frame_count;
        last_delta_ms = delta_ms;
    }
};

static session::SessionOpenRequest make_open_request(uint64_t conn_id, uint64_t now_ms)
{
    session::SessionOpenRequest request;

    request.conn_id = conn_id;
    request.now_ms = now_ms;
    request.user_data = NULL;
    return request;
}

static int test_session_lifecycle(void)
{
    TestSession current;
    session::SessionOpenRequest request;
    int user_data;
    const char payload[] = "abc";

    request = make_open_request(1001u, 100u);
    request.user_data = &user_data;

    TEST_REQUIRE(current.open(7u, request) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current.active());
    TEST_REQUIRE(current.server_id() == 7u);
    TEST_REQUIRE(current.conn_id() == 1001u);
    TEST_REQUIRE(current.last_active_ms() == 100u);
    TEST_REQUIRE(current.user_data() == &user_data);
    TEST_REQUIRE(current.connect_count == 1u);
    TEST_REQUIRE(current.last_user_data == &user_data);

    TEST_REQUIRE(current.receive(payload, 3u, 150u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current.message_count == 1u);
    TEST_REQUIRE(current.last_message_size == 3u);
    TEST_REQUIRE(current.last_active_ms() == 150u);

    TEST_REQUIRE(current.send(payload, 3u, 160u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current.send_count == 1u);
    TEST_REQUIRE(current.last_send_size == 3u);
    TEST_REQUIRE(current.last_active_ms() == 160u);

    TEST_REQUIRE(current.mark_authenticated(9001u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current.authenticated());
    TEST_REQUIRE(current.user_id() == 9001u);
    current.clear_authenticated();
    TEST_REQUIRE(!current.authenticated());
    TEST_REQUIRE(current.user_id() == 0u);

    ((session::Session*)&current)->on_frame_update(16u);
    TEST_REQUIRE(current.frame_count == 1u);
    TEST_REQUIRE(current.last_delta_ms == 16u);

    TEST_REQUIRE(!current.is_timeout(260u, 100u));
    TEST_REQUIRE(current.is_timeout(261u, 100u));

    current.close(3u, 300u);
    TEST_REQUIRE(!current.active());
    TEST_REQUIRE(current.close_reason() == 3u);
    TEST_REQUIRE(current.close_count == 1u);
    TEST_REQUIRE(current.last_active_ms() == 300u);

    current.reset();
    TEST_REQUIRE(current.conn_id() == 0u);
    TEST_REQUIRE(current.user_data() == NULL);
    TEST_REQUIRE(current.reset_count >= 1u);
    return ABE_TEST_STATUS_OK;
}

static int test_session_rejects_invalid_access(void)
{
    TestSession current;
    const char payload[] = "abc";

    TEST_REQUIRE(current.open(0u, make_open_request(1001u, 100u)) ==
        proto::ERROR_CODE_COMMON_INVALID_ARGUMENT);
    TEST_REQUIRE(current.open(7u, make_open_request(0u, 100u)) ==
        proto::ERROR_CODE_COMMON_INVALID_ARGUMENT);

    current.fail_connect = true;
    TEST_REQUIRE(current.open(7u, make_open_request(1001u, 100u)) ==
        proto::ERROR_CODE_COMMON_INVALID_ARGUMENT);
    TEST_REQUIRE(!current.active());

    current.fail_connect = false;
    TEST_REQUIRE(current.open(7u, make_open_request(1001u, 100u)) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(current.receive(NULL, 3u, 110u) ==
        proto::ERROR_CODE_COMMON_INVALID_ARGUMENT);
    TEST_REQUIRE(current.send(NULL, 3u, 110u) ==
        proto::ERROR_CODE_COMMON_INVALID_ARGUMENT);
    TEST_REQUIRE(current.mark_authenticated(0u) ==
        proto::ERROR_CODE_COMMON_INVALID_ARGUMENT);

    current.fail_send = true;
    TEST_REQUIRE(current.send(payload, 3u, 120u) ==
        proto::ERROR_CODE_COMMON_PROTOCOL_ERROR);
    TEST_REQUIRE(current.last_active_ms() == 100u);

    current.close(0u, 130u);
    TEST_REQUIRE(current.receive(payload, 3u, 140u) ==
        proto::ERROR_CODE_SESSION_CLOSED);
    TEST_REQUIRE(current.send(payload, 3u, 140u) ==
        proto::ERROR_CODE_SESSION_CLOSED);
    TEST_REQUIRE(current.mark_authenticated(1u) ==
        proto::ERROR_CODE_SESSION_CLOSED);
    return ABE_TEST_STATUS_OK;
}

static int test_server_owns_session_slots(void)
{
    session::SessionManager server;
    TestSession* first;
    TestSession* second;
    int status;

    TEST_REQUIRE(server.init<TestSession>(9u, 2u, 0u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(server.capacity() == 2u);
    TEST_REQUIRE(server.active_count() == 0u);

    first = (TestSession*)server.open_session(make_open_request(1001u, 100u), &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(first != NULL);
    TEST_REQUIRE(first->conn_id() == 1001u);
    TEST_REQUIRE(first->server_id() == 9u);
    TEST_REQUIRE(first->reset_count == 2u);
    TEST_REQUIRE(server.active_count() == 1u);

    TEST_REQUIRE(server.open_session(make_open_request(1001u, 101u), &status) == NULL);
    TEST_REQUIRE(status == proto::ERROR_CODE_SESSION_ALREADY_EXISTS);
    TEST_REQUIRE(server.active_count() == 1u);

    second = (TestSession*)server.open_session(make_open_request(1002u, 102u), &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(second != NULL);
    TEST_REQUIRE(second != first);
    TEST_REQUIRE(server.active_count() == 2u);

    TEST_REQUIRE(server.open_session(make_open_request(1003u, 103u), &status) == NULL);
    TEST_REQUIRE(status == proto::ERROR_CODE_SESSION_NO_SLOT);
    TEST_REQUIRE(server.find_session(1001u) == first);
    TEST_REQUIRE(server.find_session(1002u) == second);

    TEST_REQUIRE(server.close_session(1001u, 7u, 200u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(server.active_count() == 1u);
    TEST_REQUIRE(server.find_session(1001u) == NULL);
    TEST_REQUIRE(first->close_count == 1u);

    first = (TestSession*)server.open_session(make_open_request(1003u, 201u), &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(first != NULL);
    TEST_REQUIRE(first->conn_id() == 1003u);
    TEST_REQUIRE(server.active_count() == 2u);
    server.close(202u);
    TEST_REQUIRE(server.initialized() == 0);
    return ABE_TEST_STATUS_OK;
}

static int test_server_message_auth_and_timeout(void)
{
    session::SessionManager server;
    TestSession* current;
    uint32_t closed_count;
    int status;

    TEST_REQUIRE(server.init<TestSession>(11u, 2u, 100u) == proto::ERROR_CODE_OK);
    current = (TestSession*)server.open_session(make_open_request(3001u, 1000u), &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current != NULL);

    TEST_REQUIRE(server.handle_message(3001u, NULL, 0u, 1050u) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(current->message_count == 1u);
    TEST_REQUIRE(current->last_active_ms() == 1050u);

    TEST_REQUIRE(current->mark_authenticated(8001u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current->authenticated());
    TEST_REQUIRE(server.find_session_by_user_id(8001u) == current);
    TEST_REQUIRE(current->mark_authenticated(8002u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(server.find_session_by_user_id(8001u) == NULL);
    TEST_REQUIRE(server.find_session_by_user_id(8002u) == current);

    closed_count = 999u;
    TEST_REQUIRE(server.update(1150u, &closed_count) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(closed_count == 0u);
    TEST_REQUIRE(server.active_count() == 1u);

    TEST_REQUIRE(server.update(1151u, &closed_count) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(closed_count == 1u);
    TEST_REQUIRE(server.active_count() == 0u);
    TEST_REQUIRE(server.find_session_by_user_id(8002u) == NULL);

    current = (TestSession*)server.open_session(make_open_request(3002u, 1200u), &status);
    TEST_REQUIRE(status == proto::ERROR_CODE_OK);
    TEST_REQUIRE(current != NULL);
    server.close(1300u);
    TEST_REQUIRE(server.initialized() == 0);
    TEST_REQUIRE(server.active_count() == 0u);
    TEST_REQUIRE(server.open_session(make_open_request(3003u, 1301u), &status) == NULL);
    TEST_REQUIRE(status == proto::ERROR_CODE_SESSION_CLOSED);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_session_lifecycle() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_session_rejects_invalid_access() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_server_owns_session_slots() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_server_message_auth_and_timeout() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
