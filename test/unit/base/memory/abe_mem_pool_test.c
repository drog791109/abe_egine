#include "abe_mem_pool.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

static int test_variable_alloc_free(void)
{
    abe_mem_pool_config_t config;
    abe_mem_pool_stats_t stats;
    abe_mem_pool_t* pool;
    void* a;
    void* b;
    void* c;
    void* d;

    memset(&config, 0, sizeof(config));
    config.capacity = 512u;
    config.alignment = 16u;
    config.name = "unit_mem_pool";

    pool = NULL;
    TEST_REQUIRE(abe_mem_pool_create(&config, &pool) == ABE_MEM_POOL_OK);
    TEST_REQUIRE(pool != NULL);

    a = abe_mem_pool_alloc(pool, 24u);
    b = abe_mem_pool_alloc(pool, 48u);
    c = abe_mem_pool_calloc(pool, 4u, 8u);
    TEST_REQUIRE(a != NULL);
    TEST_REQUIRE(b != NULL);
    TEST_REQUIRE(c != NULL);
    TEST_REQUIRE(((uintptr_t)a % 16u) == 0u);
    TEST_REQUIRE(((uintptr_t)b % 16u) == 0u);
    TEST_REQUIRE(((uintptr_t)c % 16u) == 0u);
    TEST_REQUIRE(memcmp(c, "\0\0\0\0\0\0\0\0", 8u) == 0);

    memset(a, 0xab, 24u);
    memset(b, 0xcd, 48u);

    TEST_REQUIRE(abe_mem_pool_get_stats(pool, &stats) == ABE_MEM_POOL_OK);
    TEST_REQUIRE(stats.used_bytes == 104u);
    TEST_REQUIRE(stats.used_block_count == 3u);
    TEST_REQUIRE(stats.peak_used_bytes == 104u);
    TEST_REQUIRE(stats.free_block_count >= 1u);

    TEST_REQUIRE(abe_mem_pool_free(pool, b) == ABE_MEM_POOL_OK);
    TEST_REQUIRE(abe_mem_pool_free(pool, b) == ABE_MEM_POOL_DOUBLE_FREE);
    TEST_REQUIRE(abe_mem_pool_get_stats(pool, &stats) == ABE_MEM_POOL_OK);
    TEST_REQUIRE(stats.used_bytes == 56u);
    TEST_REQUIRE(stats.used_block_count == 2u);

    d = abe_mem_pool_alloc(pool, 40u);
    TEST_REQUIRE(d != NULL);
    TEST_REQUIRE(((uintptr_t)d % 16u) == 0u);

    TEST_REQUIRE(abe_mem_pool_free(pool, a) == ABE_MEM_POOL_OK);
    TEST_REQUIRE(abe_mem_pool_free(pool, c) == ABE_MEM_POOL_OK);
    TEST_REQUIRE(abe_mem_pool_free(pool, d) == ABE_MEM_POOL_OK);

    TEST_REQUIRE(abe_mem_pool_get_stats(pool, &stats) == ABE_MEM_POOL_OK);
    TEST_REQUIRE(stats.used_bytes == 0u);
    TEST_REQUIRE(stats.used_block_count == 0u);
    TEST_REQUIRE(stats.free_block_count == 1u);
    TEST_REQUIRE(stats.largest_free_block >= 512u);

    abe_mem_pool_destroy(pool);
    return 0;
}

static int test_reset_and_invalid_config(void)
{
    abe_mem_pool_config_t config;
    abe_mem_pool_stats_t stats;
    abe_mem_pool_t* pool;
    void* a;
    void* b;

    memset(&config, 0, sizeof(config));
    config.capacity = 128u;
    config.alignment = 8u;

    pool = NULL;
    TEST_REQUIRE(abe_mem_pool_create(&config, &pool) == ABE_MEM_POOL_OK);
    a = abe_mem_pool_alloc(pool, 64u);
    b = abe_mem_pool_alloc(pool, 1024u);
    TEST_REQUIRE(a != NULL);
    TEST_REQUIRE(b == NULL);

    abe_mem_pool_reset(pool);
    TEST_REQUIRE(abe_mem_pool_free(pool, a) == ABE_MEM_POOL_DOUBLE_FREE);
    TEST_REQUIRE(abe_mem_pool_get_stats(pool, &stats) == ABE_MEM_POOL_OK);
    TEST_REQUIRE(stats.used_bytes == 0u);
    TEST_REQUIRE(stats.free_block_count == 1u);
    TEST_REQUIRE(stats.peak_used_bytes == 64u);
    abe_mem_pool_destroy(pool);

    memset(&config, 0, sizeof(config));
    config.capacity = 0u;
    TEST_REQUIRE(abe_mem_pool_create(&config, &pool) == ABE_MEM_POOL_INVALID_ARG);
    TEST_REQUIRE(pool == NULL);

    config.capacity = 64u;
    config.alignment = 3u;
    TEST_REQUIRE(abe_mem_pool_create(&config, &pool) == ABE_MEM_POOL_INVALID_ARG);
    TEST_REQUIRE(pool == NULL);

    return 0;
}

int main(void)
{
    if (test_variable_alloc_free() != 0) {
        return 1;
    }
    if (test_reset_and_invalid_config() != 0) {
        return 1;
    }
    return 0;
}
