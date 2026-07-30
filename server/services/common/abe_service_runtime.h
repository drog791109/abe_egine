#ifndef ABE_SERVICE_RUNTIME_H
#define ABE_SERVICE_RUNTIME_H

#include "abe_config.h"
#include "abe_db.h"
#include "abe_error.h"
#include "abe_net_link.h"
#include "abe_time_wheel.h"

#include <stdint.h>

typedef struct abe_db_mysql_async abe_db_mysql_async_t;
typedef struct abe_redis_async abe_redis_async_t;
typedef struct abe_snowflake abe_snowflake_t;

namespace abe {
namespace service {
namespace common {

enum ServiceStatus {
    SERVICE_STATUS_OK = ABE_OK,
    SERVICE_STATUS_INVALID_ARG = ABE_INVALID_ARG,
    SERVICE_STATUS_NO_SLOT = ABE_NO_SLOT,
    SERVICE_STATUS_DUPLICATE = ABE_ALREADY_EXISTS,
    SERVICE_STATUS_FAILED = ABE_ERROR
};

struct Message {
    void* source;
    uint64_t source_id;
    uint64_t enqueue_time_ms;
    const void* data;
    uint32_t data_size;
};

class Service;

class MessageQueue {
public:
    MessageQueue();
    ~MessageQueue();

    int init(uint32_t capacity);
    void close();

    int push(
        void* source,
        uint64_t source_id,
        const void* data,
        uint32_t data_size,
        uint64_t now_ms);
    int process(
        Service& service,
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
};

class Service {
public:
    virtual ~Service();

    virtual const char* name() const = 0;
    virtual const char* config_path() const;
    virtual void defaults();
    virtual int load_config(const abe_config_t* config);
    virtual int init(Context& context) = 0;
    virtual int process_message(const Message& message);
    virtual int update(uint64_t now_ms);
    virtual void close(uint64_t now_ms);
};

void reset_stop();
void request_stop();
int stop_requested();
void install_stop_signal_handlers();
int run(Service& service);

} /* namespace common */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_RUNTIME_H */
