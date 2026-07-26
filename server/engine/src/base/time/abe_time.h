#ifndef ABE_TIME_H
#define ABE_TIME_H

#include "abe_error.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABE_TIME_MIN_TIMEZONE_OFFSET_MINUTES (-14 * 60)
#define ABE_TIME_MAX_TIMEZONE_OFFSET_MINUTES (14 * 60)

typedef enum abe_time_status {
    ABE_TIME_OK = ABE_OK,
    ABE_TIME_ERROR = ABE_ERROR,
    ABE_TIME_INVALID_ARG = ABE_INVALID_ARG,
    ABE_TIME_SYSTEM_ERROR = ABE_SYSTEM_ERROR,
    ABE_TIME_OVERFLOW = ABE_OVERFLOW
} abe_time_status_t;

enum abe_time_weekday {
    ABE_TIME_SUNDAY = 0,
    ABE_TIME_MONDAY = 1,
    ABE_TIME_TUESDAY = 2,
    ABE_TIME_WEDNESDAY = 3,
    ABE_TIME_THURSDAY = 4,
    ABE_TIME_FRIDAY = 5,
    ABE_TIME_SATURDAY = 6
};

typedef struct abe_time_date {
    int year;
    int month;
    int day;
    int weekday;
} abe_time_date_t;

typedef struct abe_time_difference {
    uint64_t days;
    uint32_t hours;
    uint32_t minutes;
} abe_time_difference_t;

/*
 * Real time is Unix epoch time from the system wall clock. If the wall clock
 * moves backwards, the returned value is clamped to the last value returned by
 * this process.
 */
uint64_t abe_time_real_ms(void);
uint64_t abe_time_real_sec(void);

/*
 * Monotonic time is for timers and elapsed-time measurement. The value is not
 * Unix time and is never expected to move backwards.
 */
uint64_t abe_time_mono_ms(void);
uint64_t abe_time_mono_sec(void);

/*
 * Returns the current system local timezone as a fixed UTC offset in minutes.
 * For example, UTC+08:00 returns 480.
 */
int abe_time_get_timezone_offset_minutes(int* out_offset_minutes);

/*
 * timestamp_sec is Unix epoch seconds.
 * abe_time_get_date uses the system local timezone.
 */
int abe_time_get_date(uint64_t timestamp_sec, abe_time_date_t* out_date);

/*
 * Converts Unix epoch seconds using a fixed UTC offset from -14:00 to +14:00.
 */
int abe_time_get_date_with_offset(
    uint64_t timestamp_sec,
    int utc_offset_minutes,
    abe_time_date_t* out_date);

/*
 * Calculates end_ms - start_ms and returns complete days, hours and minutes.
 * A negative interval is rejected.
 */
int abe_time_diff_ms(
    uint64_t start_ms,
    uint64_t end_ms,
    abe_time_difference_t* out_difference);

#ifdef __cplusplus
}
#endif

#endif /* ABE_TIME_H */
