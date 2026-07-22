#define _POSIX_C_SOURCE 200809L

#include "abe_time.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define ABE_TIME_MILLISECONDS_PER_SECOND 1000u
#define ABE_TIME_MINUTES_PER_HOUR 60u
#define ABE_TIME_HOURS_PER_DAY 24u
#define ABE_TIME_SECONDS_PER_MINUTE 60u

static pthread_mutex_t g_abe_time_current_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_abe_time_last_current_ms = 0u;

static int abe_time_read_clock_ms(clockid_t clock_id, uint64_t* out_ms)
{
    struct timespec current;
    uint64_t seconds;
    uint64_t milliseconds;

    if (out_ms == NULL) {
        return ABE_TIME_INVALID_ARG;
    }
    if (clock_gettime(clock_id, &current) != 0 ||
        current.tv_sec < 0 ||
        current.tv_nsec < 0) {
        return ABE_TIME_SYSTEM_ERROR;
    }

    seconds = (uint64_t)current.tv_sec;
    milliseconds = (uint64_t)current.tv_nsec / 1000000u;
    if (seconds > (UINT64_MAX - milliseconds) / ABE_TIME_MILLISECONDS_PER_SECOND) {
        return ABE_TIME_OVERFLOW;
    }

    *out_ms = seconds * ABE_TIME_MILLISECONDS_PER_SECOND + milliseconds;
    return ABE_TIME_OK;
}

static int abe_time_is_valid_offset(int utc_offset_minutes)
{
    return utc_offset_minutes >= ABE_TIME_MIN_TIMEZONE_OFFSET_MINUTES &&
        utc_offset_minutes <= ABE_TIME_MAX_TIMEZONE_OFFSET_MINUTES;
}

static int abe_time_seconds_to_time_t(uint64_t timestamp_sec, time_t* out_time)
{
    time_t converted;

    if (out_time == NULL) {
        return ABE_TIME_INVALID_ARG;
    }
    if (timestamp_sec > (uint64_t)INT64_MAX) {
        return ABE_TIME_OVERFLOW;
    }

    converted = (time_t)timestamp_sec;
    if ((uint64_t)converted != timestamp_sec) {
        return ABE_TIME_OVERFLOW;
    }

    *out_time = converted;
    return ABE_TIME_OK;
}

static int abe_time_seconds_to_time_t_with_offset(
    uint64_t timestamp_sec,
    int utc_offset_minutes,
    time_t* out_time)
{
    int64_t shifted_seconds;
    int64_t offset_seconds;
    time_t converted;

    if (out_time == NULL) {
        return ABE_TIME_INVALID_ARG;
    }
    if (!abe_time_is_valid_offset(utc_offset_minutes)) {
        return ABE_TIME_INVALID_ARG;
    }
    if (timestamp_sec > (uint64_t)INT64_MAX) {
        return ABE_TIME_OVERFLOW;
    }

    shifted_seconds = (int64_t)timestamp_sec;
    offset_seconds = (int64_t)utc_offset_minutes * 60;
    if (offset_seconds > 0 &&
        shifted_seconds > INT64_MAX - offset_seconds) {
        return ABE_TIME_OVERFLOW;
    }
    if (offset_seconds < 0 &&
        shifted_seconds < INT64_MIN - offset_seconds) {
        return ABE_TIME_OVERFLOW;
    }
    shifted_seconds += offset_seconds;

    converted = (time_t)shifted_seconds;
    if ((int64_t)converted != shifted_seconds) {
        return ABE_TIME_OVERFLOW;
    }

    *out_time = converted;
    return ABE_TIME_OK;
}

static void abe_time_copy_date(const struct tm* source, abe_time_date_t* out_date)
{
    out_date->year = source->tm_year + 1900;
    out_date->month = source->tm_mon + 1;
    out_date->day = source->tm_mday;
    out_date->weekday = source->tm_wday;
}

/*
 * Converts a civil date to a day number relative to 1970-01-01.
 * This avoids mktime and therefore does not mutate process-wide timezone state.
 */
static int64_t abe_time_days_from_civil(int year, int month, int day)
{
    int64_t adjusted_year;
    int64_t era;
    int64_t year_of_era;
    int64_t day_of_year;
    int64_t day_of_era;

    adjusted_year = (int64_t)year - (month <= 2 ? 1 : 0);
    era = adjusted_year >= 0 ?
        adjusted_year / 400 :
        (adjusted_year - 399) / 400;
    year_of_era = adjusted_year - era * 400;
    day_of_year =
        (153 * ((int64_t)month + (month > 2 ? -3 : 9)) + 2) / 5 +
        (int64_t)day - 1;
    day_of_era =
        year_of_era * 365 +
        year_of_era / 4 -
        year_of_era / 100 +
        day_of_year;
    return era * 146097 + day_of_era - 719468;
}

static int abe_time_get_offset_from_tm(
    const struct tm* local_time,
    const struct tm* utc_time,
    int* out_offset_minutes)
{
    int64_t local_days;
    int64_t utc_days;
    int64_t offset_seconds;

    if (local_time == NULL || utc_time == NULL || out_offset_minutes == NULL) {
        return ABE_TIME_INVALID_ARG;
    }

    local_days = abe_time_days_from_civil(
        local_time->tm_year + 1900,
        local_time->tm_mon + 1,
        local_time->tm_mday);
    utc_days = abe_time_days_from_civil(
        utc_time->tm_year + 1900,
        utc_time->tm_mon + 1,
        utc_time->tm_mday);
    offset_seconds =
        (local_days - utc_days) * 24 * 60 * 60 +
        ((int64_t)local_time->tm_hour - (int64_t)utc_time->tm_hour) * 60 * 60 +
        ((int64_t)local_time->tm_min - (int64_t)utc_time->tm_min) * 60 +
        (int64_t)local_time->tm_sec - (int64_t)utc_time->tm_sec;

    *out_offset_minutes = (int)(offset_seconds / 60);
    return ABE_TIME_OK;
}

uint64_t abe_time_mono_ms(void)
{
    uint64_t now_ms;

    if (abe_time_read_clock_ms(CLOCK_MONOTONIC, &now_ms) != ABE_TIME_OK) {
        return 0u;
    }
    return now_ms;
}

uint64_t abe_time_mono_sec(void)
{
    return abe_time_mono_ms() / ABE_TIME_MILLISECONDS_PER_SECOND;
}

uint64_t abe_time_real_ms(void)
{
    uint64_t current_ms;
    uint64_t result_ms;

    if (abe_time_read_clock_ms(CLOCK_REALTIME, &current_ms) != ABE_TIME_OK) {
        current_ms = 0u;
    }

    (void)pthread_mutex_lock(&g_abe_time_current_mutex);
    if (current_ms < g_abe_time_last_current_ms) {
        result_ms = g_abe_time_last_current_ms;
    } else {
        g_abe_time_last_current_ms = current_ms;
        result_ms = current_ms;
    }
    (void)pthread_mutex_unlock(&g_abe_time_current_mutex);
    return result_ms;
}

uint64_t abe_time_real_sec(void)
{
    return abe_time_real_ms() / ABE_TIME_MILLISECONDS_PER_SECOND;
}

int abe_time_get_timezone_offset_minutes(int* out_offset_minutes)
{
    time_t current_time;
    struct tm local_time;
    struct tm utc_time;
    int rc;

    if (out_offset_minutes == NULL) {
        return ABE_TIME_INVALID_ARG;
    }
    rc = abe_time_seconds_to_time_t(abe_time_real_sec(), &current_time);
    if (rc != ABE_TIME_OK) {
        return rc;
    }
    if (localtime_r(&current_time, &local_time) == NULL ||
        gmtime_r(&current_time, &utc_time) == NULL) {
        return ABE_TIME_SYSTEM_ERROR;
    }
    return abe_time_get_offset_from_tm(&local_time, &utc_time, out_offset_minutes);
}

int abe_time_get_date(uint64_t timestamp_sec, abe_time_date_t* out_date)
{
    time_t timestamp;
    struct tm local_time;
    int rc;

    if (out_date == NULL) {
        return ABE_TIME_INVALID_ARG;
    }
    rc = abe_time_seconds_to_time_t(timestamp_sec, &timestamp);
    if (rc != ABE_TIME_OK) {
        return rc;
    }
    if (localtime_r(&timestamp, &local_time) == NULL) {
        return ABE_TIME_SYSTEM_ERROR;
    }

    abe_time_copy_date(&local_time, out_date);
    return ABE_TIME_OK;
}

int abe_time_get_date_with_offset(
    uint64_t timestamp_sec,
    int utc_offset_minutes,
    abe_time_date_t* out_date)
{
    time_t shifted_timestamp;
    struct tm shifted_time;
    int rc;

    if (out_date == NULL) {
        return ABE_TIME_INVALID_ARG;
    }
    rc = abe_time_seconds_to_time_t_with_offset(
        timestamp_sec,
        utc_offset_minutes,
        &shifted_timestamp);
    if (rc != ABE_TIME_OK) {
        return rc;
    }
    if (gmtime_r(&shifted_timestamp, &shifted_time) == NULL) {
        return ABE_TIME_SYSTEM_ERROR;
    }

    abe_time_copy_date(&shifted_time, out_date);
    return ABE_TIME_OK;
}

int abe_time_diff_ms(
    uint64_t start_ms,
    uint64_t end_ms,
    abe_time_difference_t* out_difference)
{
    uint64_t difference_ms;
    uint64_t total_minutes;

    if (out_difference == NULL) {
        return ABE_TIME_INVALID_ARG;
    }
    memset(out_difference, 0, sizeof(*out_difference));
    if (end_ms < start_ms) {
        return ABE_TIME_INVALID_ARG;
    }

    difference_ms = end_ms - start_ms;
    total_minutes = difference_ms / (60u * 1000u);
    out_difference->days = total_minutes / (ABE_TIME_HOURS_PER_DAY * ABE_TIME_MINUTES_PER_HOUR);
    total_minutes %= ABE_TIME_HOURS_PER_DAY * ABE_TIME_MINUTES_PER_HOUR;
    out_difference->hours =
        (uint32_t)(total_minutes / ABE_TIME_MINUTES_PER_HOUR);
    out_difference->minutes =
        (uint32_t)(total_minutes % ABE_TIME_MINUTES_PER_HOUR);
    return ABE_TIME_OK;
}
