#include "abe_pool.h"

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

static int test_pool_alloc_free(void)
{
    abe_pool_config_t config;
    abe_pool_stats_t stats;
    abe_pool_t* pool;
    void* a;
    void* b;
    void* c;

    memset(&config, 0, sizeof(config));
    config.block_size = 32u;
    config.block_count = 2u;
    config.alignment = 16u;
    config.name = "unit_pool";

    pool = NULL;
    TEST_REQUIRE(abe_pool_create(&config, &pool) == ABE_POOL_OK);
    TEST_REQUIRE(pool != NULL);

    a = abe_pool_alloc(pool, 16u);
    b = abe_pool_alloc(pool, 32u);
    c = abe_pool_alloc(pool, 1u);
    TEST_REQUIRE(a != NULL);
    TEST_REQUIRE(b != NULL);
    TEST_REQUIRE(c == NULL);
    TEST_REQUIRE(((uintptr_t)a % 16u) == 0u);
    TEST_REQUIRE(((uintptr_t)b % 16u) == 0u);
    TEST_REQUIRE(abe_pool_alloc(pool, 33u) == NULL);

    TEST_REQUIRE(abe_pool_get_stats(pool, &stats) == ABE_POOL_OK);
    TEST_REQUIRE(stats.used_count == 2u);
    TEST_REQUIRE(stats.free_count == 0u);
    TEST_REQUIRE(stats.peak_used_count == 2u);

    TEST_REQUIRE(abe_pool_free(pool, a) == ABE_POOL_OK);
    TEST_REQUIRE(abe_pool_free(pool, a) == ABE_POOL_DOUBLE_FREE);
    TEST_REQUIRE(abe_pool_get_stats(pool, &stats) == ABE_POOL_OK);
    TEST_REQUIRE(stats.used_count == 1u);
    TEST_REQUIRE(stats.free_count == 1u);

    c = abe_pool_alloc(pool, 8u);
    TEST_REQUIRE(c != NULL);
    TEST_REQUIRE(abe_pool_free(pool, b) == ABE_POOL_OK);
    TEST_REQUIRE(abe_pool_free(pool, c) == ABE_POOL_OK);

    abe_pool_reset(pool);
    TEST_REQUIRE(abe_pool_get_stats(pool, &stats) == ABE_POOL_OK);
    TEST_REQUIRE(stats.used_count == 0u);
    TEST_REQUIRE(stats.free_count == 2u);

    abe_pool_destroy(pool);
    return 0;
}

static int test_pool_invalid_config(void)
{
    abe_pool_config_t config;
    abe_pool_t* pool;

    memset(&config, 0, sizeof(config));
    config.block_size = 16u;
    config.block_count = 1u;
    config.alignment = 3u;
    pool = NULL;
    TEST_REQUIRE(abe_pool_create(&config, &pool) == ABE_POOL_INVALID_ARG);
    TEST_REQUIRE(pool == NULL);

    config.alignment = 8u;
    config.block_count = 0u;
    TEST_REQUIRE(abe_pool_create(&config, &pool) == ABE_POOL_INVALID_ARG);

    return 0;
}

int main(void)
{
    if (test_pool_alloc_free() != 0) {
        return 1;
    }
    if (test_pool_invalid_config() != 0) {
        return 1;
    }
    return 0;
}
