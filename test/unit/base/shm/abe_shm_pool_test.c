#define _POSIX_C_SOURCE 200809L

#include "abe_shm_pool.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

static void make_pool_name(char* out_name, unsigned int name_size)
{
    (void)snprintf(out_name, name_size, "/abe_shm_pool_test_%ld", (long)getpid());
}

static int test_shared_mapping(void)
{
    char name[64];
    abe_shm_pool_config_t create_config;
    abe_shm_pool_config_t open_config;
    abe_shm_pool_stats_t stats;
    abe_shm_pool_t* owner;
    abe_shm_pool_t* peer;
    abe_shm_offset_t offset;
    abe_shm_offset_t roundtrip_offset;
    void* owner_ptr;
    void* peer_ptr;

    make_pool_name(name, sizeof(name));
    (void)abe_shm_pool_unlink(name);

    memset(&create_config, 0, sizeof(create_config));
    create_config.name = name;
    create_config.block_size = 64u;
    create_config.block_count = 2u;
    create_config.alignment = 16u;
    create_config.flags = ABE_SHM_POOL_CREATE | ABE_SHM_POOL_EXCLUSIVE;

    owner = NULL;
    TEST_REQUIRE(abe_shm_pool_open(&create_config, &owner) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(owner != NULL);

    memset(&open_config, 0, sizeof(open_config));
    open_config.name = name;
    peer = NULL;
    TEST_REQUIRE(abe_shm_pool_open(&open_config, &peer) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(peer != NULL);

    offset = ABE_SHM_POOL_INVALID_OFFSET;
    owner_ptr = NULL;
    TEST_REQUIRE(abe_shm_pool_alloc(owner, 16u, &offset, &owner_ptr) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(offset != ABE_SHM_POOL_INVALID_OFFSET);
    TEST_REQUIRE(owner_ptr != NULL);
    TEST_REQUIRE(((uintptr_t)owner_ptr % 16u) == 0u);

    memcpy(owner_ptr, "shared-data", 12u);
    peer_ptr = abe_shm_pool_ptr(peer, offset);
    TEST_REQUIRE(peer_ptr != NULL);
    TEST_REQUIRE(memcmp(peer_ptr, "shared-data", 12u) == 0);

    roundtrip_offset = ABE_SHM_POOL_INVALID_OFFSET;
    TEST_REQUIRE(abe_shm_pool_offset(peer, peer_ptr, &roundtrip_offset) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(roundtrip_offset == offset);

    TEST_REQUIRE(abe_shm_pool_get_stats(peer, &stats) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(stats.used_count == 1u);
    TEST_REQUIRE(stats.free_count == 1u);
    TEST_REQUIRE(stats.peak_used_count == 1u);

    TEST_REQUIRE(abe_shm_pool_free(peer, offset) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(abe_shm_pool_free(owner, offset) == ABE_SHM_POOL_DOUBLE_FREE);
    TEST_REQUIRE(abe_shm_pool_ptr(owner, offset) == NULL);

    TEST_REQUIRE(abe_shm_pool_get_stats(owner, &stats) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(stats.used_count == 0u);
    TEST_REQUIRE(stats.free_count == 2u);

    abe_shm_pool_close(peer);
    abe_shm_pool_close(owner);
    TEST_REQUIRE(abe_shm_pool_unlink(name) == ABE_SHM_POOL_OK);
    return 0;
}

static int test_shared_exhaustion_and_reset(void)
{
    char name[64];
    abe_shm_pool_config_t config;
    abe_shm_pool_t* pool;
    abe_shm_offset_t first;
    abe_shm_offset_t second;
    abe_shm_offset_t third;
    void* ptr;

    make_pool_name(name, sizeof(name));
    (void)abe_shm_pool_unlink(name);

    memset(&config, 0, sizeof(config));
    config.name = name;
    config.block_size = 8u;
    config.block_count = 2u;
    config.flags = ABE_SHM_POOL_CREATE | ABE_SHM_POOL_EXCLUSIVE | ABE_SHM_POOL_UNLINK_ON_CLOSE;

    pool = NULL;
    TEST_REQUIRE(abe_shm_pool_open(&config, &pool) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(abe_shm_pool_alloc(pool, 8u, &first, &ptr) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(abe_shm_pool_alloc(pool, 1u, &second, NULL) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(abe_shm_pool_alloc(pool, 1u, &third, NULL) == ABE_SHM_POOL_NO_MEMORY);
    TEST_REQUIRE(abe_shm_pool_alloc(pool, 9u, &third, NULL) == ABE_SHM_POOL_INVALID_ARG);
    TEST_REQUIRE(abe_shm_pool_reset(pool) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(abe_shm_pool_ptr(pool, first) == NULL);
    TEST_REQUIRE(abe_shm_pool_alloc(pool, 8u, &third, NULL) == ABE_SHM_POOL_OK);
    TEST_REQUIRE(third != ABE_SHM_POOL_INVALID_OFFSET);

    (void)second;
    abe_shm_pool_close(pool);
    return 0;
}

int main(void)
{
    if (test_shared_mapping() != 0) {
        return 1;
    }
    if (test_shared_exhaustion_and_reset() != 0) {
        return 1;
    }
    return 0;
}
