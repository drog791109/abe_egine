#include "abe_redis_async.h"

#include "abe_time.h"

#include <event2/event.h>
#include <hiredis/adapters/libevent.h>
#include <hiredis/async.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ABE_REDIS_ASYNC_ERROR_SIZE 512u

/* Keep this layout in sync with abe_redis.c for reply view helpers. */
struct abe_redis_reply {
    abe_redis_t* owner;
    redisReply* reply;
    int owns_reply;
};

struct abe_redis_async_command {
    struct abe_redis_async* owner;
    abe_redis_async_command_callback_fn callback;
    void* user_data;
    uint64_t submitted_at_ms;
    int timed_out;
    struct abe_redis_async_command* previous;
    struct abe_redis_async_command* next;
};

struct abe_redis_async {
    redisAsyncContext* context;
    struct event_base* event_base;
    char* password;
    int database;
    int connected;
    int ready;
    int failed;
    int setup_pending;
    int setup_failed;
    int stopping;
    uint64_t connect_started_at_ms;
    uint32_t connect_timeout_ms;
    uint32_t command_timeout_ms;
    char last_error[ABE_REDIS_ASYNC_ERROR_SIZE];
    struct abe_redis_async_command* command_head;
};

static void abe_redis_async_copy_error(abe_redis_async_t* redis, const char* message)
{
    const char* text;

    if (redis == NULL) {
        return;
    }
    text = message == NULL || message[0] == '\0' ? "redis async operation failed" : message;
    (void)strncpy(redis->last_error, text, sizeof(redis->last_error) - 1u);
    redis->last_error[sizeof(redis->last_error) - 1u] = '\0';
}

static uint64_t abe_redis_async_cstr_size(const char* value)
{
    return value == NULL ? 0u : (uint64_t)strlen(value);
}

static char* abe_redis_async_copy_text(const char* value)
{
    char* copy;
    size_t size;

    if (value == NULL) {
        return NULL;
    }
    size = strlen(value) + 1u;
    copy = (char*)malloc(size);
    if (copy != NULL) {
        memcpy(copy, value, size);
    }
    return copy;
}

static void abe_redis_async_add_command(
    abe_redis_async_t* redis,
    struct abe_redis_async_command* command)
{
    command->next = redis->command_head;
    command->previous = NULL;
    if (redis->command_head != NULL) {
        redis->command_head->previous = command;
    }
    redis->command_head = command;
}

static void abe_redis_async_remove_command(struct abe_redis_async_command* command)
{
    abe_redis_async_t* redis;

    if (command == NULL || command->owner == NULL) {
        return;
    }
    redis = command->owner;
    if (command->previous != NULL) {
        command->previous->next = command->next;
    } else {
        redis->command_head = command->next;
    }
    if (command->next != NULL) {
        command->next->previous = command->previous;
    }
    command->previous = NULL;
    command->next = NULL;
}

static void abe_redis_async_complete_setup(abe_redis_async_t* redis, int success)
{
    if (redis == NULL) {
        return;
    }
    if (!success) {
        redis->setup_failed = 1;
        redis->failed = 1;
    }
    if (redis->setup_pending > 0) {
        --redis->setup_pending;
    }
    if (redis->setup_pending == 0 && !redis->setup_failed) {
        redis->ready = 1;
    }
}

static void abe_redis_async_setup_callback(
    redisAsyncContext* context,
    void* raw_reply,
    void* user_data)
{
    abe_redis_async_t* redis;
    redisReply* reply;

    (void)context;
    redis = (abe_redis_async_t*)user_data;
    reply = (redisReply*)raw_reply;
    if (redis == NULL || redis->stopping) {
        return;
    }
    if (reply == NULL || reply->type == REDIS_REPLY_ERROR) {
        abe_redis_async_copy_error(redis, reply == NULL ? "redis setup command failed" : reply->str);
        abe_redis_async_complete_setup(redis, 0);
        return;
    }
    abe_redis_async_complete_setup(redis, 1);
}

static int abe_redis_async_send_setup(
    abe_redis_async_t* redis,
    uint32_t argc,
    const char** argv,
    const uint64_t* argv_sizes)
{
    size_t sizes[2];
    uint32_t index;

    for (index = 0u; index < argc; ++index) {
        if (argv_sizes[index] > (uint64_t)((size_t)-1)) {
            return ABE_REDIS_INVALID_ARG;
        }
        sizes[index] = (size_t)argv_sizes[index];
    }
    if (redisAsyncCommandArgv(
            redis->context,
            abe_redis_async_setup_callback,
            redis,
            (int)argc,
            argv,
            sizes) != REDIS_OK) {
        abe_redis_async_copy_error(redis, redis->context->errstr);
        return ABE_REDIS_COMMAND_FAILED;
    }
    ++redis->setup_pending;
    return ABE_REDIS_OK;
}

static void abe_redis_async_connect_callback(const redisAsyncContext* context, int status)
{
    abe_redis_async_t* redis;
    char database_text[32];

    redis = context == NULL ? NULL : (abe_redis_async_t*)context->data;
    if (redis == NULL || redis->stopping) {
        return;
    }
    if (status != REDIS_OK) {
        abe_redis_async_copy_error(redis, context->errstr);
        redis->failed = 1;
        return;
    }

    redis->connected = 1;
    if (redis->password != NULL && redis->password[0] != '\0') {
        const char* argv[2];
        uint64_t sizes[2];

        argv[0] = "AUTH";
        argv[1] = redis->password;
        sizes[0] = 4u;
        sizes[1] = abe_redis_async_cstr_size(redis->password);
        if (abe_redis_async_send_setup(redis, 2u, argv, sizes) != ABE_REDIS_OK) {
            redis->setup_failed = 1;
            redis->failed = 1;
        }
    }
    if (redis->database >= 0) {
        const char* argv[2];
        uint64_t sizes[2];
        int written;

        written = snprintf(database_text, sizeof(database_text), "%d", redis->database);
        if (written < 0 || (size_t)written >= sizeof(database_text)) {
            redis->setup_failed = 1;
            redis->failed = 1;
        } else {
            argv[0] = "SELECT";
            argv[1] = database_text;
            sizes[0] = 6u;
            sizes[1] = (uint64_t)written;
            if (abe_redis_async_send_setup(redis, 2u, argv, sizes) != ABE_REDIS_OK) {
                redis->setup_failed = 1;
                redis->failed = 1;
            }
        }
    }
    if (redis->setup_pending == 0 && !redis->setup_failed) {
        redis->ready = 1;
    }
}

static void abe_redis_async_disconnect_callback(const redisAsyncContext* context, int status)
{
    abe_redis_async_t* redis;

    redis = context == NULL ? NULL : (abe_redis_async_t*)context->data;
    if (redis == NULL) {
        return;
    }
    redis->connected = 0;
    redis->ready = 0;
    if (!redis->stopping && status != REDIS_OK) {
        abe_redis_async_copy_error(redis, context->errstr);
        redis->failed = 1;
    }
}

static void abe_redis_async_command_callback(
    redisAsyncContext* context,
    void* raw_reply,
    void* user_data)
{
    struct abe_redis_async_command* command;
    abe_redis_async_t* redis;
    redisReply* raw;
    struct abe_redis_reply reply;
    int status;
    const char* error_message;

    (void)context;
    command = (struct abe_redis_async_command*)user_data;
    if (command == NULL) {
        return;
    }
    redis = command->owner;
    raw = (redisReply*)raw_reply;
    status = ABE_REDIS_OK;
    error_message = "";
    if (raw == NULL) {
        status = ABE_REDIS_COMMAND_FAILED;
        error_message = redis == NULL ? "redis command failed" : redis->last_error;
    } else if (raw->type == REDIS_REPLY_ERROR) {
        status = ABE_REDIS_COMMAND_FAILED;
        error_message = raw->str == NULL ? "redis command failed" : raw->str;
        if (redis != NULL) {
            abe_redis_async_copy_error(redis, error_message);
        }
    }

    memset(&reply, 0, sizeof(reply));
    reply.reply = raw;
    reply.owns_reply = 0;
    abe_redis_async_remove_command(command);
    if (redis != NULL && !redis->stopping && command->callback != NULL) {
        command->callback(
            redis,
            status,
            raw == NULL ? NULL : &reply,
            error_message,
            command->user_data);
    }
    free(command);
}

static void abe_redis_async_check_timeouts(abe_redis_async_t* redis, uint64_t now_ms)
{
    struct abe_redis_async_command* command;

    if (redis == NULL || redis->command_timeout_ms == 0u) {
        return;
    }
    command = redis->command_head;
    while (command != NULL) {
        struct abe_redis_async_command* next;

        next = command->next;
        if (!command->timed_out && now_ms >= command->submitted_at_ms &&
            now_ms - command->submitted_at_ms >= redis->command_timeout_ms) {
            abe_redis_async_command_callback_fn callback;
            void* user_data;

            command->timed_out = 1;
            callback = command->callback;
            user_data = command->user_data;
            command->callback = NULL;
            if (callback != NULL && !redis->stopping) {
                callback(redis, ABE_TIMEOUT, NULL, "redis command timeout", user_data);
            }
        }
        command = next;
    }
}

int abe_redis_async_create(
    const abe_redis_config_t* config,
    abe_redis_async_t** out_redis)
{
    abe_redis_async_t* redis;
    const char* host;
    uint16_t port;

    if (config == NULL || out_redis == NULL) {
        return ABE_REDIS_INVALID_ARG;
    }
    *out_redis = NULL;
    redis = (abe_redis_async_t*)calloc(1u, sizeof(*redis));
    if (redis == NULL) {
        return ABE_REDIS_NO_MEMORY;
    }
    if (config->password != NULL) {
        redis->password = abe_redis_async_copy_text(config->password);
        if (redis->password == NULL) {
            free(redis);
            return ABE_REDIS_NO_MEMORY;
        }
    }
    redis->database = config->database;
    redis->connect_timeout_ms = config->connect_timeout_ms;
    redis->command_timeout_ms = config->command_timeout_ms;
    redis->connect_started_at_ms = abe_time_mono_ms();
    host = config->host == NULL || config->host[0] == '\0' ? "127.0.0.1" : config->host;
    port = config->port == 0u ? 6379u : config->port;

    redis->event_base = event_base_new();
    if (redis->event_base == NULL) {
        free(redis->password);
        free(redis);
        return ABE_REDIS_NO_MEMORY;
    }
    redis->context = redisAsyncConnect(host, (int)port);
    if (redis->context == NULL || redis->context->err != 0) {
        abe_redis_async_copy_error(
            redis,
            redis->context == NULL ? "redis async connect failed" : redis->context->errstr);
        if (redis->context != NULL) {
            redisAsyncFree(redis->context);
        }
        event_base_free(redis->event_base);
        free(redis->password);
        free(redis);
        return ABE_REDIS_CONNECT_FAILED;
    }
    redis->context->data = redis;
    if (redisLibeventAttach(redis->context, redis->event_base) != REDIS_OK ||
        redisAsyncSetConnectCallback(redis->context, abe_redis_async_connect_callback) != REDIS_OK ||
        redisAsyncSetDisconnectCallback(redis->context, abe_redis_async_disconnect_callback) != REDIS_OK) {
        abe_redis_async_copy_error(redis, redis->context->errstr);
        redisAsyncFree(redis->context);
        event_base_free(redis->event_base);
        free(redis->password);
        free(redis);
        return ABE_REDIS_ERROR;
    }

    *out_redis = redis;
    return ABE_REDIS_OK;
}

void abe_redis_async_destroy(abe_redis_async_t* redis)
{
    struct abe_redis_async_command* command;

    if (redis == NULL) {
        return;
    }
    redis->stopping = 1;
    if (redis->context != NULL) {
        redis->context->data = NULL;
        redisAsyncFree(redis->context);
        redis->context = NULL;
    }
    command = redis->command_head;
    while (command != NULL) {
        struct abe_redis_async_command* next;

        next = command->next;
        free(command);
        command = next;
    }
    if (redis->event_base != NULL) {
        event_base_free(redis->event_base);
    }
    free(redis->password);
    free(redis);
}

int abe_redis_async_command(
    abe_redis_async_t* redis,
    uint32_t argc,
    const char** argv,
    const uint64_t* argv_sizes,
    abe_redis_async_command_callback_fn callback,
    void* user_data)
{
    size_t sizes[ABE_REDIS_MAX_ARGC];
    struct abe_redis_async_command* command;
    uint32_t index;

    if (redis == NULL || redis->context == NULL || argc == 0u ||
        argc > ABE_REDIS_MAX_ARGC || argv == NULL || callback == NULL) {
        return ABE_REDIS_INVALID_ARG;
    }
    if (redis->failed) {
        return ABE_REDIS_CONNECT_FAILED;
    }
    if (!redis->ready) {
        return ABE_WOULD_BLOCK;
    }
    for (index = 0u; index < argc; ++index) {
        uint64_t size;

        if (argv[index] == NULL) {
            return ABE_REDIS_INVALID_ARG;
        }
        size = argv_sizes == NULL ? abe_redis_async_cstr_size(argv[index]) : argv_sizes[index];
        if (size > (uint64_t)((size_t)-1)) {
            return ABE_REDIS_INVALID_ARG;
        }
        sizes[index] = (size_t)size;
    }

    command = (struct abe_redis_async_command*)calloc(1u, sizeof(*command));
    if (command == NULL) {
        return ABE_REDIS_NO_MEMORY;
    }
    command->owner = redis;
    command->callback = callback;
    command->user_data = user_data;
    command->submitted_at_ms = abe_time_mono_ms();
    abe_redis_async_add_command(redis, command);
    if (redisAsyncCommandArgv(
            redis->context,
            abe_redis_async_command_callback,
            command,
            (int)argc,
            argv,
            sizes) != REDIS_OK) {
        abe_redis_async_remove_command(command);
        abe_redis_async_copy_error(redis, redis->context->errstr);
        free(command);
        return ABE_REDIS_COMMAND_FAILED;
    }
    return ABE_REDIS_OK;
}

int abe_redis_async_update(abe_redis_async_t* redis)
{
    uint64_t now_ms;

    if (redis == NULL || redis->event_base == NULL) {
        return ABE_REDIS_INVALID_ARG;
    }
    if (event_base_loop(redis->event_base, EVLOOP_NONBLOCK) < 0) {
        abe_redis_async_copy_error(redis, "redis async event loop failed");
        return ABE_REDIS_ERROR;
    }
    now_ms = abe_time_mono_ms();
    if (!redis->connected && !redis->failed && redis->connect_timeout_ms != 0u &&
        now_ms >= redis->connect_started_at_ms &&
        now_ms - redis->connect_started_at_ms >= redis->connect_timeout_ms) {
        abe_redis_async_copy_error(redis, "redis async connect timeout");
        redis->failed = 1;
        redisAsyncDisconnect(redis->context);
    }
    abe_redis_async_check_timeouts(redis, now_ms);
    if (redis->failed) {
        return ABE_REDIS_ERROR;
    }
    return ABE_REDIS_OK;
}

int abe_redis_async_ready(const abe_redis_async_t* redis)
{
    return redis != NULL && redis->ready && !redis->failed && !redis->stopping;
}

const char* abe_redis_async_last_error(const abe_redis_async_t* redis)
{
    return redis == NULL ? "" : redis->last_error;
}
