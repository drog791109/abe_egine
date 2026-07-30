#include "abe_service_runtime.h"

#include "../../abe_test.h"

#include <signal.h>
#include <stdint.h>
#include <string.h>

namespace service_common = abe::service::common;

class TestService : public service_common::Service {
public:
    TestService()
        : defaults_count(0u),
          load_config_count(0u),
          init_count(0u),
          update_count(0u),
          close_count(0u),
          loop_seen(false),
          time_wheel_seen(false),
          message_queue_seen(false),
          config_seen(false),
          mysql_seen(false),
          id_seen(false),
          schedule_timer(false),
          wait_for_timer(false),
          timer_fired(false),
          timer(NULL),
          schedule_messages(false),
          wait_for_messages(false),
          message_count(0u),
          target_message_count(0u),
          first_update_message_count(0u),
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
        message_queue_seen = context.message_queue != NULL;
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
        if (schedule_messages) {
            uint32_t index;

            if (context.message_queue == NULL || target_message_count == 0u) {
                return service_common::SERVICE_STATUS_INVALID_ARG;
            }
            index = 0u;
            while (index < target_message_count) {
                rc = context.message_queue->push(
                    this,
                    (uint64_t)(index + 1u),
                    NULL,
                    0u,
                    abe_time_mono_ms());
                if (rc != service_common::SERVICE_STATUS_OK) {
                    return rc;
                }
                ++index;
            }
        }
        return service_common::SERVICE_STATUS_OK;
    }

    virtual int process_message(const service_common::Message& message)
    {
        if (message.source != this) {
            return service_common::SERVICE_STATUS_INVALID_ARG;
        }
        ++message_count;
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
        if (wait_for_messages) {
            if (update_count == 1u) {
                first_update_message_count = message_count;
            }
            if (message_count >= target_message_count) {
                service_common::request_stop();
                return service_common::SERVICE_STATUS_OK;
            }
            if (update_count > 128u) {
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
    uint32_t load_config_count;
    uint32_t init_count;
    uint32_t update_count;
    uint32_t close_count;
    bool loop_seen;
    bool time_wheel_seen;
    bool message_queue_seen;
    bool config_seen;
    bool mysql_seen;
    bool id_seen;
    bool schedule_timer;
    bool wait_for_timer;
    bool timer_fired;
    abe_timer_t* timer;
    bool schedule_messages;
    bool wait_for_messages;
    uint32_t message_count;
    uint32_t target_message_count;
    uint32_t first_update_message_count;
    int load_config_status;
    int init_status;
    int update_status;
};

class QueueCounterService : public service_common::Service {
public:
    QueueCounterService()
        : count(0u),
          last_source_id(0u)
    {
    }

    virtual const char* name() const
    {
        return "queue_counter";
    }

    virtual int init(service_common::Context& context)
    {
        (void)context;
        return service_common::SERVICE_STATUS_OK;
    }

    virtual int process_message(const service_common::Message& message)
    {
        ++count;
        last_source_id = message.source_id;
        return service_common::SERVICE_STATUS_OK;
    }

    uint32_t count;
    uint64_t last_source_id;
};

static int test_message_queue_process_limit(void)
{
    service_common::MessageQueue queue;
    QueueCounterService counter;
    uint32_t processed_count;
    uint32_t failed_count;
    const char payload[] = "x";

    TEST_REQUIRE(queue.init(4u) == service_common::SERVICE_STATUS_OK);
    TEST_REQUIRE(queue.push(
        NULL,
        1u,
        payload,
        1u,
        100u) == service_common::SERVICE_STATUS_OK);
    TEST_REQUIRE(queue.push(
        NULL,
        2u,
        payload,
        1u,
        100u) == service_common::SERVICE_STATUS_OK);
    TEST_REQUIRE(queue.push(
        NULL,
        3u,
        payload,
        1u,
        100u) == service_common::SERVICE_STATUS_OK);

    processed_count = 0u;
    failed_count = 0u;
    TEST_REQUIRE(queue.process(counter, 2u, &processed_count, &failed_count) ==
        service_common::SERVICE_STATUS_OK);
    TEST_REQUIRE(processed_count == 2u);
    TEST_REQUIRE(failed_count == 0u);
    TEST_REQUIRE(counter.count == 2u);
    TEST_REQUIRE(counter.last_source_id == 2u);
    TEST_REQUIRE(queue.count() == 1u);

    TEST_REQUIRE(queue.process(counter, 2u, &processed_count, &failed_count) ==
        service_common::SERVICE_STATUS_OK);
    TEST_REQUIRE(processed_count == 1u);
    TEST_REQUIRE(failed_count == 0u);
    TEST_REQUIRE(counter.count == 3u);
    TEST_REQUIRE(counter.last_source_id == 3u);
    TEST_REQUIRE(queue.count() == 0u);
    queue.close();
    return ABE_TEST_STATUS_OK;
}

static int test_run_calls_service_directly(void)
{
    TestService service;

    TEST_REQUIRE(service_common::run(service) == service_common::SERVICE_STATUS_OK);
    TEST_REQUIRE(service.defaults_count == 1u);
    TEST_REQUIRE(service.load_config_count == 1u);
    TEST_REQUIRE(service.init_count == 1u);
    TEST_REQUIRE(service.update_count == 1u);
    TEST_REQUIRE(service.close_count == 1u);
    TEST_REQUIRE(service.loop_seen);
    TEST_REQUIRE(service.time_wheel_seen);
    TEST_REQUIRE(service.message_queue_seen);
    TEST_REQUIRE(!service.config_seen);
    TEST_REQUIRE(!service.mysql_seen);
    TEST_REQUIRE(service.id_seen);
    return ABE_TEST_STATUS_OK;
}

static int test_run_updates_time_wheel(void)
{
    TestService service;

    service.schedule_timer = true;
    service.wait_for_timer = true;
    TEST_REQUIRE(service_common::run(service) == service_common::SERVICE_STATUS_OK);
    TEST_REQUIRE(service.time_wheel_seen);
    TEST_REQUIRE(service.timer_fired);
    TEST_REQUIRE(service.update_count > 0u);
    TEST_REQUIRE(service.close_count == 1u);
    return ABE_TEST_STATUS_OK;
}

static int test_run_processes_message_queue_by_tick(void)
{
    TestService service;

    service.schedule_messages = true;
    service.wait_for_messages = true;
    service.target_message_count = 3u;
    TEST_REQUIRE(service_common::run(service) == service_common::SERVICE_STATUS_OK);
    TEST_REQUIRE(service.message_queue_seen);
    TEST_REQUIRE(service.message_count == 3u);
    TEST_REQUIRE(service.first_update_message_count == 3u);
    TEST_REQUIRE(service.close_count == 1u);
    return ABE_TEST_STATUS_OK;
}

static int test_run_returns_load_config_error(void)
{
    TestService service;

    service.load_config_status = service_common::SERVICE_STATUS_INVALID_ARG;
    TEST_REQUIRE(service_common::run(service) == service_common::SERVICE_STATUS_INVALID_ARG);
    TEST_REQUIRE(service.load_config_count == 1u);
    TEST_REQUIRE(service.init_count == 0u);
    return ABE_TEST_STATUS_OK;
}

static int test_run_returns_init_error(void)
{
    TestService service;

    service.init_status = service_common::SERVICE_STATUS_FAILED;
    TEST_REQUIRE(service_common::run(service) == service_common::SERVICE_STATUS_FAILED);
    TEST_REQUIRE(service.init_count == 1u);
    TEST_REQUIRE(service.update_count == 0u);
    TEST_REQUIRE(service.close_count == 0u);
    return ABE_TEST_STATUS_OK;
}

static int test_run_returns_update_error(void)
{
    TestService service;

    service.update_status = service_common::SERVICE_STATUS_FAILED;
    TEST_REQUIRE(service_common::run(service) == service_common::SERVICE_STATUS_FAILED);
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
    if (test_message_queue_process_limit() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_run_calls_service_directly() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_run_updates_time_wheel() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_run_processes_message_queue_by_tick() != ABE_TEST_STATUS_OK) {
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
