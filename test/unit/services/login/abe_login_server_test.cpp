#include "abe_config.h"
#include "abe_login_server.h"
#include "abe_snowflake.h"
#include "protocol.pb.h"

#include "../../abe_test.h"

#include <string.h>

namespace gatehub = abe::service::gatehub;
namespace login = abe::service::login;
namespace proto = abe::proto::client;
namespace svc = abe::service::common;

static int init_login_server(
    login::LoginServer* server,
    abe_snowflake_t* id_generator)
{
    static const char json_text[] =
        "{"
        "  \"login\": {"
        "    \"max_accounts\": 8,"
        "    \"allow_register\": true,"
        "    \"unique_nickname\": true,"
        "    \"require_auth_token\": false,"
        "    \"dirty_words\": \"bad,gm\","
        "    \"default_region\": \"test\","
        "    \"max_sessions\": 8,"
        "    \"allow_reconnect\": true,"
        "    \"replace_duplicate_login\": true,"
        "    \"reconnect_grace_ms\": 1000,"
        "    \"session_ttl_ms\": 60000"
        "  }"
        "}";
    abe_config_t* config;
    svc::Context context;

    config = NULL;
    TEST_REQUIRE(abe_config_load_json_text(json_text, &config) == ABE_CONFIG_OK);
    TEST_REQUIRE(server->load_config(config) == svc::SERVICE_STATUS_OK);
    abe_config_destroy(config);

    memset(&context, 0, sizeof(context));
    context.id_generator = id_generator;
    TEST_REQUIRE(server->init(context) == svc::SERVICE_STATUS_OK);
    return ABE_TEST_STATUS_OK;
}

static void fill_login_request(
    proto::PB_CS_LOGIN_REQ* request,
    const char* account,
    const char* nickname,
    const char* session_token)
{
    request->Clear();
    request->mutable_header()->set_seq(100u);
    request->set_account(account == NULL ? "" : account);
    request->set_nickname(nickname == NULL ? "" : nickname);
    request->set_device_id("device-a");
    request->set_client_version("1.0.0");
    if (session_token != NULL) {
        request->set_session_token(session_token);
    }
}

static int test_login_server_success_and_reconnect(void)
{
    abe_snowflake_t* id_generator;
    login::LoginServer server;
    proto::PB_CS_LOGIN_REQ request;
    proto::PB_SC_LOGIN_RESP response;
    gatehub::GateHubSessionInfo session;
    char token[gatehub::ABE_GATEHUB_SESSION_TOKEN_CAPACITY];

    id_generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(21u, &id_generator) == ABE_OK);
    TEST_REQUIRE(init_login_server(&server, id_generator) == ABE_TEST_STATUS_OK);

    fill_login_request(&request, "alice01", "Alice", NULL);
    TEST_REQUIRE(server.handle_login(1u, 11u, request, &response, 1000u) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(response.result().error_code() == proto::ERROR_CODE_OK);
    TEST_REQUIRE(response.player().uid() != 0u);
    TEST_REQUIRE(response.account_info().account_name() == "alice01");
    TEST_REQUIRE(response.account_info().nickname() == "Alice");
    TEST_REQUIRE(response.player_data().player().uid() == response.player().uid());
    TEST_REQUIRE(response.player_data().player().open_id() == "alice01");
    TEST_REQUIRE(response.player_data().nickname() == "Alice");
    TEST_REQUIRE(response.player_data().level() == 1u);
    TEST_REQUIRE(response.session_token().empty() == false);
    strncpy(token, response.session_token().c_str(), sizeof(token));
    token[sizeof(token) - 1u] = '\0';

    TEST_REQUIRE(server.handle_disconnect(1u, 11u, 1100u) == proto::ERROR_CODE_OK);
    fill_login_request(&request, "alice01", "", token);
    TEST_REQUIRE(server.handle_login(2u, 22u, request, &response, 1200u) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(response.session_token() == token);
    TEST_REQUIRE(server.session_registry()->find_session(
            response.player().uid(),
            &session) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(session.gateway_id == 2u);
    TEST_REQUIRE(session.connection_id == 22u);

    server.close(2000u);
    abe_snowflake_destroy(id_generator);
    return ABE_TEST_STATUS_OK;
}

static int test_login_server_rejects_bad_inputs(void)
{
    abe_snowflake_t* id_generator;
    login::LoginServer server;
    proto::PB_CS_LOGIN_REQ request;
    proto::PB_SC_LOGIN_RESP response;

    id_generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(22u, &id_generator) == ABE_OK);
    TEST_REQUIRE(init_login_server(&server, id_generator) == ABE_TEST_STATUS_OK);

    fill_login_request(&request, "gm_user", "Name", NULL);
    TEST_REQUIRE(server.handle_login(1u, 11u, request, &response, 1000u) ==
        proto::ERROR_CODE_AUTH_DIRTY_WORD);
    TEST_REQUIRE(response.result().error_code() == proto::ERROR_CODE_AUTH_DIRTY_WORD);

    fill_login_request(&request, "bob02", "select name", NULL);
    TEST_REQUIRE(server.handle_login(1u, 12u, request, &response, 1010u) ==
        proto::ERROR_CODE_AUTH_SQL_PATTERN);

    server.close(2000u);
    abe_snowflake_destroy(id_generator);
    return ABE_TEST_STATUS_OK;
}

static int test_login_server_character_handlers(void)
{
    abe_snowflake_t* id_generator;
    login::LoginServer server;
    proto::PB_CS_CREATE_CHARACTOR create_request;
    proto::PB_SC_CREATE_CHARACTOR create_response;
    proto::PB_CS_SELECT_CHARACTOR select_request;
    proto::PB_SC_SELECT_CHARACTOR select_response;
    proto::PB_CS_DELETE_CHARACTOR delete_request;
    proto::PB_SC_DELETE_CHARACTOR delete_response;

    id_generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(23u, &id_generator) == ABE_OK);
    TEST_REQUIRE(init_login_server(&server, id_generator) == ABE_TEST_STATUS_OK);

    create_request.mutable_header()->set_protocol_id(proto::CS_CREATE_CHARACTOR);
    create_request.mutable_header()->set_seq(201u);
    TEST_REQUIRE(server.handle_create_charactor(
            1u,
            11u,
            create_request,
            &create_response,
            1000u) == proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE);
    TEST_REQUIRE(create_response.header().protocol_id() == proto::SC_CREATE_CHARACTOR);
    TEST_REQUIRE(create_response.header().seq() == 201u);
    TEST_REQUIRE(create_response.result().error_code() ==
        proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE);

    select_request.mutable_header()->set_protocol_id(proto::CS_SELECT_CHARACTOR);
    TEST_REQUIRE(server.handle_select_charactor(
            1u,
            11u,
            select_request,
            &select_response,
            1010u) == proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE);
    TEST_REQUIRE(select_response.header().protocol_id() == proto::SC_SELECT_CHARACTOR);

    delete_request.mutable_header()->set_protocol_id(proto::CS_DELETE_CHARACTOR);
    TEST_REQUIRE(server.handle_delete_charactor(
            1u,
            11u,
            delete_request,
            &delete_response,
            1020u) == proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE);
    TEST_REQUIRE(delete_response.header().protocol_id() == proto::SC_DELETE_CHARACTOR);

    server.close(2000u);
    abe_snowflake_destroy(id_generator);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_login_server_success_and_reconnect() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_login_server_rejects_bad_inputs() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_login_server_character_handlers() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
