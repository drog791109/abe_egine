#ifndef ABE_DB_H
#define ABE_DB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abe_db abe_db_t;
typedef struct abe_db_result abe_db_result_t;

typedef enum abe_db_status {
    ABE_DB_OK = 0,
    ABE_DB_ERROR = -1,
    ABE_DB_INVALID_ARG = -2,
    ABE_DB_NO_MEMORY = -3,
    ABE_DB_NOT_CONNECTED = -4,
    ABE_DB_QUERY_FAILED = -5,
    ABE_DB_NO_ROW = -6,
    ABE_DB_OUT_OF_RANGE = -7,
    ABE_DB_BAD_VALUE = -8,
    ABE_DB_BUFFER_TOO_SMALL = -9,
    ABE_DB_UNSUPPORTED = -10
} abe_db_status_t;

typedef struct abe_db_async_query_result {
    int status;
    abe_db_result_t* result;
    const char* error_message;
} abe_db_async_query_result_t;

typedef struct abe_db_async_execute_result {
    int status;
    uint64_t affected_rows;
    const char* error_message;
} abe_db_async_execute_result_t;

typedef void (*abe_db_query_callback_fn)(
    abe_db_t* db,
    const abe_db_async_query_result_t* result,
    void* user_data);

typedef void (*abe_db_execute_callback_fn)(
    abe_db_t* db,
    const abe_db_async_execute_result_t* result,
    void* user_data);

int abe_db_ping(abe_db_t* db);
int abe_db_execute(abe_db_t* db, const char* sql, uint64_t* out_affected_rows);
int abe_db_query(abe_db_t* db, const char* sql, abe_db_result_t** out_result);
int abe_db_begin(abe_db_t* db);
int abe_db_commit(abe_db_t* db);
int abe_db_rollback(abe_db_t* db);
uint64_t abe_db_last_insert_id(const abe_db_t* db);
const char* abe_db_last_error(const abe_db_t* db);

/*
 * Escape a string literal body for the active connection character set.
 * The caller owns out_buffer. Its capacity must be at least input_length * 2 + 1.
 * On success out_length receives the escaped byte count, excluding the trailing zero.
 */
int abe_db_escape_string(
    abe_db_t* db,
    const char* input,
    uint64_t input_length,
    char* out_buffer,
    uint64_t buffer_size,
    uint64_t* out_length);

/*
 * The default async helpers may complete before returning. If a query callback
 * receives result != NULL, the callback owns it and must destroy it.
 */
int abe_db_query_async(
    abe_db_t* db,
    const char* sql,
    abe_db_query_callback_fn callback,
    void* user_data);

int abe_db_execute_async(
    abe_db_t* db,
    const char* sql,
    abe_db_execute_callback_fn callback,
    void* user_data);

void abe_db_destroy(abe_db_t* db);

uint32_t abe_db_result_column_count(const abe_db_result_t* result);
uint64_t abe_db_result_row_count(const abe_db_result_t* result);
const char* abe_db_result_column_name(const abe_db_result_t* result, uint32_t column);
int abe_db_result_column_index(
    const abe_db_result_t* result,
    const char* column_name,
    int* out_column);

/*
 * Move to the next row. out_has_row is set to 1 when getters can read the row,
 * or 0 when iteration is complete.
 */
int abe_db_result_next(abe_db_result_t* result, int* out_has_row);

int abe_db_result_is_null(
    const abe_db_result_t* result,
    uint32_t column,
    int* out_is_null);

/*
 * Returned data is owned by the result object and remains valid until the next
 * abe_db_result_next call or until the result is destroyed.
 */
const void* abe_db_result_get_blob(
    const abe_db_result_t* result,
    uint32_t column,
    uint64_t* out_length);

const char* abe_db_result_get_string(
    const abe_db_result_t* result,
    uint32_t column,
    uint64_t* out_length);

int abe_db_result_get_i64(
    const abe_db_result_t* result,
    uint32_t column,
    int64_t* out_value);

int abe_db_result_get_u64(
    const abe_db_result_t* result,
    uint32_t column,
    uint64_t* out_value);

int abe_db_result_get_double(
    const abe_db_result_t* result,
    uint32_t column,
    double* out_value);

void abe_db_result_destroy(abe_db_result_t* result);

#ifdef __cplusplus
}
#endif

#endif /* ABE_DB_H */
