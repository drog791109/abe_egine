#include "abe_match_server.h"

#include "../../abe_test.h"

#include <string.h>

namespace match = abe::service::match;
namespace svc = abe::service::common;

static int test_match_server_defaults_and_config(void)
{
    match::MatchServer server;
    match::MatchServerConfig config;
    abe_config_t* parsed;
    const char json_text[] =
        "{"
        "  \"match\": {"
        "    \"max_queues\": 8,"
        "    \"max_players_per_queue\": 16,"
        "    \"server_id\": 3,"
        "    \"idle_timeout_ms\": 5000"
        "  }"
        "}";

    memset(&config, 0, sizeof(config));
    match::set_match_server_defaults(&config);
    TEST_REQUIRE(config.max_queues == 1024u);
    TEST_REQUIRE(config.max_players_per_queue == 64u);
    TEST_REQUIRE(config.server_id == 1u);
    TEST_REQUIRE(config.idle_timeout_ms == 60000u);

    parsed = NULL;
    TEST_REQUIRE(abe_config_load_json_text(json_text, &parsed) == ABE_CONFIG_OK);
    TEST_REQUIRE(server.load_config(parsed) == svc::SERVICE_STATUS_OK);
    abe_config_destroy(parsed);

    TEST_REQUIRE(server.name() != NULL);
    TEST_REQUIRE(strcmp(server.name(), "match") == 0);
    TEST_REQUIRE(server.config_path() != NULL);
    TEST_REQUIRE(strcmp(server.config_path(), "server/bin/match.json") == 0);
    TEST_REQUIRE(server.initialized() == 0);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_match_server_defaults_and_config() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
