#ifndef ABE_REDIS_H
#define ABE_REDIS_H

#include "abe_error.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABE_REDIS_MAX_ARGC 128u

typedef struct abe_redis abe_redis_t;
typedef struct abe_redis_reply abe_redis_reply_t;

typedef enum abe_redis_status {
    ABE_REDIS_OK = ABE_OK,
    ABE_REDIS_ERROR = ABE_ERROR,
    ABE_REDIS_INVALID_ARG = ABE_INVALID_ARG,
    ABE_REDIS_NO_MEMORY = ABE_NO_MEMORY,
    ABE_REDIS_CONNECT_FAILED = ABE_CONNECT_FAILED,
    ABE_REDIS_COMMAND_FAILED = ABE_COMMAND_FAILED,
    ABE_REDIS_OUT_OF_RANGE = ABE_OUT_OF_RANGE
} abe_redis_status_t;

typedef enum abe_redis_reply_type {
    ABE_REDIS_REPLY_UNKNOWN = 0,
    ABE_REDIS_REPLY_STRING = 1,
    ABE_REDIS_REPLY_ARRAY = 2,
    ABE_REDIS_REPLY_INTEGER = 3,
    ABE_REDIS_REPLY_NIL = 4,
    ABE_REDIS_REPLY_STATUS = 5,
    ABE_REDIS_REPLY_ERROR = 6
} abe_redis_reply_type_t;

typedef struct abe_redis_config {
    const char* host;
    uint16_t port;
    const char* password;
    int database;
    uint32_t connect_timeout_ms;
    uint32_t command_timeout_ms;
    uint64_t memory_pool_capacity;
} abe_redis_config_t;

typedef struct abe_redis_reply_view {
    int type;
    int64_t integer;
    const char* data;
    uint64_t data_size;
    uint32_t element_count;
} abe_redis_reply_view_t;

int abe_redis_create(const abe_redis_config_t* config, abe_redis_t** out_redis);
int abe_redis_ping(abe_redis_t* redis);

int abe_redis_command(
    abe_redis_t* redis,
    uint32_t argc,
    const char** argv,
    const uint64_t* argv_sizes,
    abe_redis_reply_t** out_reply);

const char* abe_redis_last_error(const abe_redis_t* redis);
void abe_redis_destroy(abe_redis_t* redis);

int abe_redis_reply_view(const abe_redis_reply_t* reply, abe_redis_reply_view_t* out_view);
int abe_redis_reply_element_view(
    const abe_redis_reply_t* reply,
    uint32_t index,
    abe_redis_reply_view_t* out_view);
void abe_redis_reply_destroy(abe_redis_reply_t* reply);

#ifdef __cplusplus
}
#endif

#endif /* ABE_REDIS_H */
