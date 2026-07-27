#ifndef ABE_REDIS_ASYNC_H
#define ABE_REDIS_ASYNC_H

#include "abe_redis.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abe_redis_async abe_redis_async_t;

/*
 * The reply is callback-local and must not be destroyed by the callback. It is
 * a view over hiredis memory and is invalid as soon as the callback returns.
 */
typedef void (*abe_redis_async_command_callback_fn)(
    abe_redis_async_t* redis,
    int status,
    const abe_redis_reply_t* reply,
    const char* error_message,
    void* user_data);

/*
 * Starts a non-blocking hiredis connection. Call update from the service
 * thread every tick. Commands are accepted only after ready returns nonzero;
 * before that, command returns ABE_WOULD_BLOCK without invoking a callback.
 */
int abe_redis_async_create(
    const abe_redis_config_t* config,
    abe_redis_async_t** out_redis);
void abe_redis_async_destroy(abe_redis_async_t* redis);

int abe_redis_async_command(
    abe_redis_async_t* redis,
    uint32_t argc,
    const char** argv,
    const uint64_t* argv_sizes,
    abe_redis_async_command_callback_fn callback,
    void* user_data);

int abe_redis_async_update(abe_redis_async_t* redis);
int abe_redis_async_ready(const abe_redis_async_t* redis);
const char* abe_redis_async_last_error(const abe_redis_async_t* redis);

#ifdef __cplusplus
}
#endif

#endif /* ABE_REDIS_ASYNC_H */
