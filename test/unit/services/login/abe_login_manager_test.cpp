#include "abe_login_manager.h"
#include "abe_snowflake.h"
#include "protocol.pb.h"

#include "../../abe_test.h"

#include <string.h>

namespace login = abe::service::login;
namespace proto = abe::proto::client;

static void fill_request(
    login::LoginAuthRequest* request,
    const char* account,
    const char* nickname,
    uint64_t now_ms)
{
    memset(request, 0, sizeof(*request));
    request->account = account;
    request->nickname = nickname;
    request->auth_token = "token";
    request->device_id = "device-a";
    request->client_version = "1.0.0";
    request->region = "global";
    request->now_ms = now_ms;
}

static int test_login_validation(void)
{
    TEST_REQUIRE(login::login_validate_account_name("alice_01", "bad") ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(login::login_validate_account_name("gm_player", "gm") ==
        proto::ERROR_CODE_AUTH_DIRTY_WORD);
    TEST_REQUIRE(login::login_validate_account_name("alice select", "") ==
        proto::ERROR_CODE_AUTH_SQL_PATTERN);
    TEST_REQUIRE(login::login_validate_account_name("a", "") ==
        proto::ERROR_CODE_AUTH_INVALID_ACCOUNT);
    TEST_REQUIRE(login::login_validate_account_name("alice' or '1", "") ==
        proto::ERROR_CODE_AUTH_SQL_PATTERN);

    TEST_REQUIRE(login::login_validate_nickname("Alice", "bad") ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(login::login_validate_nickname("badNick", "bad") ==
        proto::ERROR_CODE_AUTH_DIRTY_WORD);
    TEST_REQUIRE(login::login_validate_nickname("select name", "") ==
        proto::ERROR_CODE_AUTH_SQL_PATTERN);
    TEST_REQUIRE(login::login_validate_nickname("A", "") ==
        proto::ERROR_CODE_AUTH_INVALID_NICKNAME);
    return ABE_TEST_STATUS_OK;
}

static int test_login_manager_account_rules(void)
{
    abe_snowflake_t* id_generator;
    login::LoginManagerConfig config;
    login::LoginManager manager;
    login::LoginAuthRequest request;
    login::LoginAuthResult result;
    login::LoginAccountInfo account;

    id_generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(20u, &id_generator) == ABE_OK);

    login::set_login_manager_defaults(&config);
    config.max_accounts = 2u;
    config.require_auth_token = 0u;
    config.dirty_words = "bad,gm";
    TEST_REQUIRE(manager.init(config, id_generator) == proto::ERROR_CODE_OK);

    fill_request(&request, "alice01", "Alice", 1000u);
    TEST_REQUIRE(manager.authenticate(request, &result) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(result.created == 1u);
    TEST_REQUIRE(result.account.uid != 0u);
    TEST_REQUIRE(strcmp(result.account.account_name, "alice01") == 0);
    TEST_REQUIRE(strcmp(result.account.nickname, "Alice") == 0);
    TEST_REQUIRE(manager.account_count() == 1u);

    fill_request(&request, "bob02", "Alice", 1010u);
    TEST_REQUIRE(manager.authenticate(request, &result) ==
        proto::ERROR_CODE_AUTH_NICKNAME_EXISTS);

    fill_request(&request, "alice01", "", 1020u);
    TEST_REQUIRE(manager.authenticate(request, &result) == proto::ERROR_CODE_OK);
    TEST_REQUIRE(result.created == 0u);
    TEST_REQUIRE(manager.mark_login_success(result.account.uid, request, &account) ==
        proto::ERROR_CODE_OK);
    TEST_REQUIRE(account.last_login_time_ms == 1020u);

    fill_request(&request, "drop", "Dropper", 1030u);
    TEST_REQUIRE(manager.authenticate(request, &result) ==
        proto::ERROR_CODE_AUTH_SQL_PATTERN);

    manager.close();
    abe_snowflake_destroy(id_generator);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_login_validation() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_login_manager_account_rules() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
