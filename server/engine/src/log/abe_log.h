#ifndef ABE_LOG_H
#define ABE_LOG_H

namespace abe {
namespace log {

enum level {
    level_trace = 0,
    level_debug = 1,
    level_info = 2,
    level_warn = 3,
    level_error = 4,
    level_critical = 5,
    level_off = 6
};

enum status {
    status_ok = 0,
    status_invalid_argument = -1,
    status_initialize_failed = -2
};

/* Call initialization and shutdown outside concurrent logging. */
/* String arguments are borrowed only for the duration of each call. */
int init_console(const char* logger_name);
int init_file(const char* logger_name, const char* file_path, bool truncate_file);
/*
 * Writes to <root_directory>/YYYY-MM-DD/<logger_name>.log.
 * The date is calculated with utc_offset_minutes and the file is appended.
 */
int init_daily_file(
    const char* logger_name,
    const char* root_directory,
    int utc_offset_minutes);
void shutdown(void);

/* The supported fixed UTC offset range is -14:00 through +14:00. */
int set_timezone_offset_minutes(int utc_offset_minutes);
int get_timezone_offset_minutes(void);

int set_level(level value);
level get_level(void);
int is_enabled(level value);
const char* level_name(level value);
void flush(void);

/* The formatted message uses a bounded internal buffer and may be truncated. */
void write(
    level value,
    const char* file,
    int line,
    const char* function,
    const char* format,
    ...);

} /* namespace log */
} /* namespace abe */

#define ABE_LOG_TRACE(...) \
    ::abe::log::write(::abe::log::level_trace, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ABE_LOG_DEBUG(...) \
    ::abe::log::write(::abe::log::level_debug, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ABE_LOG_INFO(...) \
    ::abe::log::write(::abe::log::level_info, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ABE_LOG_WARN(...) \
    ::abe::log::write(::abe::log::level_warn, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ABE_LOG_ERROR(...) \
    ::abe::log::write(::abe::log::level_error, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ABE_LOG_CRITICAL(...) \
    ::abe::log::write(::abe::log::level_critical, __FILE__, __LINE__, __func__, __VA_ARGS__)

#endif /* ABE_LOG_H */
