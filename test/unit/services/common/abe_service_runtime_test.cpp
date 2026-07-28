#include "abe_service_runtime.h"

#include "../../abe_test.h"

#include <signal.h>
#include <stdint.h>

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
          loop_seen(false),
          time_wheel_seen(false),
          config_seen(false),
          mysql_seen(false),
          id_seen(false),
          schedule_timer(false),
          wait_for_timer(false),
          timer_fired(false),
          timer(NULL),
          options_status(service_common::SERVICE_STATUS_OK),
          load_config_status(service_common::SERVICE_STATUS_OK),
          init_status(service_common::SERVICE_STATUS_OK),
          update_status(service_common::SERVICE_STATUS_OK)
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
        if (options_status != service_common::SERVICE_STATUS_OK) {
            return options_status;
        }
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
        config_seen = config != NULL;
        return load_config_status;
    }

    virtual int init(service_common::Context& context)
    {
        int rc;

        ++init_count;
        loop_seen = context.loop != NULL;
        time_wheel_seen = context.time_wheel != NULL;
        mysql_seen = context.mysql != NULL;
        id_seen = context.id_generator != NULL;
        if (init_status != service_common::SERVICE_STATUS_OK) {
            return init_status;
        }
        if (schedule_timer) {
            if (context.time_wheel == NULL) {
                return service_common::SERVICE_STATUS_INVALID_ARG;
            }
            rc = abe_time_wheel_schedule_once(
                context.time_wheel,
                0u,
                on_timer,
                this,
                &timer);
            if (rc != ABE_TIMER_OK) {
                return rc;
            }
        }
        return service_common::SERVICE_STATUS_OK;
    }

    virtual int update(uint64_t now_ms)
    {
        (void)now_ms;
        ++update_count;
        if (update_status != service_common::SERVICE_STATUS_OK) {
            return update_status;
        }
        if (wait_for_timer) {
            if (timer_fired) {
                service_common::request_stop();
                return service_common::SERVICE_STATUS_OK;
            }
            if (update_count > 32u) {
                return service_common::SERVICE_STATUS_FAILED;
            }
            return service_common::SERVICE_STATUS_OK;
        }
        service_common::request_stop();
        return service_common::SERVICE_STATUS_OK;
    }

    virtual void close(uint64_t now_ms)
    {
        (void)now_ms;
        ++close_count;
    }

    static void on_timer(abe_timer_t* timer, uint64_t now_ms, void* user_data)
    {
        TestService* service;

        (void)timer;
        (void)now_ms;
        service = (TestService*)user_data;
        if (service != NULL) {
            service->timer_fired = true;
        }
    }

    uint32_t defaults_count;
    uint32_t options_count;
    uint32_t load_config_count;
    uint32_t init_count;
    uint32_t update_count;
    uint32_t close_count;
    uint32_t test_value;
    bool loop_seen;
    bool time_wheel_seen;
    bool config_seen;
    bool mysql_seen;
    bool id_seen;
    bool schedule_timer;
    bool wait_for_timer;
    bool timer_fired;
    abe_timer_t* timer;
    int options_status;
    int load_config_status;
    int init_status;
    int update_status;
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

    TEST_REQUIRE(service_common::run(5, argv, service) == service_common::SERVICE_STATUS_OK);
    TEST_REQUIRE(service.defaults_count == 1u);
    TEST_REQUIRE(service.options_count == 1u);
    TEST_REQUIRE(service.load_config_count == 1u);
    TEST_REQUIRE(service.init_count == 1u);
    TEST_REQUIRE(service.update_count == 1u);
    TEST_REQUIRE(service.close_count == 1u);
    TEST_REQUIRE(service.test_value == 7u);
    TEST_REQUIRE(service.loop_seen);
    TEST_REQUIRE(service.time_wheel_seen);
    TEST_REQUIRE(!service.config_seen);
    TEST_REQUIRE(!service.mysql_seen);
    TEST_REQUIRE(service.id_seen);
    return ABE_TEST_STATUS_OK;
}

static int test_run_updates_time_wheel(void)
{
    TestService service;
    char program[] = "runtime_test";
    char log_level_name[] = "--log-level";
    char log_level_value[] = "off";
    char tick_name[] = "--tick-ms";
    char tick_value[] = "1";
    char timer_max_name[] = "--timer-max-count";
    char timer_max_value[] = "16";
    char* argv[] = {
        program,
        log_level_name,
        log_level_value,
        tick_name,
        tick_value,
        timer_max_name,
        timer_max_value
    };

    service.schedule_timer = true;
    service.wait_for_timer = true;
    TEST_REQUIRE(service_common::run(7, argv, service) == service_common::SERVICE_STATUS_OK);
    TEST_REQUIRE(service.time_wheel_seen);
    TEST_REQUIRE(service.timer_fired);
    TEST_REQUIRE(service.update_count > 0u);
    TEST_REQUIRE(service.close_count == 1u);
    return ABE_TEST_STATUS_OK;
}

static int test_run_returns_option_registration_error(void)
{
    TestService service;
    char program[] = "runtime_test";
    char* argv[] = { program };

    service.options_status = service_common::SERVICE_STATUS_NO_SLOT;
    TEST_REQUIRE(service_common::run(1, argv, service) == service_common::SERVICE_STATUS_NO_SLOT);
    TEST_REQUIRE(service.options_count == 1u);
    TEST_REQUIRE(service.load_config_count == 0u);
    TEST_REQUIRE(service.init_count == 0u);
    return ABE_TEST_STATUS_OK;
}

static int test_run_returns_argument_error(void)
{
    TestService service;
    char program[] = "runtime_test";
    char unknown[] = "--unknown";
    char* argv[] = { program, unknown };

    TEST_REQUIRE(service_common::run(2, argv, service) == service_common::SERVICE_ARG_UNKNOWN_OPTION);
    TEST_REQUIRE(service.load_config_count == 1u);
    TEST_REQUIRE(service.init_count == 0u);
    return ABE_TEST_STATUS_OK;
}

static int test_run_returns_load_config_error(void)
{
    TestService service;
    char program[] = "runtime_test";
    char* argv[] = { program };

    service.load_config_status = service_common::SERVICE_STATUS_INVALID_ARG;
    TEST_REQUIRE(service_common::run(1, argv, service) == service_common::SERVICE_STATUS_INVALID_ARG);
    TEST_REQUIRE(service.load_config_count == 1u);
    TEST_REQUIRE(service.init_count == 0u);
    return ABE_TEST_STATUS_OK;
}

static int test_run_returns_init_error(void)
{
    TestService service;
    char program[] = "runtime_test";
    char log_level_name[] = "--log-level";
    char log_level_value[] = "off";
    char* argv[] = { program, log_level_name, log_level_value };

    service.init_status = service_common::SERVICE_STATUS_FAILED;
    TEST_REQUIRE(service_common::run(3, argv, service) == service_common::SERVICE_STATUS_FAILED);
    TEST_REQUIRE(service.init_count == 1u);
    TEST_REQUIRE(service.update_count == 0u);
    TEST_REQUIRE(service.close_count == 0u);
    return ABE_TEST_STATUS_OK;
}

static int test_run_returns_update_error(void)
{
    TestService service;
    char program[] = "runtime_test";
    char log_level_name[] = "--log-level";
    char log_level_value[] = "off";
    char* argv[] = { program, log_level_name, log_level_value };

    service.update_status = service_common::SERVICE_STATUS_FAILED;
    TEST_REQUIRE(service_common::run(3, argv, service) == service_common::SERVICE_STATUS_FAILED);
    TEST_REQUIRE(service.init_count == 1u);
    TEST_REQUIRE(service.update_count == 1u);
    TEST_REQUIRE(service.close_count == 1u);
    return ABE_TEST_STATUS_OK;
}

static int test_stop_signal_handlers(void)
{
    service_common::reset_stop();
    service_common::install_stop_signal_handlers();

    TEST_REQUIRE(raise(SIGTERM) == 0);
    TEST_REQUIRE(service_common::stop_requested() == 1);
    service_common::reset_stop();

    TEST_REQUIRE(raise(SIGHUP) == 0);
    TEST_REQUIRE(service_common::stop_requested() == 1);
    service_common::reset_stop();

    TEST_REQUIRE(raise(SIGQUIT) == 0);
    TEST_REQUIRE(service_common::stop_requested() == 1);
    service_common::reset_stop();

    TEST_REQUIRE(raise(SIGPIPE) == 0);
    TEST_REQUIRE(service_common::stop_requested() == 0);
    return ABE_TEST_STATUS_OK;
}

static int test_service_status_uses_common_error_codes(void)
{
    TEST_REQUIRE((int)service_common::SERVICE_STATUS_OK == (int)ABE_OK);
    TEST_REQUIRE((int)service_common::SERVICE_STATUS_INVALID_ARG == (int)ABE_INVALID_ARG);
    TEST_REQUIRE((int)service_common::SERVICE_STATUS_NO_SLOT == (int)ABE_NO_SLOT);
    TEST_REQUIRE((int)service_common::SERVICE_STATUS_DUPLICATE == (int)ABE_ALREADY_EXISTS);
    TEST_REQUIRE((int)service_common::SERVICE_STATUS_FAILED == (int)ABE_ERROR);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_service_status_uses_common_error_codes() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_run_calls_service_directly() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_run_updates_time_wheel() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_run_returns_option_registration_error() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_run_returns_argument_error() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_run_returns_load_config_error() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_run_returns_init_error() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_run_returns_update_error() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_stop_signal_handlers() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
