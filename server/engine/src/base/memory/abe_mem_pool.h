#ifndef ABE_MEM_POOL_H
#define ABE_MEM_POOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abe_mem_pool abe_mem_pool_t;

typedef enum abe_mem_pool_status {
    ABE_MEM_POOL_OK = 0,
    ABE_MEM_POOL_ERROR = -1,
    ABE_MEM_POOL_INVALID_ARG = -2,
    ABE_MEM_POOL_NO_MEMORY = -3,
    ABE_MEM_POOL_OUT_OF_RANGE = -4,
    ABE_MEM_POOL_DOUBLE_FREE = -5
} abe_mem_pool_status_t;

typedef struct abe_mem_pool_config {
    uint64_t capacity;
    uint32_t alignment;
    const char* name;
} abe_mem_pool_config_t;

typedef struct abe_mem_pool_stats {
    uint64_t capacity;
    uint64_t used_bytes;
    uint64_t reserved_bytes;
    uint64_t free_bytes;
    uint64_t peak_used_bytes;
    uint32_t used_block_count;
    uint32_t free_block_count;
    uint64_t largest_free_block;
} abe_mem_pool_stats_t;

/*
 * Variable-size local memory pool.
 *
 * alignment 0 means pointer-size alignment. Returned pointers are owned by the
 * pool and remain valid until freed, reset, or destroyed. This pool is not
 * internally synchronized.
 */
int abe_mem_pool_create(const abe_mem_pool_config_t* config, abe_mem_pool_t** out_pool);
void* abe_mem_pool_alloc(abe_mem_pool_t* pool, uint64_t size);
void* abe_mem_pool_calloc(abe_mem_pool_t* pool, uint64_t count, uint64_t size);
int abe_mem_pool_free(abe_mem_pool_t* pool, void* ptr);
void abe_mem_pool_reset(abe_mem_pool_t* pool);
int abe_mem_pool_get_stats(const abe_mem_pool_t* pool, abe_mem_pool_stats_t* out_stats);
void abe_mem_pool_destroy(abe_mem_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* ABE_MEM_POOL_H */
