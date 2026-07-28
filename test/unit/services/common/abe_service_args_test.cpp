#include "abe_service_args.h"
#include "abe_service_runtime.h"

#include "../../abe_test.h"

#include <stdint.h>
#include <string.h>

namespace service_common = abe::service::common;

static int test_parse_numbers(void)
{
    uint32_t value32;
    uint64_t value64;
    int32_t value_i32;

    value32 = 0u;
    TEST_REQUIRE(service_common::service_parse_u32("12", 1u, 20u, &value32) ==
        service_common::SERVICE_ARG_OK);
    TEST_REQUIRE(value32 == 12u);
    TEST_REQUIRE(service_common::service_parse_u32("21", 1u, 20u, &value32) ==
        service_common::SERVICE_ARG_INVALID_VALUE);

    value64 = 0u;
    TEST_REQUIRE(service_common::service_parse_u64("123456789", 0u, 200000000u, &value64) ==
        service_common::SERVICE_ARG_OK);
    TEST_REQUIRE(value64 == 123456789u);
    TEST_REQUIRE(service_common::service_parse_u64("abc", 0u, 200000000u, &value64) ==
        service_common::SERVICE_ARG_INVALID_VALUE);

    value_i32 = 0;
    TEST_REQUIRE(service_common::service_parse_i32("-300", -840, 840, &value_i32) ==
        service_common::SERVICE_ARG_OK);
    TEST_REQUIRE(value_i32 == -300);
    TEST_REQUIRE(service_common::service_parse_i32("-900", -840, 840, &value_i32) ==
        service_common::SERVICE_ARG_INVALID_VALUE);
    return ABE_TEST_STATUS_OK;
}

static int test_parse_option_table(void)
{
    const char* host;
    uint32_t port;
    uint64_t server_id;
    service_common::ServiceOption options[3];
    char program[] = "svc";
    char host_name[] = "--host";
    char host_value[] = "127.0.0.1";
    char port_name[] = "--port";
    char port_value[] = "7000";
    char server_name[] = "--server-id";
    char server_value[] = "99";
    char* argv[] = {
        program,
        host_name,
        host_value,
        port_name,
        port_value,
        server_name,
        server_value
    };

    host = NULL;
    port = 0u;
    server_id = 0u;
    memset(options, 0, sizeof(options));

    options[0].name = "--host";
    options[0].type = service_common::SERVICE_OPTION_STRING;
    options[0].out_value = &host;

    options[1].name = "--port";
    options[1].type = service_common::SERVICE_OPTION_U32;
    options[1].out_value = &port;
    options[1].min_value = 1u;
    options[1].max_value = 65535u;

    options[2].name = "--server-id";
    options[2].type = service_common::SERVICE_OPTION_U64;
    options[2].out_value = &server_id;
    options[2].min_value = 1u;
    options[2].max_value = 1000u;

    TEST_REQUIRE(service_common::service_parse_options(7, argv, options, 3u) ==
        service_common::SERVICE_ARG_OK);
    TEST_REQUIRE(host != NULL);
    TEST_REQUIRE(strcmp(host, "127.0.0.1") == 0);
    TEST_REQUIRE(port == 7000u);
    TEST_REQUIRE(server_id == 99u);
    return ABE_TEST_STATUS_OK;
}

static int test_parse_help(void)
{
    char program[] = "svc";
    char help[] = "--help";
    char* argv[] = { program, help };

    TEST_REQUIRE(service_common::service_parse_options(2, argv, NULL, 0u) ==
        service_common::SERVICE_ARG_HELP);
    return ABE_TEST_STATUS_OK;
}

static int test_service_arg_status_uses_common_error_codes(void)
{
    TEST_REQUIRE((int)service_common::SERVICE_ARG_OK == (int)ABE_OK);
    TEST_REQUIRE(service_common::SERVICE_ARG_HELP == 1);
    TEST_REQUIRE((int)service_common::SERVICE_ARG_INVALID_ARG == (int)ABE_INVALID_ARG);
    TEST_REQUIRE((int)service_common::SERVICE_ARG_UNKNOWN_OPTION == (int)ABE_NOT_FOUND);
    TEST_REQUIRE((int)service_common::SERVICE_ARG_MISSING_VALUE == (int)ABE_PARSE_ERROR);
    TEST_REQUIRE((int)service_common::SERVICE_ARG_INVALID_VALUE == (int)ABE_BAD_VALUE);
    return ABE_TEST_STATUS_OK;
}

static int test_runtime_stop_flag(void)
{
    service_common::reset_stop();
    TEST_REQUIRE(service_common::stop_requested() == 0);
    service_common::request_stop();
    TEST_REQUIRE(service_common::stop_requested() == 1);
    service_common::reset_stop();
    TEST_REQUIRE(service_common::stop_requested() == 0);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_parse_numbers() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_parse_option_table() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_parse_help() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_service_arg_status_uses_common_error_codes() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_runtime_stop_flag() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
