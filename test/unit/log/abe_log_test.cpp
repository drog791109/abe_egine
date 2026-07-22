#include "abe_log.h"
#include "abe_time.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

int main()
{
    char root_directory[256];
    char daily_directory[320];
    char log_path[384];
    char nested_directory[256];
    char test_directory[256];
    char line[512];
    char* last_slash;
    FILE* log_file;
    abe_time_date_t current_date;
    uint64_t current_seconds;
    int written;

    TEST_REQUIRE(abe::log::init_file("abe_engine_log_test", NULL, false) ==
                 abe::log::status_invalid_argument);
    TEST_REQUIRE(abe::log::init_file("abe_engine_log_test", "", false) ==
                 abe::log::status_invalid_argument);
    TEST_REQUIRE(abe::log::set_timezone_offset_minutes(15 * 60) ==
                 abe::log::status_invalid_argument);
    TEST_REQUIRE(abe::log::init_console("abe_engine_log_test") == abe::log::status_ok);
    TEST_REQUIRE(abe::log::set_level(abe::log::level_warn) == abe::log::status_ok);
    TEST_REQUIRE(abe::log::get_level() == abe::log::level_warn);
    TEST_REQUIRE(abe::log::is_enabled(abe::log::level_info) == 0);
    TEST_REQUIRE(abe::log::is_enabled(abe::log::level_error) != 0);
    TEST_REQUIRE(strcmp(abe::log::level_name(abe::log::level_warn), "warn") == 0);
    TEST_REQUIRE(abe::log::set_level(abe::log::level_trace) == abe::log::status_ok);

    ABE_LOG_INFO("engine log %d", 7);
    ABE_LOG_WARN("engine log warning");
    abe::log::flush();

    TEST_REQUIRE(abe::log::init_console("abe_engine_log_test") == abe::log::status_ok);
    abe::log::shutdown();

    written = snprintf(
        root_directory,
        sizeof(root_directory),
        "/tmp/abe_engine_log_test_%ld/nested/logs",
        (long)getpid());
    TEST_REQUIRE(written > 0 && (size_t)written < sizeof(root_directory));
    TEST_REQUIRE(abe::log::init_daily_file(
                     "abe_daily_test",
                     root_directory,
                     8 * 60) == abe::log::status_ok);
    TEST_REQUIRE(abe::log::get_timezone_offset_minutes() == 8 * 60);
    ABE_LOG_INFO("daily log message");
    abe::log::flush();

    current_seconds = abe_time_real_sec();
    TEST_REQUIRE(abe_time_get_date_with_offset(
            current_seconds,
            8 * 60,
            &current_date) == ABE_TIME_OK);
    written = snprintf(
        daily_directory,
        sizeof(daily_directory),
        "%s/%04d-%02d-%02d",
        root_directory,
        current_date.year,
        current_date.month,
        current_date.day);
    TEST_REQUIRE(written > 0 && (size_t)written < sizeof(daily_directory));
    written = snprintf(
        log_path,
        sizeof(log_path),
        "%s/abe_daily_test.log",
        daily_directory);
    TEST_REQUIRE(written > 0 && (size_t)written < sizeof(log_path));
    log_file = fopen(log_path, "r");
    TEST_REQUIRE(log_file != NULL);
    TEST_REQUIRE(fgets(line, sizeof(line), log_file) != NULL);
    TEST_REQUIRE(strstr(line, "UTC+08:00") != NULL);
    (void)fclose(log_file);

    TEST_REQUIRE(abe::log::set_timezone_offset_minutes(-5 * 60) ==
                 abe::log::status_ok);
    TEST_REQUIRE(abe::log::get_timezone_offset_minutes() == -5 * 60);
    abe::log::shutdown();

    written = snprintf(
        nested_directory,
        sizeof(nested_directory),
        "%s",
        root_directory);
    TEST_REQUIRE(written > 0 && (size_t)written < sizeof(nested_directory));
    last_slash = strrchr(nested_directory, '/');
    TEST_REQUIRE(last_slash != NULL);
    *last_slash = '\0';
    written = snprintf(
        test_directory,
        sizeof(test_directory),
        "%s",
        nested_directory);
    TEST_REQUIRE(written > 0 && (size_t)written < sizeof(test_directory));
    last_slash = strrchr(test_directory, '/');
    TEST_REQUIRE(last_slash != NULL);
    *last_slash = '\0';
    (void)remove(log_path);
    (void)rmdir(daily_directory);
    (void)rmdir(root_directory);
    (void)rmdir(nested_directory);
    (void)rmdir(test_directory);
    return 0;
}
