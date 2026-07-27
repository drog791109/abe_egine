#ifndef ABE_DB_MYSQL_ASYNC_H
#define ABE_DB_MYSQL_ASYNC_H

#include "abe_db_mysql.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abe_db_mysql_async abe_db_mysql_async_t;
typedef struct abe_db_mysql_async_result abe_db_mysql_async_result_t;

typedef struct abe_db_mysql_async_config {
    abe_db_mysql_config_t mysql;
    uint32_t worker_count;
    uint32_t queue_capacity;
} abe_db_mysql_async_config_t;

typedef struct abe_db_mysql_async_query_result {
    int status;
    const abe_db_mysql_async_result_t* result;
    const char* error_message;
} abe_db_mysql_async_query_result_t;

typedef struct abe_db_mysql_async_execute_result {
    int status;
    uint64_t affected_rows;
    const char* error_message;
} abe_db_mysql_async_execute_result_t;

/*
 * Result data is owned by the async handle and is valid only while its query
 * callback runs. Values are copied out of the worker connection.
 */
typedef void (*abe_db_mysql_async_query_callback_fn)(
    abe_db_mysql_async_t* mysql,
    const abe_db_mysql_async_query_result_t* result,
    void* user_data);

typedef void (*abe_db_mysql_async_execute_callback_fn)(
    abe_db_mysql_async_t* mysql,
    const abe_db_mysql_async_execute_result_t* result,
    void* user_data);

/*
 * Each worker owns one MySQL connection. worker_count defaults to 1 and
 * queue_capacity defaults to 1024. Call update from the service thread; every
 * completion callback runs there, never on a MySQL worker thread.
 */
int abe_db_mysql_async_create(
    const abe_db_mysql_async_config_t* config,
    abe_db_mysql_async_t** out_mysql);
void abe_db_mysql_async_destroy(abe_db_mysql_async_t* mysql);

int abe_db_mysql_async_query(
    abe_db_mysql_async_t* mysql,
    const char* sql,
    abe_db_mysql_async_query_callback_fn callback,
    void* user_data);

int abe_db_mysql_async_execute(
    abe_db_mysql_async_t* mysql,
    const char* sql,
    abe_db_mysql_async_execute_callback_fn callback,
    void* user_data);

/* Dispatches at most max_count completions. max_count == 0 dispatches all. */
int abe_db_mysql_async_update(
    abe_db_mysql_async_t* mysql,
    uint32_t max_count,
    uint32_t* out_count);

uint32_t abe_db_mysql_async_pending_count(const abe_db_mysql_async_t* mysql);
const char* abe_db_mysql_async_last_error(const abe_db_mysql_async_t* mysql);

uint32_t abe_db_mysql_async_result_column_count(const abe_db_mysql_async_result_t* result);
uint64_t abe_db_mysql_async_result_row_count(const abe_db_mysql_async_result_t* result);
const char* abe_db_mysql_async_result_column_name(
    const abe_db_mysql_async_result_t* result,
    uint32_t column);
int abe_db_mysql_async_result_is_null(
    const abe_db_mysql_async_result_t* result,
    uint64_t row,
    uint32_t column,
    int* out_is_null);
const void* abe_db_mysql_async_result_get_blob(
    const abe_db_mysql_async_result_t* result,
    uint64_t row,
    uint32_t column,
    uint64_t* out_size);

#ifdef __cplusplus
}
#endif

#endif /* ABE_DB_MYSQL_ASYNC_H */
