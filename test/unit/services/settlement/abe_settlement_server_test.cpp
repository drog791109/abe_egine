#include "abe_settlement_server.h"

#include "../../abe_test.h"

#include <string.h>

namespace settlement = abe::service::settlement;
namespace svc = abe::service::common;

static int test_settlement_server_defaults_and_config(void)
{
    settlement::SettlementServer server;
    settlement::SettlementServerConfig config;
    abe_config_t* parsed;
    const char json_text[] =
        "{"
        "  \"settlement\": {"
        "    \"max_pending_events\": 128,"
        "    \"server_id\": 5,"
        "    \"idle_timeout_ms\": 8000"
        "  }"
        "}";

    memset(&config, 0, sizeof(config));
    settlement::set_settlement_server_defaults(&config);
    TEST_REQUIRE(config.max_pending_events == 8192u);
    TEST_REQUIRE(config.server_id == 1u);
    TEST_REQUIRE(config.idle_timeout_ms == 60000u);

    parsed = NULL;
    TEST_REQUIRE(abe_config_load_json_text(json_text, &parsed) == ABE_CONFIG_OK);
    TEST_REQUIRE(server.load_config(parsed) == svc::SERVICE_STATUS_OK);
    abe_config_destroy(parsed);

    TEST_REQUIRE(server.name() != NULL);
    TEST_REQUIRE(strcmp(server.name(), "settlement") == 0);
    TEST_REQUIRE(server.config_path() != NULL);
    TEST_REQUIRE(strcmp(server.config_path(), "server/bin/settlement.json") == 0);
    TEST_REQUIRE(server.initialized() == 0);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_settlement_server_defaults_and_config() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
