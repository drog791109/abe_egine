#include "abe_service_memory_pool.h"

#include "../../abe_test.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace service_common = abe::service::common;

static void make_pool_name(char* out_name, unsigned int name_size)
{
    (void)snprintf(out_name, name_size, "/abe_service_memory_pool_test_%ld", (long)getpid());
}

struct PoolTestObject {
    PoolTestObject() : value(7u) {}

    uint32_t value;
};

static int test_stl_memory_pool_manager(void)
{
    service_common::StlMemoryPoolManager manager;
    PoolTestObject* objects;
    PoolTestObject* found_objects;
    PoolTestObject* duplicate_objects;
    uint32_t* numbers;

    objects = NULL;
    TEST_REQUIRE(manager.create_pool<PoolTestObject>("objects", 3u, &objects) == ABE_OK);
    TEST_REQUIRE(objects != NULL);
    duplicate_objects = NULL;
    TEST_REQUIRE(
        manager.create_pool<PoolTestObject>("objects", 3u, &duplicate_objects) ==
        ABE_ALREADY_EXISTS);
    TEST_REQUIRE(duplicate_objects == NULL);
    TEST_REQUIRE(manager.find_pool("objects") != NULL);
    TEST_REQUIRE(manager.find_pool("missing") == NULL);
    TEST_REQUIRE(manager.find_pool<uint32_t>("objects") == NULL);
    TEST_REQUIRE(manager.pool_count() == 1u);
    TEST_REQUIRE(manager.item_count("objects") == 3u);

    TEST_REQUIRE(objects[0].value == 7u);
    objects[1].value = 99u;

    found_objects = manager.find_pool<PoolTestObject>("objects");
    TEST_REQUIRE(found_objects == objects);
    TEST_REQUIRE(found_objects[1].value == 99u);

    numbers = NULL;
    TEST_REQUIRE(manager.create_pool<uint32_t>("numbers", 2u, &numbers) == ABE_OK);
    TEST_REQUIRE(numbers != NULL);
    TEST_REQUIRE(numbers[0] == 0u);
    TEST_REQUIRE(numbers[1] == 0u);
    TEST_REQUIRE(manager.pool_count() == 2u);

    TEST_REQUIRE(manager.destroy_pool("objects") == ABE_OK);
    TEST_REQUIRE(manager.find_pool("objects") == NULL);
    TEST_REQUIRE(manager.destroy_pool("objects") == ABE_NOT_FOUND);
    TEST_REQUIRE(manager.pool_count() == 1u);
    manager.close();
    TEST_REQUIRE(manager.pool_count() == 0u);
    return ABE_TEST_STATUS_OK;
}

static int test_shared_memory_pool_manager(void)
{
    service_common::SharedMemoryPoolManager manager;
    abe_shm_pool_config_t config;
    abe_shm_pool_stats_t stats;
    abe_shm_offset_t offset;
    void* ptr;
    char name[96];

    make_pool_name(name, sizeof(name));
    (void)manager.unlink_pool(name);

    memset(&config, 0, sizeof(config));
    config.name = name;
    config.block_size = 64u;
    config.block_count = 2u;
    config.alignment = 16u;
    config.flags = ABE_SHM_POOL_CREATE | ABE_SHM_POOL_EXCLUSIVE;

    TEST_REQUIRE(manager.open_pool(config) == ABE_OK);
    TEST_REQUIRE(manager.open_pool(config) == ABE_ALREADY_EXISTS);
    TEST_REQUIRE(manager.find_pool(name) != NULL);
    TEST_REQUIRE(manager.pool_count() == 1u);

    offset = ABE_SHM_POOL_INVALID_OFFSET;
    ptr = NULL;
    TEST_REQUIRE(manager.alloc(name, 16u, &offset, &ptr) == ABE_OK);
    TEST_REQUIRE(offset != ABE_SHM_POOL_INVALID_OFFSET);
    TEST_REQUIRE(ptr != NULL);
    memcpy(ptr, "shared", 7u);
    TEST_REQUIRE(memcmp(manager.ptr(name, offset), "shared", 7u) == 0);

    memset(&stats, 0, sizeof(stats));
    TEST_REQUIRE(manager.get_stats(name, &stats) == ABE_OK);
    TEST_REQUIRE(stats.used_count == 1u);
    TEST_REQUIRE(stats.free_count == 1u);

    TEST_REQUIRE(manager.free(name, offset) == ABE_OK);
    TEST_REQUIRE(manager.close_pool(name) == ABE_OK);
    TEST_REQUIRE(manager.unlink_pool(name) == ABE_OK);
    TEST_REQUIRE(manager.pool_count() == 0u);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_stl_memory_pool_manager() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_shared_memory_pool_manager() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
