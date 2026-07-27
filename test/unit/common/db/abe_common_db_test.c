#include "abe_db.h"
#include "abe_db_driver.h"

#include <stdio.h>
#include <string.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

struct fake_db {
    abe_db_t* db;
    int begin_count;
    int commit_count;
    int rollback_count;
    int destroyed;
};

struct fake_result {
    int row_index;
};

static const char* g_fake_columns[] = {"id", "name", "score"};
static const char* g_fake_rows[2][3] = {
    {"42", "alice", NULL},
    {"73", "bob", "12.5"}
};

static int fake_destroy(void* driver_data)
{
    struct fake_db* db;

    db = (struct fake_db*)driver_data;
    if (db != NULL) {
        db->destroyed = 1;
    }
    return ABE_DB_OK;
}

static int fake_ping(void* driver_data)
{
    return driver_data == NULL ? ABE_DB_INVALID_ARG : ABE_DB_OK;
}

static int fake_execute(void* driver_data, const char* sql, uint64_t* out_affected_rows)
{
    (void)driver_data;
    if (sql == NULL) {
        return ABE_DB_INVALID_ARG;
    }
    if (strcmp(sql, "bad") == 0) {
        return ABE_DB_QUERY_FAILED;
    }
    if (out_affected_rows != NULL) {
        *out_affected_rows = 3u;
    }
    return ABE_DB_OK;
}

static int fake_query(void* driver_data, const char* sql, void** out_driver_result)
{
    struct fake_db* db;
    struct fake_result* result;

    if (driver_data == NULL || sql == NULL || out_driver_result == NULL) {
        return ABE_DB_INVALID_ARG;
    }
    if (strcmp(sql, "bad") == 0) {
        return ABE_DB_QUERY_FAILED;
    }

    db = (struct fake_db*)driver_data;
    result = (struct fake_result*)abe_mem_pool_calloc(
        abe_db_memory_pool(db->db),
        1u,
        sizeof(*result));
    if (result == NULL) {
        return ABE_DB_NO_MEMORY;
    }
    result->row_index = -1;
    *out_driver_result = result;
    return ABE_DB_OK;
}

static int fake_begin(void* driver_data)
{
    ((struct fake_db*)driver_data)->begin_count += 1;
    return ABE_DB_OK;
}

static int fake_commit(void* driver_data)
{
    ((struct fake_db*)driver_data)->commit_count += 1;
    return ABE_DB_OK;
}

static int fake_rollback(void* driver_data)
{
    ((struct fake_db*)driver_data)->rollback_count += 1;
    return ABE_DB_OK;
}

static uint64_t fake_last_insert_id(const void* driver_data)
{
    (void)driver_data;
    return 99u;
}

static int fake_escape(
    void* driver_data,
    const char* input,
    uint64_t input_length,
    char* out_buffer,
    uint64_t buffer_size,
    uint64_t* out_length)
{
    uint64_t i;
    uint64_t j;

    (void)driver_data;
    if (buffer_size < input_length * 2u + 1u) {
        if (out_length != NULL) {
            *out_length = input_length * 2u + 1u;
        }
        return ABE_DB_BUFFER_TOO_SMALL;
    }

    j = 0u;
    for (i = 0u; i < input_length; ++i) {
        if (input[i] == '\'') {
            out_buffer[j++] = '\\';
        }
        out_buffer[j++] = input[i];
    }
    out_buffer[j] = '\0';
    if (out_length != NULL) {
        *out_length = j;
    }
    return ABE_DB_OK;
}

static const char* fake_last_error(const void* driver_data)
{
    (void)driver_data;
    return "fake database error";
}

static void fake_result_destroy(void* driver_result)
{
    struct fake_result* result;

    result = (struct fake_result*)driver_result;
    if (result != NULL) {
        result->row_index = -2;
    }
}

static uint32_t fake_column_count(const void* driver_result)
{
    (void)driver_result;
    return 3u;
}

static uint64_t fake_row_count(const void* driver_result)
{
    (void)driver_result;
    return 2u;
}

static const char* fake_column_name(const void* driver_result, uint32_t column)
{
    (void)driver_result;
    if (column >= 3u) {
        return NULL;
    }
    return g_fake_columns[column];
}

static int fake_column_index(const void* driver_result, const char* column_name, int* out_column)
{
    uint32_t i;

    (void)driver_result;
    for (i = 0u; i < 3u; ++i) {
        if (strcmp(g_fake_columns[i], column_name) == 0) {
            *out_column = (int)i;
            return ABE_DB_OK;
        }
    }
    return ABE_DB_OUT_OF_RANGE;
}

static int fake_next(void* driver_result, int* out_has_row)
{
    struct fake_result* result;

    result = (struct fake_result*)driver_result;
    result->row_index += 1;
    *out_has_row = result->row_index < 2;
    return ABE_DB_OK;
}

static int fake_is_null(const void* driver_result, uint32_t column, int* out_is_null)
{
    const struct fake_result* result;

    result = (const struct fake_result*)driver_result;
    if (column >= 3u) {
        return ABE_DB_OUT_OF_RANGE;
    }
    if (result->row_index < 0 || result->row_index >= 2) {
        return ABE_DB_NO_ROW;
    }
    *out_is_null = g_fake_rows[result->row_index][column] == NULL;
    return ABE_DB_OK;
}

static const void* fake_get_data(
    const void* driver_result,
    uint32_t column,
    uint64_t* out_length)
{
    const struct fake_result* result;
    const char* value;

    result = (const struct fake_result*)driver_result;
    if (column >= 3u || result->row_index < 0 || result->row_index >= 2) {
        return NULL;
    }
    value = g_fake_rows[result->row_index][column];
    if (value == NULL) {
        return NULL;
    }
    *out_length = (uint64_t)strlen(value);
    return value;
}

static const abe_db_driver_ops_t g_fake_ops = {
    fake_destroy,
    fake_ping,
    fake_execute,
    fake_query,
    fake_begin,
    fake_commit,
    fake_rollback,
    fake_last_insert_id,
    fake_escape,
    fake_last_error
};

static const abe_db_result_ops_t g_fake_result_ops = {
    fake_result_destroy,
    fake_column_count,
    fake_row_count,
    fake_column_name,
    fake_column_index,
    fake_next,
    fake_is_null,
    fake_get_data
};

static int test_common_db(void)
{
    abe_mem_pool_config_t pool_config;
    abe_mem_pool_t* mem_pool;
    struct fake_db* fake;
    abe_db_t* db;
    abe_db_result_t* result;
    uint64_t affected_rows;
    uint64_t length;
    int64_t id;
    double score;
    int has_row;
    int is_null;
    int column;
    char escaped[32];

    memset(&pool_config, 0, sizeof(pool_config));
    pool_config.capacity = 65536u;
    pool_config.name = "fake_db";
    mem_pool = NULL;
    TEST_REQUIRE(abe_mem_pool_create(&pool_config, &mem_pool) == ABE_MEM_POOL_OK);

    fake = (struct fake_db*)abe_mem_pool_calloc(mem_pool, 1u, sizeof(*fake));
    TEST_REQUIRE(fake != NULL);
    db = NULL;
    TEST_REQUIRE(abe_db_create_driver(
        &g_fake_ops,
        &g_fake_result_ops,
        fake,
        mem_pool,
        1,
        &db) == ABE_DB_OK);
    fake->db = db;

    TEST_REQUIRE(abe_db_ping(db) == ABE_DB_OK);
    TEST_REQUIRE(abe_db_begin(db) == ABE_DB_OK);
    TEST_REQUIRE(abe_db_commit(db) == ABE_DB_OK);
    TEST_REQUIRE(abe_db_rollback(db) == ABE_DB_OK);
    TEST_REQUIRE(fake->begin_count == 1);
    TEST_REQUIRE(fake->commit_count == 1);
    TEST_REQUIRE(fake->rollback_count == 1);
    TEST_REQUIRE(abe_db_last_insert_id(db) == 99u);

    affected_rows = 0u;
    TEST_REQUIRE(abe_db_execute(db, "update t set v=1", &affected_rows) == ABE_DB_OK);
    TEST_REQUIRE(affected_rows == 3u);
    TEST_REQUIRE(abe_db_execute(db, "bad", NULL) == ABE_DB_QUERY_FAILED);
    TEST_REQUIRE(strcmp(abe_db_last_error(db), "fake database error") == 0);

    TEST_REQUIRE(abe_db_escape_string(db, "a'b", 3u, escaped, sizeof(escaped), &length) ==
        ABE_DB_OK);
    TEST_REQUIRE(length == 4u);
    TEST_REQUIRE(strcmp(escaped, "a\\'b") == 0);

    result = NULL;
    TEST_REQUIRE(abe_db_query(db, "select * from t", &result) == ABE_DB_OK);
    TEST_REQUIRE(result != NULL);
    TEST_REQUIRE(abe_db_result_column_count(result) == 3u);
    TEST_REQUIRE(abe_db_result_row_count(result) == 2u);
    TEST_REQUIRE(strcmp(abe_db_result_column_name(result, 1u), "name") == 0);
    TEST_REQUIRE(abe_db_result_column_index(result, "score", &column) == ABE_DB_OK);
    TEST_REQUIRE(column == 2);

    TEST_REQUIRE(abe_db_result_next(result, &has_row) == ABE_DB_OK);
    TEST_REQUIRE(has_row == 1);
    TEST_REQUIRE(abe_db_result_get_i64(result, 0u, &id) == ABE_DB_OK);
    TEST_REQUIRE(id == 42);
    TEST_REQUIRE(strcmp(abe_db_result_get_string(result, 1u, &length), "alice") == 0);
    TEST_REQUIRE(length == 5u);
    TEST_REQUIRE(abe_db_result_is_null(result, 2u, &is_null) == ABE_DB_OK);
    TEST_REQUIRE(is_null == 1);

    TEST_REQUIRE(abe_db_result_next(result, &has_row) == ABE_DB_OK);
    TEST_REQUIRE(has_row == 1);
    TEST_REQUIRE(abe_db_result_get_i64(result, 0u, &id) == ABE_DB_OK);
    TEST_REQUIRE(id == 73);
    TEST_REQUIRE(abe_db_result_get_double(result, 2u, &score) == ABE_DB_OK);
    TEST_REQUIRE(score > 12.49 && score < 12.51);

    TEST_REQUIRE(abe_db_result_next(result, &has_row) == ABE_DB_OK);
    TEST_REQUIRE(has_row == 0);
    abe_db_result_destroy(result);

    abe_db_destroy(db);
    return 0;
}

int main(void)
{
    if (test_common_db() != 0) {
        return 1;
    }
    return 0;
}
