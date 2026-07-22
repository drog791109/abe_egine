#include "abe_time_wheel.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

struct timer_test_state {
    abe_timer_t* repeat_timer;
    int once_count;
    int repeat_count;
    int cancelled_count;
    int reschedule_count;
    int cascade_count;
    int failed_line;
    uint64_t once_time;
    uint64_t repeat_times[4];
    uint64_t cascade_times[8];
};

#define STATE_REQUIRE(state, expr) \
    do { \
        if (!(expr) && (state)->failed_line == 0) { \
            (state)->failed_line = __LINE__; \
        } \
    } while (0)

static void on_once(abe_timer_t* timer, uint64_t now_ms, void* user_data)
{
    struct timer_test_state* state;

    state = (struct timer_test_state*)user_data;
    state->once_count += 1;
    state->once_time = now_ms;
    STATE_REQUIRE(state, abe_timer_is_active(timer));
}

static void on_repeat(abe_timer_t* timer, uint64_t now_ms, void* user_data)
{
    struct timer_test_state* state;

    state = (struct timer_test_state*)user_data;
    if (state->repeat_count < 4) {
        state->repeat_times[state->repeat_count] = now_ms;
    }
    state->repeat_count += 1;
    if (state->repeat_count == 3) {
        STATE_REQUIRE(state, abe_timer_cancel(timer) == ABE_TIMER_OK);
    }
}

static void on_cancelled(abe_timer_t* timer, uint64_t now_ms, void* user_data)
{
    struct timer_test_state* state;

    (void)timer;
    (void)now_ms;
    state = (struct timer_test_state*)user_data;
    state->cancelled_count += 1;
}

static void on_rescheduled(abe_timer_t* timer, uint64_t now_ms, void* user_data)
{
    struct timer_test_state* state;

    (void)timer;
    (void)now_ms;
    state = (struct timer_test_state*)user_data;
    state->reschedule_count += 1;
}

static void on_cascade(abe_timer_t* timer, uint64_t now_ms, void* user_data)
{
    struct timer_test_state* state;

    (void)timer;
    state = (struct timer_test_state*)user_data;
    if (state->cascade_count < 8) {
        state->cascade_times[state->cascade_count] = now_ms;
    }
    state->cascade_count += 1;
}

static int make_wheel(abe_time_wheel_t** out_wheel)
{
    abe_time_wheel_config_t config;

    memset(&config, 0, sizeof(config));
    config.tick_ms = 10u;
    config.slot_count = 8u;
    config.max_timer_count = 16u;
    config.start_time_ms = 1000u;
    config.name = "unit_time_wheel";
    return abe_time_wheel_create(&config, out_wheel);
}

static int make_mono_wheel(abe_time_wheel_t** out_wheel)
{
    abe_time_wheel_config_t config;

    memset(&config, 0, sizeof(config));
    config.tick_ms = 5u;
    config.max_timer_count = 16u;
    config.memory_capacity = 32768u;
    config.name = "unit_mono_time_wheel";
    return abe_time_wheel_create_mono(&config, out_wheel);
}

static int test_once_and_repeat(void)
{
    abe_time_wheel_t* wheel;
    abe_timer_t* once_timer;
    struct timer_test_state state;
    abe_time_wheel_stats_t stats;
    uint32_t fired_count;

    memset(&state, 0, sizeof(state));
    wheel = NULL;
    TEST_REQUIRE(make_wheel(&wheel) == ABE_TIMER_OK);

    once_timer = NULL;
    TEST_REQUIRE(abe_time_wheel_schedule_once(wheel, 25u, on_once, &state, &once_timer) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(once_timer != NULL);
    TEST_REQUIRE(abe_time_wheel_schedule_repeat(
            wheel,
            0u,
            20u,
            on_repeat,
            &state,
            &state.repeat_timer) == ABE_TIMER_OK);
    TEST_REQUIRE(state.repeat_timer != NULL);
    TEST_REQUIRE(abe_timer_get_id(once_timer) != abe_timer_get_id(state.repeat_timer));

    fired_count = 99u;
    TEST_REQUIRE(abe_time_wheel_update(wheel, 1019u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 0u);
    TEST_REQUIRE(state.once_count == 0);
    TEST_REQUIRE(state.repeat_count == 0);

    TEST_REQUIRE(abe_time_wheel_update(wheel, 1020u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(state.failed_line == 0);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.repeat_count == 1);
    TEST_REQUIRE(state.repeat_times[0] == 1020u);

    TEST_REQUIRE(abe_time_wheel_update(wheel, 1030u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(state.failed_line == 0);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.once_count == 1);
    TEST_REQUIRE(state.once_time == 1030u);

    TEST_REQUIRE(abe_time_wheel_update(wheel, 1040u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(state.failed_line == 0);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.repeat_count == 2);
    TEST_REQUIRE(state.repeat_times[1] == 1040u);

    TEST_REQUIRE(abe_time_wheel_update(wheel, 1060u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(state.failed_line == 0);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.repeat_count == 3);
    TEST_REQUIRE(state.repeat_times[2] == 1060u);

    TEST_REQUIRE(abe_time_wheel_update(wheel, 1100u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(state.failed_line == 0);
    TEST_REQUIRE(fired_count == 0u);
    TEST_REQUIRE(state.repeat_count == 3);

    TEST_REQUIRE(abe_time_wheel_get_stats(wheel, &stats) == ABE_TIMER_OK);
    TEST_REQUIRE(stats.active_count == 0u);
    TEST_REQUIRE(stats.peak_active_count == 2u);
    TEST_REQUIRE(stats.time_source == ABE_TIME_WHEEL_SOURCE_MANUAL);
    TEST_REQUIRE(stats.current_time_ms == 1100u);

    abe_time_wheel_destroy(wheel);
    return 0;
}

static int test_cancel_and_reschedule(void)
{
    abe_time_wheel_t* wheel;
    abe_timer_t* cancelled_timer;
    abe_timer_t* rescheduled_timer;
    struct timer_test_state state;
    uint32_t fired_count;

    memset(&state, 0, sizeof(state));
    wheel = NULL;
    TEST_REQUIRE(make_wheel(&wheel) == ABE_TIMER_OK);

    cancelled_timer = NULL;
    TEST_REQUIRE(abe_time_wheel_schedule_once(
            wheel,
            20u,
            on_cancelled,
            &state,
            &cancelled_timer) == ABE_TIMER_OK);
    TEST_REQUIRE(abe_timer_cancel(cancelled_timer) == ABE_TIMER_OK);
    cancelled_timer = NULL;

    TEST_REQUIRE(abe_time_wheel_update(wheel, 1050u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 0u);
    TEST_REQUIRE(state.cancelled_count == 0);

    rescheduled_timer = NULL;
    TEST_REQUIRE(abe_time_wheel_schedule_once(
            wheel,
            100u,
            on_rescheduled,
            &state,
            &rescheduled_timer) == ABE_TIMER_OK);
    TEST_REQUIRE(abe_timer_reschedule(rescheduled_timer, 10u) == ABE_TIMER_OK);
    TEST_REQUIRE(abe_time_wheel_advance(wheel, 9u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 0u);
    TEST_REQUIRE(abe_time_wheel_advance(wheel, 1u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.reschedule_count == 1);

    abe_time_wheel_destroy(wheel);
    return 0;
}

static int test_limit_and_now(void)
{
    abe_time_wheel_config_t config;
    abe_time_wheel_t* wheel;
    abe_timer_t* first;
    abe_timer_t* second;
    abe_timer_t* third;
    struct timer_test_state state;
    abe_time_wheel_stats_t stats;
    uint64_t now_ms;

    memset(&config, 0, sizeof(config));
    config.tick_ms = 5u;
    config.slot_count = 4u;
    config.max_timer_count = 2u;
    config.memory_capacity = 16384u;

    memset(&state, 0, sizeof(state));
    wheel = NULL;
    TEST_REQUIRE(abe_time_wheel_create(&config, &wheel) == ABE_TIMER_OK);

    first = NULL;
    second = NULL;
    third = NULL;
    TEST_REQUIRE(abe_time_wheel_schedule_once(wheel, 10u, on_once, &state, &first) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(abe_time_wheel_schedule_once(wheel, 20u, on_once, &state, &second) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(abe_time_wheel_schedule_once(wheel, 30u, on_once, &state, &third) ==
        ABE_TIMER_LIMIT);
    TEST_REQUIRE(third == NULL);

    TEST_REQUIRE(abe_time_wheel_get_stats(wheel, &stats) == ABE_TIMER_OK);
    TEST_REQUIRE(stats.active_count == 2u);
    TEST_REQUIRE(stats.max_timer_count == 2u);
    TEST_REQUIRE(stats.time_source == ABE_TIME_WHEEL_SOURCE_MANUAL);
    TEST_REQUIRE(stats.tick_ms == 5u);
    TEST_REQUIRE(stats.slot_count == ABE_TIME_WHEEL_LINUX_SLOT_COUNT);

    TEST_REQUIRE(abe_timer_cancel(first) == ABE_TIMER_OK);
    first = NULL;
    TEST_REQUIRE(abe_time_wheel_schedule_once(wheel, 30u, on_once, &state, &third) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(third != NULL);
    TEST_REQUIRE(abe_timer_cancel(second) == ABE_TIMER_OK);
    TEST_REQUIRE(abe_timer_cancel(third) == ABE_TIMER_OK);

    now_ms = abe_time_mono_ms();
    TEST_REQUIRE(now_ms > 0u);

    abe_time_wheel_destroy(wheel);
    return 0;
}

static int test_linux_hierarchical_cascade(void)
{
    abe_time_wheel_config_t config;
    abe_time_wheel_t* wheel;
    abe_timer_t* timer_255;
    abe_timer_t* timer_256;
    abe_timer_t* timer_257;
    abe_timer_t* timer_16384;
    struct timer_test_state state;
    uint32_t fired_count;

    memset(&config, 0, sizeof(config));
    config.tick_ms = 1u;
    config.max_timer_count = 8u;
    config.start_time_ms = 0u;

    memset(&state, 0, sizeof(state));
    wheel = NULL;
    TEST_REQUIRE(abe_time_wheel_create(&config, &wheel) == ABE_TIMER_OK);

    timer_255 = NULL;
    timer_256 = NULL;
    timer_257 = NULL;
    timer_16384 = NULL;

    TEST_REQUIRE(abe_time_wheel_schedule_once(wheel, 255u, on_cascade, &state, &timer_255) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(abe_time_wheel_schedule_once(wheel, 256u, on_cascade, &state, &timer_256) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(abe_time_wheel_schedule_once(wheel, 257u, on_cascade, &state, &timer_257) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(abe_time_wheel_schedule_once(
            wheel,
            16384u,
            on_cascade,
            &state,
            &timer_16384) == ABE_TIMER_OK);

    TEST_REQUIRE(abe_time_wheel_update(wheel, 254u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 0u);
    TEST_REQUIRE(state.cascade_count == 0);

    TEST_REQUIRE(abe_time_wheel_update(wheel, 255u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.cascade_count == 1);
    TEST_REQUIRE(state.cascade_times[0] == 255u);

    TEST_REQUIRE(abe_time_wheel_update(wheel, 256u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.cascade_count == 2);
    TEST_REQUIRE(state.cascade_times[1] == 256u);

    TEST_REQUIRE(abe_time_wheel_update(wheel, 257u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.cascade_count == 3);
    TEST_REQUIRE(state.cascade_times[2] == 257u);

    TEST_REQUIRE(abe_time_wheel_update(wheel, 16383u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 0u);
    TEST_REQUIRE(state.cascade_count == 3);

    TEST_REQUIRE(abe_time_wheel_update(wheel, 16384u, &fired_count) == ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.cascade_count == 4);
    TEST_REQUIRE(state.cascade_times[3] == 16384u);

    abe_time_wheel_destroy(wheel);
    return 0;
}

static int test_mono_and_utc_schedule(void)
{
    abe_time_wheel_t* manual_wheel;
    abe_time_wheel_t* wheel;
    abe_timer_t* timer;
    abe_time_wheel_stats_t stats;
    struct timer_test_state state;
    uint32_t fired_count;
    uint64_t target_mono_ms;
    uint64_t update_time_ms;

    memset(&state, 0, sizeof(state));
    manual_wheel = NULL;
    TEST_REQUIRE(make_wheel(&manual_wheel) == ABE_TIMER_OK);
    TEST_REQUIRE(abe_time_wheel_update_mono(manual_wheel, &fired_count) ==
        ABE_TIMER_INVALID_ARG);
    TEST_REQUIRE(abe_time_wheel_schedule_once_at_mono_ms(
            manual_wheel,
            1010u,
            on_once,
            &state,
            &timer) == ABE_TIMER_INVALID_ARG);
    TEST_REQUIRE(abe_time_wheel_schedule_once_at_utc_ms(
            manual_wheel,
            abe_time_real_ms(),
            on_once,
            &state,
            &timer) == ABE_TIMER_INVALID_ARG);
    abe_time_wheel_destroy(manual_wheel);

    wheel = NULL;
    TEST_REQUIRE(make_mono_wheel(&wheel) == ABE_TIMER_OK);
    TEST_REQUIRE(abe_time_wheel_get_stats(wheel, &stats) == ABE_TIMER_OK);
    TEST_REQUIRE(stats.time_source == ABE_TIME_WHEEL_SOURCE_MONO);
    TEST_REQUIRE(stats.current_time_ms > 0u);

    timer = NULL;
    target_mono_ms = stats.current_time_ms + 20u;
    TEST_REQUIRE(abe_time_wheel_schedule_once_at_mono_ms(
            wheel,
            target_mono_ms,
            on_once,
            &state,
            &timer) == ABE_TIMER_OK);
    TEST_REQUIRE(timer != NULL);
    TEST_REQUIRE(abe_time_wheel_update(wheel, target_mono_ms - 1u, &fired_count) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 0u);
    TEST_REQUIRE(state.once_count == 0);
    TEST_REQUIRE(abe_time_wheel_update(wheel, target_mono_ms, &fired_count) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.once_count == 1);
    TEST_REQUIRE(state.once_time == target_mono_ms);

    memset(&state, 0, sizeof(state));
    TEST_REQUIRE(abe_time_wheel_get_stats(wheel, &stats) == ABE_TIMER_OK);
    timer = NULL;
    TEST_REQUIRE(abe_time_wheel_schedule_once(
            wheel,
            100u,
            on_once,
            &state,
            &timer) == ABE_TIMER_OK);
    TEST_REQUIRE(abe_timer_reschedule_at_mono_ms(
            timer,
            stats.current_time_ms + 10u) == ABE_TIMER_OK);
    TEST_REQUIRE(abe_time_wheel_update(wheel, stats.current_time_ms + 9u, &fired_count) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 0u);
    TEST_REQUIRE(abe_time_wheel_update(wheel, stats.current_time_ms + 10u, &fired_count) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.once_count == 1);

    memset(&state, 0, sizeof(state));
    TEST_REQUIRE(abe_time_wheel_get_stats(wheel, &stats) == ABE_TIMER_OK);
    timer = NULL;
    TEST_REQUIRE(abe_time_wheel_schedule_once_at_utc_ms(
            wheel,
            abe_time_real_ms(),
            on_once,
            &state,
            &timer) == ABE_TIMER_OK);
    TEST_REQUIRE(timer != NULL);
    update_time_ms = stats.current_time_ms + stats.tick_ms;
    TEST_REQUIRE(abe_time_wheel_update(wheel, update_time_ms, &fired_count) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.once_count == 1);

    memset(&state, 0, sizeof(state));
    TEST_REQUIRE(abe_time_wheel_get_stats(wheel, &stats) == ABE_TIMER_OK);
    timer = NULL;
    TEST_REQUIRE(abe_time_wheel_schedule_once(
            wheel,
            100u,
            on_once,
            &state,
            &timer) == ABE_TIMER_OK);
    TEST_REQUIRE(abe_timer_reschedule_at_utc_ms(timer, abe_time_real_ms()) ==
        ABE_TIMER_OK);
    update_time_ms = stats.current_time_ms + stats.tick_ms;
    TEST_REQUIRE(abe_time_wheel_update(wheel, update_time_ms, &fired_count) ==
        ABE_TIMER_OK);
    TEST_REQUIRE(fired_count == 1u);
    TEST_REQUIRE(state.once_count == 1);

    abe_time_wheel_destroy(wheel);
    return 0;
}

int main(void)
{
    if (test_once_and_repeat() != 0) {
        return 1;
    }
    if (test_cancel_and_reschedule() != 0) {
        return 1;
    }
    if (test_limit_and_now() != 0) {
        return 1;
    }
    if (test_linux_hierarchical_cascade() != 0) {
        return 1;
    }
    if (test_mono_and_utc_schedule() != 0) {
        return 1;
    }
    return 0;
}
