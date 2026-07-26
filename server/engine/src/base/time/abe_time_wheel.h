#ifndef ABE_TIME_WHEEL_H
#define ABE_TIME_WHEEL_H

#include "abe_time.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abe_time_wheel abe_time_wheel_t;
typedef struct abe_timer abe_timer_t;
typedef uint64_t abe_timer_id_t;

#define ABE_TIME_WHEEL_LINUX_TV1_SIZE 256u
#define ABE_TIME_WHEEL_LINUX_TVN_SIZE 64u
#define ABE_TIME_WHEEL_LINUX_LEVELS 5u
#define ABE_TIME_WHEEL_LINUX_SLOT_COUNT \
    (ABE_TIME_WHEEL_LINUX_TV1_SIZE + \
     (ABE_TIME_WHEEL_LINUX_TVN_SIZE * (ABE_TIME_WHEEL_LINUX_LEVELS - 1u)))

#define ABE_TIME_WHEEL_SOURCE_MANUAL 0u
#define ABE_TIME_WHEEL_SOURCE_MONO 1u

typedef enum abe_timer_status {
    ABE_TIMER_OK = ABE_OK,
    ABE_TIMER_ERROR = ABE_ERROR,
    ABE_TIMER_INVALID_ARG = ABE_INVALID_ARG,
    ABE_TIMER_NO_MEMORY = ABE_NO_MEMORY,
    ABE_TIMER_NOT_FOUND = ABE_NOT_FOUND,
    ABE_TIMER_LIMIT = ABE_LIMIT
} abe_timer_status_t;

typedef void (*abe_timer_cb)(abe_timer_t* timer, uint64_t now_ms, void* user_data);

typedef struct abe_time_wheel_config {
    uint32_t tick_ms;
    /* Kept for API compatibility. Linux-style implementation uses fixed 512 slots. */
    uint32_t slot_count;
    uint32_t max_timer_count;
    uint64_t start_time_ms;
    uint64_t memory_capacity;
    const char* name;
    /*
     * MANUAL keeps start_time_ms as the wheel clock.
     * MONO uses abe_time_mono_ms() and ignores start_time_ms.
     */
    uint32_t time_source;
} abe_time_wheel_config_t;

typedef struct abe_time_wheel_stats {
    uint32_t tick_ms;
    uint32_t slot_count;
    uint32_t active_count;
    uint32_t peak_active_count;
    uint32_t max_timer_count;
    uint64_t current_tick;
    uint64_t current_time_ms;
    uint64_t memory_capacity;
    uint32_t time_source;
} abe_time_wheel_stats_t;

/*
 * Manual Linux-style hierarchical time wheel. Call update or advance from the service tick.
 *
 * delay_ms 0 fires on the next wheel tick. Repeating timers use interval_ms as
 * their period; if repeat delay is 0, the first fire is after interval_ms.
 *
 * Timer pointers are owned by the wheel. After cancel returns, or after a
 * one-shot callback returns, that timer pointer is invalid. During a callback,
 * cancelling the same timer is supported.
 */
int abe_time_wheel_create(
    const abe_time_wheel_config_t* config,
    abe_time_wheel_t** out_wheel);

int abe_time_wheel_create_mono(
    const abe_time_wheel_config_t* config,
    abe_time_wheel_t** out_wheel);

void abe_time_wheel_destroy(abe_time_wheel_t* wheel);

int abe_time_wheel_update(
    abe_time_wheel_t* wheel,
    uint64_t now_ms,
    uint32_t* out_fired_count);

int abe_time_wheel_update_mono(
    abe_time_wheel_t* wheel,
    uint32_t* out_fired_count);

int abe_time_wheel_advance(
    abe_time_wheel_t* wheel,
    uint64_t elapsed_ms,
    uint32_t* out_fired_count);

int abe_time_wheel_schedule_once(
    abe_time_wheel_t* wheel,
    uint64_t delay_ms,
    abe_timer_cb callback,
    void* user_data,
    abe_timer_t** out_timer);

int abe_time_wheel_schedule_repeat(
    abe_time_wheel_t* wheel,
    uint64_t delay_ms,
    uint64_t interval_ms,
    abe_timer_cb callback,
    void* user_data,
    abe_timer_t** out_timer);

/*
 * Schedules by absolute monotonic milliseconds returned from abe_time_mono_ms().
 * The wheel must use ABE_TIME_WHEEL_SOURCE_MONO.
 */
int abe_time_wheel_schedule_once_at_mono_ms(
    abe_time_wheel_t* wheel,
    uint64_t target_mono_ms,
    abe_timer_cb callback,
    void* user_data,
    abe_timer_t** out_timer);

/*
 * Schedules by absolute UTC Unix milliseconds returned from abe_time_real_ms().
 * The wheel must use ABE_TIME_WHEEL_SOURCE_MONO. UTC is converted to a monotonic
 * target when this function is called.
 */
int abe_time_wheel_schedule_once_at_utc_ms(
    abe_time_wheel_t* wheel,
    uint64_t target_utc_ms,
    abe_timer_cb callback,
    void* user_data,
    abe_timer_t** out_timer);

int abe_timer_cancel(abe_timer_t* timer);
int abe_timer_reschedule(abe_timer_t* timer, uint64_t delay_ms);
int abe_timer_reschedule_at_mono_ms(abe_timer_t* timer, uint64_t target_mono_ms);
int abe_timer_reschedule_at_utc_ms(abe_timer_t* timer, uint64_t target_utc_ms);
abe_timer_id_t abe_timer_get_id(const abe_timer_t* timer);
int abe_timer_is_active(const abe_timer_t* timer);
void abe_timer_set_user_data(abe_timer_t* timer, void* user_data);
void* abe_timer_get_user_data(const abe_timer_t* timer);

int abe_time_wheel_get_stats(
    const abe_time_wheel_t* wheel,
    abe_time_wheel_stats_t* out_stats);

#ifdef __cplusplus
}
#endif

#endif /* ABE_TIME_WHEEL_H */
