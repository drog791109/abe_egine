#include "abe_gatehub_server.h"
#include "abe_snowflake.h"
#include "protocol.pb.h"

#include "../../abe_test.h"

#include <string.h>

namespace gatehub = abe::service::gatehub;
namespace proto = abe::proto::client;
namespace svc = abe::service::common;

static int open_session(
    gatehub::GateHubRegistry* registry,
    uint64_t uid,
    uint64_t gateway_id,
    uint64_t connection_id,
    const char* session_token,
    uint64_t now_ms,
    gatehub::GateHubOpenResult* out_result)
{
    gatehub::GateHubOpenRequest request;

    memset(&request, 0, sizeof(request));
    request.account_id = uid + 1000u;
    request.uid = uid;
    request.gateway_id = gateway_id;
    request.connection_id = connection_id;
    request.session_token = session_token;
    request.now_ms = now_ms;
    return registry->open_session(request, out_result);
}

static int test_gatehub_reconnect(void)
{
    abe_snowflake_t* id_generator;
    gatehub::GateHubConfig config;
    gatehub::GateHubRegistry registry;
    gatehub::GateHubOpenResult first;
    gatehub::GateHubOpenResult second;
    gatehub::GateHubSessionInfo info;
    char token[gatehub::ABE_GATEHUB_SESSION_TOKEN_CAPACITY];

    id_generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(10u, &id_generator) == ABE_OK);

    gatehub::set_gatehub_defaults(&config);
    config.max_sessions = 2u;
    config.reconnect_grace_ms = 20u;
    TEST_REQUIRE(registry.init(config, id_generator) == proto::ERROR_CODE_OK);

    TEST_REQUIRE(open_session(&registry, 100u, 1u, 11u, NULL, 1000u, &first) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(first.session.uid == 100u);
    TEST_REQUIRE(first.session.state == gatehub::GATEHUB_SESSION_ONLINE);
    TEST_REQUIRE(first.session.session_token[0] != '\0');
    TEST_REQUIRE(registry.active_count() == 1u);
    strncpy(token, first.session.session_token, sizeof(token));
    token[sizeof(token) - 1u] = '\0';

    TEST_REQUIRE(registry.disconnect(1u, 11u, 1005u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(registry.find_session(100u, &info) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(info.state == gatehub::GATEHUB_SESSION_RECONNECTING);

    TEST_REQUIRE(open_session(&registry, 100u, 2u, 22u, token, 1010u, &second) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(second.reconnected == 1u);
    TEST_REQUIRE(second.session.gateway_id == 2u);
    TEST_REQUIRE(second.session.connection_id == 22u);
    TEST_REQUIRE(strcmp(second.session.session_token, token) == 0);
    TEST_REQUIRE(second.session.session_id == first.session.session_id);
    TEST_REQUIRE(registry.active_count() == 1u);

    registry.close();
    abe_snowflake_destroy(id_generator);
    return ABE_TEST_STATUS_OK;
}

static int test_gatehub_duplicate_policy(void)
{
    abe_snowflake_t* id_generator;
    gatehub::GateHubConfig config;
    gatehub::GateHubRegistry registry;
    gatehub::GateHubOpenResult first;
    gatehub::GateHubOpenResult second;

    id_generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(11u, &id_generator) == ABE_OK);

    gatehub::set_gatehub_defaults(&config);
    config.max_sessions = 2u;
    config.replace_duplicate_login = 0u;
    TEST_REQUIRE(registry.init(config, id_generator) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(open_session(&registry, 200u, 1u, 11u, NULL, 1000u, &first) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(open_session(&registry, 200u, 1u, 12u, NULL, 1010u, &second) ==
        proto::ERROR_CODE_AUTH_DUPLICATE_LOGIN);
    registry.close();

    config.replace_duplicate_login = 1u;
    TEST_REQUIRE(registry.init(config, id_generator) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(open_session(&registry, 200u, 1u, 11u, NULL, 1000u, &first) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(open_session(&registry, 200u, 2u, 22u, NULL, 1010u, &second) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(second.replaced == 1u);
    TEST_REQUIRE(second.replaced_gateway_id == 1u);
    TEST_REQUIRE(second.replaced_connection_id == 11u);
    TEST_REQUIRE(second.session.gateway_id == 2u);
    TEST_REQUIRE(registry.active_count() == 1u);

    registry.close();
    abe_snowflake_destroy(id_generator);
    return ABE_TEST_STATUS_OK;
}

static int test_gatehub_reconnect_expire(void)
{
    abe_snowflake_t* id_generator;
    gatehub::GateHubConfig config;
    gatehub::GateHubRegistry registry;
    gatehub::GateHubOpenResult result;
    uint32_t closed_count;

    id_generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(12u, &id_generator) == ABE_OK);

    gatehub::set_gatehub_defaults(&config);
    config.max_sessions = 1u;
    config.reconnect_grace_ms = 5u;
    TEST_REQUIRE(registry.init(config, id_generator) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(open_session(&registry, 300u, 3u, 33u, NULL, 1000u, &result) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(registry.disconnect(3u, 33u, 1001u) == proto::ERROR_CODE_OK);

    closed_count = 0u;
    TEST_REQUIRE(registry.update(1007u, &closed_count) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(closed_count == 1u);
    TEST_REQUIRE(registry.active_count() == 0u);

    registry.close();
    abe_snowflake_destroy(id_generator);
    return ABE_TEST_STATUS_OK;
}

static int test_gatehub_server_message_handlers(void)
{
    abe_snowflake_t* id_generator;
    gatehub::GateHubServer server;
    gatehub::GateHubOpenRequest open_request;
    gatehub::GateHubOpenResult open_result;
    proto::PB_CS_ENTER_LOBBY_REQ enter_lobby;
    proto::PB_SC_ENTER_LOBBY_RESP lobby_response;
    proto::PB_CS_ROOM_LIST_REQ room_list;
    proto::PB_SC_ROOM_LIST_RESP room_list_response;
    svc::Context context;

    id_generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(13u, &id_generator) == ABE_OK);

    memset(&context, 0, sizeof(context));
    context.id_generator = id_generator;
    TEST_REQUIRE(server.init(context) == svc::SERVICE_STATUS_OK);

    memset(&open_request, 0, sizeof(open_request));
    open_request.account_id = 4100u;
    open_request.uid = 400u;
    open_request.gateway_id = 4u;
    open_request.connection_id = 44u;
    open_request.now_ms = 1000u;
    TEST_REQUIRE(server.registry()->open_session(open_request, &open_result) ==
        proto::ERROR_CODE_OK);

    enter_lobby.mutable_header()->set_protocol_id(proto::CS_ENTER_LOBBY_REQ);
    enter_lobby.mutable_header()->set_seq(301u);
    enter_lobby.set_uid(400u);
    TEST_REQUIRE(server.handle_enter_lobby(
            4u,
            44u,
            enter_lobby,
            &lobby_response,
            1010u) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(lobby_response.header().protocol_id() == proto::SC_ENTER_LOBBY_RESP);
    TEST_REQUIRE(lobby_response.header().seq() == 301u);
    TEST_REQUIRE(lobby_response.result().error_code() == proto::ERROR_CODE_OK);
    TEST_REQUIRE(lobby_response.player().uid() == 400u);
    TEST_REQUIRE(lobby_response.lobby_time_ms() == 1010u);

    room_list.mutable_header()->set_protocol_id(proto::CS_ROOM_LIST_REQ);
    room_list.mutable_header()->set_seq(302u);
    room_list.set_uid(400u);
    TEST_REQUIRE(server.handle_room_list(
            4u,
            44u,
            room_list,
            &room_list_response,
            1020u) == proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE);
    TEST_REQUIRE(room_list_response.header().protocol_id() == proto::SC_ROOM_LIST_RESP);
    TEST_REQUIRE(room_list_response.header().seq() == 302u);
    TEST_REQUIRE(room_list_response.result().error_code() ==
        proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE);

    server.close(2000u);
    abe_snowflake_destroy(id_generator);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_gatehub_reconnect() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_gatehub_duplicate_policy() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_gatehub_reconnect_expire() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_gatehub_server_message_handlers() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
