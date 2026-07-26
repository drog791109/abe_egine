#ifndef ABE_KAFKA_H
#define ABE_KAFKA_H

#include "abe_error.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABE_KAFKA_PARTITION_ANY -1

typedef struct abe_kafka_producer abe_kafka_producer_t;
typedef struct abe_kafka_consumer abe_kafka_consumer_t;
typedef struct abe_kafka_message abe_kafka_message_t;

typedef enum abe_kafka_status {
    ABE_KAFKA_OK = ABE_OK,
    ABE_KAFKA_NO_MESSAGE = 1,
    ABE_KAFKA_ERROR = ABE_ERROR,
    ABE_KAFKA_INVALID_ARG = ABE_INVALID_ARG,
    ABE_KAFKA_NO_MEMORY = ABE_NO_MEMORY,
    ABE_KAFKA_CONFIG_ERROR = ABE_BAD_VALUE,
    ABE_KAFKA_CONNECT_FAILED = ABE_CONNECT_FAILED,
    ABE_KAFKA_SEND_FAILED = ABE_SEND_FAILED
} abe_kafka_status_t;

typedef struct abe_kafka_config_entry {
    const char* name;
    const char* value;
} abe_kafka_config_entry_t;

typedef struct abe_kafka_producer_config {
    const char* brokers;
    const char* client_id;
    const abe_kafka_config_entry_t* properties;
    uint32_t property_count;
    uint64_t memory_pool_capacity;
} abe_kafka_producer_config_t;

typedef struct abe_kafka_consumer_config {
    const char* brokers;
    const char* group_id;
    const char* client_id;
    const char* auto_offset_reset;
    const abe_kafka_config_entry_t* properties;
    uint32_t property_count;
    uint64_t memory_pool_capacity;
} abe_kafka_consumer_config_t;

typedef struct abe_kafka_message_view {
    const char* topic;
    int32_t partition;
    int64_t offset;
    const void* key;
    uint64_t key_size;
    const void* payload;
    uint64_t payload_size;
    int error_code;
    const char* error_message;
} abe_kafka_message_view_t;

int abe_kafka_producer_create(
    const abe_kafka_producer_config_t* config,
    abe_kafka_producer_t** out_producer);
int abe_kafka_producer_produce(
    abe_kafka_producer_t* producer,
    const char* topic,
    int32_t partition,
    const void* key,
    uint64_t key_size,
    const void* payload,
    uint64_t payload_size);
int abe_kafka_producer_poll(
    abe_kafka_producer_t* producer,
    int timeout_ms,
    int* out_event_count);
int abe_kafka_producer_flush(abe_kafka_producer_t* producer, int timeout_ms);
const char* abe_kafka_producer_last_error(const abe_kafka_producer_t* producer);
void abe_kafka_producer_destroy(abe_kafka_producer_t* producer);

int abe_kafka_consumer_create(
    const abe_kafka_consumer_config_t* config,
    abe_kafka_consumer_t** out_consumer);
int abe_kafka_consumer_subscribe(
    abe_kafka_consumer_t* consumer,
    const char** topics,
    uint32_t topic_count);
int abe_kafka_consumer_poll(
    abe_kafka_consumer_t* consumer,
    int timeout_ms,
    abe_kafka_message_t** out_message);
int abe_kafka_consumer_commit_message(
    abe_kafka_consumer_t* consumer,
    const abe_kafka_message_t* message,
    int async);
const char* abe_kafka_consumer_last_error(const abe_kafka_consumer_t* consumer);
void abe_kafka_consumer_destroy(abe_kafka_consumer_t* consumer);

int abe_kafka_message_view(
    const abe_kafka_message_t* message,
    abe_kafka_message_view_t* out_view);
void abe_kafka_message_destroy(abe_kafka_message_t* message);

#ifdef __cplusplus
}
#endif

#endif /* ABE_KAFKA_H */
