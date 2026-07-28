#include "abe_log.h"
#include "abe_time.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <memory>
#include <mutex>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#define ABE_LOG_MESSAGE_SIZE 2048u
#define ABE_LOG_OUTPUT_SIZE 2304u
#define ABE_LOG_NAME_SIZE 128u
#define ABE_LOG_PATH_SIZE 1024u

static std::mutex g_abe_log_mutex;
static std::shared_ptr<spdlog::logger> g_abe_log_logger;
static int g_abe_log_timezone_offset_minutes = 0;
static int g_abe_log_daily_enabled = 0;
static int g_abe_log_daily_day_key = -1;
static char g_abe_log_daily_name[ABE_LOG_NAME_SIZE];
static char g_abe_log_daily_root[ABE_LOG_PATH_SIZE];

struct abe_log_time_parts {
    abe_time_date_t date;
    int hour;
    int minute;
    int second;
    long milliseconds;
};

static int abe_log_copy_text(char* output, size_t output_size, const char* input)
{
    int written;

    if (output == NULL || output_size == 0u || input == NULL) {
        return -1;
    }

    written = snprintf(output, output_size, "%s", input);
    if (written < 0 || (size_t)written >= output_size) {
        output[0] = '\0';
        return -1;
    }
    return 0;
}

static int abe_log_copy_name(char* output, size_t output_size, const char* logger_name)
{
    const char* name;

    name = logger_name == NULL || logger_name[0] == '\0' ? "abe" : logger_name;
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        return -1;
    }
    return abe_log_copy_text(output, output_size, name);
}

static int abe_log_copy_root(char* output, size_t output_size, const char* root_directory)
{
    size_t length;

    if (root_directory == NULL || root_directory[0] == '\0') {
        return -1;
    }
    if (abe_log_copy_text(output, output_size, root_directory) != 0) {
        return -1;
    }

    length = strlen(output);
    while (length > 1u && output[length - 1u] == '/') {
        output[length - 1u] = '\0';
        length -= 1u;
    }
    return 0;
}

static int abe_log_timezone_is_valid(int utc_offset_minutes)
{
    return utc_offset_minutes >= ABE_TIME_MIN_TIMEZONE_OFFSET_MINUTES &&
           utc_offset_minutes <= ABE_TIME_MAX_TIMEZONE_OFFSET_MINUTES;
}

static int abe_log_get_time(int utc_offset_minutes, abe_log_time_parts* output_time)
{
    uint64_t current_ms;
    uint64_t current_seconds;
    int64_t base_seconds;
    int64_t offset_seconds;
    int64_t shifted_seconds;
    time_t shifted_time;
    struct tm shifted_tm;

    if (output_time == NULL || !abe_log_timezone_is_valid(utc_offset_minutes)) {
        return -1;
    }

    current_ms = abe_time_real_ms();
    current_seconds = current_ms / 1000u;
    if (abe_time_get_date_with_offset(
            current_seconds,
            utc_offset_minutes,
            &output_time->date) != ABE_TIME_OK) {
        return -1;
    }

    if (current_seconds > (uint64_t)INT64_MAX) {
        return -1;
    }
    base_seconds = (int64_t)current_seconds;
    offset_seconds = (int64_t)utc_offset_minutes * 60;
    if (offset_seconds > 0 && base_seconds > INT64_MAX - offset_seconds) {
        return -1;
    }
    if (offset_seconds < 0 && base_seconds < INT64_MIN - offset_seconds) {
        return -1;
    }
    shifted_seconds = base_seconds + offset_seconds;
    shifted_time = (time_t)shifted_seconds;
    if ((int64_t)shifted_time != shifted_seconds ||
        gmtime_r(&shifted_time, &shifted_tm) == NULL) {
        return -1;
    }

    output_time->hour = shifted_tm.tm_hour;
    output_time->minute = shifted_tm.tm_min;
    output_time->second = shifted_tm.tm_sec;
    output_time->milliseconds = (long)(current_ms % 1000u);
    return 0;
}

static int abe_log_make_directory(const char* path)
{
    struct stat path_status;

    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    if (errno != EEXIST) {
        return -1;
    }
    if (stat(path, &path_status) != 0 || !S_ISDIR(path_status.st_mode)) {
        return -1;
    }
    return 0;
}

static int abe_log_make_directories(const char* path)
{
    char directory[ABE_LOG_PATH_SIZE];
    char* cursor;

    if (abe_log_copy_text(directory, sizeof(directory), path) != 0) {
        return -1;
    }

    cursor = directory + 1;
    while (*cursor != '\0') {
        if (*cursor == '/') {
            *cursor = '\0';
            if (directory[0] != '\0' && abe_log_make_directory(directory) != 0) {
                return -1;
            }
            *cursor = '/';
        }
        cursor += 1;
    }
    return abe_log_make_directory(directory);
}

static spdlog::level::level_enum abe_log_to_spdlog_level(abe::log::level value)
{
    switch (value) {
    case abe::log::level_trace:
        return spdlog::level::trace;
    case abe::log::level_debug:
        return spdlog::level::debug;
    case abe::log::level_info:
        return spdlog::level::info;
    case abe::log::level_warn:
        return spdlog::level::warn;
    case abe::log::level_error:
        return spdlog::level::err;
    case abe::log::level_critical:
        return spdlog::level::critical;
    case abe::log::level_off:
    default:
        return spdlog::level::off;
    }
}

static abe::log::level abe_log_from_spdlog_level(spdlog::level::level_enum value)
{
    switch (value) {
    case spdlog::level::trace:
        return abe::log::level_trace;
    case spdlog::level::debug:
        return abe::log::level_debug;
    case spdlog::level::info:
        return abe::log::level_info;
    case spdlog::level::warn:
        return abe::log::level_warn;
    case spdlog::level::err:
        return abe::log::level_error;
    case spdlog::level::critical:
        return abe::log::level_critical;
    case spdlog::level::off:
    default:
        return abe::log::level_off;
    }
}

static void abe_log_configure_logger(
    spdlog::logger* logger,
    spdlog::level::level_enum log_level)
{
    if (logger == NULL) {
        return;
    }

    logger->set_level(log_level);
    logger->flush_on(log_level);
    logger->set_pattern("%v");
}

static void abe_log_drop_logger(const char* logger_name)
{
    if (logger_name == NULL) {
        return;
    }

    try {
        spdlog::drop(logger_name);
    } catch (...) {
    }
}

static void abe_log_release_logger_locked(void)
{
    std::shared_ptr<spdlog::logger> logger;

    logger = g_abe_log_logger;
    g_abe_log_logger.reset();
    if (!logger) {
        return;
    }

    try {
        logger->flush();
    } catch (...) {
    }
    abe_log_drop_logger(logger->name().c_str());
}

static void abe_log_disable_daily_locked(void)
{
    g_abe_log_daily_enabled = 0;
    g_abe_log_daily_day_key = -1;
    g_abe_log_daily_name[0] = '\0';
    g_abe_log_daily_root[0] = '\0';
}

static int abe_log_open_daily_locked(const abe_time_date_t* current_date)
{
    char daily_directory[ABE_LOG_PATH_SIZE];
    char file_path[ABE_LOG_PATH_SIZE];
    int day_key;
    int written;
    spdlog::level::level_enum current_level;

    if (current_date == NULL || !g_abe_log_daily_enabled) {
        return abe::log::status_initialize_failed;
    }

    day_key = current_date->year * 10000 +
              current_date->month * 100 +
              current_date->day;
    if (g_abe_log_daily_day_key == day_key && g_abe_log_logger) {
        return abe::log::status_ok;
    }

    written = snprintf(
        daily_directory,
        sizeof(daily_directory),
        "%s/%04d-%02d-%02d",
        g_abe_log_daily_root,
        current_date->year,
        current_date->month,
        current_date->day);
    if (written < 0 || (size_t)written >= sizeof(daily_directory)) {
        return abe::log::status_initialize_failed;
    }
    if (abe_log_make_directories(daily_directory) != 0) {
        return abe::log::status_initialize_failed;
    }

    written = snprintf(
        file_path,
        sizeof(file_path),
        "%s/%s.log",
        daily_directory,
        g_abe_log_daily_name);
    if (written < 0 || (size_t)written >= sizeof(file_path)) {
        return abe::log::status_initialize_failed;
    }

    current_level = g_abe_log_logger ? g_abe_log_logger->level() : spdlog::level::info;
    abe_log_release_logger_locked();

    try {
        abe_log_drop_logger(g_abe_log_daily_name);
        g_abe_log_logger =
            spdlog::basic_logger_mt(g_abe_log_daily_name, file_path, false);
        abe_log_configure_logger(g_abe_log_logger.get(), current_level);
        g_abe_log_daily_day_key = day_key;
        return abe::log::status_ok;
    } catch (...) {
        abe_log_drop_logger(g_abe_log_daily_name);
        g_abe_log_logger.reset();
        g_abe_log_daily_day_key = -1;
        return abe::log::status_initialize_failed;
    }
}

static int abe_log_format_timestamp(
    char* output,
    size_t output_size,
    const abe_log_time_parts* current_time,
    int utc_offset_minutes)
{
    int absolute_offset;
    int offset_hours;
    int offset_minutes;
    char offset_sign;
    int written;

    if (output == NULL || output_size == 0u || current_time == NULL) {
        return -1;
    }

    offset_sign = utc_offset_minutes < 0 ? '-' : '+';
    absolute_offset =
        utc_offset_minutes < 0 ? -utc_offset_minutes : utc_offset_minutes;
    offset_hours = absolute_offset / 60;
    offset_minutes = absolute_offset % 60;

    written = snprintf(
        output,
        output_size,
        "%04d-%02d-%02d %02d:%02d:%02d.%03ld UTC%c%02d:%02d",
        current_time->date.year,
        current_time->date.month,
        current_time->date.day,
        current_time->hour,
        current_time->minute,
        current_time->second,
        current_time->milliseconds,
        offset_sign,
        offset_hours,
        offset_minutes);
    if (written < 0 || (size_t)written >= output_size) {
        return -1;
    }
    return 0;
}

static const char* abe_log_file_name(const char* file)
{
    const char* name;
    const char* cursor;

    if (file == NULL) {
        return "";
    }

    name = file;
    cursor = file;
    while (*cursor != '\0') {
        if (*cursor == '/' || *cursor == '\\') {
            name = cursor + 1;
        }
        cursor += 1;
    }
    return name;
}

namespace abe {
namespace log {

int init_console(const char* logger_name)
{
    char name[ABE_LOG_NAME_SIZE];
    std::lock_guard<std::mutex> lock(g_abe_log_mutex);

    if (abe_log_copy_name(name, sizeof(name), logger_name) != 0) {
        return status_invalid_argument;
    }

    abe_log_disable_daily_locked();
    abe_log_release_logger_locked();

    try {
        abe_log_drop_logger(name);
        g_abe_log_logger = spdlog::stdout_color_mt(name);
        abe_log_configure_logger(g_abe_log_logger.get(), spdlog::level::info);
        return status_ok;
    } catch (...) {
        abe_log_drop_logger(name);
        g_abe_log_logger.reset();
        return status_initialize_failed;
    }
}

int init_file(const char* logger_name, const char* file_path, bool truncate_file)
{
    char name[ABE_LOG_NAME_SIZE];
    std::lock_guard<std::mutex> lock(g_abe_log_mutex);

    if (file_path == NULL || file_path[0] == '\0' ||
        abe_log_copy_name(name, sizeof(name), logger_name) != 0) {
        return status_invalid_argument;
    }

    abe_log_disable_daily_locked();
    abe_log_release_logger_locked();

    try {
        abe_log_drop_logger(name);
        g_abe_log_logger = spdlog::basic_logger_mt(name, file_path, truncate_file);
        abe_log_configure_logger(g_abe_log_logger.get(), spdlog::level::info);
        return status_ok;
    } catch (...) {
        abe_log_drop_logger(name);
        g_abe_log_logger.reset();
        return status_initialize_failed;
    }
}

int init_daily_file(
    const char* logger_name,
    const char* root_directory,
    int utc_offset_minutes)
{
    char name[ABE_LOG_NAME_SIZE];
    char root[ABE_LOG_PATH_SIZE];
    abe_log_time_parts current_time;
    std::lock_guard<std::mutex> lock(g_abe_log_mutex);

    if (!abe_log_timezone_is_valid(utc_offset_minutes) ||
        abe_log_copy_name(name, sizeof(name), logger_name) != 0 ||
        abe_log_copy_root(root, sizeof(root), root_directory) != 0) {
        return status_invalid_argument;
    }

    abe_log_release_logger_locked();
    g_abe_log_timezone_offset_minutes = utc_offset_minutes;
    g_abe_log_daily_enabled = 1;
    g_abe_log_daily_day_key = -1;
    (void)abe_log_copy_text(
        g_abe_log_daily_name,
        sizeof(g_abe_log_daily_name),
        name);
    (void)abe_log_copy_text(
        g_abe_log_daily_root,
        sizeof(g_abe_log_daily_root),
        root);

    if (abe_log_get_time(g_abe_log_timezone_offset_minutes, &current_time) != 0) {
        abe_log_disable_daily_locked();
        return status_initialize_failed;
    }
    return abe_log_open_daily_locked(&current_time.date);
}

void shutdown(void)
{
    std::lock_guard<std::mutex> lock(g_abe_log_mutex);

    abe_log_disable_daily_locked();
    abe_log_release_logger_locked();
}

int set_timezone_offset_minutes(int utc_offset_minutes)
{
    std::lock_guard<std::mutex> lock(g_abe_log_mutex);

    if (!abe_log_timezone_is_valid(utc_offset_minutes)) {
        return status_invalid_argument;
    }

    g_abe_log_timezone_offset_minutes = utc_offset_minutes;
    if (g_abe_log_daily_enabled) {
        g_abe_log_daily_day_key = -1;
    }
    return status_ok;
}

int get_timezone_offset_minutes(void)
{
    std::lock_guard<std::mutex> lock(g_abe_log_mutex);

    return g_abe_log_timezone_offset_minutes;
}

int set_level(level value)
{
    std::lock_guard<std::mutex> lock(g_abe_log_mutex);

    if (value < level_trace || value > level_off) {
        return status_invalid_argument;
    }
    if (!g_abe_log_logger) {
        return status_initialize_failed;
    }

    try {
        spdlog::level::level_enum log_level;

        log_level = abe_log_to_spdlog_level(value);
        g_abe_log_logger->set_level(log_level);
        g_abe_log_logger->flush_on(log_level);
        return status_ok;
    } catch (...) {
        return status_initialize_failed;
    }
}

level get_level(void)
{
    std::lock_guard<std::mutex> lock(g_abe_log_mutex);

    if (!g_abe_log_logger) {
        return level_off;
    }
    return abe_log_from_spdlog_level(g_abe_log_logger->level());
}

int is_enabled(level value)
{
    std::lock_guard<std::mutex> lock(g_abe_log_mutex);

    if (value < level_trace || value >= level_off || !g_abe_log_logger) {
        return 0;
    }
    return g_abe_log_logger->should_log(abe_log_to_spdlog_level(value)) ? 1 : 0;
}

const char* level_name(level value)
{
    switch (value) {
    case level_trace:
        return "trace";
    case level_debug:
        return "debug";
    case level_info:
        return "info";
    case level_warn:
        return "warn";
    case level_error:
        return "error";
    case level_critical:
        return "critical";
    case level_off:
        return "off";
    default:
        return "unknown";
    }
}

void flush(void)
{
    std::shared_ptr<spdlog::logger> logger;

    {
        std::lock_guard<std::mutex> lock(g_abe_log_mutex);
        logger = g_abe_log_logger;
    }
    if (!logger) {
        return;
    }

    try {
        logger->flush();
    } catch (...) {
    }
}

void write(
    level value,
    const char* file,
    int line,
    const char* function,
    const char* format,
    ...)
{
    char message[ABE_LOG_MESSAGE_SIZE];
    char timestamp[64];
    char output[ABE_LOG_OUTPUT_SIZE];
    abe_log_time_parts current_time;
    int timezone_offset_minutes;
    int written;
    va_list args;
    std::shared_ptr<spdlog::logger> logger;

    if (format == NULL || value < level_trace || value >= level_off) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_abe_log_mutex);

        timezone_offset_minutes = g_abe_log_timezone_offset_minutes;
        if (abe_log_get_time(timezone_offset_minutes, &current_time) != 0) {
            return;
        }
        if (g_abe_log_daily_enabled &&
            abe_log_open_daily_locked(&current_time.date) != status_ok) {
            return;
        }
        logger = g_abe_log_logger;
    }

    if (!logger || !logger->should_log(abe_log_to_spdlog_level(value))) {
        return;
    }

    va_start(args, format);
    (void)vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    message[sizeof(message) - 1u] = '\0';

    if (abe_log_format_timestamp(
            timestamp,
            sizeof(timestamp),
            &current_time,
            timezone_offset_minutes) != 0) {
        return;
    }

    written = snprintf(
        output,
        sizeof(output),
        "[%s][%s][%s] %s",
        timestamp,
        level_name(value),
        abe_log_file_name(file),
        message);
    if (written < 0) {
        return;
    }
    output[sizeof(output) - 1u] = '\0';

    try {
        logger->log(
            spdlog::source_loc(
                file == NULL ? "" : file,
                line,
                function == NULL ? "" : function),
            abe_log_to_spdlog_level(value),
            "{}",
            output);
    } catch (...) {
    }
}

} /* namespace log */
} /* namespace abe */
