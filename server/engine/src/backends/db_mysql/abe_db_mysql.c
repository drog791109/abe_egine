#include "abe_db_mysql.h"

#include "abe_db_driver.h"

#include <mysql.h>
#include <pthread.h>
#include <string.h>

#define ABE_DB_MYSQL_DEFAULT_PORT 3306u
#define ABE_DB_MYSQL_DEFAULT_POOL_CAPACITY (4u * 1024u * 1024u)
#define ABE_DB_MYSQL_MIN_POOL_CAPACITY (64u * 1024u)
#define ABE_DB_MYSQL_ERROR_SIZE 512u

struct abe_db_mysql {
    abe_db_t* db;
    MYSQL* mysql;
    char last_error[ABE_DB_MYSQL_ERROR_SIZE];
};

struct abe_db_mysql_result {
    struct abe_db_mysql* owner;
    MYSQL_RES* result;
    MYSQL_ROW row;
    unsigned long* lengths;
    MYSQL_FIELD* fields;
    uint32_t field_count;
    uint64_t row_count;
};

static pthread_mutex_t g_abe_db_mysql_library_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_abe_db_mysql_library_refs = 0u;

static void abe_db_mysql_copy_error(struct abe_db_mysql* mysql_db, const char* fallback)
{
    const char* message;

    if (mysql_db == NULL) {
        return;
    }

    message = NULL;
    if (mysql_db->mysql != NULL) {
        message = mysql_error(mysql_db->mysql);
    }
    if (message == NULL || message[0] == '\0') {
        message = fallback == NULL ? "mysql operation failed" : fallback;
    }

    (void)strncpy(mysql_db->last_error, message, sizeof(mysql_db->last_error) - 1u);
    mysql_db->last_error[sizeof(mysql_db->last_error) - 1u] = '\0';
    abe_db_set_last_error(mysql_db->db, mysql_db->last_error);
}

static int abe_db_mysql_real_query(struct abe_db_mysql* mysql_db, const char* sql)
{
    if (mysql_db == NULL || mysql_db->mysql == NULL || sql == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    if (mysql_real_query(mysql_db->mysql, sql, (unsigned long)strlen(sql)) != 0) {
        abe_db_mysql_copy_error(mysql_db, "mysql query failed");
        return ABE_DB_QUERY_FAILED;
    }
    return ABE_DB_OK;
}

static int abe_db_mysql_destroy(void* driver_data)
{
    struct abe_db_mysql* mysql_db;

    mysql_db = (struct abe_db_mysql*)driver_data;
    if (mysql_db != NULL && mysql_db->mysql != NULL) {
        mysql_close(mysql_db->mysql);
        mysql_db->mysql = NULL;
    }
    return ABE_DB_OK;
}

static int abe_db_mysql_ping(void* driver_data)
{
    struct abe_db_mysql* mysql_db;

    mysql_db = (struct abe_db_mysql*)driver_data;
    if (mysql_db == NULL || mysql_db->mysql == NULL) {
        return ABE_DB_NOT_CONNECTED;
    }
    if (mysql_ping(mysql_db->mysql) != 0) {
        abe_db_mysql_copy_error(mysql_db, "mysql ping failed");
        return ABE_DB_NOT_CONNECTED;
    }
    return ABE_DB_OK;
}

static int abe_db_mysql_execute(
    void* driver_data,
    const char* sql,
    uint64_t* out_affected_rows)
{
    struct abe_db_mysql* mysql_db;
    MYSQL_RES* result;
    my_ulonglong affected_rows;
    int status;

    mysql_db = (struct abe_db_mysql*)driver_data;
    if (out_affected_rows != NULL) {
        *out_affected_rows = 0u;
    }

    status = abe_db_mysql_real_query(mysql_db, sql);
    if (status != ABE_DB_OK) {
        return status;
    }

    result = mysql_store_result(mysql_db->mysql);
    if (result != NULL) {
        mysql_free_result(result);
    } else if (mysql_field_count(mysql_db->mysql) != 0u) {
        abe_db_mysql_copy_error(mysql_db, "mysql result retrieval failed");
        return ABE_DB_QUERY_FAILED;
    }

    affected_rows = mysql_affected_rows(mysql_db->mysql);
    if (affected_rows != (my_ulonglong)-1 && out_affected_rows != NULL) {
        *out_affected_rows = (uint64_t)affected_rows;
    }
    return ABE_DB_OK;
}

static int abe_db_mysql_query(void* driver_data, const char* sql, void** out_driver_result)
{
    struct abe_db_mysql* mysql_db;
    struct abe_db_mysql_result* mysql_result;
    abe_mem_pool_t* mem_pool;
    MYSQL_RES* result;
    int status;

    if (out_driver_result != NULL) {
        *out_driver_result = NULL;
    }
    if (driver_data == NULL || out_driver_result == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    mysql_db = (struct abe_db_mysql*)driver_data;
    status = abe_db_mysql_real_query(mysql_db, sql);
    if (status != ABE_DB_OK) {
        return status;
    }

    result = mysql_store_result(mysql_db->mysql);
    if (result == NULL) {
        if (mysql_field_count(mysql_db->mysql) != 0u) {
            abe_db_mysql_copy_error(mysql_db, "mysql result retrieval failed");
        } else {
            abe_db_mysql_copy_error(mysql_db, "mysql query did not return a result set");
        }
        return ABE_DB_QUERY_FAILED;
    }

    mem_pool = abe_db_memory_pool(mysql_db->db);
    mysql_result = (struct abe_db_mysql_result*)abe_mem_pool_calloc(
        mem_pool,
        1u,
        sizeof(*mysql_result));
    if (mysql_result == NULL) {
        mysql_free_result(result);
        abe_db_mysql_copy_error(mysql_db, "mysql result wrapper allocation failed");
        return ABE_DB_NO_MEMORY;
    }

    mysql_result->owner = mysql_db;
    mysql_result->result = result;
    mysql_result->row = NULL;
    mysql_result->lengths = NULL;
    mysql_result->field_count = (uint32_t)mysql_num_fields(result);
    mysql_result->fields = mysql_fetch_fields(result);
    mysql_result->row_count = (uint64_t)mysql_num_rows(result);
    *out_driver_result = mysql_result;
    return ABE_DB_OK;
}

static int abe_db_mysql_begin(void* driver_data)
{
    return abe_db_mysql_execute(driver_data, "BEGIN", NULL);
}

static int abe_db_mysql_commit(void* driver_data)
{
    return abe_db_mysql_execute(driver_data, "COMMIT", NULL);
}

static int abe_db_mysql_rollback(void* driver_data)
{
    return abe_db_mysql_execute(driver_data, "ROLLBACK", NULL);
}

static uint64_t abe_db_mysql_last_insert_id(const void* driver_data)
{
    const struct abe_db_mysql* mysql_db;

    mysql_db = (const struct abe_db_mysql*)driver_data;
    if (mysql_db == NULL || mysql_db->mysql == NULL) {
        return 0u;
    }
    return (uint64_t)mysql_insert_id((MYSQL*)mysql_db->mysql);
}

static int abe_db_mysql_escape_string(
    void* driver_data,
    const char* input,
    uint64_t input_length,
    char* out_buffer,
    uint64_t buffer_size,
    uint64_t* out_length)
{
    struct abe_db_mysql* mysql_db;
    unsigned long escaped_length;
    uint64_t required_capacity;

    mysql_db = (struct abe_db_mysql*)driver_data;
    if (mysql_db == NULL || mysql_db->mysql == NULL ||
        input == NULL || out_buffer == NULL) {
        return ABE_DB_INVALID_ARG;
    }
    if (input_length > (uint64_t)(((unsigned long)-1) / 2u)) {
        return ABE_DB_BAD_VALUE;
    }

    required_capacity = input_length * 2u + 1u;
    if (buffer_size < required_capacity) {
        if (out_length != NULL) {
            *out_length = required_capacity;
        }
        abe_db_mysql_copy_error(mysql_db, "mysql escape output buffer is too small");
        return ABE_DB_BUFFER_TOO_SMALL;
    }

    escaped_length = mysql_real_escape_string(
        mysql_db->mysql,
        out_buffer,
        input,
        (unsigned long)input_length);
    out_buffer[escaped_length] = '\0';
    if (out_length != NULL) {
        *out_length = (uint64_t)escaped_length;
    }
    return ABE_DB_OK;
}

static const char* abe_db_mysql_last_error(const void* driver_data)
{
    const struct abe_db_mysql* mysql_db;

    mysql_db = (const struct abe_db_mysql*)driver_data;
    if (mysql_db == NULL) {
        return "";
    }
    return mysql_db->last_error;
}

static void abe_db_mysql_result_destroy(void* driver_result)
{
    struct abe_db_mysql_result* mysql_result;
    abe_mem_pool_t* mem_pool;

    mysql_result = (struct abe_db_mysql_result*)driver_result;
    if (mysql_result == NULL) {
        return;
    }

    if (mysql_result->result != NULL) {
        mysql_free_result(mysql_result->result);
        mysql_result->result = NULL;
    }

    mem_pool = mysql_result->owner == NULL ? NULL : abe_db_memory_pool(mysql_result->owner->db);
    if (mem_pool != NULL) {
        (void)abe_mem_pool_free(mem_pool, mysql_result);
    }
}

static uint32_t abe_db_mysql_result_column_count(const void* driver_result)
{
    const struct abe_db_mysql_result* mysql_result;

    mysql_result = (const struct abe_db_mysql_result*)driver_result;
    return mysql_result == NULL ? 0u : mysql_result->field_count;
}

static uint64_t abe_db_mysql_result_row_count(const void* driver_result)
{
    const struct abe_db_mysql_result* mysql_result;

    mysql_result = (const struct abe_db_mysql_result*)driver_result;
    return mysql_result == NULL ? 0u : mysql_result->row_count;
}

static const char* abe_db_mysql_result_column_name(
    const void* driver_result,
    uint32_t column)
{
    const struct abe_db_mysql_result* mysql_result;

    mysql_result = (const struct abe_db_mysql_result*)driver_result;
    if (mysql_result == NULL || column >= mysql_result->field_count ||
        mysql_result->fields == NULL) {
        return NULL;
    }
    return mysql_result->fields[column].name;
}

static int abe_db_mysql_result_column_index(
    const void* driver_result,
    const char* column_name,
    int* out_column)
{
    const struct abe_db_mysql_result* mysql_result;
    uint32_t i;

    if (out_column != NULL) {
        *out_column = -1;
    }
    if (driver_result == NULL || column_name == NULL || out_column == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    mysql_result = (const struct abe_db_mysql_result*)driver_result;
    for (i = 0u; i < mysql_result->field_count; ++i) {
        if (mysql_result->fields != NULL &&
            mysql_result->fields[i].name != NULL &&
            strcmp(mysql_result->fields[i].name, column_name) == 0) {
            *out_column = (int)i;
            return ABE_DB_OK;
        }
    }

    return ABE_DB_OUT_OF_RANGE;
}

static int abe_db_mysql_result_next(void* driver_result, int* out_has_row)
{
    struct abe_db_mysql_result* mysql_result;

    if (out_has_row != NULL) {
        *out_has_row = 0;
    }
    if (driver_result == NULL || out_has_row == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    mysql_result = (struct abe_db_mysql_result*)driver_result;
    mysql_result->row = mysql_fetch_row(mysql_result->result);
    if (mysql_result->row == NULL) {
        mysql_result->lengths = NULL;
        *out_has_row = 0;
        if (mysql_result->owner != NULL &&
            mysql_result->owner->mysql != NULL &&
            mysql_errno(mysql_result->owner->mysql) != 0u) {
            abe_db_mysql_copy_error(mysql_result->owner, "mysql row fetch failed");
            return ABE_DB_QUERY_FAILED;
        }
        return ABE_DB_OK;
    }

    mysql_result->lengths = mysql_fetch_lengths(mysql_result->result);
    if (mysql_result->field_count != 0u && mysql_result->lengths == NULL) {
        abe_db_mysql_copy_error(mysql_result->owner, "mysql row length fetch failed");
        return ABE_DB_QUERY_FAILED;
    }

    *out_has_row = 1;
    return ABE_DB_OK;
}

static int abe_db_mysql_result_is_null(
    const void* driver_result,
    uint32_t column,
    int* out_is_null)
{
    const struct abe_db_mysql_result* mysql_result;

    if (out_is_null != NULL) {
        *out_is_null = 1;
    }
    if (driver_result == NULL || out_is_null == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    mysql_result = (const struct abe_db_mysql_result*)driver_result;
    if (column >= mysql_result->field_count) {
        return ABE_DB_OUT_OF_RANGE;
    }
    if (mysql_result->row == NULL) {
        return ABE_DB_NO_ROW;
    }

    *out_is_null = mysql_result->row[column] == NULL;
    return ABE_DB_OK;
}

static const void* abe_db_mysql_result_get_data(
    const void* driver_result,
    uint32_t column,
    uint64_t* out_length)
{
    const struct abe_db_mysql_result* mysql_result;

    if (out_length != NULL) {
        *out_length = 0u;
    }
    if (driver_result == NULL || out_length == NULL) {
        return NULL;
    }

    mysql_result = (const struct abe_db_mysql_result*)driver_result;
    if (mysql_result->row == NULL || column >= mysql_result->field_count ||
        mysql_result->row[column] == NULL) {
        return NULL;
    }

    if (mysql_result->lengths != NULL) {
        *out_length = (uint64_t)mysql_result->lengths[column];
    }
    return mysql_result->row[column];
}

static const abe_db_driver_ops_t g_abe_db_mysql_ops = {
    abe_db_mysql_destroy,
    abe_db_mysql_ping,
    abe_db_mysql_execute,
    abe_db_mysql_query,
    abe_db_mysql_begin,
    abe_db_mysql_commit,
    abe_db_mysql_rollback,
    abe_db_mysql_last_insert_id,
    abe_db_mysql_escape_string,
    abe_db_mysql_last_error
};

static const abe_db_result_ops_t g_abe_db_mysql_result_ops = {
    abe_db_mysql_result_destroy,
    abe_db_mysql_result_column_count,
    abe_db_mysql_result_row_count,
    abe_db_mysql_result_column_name,
    abe_db_mysql_result_column_index,
    abe_db_mysql_result_next,
    abe_db_mysql_result_is_null,
    abe_db_mysql_result_get_data
};

int abe_db_mysql_library_init(void)
{
    int status;

    (void)pthread_mutex_lock(&g_abe_db_mysql_library_mutex);
    status = ABE_DB_OK;
    if (g_abe_db_mysql_library_refs == 0u && mysql_library_init(0, NULL, NULL) != 0) {
        status = ABE_DB_ERROR;
    }
    if (status == ABE_DB_OK) {
        ++g_abe_db_mysql_library_refs;
    }
    (void)pthread_mutex_unlock(&g_abe_db_mysql_library_mutex);
    return status;
}

void abe_db_mysql_library_end(void)
{
    (void)pthread_mutex_lock(&g_abe_db_mysql_library_mutex);
    if (g_abe_db_mysql_library_refs > 0u) {
        --g_abe_db_mysql_library_refs;
        if (g_abe_db_mysql_library_refs == 0u) {
            mysql_library_end();
        }
    }
    (void)pthread_mutex_unlock(&g_abe_db_mysql_library_mutex);
}

int abe_db_mysql_create(const abe_db_mysql_config_t* config, abe_db_t** out_db)
{
    abe_mem_pool_config_t pool_config;
    abe_mem_pool_t* mem_pool;
    struct abe_db_mysql* mysql_db;
    uint64_t pool_capacity;
    unsigned int timeout;
    unsigned int port;
    MYSQL* connected;
    int status;

    if (out_db != NULL) {
        *out_db = NULL;
    }
    if (config == NULL || out_db == NULL || config->user == NULL) {
        return ABE_DB_INVALID_ARG;
    }

    pool_capacity = config->memory_pool_capacity == 0u ?
        ABE_DB_MYSQL_DEFAULT_POOL_CAPACITY :
        config->memory_pool_capacity;
    if (pool_capacity < ABE_DB_MYSQL_MIN_POOL_CAPACITY) {
        pool_capacity = ABE_DB_MYSQL_MIN_POOL_CAPACITY;
    }

    memset(&pool_config, 0, sizeof(pool_config));
    pool_config.capacity = pool_capacity;
    pool_config.alignment = 0u;
    pool_config.name = "abe_db_mysql";

    mem_pool = NULL;
    status = abe_mem_pool_create(&pool_config, &mem_pool);
    if (status != ABE_MEM_POOL_OK) {
        return ABE_DB_NO_MEMORY;
    }

    mysql_db = (struct abe_db_mysql*)abe_mem_pool_calloc(mem_pool, 1u, sizeof(*mysql_db));
    if (mysql_db == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_DB_NO_MEMORY;
    }

    mysql_db->mysql = mysql_init(NULL);
    if (mysql_db->mysql == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_DB_NO_MEMORY;
    }

    if (config->connect_timeout_seconds != 0u) {
        timeout = (unsigned int)config->connect_timeout_seconds;
        (void)mysql_options(mysql_db->mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    }
    if (config->read_timeout_seconds != 0u) {
        timeout = (unsigned int)config->read_timeout_seconds;
        (void)mysql_options(mysql_db->mysql, MYSQL_OPT_READ_TIMEOUT, &timeout);
    }
    if (config->write_timeout_seconds != 0u) {
        timeout = (unsigned int)config->write_timeout_seconds;
        (void)mysql_options(mysql_db->mysql, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
    }
    if (config->charset != NULL && config->charset[0] != '\0') {
        (void)mysql_options(mysql_db->mysql, MYSQL_SET_CHARSET_NAME, config->charset);
    }
    if (config->reconnect) {
        char reconnect;

        reconnect = 1;
        (void)mysql_options(mysql_db->mysql, MYSQL_OPT_RECONNECT, &reconnect);
    }

    port = config->port == 0u ? ABE_DB_MYSQL_DEFAULT_PORT : (unsigned int)config->port;
    connected = mysql_real_connect(
        mysql_db->mysql,
        config->host == NULL || config->host[0] == '\0' ? "127.0.0.1" : config->host,
        config->user,
        config->password == NULL ? "" : config->password,
        config->database == NULL || config->database[0] == '\0' ? NULL : config->database,
        port,
        config->unix_socket == NULL || config->unix_socket[0] == '\0' ? NULL : config->unix_socket,
        config->client_flags);
    if (connected == NULL) {
        abe_db_mysql_copy_error(mysql_db, "mysql connect failed");
        mysql_close(mysql_db->mysql);
        mysql_db->mysql = NULL;
        abe_mem_pool_destroy(mem_pool);
        return ABE_DB_NOT_CONNECTED;
    }

    status = abe_db_create_driver(
        &g_abe_db_mysql_ops,
        &g_abe_db_mysql_result_ops,
        mysql_db,
        mem_pool,
        1,
        out_db);
    if (status != ABE_DB_OK) {
        mysql_close(mysql_db->mysql);
        mysql_db->mysql = NULL;
        abe_mem_pool_destroy(mem_pool);
        return status;
    }

    mysql_db->db = *out_db;
    return ABE_DB_OK;
}
