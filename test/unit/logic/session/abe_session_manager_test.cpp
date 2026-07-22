#include "abe_session_manager.h"

#include <stdio.h>
#include <string>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

namespace session = abe::logic::session;

static session::LoginRequest make_login(uint64_t uid, uint64_t gateway, uint64_t conn, uint64_t now)
{
    session::LoginRequest request;

    request.uid = uid;
    request.gateway_id = gateway;
    request.conn_id = conn;
    request.login_token = "login-token";
    request.device_id = "device-a";
    request.now_ms = now;
    return request;
}

static int test_login_and_duplicate_kick(void)
{
    session::SessionManager manager;
    session::SessionConfig config;
    session::SessionInfo current;
    session::SessionInfo kicked;
    std::string old_token;

    config.session_ttl_ms = 1000u;
    config.reconnect_ttl_ms = 300u;
    manager.init(config);

    TEST_REQUIRE(manager.login(make_login(100u, 1u, 10u, 1000u), &current, NULL) ==
        session::SESSION_STATUS_OK);
    TEST_REQUIRE(current.uid == 100u);
    TEST_REQUIRE(current.state == session::SESSION_STATE_ONLINE);
    TEST_REQUIRE(current.gateway_id == 1u);
    TEST_REQUIRE(current.conn_id == 10u);
    TEST_REQUIRE(!current.session_token.empty());
    old_token = current.session_token;

    TEST_REQUIRE(manager.login(make_login(100u, 2u, 20u, 1100u), &current, &kicked) ==
        session::SESSION_STATUS_OK);
    TEST_REQUIRE(kicked.uid == 100u);
    TEST_REQUIRE(kicked.gateway_id == 1u);
    TEST_REQUIRE(current.gateway_id == 2u);
    TEST_REQUIRE(current.conn_id == 20u);
    TEST_REQUIRE(old_token != current.session_token);
    TEST_REQUIRE(manager.count() == 1u);
    return 0;
}

static int test_heartbeat_and_expire(void)
{
    session::SessionManager manager;
    session::SessionConfig config;
    session::SessionInfo info;
    std::string token;

    config.session_ttl_ms = 1000u;
    config.reconnect_ttl_ms = 300u;
    manager.init(config);

    TEST_REQUIRE(manager.login(make_login(101u, 1u, 11u, 1000u), &info, NULL) ==
        session::SESSION_STATUS_OK);
    token = info.session_token;

    TEST_REQUIRE(manager.heartbeat(101u, "bad-token", 1100u, &info) ==
        session::SESSION_STATUS_TOKEN_MISMATCH);
    TEST_REQUIRE(manager.heartbeat(101u, token.c_str(), 1500u, &info) == session::SESSION_STATUS_OK);
    TEST_REQUIRE(info.last_heartbeat_ms == 1500u);
    TEST_REQUIRE(info.expire_time_ms == 2500u);

    TEST_REQUIRE(manager.heartbeat(101u, token.c_str(), 2600u, &info) == session::SESSION_STATUS_EXPIRED);
    TEST_REQUIRE(manager.remove_expired(2600u) == 1);
    TEST_REQUIRE(manager.count() == 0u);
    return 0;
}

static int test_disconnect_reconnect_and_room(void)
{
    session::SessionManager manager;
    session::SessionConfig config;
    session::SessionInfo info;

    config.session_ttl_ms = 1000u;
    config.reconnect_ttl_ms = 300u;
    manager.init(config);

    TEST_REQUIRE(manager.login(make_login(102u, 1u, 12u, 1000u), &info, NULL) ==
        session::SESSION_STATUS_OK);
    TEST_REQUIRE(manager.bind_room(102u, 9001u, &info) == session::SESSION_STATUS_OK);
    TEST_REQUIRE(info.room_id == 9001u);

    TEST_REQUIRE(manager.disconnect(102u, 12u, 1200u, &info) == session::SESSION_STATUS_OK);
    TEST_REQUIRE(info.state == session::SESSION_STATE_RECONNECTING);
    TEST_REQUIRE(info.gateway_id == 0u);
    TEST_REQUIRE(info.reconnect_deadline_ms == 1500u);

    TEST_REQUIRE(manager.reconnect(make_login(102u, 2u, 22u, 1400u), &info) ==
        session::SESSION_STATUS_OK);
    TEST_REQUIRE(info.state == session::SESSION_STATE_ONLINE);
    TEST_REQUIRE(info.gateway_id == 2u);
    TEST_REQUIRE(info.conn_id == 22u);
    TEST_REQUIRE(info.room_id == 9001u);

    TEST_REQUIRE(manager.clear_room(102u, 9001u, &info) == session::SESSION_STATUS_OK);
    TEST_REQUIRE(info.room_id == 0u);
    return 0;
}

static int test_reconnect_expire_and_kick(void)
{
    session::SessionManager manager;
    session::SessionConfig config;
    session::SessionInfo info;

    config.session_ttl_ms = 1000u;
    config.reconnect_ttl_ms = 300u;
    manager.init(config);

    TEST_REQUIRE(manager.login(make_login(103u, 1u, 13u, 1000u), &info, NULL) ==
        session::SESSION_STATUS_OK);
    TEST_REQUIRE(manager.disconnect(103u, 13u, 1200u, &info) == session::SESSION_STATUS_OK);
    TEST_REQUIRE(manager.reconnect(make_login(103u, 2u, 23u, 1600u), &info) ==
        session::SESSION_STATUS_EXPIRED);
    TEST_REQUIRE(manager.count() == 0u);

    TEST_REQUIRE(manager.login(make_login(104u, 1u, 14u, 1000u), &info, NULL) ==
        session::SESSION_STATUS_OK);
    TEST_REQUIRE(manager.kick(104u, 1100u, &info) == session::SESSION_STATUS_OK);
    TEST_REQUIRE(info.uid == 104u);
    TEST_REQUIRE(manager.find(104u, &info) == session::SESSION_STATUS_NOT_FOUND);
    return 0;
}

int main()
{
    if (test_login_and_duplicate_kick() != 0) {
        return 1;
    }
    if (test_heartbeat_and_expire() != 0) {
        return 1;
    }
    if (test_disconnect_reconnect_and_room() != 0) {
        return 1;
    }
    if (test_reconnect_expire_and_kick() != 0) {
        return 1;
    }
    return 0;
}
