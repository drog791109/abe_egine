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
    SERVICE_RUNTIME_DEFAULT_REDIS_PORT = 6379u
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

bool Options::exists(const char* name) const
{
    uint32_t index;

    if (options_ == NULL || name == NULL) {
        return false;
    }

    index = 0u;
    while (index < count_) {
        if (options_[index].name != NULL && strcmp(options_[index].name, name) == 0) {
            return true;
        }
        ++index;
    }
    return false;
}

Service::~Service()
{
}

struct MessageQueue::Entry {
    MessageHandler handler;
    void* user_data;
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
    MessageHandler handler,
    void* user_data,
    void* source,
    uint64_t source_id,
    const void* data,
    uint32_t data_size,
    uint64_t now_ms)
{
    Entry* entry;
    unsigned char* copied_data;

    if (entries_ == NULL || capacity_ == 0u || handler == NULL ||
        (data_size != 0u && data == NULL)) {
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
    entry->handler = handler;
    entry->user_data = user_data;
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

        rc = entry.handler(message, entry.user_data);
        if (rc != SERVICE_STATUS_OK) {
            ++failed_count;
            ABE_LOG_WARN(
                "service message handler failed rc=%d status=%s source_id=%llu",
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
    config->timer_max_count = SERVICE_RUNTIME_DEFAULT_TIMER_MAX_COUNT;
    config->message_tick_hz = SERVICE_RUNTIME_DEFAULT_MESSAGE_TICK_HZ;
    config->message_max_per_tick = SERVICE_RUNTIME_DEFAULT_MESSAGE_MAX_PER_TICK;
    config->message_queue_capacity = SERVICE_RUNTIME_DEFAULT_MESSAGE_QUEUE_CAPACITY;
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

    rc = options.add_u32(
        "--timer-max-count",
        "count",
        "service time wheel maximum timers, default 65536",
        1u,
        1048576u,
        &config->timer_max_count);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_u32(
        "--message-tick-hz",
        "hz",
        "message queue process rate, default 30",
        1u,
        1000u,
        &config->message_tick_hz);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_u32(
        "--message-max-per-tick",
        "count",
        "maximum received messages processed per message tick, default 500",
        1u,
        100000u,
        &config->message_max_per_tick);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }

    rc = options.add_u32(
        "--message-queue-capacity",
        "count",
        "received message queue capacity, default 65536",
        1u,
        1048576u,
        &config->message_queue_capacity);
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
        ABE_LOG_ERROR(
            "service config load failed path=%s status=%s",
            runtime_config->config_path,
            abe_status_name(SERVICE_STATUS_FAILED));
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
        ABE_LOG_ERROR(
            "invalid string config value path=%s status=%s",
            path,
            abe_status_name(SERVICE_STATUS_INVALID_ARG));
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
        ABE_LOG_ERROR(
            "invalid unsigned config value path=%s status=%s",
            path,
            abe_status_name(SERVICE_STATUS_INVALID_ARG));
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
        ABE_LOG_ERROR(
            "invalid signed config value path=%s status=%s",
            path,
            abe_status_name(SERVICE_STATUS_INVALID_ARG));
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
        ABE_LOG_ERROR(
            "invalid unsigned config value path=%s status=%s",
            path,
            abe_status_name(SERVICE_STATUS_INVALID_ARG));
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
    rc = read_config_u32(
        config,
        "runtime.timer_max_count",
        1u,
        1048576u,
        &runtime_config->timer_max_count);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_u32(
        config,
        "runtime.message_tick_hz",
        1u,
        1000u,
        &runtime_config->message_tick_hz);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_u32(
        config,
        "runtime.message_max_per_tick",
        1u,
        100000u,
        &runtime_config->message_max_per_tick);
    if (rc != SERVICE_STATUS_OK) {
        return rc;
    }
    rc = read_config_u32(
        config,
        "runtime.message_queue_capacity",
        1u,
        1048576u,
        &runtime_config->message_queue_capacity);
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
            ABE_LOG_ERROR(
                "service log file path is required when log-output=file status=%s",
                abe_status_name(SERVICE_STATUS_INVALID_ARG));
            return SERVICE_STATUS_INVALID_ARG;
        }
        rc = abe::log::init_file(service_name, config->log_file, false);
    } else if (strcmp(log_output, "daily") == 0) {
        rc = abe::log::init_daily_file(
            service_name,
            config->log_dir == NULL ? "logs" : config->log_dir,
            config->log_utc_offset_minutes);
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

    rc = parse_log_level(config->log_level, &log_level);
    if (rc != SERVICE_STATUS_OK) {
        ABE_LOG_ERROR(
            "unknown log level value=%s status=%s",
            config->log_level == NULL ? "" : config->log_level,
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

static int init_time_wheel(
    const RuntimeConfig* config,
    abe_time_wheel_t** out_time_wheel)
{
    abe_time_wheel_config_t wheel_config;
    uint32_t timer_max_count;
    int rc;

    if (out_time_wheel == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }
    *out_time_wheel = NULL;

    timer_max_count = config == NULL || config->timer_max_count == 0u
        ? SERVICE_RUNTIME_DEFAULT_TIMER_MAX_COUNT
        : config->timer_max_count;

    memset(&wheel_config, 0, sizeof(wheel_config));
    wheel_config.tick_ms = config == NULL
        ? SERVICE_RUNTIME_DEFAULT_TICK_MS
        : config->tick_ms;
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
    const RuntimeConfig* config,
    MessageQueue* queue)
{
    uint32_t capacity;
    int rc;

    if (queue == NULL) {
        return SERVICE_STATUS_INVALID_ARG;
    }

    capacity = config == NULL || config->message_queue_capacity == 0u
        ? SERVICE_RUNTIME_DEFAULT_MESSAGE_QUEUE_CAPACITY
        : config->message_queue_capacity;
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

static uint32_t message_tick_interval_ms(const RuntimeConfig* config)
{
    uint32_t tick_hz;
    uint32_t interval_ms;

    tick_hz = config == NULL || config->message_tick_hz == 0u
        ? SERVICE_RUNTIME_DEFAULT_MESSAGE_TICK_HZ
        : config->message_tick_hz;
    interval_ms = 1000u / tick_hz;
    return interval_ms == 0u ? 1u : interval_ms;
}

static uint32_t message_max_per_tick(const RuntimeConfig* config)
{
    if (config == NULL || config->message_max_per_tick == 0u) {
        return SERVICE_RUNTIME_DEFAULT_MESSAGE_MAX_PER_TICK;
    }
    return config->message_max_per_tick;
}

static int update_message_queue(
    Context* context,
    const RuntimeConfig* config,
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
    const RuntimeConfig* runtime_config)
{
    uint64_t next_message_tick_ms;
    uint32_t tick_ms;
    int result;
    int rc;

    reset_stop();
    install_stop_signal_handlers();

    tick_ms = runtime_config == NULL
        ? SERVICE_RUNTIME_DEFAULT_TICK_MS
        : runtime_config->tick_ms;
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
        rc = update_message_queue(context, runtime_config, now_ms, &next_message_tick_ms);
        if (rc != SERVICE_STATUS_OK) {
            result = rc;
            break;
        }

#ifdef ABE_SERVICE_HAS_MYSQL
        if (context->mysql != NULL) {
            rc = abe_db_mysql_async_update(context->mysql, 0u, NULL);
            if (rc != ABE_DB_OK) {
                ABE_LOG_ERROR("service mysql async update failed rc=%d", rc);
                result = rc;
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
                result = rc;
                break;
            }
        }
#endif

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

static void log_option_registration_failed(
    const char* service_name,
    int rc)
{
    ABE_LOG_ERROR(
        "service option registration failed service=%s rc=%d status=%s",
        service_name == NULL ? "" : service_name,
        rc,
        abe_status_name(rc));
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

    service_name = service.name();
    if (service_name == NULL || service_name[0] == '\0') {
        service_name = "abe_service";
    }
    log_ready = init_startup_log(service_name);

    set_runtime_defaults(service_name, &runtime_config);
    runtime_config.config_path = service.config_path();

    service.defaults();

    rc = add_common_options(&runtime_config, option_list);
    if (rc == SERVICE_STATUS_OK) {
        rc = service.options(option_list);
    }
    if (rc != SERVICE_STATUS_OK) {
        log_option_registration_failed(service_name, rc);
        shutdown_log_if_ready(&log_ready);
        return rc;
    }

    rc = parse_config_option(argc, argv, &runtime_config);
    if (rc == SERVICE_ARG_HELP) {
        service_log_usage(argv == NULL ? NULL : argv[0], option_list.data(), option_list.count());
        shutdown_log_if_ready(&log_ready);
        return SERVICE_STATUS_OK;
    }
    if (rc != SERVICE_ARG_OK) {
        ABE_LOG_ERROR(
            "service config option parse failed rc=%d status=%s",
            rc,
            abe_status_name(rc));
        service_log_usage(argv == NULL ? NULL : argv[0], option_list.data(), option_list.count());
        shutdown_log_if_ready(&log_ready);
        return rc;
    }

    config = NULL;
    rc = load_config_file(&runtime_config, &config);
    if (rc != SERVICE_STATUS_OK) {
        shutdown_log_if_ready(&log_ready);
        return rc;
    }
    rc = apply_runtime_config(&runtime_config, config);
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
    rc = service_parse_options(argc, argv, option_list.data(), option_list.count());
    if (rc == SERVICE_ARG_HELP) {
        service_log_usage(argv == NULL ? NULL : argv[0], option_list.data(), option_list.count());
        if (config != NULL) {
            abe_config_destroy(config);
        }
        shutdown_log_if_ready(&log_ready);
        return SERVICE_STATUS_OK;
    }
    if (rc != SERVICE_ARG_OK) {
        service_log_usage(argv == NULL ? NULL : argv[0], option_list.data(), option_list.count());
        if (config != NULL) {
            abe_config_destroy(config);
        }
        shutdown_log_if_ready(&log_ready);
        return rc;
    }

    rc = init_log(service_name, &runtime_config);
    if (rc != SERVICE_STATUS_OK) {
        if (config != NULL) {
            abe_config_destroy(config);
        }
        shutdown_log_if_ready(&log_ready);
        return rc;
    }

    id_generator = NULL;
    rc = abe_snowflake_create((uint16_t)runtime_config.id_node_id, &id_generator);
    if (rc != ABE_SNOWFLAKE_OK) {
        ABE_LOG_ERROR("snowflake init failed rc=%d node_id=%u", rc, runtime_config.id_node_id);
        abe::log::shutdown();
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return rc;
    }

    mysql = NULL;
    rc = init_mysql(&runtime_config, &mysql);
    if (rc != SERVICE_STATUS_OK) {
        abe_snowflake_destroy(id_generator);
        abe::log::shutdown();
        if (config != NULL) {
            abe_config_destroy(config);
        }
        return rc;
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
        rc = init_time_wheel(&runtime_config, &time_wheel);
        if (rc != SERVICE_STATUS_OK) {
            result = rc;
        } else {
            time_wheel_ready = 1;
            rc = init_message_queue(&runtime_config, &message_queue);
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
                context.runtime = &runtime_config;

                rc = service.init(context);
                if (rc != SERVICE_STATUS_OK) {
                    ABE_LOG_ERROR("service init failed rc=%d", rc);
                    result = rc;
                } else {
                    service_ready = 1;
                    ABE_LOG_INFO("service started name=%s", service_name);
                    result = run_loop(&context, service, &runtime_config);
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
    install_signal_handler(SIGINT, "SIGINT", on_signal);
    install_signal_handler(SIGTERM, "SIGTERM", on_signal);
    install_signal_handler(SIGHUP, "SIGHUP", on_signal);
    install_signal_handler(SIGQUIT, "SIGQUIT", on_signal);
    ignore_signal(SIGPIPE, "SIGPIPE");
}

} /* namespace common */
} /* namespace service */
} /* namespace abe */
