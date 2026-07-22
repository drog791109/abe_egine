#include "abe_time.h"

#include <stdint.h>
#include <stdio.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

static int test_real_time_is_non_decreasing(void)
{
    uint64_t previous_ms;
    uint64_t current_ms;
    uint64_t current_sec;
    int index;

    previous_ms = abe_time_real_ms();
    for (index = 0; index < 1000; ++index) {
        current_ms = abe_time_real_ms();
        TEST_REQUIRE(current_ms >= previous_ms);
        previous_ms = current_ms;
    }

    current_ms = abe_time_real_ms();
    current_sec = abe_time_real_sec();
    TEST_REQUIRE(current_sec >= current_ms / 1000u);
    TEST_REQUIRE(current_sec <= current_ms / 1000u + 1u);
    return 0;
}

static int test_mono_time_is_non_decreasing(void)
{
    uint64_t previous_ms;
    uint64_t current_ms;
    uint64_t current_sec;
    int index;

    previous_ms = abe_time_mono_ms();
    for (index = 0; index < 1000; ++index) {
        current_ms = abe_time_mono_ms();
        TEST_REQUIRE(current_ms >= previous_ms);
        previous_ms = current_ms;
    }

    current_ms = abe_time_mono_ms();
    current_sec = abe_time_mono_sec();
    TEST_REQUIRE(current_sec >= current_ms / 1000u);
    TEST_REQUIRE(current_sec <= current_ms / 1000u + 1u);
    return 0;
}

static int test_date_and_timezone(void)
{
    abe_time_date_t date;
    int timezone_offset_minutes;

    TEST_REQUIRE(abe_time_get_timezone_offset_minutes(&timezone_offset_minutes) ==
        ABE_TIME_OK);
    TEST_REQUIRE(timezone_offset_minutes >= -24 * 60);
    TEST_REQUIRE(timezone_offset_minutes <= 24 * 60);

    TEST_REQUIRE(abe_time_get_date_with_offset(
            1704067200u,
            0,
            &date) == ABE_TIME_OK);
    TEST_REQUIRE(date.year == 2024);
    TEST_REQUIRE(date.month == 1);
    TEST_REQUIRE(date.day == 1);
    TEST_REQUIRE(date.weekday == ABE_TIME_MONDAY);

    TEST_REQUIRE(abe_time_get_date_with_offset(
            1704067200u,
            8 * 60,
            &date) == ABE_TIME_OK);
    TEST_REQUIRE(date.year == 2024);
    TEST_REQUIRE(date.month == 1);
    TEST_REQUIRE(date.day == 1);
    TEST_REQUIRE(date.weekday == ABE_TIME_MONDAY);

    TEST_REQUIRE(abe_time_get_date_with_offset(
            1704067200u,
            -8 * 60,
            &date) == ABE_TIME_OK);
    TEST_REQUIRE(date.year == 2023);
    TEST_REQUIRE(date.month == 12);
    TEST_REQUIRE(date.day == 31);
    TEST_REQUIRE(date.weekday == ABE_TIME_SUNDAY);

    TEST_REQUIRE(abe_time_get_date(abe_time_real_sec(), &date) == ABE_TIME_OK);
    TEST_REQUIRE(date.month >= 1 && date.month <= 12);
    TEST_REQUIRE(date.day >= 1 && date.day <= 31);
    TEST_REQUIRE(date.weekday >= ABE_TIME_SUNDAY);
    TEST_REQUIRE(date.weekday <= ABE_TIME_SATURDAY);
    return 0;
}

static int test_time_difference(void)
{
    abe_time_difference_t difference;
    uint64_t duration_ms;

    duration_ms =
        (((uint64_t)2 * 24u + 3u) * 60u + 45u) * 60u * 1000u +
        59u * 1000u;
    TEST_REQUIRE(abe_time_diff_ms(
            1000u,
            1000u + duration_ms,
            &difference) == ABE_TIME_OK);
    TEST_REQUIRE(difference.days == 2u);
    TEST_REQUIRE(difference.hours == 3u);
    TEST_REQUIRE(difference.minutes == 45u);

    TEST_REQUIRE(abe_time_diff_ms(
            2u,
            1u,
            &difference) == ABE_TIME_INVALID_ARG);
    TEST_REQUIRE(difference.days == 0u);
    TEST_REQUIRE(difference.hours == 0u);
    TEST_REQUIRE(difference.minutes == 0u);
    TEST_REQUIRE(abe_time_diff_ms(0u, 1u, NULL) == ABE_TIME_INVALID_ARG);
    return 0;
}

int main(void)
{
    if (test_real_time_is_non_decreasing() != 0) {
        return 1;
    }
    if (test_mono_time_is_non_decreasing() != 0) {
        return 1;
    }
    if (test_date_and_timezone() != 0) {
        return 1;
    }
    if (test_time_difference() != 0) {
        return 1;
    }
    return 0;
}
