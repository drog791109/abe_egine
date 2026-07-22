#ifndef ABE_RABBITMQ_H
#define ABE_RABBITMQ_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abe_rabbitmq abe_rabbitmq_t;
typedef struct abe_rabbitmq_message abe_rabbitmq_message_t;

typedef enum abe_rabbitmq_status {
    ABE_RABBITMQ_OK = 0,
    ABE_RABBITMQ_NO_MESSAGE = 1,
    ABE_RABBITMQ_ERROR = -1,
    ABE_RABBITMQ_INVALID_ARG = -2,
    ABE_RABBITMQ_NO_MEMORY = -3,
    ABE_RABBITMQ_CONNECT_FAILED = -4,
    ABE_RABBITMQ_PROTOCOL_ERROR = -5,
    ABE_RABBITMQ_SEND_FAILED = -6
} abe_rabbitmq_status_t;

typedef struct abe_rabbitmq_config {
    const char* host;
    uint16_t port;
    const char* virtual_host;
    const char* user;
    const char* password;
    uint16_t channel;
    uint32_t frame_max;
    uint16_t heartbeat_seconds;
    uint64_t memory_pool_capacity;
} abe_rabbitmq_config_t;

typedef struct abe_rabbitmq_message_view {
    const void* body;
    uint64_t body_size;
    const char* exchange;
    uint64_t exchange_size;
    const char* routing_key;
    uint64_t routing_key_size;
    uint64_t delivery_tag;
    int redelivered;
} abe_rabbitmq_message_view_t;

int abe_rabbitmq_create(const abe_rabbitmq_config_t* config, abe_rabbitmq_t** out_rabbitmq);

int abe_rabbitmq_declare_exchange(
    abe_rabbitmq_t* rabbitmq,
    const char* exchange,
    const char* type,
    int durable,
    int auto_delete);

int abe_rabbitmq_declare_queue(
    abe_rabbitmq_t* rabbitmq,
    const char* queue,
    int durable,
    int exclusive,
    int auto_delete);

int abe_rabbitmq_bind_queue(
    abe_rabbitmq_t* rabbitmq,
    const char* queue,
    const char* exchange,
    const char* routing_key);

int abe_rabbitmq_publish(
    abe_rabbitmq_t* rabbitmq,
    const char* exchange,
    const char* routing_key,
    const void* body,
    uint64_t body_size,
    const char* content_type,
    int persistent);

int abe_rabbitmq_consume(
    abe_rabbitmq_t* rabbitmq,
    const char* queue,
    const char* consumer_tag,
    int auto_ack);

int abe_rabbitmq_poll(
    abe_rabbitmq_t* rabbitmq,
    int timeout_ms,
    abe_rabbitmq_message_t** out_message);

int abe_rabbitmq_ack(abe_rabbitmq_t* rabbitmq, uint64_t delivery_tag);
int abe_rabbitmq_reject(abe_rabbitmq_t* rabbitmq, uint64_t delivery_tag, int requeue);
const char* abe_rabbitmq_last_error(const abe_rabbitmq_t* rabbitmq);
void abe_rabbitmq_destroy(abe_rabbitmq_t* rabbitmq);

int abe_rabbitmq_message_view(
    const abe_rabbitmq_message_t* message,
    abe_rabbitmq_message_view_t* out_view);
void abe_rabbitmq_message_destroy(abe_rabbitmq_message_t* message);

#ifdef __cplusplus
}
#endif

#endif /* ABE_RABBITMQ_H */
