#include "abe_db.h"
#include "abe_db_driver.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define ABE_DB_ERROR_MESSAGE_SIZE 512u
#define ABE_DB_DOUBLE_BUFFER_SIZE 128u

struct abe_db {
    const abe_db_driver_ops_t* ops;
    const abe_db_result_ops_t* result_ops;
    void* driver_data;
    abe_mem_pool_t* mem_pool;
    int owns_mem_pool;
    char last_error[ABE_DB_ERROR_MESSAGE_SIZE];
};

struct abe_db_result {
    abe_db_t* db;
    const abe_db_result_ops_t* ops;
    void* driver_result;
    int has_row;
};

static void abe_db_copy_error(char out[ABE_DB_ERROR_MESSAGE_SIZE], const char* message)
{
    if (out == NULL) {
        return;
    }

    if (message == NULL || message[0] == '\0') {
        out[0] = '\0';
        return;
    }

    (void)strncpy(out, message, ABE_DB_ERROR_MESSAGE_SIZE - 1u);
    out[ABE_DB_ERROR_MESSAGE_SIZE - 1u] = '\0';
}

static void abe_db_clear_error(abe_db_t* db)
{
    if (db != NULL) {
        db->last_error[0] = '\0';
    }
}

static int abe_db_set_driver_error(abe_db_t* db, int fallback_status)
{
    const char* error_message;

    if (db == NULL) {
        return fallback_status;
    }

    error_message = NULL;
    if (db->ops != NULL && db->ops->last_error != NULL) {
        error_message = db->ops->last_error(db->driver_data);
    }
    if (error_message == NULL || error_message[0] == '\0') {
        error_message = "database operation failed";
    }
    abe_db_copy_error(db->last_error, error_message);
    return fallback_status;
}

static int abe_db_wrap_result(
    abe_db_t* db,
    void* driver_result,
    abe_db_result_t** out_result)
{
    abe_db_result_t* result;

    if (db == NULL || db->mem_pool == NULL || db->result_ops == NULL || out_result == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    result = (abe_db_result_t*)abe_mem_pool_calloc(db->mem_pool, 1u, sizeof(*result));
    if (result == NULL) {
        if (db->result_ops->destroy != NULL && driver_result != NULL) {
            db->result_ops->destroy(driver_result);
        }
        abe_db_copy_error(db->last_error, "database result wrapper allocation failed");
        return ABE_DB_NO_MEMORY;
    }

    result->db = db;
    result->ops = db->result_ops;
    result->driver_result = driver_result;
    result->has_row = 0;
    *out_result = result;
    return ABE_DB_OK;
}

static int abe_db_is_space_char(char c)
{
    return isspace((unsigned char)c) != 0;
}

static int abe_db_parse_u64_text(const char* data, uint64_t length, uint64_t* out_value)
{
    uint64_t value;
    uint64_t index;
    int saw_digit;

    if (data == NULL || out_value == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    index = 0u;
    while (index < length && abe_db_is_space_char(data[index])) {
        ++index;
    }
    if (index < length && data[index] == '+') {
        ++index;
    }

    value = 0u;
    saw_digit = 0;
    while (index < length && data[index] >= '0' && data[index] <= '9') {
        uint64_t digit;

        digit = (uint64_t)(data[index] - '0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return ABE_DB_BAD_VALUE;
        }
        value = value * 10u + digit;
        saw_digit = 1;
        ++index;
    }

    while (index < length && abe_db_is_space_char(data[index])) {
        ++index;
    }
    if (!saw_digit || index != length) {
        return ABE_DB_BAD_VALUE;
    }

    *out_value = value;
    return ABE_DB_OK;
}

static int abe_db_parse_i64_text(const char* data, uint64_t length, int64_t* out_value)
{
    uint64_t limit;
    uint64_t value;
    uint64_t index;
    int negative;
    int saw_digit;

    if (data == NULL || out_value == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    index = 0u;
    while (index < length && abe_db_is_space_char(data[index])) {
        ++index;
    }

    negative = 0;
    if (index < length && (data[index] == '-' || data[index] == '+')) {
        negative = data[index] == '-';
        ++index;
    }

    limit = negative ? ((uint64_t)INT64_MAX + 1u) : (uint64_t)INT64_MAX;
    value = 0u;
    saw_digit = 0;
    while (index < length && data[index] >= '0' && data[index] <= '9') {
        uint64_t digit;

        digit = (uint64_t)(data[index] - '0');
        if (value > (limit - digit) / 10u) {
            return ABE_DB_BAD_VALUE;
        }
        value = value * 10u + digit;
        saw_digit = 1;
        ++index;
    }

    while (index < length && abe_db_is_space_char(data[index])) {
        ++index;
    }
    if (!saw_digit || index != length) {
        return ABE_DB_BAD_VALUE;
    }

    if (negative) {
        if (value == ((uint64_t)INT64_MAX + 1u)) {
            *out_value = INT64_MIN;
        } else {
            *out_value = -(int64_t)value;
        }
    } else {
        *out_value = (int64_t)value;
    }

    return ABE_DB_OK;
}

int abe_db_create_driver(
    const abe_db_driver_ops_t* ops,
    const abe_db_result_ops_t* result_ops,
    void* driver_data,
    abe_mem_pool_t* mem_pool,
    int owns_mem_pool,
    abe_db_t** out_db)
{
    abe_db_t* db;

    if (ops == NULL || result_ops == NULL || mem_pool == NULL || out_db == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    *out_db = NULL;
    db = (abe_db_t*)abe_mem_pool_calloc(mem_pool, 1u, sizeof(*db));
    if (db == NULL) {
        return ABE_DB_NO_MEMORY;
    }

    db->ops = ops;
    db->result_ops = result_ops;
    db->driver_data = driver_data;
    db->mem_pool = mem_pool;
    db->owns_mem_pool = owns_mem_pool != 0;
    db->last_error[0] = '\0';
    *out_db = db;
    return ABE_DB_OK;
}

abe_mem_pool_t* abe_db_memory_pool(abe_db_t* db)
{
    return db == NULL ? NULL : db->mem_pool;
}

void* abe_db_driver_data(abe_db_t* db)
{
    return db == NULL ? NULL : db->driver_data;
}

void abe_db_set_last_error(abe_db_t* db, const char* error_message)
{
    if (db != NULL) {
        abe_db_copy_error(db->last_error, error_message);
    }
}

int abe_db_ping(abe_db_t* db)
{
    int status;

    if (db == NULL || db->ops == NULL || db->ops->ping == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    abe_db_clear_error(db);
    status = db->ops->ping(db->driver_data);
    if (status != ABE_DB_OK) {
        return abe_db_set_driver_error(db, status);
    }
    return ABE_DB_OK;
}

int abe_db_execute(abe_db_t* db, const char* sql, uint64_t* out_affected_rows)
{
    uint64_t affected_rows;
    int status;

    if (db == NULL || db->ops == NULL || db->ops->execute == NULL || sql == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    abe_db_clear_error(db);
    affected_rows = 0u;
    status = db->ops->execute(db->driver_data, sql, &affected_rows);
    if (status != ABE_DB_OK) {
        return abe_db_set_driver_error(db, status);
    }
    if (out_affected_rows != NULL) {
        *out_affected_rows = affected_rows;
    }
    return ABE_DB_OK;
}

int abe_db_query(abe_db_t* db, const char* sql, abe_db_result_t** out_result)
{
    void* driver_result;
    int status;

    if (out_result != NULL) {
        *out_result = NULL;
    }
    if (db == NULL || db->ops == NULL || db->ops->query == NULL ||
        sql == NULL || out_result == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    abe_db_clear_error(db);
    driver_result = NULL;
    status = db->ops->query(db->driver_data, sql, &driver_result);
    if (status != ABE_DB_OK) {
        return abe_db_set_driver_error(db, status);
    }

    return abe_db_wrap_result(db, driver_result, out_result);
}

int abe_db_begin(abe_db_t* db)
{
    int status;

    if (db == NULL || db->ops == NULL || db->ops->begin == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    abe_db_clear_error(db);
    status = db->ops->begin(db->driver_data);
    if (status != ABE_DB_OK) {
        return abe_db_set_driver_error(db, status);
    }
    return ABE_DB_OK;
}

int abe_db_commit(abe_db_t* db)
{
    int status;

    if (db == NULL || db->ops == NULL || db->ops->commit == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    abe_db_clear_error(db);
    status = db->ops->commit(db->driver_data);
    if (status != ABE_DB_OK) {
        return abe_db_set_driver_error(db, status);
    }
    return ABE_DB_OK;
}

int abe_db_rollback(abe_db_t* db)
{
    int status;

    if (db == NULL || db->ops == NULL || db->ops->rollback == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    abe_db_clear_error(db);
    status = db->ops->rollback(db->driver_data);
    if (status != ABE_DB_OK) {
        return abe_db_set_driver_error(db, status);
    }
    return ABE_DB_OK;
}

uint64_t abe_db_last_insert_id(const abe_db_t* db)
{
    if (db == NULL || db->ops == NULL || db->ops->last_insert_id == NULL) {
        return 0u;
    }
    return db->ops->last_insert_id(db->driver_data);
}

const char* abe_db_last_error(const abe_db_t* db)
{
    if (db == NULL) {
        return "invalid database handle";
    }
    return db->last_error;
}

int abe_db_escape_string(
    abe_db_t* db,
    const char* input,
    uint64_t input_length,
    char* out_buffer,
    uint64_t buffer_size,
    uint64_t* out_length)
{
    int status;

    if (db == NULL || db->ops == NULL || db->ops->escape_string == NULL ||
        out_buffer == NULL || buffer_size == 0u || (input == NULL && input_length != 0u)) {
        return ABE_DB_INVALID_ARG;
    }

    abe_db_clear_error(db);
    status = db->ops->escape_string(
        db->driver_data,
        input == NULL ? "" : input,
        input_length,
        out_buffer,
        buffer_size,
        out_length);
    if (status != ABE_DB_OK) {
        return abe_db_set_driver_error(db, status);
    }
    return ABE_DB_OK;
}

void abe_db_destroy(abe_db_t* db)
{
    abe_mem_pool_t* mem_pool;
    int owns_mem_pool;

    if (db == NULL) {
        return;
    }

    mem_pool = db->mem_pool;
    owns_mem_pool = db->owns_mem_pool;
    if (db->ops != NULL && db->ops->destroy != NULL) {
        (void)db->ops->destroy(db->driver_data);
    }

    if (owns_mem_pool && mem_pool != NULL) {
        abe_mem_pool_destroy(mem_pool);
    } else if (mem_pool != NULL) {
        (void)abe_mem_pool_free(mem_pool, db);
    }
}

uint32_t abe_db_result_column_count(const abe_db_result_t* result)
{
    if (result == NULL || result->ops == NULL || result->ops->column_count == NULL) {
        return 0u;
    }
    return result->ops->column_count(result->driver_result);
}

uint64_t abe_db_result_row_count(const abe_db_result_t* result)
{
    if (result == NULL || result->ops == NULL || result->ops->row_count == NULL) {
        return 0u;
    }
    return result->ops->row_count(result->driver_result);
}

const char* abe_db_result_column_name(const abe_db_result_t* result, uint32_t column)
{
    if (result == NULL || result->ops == NULL || result->ops->column_name == NULL) {
        return NULL;
    }
    return result->ops->column_name(result->driver_result, column);
}

int abe_db_result_column_index(
    const abe_db_result_t* result,
    const char* column_name,
    int* out_column)
{
    if (out_column != NULL) {
        *out_column = -1;
    }
    if (result == NULL || result->ops == NULL || result->ops->column_index == NULL ||
        column_name == NULL || out_column == NULL) {
        return ABE_DB_INVALID_ARG;
    }
    return result->ops->column_index(result->driver_result, column_name, out_column);
}

int abe_db_result_next(abe_db_result_t* result, int* out_has_row)
{
    int status;
    int has_row;

    if (out_has_row != NULL) {
        *out_has_row = 0;
    }
    if (result == NULL || result->ops == NULL || result->ops->next == NULL ||
        out_has_row == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    has_row = 0;
    status = result->ops->next(result->driver_result, &has_row);
    result->has_row = status == ABE_DB_OK && has_row != 0;
    *out_has_row = result->has_row;
    if (status != ABE_DB_OK && result->db != NULL) {
        return abe_db_set_driver_error(result->db, status);
    }
    return status;
}

int abe_db_result_is_null(
    const abe_db_result_t* result,
    uint32_t column,
    int* out_is_null)
{
    if (out_is_null != NULL) {
        *out_is_null = 1;
    }
    if (result == NULL || result->ops == NULL || result->ops->is_null == NULL ||
        out_is_null == NULL) {
        return ABE_DB_INVALID_ARG;
    }
    if (!result->has_row) {
        return ABE_DB_NO_ROW;
    }
    return result->ops->is_null(result->driver_result, column, out_is_null);
}

const void* abe_db_result_get_blob(
    const abe_db_result_t* result,
    uint32_t column,
    uint64_t* out_length)
{
    if (out_length != NULL) {
        *out_length = 0u;
    }
    if (result == NULL || result->ops == NULL || result->ops->get_data == NULL ||
        out_length == NULL || !result->has_row) {
        return NULL;
    }
    return result->ops->get_data(result->driver_result, column, out_length);
}

const char* abe_db_result_get_string(
    const abe_db_result_t* result,
    uint32_t column,
    uint64_t* out_length)
{
    return (const char*)abe_db_result_get_blob(result, column, out_length);
}

int abe_db_result_get_i64(
    const abe_db_result_t* result,
    uint32_t column,
    int64_t* out_value)
{
    const char* data;
    uint64_t length;

    if (out_value == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    data = abe_db_result_get_string(result, column, &length);
    if (data == NULL) {
        return ABE_DB_BAD_VALUE;
    }
    return abe_db_parse_i64_text(data, length, out_value);
}

int abe_db_result_get_u64(
    const abe_db_result_t* result,
    uint32_t column,
    uint64_t* out_value)
{
    const char* data;
    uint64_t length;

    if (out_value == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    data = abe_db_result_get_string(result, column, &length);
    if (data == NULL) {
        return ABE_DB_BAD_VALUE;
    }
    return abe_db_parse_u64_text(data, length, out_value);
}

int abe_db_result_get_double(
    const abe_db_result_t* result,
    uint32_t column,
    double* out_value)
{
    const char* data;
    char buffer[ABE_DB_DOUBLE_BUFFER_SIZE];
    char* end_ptr;
    uint64_t length;

    if (out_value == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    data = abe_db_result_get_string(result, column, &length);
    if (data == NULL || length >= (uint64_t)sizeof(buffer)) {
        return ABE_DB_BAD_VALUE;
    }

    (void)memcpy(buffer, data, (size_t)length);
    buffer[(size_t)length] = '\0';

    errno = 0;
    end_ptr = NULL;
    *out_value = strtod(buffer, &end_ptr);
    if (errno != 0 || end_ptr == buffer) {
        return ABE_DB_BAD_VALUE;
    }
    while (end_ptr != NULL && *end_ptr != '\0') {
        if (!abe_db_is_space_char(*end_ptr)) {
            return ABE_DB_BAD_VALUE;
        }
        ++end_ptr;
    }
    return ABE_DB_OK;
}

void abe_db_result_destroy(abe_db_result_t* result)
{
    abe_mem_pool_t* mem_pool;

    if (result == NULL) {
        return;
    }

    mem_pool = result->db == NULL ? NULL : result->db->mem_pool;
    if (result->ops != NULL && result->ops->destroy != NULL) {
        result->ops->destroy(result->driver_result);
    }
    if (mem_pool != NULL) {
        (void)abe_mem_pool_free(mem_pool, result);
    }
}
