#include "abe_service_runtime.h"

#include <stdint.h>
#include <stdio.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

namespace service_common = abe::service::common;

class TestService : public service_common::Service {
public:
    TestService()
        : defaults_count(0u),
          options_count(0u),
          load_config_count(0u),
          init_count(0u),
          update_count(0u),
          close_count(0u),
          test_value(0u),
          loop_seen(0),
          config_seen(0),
          mysql_seen(0),
          id_seen(0)
    {
    }

    virtual const char* name() const
    {
        return "abe_service_runtime_test";
    }

    virtual void defaults()
    {
        ++defaults_count;
    }

    virtual int options(service_common::Options& options)
    {
        ++options_count;
        return options.add_u32(
            "--test-value",
            "value",
            "test value",
            0u,
            10u,
            &test_value);
    }

    virtual int load_config(const abe_config_t* config)
    {
        ++load_config_count;
        config_seen = config != NULL ? 1 : 0;
        return service_common::SERVICE_STATUS_OK;
    }

    virtual int init(service_common::Context& context)
    {
        ++init_count;
        loop_seen = context.loop != NULL ? 1 : 0;
        mysql_seen = context.mysql != NULL ? 1 : 0;
        id_seen = context.id_generator != NULL ? 1 : 0;
        return service_common::SERVICE_STATUS_OK;
    }

    virtual int update(uint64_t now_ms)
    {
        (void)now_ms;
        ++update_count;
        service_common::request_stop();
        return service_common::SERVICE_STATUS_OK;
    }

    virtual void close(uint64_t now_ms)
    {
        (void)now_ms;
        ++close_count;
    }

    uint32_t defaults_count;
    uint32_t options_count;
    uint32_t load_config_count;
    uint32_t init_count;
    uint32_t update_count;
    uint32_t close_count;
    uint32_t test_value;
    int loop_seen;
    int config_seen;
    int mysql_seen;
    int id_seen;
};

static int test_run_calls_service_directly(void)
{
    TestService service;
    char program[] = "runtime_test";
    char log_level_name[] = "--log-level";
    char log_level_value[] = "off";
    char test_name[] = "--test-value";
    char test_value[] = "7";
    char* argv[] = {
        program,
        log_level_name,
        log_level_value,
        test_name,
        test_value
    };

    TEST_REQUIRE(service_common::run(5, argv, service) == 0);
    TEST_REQUIRE(service.defaults_count == 1u);
    TEST_REQUIRE(service.options_count == 1u);
    TEST_REQUIRE(service.load_config_count == 1u);
    TEST_REQUIRE(service.init_count == 1u);
    TEST_REQUIRE(service.update_count == 1u);
    TEST_REQUIRE(service.close_count == 1u);
    TEST_REQUIRE(service.test_value == 7u);
    TEST_REQUIRE(service.loop_seen == 1);
    TEST_REQUIRE(service.config_seen == 0);
    TEST_REQUIRE(service.mysql_seen == 0);
    TEST_REQUIRE(service.id_seen == 1);
    return 0;
}

int main()
{
    if (test_run_calls_service_directly() != 0) {
        return 1;
    }
    return 0;
}
