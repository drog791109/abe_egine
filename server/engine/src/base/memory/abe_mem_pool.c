#include "abe_mem_pool.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ABE_MEM_POOL_BLOCK_MAGIC 0x414D5042u
#define ABE_MEM_POOL_BLOCK_FREE 0x46524545u
#define ABE_MEM_POOL_BLOCK_USED 0x55534544u

struct abe_mem_pool_block {
    struct abe_mem_pool_block* prev;
    struct abe_mem_pool_block* next;
    abe_mem_pool_t* owner;
    uint64_t payload_size;
    uint64_t requested_size;
    uint32_t magic;
    uint32_t state;
};

struct abe_mem_pool {
    char name[64];
    unsigned char* raw_memory;
    unsigned char* memory;
    size_t raw_size;
    size_t header_size;
    uint64_t capacity;
    uint64_t used_bytes;
    uint64_t reserved_bytes;
    uint64_t peak_used_bytes;
    uint32_t used_block_count;
    uint32_t alignment;
    struct abe_mem_pool_block* first;
};

static int abe_mem_pool_is_power_of_two(uint64_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static uint64_t abe_mem_pool_align_up_u64(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static size_t abe_mem_pool_align_up_size(size_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static unsigned char* abe_mem_pool_align_up_ptr(unsigned char* ptr, size_t alignment)
{
    uintptr_t value;

    value = (uintptr_t)ptr;
    value = (value + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
    return (unsigned char*)value;
}

static int abe_mem_pool_add_overflow_size(size_t a, size_t b, size_t* out)
{
    if (out == NULL) {
        return 1;
    }
    if (b > ((size_t)-1) - a) {
        return 1;
    }
    *out = a + b;
    return 0;
}

static int abe_mem_pool_u64_to_size(uint64_t value, size_t* out)
{
    if (out == NULL || value > (uint64_t)((size_t)-1)) {
        return 1;
    }
    *out = (size_t)value;
    return 0;
}

static void* abe_mem_pool_payload(struct abe_mem_pool* pool, struct abe_mem_pool_block* block)
{
    return (void*)((unsigned char*)block + pool->header_size);
}

static void abe_mem_pool_init_single_free_block(abe_mem_pool_t* pool)
{
    struct abe_mem_pool_block* block;

    block = (struct abe_mem_pool_block*)pool->memory;
    block->prev = NULL;
    block->next = NULL;
    block->owner = pool;
    block->payload_size = pool->capacity;
    block->requested_size = 0u;
    block->magic = ABE_MEM_POOL_BLOCK_MAGIC;
    block->state = ABE_MEM_POOL_BLOCK_FREE;

    pool->first = block;
    pool->used_bytes = 0u;
    pool->reserved_bytes = 0u;
    pool->used_block_count = 0u;
}

static struct abe_mem_pool_block* abe_mem_pool_find_free_block(
    abe_mem_pool_t* pool,
    uint64_t aligned_size)
{
    struct abe_mem_pool_block* block;

    block = pool->first;
    while (block != NULL) {
        if (block->state == ABE_MEM_POOL_BLOCK_FREE && block->payload_size >= aligned_size) {
            return block;
        }
        block = block->next;
    }

    return NULL;
}

static void abe_mem_pool_split_block(
    abe_mem_pool_t* pool,
    struct abe_mem_pool_block* block,
    uint64_t aligned_size)
{
    struct abe_mem_pool_block* next_block;
    uint64_t remaining;

    if (block->payload_size <= aligned_size) {
        return;
    }

    remaining = block->payload_size - aligned_size;
    if (remaining <= (uint64_t)pool->header_size + (uint64_t)pool->alignment) {
        return;
    }

    next_block = (struct abe_mem_pool_block*)(
        (unsigned char*)abe_mem_pool_payload(pool, block) + (size_t)aligned_size);
    next_block->prev = block;
    next_block->next = block->next;
    next_block->owner = pool;
    next_block->payload_size = remaining - (uint64_t)pool->header_size;
    next_block->requested_size = 0u;
    next_block->magic = ABE_MEM_POOL_BLOCK_MAGIC;
    next_block->state = ABE_MEM_POOL_BLOCK_FREE;

    if (block->next != NULL) {
        block->next->prev = next_block;
    }
    block->next = next_block;
    block->payload_size = aligned_size;
}

static void abe_mem_pool_merge_with_next(
    abe_mem_pool_t* pool,
    struct abe_mem_pool_block* block)
{
    struct abe_mem_pool_block* next_block;

    if (pool == NULL || block == NULL || block->next == NULL) {
        return;
    }

    next_block = block->next;
    if (next_block->state != ABE_MEM_POOL_BLOCK_FREE) {
        return;
    }

    block->payload_size += (uint64_t)pool->header_size + next_block->payload_size;
    block->next = next_block->next;
    if (next_block->next != NULL) {
        next_block->next->prev = block;
    }
}

static struct abe_mem_pool_block* abe_mem_pool_block_from_payload(
    abe_mem_pool_t* pool,
    void* ptr)
{
    unsigned char* target;
    unsigned char* start;
    unsigned char* end;
    struct abe_mem_pool_block* block;

    if (pool == NULL || ptr == NULL) {
        return NULL;
    }

    target = (unsigned char*)ptr;
    start = pool->memory + pool->header_size;
    end = pool->memory + pool->header_size + (size_t)pool->capacity;
    if (target < start || target >= end) {
        return NULL;
    }

    block = (struct abe_mem_pool_block*)(target - pool->header_size);
    if (block->magic != ABE_MEM_POOL_BLOCK_MAGIC ||
        block->owner != pool ||
        abe_mem_pool_payload(pool, block) != ptr) {
        return NULL;
    }

    return block;
}

int abe_mem_pool_create(const abe_mem_pool_config_t* config, abe_mem_pool_t** out_pool)
{
    abe_mem_pool_t* pool;
    uint64_t alignment64;
    uint64_t aligned_capacity64;
    size_t alignment;
    size_t capacity;
    size_t header_size;
    size_t total_size;
    size_t raw_size;

    if (config == NULL || out_pool == NULL) {
        return ABE_MEM_POOL_INVALID_ARG;
    }
    *out_pool = NULL;
    if (config->capacity == 0u) {
        return ABE_MEM_POOL_INVALID_ARG;
    }

    alignment64 = config->alignment == 0u ? (uint64_t)sizeof(void*) : (uint64_t)config->alignment;
    if (alignment64 < (uint64_t)sizeof(void*) || !abe_mem_pool_is_power_of_two(alignment64)) {
        return ABE_MEM_POOL_INVALID_ARG;
    }
    if (abe_mem_pool_u64_to_size(alignment64, &alignment) != 0) {
        return ABE_MEM_POOL_INVALID_ARG;
    }

    aligned_capacity64 = abe_mem_pool_align_up_u64(config->capacity, alignment64);
    if (aligned_capacity64 < config->capacity) {
        return ABE_MEM_POOL_INVALID_ARG;
    }
    if (abe_mem_pool_u64_to_size(aligned_capacity64, &capacity) != 0) {
        return ABE_MEM_POOL_INVALID_ARG;
    }

    header_size = abe_mem_pool_align_up_size(sizeof(struct abe_mem_pool_block), alignment);
    if (abe_mem_pool_add_overflow_size(header_size, capacity, &total_size) != 0) {
        return ABE_MEM_POOL_INVALID_ARG;
    }
    if (abe_mem_pool_add_overflow_size(total_size, alignment - 1u, &raw_size) != 0) {
        return ABE_MEM_POOL_INVALID_ARG;
    }

    pool = (abe_mem_pool_t*)calloc(1u, sizeof(*pool));
    if (pool == NULL) {
        return ABE_MEM_POOL_NO_MEMORY;
    }

    pool->raw_memory = (unsigned char*)malloc(raw_size);
    if (pool->raw_memory == NULL) {
        free(pool);
        return ABE_MEM_POOL_NO_MEMORY;
    }

    if (config->name != NULL) {
        strncpy(pool->name, config->name, sizeof(pool->name) - 1u);
    }
    pool->raw_size = raw_size;
    pool->memory = abe_mem_pool_align_up_ptr(pool->raw_memory, alignment);
    pool->header_size = header_size;
    pool->capacity = aligned_capacity64;
    pool->alignment = (uint32_t)alignment;

    abe_mem_pool_init_single_free_block(pool);
    *out_pool = pool;
    return ABE_MEM_POOL_OK;
}

void* abe_mem_pool_alloc(abe_mem_pool_t* pool, uint64_t size)
{
    struct abe_mem_pool_block* block;
    uint64_t aligned_size;

    if (pool == NULL || size == 0u || size > pool->capacity) {
        return NULL;
    }

    aligned_size = abe_mem_pool_align_up_u64(size, (uint64_t)pool->alignment);
    if (aligned_size < size) {
        return NULL;
    }

    block = abe_mem_pool_find_free_block(pool, aligned_size);
    if (block == NULL) {
        return NULL;
    }

    abe_mem_pool_split_block(pool, block, aligned_size);
    block->state = ABE_MEM_POOL_BLOCK_USED;
    block->requested_size = size;
    block->owner = pool;

    pool->used_bytes += size;
    pool->reserved_bytes += block->payload_size;
    ++pool->used_block_count;
    if (pool->used_bytes > pool->peak_used_bytes) {
        pool->peak_used_bytes = pool->used_bytes;
    }

    return abe_mem_pool_payload(pool, block);
}

void* abe_mem_pool_calloc(abe_mem_pool_t* pool, uint64_t count, uint64_t size)
{
    uint64_t total_size;
    void* ptr;

    if (count != 0u && size > ((uint64_t)-1) / count) {
        return NULL;
    }
    total_size = count * size;
    ptr = abe_mem_pool_alloc(pool, total_size);
    if (ptr != NULL) {
        memset(ptr, 0, (size_t)total_size);
    }

    return ptr;
}

int abe_mem_pool_free(abe_mem_pool_t* pool, void* ptr)
{
    struct abe_mem_pool_block* block;

    if (pool == NULL || ptr == NULL) {
        return ABE_MEM_POOL_INVALID_ARG;
    }

    block = abe_mem_pool_block_from_payload(pool, ptr);
    if (block == NULL) {
        return ABE_MEM_POOL_OUT_OF_RANGE;
    }
    if (block->state != ABE_MEM_POOL_BLOCK_USED) {
        return ABE_MEM_POOL_DOUBLE_FREE;
    }

    if (pool->used_bytes >= block->requested_size) {
        pool->used_bytes -= block->requested_size;
    } else {
        pool->used_bytes = 0u;
    }
    if (pool->reserved_bytes >= block->payload_size) {
        pool->reserved_bytes -= block->payload_size;
    } else {
        pool->reserved_bytes = 0u;
    }
    if (pool->used_block_count > 0u) {
        --pool->used_block_count;
    }

    block->state = ABE_MEM_POOL_BLOCK_FREE;
    block->requested_size = 0u;

    abe_mem_pool_merge_with_next(pool, block);
    if (block->prev != NULL && block->prev->state == ABE_MEM_POOL_BLOCK_FREE) {
        block = block->prev;
        abe_mem_pool_merge_with_next(pool, block);
    }

    return ABE_MEM_POOL_OK;
}

void abe_mem_pool_reset(abe_mem_pool_t* pool)
{
    if (pool == NULL) {
        return;
    }
    abe_mem_pool_init_single_free_block(pool);
}

int abe_mem_pool_get_stats(const abe_mem_pool_t* pool, abe_mem_pool_stats_t* out_stats)
{
    const struct abe_mem_pool_block* block;

    if (pool == NULL || out_stats == NULL) {
        return ABE_MEM_POOL_INVALID_ARG;
    }

    memset(out_stats, 0, sizeof(*out_stats));
    out_stats->capacity = pool->capacity;
    out_stats->used_bytes = pool->used_bytes;
    out_stats->reserved_bytes = pool->reserved_bytes;
    out_stats->peak_used_bytes = pool->peak_used_bytes;
    out_stats->used_block_count = pool->used_block_count;

    block = pool->first;
    while (block != NULL) {
        if (block->state == ABE_MEM_POOL_BLOCK_FREE) {
            ++out_stats->free_block_count;
            out_stats->free_bytes += block->payload_size;
            if (block->payload_size > out_stats->largest_free_block) {
                out_stats->largest_free_block = block->payload_size;
            }
        }
        block = block->next;
    }

    return ABE_MEM_POOL_OK;
}

void abe_mem_pool_destroy(abe_mem_pool_t* pool)
{
    if (pool == NULL) {
        return;
    }
    free(pool->raw_memory);
    pool->raw_memory = NULL;
    pool->memory = NULL;
    free(pool);
}
