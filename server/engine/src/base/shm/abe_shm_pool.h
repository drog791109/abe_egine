#ifndef ABE_SHM_POOL_H
#define ABE_SHM_POOL_H

#include "abe_error.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABE_SHM_POOL_INVALID_OFFSET UINT64_MAX

#define ABE_SHM_POOL_CREATE 0x01u
#define ABE_SHM_POOL_EXCLUSIVE 0x02u
#define ABE_SHM_POOL_TRUNCATE 0x04u
#define ABE_SHM_POOL_UNLINK_ON_CLOSE 0x08u

typedef struct abe_shm_pool abe_shm_pool_t;
typedef uint64_t abe_shm_offset_t;

typedef enum abe_shm_pool_status {
    ABE_SHM_POOL_OK = ABE_OK,
    ABE_SHM_POOL_ERROR = ABE_ERROR,
    ABE_SHM_POOL_INVALID_ARG = ABE_INVALID_ARG,
    ABE_SHM_POOL_NO_MEMORY = ABE_NO_MEMORY,
    ABE_SHM_POOL_OUT_OF_RANGE = ABE_OUT_OF_RANGE,
    ABE_SHM_POOL_DOUBLE_FREE = ABE_DOUBLE_FREE,
    ABE_SHM_POOL_EXISTS = ABE_ALREADY_EXISTS,
    ABE_SHM_POOL_NOT_FOUND = ABE_NOT_FOUND
} abe_shm_pool_status_t;

typedef struct abe_shm_pool_config {
    const char* name;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t alignment;
    uint32_t flags;
} abe_shm_pool_config_t;

typedef struct abe_shm_pool_stats {
    uint32_t block_size;
    uint32_t block_count;
    uint32_t used_count;
    uint32_t free_count;
    uint32_t peak_used_count;
    uint64_t total_bytes;
} abe_shm_pool_stats_t;

/*
 * Shared memory blocks must be referenced by abe_shm_offset_t in durable data.
 * Pointers returned by abe_shm_pool_ptr or abe_shm_pool_alloc are valid only in
 * the current process mapping.
 */
int abe_shm_pool_open(const abe_shm_pool_config_t* config, abe_shm_pool_t** out_pool);
void abe_shm_pool_close(abe_shm_pool_t* pool);
int abe_shm_pool_unlink(const char* name);

int abe_shm_pool_alloc(
    abe_shm_pool_t* pool,
    uint32_t size,
    abe_shm_offset_t* out_offset,
    void** out_ptr);
int abe_shm_pool_free(abe_shm_pool_t* pool, abe_shm_offset_t offset);
void* abe_shm_pool_ptr(abe_shm_pool_t* pool, abe_shm_offset_t offset);
int abe_shm_pool_offset(
    abe_shm_pool_t* pool,
    const void* ptr,
    abe_shm_offset_t* out_offset);
int abe_shm_pool_reset(abe_shm_pool_t* pool);
int abe_shm_pool_get_stats(abe_shm_pool_t* pool, abe_shm_pool_stats_t* out_stats);

#ifdef __cplusplus
}
#endif

#endif /* ABE_SHM_POOL_H */
