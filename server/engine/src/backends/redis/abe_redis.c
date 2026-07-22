#include "abe_redis.h"
#include "abe_mem_pool.h"

#include <hiredis.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define ABE_REDIS_ERROR_SIZE 512u
#define ABE_REDIS_DEFAULT_POOL_CAPACITY (64u * 1024u)

struct abe_redis {
    redisContext* context;
    abe_mem_pool_t* mem_pool;
    int owns_mem_pool;
    char last_error[ABE_REDIS_ERROR_SIZE];
};

struct abe_redis_reply {
    abe_redis_t* owner;
    redisReply* reply;
};

static uint64_t abe_redis_pool_capacity(uint64_t configured_capacity)
{
    return configured_capacity == 0u ? ABE_REDIS_DEFAULT_POOL_CAPACITY : configured_capacity;
}

static void abe_redis_copy_error(abe_redis_t* redis, const char* message)
{
    if (redis == NULL) {
        return;
    }
    if (message == NULL || message[0] == '\0') {
        redis->last_error[0] = '\0';
        return;
    }
    (void)strncpy(redis->last_error, message, ABE_REDIS_ERROR_SIZE - 1u);
    redis->last_error[ABE_REDIS_ERROR_SIZE - 1u] = '\0';
}

static void abe_redis_copy_context_error(abe_redis_t* redis, const char* fallback)
{
    if (redis != NULL && redis->context != NULL &&
        redis->context->errstr[0] != '\0') {
        abe_redis_copy_error(redis, redis->context->errstr);
    } else {
        abe_redis_copy_error(redis, fallback);
    }
}

static void abe_redis_timeval_from_ms(uint32_t timeout_ms, struct timeval* out_timeval)
{
    if (out_timeval == NULL) {
        return;
    }
    out_timeval->tv_sec = (long)(timeout_ms / 1000u);
    out_timeval->tv_usec = (long)((timeout_ms % 1000u) * 1000u);
}

static uint64_t abe_redis_cstr_size(const char* text)
{
    return text == NULL ? 0u : (uint64_t)strlen(text);
}

static int abe_redis_send_simple_command(
    abe_redis_t* redis,
    uint32_t argc,
    const char** argv,
    const uint64_t* argv_sizes)
{
    abe_redis_reply_t* reply;
    int status;

    reply = NULL;
    status = abe_redis_command(redis, argc, argv, argv_sizes, &reply);
    if (status == ABE_REDIS_OK && reply != NULL && reply->reply != NULL &&
        reply->reply->type == REDIS_REPLY_ERROR) {
        abe_redis_copy_error(redis, reply->reply->str);
        status = ABE_REDIS_COMMAND_FAILED;
    }
    if (reply != NULL) {
        abe_redis_reply_destroy(reply);
    }
    return status;
}

static void abe_redis_fill_view(const redisReply* reply, abe_redis_reply_view_t* out_view)
{
    if (out_view == NULL) {
        return;
    }

    memset(out_view, 0, sizeof(*out_view));
    if (reply == NULL) {
        out_view->type = ABE_REDIS_REPLY_UNKNOWN;
        return;
    }

    out_view->type = reply->type;
    out_view->integer = reply->integer;
    if (reply->str != NULL) {
        out_view->data = reply->str;
        out_view->data_size = (uint64_t)reply->len;
    }
    if (reply->elements > 0u) {
        out_view->element_count = (uint32_t)reply->elements;
    }
}

int abe_redis_create(const abe_redis_config_t* config, abe_redis_t** out_redis)
{
    abe_mem_pool_config_t pool_config;
    abe_mem_pool_t* mem_pool;
    abe_redis_t* redis;
    struct timeval timeout;
    const char* host;
    uint16_t port;
    int status;

    if (config == NULL || out_redis == NULL) {
        return ABE_REDIS_INVALID_ARG;
    }

    *out_redis = NULL;
    host = config->host == NULL ? "127.0.0.1" : config->host;
    port = config->port == 0u ? 6379u : config->port;

    memset(&pool_config, 0, sizeof(pool_config));
    pool_config.capacity = abe_redis_pool_capacity(config->memory_pool_capacity);
    pool_config.name = "abe_redis";
    mem_pool = NULL;
    status = abe_mem_pool_create(&pool_config, &mem_pool);
    if (status != ABE_MEM_POOL_OK) {
        return ABE_REDIS_NO_MEMORY;
    }

    redis = (abe_redis_t*)abe_mem_pool_calloc(mem_pool, 1u, sizeof(*redis));
    if (redis == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_REDIS_NO_MEMORY;
    }
    redis->mem_pool = mem_pool;
    redis->owns_mem_pool = 1;

    abe_redis_timeval_from_ms(config->connect_timeout_ms, &timeout);
    if (config->connect_timeout_ms == 0u) {
        redis->context = redisConnect(host, (int)port);
    } else {
        redis->context = redisConnectWithTimeout(host, (int)port, timeout);
    }
    if (redis->context == NULL || redis->context->err != 0) {
        abe_redis_copy_context_error(redis, "redis connect failed");
        if (redis->context != NULL) {
            redisFree(redis->context);
            redis->context = NULL;
        }
        abe_mem_pool_destroy(mem_pool);
        return ABE_REDIS_CONNECT_FAILED;
    }

    if (config->command_timeout_ms != 0u) {
        abe_redis_timeval_from_ms(config->command_timeout_ms, &timeout);
        if (redisSetTimeout(redis->context, timeout) != REDIS_OK) {
            abe_redis_copy_context_error(redis, "redis timeout setup failed");
            redisFree(redis->context);
            redis->context = NULL;
            abe_mem_pool_destroy(mem_pool);
            return ABE_REDIS_ERROR;
        }
    }

    if (config->password != NULL && config->password[0] != '\0') {
        const char* argv[2];
        uint64_t argv_sizes[2];

        argv[0] = "AUTH";
        argv[1] = config->password;
        argv_sizes[0] = 4u;
        argv_sizes[1] = abe_redis_cstr_size(config->password);
        status = abe_redis_send_simple_command(redis, 2u, argv, argv_sizes);
        if (status != ABE_REDIS_OK) {
            redisFree(redis->context);
            redis->context = NULL;
            abe_mem_pool_destroy(mem_pool);
            return status;
        }
    }

    if (config->database >= 0) {
        char db_index[32];
        const char* argv[2];
        uint64_t argv_sizes[2];

        (void)snprintf(db_index, sizeof(db_index), "%d", config->database);
        argv[0] = "SELECT";
        argv[1] = db_index;
        argv_sizes[0] = 6u;
        argv_sizes[1] = abe_redis_cstr_size(db_index);
        status = abe_redis_send_simple_command(redis, 2u, argv, argv_sizes);
        if (status != ABE_REDIS_OK) {
            redisFree(redis->context);
            redis->context = NULL;
            abe_mem_pool_destroy(mem_pool);
            return status;
        }
    }

    *out_redis = redis;
    return ABE_REDIS_OK;
}

int abe_redis_ping(abe_redis_t* redis)
{
    const char* argv[1];
    uint64_t argv_sizes[1];

    argv[0] = "PING";
    argv_sizes[0] = 4u;
    return abe_redis_send_simple_command(redis, 1u, argv, argv_sizes);
}

int abe_redis_command(
    abe_redis_t* redis,
    uint32_t argc,
    const char** argv,
    const uint64_t* argv_sizes,
    abe_redis_reply_t** out_reply)
{
    size_t hiredis_sizes[ABE_REDIS_MAX_ARGC];
    redisReply* raw_reply;
    abe_redis_reply_t* reply;
    uint32_t index;

    if (redis == NULL || redis->context == NULL ||
        argc == 0u || argc > ABE_REDIS_MAX_ARGC ||
        argv == NULL || out_reply == NULL) {
        return ABE_REDIS_INVALID_ARG;
    }

    *out_reply = NULL;
    for (index = 0u; index < argc; ++index) {
        uint64_t size;

        if (argv[index] == NULL) {
            return ABE_REDIS_INVALID_ARG;
        }
        size = argv_sizes == NULL ? abe_redis_cstr_size(argv[index]) : argv_sizes[index];
        if (size > (uint64_t)((size_t)-1)) {
            return ABE_REDIS_INVALID_ARG;
        }
        hiredis_sizes[index] = (size_t)size;
    }

    raw_reply = (redisReply*)redisCommandArgv(redis->context, (int)argc, argv, hiredis_sizes);
    if (raw_reply == NULL) {
        abe_redis_copy_context_error(redis, "redis command failed");
        return ABE_REDIS_COMMAND_FAILED;
    }

    reply = (abe_redis_reply_t*)abe_mem_pool_calloc(redis->mem_pool, 1u, sizeof(*reply));
    if (reply == NULL) {
        freeReplyObject(raw_reply);
        abe_redis_copy_error(redis, "redis reply wrapper allocation failed");
        return ABE_REDIS_NO_MEMORY;
    }

    reply->owner = redis;
    reply->reply = raw_reply;
    *out_reply = reply;
    return ABE_REDIS_OK;
}

const char* abe_redis_last_error(const abe_redis_t* redis)
{
    return redis == NULL ? "" : redis->last_error;
}

void abe_redis_destroy(abe_redis_t* redis)
{
    abe_mem_pool_t* mem_pool;
    int owns_mem_pool;

    if (redis == NULL) {
        return;
    }

    mem_pool = redis->mem_pool;
    owns_mem_pool = redis->owns_mem_pool;
    if (redis->context != NULL) {
        redisFree(redis->context);
        redis->context = NULL;
    }
    if (mem_pool != NULL) {
        (void)abe_mem_pool_free(mem_pool, redis);
        if (owns_mem_pool != 0) {
            abe_mem_pool_destroy(mem_pool);
        }
    }
}

int abe_redis_reply_view(const abe_redis_reply_t* reply, abe_redis_reply_view_t* out_view)
{
    if (reply == NULL || reply->reply == NULL || out_view == NULL) {
        return ABE_REDIS_INVALID_ARG;
    }
    abe_redis_fill_view(reply->reply, out_view);
    return ABE_REDIS_OK;
}

int abe_redis_reply_element_view(
    const abe_redis_reply_t* reply,
    uint32_t index,
    abe_redis_reply_view_t* out_view)
{
    if (reply == NULL || reply->reply == NULL || out_view == NULL ||
        reply->reply->element == NULL || index >= reply->reply->elements) {
        return ABE_REDIS_OUT_OF_RANGE;
    }
    abe_redis_fill_view(reply->reply->element[index], out_view);
    return ABE_REDIS_OK;
}

void abe_redis_reply_destroy(abe_redis_reply_t* reply)
{
    abe_mem_pool_t* mem_pool;

    if (reply == NULL) {
        return;
    }
    mem_pool = reply->owner == NULL ? NULL : reply->owner->mem_pool;
    if (reply->reply != NULL) {
        freeReplyObject(reply->reply);
        reply->reply = NULL;
    }
    if (mem_pool != NULL) {
        (void)abe_mem_pool_free(mem_pool, reply);
    }
}
