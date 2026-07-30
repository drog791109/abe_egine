#include "abe_service_runtime.h"

#include "abe_log.h"
#include "abe_snowflake.h"
#include "abe_time.h"

#include "abe_db_mysql_async.h"
#include "abe_redis_async.h"

#include <errno.h>
#include <new>
#include <signal.h>
#include <string.h>
#include <unistd.h>

namespace abe {
namespace service {
namespace common {

enum {
    SERVICE_RUNTIME_DEFAULT_TICK_MS = 10u,
    SERVICE_RUNTIME_DEFAULT_TIMER_MAX_COUNT = 65536u,
    SERVICE_RUNTIME_DEFAULT_MESSAGE_TICK_HZ = 30u,
    SERVICE_RUNTIME_DEFAULT_MESSAGE_MAX_PER_TICK = 500u,
    SERVICE_RUNTIME_DEFAULT_MESSAGE_QUEUE_CAPACITY = 65536u,
    SERVICE_RUNTIME_DEFAULT_MYSQL_PORT = 3306u,
    SERVICE_RUNTIME_DEFAULT_REDIS_PORT = 6379u,
    SERVICE_RUNTIME_SETTING_SMALL_TEXT_SIZE = 32u,
    SERVICE_RUNTIME_SETTING_NAME_SIZE = 128u,
    SERVICE_RUNTIME_SETTING_HOST_SIZE = 256u,
    SERVICE_RUNTIME_SETTING_SECRET_SIZE = 256u,
    SERVICE_RUNTIME_SETTING_PATH_SIZE = 512u
};

static void init_setting_text(char* out_text, size_t out_size, const char* value)
{
    size_t value_size;

    if (out_text == NULL || out_size == 0u) {
        return;
    }

    value = value == NULL ? "" : value;
    value_size = strlen(value);
    if (value_size >= out_size) {
        value_size = out_size - 1u;
    }
    memcpy(out_text, value, value_size);
    out_text[value_size] = '\0';
}

static int copy_setting_text(
    const char* path,
    const char* value,
    char* out_text,
    size_t out_size)
{
    size_t value_size;

    if (path == NULL || out_text == NULL || out_size == 0u) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    value = value == NULL ? "" : value;
    value_size = strlen(value);
    if (value_size >= out_size) {
        ABE_LOG_ERROR(
            "config string value is too long path=%s max_size=%u status=%s",
            path,
            (unsigned int)(out_size - 1u),
            abe_status_name(SERVICE_STATUS_INVALID_ARG));
        return SERVICE_STATUS_INVALID_ARG;
    }

    memcpy(out_text, value, value_size + 1u);
    return SERVICE_STATUS_OK;
}

struct RuntimeSettings {
    uint32_t tick_ms = SERVICE_RUNTIME_DEFAULT_TICK_MS;
    uint32_t timer_max_count = SERVICE_RUNTIME_DEFAULT_TIMER_MAX_COUNT;
    uint32_t message_tick_hz = SERVICE_RUNTIME_DEFAULT_MESSAGE_TICK_HZ;
    uint32_t message_max_per_tick = SERVICE_RUNTIME_DEFAULT_MESSAGE_MAX_PER_TICK;
    uint32_t message_queue_capacity = SERVICE_RUNTIME_DEFAULT_MESSAGE_QUEUE_CAPACITY;
};

struct LogSettings {
    LogSettings()
        : utc_offset_minutes(0)
    {
        init_setting_text(output, sizeof(output), "console");
        init_setting_text(file, sizeof(file), "");
        init_setting_text(dir, sizeof(dir), "logs");
        init_setting_text(level, sizeof(level), "info");
    }

    char output[SERVICE_RUNTIME_SETTING_SMALL_TEXT_SIZE];
    char file[SERVICE_RUNTIME_SETTING_PATH_SIZE];
    char dir[SERVICE_RUNTIME_SETTING_PATH_SIZE];
    char level[SERVICE_RUNTIME_SETTING_SMALL_TEXT_SIZE];
    int32_t utc_offset_minutes;
};

struct MysqlSettings {
    MysqlSettings()
        : port(SERVICE_RUNTIME_DEFAULT_MYSQL_PORT),
          worker_count(4u),
          queue_capacity(4096u)
    {
        init_setting_text(host, sizeof(host), "127.0.0.1");
        init_setting_text(database, sizeof(database), "abe_engine");
        init_setting_text(user, sizeof(user), "abe");
        init_setting_text(password, sizeof(password), "abe123");
    }

    char host[SERVICE_RUNTIME_SETTING_HOST_SIZE];
    uint32_t port;
    char database[SERVICE_RUNTIME_SETTING_NAME_SIZE];
    char user[SERVICE_RUNTIME_SETTING_NAME_SIZE];
    char password[SERVICE_RUNTIME_SETTING_SECRET_SIZE];
    uint32_t worker_count;
    uint32_t queue_capacity;
};

struct RedisSettings {
    RedisSettings()
        : port(SERVICE_RUNTIME_DEFAULT_REDIS_PORT),
          database(0),
          connect_timeout_ms(1000u),
          command_timeout_ms(1000u),
          memory_pool_capacity(0u)
    {
        init_setting_text(host, sizeof(host), "127.0.0.1");
        init_setting_text(password, sizeof(password), "");
    }

    char host[SERVICE_RUNTIME_SETTING_HOST_SIZE];
    uint32_t port;
    char password[SERVICE_RUNTIME_SETTING_SECRET_SIZE];
    int32_t database;
    uint32_t connect_timeout_ms;
    uint32_t command_timeout_ms;
    uint64_t memory_pool_capacity;
};

struct IdSettings {
    uint32_t node_id = 0u;
};

struct ServiceSettings {
    RuntimeSettings runtime;
    LogSettings log;
    MysqlSettings mysql;
    RedisSettings redis;
    IdSettings id;
};

static volatile sig_atomic_t g_stop_requested = 0;

static void on_signal(int value)
{
    (void)value;
    g_stop_requested = 1;
}

static void install_signal_handler(
    int signal_value,
    const char* signal_name,
    void (*handler)(int))
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(signal_value, &action, NULL) != 0) {
        ABE_LOG_WARN(
            "service signal handler install failed signal=%s errno=%d",
            signal_name == NULL ? "" : signal_name,
            errno);
    }
}

static void ignore_signal(int signal_value, const char* signal_name)
{
    install_signal_handler(signal_value, signal_name, SIG_IGN);
}

Service::~Service()
{
}

struct MessageQueue::Entry {
    void* source;
    uint64_t source_id;
    uint64_t enqueue_time_ms;
    unsigned char* data;
    uint32_t data_size;
};

MessageQueue::MessageQueue()
    : entries_(NULL),
      capacity_(0u),
      head_(0u),
      tail_(0u),
      count_(0u)
{
}

MessageQueue::~MessageQueue()
{
    close();
}

int MessageQueue::init(uint32_t capacity)
{
    close();
    if (capacity == 0u) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    entries_ = new (std::nothrow) Entry[capacity];
    if (entries_ == NULL) {
        return ABE_NO_MEMORY;
    }
    memset(entries_, 0, sizeof(Entry) * capacity);
    capacity_ = capacity;
    head_ = 0u;
    tail_ = 0u;
    count_ = 0u;
    return SERVICE_STATUS_OK;
}

void MessageQueue::close()
{
    uint32_t index;

    if (entries_ == NULL) {
        capacity_ = 0u;
        head_ = 0u;
        tail_ = 0u;
        count_ = 0u;
        return;
    }

    index = 0u;
    while (index < capacity_) {
        release_entry(&entries_[index]);
        ++index;
    }
    delete[] entries_;
    entries_ = NULL;
    capacity_ = 0u;
    head_ = 0u;
    tail_ = 0u;
    count_ = 0u;
}

int MessageQueue::push(
    void* source,
    uint64_t source_id,
    const void* data,
    uint32_t data_size,
    uint64_t now_ms)
{
    Entry* entry;
    unsigned char* copied_data;

    if (entries_ == NULL || capacity_ == 0u || (data_size != 0u && data == NULL)) {
        return SERVICE_STATUS_INVALID_ARG;
    }
    if (count_ >= capacity_) {
        return SERVICE_STATUS_NO_SLOT;
    }

    copied_data = NULL;
    if (data_size != 0u) {
        copied_data = new (std::nothrow) unsigned char[data_size];
        if (copied_data == NULL) {
            return ABE_NO_MEMORY;
        }
        memcpy(copied_data, data, data_size);
    }

    entry = &entries_[tail_];
    release_entry(entry);
    entry->source = source;
    entry->source_id = source_id;
    entry->enqueue_time_ms = now_ms;
    entry->data = copied_data;
    entry->data_size = data_size;

    ++tail_;
    if (tail_ >= capacity_) {
        tail_ = 0u;
    }
    ++count_;
    return SERVICE_STATUS_OK;
}

int MessageQueue::process(
    Service& service,
    uint32_t max_count,
    uint32_t* out_processed_count,
    uint32_t* out_failed_count)
{
    uint32_t processed_count;
    uint32_t failed_count;

    if (out_processed_count != NULL) {
        *out_processed_count = 0u;
    }
    if (out_failed_count != NULL) {
        *out_failed_count = 0u;
    }
    if (entries_ == NULL || capacity_ == 0u || max_count == 0u) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    processed_count = 0u;
    failed_count = 0u;
    while (processed_count < max_count && count_ != 0u) {
        Entry entry;
        Message message;
        int rc;

        entry = entries_[head_];
        memset(&entries_[head_], 0, sizeof(entries_[head_]));
        ++head_;
        if (head_ >= capacity_) {
            head_ = 0u;
        }
        --count_;

        memset(&message, 0, sizeof(message));
        message.source = entry.source;
        message.source_id = entry.source_id;
        message.enqueue_time_ms = entry.enqueue_time_ms;
        message.data = entry.data;
        message.data_size = entry.data_size;

        rc = service.process_message(message);
        if (rc != SERVICE_STATUS_OK) {
            ++failed_count;
            ABE_LOG_WARN(
                "service message process failed rc=%d status=%s source_id=%llu",
                rc,
                abe_status_name(rc),
                (unsigned long long)entry.source_id);
        }

        delete[] entry.data;
        ++processed_count;
    }

    if (out_processed_count != NULL) {
        *out_processed_count = processed_count;
    }
    if (out_failed_count != NULL) {
        *out_failed_count = failed_count;
    }
    return SERVICE_STATUS_OK;
}

uint32_t MessageQueue::count() const
{
    return count_;
}

uint32_t MessageQueue::capacity() const
{
    return capacity_;
}

void MessageQueue::release_entry(Entry* entry)
{
    if (entry == NULL) {
        return;
    }
    delete[] entry->data;
    memset(entry, 0, sizeof(*entry));
}

const char* Service::config_path() const
{
    return NULL;
}

void Service::defaults()
{
}

int Service::load_config(const abe_config_t* config)
{
    (void)config;
    return SERVICE_STATUS_OK;
}

int Service::process_message(const Message& message)
{
    (void)message;
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

static bool init_startup_log(const char* service_name)
{
    const char* name;

    name = service_name == NULL || service_name[0] == '\0' ? "abe_service" : service_name;
    if (abe::log::init_console(name) == abe::log::status_ok) {
        return true;
    }
    if (strcmp(name, "abe_service") != 0 &&
        abe::log::init_console("abe_service") == abe::log::status_ok) {
        return true;
    }
    return false;
}

static void shutdown_log_if_ready(bool* log_ready)
{
    abe::log::shutdown();
    if (log_ready != NULL) {
        *log_ready = false;
    }
}

static void apply_dynamic_defaults(ServiceSettings* settings)
{
    int timezone_offset;

    if (settings == NULL) {
        return;
    }

    if (abe_time_get_timezone_offset_minutes(&timezone_offset) == ABE_TIME_OK) {
        settings->log.utc_offset_minutes = timezone_offset;
    }
}

static int load_config_file(
    const char* config_path,
    abe_config_t** out_config)
{
    if (out_config == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }
    *out_config = NULL;

    if (config_path == NULL || config_path[0] == '\0') {
        return SERVICE_STATUS_OK;
    }
    if (abe_config_load_json_file(config_path, out_config) != ABE_CONFIG_OK) {
        ABE_LOG_ERROR(
            "service config load failed path=%s status=%s",
            config_path,
            abe_status_name(SERVICE_STATUS_FAILED));
        return SERVICE_STATUS_FAILED;
    }
    return SERVICE_STATUS_OK;
}

static int apply_runtime_config(
    ServiceSettings* settings,
    const abe_config_t* config)
{
    struct StringField {
        const char* path;
        char* value;
        size_t value_size;
    };
    struct U32Field {
        const char* path;
        uint32_t min_value;
        uint32_t max_value;
        uint32_t* value;
    };
    struct I32Field {
        const char* path;
        int32_t min_value;
        int32_t max_value;
        int32_t* value;
    };
    struct U64Field {
        const char* path;
        uint64_t min_value;
        uint64_t max_value;
        uint64_t* value;
    };
    size_t index;
    int rc;

    if (settings == NULL || config == NULL) {
        return SERVICE_STATUS_OK;
    }

    const StringField string_fields[] = {
        {"log.output", settings->log.output, sizeof(settings->log.output)},
        {"log.file", settings->log.file, sizeof(settings->log.file)},
        {"log.dir", settings->log.dir, sizeof(settings->log.dir)},
        {"log.level", settings->log.level, sizeof(settings->log.level)},
        {"mysql.host", settings->mysql.host, sizeof(settings->mysql.host)},
        {"mysql.database", settings->mysql.database, sizeof(settings->mysql.database)},
        {"mysql.user", settings->mysql.user, sizeof(settings->mysql.user)},
        {"mysql.password", settings->mysql.password, sizeof(settings->mysql.password)},
        {"redis.host", settings->redis.host, sizeof(settings->redis.host)},
        {"redis.password", settings->redis.password, sizeof(settings->redis.password)}
    };
    const U32Field u32_fields[] = {
        {"runtime.tick_ms", 0u, 1000u, &settings->runtime.tick_ms},
        {"runtime.timer_max_count", 1u, 1048576u, &settings->runtime.timer_max_count},
        {"runtime.message_tick_hz", 1u, 1000u, &settings->runtime.message_tick_hz},
        {"runtime.message_max_per_tick", 1u, 100000u, &settings->runtime.message_max_per_tick},
        {"runtime.message_queue_capacity", 1u, 1048576u, &settings->runtime.message_queue_capacity},
        {"mysql.port", 1u, 65535u, &settings->mysql.port},
        {"mysql.worker_count", 1u, 32u, &settings->mysql.worker_count},
        {"mysql.queue_capacity", 1u, 1048576u, &settings->mysql.queue_capacity},
        {"redis.port", 1u, 65535u, &settings->redis.port},
        {"redis.connect_timeout_ms", 1u, 60000u, &settings->redis.connect_timeout_ms},
        {"redis.command_timeout_ms", 0u, 60000u, &settings->redis.command_timeout_ms},
        {"id.node_id", 0u, ABE_SNOWFLAKE_MAX_NODE_ID, &settings->id.node_id}
    };
    const I32Field i32_fields[] = {
        {"log.utc_offset_minutes", -840, 840, &settings->log.utc_offset_minutes},
        {"redis.database", 0, 255, &settings->redis.database}
    };
    const U64Field u64_fields[] = {
        {
            "redis.memory_pool_capacity",
            0u,
            0xffffffffffffffffull,
            &settings->redis.memory_pool_capacity
        }
    };

    for (index = 0u; index < sizeof(string_fields) / sizeof(string_fields[0]); ++index) {
        const char* value;

        value = NULL;
        rc = abe_config_get_string(config, string_fields[index].path, &value);
        if (rc == ABE_CONFIG_NOT_FOUND) {
            continue;
        }
        if (rc != ABE_CONFIG_OK) {
            ABE_LOG_ERROR(
                "invalid string config value path=%s status=%s",
                string_fields[index].path,
                abe_status_name(SERVICE_STATUS_INVALID_ARG));
            return SERVICE_STATUS_INVALID_ARG;
        }
        rc = copy_setting_text(
            string_fields[index].path,
            value,
            string_fields[index].value,
            string_fields[index].value_size);
        if (rc != SERVICE_STATUS_OK) {
            return rc;
        }
    }

    for (index = 0u; index < sizeof(u32_fields) / sizeof(u32_fields[0]); ++index) {
        uint64_t value;

        value = 0u;
        rc = abe_config_get_u64(config, u32_fields[index].path, &value);
        if (rc == ABE_CONFIG_NOT_FOUND) {
            continue;
        }
        if (rc != ABE_CONFIG_OK ||
            value < u32_fields[index].min_value ||
            value > u32_fields[index].max_value) {
            ABE_LOG_ERROR(
                "invalid unsigned config value path=%s status=%s",
                u32_fields[index].path,
                abe_status_name(SERVICE_STATUS_INVALID_ARG));
            return SERVICE_STATUS_INVALID_ARG;
        }
        *u32_fields[index].value = (uint32_t)value;
    }

    for (index = 0u; index < sizeof(i32_fields) / sizeof(i32_fields[0]); ++index) {
        int64_t value;

        value = 0;
        rc = abe_config_get_i64(config, i32_fields[index].path, &value);
        if (rc == ABE_CONFIG_NOT_FOUND) {
            continue;
        }
        if (rc != ABE_CONFIG_OK ||
            value < i32_fields[index].min_value ||
            value > i32_fields[index].max_value) {
            ABE_LOG_ERROR(
                "invalid signed config value path=%s status=%s",
                i32_fields[index].path,
                abe_status_name(SERVICE_STATUS_INVALID_ARG));
            return SERVICE_STATUS_INVALID_ARG;
        }
        *i32_fields[index].value = (int32_t)value;
    }

    for (index = 0u; index < sizeof(u64_fields) / sizeof(u64_fields[0]); ++index) {
        uint64_t value;

        value = 0u;
        rc = abe_config_get_u64(config, u64_fields[index].path, &value);
        if (rc == ABE_CONFIG_NOT_FOUND) {
            continue;
        }
        if (rc != ABE_CONFIG_OK ||
            value < u64_fields[index].min_value ||
            value > u64_fields[index].max_value) {
            ABE_LOG_ERROR(
                "invalid unsigned config value path=%s status=%s",
                u64_fields[index].path,
                abe_status_name(SERVICE_STATUS_INVALID_ARG));
            return SERVICE_STATUS_INVALID_ARG;
        }
        *u64_fields[index].value = value;
    }

    return SERVICE_STATUS_OK;
}

static int init_log(
    const char* service_name,
    const ServiceSettings* config)
{
    const char* log_output;
    abe::log::level log_level;
    int rc;

    if (config == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    log_output = config->log.output[0] == '\0' ? "console" : config->log.output;
    if (strcmp(log_output, "console") == 0) {
        rc = abe::log::init_console(service_name);
    } else if (strcmp(log_output, "file") == 0) {
        if (config->log.file[0] == '\0') {
            ABE_LOG_ERROR(
                "service log file path is required when log-output=file status=%s",
                abe_status_name(SERVICE_STATUS_INVALID_ARG));
            return SERVICE_STATUS_INVALID_ARG;
        }
        rc = abe::log::init_file(service_name, config->log.file, false);
    } else if (strcmp(log_output, "daily") == 0) {
        rc = abe::log::init_daily_file(
            service_name,
            config->log.dir[0] == '\0' ? "logs" : config->log.dir,
            config->log.utc_offset_minutes);
    } else {
        ABE_LOG_ERROR(
            "unknown log output value=%s status=%s",
            log_output,
            abe_status_name(SERVICE_STATUS_INVALID_ARG));
        return SERVICE_STATUS_INVALID_ARG;
    }

    if (rc != abe::log::status_ok) {
        (void)init_startup_log(service_name);
        ABE_LOG_ERROR(
            "service log init failed rc=%d status=%s",
            rc,
            abe_status_name(SERVICE_STATUS_FAILED));
        return SERVICE_STATUS_FAILED;
    }

    rc = parse_log_level(config->log.level, &log_level);
    if (rc != SERVICE_STATUS_OK) {
        ABE_LOG_ERROR(
            "unknown log level value=%s status=%s",
            config->log.level,
            abe_status_name(SERVICE_STATUS_INVALID_ARG));
        return SERVICE_STATUS_INVALID_ARG;
    }
    rc = abe::log::set_level(log_level);
    if (rc != abe::log::status_ok) {
        ABE_LOG_ERROR(
            "service log level init failed rc=%d status=%s",
            rc,
            abe_status_name(SERVICE_STATUS_FAILED));
        return SERVICE_STATUS_FAILED;
    }
    return SERVICE_STATUS_OK;
}

static int init_mysql(
    const ServiceSettings* config,
    abe_db_mysql_async_t** out_mysql)
{
    abe_db_mysql_async_config_t mysql_config;
    int rc;

    if (config == NULL || out_mysql == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }
    *out_mysql = NULL;

    memset(&mysql_config, 0, sizeof(mysql_config));
    mysql_config.mysql.host = config->mysql.host;
    mysql_config.mysql.port = (uint16_t)config->mysql.port;
    mysql_config.mysql.database = config->mysql.database;
    mysql_config.mysql.user = config->mysql.user;
    mysql_config.mysql.password = config->mysql.password;
    mysql_config.mysql.charset = "utf8mb4";
    mysql_config.mysql.connect_timeout_seconds = 5u;
    mysql_config.mysql.read_timeout_seconds = 5u;
    mysql_config.mysql.write_timeout_seconds = 5u;
    mysql_config.mysql.reconnect = 1;
    mysql_config.worker_count = config->mysql.worker_count;
    mysql_config.queue_capacity = config->mysql.queue_capacity;

    rc = abe_db_mysql_async_create(&mysql_config, out_mysql);
    if (rc != ABE_DB_OK) {
        ABE_LOG_ERROR("mysql async connect failed rc=%d host=%s port=%u database=%s",
            rc,
            config->mysql.host,
            config->mysql.port,
            config->mysql.database);
        return SERVICE_STATUS_FAILED;
    }

    ABE_LOG_INFO("mysql async connected host=%s port=%u database=%s workers=%u",
        config->mysql.host,
        config->mysql.port,
        config->mysql.database,
        config->mysql.worker_count);
    return SERVICE_STATUS_OK;
}

static int init_redis(
    const ServiceSettings* config,
    abe_redis_async_t** out_redis)
{
    abe_redis_config_t redis_config;
    uint64_t start_ms;
    uint64_t now_ms;
    int rc;

    if (config == NULL || out_redis == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }
    *out_redis = NULL;

    memset(&redis_config, 0, sizeof(redis_config));
    redis_config.host = config->redis.host;
    redis_config.port = (uint16_t)config->redis.port;
    redis_config.password = config->redis.password;
    redis_config.database = config->redis.database;
    redis_config.connect_timeout_ms = config->redis.connect_timeout_ms;
    redis_config.command_timeout_ms = config->redis.command_timeout_ms;
    redis_config.memory_pool_capacity = config->redis.memory_pool_capacity;

    rc = abe_redis_async_create(&redis_config, out_redis);
    if (rc != ABE_REDIS_OK) {
        ABE_LOG_ERROR("redis async connect failed rc=%d host=%s port=%u database=%d",
            rc,
            config->redis.host,
            config->redis.port,
            config->redis.database);
        return SERVICE_STATUS_FAILED;
    }

    start_ms = abe_time_mono_ms();
    for (;;) {
        rc = abe_redis_async_update(*out_redis);
        if (rc != ABE_REDIS_OK) {
            ABE_LOG_ERROR("redis async init update failed rc=%d error=%s",
                rc,
                abe_redis_async_last_error(*out_redis));
            abe_redis_async_destroy(*out_redis);
            *out_redis = NULL;
            return SERVICE_STATUS_FAILED;
        }
        if (abe_redis_async_ready(*out_redis)) {
            break;
        }

        now_ms = abe_time_mono_ms();
        if (now_ms >= start_ms &&
            now_ms - start_ms >= config->redis.connect_timeout_ms) {
            ABE_LOG_ERROR("redis async init timeout host=%s port=%u database=%d error=%s",
                config->redis.host,
                config->redis.port,
                config->redis.database,
                abe_redis_async_last_error(*out_redis));
            abe_redis_async_destroy(*out_redis);
            *out_redis = NULL;
            return SERVICE_STATUS_FAILED;
        }
        usleep(1000u);
    }

    ABE_LOG_INFO("redis async connected host=%s port=%u database=%d",
        config->redis.host,
        config->redis.port,
        config->redis.database);
    return SERVICE_STATUS_OK;
}

static int init_time_wheel(
    const ServiceSettings* config,
    abe_time_wheel_t** out_time_wheel)
{
    abe_time_wheel_config_t wheel_config;
    uint32_t timer_max_count;
    int rc;

    if (out_time_wheel == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }
    *out_time_wheel = NULL;

    timer_max_count = config == NULL || config->runtime.timer_max_count == 0u
        ? SERVICE_RUNTIME_DEFAULT_TIMER_MAX_COUNT
        : config->runtime.timer_max_count;

    memset(&wheel_config, 0, sizeof(wheel_config));
    wheel_config.tick_ms = config == NULL
        ? SERVICE_RUNTIME_DEFAULT_TICK_MS
        : config->runtime.tick_ms;
    wheel_config.max_timer_count = timer_max_count;
    wheel_config.name = "service_time_wheel";

    rc = abe_time_wheel_create_mono(&wheel_config, out_time_wheel);
    if (rc != ABE_TIMER_OK) {
        ABE_LOG_ERROR("service time wheel init failed rc=%d status=%s",
            rc,
            abe_status_name(rc));
        return rc;
    }

    ABE_LOG_INFO(
        "service time wheel started tick_ms=%u max_timer_count=%u",
        wheel_config.tick_ms == 0u ? SERVICE_RUNTIME_DEFAULT_TICK_MS : wheel_config.tick_ms,
        timer_max_count);
    return SERVICE_STATUS_OK;
}

static int init_message_queue(
    const ServiceSettings* config,
    MessageQueue* queue)
{
    uint32_t capacity;
    int rc;

    if (queue == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    capacity = config == NULL || config->runtime.message_queue_capacity == 0u
        ? SERVICE_RUNTIME_DEFAULT_MESSAGE_QUEUE_CAPACITY
        : config->runtime.message_queue_capacity;
    rc = queue->init(capacity);
    if (rc != SERVICE_STATUS_OK) {
        ABE_LOG_ERROR("service message queue init failed rc=%d status=%s capacity=%u",
            rc,
            abe_status_name(rc),
            capacity);
        return rc;
    }

    ABE_LOG_INFO("service message queue started capacity=%u", capacity);
    return SERVICE_STATUS_OK;
}

static uint32_t message_tick_interval_ms(const ServiceSettings* config)
{
    uint32_t tick_hz;
    uint32_t interval_ms;

    tick_hz = config == NULL || config->runtime.message_tick_hz == 0u
        ? SERVICE_RUNTIME_DEFAULT_MESSAGE_TICK_HZ
        : config->runtime.message_tick_hz;
    interval_ms = 1000u / tick_hz;
    return interval_ms == 0u ? 1u : interval_ms;
}

static uint32_t message_max_per_tick(const ServiceSettings* config)
{
    if (config == NULL || config->runtime.message_max_per_tick == 0u) {
        return SERVICE_RUNTIME_DEFAULT_MESSAGE_MAX_PER_TICK;
    }
    return config->runtime.message_max_per_tick;
}

static int update_message_queue(
    Context* context,
    Service& service,
    const ServiceSettings* config,
    uint64_t now_ms,
    uint64_t* next_tick_ms)
{
    uint32_t processed_count;
    uint32_t failed_count;
    uint32_t interval_ms;
    int rc;

    if (context == NULL || context->message_queue == NULL || next_tick_ms == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }
    if (now_ms < *next_tick_ms) {
        return SERVICE_STATUS_OK;
    }

    processed_count = 0u;
    failed_count = 0u;
    rc = context->message_queue->process(
        service,
        message_max_per_tick(config),
        &processed_count,
        &failed_count);
    if (rc != SERVICE_STATUS_OK) {
        ABE_LOG_ERROR("service message queue update failed rc=%d status=%s",
            rc,
            abe_status_name(rc));
        return rc;
    }

    interval_ms = message_tick_interval_ms(config);
    *next_tick_ms += interval_ms;
    if (*next_tick_ms <= now_ms) {
        *next_tick_ms = now_ms + interval_ms;
    }
    return SERVICE_STATUS_OK;
}

static int run_loop(
    Context* context,
    Service& service,
    const ServiceSettings* settings)
{
    uint64_t next_message_tick_ms;
    uint32_t tick_ms;
    int result;
    int rc;

    reset_stop();
    install_stop_signal_handlers();

    tick_ms = settings == NULL
        ? SERVICE_RUNTIME_DEFAULT_TICK_MS
        : settings->runtime.tick_ms;
    next_message_tick_ms = abe_time_mono_ms();

    result = SERVICE_STATUS_OK;
    while (!stop_requested()) {
        uint32_t timer_fired_count;
        uint64_t now_ms;

        rc = context->loop->update();
        if (rc != ABE_NET_OK) {
            ABE_LOG_ERROR("service net update failed rc=%d", rc);
            result = rc;
            break;
        }

        timer_fired_count = 0u;
        rc = abe_time_wheel_update_mono(context->time_wheel, &timer_fired_count);
        if (rc != ABE_TIMER_OK) {
            ABE_LOG_ERROR("service time wheel update failed rc=%d status=%s",
                rc,
                abe_status_name(rc));
            result = rc;
            break;
        }

        now_ms = abe_time_mono_ms();
        rc = update_message_queue(
            context,
            service,
            settings,
            now_ms,
            &next_message_tick_ms);
        if (rc != SERVICE_STATUS_OK) {
            result = rc;
            break;
        }

        rc = abe_db_mysql_async_update(context->mysql, 0u, NULL);
        if (rc != ABE_DB_OK) {
            ABE_LOG_ERROR("service mysql async update failed rc=%d", rc);
            result = rc;
            break;
        }

        rc = abe_redis_async_update(context->redis);
        if (rc != ABE_REDIS_OK) {
            ABE_LOG_ERROR("service redis async update failed rc=%d error=%s",
                rc,
                abe_redis_async_last_error(context->redis));
            result = rc;
            break;
        }

        rc = service.update(now_ms);
        if (rc != SERVICE_STATUS_OK) {
            ABE_LOG_ERROR("service update failed rc=%d", rc);
            result = rc;
            break;
        }

        if (tick_ms != 0u) {
            usleep((useconds_t)tick_ms * 1000u);
        }
    }
    return result;
}

int run(Service& service)
{
    ServiceSettings settings;
    Context context;
    abe_config_t* config;
    abe_db_mysql_async_t* mysql;
    abe_redis_async_t* redis;
    abe_snowflake_t* id_generator;
    abe_time_wheel_t* time_wheel;
    MessageQueue message_queue;
    abe::adapter::net::Loop loop;
    const char* service_name;
    int loop_ready;
    int time_wheel_ready;
    int message_queue_ready;
    int service_ready;
    bool log_ready;
    int rc;
    int result;
    const char* config_path;

    service_name = service.name();
    if (service_name == NULL || service_name[0] == '\0') {
        service_name = "abe_service";
    }
    log_ready = init_startup_log(service_name);

    apply_dynamic_defaults(&settings);
    service.defaults();

    config_path = service.config_path();
    config = NULL;
    rc = load_config_file(config_path, &config);
    if (rc != SERVICE_STATUS_OK) {
        shutdown_log_if_ready(&log_ready);
        return rc;
    }
    rc = apply_runtime_config(&settings, config);
    if (rc != SERVICE_STATUS_OK) {
        if (config != NULL) {
            abe_config_destroy(config);
        }
        shutdown_log_if_ready(&log_ready);
        return rc;
    }
    rc = service.load_config(config);
    if (rc != SERVICE_STATUS_OK) {
        if (config != NULL) {
            abe_config_destroy(config);
        }
        shutdown_log_if_ready(&log_ready);
        return rc;
    }

    rc = init_log(service_name, &settings);
    if (rc != SERVICE_STATUS_OK) {
        if (config != NULL) {
            abe_config_destroy(config);
        }
        shutdown_log_if_ready(&log_ready);
        return rc;
    }

    id_generator = NULL;
    rc = abe_snowflake_create((uint16_t)settings.id.node_id, &id_generator);
    if (rc != ABE_SNOWFLAKE_OK) {
        ABE_LOG_ERROR("snowflake init failed rc=%d node_id=%u", rc, settings.id.node_id);
        abe::log::shutdown();
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return rc;
    }

    mysql = NULL;
    rc = init_mysql(&settings, &mysql);
    if (rc != SERVICE_STATUS_OK) {
        abe_snowflake_destroy(id_generator);
        abe::log::shutdown();
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return rc;
    }

    redis = NULL;
    rc = init_redis(&settings, &redis);
    if (rc != SERVICE_STATUS_OK) {
        if (mysql != NULL) {
            abe_db_mysql_async_destroy(mysql);
        }
        abe_snowflake_destroy(id_generator);
        abe::log::shutdown();
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return rc;
    }

    loop_ready = 0;
    time_wheel_ready = 0;
    message_queue_ready = 0;
    service_ready = 0;
    time_wheel = NULL;
    memset(&context, 0, sizeof(context));
    rc = loop.create();
    if (rc != ABE_NET_OK) {
        ABE_LOG_ERROR("service loop create failed rc=%d", rc);
        result = rc;
    } else {
        loop_ready = 1;
        rc = init_time_wheel(&settings, &time_wheel);
        if (rc != SERVICE_STATUS_OK) {
            result = rc;
        } else {
            time_wheel_ready = 1;
            rc = init_message_queue(&settings, &message_queue);
            if (rc != SERVICE_STATUS_OK) {
                result = rc;
            } else {
                message_queue_ready = 1;
                context.loop = &loop;
                context.time_wheel = time_wheel;
                context.message_queue = &message_queue;
                context.config = config;
                context.mysql = mysql;
                context.redis = redis;
                context.id_generator = id_generator;

                rc = service.init(context);
                if (rc != SERVICE_STATUS_OK) {
                    ABE_LOG_ERROR("service init failed rc=%d", rc);
                    result = rc;
                } else {
                    service_ready = 1;
                    ABE_LOG_INFO("service started name=%s", service_name);
                    result = run_loop(&context, service, &settings);
                }
            }
        }
    }

    if (service_ready) {
        service.close(abe_time_mono_ms());
    }
    if (message_queue_ready) {
        message_queue.close();
    }
    if (time_wheel_ready) {
        abe_time_wheel_destroy(time_wheel);
    }
    if (loop_ready) {
        loop.destroy();
    }
    if (redis != NULL) {
        abe_redis_async_destroy(redis);
    }
    if (mysql != NULL) {
        abe_db_mysql_async_destroy(mysql);
    }
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
    install_signal_handler(SIGINT, "SIGINT", on_signal);
    install_signal_handler(SIGTERM, "SIGTERM", on_signal);
    install_signal_handler(SIGHUP, "SIGHUP", on_signal);
    install_signal_handler(SIGQUIT, "SIGQUIT", on_signal);
    ignore_signal(SIGPIPE, "SIGPIPE");
}

} /* namespace common */
} /* namespace service */
} /* namespace abe */
