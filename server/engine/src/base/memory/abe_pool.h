#ifndef ABE_POOL_H
#define ABE_POOL_H

#include "abe_error.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abe_pool abe_pool_t;

typedef enum abe_pool_status {
    ABE_POOL_OK = ABE_OK,
    ABE_POOL_ERROR = ABE_ERROR,
    ABE_POOL_INVALID_ARG = ABE_INVALID_ARG,
    ABE_POOL_NO_MEMORY = ABE_NO_MEMORY,
    ABE_POOL_OUT_OF_RANGE = ABE_OUT_OF_RANGE,
    ABE_POOL_DOUBLE_FREE = ABE_DOUBLE_FREE
} abe_pool_status_t;

typedef struct abe_pool_config {
    uint32_t block_size;
    uint32_t block_count;
    uint32_t alignment;
    const char* name;
} abe_pool_config_t;

typedef struct abe_pool_stats {
    uint32_t block_size;
    uint32_t block_count;
    uint32_t used_count;
    uint32_t free_count;
    uint32_t peak_used_count;
    uint64_t total_bytes;
} abe_pool_stats_t;

/* Create a fixed-size block pool. alignment 0 means pointer-size alignment. */
int abe_pool_create(const abe_pool_config_t* config, abe_pool_t** out_pool);

/* The returned pointer is owned by the pool and remains valid until freed, reset, or destroyed. */
void* abe_pool_alloc(abe_pool_t* pool, uint32_t size);

/* The pointer must come from this pool and must not have been freed already. */
int abe_pool_free(abe_pool_t* pool, void* ptr);

/* Marks every block free. Existing block pointers become invalid for ownership purposes. */
void abe_pool_reset(abe_pool_t* pool);

int abe_pool_get_stats(const abe_pool_t* pool, abe_pool_stats_t* out_stats);
void abe_pool_destroy(abe_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* ABE_POOL_H */
