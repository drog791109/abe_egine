#define _POSIX_C_SOURCE 200809L

#include "abe_shm_pool.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define ABE_SHM_POOL_MAGIC 0x41425350u
#define ABE_SHM_POOL_VERSION 1u
#define ABE_SHM_POOL_FREE_INDEX UINT32_MAX
#define ABE_SHM_POOL_BLOCK_FREE 0x46524545u
#define ABE_SHM_POOL_BLOCK_USED 0x55534544u
#define ABE_SHM_POOL_NAME_SIZE 128u

struct abe_shm_pool_header {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t alignment;
    uint32_t header_size;
    uint32_t block_header_size;
    uint32_t block_stride;
    uint64_t total_size;
    uint64_t data_offset;
    uint32_t free_head;
    uint32_t used_count;
    uint32_t peak_used_count;
    pthread_mutex_t mutex;
};

struct abe_shm_pool_block_header {
    uint32_t state;
    uint32_t next_index;
};

struct abe_shm_pool {
    char name[ABE_SHM_POOL_NAME_SIZE];
    int fd;
    void* mapping;
    size_t mapping_size;
    uint32_t flags;
};

static int abe_shm_pool_is_power_of_two(size_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static size_t abe_shm_pool_align_up_size(size_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static struct abe_shm_pool_header* abe_shm_pool_header(abe_shm_pool_t* pool)
{
    if (pool == NULL || pool->mapping == NULL) {
        return NULL;
    }
    return (struct abe_shm_pool_header*)pool->mapping;
}

static int abe_shm_pool_normalize_name(const char* name, char out_name[ABE_SHM_POOL_NAME_SIZE])
{
    size_t len;
    const char* body;

    if (name == NULL || name[0] == '\0' || out_name == NULL) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    memset(out_name, 0, ABE_SHM_POOL_NAME_SIZE);
    if (name[0] == '/') {
        len = strlen(name);
        body = name + 1;
        if (len < 2u || len >= ABE_SHM_POOL_NAME_SIZE || strchr(body, '/') != NULL) {
            return ABE_SHM_POOL_INVALID_ARG;
        }
        memcpy(out_name, name, len + 1u);
        return ABE_SHM_POOL_OK;
    }

    if (strchr(name, '/') != NULL) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    len = strlen(name);
    if (len + 2u > ABE_SHM_POOL_NAME_SIZE) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    out_name[0] = '/';
    memcpy(out_name + 1, name, len + 1u);
    return ABE_SHM_POOL_OK;
}

static int abe_shm_pool_mul_overflow(size_t a, size_t b, size_t* out)
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

static int abe_shm_pool_add_overflow(size_t a, size_t b, size_t* out)
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

static int abe_shm_pool_compute_layout(
    uint32_t block_size,
    uint32_t block_count,
    uint32_t configured_alignment,
    uint32_t* out_alignment,
    size_t* out_header_size,
    size_t* out_block_header_size,
    size_t* out_block_stride,
    size_t* out_total_size)
{
    size_t alignment;
    size_t header_size;
    size_t block_header_size;
    size_t payload_size;
    size_t block_stride;
    size_t data_bytes;
    size_t total_size;

    if (out_alignment == NULL || out_header_size == NULL || out_block_header_size == NULL ||
        out_block_stride == NULL || out_total_size == NULL) {
        return ABE_SHM_POOL_INVALID_ARG;
    }
    if (block_size == 0u || block_count == 0u) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    alignment = configured_alignment == 0u ? sizeof(void*) : (size_t)configured_alignment;
    if (alignment < sizeof(void*) || !abe_shm_pool_is_power_of_two(alignment)) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    header_size = abe_shm_pool_align_up_size(sizeof(struct abe_shm_pool_header), alignment);
    block_header_size = abe_shm_pool_align_up_size(sizeof(struct abe_shm_pool_block_header), alignment);
    payload_size = abe_shm_pool_align_up_size((size_t)block_size, alignment);
    if (abe_shm_pool_add_overflow(block_header_size, payload_size, &block_stride) != 0) {
        return ABE_SHM_POOL_INVALID_ARG;
    }
    if (abe_shm_pool_mul_overflow(block_stride, (size_t)block_count, &data_bytes) != 0) {
        return ABE_SHM_POOL_INVALID_ARG;
    }
    if (abe_shm_pool_add_overflow(header_size, data_bytes, &total_size) != 0) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    *out_alignment = (uint32_t)alignment;
    *out_header_size = header_size;
    *out_block_header_size = block_header_size;
    *out_block_stride = block_stride;
    *out_total_size = total_size;
    return ABE_SHM_POOL_OK;
}

static struct abe_shm_pool_block_header* abe_shm_pool_block_at(
    abe_shm_pool_t* pool,
    uint32_t index)
{
    struct abe_shm_pool_header* header;
    unsigned char* base;

    header = abe_shm_pool_header(pool);
    base = (unsigned char*)pool->mapping;
    return (struct abe_shm_pool_block_header*)(
        base + (size_t)header->data_offset + ((size_t)index * header->block_stride));
}

static abe_shm_offset_t abe_shm_pool_payload_offset(
    const struct abe_shm_pool_header* header,
    uint32_t index)
{
    return (abe_shm_offset_t)(
        header->data_offset +
        ((uint64_t)index * (uint64_t)header->block_stride) +
        header->block_header_size);
}

static int abe_shm_pool_block_index_from_offset(
    const struct abe_shm_pool_header* header,
    abe_shm_offset_t offset,
    uint32_t* out_index)
{
    uint64_t diff;
    uint64_t index;

    if (header == NULL || out_index == NULL) {
        return ABE_SHM_POOL_INVALID_ARG;
    }
    if (offset < header->data_offset + header->block_header_size) {
        return ABE_SHM_POOL_OUT_OF_RANGE;
    }

    diff = offset - header->data_offset;
    if ((diff % header->block_stride) != header->block_header_size) {
        return ABE_SHM_POOL_OUT_OF_RANGE;
    }

    index = diff / header->block_stride;
    if (index >= header->block_count) {
        return ABE_SHM_POOL_OUT_OF_RANGE;
    }

    *out_index = (uint32_t)index;
    return ABE_SHM_POOL_OK;
}

static void abe_shm_pool_init_blocks(abe_shm_pool_t* pool)
{
    struct abe_shm_pool_header* header;
    uint32_t index;

    header = abe_shm_pool_header(pool);
    header->free_head = 0u;
    header->used_count = 0u;

    index = 0u;
    while (index < header->block_count) {
        struct abe_shm_pool_block_header* block;

        block = abe_shm_pool_block_at(pool, index);
        block->state = ABE_SHM_POOL_BLOCK_FREE;
        block->next_index = index + 1u < header->block_count ?
            index + 1u :
            ABE_SHM_POOL_FREE_INDEX;
        ++index;
    }
}

static int abe_shm_pool_lock(abe_shm_pool_t* pool)
{
    struct abe_shm_pool_header* header;
    int rc;

    header = abe_shm_pool_header(pool);
    if (header == NULL) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    rc = pthread_mutex_lock(&header->mutex);
    return rc == 0 ? ABE_SHM_POOL_OK : ABE_SHM_POOL_ERROR;
}

static void abe_shm_pool_unlock(abe_shm_pool_t* pool)
{
    struct abe_shm_pool_header* header;

    header = abe_shm_pool_header(pool);
    if (header != NULL) {
        (void)pthread_mutex_unlock(&header->mutex);
    }
}

static int abe_shm_pool_verify_header(abe_shm_pool_t* pool)
{
    struct abe_shm_pool_header* header;

    header = abe_shm_pool_header(pool);
    if (header == NULL) {
        return ABE_SHM_POOL_INVALID_ARG;
    }
    if (header->magic != ABE_SHM_POOL_MAGIC || header->version != ABE_SHM_POOL_VERSION) {
        return ABE_SHM_POOL_ERROR;
    }
    if (header->total_size == 0u || header->total_size > (uint64_t)pool->mapping_size) {
        return ABE_SHM_POOL_ERROR;
    }
    return ABE_SHM_POOL_OK;
}

static int abe_shm_pool_initialize_mapping(
    abe_shm_pool_t* pool,
    const abe_shm_pool_config_t* config)
{
    struct abe_shm_pool_header* header;
    pthread_mutexattr_t mutex_attr;
    size_t header_size;
    size_t block_header_size;
    size_t block_stride;
    size_t total_size;
    uint32_t alignment;
    int rc;

    rc = abe_shm_pool_compute_layout(
        config->block_size,
        config->block_count,
        config->alignment,
        &alignment,
        &header_size,
        &block_header_size,
        &block_stride,
        &total_size);
    if (rc != ABE_SHM_POOL_OK) {
        return rc;
    }

    memset(pool->mapping, 0, pool->mapping_size);
    header = abe_shm_pool_header(pool);
    header->version = ABE_SHM_POOL_VERSION;
    header->block_size = config->block_size;
    header->block_count = config->block_count;
    header->alignment = alignment;
    header->header_size = (uint32_t)header_size;
    header->block_header_size = (uint32_t)block_header_size;
    header->block_stride = (uint32_t)block_stride;
    header->total_size = (uint64_t)total_size;
    header->data_offset = (uint64_t)header_size;
    header->free_head = ABE_SHM_POOL_FREE_INDEX;

    rc = pthread_mutexattr_init(&mutex_attr);
    if (rc != 0) {
        return ABE_SHM_POOL_ERROR;
    }
    rc = pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    if (rc == 0) {
        rc = pthread_mutex_init(&header->mutex, &mutex_attr);
    }
    (void)pthread_mutexattr_destroy(&mutex_attr);
    if (rc != 0) {
        return ABE_SHM_POOL_ERROR;
    }

    abe_shm_pool_init_blocks(pool);
    header->magic = ABE_SHM_POOL_MAGIC;
    return ABE_SHM_POOL_OK;
}

static int abe_shm_pool_map_fd(
    abe_shm_pool_t* pool,
    size_t mapping_size)
{
    void* mapping;

    mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd, 0);
    if (mapping == MAP_FAILED) {
        pool->mapping = NULL;
        return ABE_SHM_POOL_ERROR;
    }

    pool->mapping = mapping;
    pool->mapping_size = mapping_size;
    return ABE_SHM_POOL_OK;
}

int abe_shm_pool_open(const abe_shm_pool_config_t* config, abe_shm_pool_t** out_pool)
{
    abe_shm_pool_t* pool;
    struct stat stat_buf;
    char name[ABE_SHM_POOL_NAME_SIZE];
    size_t header_size;
    size_t block_header_size;
    size_t block_stride;
    size_t total_size;
    uint32_t alignment;
    int oflag;
    int rc;
    int need_init;

    if (config == NULL || out_pool == NULL) {
        return ABE_SHM_POOL_INVALID_ARG;
    }
    *out_pool = NULL;

    rc = abe_shm_pool_normalize_name(config->name, name);
    if (rc != ABE_SHM_POOL_OK) {
        return rc;
    }

    oflag = O_RDWR;
    if ((config->flags & ABE_SHM_POOL_CREATE) != 0u) {
        oflag |= O_CREAT;
    }
    if ((config->flags & ABE_SHM_POOL_EXCLUSIVE) != 0u) {
        oflag |= O_EXCL;
    }

    pool = (abe_shm_pool_t*)calloc(1u, sizeof(*pool));
    if (pool == NULL) {
        return ABE_SHM_POOL_NO_MEMORY;
    }
    pool->fd = -1;
    memcpy(pool->name, name, sizeof(pool->name));
    pool->fd = shm_open(name, oflag, 0600);
    if (pool->fd < 0) {
        rc = errno == EEXIST ? ABE_SHM_POOL_EXISTS :
            (errno == ENOENT ? ABE_SHM_POOL_NOT_FOUND : ABE_SHM_POOL_ERROR);
        free(pool);
        return rc;
    }
    pool->flags = config->flags;

    if (fstat(pool->fd, &stat_buf) != 0) {
        abe_shm_pool_close(pool);
        return ABE_SHM_POOL_ERROR;
    }

    need_init = (config->flags & ABE_SHM_POOL_TRUNCATE) != 0u || stat_buf.st_size == 0;
    if (need_init) {
        rc = abe_shm_pool_compute_layout(
            config->block_size,
            config->block_count,
            config->alignment,
            &alignment,
            &header_size,
            &block_header_size,
            &block_stride,
            &total_size);
        (void)alignment;
        (void)header_size;
        (void)block_header_size;
        (void)block_stride;
        if (rc != ABE_SHM_POOL_OK) {
            abe_shm_pool_close(pool);
            return rc;
        }
        if (ftruncate(pool->fd, (off_t)total_size) != 0) {
            abe_shm_pool_close(pool);
            return ABE_SHM_POOL_ERROR;
        }
        rc = abe_shm_pool_map_fd(pool, total_size);
        if (rc != ABE_SHM_POOL_OK) {
            abe_shm_pool_close(pool);
            return rc;
        }
        rc = abe_shm_pool_initialize_mapping(pool, config);
        if (rc != ABE_SHM_POOL_OK) {
            abe_shm_pool_close(pool);
            return rc;
        }
    } else {
        if (stat_buf.st_size <= 0) {
            abe_shm_pool_close(pool);
            return ABE_SHM_POOL_ERROR;
        }
        rc = abe_shm_pool_map_fd(pool, (size_t)stat_buf.st_size);
        if (rc != ABE_SHM_POOL_OK) {
            abe_shm_pool_close(pool);
            return rc;
        }
        rc = abe_shm_pool_verify_header(pool);
        if (rc != ABE_SHM_POOL_OK) {
            abe_shm_pool_close(pool);
            return rc;
        }
    }

    *out_pool = pool;
    return ABE_SHM_POOL_OK;
}

void abe_shm_pool_close(abe_shm_pool_t* pool)
{
    if (pool == NULL) {
        return;
    }
    if (pool->mapping != NULL) {
        (void)munmap(pool->mapping, pool->mapping_size);
        pool->mapping = NULL;
    }
    if (pool->fd >= 0) {
        (void)close(pool->fd);
        pool->fd = -1;
    }
    if ((pool->flags & ABE_SHM_POOL_UNLINK_ON_CLOSE) != 0u) {
        (void)shm_unlink(pool->name);
    }
    free(pool);
}

int abe_shm_pool_unlink(const char* name)
{
    char normalized_name[ABE_SHM_POOL_NAME_SIZE];
    int rc;

    rc = abe_shm_pool_normalize_name(name, normalized_name);
    if (rc != ABE_SHM_POOL_OK) {
        return rc;
    }

    if (shm_unlink(normalized_name) != 0) {
        return errno == ENOENT ? ABE_SHM_POOL_NOT_FOUND : ABE_SHM_POOL_ERROR;
    }

    return ABE_SHM_POOL_OK;
}

int abe_shm_pool_alloc(
    abe_shm_pool_t* pool,
    uint32_t size,
    abe_shm_offset_t* out_offset,
    void** out_ptr)
{
    struct abe_shm_pool_header* header;
    struct abe_shm_pool_block_header* block;
    abe_shm_offset_t offset;
    uint32_t index;
    int rc;

    if (pool == NULL || out_offset == NULL) {
        return ABE_SHM_POOL_INVALID_ARG;
    }
    *out_offset = ABE_SHM_POOL_INVALID_OFFSET;
    if (out_ptr != NULL) {
        *out_ptr = NULL;
    }

    header = abe_shm_pool_header(pool);
    if (header == NULL || header->magic != ABE_SHM_POOL_MAGIC || size > header->block_size) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    rc = abe_shm_pool_lock(pool);
    if (rc != ABE_SHM_POOL_OK) {
        return rc;
    }

    if (header->free_head == ABE_SHM_POOL_FREE_INDEX) {
        abe_shm_pool_unlock(pool);
        return ABE_SHM_POOL_NO_MEMORY;
    }

    index = header->free_head;
    block = abe_shm_pool_block_at(pool, index);
    header->free_head = block->next_index;
    block->state = ABE_SHM_POOL_BLOCK_USED;
    block->next_index = ABE_SHM_POOL_FREE_INDEX;
    ++header->used_count;
    if (header->used_count > header->peak_used_count) {
        header->peak_used_count = header->used_count;
    }

    offset = abe_shm_pool_payload_offset(header, index);
    abe_shm_pool_unlock(pool);

    *out_offset = offset;
    if (out_ptr != NULL) {
        *out_ptr = (void*)((unsigned char*)pool->mapping + (size_t)offset);
    }
    return ABE_SHM_POOL_OK;
}

int abe_shm_pool_free(abe_shm_pool_t* pool, abe_shm_offset_t offset)
{
    struct abe_shm_pool_header* header;
    struct abe_shm_pool_block_header* block;
    uint32_t index;
    int rc;

    if (pool == NULL || offset == ABE_SHM_POOL_INVALID_OFFSET) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    header = abe_shm_pool_header(pool);
    rc = abe_shm_pool_block_index_from_offset(header, offset, &index);
    if (rc != ABE_SHM_POOL_OK) {
        return rc;
    }

    rc = abe_shm_pool_lock(pool);
    if (rc != ABE_SHM_POOL_OK) {
        return rc;
    }

    block = abe_shm_pool_block_at(pool, index);
    if (block->state != ABE_SHM_POOL_BLOCK_USED) {
        abe_shm_pool_unlock(pool);
        return ABE_SHM_POOL_DOUBLE_FREE;
    }

    block->state = ABE_SHM_POOL_BLOCK_FREE;
    block->next_index = header->free_head;
    header->free_head = index;
    if (header->used_count > 0u) {
        --header->used_count;
    }

    abe_shm_pool_unlock(pool);
    return ABE_SHM_POOL_OK;
}

void* abe_shm_pool_ptr(abe_shm_pool_t* pool, abe_shm_offset_t offset)
{
    struct abe_shm_pool_header* header;
    struct abe_shm_pool_block_header* block;
    uint32_t index;
    void* ptr;
    int rc;

    if (pool == NULL || offset == ABE_SHM_POOL_INVALID_OFFSET) {
        return NULL;
    }

    header = abe_shm_pool_header(pool);
    rc = abe_shm_pool_block_index_from_offset(header, offset, &index);
    if (rc != ABE_SHM_POOL_OK) {
        return NULL;
    }

    rc = abe_shm_pool_lock(pool);
    if (rc != ABE_SHM_POOL_OK) {
        return NULL;
    }
    block = abe_shm_pool_block_at(pool, index);
    ptr = block->state == ABE_SHM_POOL_BLOCK_USED ?
        (void*)((unsigned char*)pool->mapping + (size_t)offset) :
        NULL;
    abe_shm_pool_unlock(pool);

    return ptr;
}

int abe_shm_pool_offset(
    abe_shm_pool_t* pool,
    const void* ptr,
    abe_shm_offset_t* out_offset)
{
    const unsigned char* base;
    const unsigned char* target;
    uint64_t offset;
    uint32_t index;
    struct abe_shm_pool_header* header;
    int rc;

    if (pool == NULL || pool->mapping == NULL || ptr == NULL || out_offset == NULL) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    base = (const unsigned char*)pool->mapping;
    target = (const unsigned char*)ptr;
    if (target < base || target >= base + pool->mapping_size) {
        return ABE_SHM_POOL_OUT_OF_RANGE;
    }

    offset = (uint64_t)(target - base);
    header = abe_shm_pool_header(pool);
    rc = abe_shm_pool_block_index_from_offset(header, (abe_shm_offset_t)offset, &index);
    if (rc != ABE_SHM_POOL_OK) {
        return rc;
    }
    (void)index;

    *out_offset = (abe_shm_offset_t)offset;
    return ABE_SHM_POOL_OK;
}

int abe_shm_pool_reset(abe_shm_pool_t* pool)
{
    int rc;

    if (pool == NULL || abe_shm_pool_header(pool) == NULL) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    rc = abe_shm_pool_lock(pool);
    if (rc != ABE_SHM_POOL_OK) {
        return rc;
    }

    abe_shm_pool_init_blocks(pool);
    abe_shm_pool_unlock(pool);
    return ABE_SHM_POOL_OK;
}

int abe_shm_pool_get_stats(abe_shm_pool_t* pool, abe_shm_pool_stats_t* out_stats)
{
    struct abe_shm_pool_header* header;
    int rc;

    if (pool == NULL || out_stats == NULL) {
        return ABE_SHM_POOL_INVALID_ARG;
    }

    header = abe_shm_pool_header(pool);
    rc = abe_shm_pool_lock(pool);
    if (rc != ABE_SHM_POOL_OK) {
        return rc;
    }

    memset(out_stats, 0, sizeof(*out_stats));
    out_stats->block_size = header->block_size;
    out_stats->block_count = header->block_count;
    out_stats->used_count = header->used_count;
    out_stats->free_count = header->block_count - header->used_count;
    out_stats->peak_used_count = header->peak_used_count;
    out_stats->total_bytes = header->total_size;

    abe_shm_pool_unlock(pool);
    return ABE_SHM_POOL_OK;
}
