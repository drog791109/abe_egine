#ifndef ABE_DB_DRIVER_H
#define ABE_DB_DRIVER_H

#include "abe_db.h"
#include "abe_mem_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abe_db_driver_ops {
    int (*destroy)(void* driver_data);
    int (*ping)(void* driver_data);
    int (*execute)(void* driver_data, const char* sql, uint64_t* out_affected_rows);
    int (*query)(void* driver_data, const char* sql, void** out_driver_result);
    int (*begin)(void* driver_data);
    int (*commit)(void* driver_data);
    int (*rollback)(void* driver_data);
    uint64_t (*last_insert_id)(const void* driver_data);
    int (*escape_string)(
        void* driver_data,
        const char* input,
        uint64_t input_length,
        char* out_buffer,
        uint64_t buffer_size,
        uint64_t* out_length);
    const char* (*last_error)(const void* driver_data);
} abe_db_driver_ops_t;

typedef struct abe_db_result_ops {
    void (*destroy)(void* driver_result);
    uint32_t (*column_count)(const void* driver_result);
    uint64_t (*row_count)(const void* driver_result);
    const char* (*column_name)(const void* driver_result, uint32_t column);
    int (*column_index)(const void* driver_result, const char* column_name, int* out_column);
    int (*next)(void* driver_result, int* out_has_row);
    int (*is_null)(const void* driver_result, uint32_t column, int* out_is_null);
    const void* (*get_data)(const void* driver_result, uint32_t column, uint64_t* out_length);
} abe_db_result_ops_t;

int abe_db_create_driver(
    const abe_db_driver_ops_t* ops,
    const abe_db_result_ops_t* result_ops,
    void* driver_data,
    abe_mem_pool_t* mem_pool,
    int owns_mem_pool,
    abe_db_t** out_db);

abe_mem_pool_t* abe_db_memory_pool(abe_db_t* db);
void* abe_db_driver_data(abe_db_t* db);
void abe_db_set_last_error(abe_db_t* db, const char* error_message);

#ifdef __cplusplus
}
#endif

#endif /* ABE_DB_DRIVER_H */
