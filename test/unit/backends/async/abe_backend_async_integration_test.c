#include "abe_db_mysql_async.h"
#include "abe_redis_async.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

struct mysql_state {
    int called;
    int status;
    int value_ok;
};

struct redis_state {
    int called;
    int status;
    int value_ok;
};

static uint16_t read_port(const char* value, uint16_t default_value)
{
    unsigned long port;

    if (value == NULL || value[0] == '\0') {
        return default_value;
    }
    port = strtoul(value, NULL, 10);
    return port > 0u && port <= 65535u ? (uint16_t)port : default_value;
}

static void on_mysql_query(
    abe_db_mysql_async_t* mysql,
    const abe_db_mysql_async_query_result_t* result,
    void* user_data)
{
    struct mysql_state* state;
    const void* data;
    uint64_t size;

    (void)mysql;
    state = (struct mysql_state*)user_data;
    state->called = 1;
    state->status = result == NULL ? ABE_DB_ERROR : result->status;
    if (result == NULL || result->status != ABE_DB_OK || result->result == NULL) {
        return;
    }
    size = 0u;
    data = abe_db_mysql_async_result_get_blob(result->result, 0u, 0u, &size);
    state->value_ok = data != NULL && size == 2u && memcmp(data, "42", 2u) == 0;
}

static void on_redis_command(
    abe_redis_async_t* redis,
    int status,
    const abe_redis_reply_t* reply,
    const char* error_message,
    void* user_data)
{
    struct redis_state* state;
    abe_redis_reply_view_t view;

    (void)redis;
    (void)error_message;
    state = (struct redis_state*)user_data;
    state->called = 1;
    state->status = status;
    if (status != ABE_REDIS_OK || reply == NULL ||
        abe_redis_reply_view(reply, &view) != ABE_REDIS_OK) {
        return;
    }
    state->value_ok = view.type == ABE_REDIS_REPLY_STATUS &&
        view.data != NULL && view.data_size == 4u && memcmp(view.data, "PONG", 4u) == 0;
}

static int wait_mysql(abe_db_mysql_async_t* mysql, struct mysql_state* state)
{
    uint32_t index;

    for (index = 0u; index < 500u && !state->called; ++index) {
        TEST_REQUIRE(abe_db_mysql_async_update(mysql, 0u, NULL) == ABE_DB_OK);
        (void)poll(NULL, 0u, 10);
    }
    return state->called && state->status == ABE_DB_OK && state->value_ok ? 0 : 1;
}

static int wait_redis_ready(abe_redis_async_t* redis)
{
    uint32_t index;

    for (index = 0u; index < 500u; ++index) {
        if (abe_redis_async_update(redis) != ABE_REDIS_OK) {
            return 0;
        }
        if (abe_redis_async_ready(redis)) {
            return 1;
        }
        (void)poll(NULL, 0u, 10);
    }
    return 0;
}

static int wait_redis(abe_redis_async_t* redis, struct redis_state* state)
{
    uint32_t index;

    for (index = 0u; index < 500u && !state->called; ++index) {
        TEST_REQUIRE(abe_redis_async_update(redis) == ABE_REDIS_OK);
        (void)poll(NULL, 0u, 10);
    }
    return state->called && state->status == ABE_REDIS_OK && state->value_ok ? 0 : 1;
}

int main(void)
{
    abe_db_mysql_async_config_t mysql_config;
    abe_db_mysql_async_t* mysql;
    abe_redis_config_t redis_config;
    abe_redis_async_t* redis;
    struct mysql_state mysql_state;
    struct redis_state redis_state;
    const char* ping[1];
    uint64_t ping_sizes[1];

    if (getenv("ABE_RUN_BACKEND_INTEGRATION") == NULL) {
        return 0;
    }

    memset(&mysql_config, 0, sizeof(mysql_config));
    mysql_config.mysql.host = getenv("ABE_MYSQL_HOST");
    mysql_config.mysql.port = read_port(getenv("ABE_MYSQL_PORT"), 3306u);
    mysql_config.mysql.database = getenv("ABE_MYSQL_DATABASE");
    mysql_config.mysql.user = getenv("ABE_MYSQL_USER");
    mysql_config.mysql.password = getenv("ABE_MYSQL_PASSWORD");
    mysql_config.mysql.charset = "utf8mb4";
    mysql_config.mysql.connect_timeout_seconds = 5u;
    mysql_config.worker_count = 2u;
    mysql_config.queue_capacity = 16u;
    mysql = NULL;
    TEST_REQUIRE(abe_db_mysql_async_create(&mysql_config, &mysql) == ABE_DB_OK);
    memset(&mysql_state, 0, sizeof(mysql_state));
    TEST_REQUIRE(abe_db_mysql_async_query(mysql, "SELECT 42", on_mysql_query, &mysql_state) ==
        ABE_DB_OK);
    TEST_REQUIRE(wait_mysql(mysql, &mysql_state) == 0);
    abe_db_mysql_async_destroy(mysql);

    memset(&redis_config, 0, sizeof(redis_config));
    redis_config.host = getenv("ABE_REDIS_HOST");
    redis_config.port = read_port(getenv("ABE_REDIS_PORT"), 6379u);
    redis_config.database = 0;
    redis = NULL;
    TEST_REQUIRE(abe_redis_async_create(&redis_config, &redis) == ABE_REDIS_OK);
    TEST_REQUIRE(wait_redis_ready(redis));
    memset(&redis_state, 0, sizeof(redis_state));
    ping[0] = "PING";
    ping_sizes[0] = 4u;
    TEST_REQUIRE(abe_redis_async_command(
        redis,
        1u,
        ping,
        ping_sizes,
        on_redis_command,
        &redis_state) == ABE_REDIS_OK);
    TEST_REQUIRE(wait_redis(redis, &redis_state) == 0);
    abe_redis_async_destroy(redis);
    return 0;
}
