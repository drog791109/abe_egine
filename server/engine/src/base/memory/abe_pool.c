#include "abe_pool.h"

#include <stdlib.h>
#include <string.h>

#define ABE_POOL_BLOCK_FREE 0x46524545u
#define ABE_POOL_BLOCK_USED 0x55534544u

struct abe_pool_block_header {
    struct abe_pool_block_header* next;
    abe_pool_t* owner;
    uint32_t state;
};

struct abe_pool {
    char name[64];
    unsigned char* raw_memory;
    unsigned char* memory;
    size_t raw_size;
    size_t header_size;
    size_t payload_size;
    size_t block_stride;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t alignment;
    uint32_t used_count;
    uint32_t peak_used_count;
    struct abe_pool_block_header* free_list;
};

static int abe_pool_is_power_of_two(size_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static size_t abe_pool_align_up_size(size_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static unsigned char* abe_pool_align_up_ptr(unsigned char* ptr, size_t alignment)
{
    uintptr_t value;

    value = (uintptr_t)ptr;
    value = (value + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
    return (unsigned char*)value;
}

static int abe_pool_mul_overflow(size_t a, size_t b, size_t* out)
{
    if (out == NULL) {
        return 1;
    }
    if (a != 0u && b > ((size_t)-1) / a) {
        return 1;
    }
    *out = a * b;
    return 0;
}

static int abe_pool_add_overflow(size_t a, size_t b, size_t* out)
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

static int abe_pool_compute_layout(
    const abe_pool_config_t* config,
    uint32_t* out_alignment,
    size_t* out_header_size,
    size_t* out_payload_size,
    size_t* out_block_stride,
    size_t* out_total_size,
    size_t* out_raw_size)
{
    size_t alignment;
    size_t header_size;
    size_t payload_size;
    size_t block_stride;
    size_t total_size;
    size_t raw_size;

    if (config == NULL || out_alignment == NULL || out_header_size == NULL ||
        out_payload_size == NULL || out_block_stride == NULL ||
        out_total_size == NULL || out_raw_size == NULL) {
        return ABE_POOL_INVALID_ARG;
    }
    if (config->block_size == 0u || config->block_count == 0u) {
        return ABE_POOL_INVALID_ARG;
    }

    alignment = config->alignment == 0u ? sizeof(void*) : (size_t)config->alignment;
    if (alignment < sizeof(void*) || !abe_pool_is_power_of_two(alignment)) {
        return ABE_POOL_INVALID_ARG;
    }

    header_size = abe_pool_align_up_size(sizeof(struct abe_pool_block_header), alignment);
    payload_size = abe_pool_align_up_size((size_t)config->block_size, alignment);
    if (abe_pool_add_overflow(header_size, payload_size, &block_stride) != 0) {
        return ABE_POOL_INVALID_ARG;
    }
    if (abe_pool_mul_overflow(block_stride, (size_t)config->block_count, &total_size) != 0) {
        return ABE_POOL_INVALID_ARG;
    }
    if (abe_pool_add_overflow(total_size, alignment - 1u, &raw_size) != 0) {
        return ABE_POOL_INVALID_ARG;
    }

    *out_alignment = (uint32_t)alignment;
    *out_header_size = header_size;
    *out_payload_size = payload_size;
    *out_block_stride = block_stride;
    *out_total_size = total_size;
    *out_raw_size = raw_size;
    return ABE_POOL_OK;
}

static struct abe_pool_block_header* abe_pool_block_at(abe_pool_t* pool, uint32_t index)
{
    return (struct abe_pool_block_header*)(pool->memory + ((size_t)index * pool->block_stride));
}

static void* abe_pool_payload_from_block(abe_pool_t* pool, struct abe_pool_block_header* block)
{
    return (void*)((unsigned char*)block + pool->header_size);
}

static struct abe_pool_block_header* abe_pool_block_from_payload(abe_pool_t* pool, void* ptr)
{
    unsigned char* payload;
    unsigned char* start;
    unsigned char* end;
    size_t offset;

    if (pool == NULL || ptr == NULL) {
        return NULL;
    }

    payload = (unsigned char*)ptr;
    start = pool->memory + pool->header_size;
    end = pool->memory + ((size_t)pool->block_count * pool->block_stride);
    if (payload < start || payload >= end) {
        return NULL;
    }

    offset = (size_t)(payload - pool->memory);
    if ((offset % pool->block_stride) != pool->header_size) {
        return NULL;
    }

    return (struct abe_pool_block_header*)(payload - pool->header_size);
}

static void abe_pool_init_free_list(abe_pool_t* pool)
{
    uint32_t index;

    pool->free_list = NULL;
    pool->used_count = 0u;

    index = pool->block_count;
    while (index > 0u) {
        struct abe_pool_block_header* block;

        --index;
        block = abe_pool_block_at(pool, index);
        block->owner = pool;
        block->state = ABE_POOL_BLOCK_FREE;
        block->next = pool->free_list;
        pool->free_list = block;
    }
}

int abe_pool_create(const abe_pool_config_t* config, abe_pool_t** out_pool)
{
    abe_pool_t* pool;
    size_t header_size;
    size_t payload_size;
    size_t block_stride;
    size_t total_size;
    size_t raw_size;
    uint32_t alignment;
    int rc;

    if (out_pool == NULL) {
        return ABE_POOL_INVALID_ARG;
    }
    *out_pool = NULL;

    rc = abe_pool_compute_layout(
        config,
        &alignment,
        &header_size,
        &payload_size,
        &block_stride,
        &total_size,
        &raw_size);
    if (rc != ABE_POOL_OK) {
        return rc;
    }

    pool = (abe_pool_t*)calloc(1u, sizeof(*pool));
    if (pool == NULL) {
        return ABE_POOL_NO_MEMORY;
    }

    pool->raw_memory = (unsigned char*)malloc(raw_size);
    if (pool->raw_memory == NULL) {
        free(pool);
        return ABE_POOL_NO_MEMORY;
    }

    if (config->name != NULL) {
        strncpy(pool->name, config->name, sizeof(pool->name) - 1u);
    }
    pool->raw_size = raw_size;
    pool->memory = abe_pool_align_up_ptr(pool->raw_memory, (size_t)alignment);
    pool->header_size = header_size;
    pool->payload_size = payload_size;
    pool->block_stride = block_stride;
    pool->block_size = config->block_size;
    pool->block_count = config->block_count;
    pool->alignment = alignment;
    (void)total_size;

    abe_pool_init_free_list(pool);

    *out_pool = pool;
    return ABE_POOL_OK;
}

void* abe_pool_alloc(abe_pool_t* pool, uint32_t size)
{
    struct abe_pool_block_header* block;

    if (pool == NULL || size > pool->block_size || pool->free_list == NULL) {
        return NULL;
    }

    block = pool->free_list;
    pool->free_list = block->next;
    block->next = NULL;
    block->state = ABE_POOL_BLOCK_USED;

    ++pool->used_count;
    if (pool->used_count > pool->peak_used_count) {
        pool->peak_used_count = pool->used_count;
    }

    return abe_pool_payload_from_block(pool, block);
}

int abe_pool_free(abe_pool_t* pool, void* ptr)
{
    struct abe_pool_block_header* block;

    if (pool == NULL || ptr == NULL) {
        return ABE_POOL_INVALID_ARG;
    }

    block = abe_pool_block_from_payload(pool, ptr);
    if (block == NULL || block->owner != pool) {
        return ABE_POOL_OUT_OF_RANGE;
    }
    if (block->state != ABE_POOL_BLOCK_USED) {
        return ABE_POOL_DOUBLE_FREE;
    }

    block->state = ABE_POOL_BLOCK_FREE;
    block->next = pool->free_list;
    pool->free_list = block;
    if (pool->used_count > 0u) {
        --pool->used_count;
    }

    return ABE_POOL_OK;
}

void abe_pool_reset(abe_pool_t* pool)
{
    if (pool == NULL) {
        return;
    }
    abe_pool_init_free_list(pool);
}

int abe_pool_get_stats(const abe_pool_t* pool, abe_pool_stats_t* out_stats)
{
    if (pool == NULL || out_stats == NULL) {
        return ABE_POOL_INVALID_ARG;
    }

    memset(out_stats, 0, sizeof(*out_stats));
    out_stats->block_size = pool->block_size;
    out_stats->block_count = pool->block_count;
    out_stats->used_count = pool->used_count;
    out_stats->free_count = pool->block_count - pool->used_count;
    out_stats->peak_used_count = pool->peak_used_count;
    out_stats->total_bytes = (uint64_t)((size_t)pool->block_count * pool->block_stride);

    return ABE_POOL_OK;
}

void abe_pool_destroy(abe_pool_t* pool)
{
    if (pool == NULL) {
        return;
    }
    free(pool->raw_memory);
    pool->raw_memory = NULL;
    pool->memory = NULL;
    free(pool);
}
