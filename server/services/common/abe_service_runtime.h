#ifndef ABE_SERVICE_RUNTIME_H
#define ABE_SERVICE_RUNTIME_H

#include "abe_config.h"
#include "abe_db.h"
#include "abe_error.h"
#include "abe_net_link.h"
#include "abe_service_args.h"
#include "abe_time_wheel.h"

#include <stdint.h>

typedef struct abe_db_mysql_async abe_db_mysql_async_t;
typedef struct abe_redis_async abe_redis_async_t;
typedef struct abe_snowflake abe_snowflake_t;

namespace abe {
namespace service {
namespace common {

enum {
    SERVICE_RUNTIME_MAX_OPTIONS = 64u
};

enum ServiceStatus {
    SERVICE_STATUS_OK = ABE_OK,
    SERVICE_STATUS_INVALID_ARG = ABE_INVALID_ARG,
    SERVICE_STATUS_NO_SLOT = ABE_NO_SLOT,
    SERVICE_STATUS_DUPLICATE = ABE_ALREADY_EXISTS,
    SERVICE_STATUS_FAILED = ABE_ERROR
};

struct RuntimeConfig {
    const char* config_path;
    uint32_t tick_ms;
    uint32_t timer_max_count;
    uint32_t message_tick_hz;
    uint32_t message_max_per_tick;
    uint32_t message_queue_capacity;

    const char* log_output;
    const char* log_file;
    const char* log_dir;
    const char* log_level;
    int32_t log_utc_offset_minutes;

    uint32_t mysql_enable;
    const char* mysql_host;
    uint32_t mysql_port;
    const char* mysql_database;
    const char* mysql_user;
    const char* mysql_password;
    uint32_t mysql_worker_count;
    uint32_t mysql_queue_capacity;

    uint32_t redis_enable;
    const char* redis_host;
    uint32_t redis_port;
    const char* redis_password;
    int32_t redis_database;
    uint32_t redis_connect_timeout_ms;
    uint32_t redis_command_timeout_ms;
    uint64_t redis_memory_pool_capacity;

    uint32_t id_node_id;
};

struct Message {
    void* source;
    uint64_t source_id;
    uint64_t enqueue_time_ms;
    const void* data;
    uint32_t data_size;
};

typedef int (*MessageHandler)(const Message& message, void* user_data);

class MessageQueue {
public:
    MessageQueue();
    ~MessageQueue();

    int init(uint32_t capacity);
    void close();

    int push(
        MessageHandler handler,
        void* user_data,
        void* source,
        uint64_t source_id,
        const void* data,
        uint32_t data_size,
        uint64_t now_ms);
    int process(
        uint32_t max_count,
        uint32_t* out_processed_count,
        uint32_t* out_failed_count);

    uint32_t count() const;
    uint32_t capacity() const;

private:
    MessageQueue(const MessageQueue&);
    MessageQueue& operator=(const MessageQueue&);

    struct Entry;

    void release_entry(Entry* entry);

    Entry* entries_;
    uint32_t capacity_;
    uint32_t head_;
    uint32_t tail_;
    uint32_t count_;
};

struct Context {
    abe::adapter::net::Loop* loop;
    abe_time_wheel_t* time_wheel;
    MessageQueue* message_queue;
    const abe_config_t* config;
    abe_db_mysql_async_t* mysql;
    abe_redis_async_t* redis;
    abe_snowflake_t* id_generator;
    const RuntimeConfig* runtime;
};

class Options {
public:
    Options(ServiceOption* options, uint32_t max_options);

    int add_string(
        const char* name,
        const char* value_name,
        const char* description,
        const char** out_value);
    int add_u32(
        const char* name,
        const char* value_name,
        const char* description,
        uint32_t min_value,
        uint32_t max_value,
        uint32_t* out_value);
    int add_u64(
        const char* name,
        const char* value_name,
        const char* description,
        uint64_t min_value,
        uint64_t max_value,
        uint64_t* out_value);
    int add_i32(
        const char* name,
        const char* value_name,
        const char* description,
        int32_t min_value,
        int32_t max_value,
        int32_t* out_value);

    const ServiceOption* data() const;
    uint32_t count() const;

private:
    Options(const Options&);
    Options& operator=(const Options&);

    int add(const ServiceOption& option);
    bool exists(const char* name) const;

    ServiceOption* options_;
    uint32_t max_;
    uint32_t count_;
};

class Service {
public:
    virtual ~Service();

    virtual const char* name() const = 0;
    virtual const char* config_path() const;
    virtual void defaults();
    virtual int options(Options& options);
    virtual int load_config(const abe_config_t* config);
    virtual int init(Context& context) = 0;
    virtual int update(uint64_t now_ms);
    virtual void close(uint64_t now_ms);
};

int run(int argc, char** argv, Service& service);

void reset_stop();
void request_stop();
int stop_requested();
void install_stop_signal_handlers();

} /* namespace common */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_RUNTIME_H */
