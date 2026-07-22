#define _POSIX_C_SOURCE 200809L

#include "abe_time_wheel.h"
#include "abe_mem_pool.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ABE_TIME_WHEEL_DEFAULT_TICK_MS 10u
#define ABE_TIME_WHEEL_DEFAULT_MAX_TIMERS 4096u
#define ABE_TIME_WHEEL_DEFAULT_EXTRA_BYTES 4096u

#define ABE_TIME_TVR_BITS 8u
#define ABE_TIME_TVN_BITS 6u
#define ABE_TIME_TVR_SIZE (1u << ABE_TIME_TVR_BITS)
#define ABE_TIME_TVN_SIZE (1u << ABE_TIME_TVN_BITS)
#define ABE_TIME_TVR_MASK (ABE_TIME_TVR_SIZE - 1u)
#define ABE_TIME_TVN_MASK (ABE_TIME_TVN_SIZE - 1u)
#define ABE_TIME_LINUX_SLOT_COUNT (ABE_TIME_TVR_SIZE + (ABE_TIME_TVN_SIZE * 4u))

struct abe_time_bucket {
    abe_timer_t* head;
};

struct abe_time_wheel {
    char name[64];
    abe_mem_pool_t* mem_pool;
    struct abe_time_bucket tv1[ABE_TIME_TVR_SIZE];
    struct abe_time_bucket tv2[ABE_TIME_TVN_SIZE];
    struct abe_time_bucket tv3[ABE_TIME_TVN_SIZE];
    struct abe_time_bucket tv4[ABE_TIME_TVN_SIZE];
    struct abe_time_bucket tv5[ABE_TIME_TVN_SIZE];
    uint32_t time_source;
    uint32_t tick_ms;
    uint32_t max_timer_count;
    uint32_t active_count;
    uint32_t peak_active_count;
    uint64_t current_tick;
    uint64_t current_time_ms;
    uint64_t next_tick_time_ms;
    uint64_t next_id;
    uint64_t memory_capacity;
};

struct abe_timer {
    abe_timer_t* prev;
    abe_timer_t* next;
    abe_time_wheel_t* wheel;
    struct abe_time_bucket* bucket;
    abe_timer_id_t id;
    abe_timer_cb callback;
    void* user_data;
    uint64_t expire_tick;
    uint64_t interval_ticks;
    uint32_t repeat;
    uint32_t active;
    uint32_t firing;
    uint32_t cancelled;
};

static int abe_time_add_overflow_u64(uint64_t a, uint64_t b, uint64_t* out)
{
    if (out == NULL || b > ((uint64_t)-1) - a) {
        return 1;
    }
    *out = a + b;
    return 0;
}

static int abe_time_mul_overflow_u64(uint64_t a, uint64_t b, uint64_t* out)
{
    if (out == NULL) {
        return 1;
    }
    if (a != 0u && b > ((uint64_t)-1) / a) {
        return 1;
    }
    *out = a * b;
    return 0;
}

static int abe_time_wheel_source_is_valid(uint32_t time_source)
{
    return time_source == ABE_TIME_WHEEL_SOURCE_MANUAL ||
        time_source == ABE_TIME_WHEEL_SOURCE_MONO;
}

static uint64_t abe_time_duration_to_ticks(const abe_time_wheel_t* wheel, uint64_t duration_ms)
{
    uint64_t ticks;

    if (duration_ms == 0u) {
        return 1u;
    }

    ticks = duration_ms / (uint64_t)wheel->tick_ms;
    if ((duration_ms % (uint64_t)wheel->tick_ms) != 0u) {
        ++ticks;
    }
    return ticks == 0u ? 1u : ticks;
}

static int abe_time_delay_to_ticks(
    const abe_time_wheel_t* wheel,
    uint64_t delay_ms,
    uint64_t* out_ticks)
{
    uint64_t target_time_ms;
    uint64_t diff_ms;
    uint64_t ticks;

    if (wheel == NULL || out_ticks == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }
    if (delay_ms == 0u) {
        *out_ticks = 1u;
        return ABE_TIMER_OK;
    }
    if (abe_time_add_overflow_u64(wheel->current_time_ms, delay_ms, &target_time_ms) != 0) {
        return ABE_TIMER_INVALID_ARG;
    }
    if (target_time_ms <= wheel->next_tick_time_ms) {
        *out_ticks = 1u;
        return ABE_TIMER_OK;
    }

    diff_ms = target_time_ms - wheel->next_tick_time_ms;
    ticks = 1u + (diff_ms / (uint64_t)wheel->tick_ms);
    if ((diff_ms % (uint64_t)wheel->tick_ms) != 0u) {
        ++ticks;
    }

    *out_ticks = ticks;
    return ABE_TIMER_OK;
}

static uint64_t abe_time_default_memory_capacity(uint32_t max_timer_count)
{
    uint64_t timers_size;
    uint64_t capacity;

    if (abe_time_mul_overflow_u64(
            (uint64_t)max_timer_count,
            (uint64_t)sizeof(abe_timer_t) + 64u,
            &timers_size) != 0) {
        return 0u;
    }
    if (abe_time_add_overflow_u64((uint64_t)sizeof(abe_time_wheel_t), timers_size, &capacity) != 0) {
        return 0u;
    }
    if (abe_time_add_overflow_u64(capacity, ABE_TIME_WHEEL_DEFAULT_EXTRA_BYTES, &capacity) != 0) {
        return 0u;
    }
    return capacity;
}

static void abe_time_link_timer(
    abe_time_wheel_t* wheel,
    struct abe_time_bucket* bucket,
    abe_timer_t* timer,
    uint32_t count_active)
{
    timer->prev = NULL;
    timer->next = bucket->head;
    if (timer->next != NULL) {
        timer->next->prev = timer;
    }
    bucket->head = timer;
    timer->bucket = bucket;
    timer->active = 1u;
    timer->cancelled = 0u;

    if (count_active != 0u) {
        ++wheel->active_count;
        if (wheel->active_count > wheel->peak_active_count) {
            wheel->peak_active_count = wheel->active_count;
        }
    }
}

static void abe_time_place_timer(
    abe_time_wheel_t* wheel,
    abe_timer_t* timer,
    uint32_t count_active)
{
    uint64_t expires;
    uint64_t delta;
    struct abe_time_bucket* bucket;

    expires = timer->expire_tick;
    if (expires <= wheel->current_tick) {
        bucket = &wheel->tv1[wheel->current_tick & ABE_TIME_TVR_MASK];
        abe_time_link_timer(wheel, bucket, timer, count_active);
        return;
    }

    delta = expires - wheel->current_tick;
    if (delta < ABE_TIME_TVR_SIZE) {
        bucket = &wheel->tv1[expires & ABE_TIME_TVR_MASK];
    } else if (delta < (1ULL << (ABE_TIME_TVR_BITS + ABE_TIME_TVN_BITS))) {
        bucket = &wheel->tv2[(expires >> ABE_TIME_TVR_BITS) & ABE_TIME_TVN_MASK];
    } else if (delta < (1ULL << (ABE_TIME_TVR_BITS + (2u * ABE_TIME_TVN_BITS)))) {
        bucket = &wheel->tv3[
            (expires >> (ABE_TIME_TVR_BITS + ABE_TIME_TVN_BITS)) & ABE_TIME_TVN_MASK];
    } else if (delta < (1ULL << (ABE_TIME_TVR_BITS + (3u * ABE_TIME_TVN_BITS)))) {
        bucket = &wheel->tv4[
            (expires >> (ABE_TIME_TVR_BITS + (2u * ABE_TIME_TVN_BITS))) & ABE_TIME_TVN_MASK];
    } else {
        bucket = &wheel->tv5[
            (expires >> (ABE_TIME_TVR_BITS + (3u * ABE_TIME_TVN_BITS))) & ABE_TIME_TVN_MASK];
    }

    abe_time_link_timer(wheel, bucket, timer, count_active);
}

static void abe_time_unlink_timer(abe_timer_t* timer, uint32_t count_active)
{
    abe_time_wheel_t* wheel;

    if (timer == NULL || timer->wheel == NULL || timer->bucket == NULL || timer->active == 0u) {
        return;
    }

    wheel = timer->wheel;
    if (timer->prev != NULL) {
        timer->prev->next = timer->next;
    } else {
        timer->bucket->head = timer->next;
    }
    if (timer->next != NULL) {
        timer->next->prev = timer->prev;
    }

    timer->prev = NULL;
    timer->next = NULL;
    timer->bucket = NULL;
    timer->active = 0u;

    if (count_active != 0u && wheel->active_count > 0u) {
        --wheel->active_count;
    }
}

static void abe_time_release_timer(abe_timer_t* timer)
{
    abe_time_wheel_t* wheel;

    if (timer == NULL || timer->wheel == NULL) {
        return;
    }

    wheel = timer->wheel;
    (void)abe_mem_pool_free(wheel->mem_pool, timer);
}

static void abe_time_cascade_bucket(abe_time_wheel_t* wheel, struct abe_time_bucket* bucket)
{
    abe_timer_t* timer;

    timer = bucket->head;
    bucket->head = NULL;

    while (timer != NULL) {
        abe_timer_t* next;

        next = timer->next;
        timer->prev = NULL;
        timer->next = NULL;
        timer->bucket = NULL;
        abe_time_place_timer(wheel, timer, 0u);
        timer = next;
    }
}

static void abe_time_cascade(abe_time_wheel_t* wheel)
{
    uint32_t index;

    if ((wheel->current_tick & ABE_TIME_TVR_MASK) != 0u) {
        return;
    }

    index = (uint32_t)((wheel->current_tick >> ABE_TIME_TVR_BITS) & ABE_TIME_TVN_MASK);
    abe_time_cascade_bucket(wheel, &wheel->tv2[index]);
    if (index != 0u) {
        return;
    }

    index = (uint32_t)(
        (wheel->current_tick >> (ABE_TIME_TVR_BITS + ABE_TIME_TVN_BITS)) &
        ABE_TIME_TVN_MASK);
    abe_time_cascade_bucket(wheel, &wheel->tv3[index]);
    if (index != 0u) {
        return;
    }

    index = (uint32_t)(
        (wheel->current_tick >> (ABE_TIME_TVR_BITS + (2u * ABE_TIME_TVN_BITS))) &
        ABE_TIME_TVN_MASK);
    abe_time_cascade_bucket(wheel, &wheel->tv4[index]);
    if (index != 0u) {
        return;
    }

    index = (uint32_t)(
        (wheel->current_tick >> (ABE_TIME_TVR_BITS + (3u * ABE_TIME_TVN_BITS))) &
        ABE_TIME_TVN_MASK);
    abe_time_cascade_bucket(wheel, &wheel->tv5[index]);
}

static int abe_time_process_one_tick(abe_time_wheel_t* wheel, uint32_t* fired_count)
{
    struct abe_time_bucket* bucket;
    abe_timer_t* timer;
    uint64_t fire_time_ms;

    fire_time_ms = wheel->next_tick_time_ms;
    if (abe_time_add_overflow_u64(wheel->current_tick, 1u, &wheel->current_tick) != 0) {
        return ABE_TIMER_ERROR;
    }
    if (abe_time_add_overflow_u64(
            wheel->next_tick_time_ms,
            (uint64_t)wheel->tick_ms,
            &wheel->next_tick_time_ms) != 0) {
        return ABE_TIMER_ERROR;
    }
    wheel->current_time_ms = fire_time_ms;

    abe_time_cascade(wheel);

    bucket = &wheel->tv1[wheel->current_tick & ABE_TIME_TVR_MASK];
    timer = bucket->head;
    bucket->head = NULL;

    while (timer != NULL) {
        abe_timer_t* next;

        next = timer->next;
        timer->prev = NULL;
        timer->next = NULL;
        timer->bucket = NULL;
        timer->active = 0u;
        if (wheel->active_count > 0u) {
            --wheel->active_count;
        }

        timer->firing = 1u;
        if (timer->callback != NULL && timer->cancelled == 0u) {
            timer->callback(timer, fire_time_ms, timer->user_data);
            if (fired_count != NULL) {
                ++(*fired_count);
            }
        }
        timer->firing = 0u;

        if (timer->repeat != 0u && timer->cancelled == 0u) {
            if (abe_time_add_overflow_u64(
                    wheel->current_tick,
                    timer->interval_ticks,
                    &timer->expire_tick) != 0) {
                timer->cancelled = 1u;
                abe_time_release_timer(timer);
                return ABE_TIMER_ERROR;
            }
            abe_time_place_timer(wheel, timer, 1u);
        } else {
            abe_time_release_timer(timer);
        }

        timer = next;
    }

    return ABE_TIMER_OK;
}

static int abe_time_target_to_delay_ms(
    const abe_time_wheel_t* wheel,
    uint64_t target_ms,
    uint64_t* out_delay_ms)
{
    if (wheel == NULL || out_delay_ms == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }

    if (target_ms <= wheel->current_time_ms) {
        *out_delay_ms = 0u;
    } else {
        *out_delay_ms = target_ms - wheel->current_time_ms;
    }
    return ABE_TIMER_OK;
}

static int abe_time_utc_to_mono_target_ms(uint64_t target_utc_ms, uint64_t* out_target_mono_ms)
{
    uint64_t now_utc_ms;
    uint64_t now_mono_ms;
    uint64_t delay_ms;

    if (out_target_mono_ms == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }

    now_utc_ms = abe_time_real_ms();
    now_mono_ms = abe_time_mono_ms();
    if (target_utc_ms <= now_utc_ms) {
        *out_target_mono_ms = now_mono_ms;
        return ABE_TIMER_OK;
    }

    delay_ms = target_utc_ms - now_utc_ms;
    if (delay_ms > UINT64_MAX - now_mono_ms) {
        return ABE_TIMER_INVALID_ARG;
    }

    *out_target_mono_ms = now_mono_ms + delay_ms;
    return ABE_TIMER_OK;
}

static int abe_time_schedule_internal(
    abe_time_wheel_t* wheel,
    uint64_t delay_ms,
    uint64_t interval_ms,
    uint32_t repeat,
    abe_timer_cb callback,
    void* user_data,
    abe_timer_t** out_timer)
{
    abe_timer_t* timer;
    uint64_t delay_ticks;
    uint64_t interval_ticks;

    if (wheel == NULL || callback == NULL || out_timer == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }
    *out_timer = NULL;
    if (repeat != 0u && interval_ms == 0u) {
        return ABE_TIMER_INVALID_ARG;
    }
    if (wheel->active_count >= wheel->max_timer_count) {
        return ABE_TIMER_LIMIT;
    }

    if (repeat != 0u && delay_ms == 0u) {
        delay_ms = interval_ms;
    }
    if (abe_time_delay_to_ticks(wheel, delay_ms, &delay_ticks) != ABE_TIMER_OK) {
        return ABE_TIMER_INVALID_ARG;
    }
    interval_ticks = repeat != 0u ? abe_time_duration_to_ticks(wheel, interval_ms) : 0u;

    timer = (abe_timer_t*)abe_mem_pool_calloc(wheel->mem_pool, 1u, sizeof(*timer));
    if (timer == NULL) {
        return ABE_TIMER_NO_MEMORY;
    }

    timer->wheel = wheel;
    timer->id = wheel->next_id;
    ++wheel->next_id;
    if (wheel->next_id == 0u) {
        wheel->next_id = 1u;
    }
    timer->callback = callback;
    timer->user_data = user_data;
    timer->interval_ticks = interval_ticks;
    timer->repeat = repeat;
    if (abe_time_add_overflow_u64(wheel->current_tick, delay_ticks, &timer->expire_tick) != 0) {
        abe_time_release_timer(timer);
        return ABE_TIMER_INVALID_ARG;
    }

    abe_time_place_timer(wheel, timer, 1u);
    *out_timer = timer;
    return ABE_TIMER_OK;
}

int abe_time_wheel_create(
    const abe_time_wheel_config_t* config,
    abe_time_wheel_t** out_wheel)
{
    abe_mem_pool_t* mem_pool;
    abe_mem_pool_config_t mem_config;
    abe_time_wheel_t* wheel;
    uint32_t tick_ms;
    uint32_t max_timer_count;
    uint64_t memory_capacity;
    uint64_t start_time_ms;

    if (config == NULL || out_wheel == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }
    *out_wheel = NULL;

    tick_ms = config->tick_ms == 0u ? ABE_TIME_WHEEL_DEFAULT_TICK_MS : config->tick_ms;
    max_timer_count = config->max_timer_count == 0u ?
        ABE_TIME_WHEEL_DEFAULT_MAX_TIMERS :
        config->max_timer_count;
    if (max_timer_count == 0u ||
        !abe_time_wheel_source_is_valid(config->time_source)) {
        return ABE_TIMER_INVALID_ARG;
    }

    memory_capacity = config->memory_capacity;
    if (memory_capacity == 0u) {
        memory_capacity = abe_time_default_memory_capacity(max_timer_count);
        if (memory_capacity == 0u) {
            return ABE_TIMER_INVALID_ARG;
        }
    }

    memset(&mem_config, 0, sizeof(mem_config));
    mem_config.capacity = memory_capacity;
    mem_config.name = config->name == NULL ? "abe_time_wheel" : config->name;
    mem_pool = NULL;
    if (abe_mem_pool_create(&mem_config, &mem_pool) != ABE_MEM_POOL_OK) {
        return ABE_TIMER_NO_MEMORY;
    }

    wheel = (abe_time_wheel_t*)abe_mem_pool_calloc(mem_pool, 1u, sizeof(*wheel));
    if (wheel == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_TIMER_NO_MEMORY;
    }

    if (config->name != NULL) {
        strncpy(wheel->name, config->name, sizeof(wheel->name) - 1u);
    }
    wheel->mem_pool = mem_pool;
    wheel->time_source = config->time_source;
    wheel->tick_ms = tick_ms;
    wheel->max_timer_count = max_timer_count;
    start_time_ms = config->time_source == ABE_TIME_WHEEL_SOURCE_MONO ?
        abe_time_mono_ms() :
        config->start_time_ms;
    wheel->current_time_ms = start_time_ms;
    if (abe_time_add_overflow_u64(
            start_time_ms,
            (uint64_t)tick_ms,
            &wheel->next_tick_time_ms) != 0) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_TIMER_INVALID_ARG;
    }
    wheel->current_tick = 0u;
    wheel->next_id = 1u;
    wheel->memory_capacity = memory_capacity;

    *out_wheel = wheel;
    return ABE_TIMER_OK;
}

int abe_time_wheel_create_mono(
    const abe_time_wheel_config_t* config,
    abe_time_wheel_t** out_wheel)
{
    abe_time_wheel_config_t mono_config;

    if (config == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }

    mono_config = *config;
    mono_config.time_source = ABE_TIME_WHEEL_SOURCE_MONO;
    return abe_time_wheel_create(&mono_config, out_wheel);
}

void abe_time_wheel_destroy(abe_time_wheel_t* wheel)
{
    abe_mem_pool_t* mem_pool;

    if (wheel == NULL) {
        return;
    }

    mem_pool = wheel->mem_pool;
    abe_mem_pool_destroy(mem_pool);
}

int abe_time_wheel_update(
    abe_time_wheel_t* wheel,
    uint64_t now_ms,
    uint32_t* out_fired_count)
{
    uint64_t ticks_to_process;
    uint64_t index;
    int rc;

    if (wheel == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }
    if (out_fired_count != NULL) {
        *out_fired_count = 0u;
    }
    if (now_ms < wheel->current_time_ms) {
        return ABE_TIMER_INVALID_ARG;
    }

    if (now_ms < wheel->next_tick_time_ms) {
        wheel->current_time_ms = now_ms;
        return ABE_TIMER_OK;
    }

    ticks_to_process = 1u + ((now_ms - wheel->next_tick_time_ms) / (uint64_t)wheel->tick_ms);
    index = 0u;
    while (index < ticks_to_process) {
        rc = abe_time_process_one_tick(wheel, out_fired_count);
        if (rc != ABE_TIMER_OK) {
            return rc;
        }
        ++index;
    }
    wheel->current_time_ms = now_ms;

    return ABE_TIMER_OK;
}

int abe_time_wheel_update_mono(
    abe_time_wheel_t* wheel,
    uint32_t* out_fired_count)
{
    if (wheel == NULL || wheel->time_source != ABE_TIME_WHEEL_SOURCE_MONO) {
        return ABE_TIMER_INVALID_ARG;
    }

    return abe_time_wheel_update(wheel, abe_time_mono_ms(), out_fired_count);
}

int abe_time_wheel_advance(
    abe_time_wheel_t* wheel,
    uint64_t elapsed_ms,
    uint32_t* out_fired_count)
{
    uint64_t now_ms;

    if (wheel == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }
    if (abe_time_add_overflow_u64(wheel->current_time_ms, elapsed_ms, &now_ms) != 0) {
        return ABE_TIMER_INVALID_ARG;
    }
    return abe_time_wheel_update(wheel, now_ms, out_fired_count);
}

int abe_time_wheel_schedule_once(
    abe_time_wheel_t* wheel,
    uint64_t delay_ms,
    abe_timer_cb callback,
    void* user_data,
    abe_timer_t** out_timer)
{
    return abe_time_schedule_internal(
        wheel,
        delay_ms,
        0u,
        0u,
        callback,
        user_data,
        out_timer);
}

int abe_time_wheel_schedule_repeat(
    abe_time_wheel_t* wheel,
    uint64_t delay_ms,
    uint64_t interval_ms,
    abe_timer_cb callback,
    void* user_data,
    abe_timer_t** out_timer)
{
    return abe_time_schedule_internal(
        wheel,
        delay_ms,
        interval_ms,
        1u,
        callback,
        user_data,
        out_timer);
}

int abe_time_wheel_schedule_once_at_mono_ms(
    abe_time_wheel_t* wheel,
    uint64_t target_mono_ms,
    abe_timer_cb callback,
    void* user_data,
    abe_timer_t** out_timer)
{
    uint64_t delay_ms;
    int rc;

    if (wheel == NULL || wheel->time_source != ABE_TIME_WHEEL_SOURCE_MONO) {
        return ABE_TIMER_INVALID_ARG;
    }

    rc = abe_time_target_to_delay_ms(wheel, target_mono_ms, &delay_ms);
    if (rc != ABE_TIMER_OK) {
        return rc;
    }
    return abe_time_schedule_internal(
        wheel,
        delay_ms,
        0u,
        0u,
        callback,
        user_data,
        out_timer);
}

int abe_time_wheel_schedule_once_at_utc_ms(
    abe_time_wheel_t* wheel,
    uint64_t target_utc_ms,
    abe_timer_cb callback,
    void* user_data,
    abe_timer_t** out_timer)
{
    uint64_t target_mono_ms;
    int rc;

    if (wheel == NULL || wheel->time_source != ABE_TIME_WHEEL_SOURCE_MONO) {
        return ABE_TIMER_INVALID_ARG;
    }

    rc = abe_time_utc_to_mono_target_ms(target_utc_ms, &target_mono_ms);
    if (rc != ABE_TIMER_OK) {
        return rc;
    }
    return abe_time_wheel_schedule_once_at_mono_ms(
        wheel,
        target_mono_ms,
        callback,
        user_data,
        out_timer);
}

int abe_timer_cancel(abe_timer_t* timer)
{
    if (timer == NULL || timer->wheel == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }

    timer->cancelled = 1u;
    if (timer->firing != 0u) {
        return ABE_TIMER_OK;
    }
    if (timer->active == 0u) {
        return ABE_TIMER_NOT_FOUND;
    }

    abe_time_unlink_timer(timer, 1u);
    abe_time_release_timer(timer);
    return ABE_TIMER_OK;
}

int abe_timer_reschedule(abe_timer_t* timer, uint64_t delay_ms)
{
    abe_time_wheel_t* wheel;
    uint64_t delay_ticks;

    if (timer == NULL || timer->wheel == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }
    if (timer->firing != 0u) {
        return ABE_TIMER_INVALID_ARG;
    }
    if (timer->active == 0u) {
        return ABE_TIMER_NOT_FOUND;
    }

    wheel = timer->wheel;
    if (abe_time_delay_to_ticks(wheel, delay_ms, &delay_ticks) != ABE_TIMER_OK) {
        return ABE_TIMER_INVALID_ARG;
    }
    abe_time_unlink_timer(timer, 1u);
    if (abe_time_add_overflow_u64(wheel->current_tick, delay_ticks, &timer->expire_tick) != 0) {
        abe_time_release_timer(timer);
        return ABE_TIMER_INVALID_ARG;
    }
    abe_time_place_timer(wheel, timer, 1u);
    return ABE_TIMER_OK;
}

int abe_timer_reschedule_at_mono_ms(abe_timer_t* timer, uint64_t target_mono_ms)
{
    abe_time_wheel_t* wheel;
    uint64_t delay_ms;
    int rc;

    if (timer == NULL || timer->wheel == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }

    wheel = timer->wheel;
    if (wheel->time_source != ABE_TIME_WHEEL_SOURCE_MONO) {
        return ABE_TIMER_INVALID_ARG;
    }

    rc = abe_time_target_to_delay_ms(wheel, target_mono_ms, &delay_ms);
    if (rc != ABE_TIMER_OK) {
        return rc;
    }
    return abe_timer_reschedule(timer, delay_ms);
}

int abe_timer_reschedule_at_utc_ms(abe_timer_t* timer, uint64_t target_utc_ms)
{
    uint64_t target_mono_ms;
    int rc;

    if (timer == NULL || timer->wheel == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }
    if (timer->wheel->time_source != ABE_TIME_WHEEL_SOURCE_MONO) {
        return ABE_TIMER_INVALID_ARG;
    }

    rc = abe_time_utc_to_mono_target_ms(target_utc_ms, &target_mono_ms);
    if (rc != ABE_TIMER_OK) {
        return rc;
    }
    return abe_timer_reschedule_at_mono_ms(timer, target_mono_ms);
}

abe_timer_id_t abe_timer_get_id(const abe_timer_t* timer)
{
    return timer == NULL ? 0u : timer->id;
}

int abe_timer_is_active(const abe_timer_t* timer)
{
    if (timer == NULL || timer->cancelled != 0u) {
        return 0;
    }
    return timer->active != 0u || timer->firing != 0u;
}

void abe_timer_set_user_data(abe_timer_t* timer, void* user_data)
{
    if (timer != NULL) {
        timer->user_data = user_data;
    }
}

void* abe_timer_get_user_data(const abe_timer_t* timer)
{
    return timer == NULL ? NULL : timer->user_data;
}

int abe_time_wheel_get_stats(
    const abe_time_wheel_t* wheel,
    abe_time_wheel_stats_t* out_stats)
{
    if (wheel == NULL || out_stats == NULL) {
        return ABE_TIMER_INVALID_ARG;
    }

    memset(out_stats, 0, sizeof(*out_stats));
    out_stats->time_source = wheel->time_source;
    out_stats->tick_ms = wheel->tick_ms;
    out_stats->slot_count = ABE_TIME_LINUX_SLOT_COUNT;
    out_stats->active_count = wheel->active_count;
    out_stats->peak_active_count = wheel->peak_active_count;
    out_stats->max_timer_count = wheel->max_timer_count;
    out_stats->current_tick = wheel->current_tick;
    out_stats->current_time_ms = wheel->current_time_ms;
    out_stats->memory_capacity = wheel->memory_capacity;
    return ABE_TIMER_OK;
}
