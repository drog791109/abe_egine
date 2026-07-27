#include "abe_service_runtime.h"

#include "abe_log.h"
#include "abe_snowflake.h"
#include "abe_time.h"

#ifdef ABE_SERVICE_HAS_MYSQL
#include "abe_db_mysql_async.h"
#endif

#ifdef ABE_SERVICE_HAS_REDIS
#include "abe_redis_async.h"
#endif

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace abe {
namespace service {
namespace common {

enum {
    SERVICE_RUNTIME_DEFAULT_TICK_MS = 10u,
    SERVICE_RUNTIME_DEFAULT_MYSQL_PORT = 3306u,
    SERVICE_RUNTIME_DEFAULT_REDIS_PORT = 6379u
};

static volatile sig_atomic_t g_stop_requested = 0;

static void on_signal(int value)
{
    (void)value;
    request_stop();
}

Options::Options(ServiceOption* options, uint32_t max_options)
    : options_(options),
      max_(max_options),
      count_(0u)
{
    if (options_ != NULL && max_ != 0u) {
        memset(options_, 0, sizeof(ServiceOption) * max_);
    }
}

int Options::add_string(
    const char* name,
    const char* value_name,
    const char* description,
    const char** out_value)
{
    ServiceOption option;

    memset(&option, 0, sizeof(option));
    option.name = name;
    option.value_name = value_name;
    option.description = description;
    option.type = SERVICE_OPTION_STRING;
    option.out_value = out_value;
    return add(option);
}

int Options::add_u32(
    const char* name,
    const char* value_name,
    const char* description,
    uint32_t min_value,
    uint32_t max_value,
    uint32_t* out_value)
{
    ServiceOption option;

    memset(&option, 0, sizeof(option));
    option.name = name;
    option.value_name = value_name;
    option.description = description;
    option.type = SERVICE_OPTION_U32;
    option.out_value = out_value;
    option.min_value = min_value;
    option.max_value = max_value;
    return add(option);
}

int Options::add_u64(
    const char* name,
    const char* value_name,
    const char* description,
    uint64_t min_value,
    uint64_t max_value,
    uint64_t* out_value)
{
    ServiceOption option;

    memset(&option, 0, sizeof(option));
    option.name = name;
    option.value_name = value_name;
    option.description = description;
    option.type = SERVICE_OPTION_U64;
    option.out_value = out_value;
    option.min_value = min_value;
    option.max_value = max_value;
    return add(option);
}

int Options::add_i32(
    const char* name,
    const char* value_name,
    const char* description,
    int32_t min_value,
    int32_t max_value,
    int32_t* out_value)
{
    ServiceOption option;

    memset(&option, 0, sizeof(option));
    option.name = name;
    option.value_name = value_name;
    option.description = description;
    option.type = SERVICE_OPTION_I32;
    option.out_value = out_value;
    option.signed_min_value = min_value;
    option.signed_max_value = max_value;
    return add(option);
}

const ServiceOption* Options::data() const
{
    return options_;
}

uint32_t Options::count() const
{
    return count_;
}

int Options::add(const ServiceOption& option)
{
    if (options_ == NULL || option.name == NULL || option.out_value == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }
    if (exists(option.name)) {
        return SERVICE_STATUS_DUPLICATE;
    }
    if (count_ >= max_) {
        return SERVICE_STATUS_NO_SLOT;
    }

    options_[count_] = option;
    ++count_;
    return SERVICE_STATUS_OK;
}

int Options::exists(const char* name) const
{
    uint32_t index;

    if (options_ == NULL || name == NULL) {
        return 0;
    }

    index = 0u;
    while (index < count_) {
        if (options_[index].name != NULL && strcmp(options_[index].name, name) == 0) {
            return 1;
        }
        ++index;
    }
    return 0;
}

Service::~Service()
{
}

const char* Service::config_path() const
{
    return NULL;
}

void Service::defaults()
{
}

int Service::options(Options& options)
{
    (void)options;
    return SERVICE_STATUS_OK;
}

int Service::load_config(const abe_config_t* config)
{
    (void)config;
    return SERVICE_STATUS_OK;
}

int Service::update(uint64_t now_ms)
{
    (void)now_ms;
    return SERVICE_STATUS_OK;
}

void Service::close(uint64_t now_ms)
{
    (void)now_ms;
}

static int parse_log_level(
    const char* text,
    abe::log::level* out_level)
{
    if (out_level == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    if (text == NULL || strcmp(text, "info") == 0) {
        *out_level = abe::log::level_info;
        return SERVICE_STATUS_OK;
    }
    if (strcmp(text, "trace") == 0) {
        *out_level = abe::log::level_trace;
        return SERVICE_STATUS_OK;
    }
    if (strcmp(text, "debug") == 0) {
        *out_level = abe::log::level_debug;
        return SERVICE_STATUS_OK;
    }
    if (strcmp(text, "warn") == 0) {
        *out_level = abe::log::level_warn;
        return SERVICE_STATUS_OK;
    }
    if (strcmp(text, "error") == 0) {
        *out_level = abe::log::level_error;
        return SERVICE_STATUS_OK;
    }
    if (strcmp(text, "critical") == 0) {
        *out_level = abe::log::level_critical;
        return SERVICE_STATUS_OK;
    }
    if (strcmp(text, "off") == 0) {
        *out_level = abe::log::level_off;
        return SERVICE_STATUS_OK;
    }
    return SERVICE_STATUS_INVALID_ARG;
}

static void set_runtime_defaults(
    const char* service_name,
    RuntimeConfig* config)
{
    int timezone_offset;

    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->tick_ms = SERVICE_RUNTIME_DEFAULT_TICK_MS;
    config->log_output = "console";
    config->log_file = NULL;
    config->log_dir = "logs";
    config->log_level = "info";
    config->log_utc_offset_minutes = 0;
    if (abe_time_get_timezone_offset_minutes(&timezone_offset) == ABE_TIME_OK) {
        config->log_utc_offset_minutes = timezone_offset;
    }

    config->mysql_enable = 0u;
    config->mysql_host = "127.0.0.1";
    config->mysql_port = SERVICE_RUNTIME_DEFAULT_MYSQL_PORT;
    config->mysql_database = service_name == NULL ? "abe_engine" : service_name;
    config->mysql_user = "abe";
    config->mysql_password = "abe123";
    config->mysql_worker_count = 4u;
    config->mysql_queue_capacity = 4096u;

    config->redis_enable = 0u;
    config->redis_host = "127.0.0.1";
    config->redis_port = SERVICE_RUNTIME_DEFAULT_REDIS_PORT;
    config->redis_password = "";
    config->redis_database = 0;
    config->redis_connect_timeout_ms = 1000u;
    config->redis_command_timeout_ms = 1000u;
    config->redis_memory_pool_capacity = 0u;
    config->id_node_id = 0u;
}

static int add_common_options(
    RuntimeConfig* config,
    Options& options)
{
    int rc;

    if (config == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    rc = options.add_string("--config", "path", "json config file", &config->config_path);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_u32(
        "--tick-ms",
        "ms",
        "main loop sleep, default 10",
        0u,
        1000u,
        &config->tick_ms);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_string(
        "--log-output",
        "console|file|daily",
        "log output, default console",
        &config->log_output);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_string(
        "--log-file",
        "path",
        "file log path when log-output=file",
        &config->log_file);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_string(
        "--log-dir",
        "path",
        "daily log root directory, default logs",
        &config->log_dir);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_string(
        "--log-level",
        "level",
        "trace/debug/info/warn/error/critical/off",
        &config->log_level);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_i32(
        "--log-utc-offset-min",
        "minutes",
        "log timezone offset minutes",
        -840,
        840,
        &config->log_utc_offset_minutes);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_u32(
        "--mysql-enable",
        "0|1",
        "connect mysql during startup, default 0",
        0u,
        1u,
        &config->mysql_enable);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_string(
        "--mysql-host",
        "host",
        "mysql host, default 127.0.0.1",
        &config->mysql_host);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_u32(
        "--mysql-port",
        "port",
        "mysql port, default 3306",
        1u,
        65535u,
        &config->mysql_port);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_string(
        "--mysql-database",
        "name",
        "mysql database",
        &config->mysql_database);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_string("--mysql-user", "user", "mysql user", &config->mysql_user);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_string(
        "--mysql-password",
        "password",
        "mysql password",
        &config->mysql_password);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_u32(
        "--mysql-workers",
        "count",
        "mysql async worker connections, default 4",
        1u,
        32u,
        &config->mysql_worker_count);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_u32(
        "--mysql-queue-capacity",
        "count",
        "mysql async maximum outstanding requests, default 4096",
        1u,
        1048576u,
        &config->mysql_queue_capacity);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_u32(
        "--redis-enable",
        "0|1",
        "connect redis during startup, default 0",
        0u,
        1u,
        &config->redis_enable);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = options.add_string(
        "--redis-host",
        "host",
        "redis host, default 127.0.0.1",
        &config->redis_host);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = options.add_u32(
        "--redis-port",
        "port",
        "redis port, default 6379",
        1u,
        65535u,
        &config->redis_port);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = options.add_string(
        "--redis-password",
        "password",
        "redis password",
        &config->redis_password);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = options.add_i32(
        "--redis-database",
        "index",
        "redis database index, default 0",
        0,
        255,
        &config->redis_database);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = options.add_u32(
        "--redis-connect-timeout-ms",
        "ms",
        "redis connect timeout, default 1000",
        0u,
        60000u,
        &config->redis_connect_timeout_ms);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = options.add_u32(
        "--redis-command-timeout-ms",
        "ms",
        "redis command timeout, default 1000",
        0u,
        60000u,
        &config->redis_command_timeout_ms);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = options.add_u64(
        "--redis-memory-pool-capacity",
        "bytes",
        "redis memory pool capacity, default backend value",
        0u,
        0xffffffffffffffffull,
        &config->redis_memory_pool_capacity);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    return options.add_u32(
        "--id-node-id",
        "0-1023",
        "globally unique snowflake node id",
        0u,
        ABE_SNOWFLAKE_MAX_NODE_ID,
        &config->id_node_id);
}

static int load_config_file(
    const RuntimeConfig* runtime_config,
    abe_config_t** out_config)
{
    if (out_config == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }
    *out_config = NULL;

    if (runtime_config == NULL ||
        runtime_config->config_path == NULL ||
        runtime_config->config_path[0] == '\0') {
        return SERVICE_STATUS_OK;
    }
    if (abe_config_load_json_file(runtime_config->config_path, out_config) != ABE_CONFIG_OK) {
        fprintf(stderr, "service config load failed: %s\n", runtime_config->config_path);
        return SERVICE_STATUS_FAILED;
    }
    return SERVICE_STATUS_OK;
}

static int read_config_string(
    const abe_config_t* config,
    const char* path,
    const char** out_value)
{
    const char* text;
    int rc;

    if (config == NULL || path == NULL || out_value == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_string(config, path, &text);
    if (rc == ABE_CONFIG_NOT_FOUND) {
        return SERVICE_STATUS_OK;
    }
    if (rc != ABE_CONFIG_OK) {
        fprintf(stderr, "invalid string config value: %s\n", path);
        return SERVICE_STATUS_INVALID_ARG;
    }

    *out_value = text;
    return SERVICE_STATUS_OK;
}

static int read_config_u32(
    const abe_config_t* config,
    const char* path,
    uint32_t min_value,
    uint32_t max_value,
    uint32_t* out_value)
{
    uint64_t value;
    int rc;

    if (config == NULL || path == NULL || out_value == NULL || min_value > max_value) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, path, &value);
    if (rc == ABE_CONFIG_NOT_FOUND) {
        return SERVICE_STATUS_OK;
    }
    if (rc != ABE_CONFIG_OK || value < min_value || value > max_value) {
        fprintf(stderr, "invalid unsigned config value: %s\n", path);
        return SERVICE_STATUS_INVALID_ARG;
    }

    *out_value = (uint32_t)value;
    return SERVICE_STATUS_OK;
}

static int read_config_i32(
    const abe_config_t* config,
    const char* path,
    int32_t min_value,
    int32_t max_value,
    int32_t* out_value)
{
    int64_t value;
    int rc;

    if (config == NULL || path == NULL || out_value == NULL || min_value > max_value) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_i64(config, path, &value);
    if (rc == ABE_CONFIG_NOT_FOUND) {
        return SERVICE_STATUS_OK;
    }
    if (rc != ABE_CONFIG_OK || value < min_value || value > max_value) {
        fprintf(stderr, "invalid signed config value: %s\n", path);
        return SERVICE_STATUS_INVALID_ARG;
    }

    *out_value = (int32_t)value;
    return SERVICE_STATUS_OK;
}

static int read_config_u64(
    const abe_config_t* config,
    const char* path,
    uint64_t min_value,
    uint64_t max_value,
    uint64_t* out_value)
{
    uint64_t value;
    int rc;

    if (config == NULL || path == NULL || out_value == NULL || min_value > max_value) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    rc = abe_config_get_u64(config, path, &value);
    if (rc == ABE_CONFIG_NOT_FOUND) {
        return SERVICE_STATUS_OK;
    }
    if (rc != ABE_CONFIG_OK || value < min_value || value > max_value) {
        fprintf(stderr, "invalid unsigned config value: %s\n", path);
        return SERVICE_STATUS_INVALID_ARG;
    }

    *out_value = value;
    return SERVICE_STATUS_OK;
}

static int apply_runtime_config(
    RuntimeConfig* runtime_config,
    const abe_config_t* config)
{
    int rc;

    if (runtime_config == NULL || config == NULL) {
        return SERVICE_STATUS_OK;
    }

    rc = read_config_u32(
        config, "runtime.tick_ms", 0u, 1000u, &runtime_config->tick_ms);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_string(config, "log.output", &runtime_config->log_output);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_string(config, "log.file", &runtime_config->log_file);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_string(config, "log.dir", &runtime_config->log_dir);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_string(config, "log.level", &runtime_config->log_level);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_i32(
        config,
        "log.utc_offset_minutes",
        -840,
        840,
        &runtime_config->log_utc_offset_minutes);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_u32(
        config, "mysql.enable", 0u, 1u, &runtime_config->mysql_enable);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_string(config, "mysql.host", &runtime_config->mysql_host);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_u32(
        config, "mysql.port", 1u, 65535u, &runtime_config->mysql_port);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_string(
        config, "mysql.database", &runtime_config->mysql_database);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_string(config, "mysql.user", &runtime_config->mysql_user);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_string(config, "mysql.password", &runtime_config->mysql_password);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_u32(
        config,
        "mysql.worker_count",
        1u,
        32u,
        &runtime_config->mysql_worker_count);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_u32(
        config,
        "mysql.queue_capacity",
        1u,
        1048576u,
        &runtime_config->mysql_queue_capacity);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = read_config_u32(config, "redis.enable", 0u, 1u, &runtime_config->redis_enable);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_string(config, "redis.host", &runtime_config->redis_host);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_u32(config, "redis.port", 1u, 65535u, &runtime_config->redis_port);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_string(config, "redis.password", &runtime_config->redis_password);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_i32(config, "redis.database", 0, 255, &runtime_config->redis_database);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_u32(
        config,
        "redis.connect_timeout_ms",
        0u,
        60000u,
        &runtime_config->redis_connect_timeout_ms);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_u32(
        config,
        "redis.command_timeout_ms",
        0u,
        60000u,
        &runtime_config->redis_command_timeout_ms);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_u64(
        config,
        "redis.memory_pool_capacity",
        0u,
        0xffffffffffffffffull,
        &runtime_config->redis_memory_pool_capacity);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    return read_config_u32(
        config,
        "id.node_id",
        0u,
        ABE_SNOWFLAKE_MAX_NODE_ID,
        &runtime_config->id_node_id);
}

static int init_log(
    const char* service_name,
    const RuntimeConfig* config)
{
    const char* log_output;
    abe::log::level log_level;
    int rc;

    if (config == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    log_output = config->log_output == NULL ? "console" : config->log_output;
    if (strcmp(log_output, "console") == 0) {
        rc = abe::log::init_console(service_name);
    } else if (strcmp(log_output, "file") == 0) {
        if (config->log_file == NULL || config->log_file[0] == '\0') {
            fprintf(stderr, "service log file path is required when log-output=file\n");
            return SERVICE_STATUS_INVALID_ARG;
        }
        rc = abe::log::init_file(service_name, config->log_file, false);
    } else if (strcmp(log_output, "daily") == 0) {
        rc = abe::log::init_daily_file(
            service_name,
            config->log_dir == NULL ? "logs" : config->log_dir,
            config->log_utc_offset_minutes);
    } else {
        fprintf(stderr, "unknown log output: %s\n", log_output);
        return SERVICE_STATUS_INVALID_ARG;
    }

    if (rc != abe::log::status_ok) {
        fprintf(stderr, "service log init failed: %d\n", rc);
        return SERVICE_STATUS_FAILED;
    }

    rc = parse_log_level(config->log_level, &log_level);
    if (rc != SERVICE_STATUS_OK) {
        fprintf(stderr, "unknown log level: %s\n",
            config->log_level == NULL ? "" : config->log_level);
        return SERVICE_STATUS_INVALID_ARG;
    }
    rc = abe::log::set_level(log_level);
    if (rc != abe::log::status_ok) {
        fprintf(stderr, "service log level init failed: %d\n", rc);
        return SERVICE_STATUS_FAILED;
    }
    return SERVICE_STATUS_OK;
}

static int init_mysql(
    const RuntimeConfig* config,
    abe_db_mysql_async_t** out_mysql)
{
#ifdef ABE_SERVICE_HAS_MYSQL
    abe_db_mysql_async_config_t mysql_config;
    int rc;
#endif

    if (out_mysql == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }
    *out_mysql = NULL;

    if (config == NULL || config->mysql_enable == 0u) {
        return SERVICE_STATUS_OK;
    }

#ifndef ABE_SERVICE_HAS_MYSQL
    ABE_LOG_ERROR("mysql backend is not available in this build");
    return SERVICE_STATUS_FAILED;
#else
    memset(&mysql_config, 0, sizeof(mysql_config));
    mysql_config.mysql.host = config->mysql_host;
    mysql_config.mysql.port = (uint16_t)config->mysql_port;
    mysql_config.mysql.database = config->mysql_database;
    mysql_config.mysql.user = config->mysql_user;
    mysql_config.mysql.password = config->mysql_password;
    mysql_config.mysql.charset = "utf8mb4";
    mysql_config.mysql.connect_timeout_seconds = 5u;
    mysql_config.mysql.read_timeout_seconds = 5u;
    mysql_config.mysql.write_timeout_seconds = 5u;
    mysql_config.mysql.reconnect = 1;
    mysql_config.worker_count = config->mysql_worker_count;
    mysql_config.queue_capacity = config->mysql_queue_capacity;

    rc = abe_db_mysql_async_create(&mysql_config, out_mysql);
    if (rc != ABE_DB_OK) {
        ABE_LOG_ERROR("mysql async connect failed rc=%d host=%s port=%u database=%s",
            rc,
            config->mysql_host == NULL ? "" : config->mysql_host,
            config->mysql_port,
            config->mysql_database == NULL ? "" : config->mysql_database);
        return SERVICE_STATUS_FAILED;
    }

    ABE_LOG_INFO("mysql async connected host=%s port=%u database=%s workers=%u",
        config->mysql_host == NULL ? "" : config->mysql_host,
        config->mysql_port,
        config->mysql_database == NULL ? "" : config->mysql_database,
        config->mysql_worker_count);
    return SERVICE_STATUS_OK;
#endif
}

static int init_redis(
    const RuntimeConfig* config,
    abe_redis_async_t** out_redis)
{
#ifdef ABE_SERVICE_HAS_REDIS
    abe_redis_config_t redis_config;
    int rc;
#endif

    if (out_redis == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }
    *out_redis = NULL;

    if (config == NULL || config->redis_enable == 0u) {
        return SERVICE_STATUS_OK;
    }

#ifndef ABE_SERVICE_HAS_REDIS
    ABE_LOG_ERROR("redis backend is not available in this build");
    return SERVICE_STATUS_FAILED;
#else
    memset(&redis_config, 0, sizeof(redis_config));
    redis_config.host = config->redis_host;
    redis_config.port = (uint16_t)config->redis_port;
    redis_config.password = config->redis_password;
    redis_config.database = config->redis_database;
    redis_config.connect_timeout_ms = config->redis_connect_timeout_ms;
    redis_config.command_timeout_ms = config->redis_command_timeout_ms;
    redis_config.memory_pool_capacity = config->redis_memory_pool_capacity;

    rc = abe_redis_async_create(&redis_config, out_redis);
    if (rc != ABE_REDIS_OK) {
        ABE_LOG_ERROR("redis async connect failed rc=%d host=%s port=%u database=%d",
            rc,
            config->redis_host == NULL ? "" : config->redis_host,
            config->redis_port,
            config->redis_database);
        return SERVICE_STATUS_FAILED;
    }

    ABE_LOG_INFO("redis async connecting host=%s port=%u database=%d",
        config->redis_host == NULL ? "" : config->redis_host,
        config->redis_port,
        config->redis_database);
    return SERVICE_STATUS_OK;
#endif
}

static int run_loop(
    Context* context,
    Service& service,
    uint32_t tick_ms)
{
    int result;
    int rc;

    reset_stop();
    install_stop_signal_handlers();

    result = 0;
    while (!stop_requested()) {
        rc = context->loop->update();
        if (rc != ABE_NET_OK) {
            ABE_LOG_ERROR("service net update failed rc=%d", rc);
            result = 1;
            break;
        }

#ifdef ABE_SERVICE_HAS_MYSQL
        if (context->mysql != NULL) {
            rc = abe_db_mysql_async_update(context->mysql, 0u, NULL);
            if (rc != ABE_DB_OK) {
                ABE_LOG_ERROR("service mysql async update failed rc=%d", rc);
                result = 1;
                break;
            }
        }
#endif

#ifdef ABE_SERVICE_HAS_REDIS
        if (context->redis != NULL) {
            rc = abe_redis_async_update(context->redis);
            if (rc != ABE_REDIS_OK) {
                ABE_LOG_ERROR("service redis async update failed rc=%d error=%s",
                    rc,
                    abe_redis_async_last_error(context->redis));
                result = 1;
                break;
            }
        }
#endif

        rc = service.update(abe_time_mono_ms());
        if (rc != SERVICE_STATUS_OK) {
            ABE_LOG_ERROR("service update failed rc=%d", rc);
            result = 1;
            break;
        }

        if (tick_ms != 0u) {
            usleep((useconds_t)tick_ms * 1000u);
        }
    }
    return result;
}

static int parse_config_option(
    int argc,
    char** argv,
    RuntimeConfig* runtime_config)
{
    int index;

    if (argc < 0 || argv == NULL || runtime_config == NULL) {
        return SERVICE_ARG_INVALID_ARG;
    }

    index = 1;
    while (index < argc) {
        if (argv[index] == NULL) {
            return SERVICE_ARG_INVALID_ARG;
        }
        if (strcmp(argv[index], "--help") == 0) {
            return SERVICE_ARG_HELP;
        }
        if (strcmp(argv[index], "--config") == 0) {
            if (index + 1 >= argc || argv[index + 1] == NULL) {
                return SERVICE_ARG_MISSING_VALUE;
            }
            runtime_config->config_path = argv[index + 1];
            index += 2;
            continue;
        }
        index += 1;
    }
    return SERVICE_ARG_OK;
}

int run(int argc, char** argv, Service& service)
{
    RuntimeConfig runtime_config;
    Context context;
    ServiceOption options[SERVICE_RUNTIME_MAX_OPTIONS];
    Options option_list(options, SERVICE_RUNTIME_MAX_OPTIONS);
    abe_config_t* config;
    abe_db_mysql_async_t* mysql;
    abe_redis_async_t* redis;
    abe_snowflake_t* id_generator;
    abe::adapter::net::Loop loop;
    const char* service_name;
    int loop_ready;
    int service_ready;
    int rc;
    int result;

    service_name = service.name();
    if (service_name == NULL || service_name[0] == '\0') {
        service_name = "abe_service";
    }

    set_runtime_defaults(service_name, &runtime_config);
    runtime_config.config_path = service.config_path();

    service.defaults();

    rc = add_common_options(&runtime_config, option_list);
    if (rc == SERVICE_STATUS_OK) {
        rc = service.options(option_list);
    }
    if (rc != SERVICE_STATUS_OK) {
        fprintf(stderr, "service option registration failed: %d\n", rc);
        return 1;
    }

    rc = parse_config_option(argc, argv, &runtime_config);
    if (rc == SERVICE_ARG_HELP) {
        service_print_usage(argv == NULL ? NULL : argv[0], option_list.data(), option_list.count());
        return 0;
    }
    if (rc != SERVICE_ARG_OK) {
        service_print_usage(argv == NULL ? NULL : argv[0], option_list.data(), option_list.count());
        return 1;
    }

    config = NULL;
    rc = load_config_file(&runtime_config, &config);
    if (rc != SERVICE_STATUS_OK) {
        return 1;
    }
    rc = apply_runtime_config(&runtime_config, config);
    if (rc != SERVICE_STATUS_OK) {
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return 1;
    }
    rc = service.load_config(config);
    if (rc != SERVICE_STATUS_OK) {
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return 1;
    }
    rc = service_parse_options(argc, argv, option_list.data(), option_list.count());
    if (rc == SERVICE_ARG_HELP) {
        service_print_usage(argv == NULL ? NULL : argv[0], option_list.data(), option_list.count());
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return 0;
    }
    if (rc != SERVICE_ARG_OK) {
        service_print_usage(argv == NULL ? NULL : argv[0], option_list.data(), option_list.count());
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return 1;
    }

    rc = init_log(service_name, &runtime_config);
    if (rc != SERVICE_STATUS_OK) {
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return 1;
    }

    id_generator = NULL;
    rc = abe_snowflake_create((uint16_t)runtime_config.id_node_id, &id_generator);
    if (rc != ABE_SNOWFLAKE_OK) {
        ABE_LOG_ERROR("snowflake init failed rc=%d node_id=%u", rc, runtime_config.id_node_id);
        abe::log::shutdown();
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return 1;
    }

    mysql = NULL;
    rc = init_mysql(&runtime_config, &mysql);
    if (rc != SERVICE_STATUS_OK) {
        abe_snowflake_destroy(id_generator);
        abe::log::shutdown();
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return 1;
    }

    redis = NULL;
    rc = init_redis(&runtime_config, &redis);
    if (rc != SERVICE_STATUS_OK) {
#ifdef ABE_SERVICE_HAS_MYSQL
        if (mysql != NULL) {
            abe_db_mysql_async_destroy(mysql);
        }
#else
        (void)mysql;
#endif
        abe_snowflake_destroy(id_generator);
        abe::log::shutdown();
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return 1;
    }

    loop_ready = 0;
    service_ready = 0;
    memset(&context, 0, sizeof(context));
    rc = loop.create();
    if (rc != ABE_NET_OK) {
        ABE_LOG_ERROR("service loop create failed rc=%d", rc);
        result = 1;
    } else {
        loop_ready = 1;
        context.loop = &loop;
        context.config = config;
        context.mysql = mysql;
        context.redis = redis;
        context.id_generator = id_generator;
        context.runtime = &runtime_config;

        rc = service.init(context);
        if (rc != SERVICE_STATUS_OK) {
            ABE_LOG_ERROR("service init failed rc=%d", rc);
            result = 1;
        } else {
            service_ready = 1;
            ABE_LOG_INFO("service started name=%s", service_name);
            result = run_loop(&context, service, runtime_config.tick_ms);
        }
    }

    if (service_ready) {
        service.close(abe_time_mono_ms());
    }
    if (loop_ready) {
        loop.destroy();
    }
#ifdef ABE_SERVICE_HAS_REDIS
    if (redis != NULL) {
        abe_redis_async_destroy(redis);
    }
#else
    (void)redis;
#endif
#ifdef ABE_SERVICE_HAS_MYSQL
    if (mysql != NULL) {
        abe_db_mysql_async_destroy(mysql);
    }
#else
    (void)mysql;
#endif
    abe_snowflake_destroy(id_generator);
    if (config != NULL) {
        abe_config_destroy(config);
    }
    ABE_LOG_INFO("service stopped name=%s result=%d", service_name, result);
    abe::log::shutdown();
    return result;
}

void reset_stop()
{
    g_stop_requested = 0;
}

void request_stop()
{
    g_stop_requested = 1;
}

int stop_requested()
{
    return g_stop_requested != 0 ? 1 : 0;
}

void install_stop_signal_handlers()
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
}

} /* namespace common */
} /* namespace service */
} /* namespace abe */
