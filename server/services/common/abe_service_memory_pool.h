#ifndef ABE_SERVICE_MEMORY_POOL_H
#define ABE_SERVICE_MEMORY_POOL_H

#include "abe_error.h"
#include "abe_shm_pool.h"

#include <cstddef>
#include <new>
#include <stdint.h>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace abe {
namespace service {
namespace common {

class StlMemoryPoolManager {
public:
    StlMemoryPoolManager();
    ~StlMemoryPoolManager();

    /*
     * Creates one named contiguous std::vector-backed object pool. The returned
     * pointer is valid until destroy_pool() or close().
     */
    template <typename T>
    int create_pool(
        const char* name,
        uint32_t count,
        T** out_items = NULL);
    int destroy_pool(const char* name);
    void close();

    void* find_pool(const char* name) const;
    template <typename T>
    T* find_pool(const char* name) const;
    uint32_t item_count(const char* name) const;
    uint32_t pool_count() const;

private:
    StlMemoryPoolManager(const StlMemoryPoolManager&);
    StlMemoryPoolManager& operator=(const StlMemoryPoolManager&);

    typedef void (*PoolDestroyFn)(void* storage);
    typedef void* (*PoolDataFn)(void* storage);
    typedef uint32_t (*PoolCountFn)(void* storage);
    typedef std::aligned_storage<
        sizeof(std::max_align_t),
        alignof(std::max_align_t)>::type StorageUnit;

    struct PoolEntry {
        void* storage;
        PoolDestroyFn destroy;
        PoolDataFn data;
        PoolCountFn count;
        const void* type_key;
    };

    typedef std::unordered_map<std::string, PoolEntry> PoolMap;

    template <typename T>
    struct ObjectPool {
        std::vector<StorageUnit> storage;
        uint32_t item_count;
        uint32_t constructed_count;

        ObjectPool()
            : item_count(0u),
              constructed_count(0u)
        {
        }
    };

    PoolMap pools_;

    bool has_pool(const char* name) const;
    const PoolEntry* find_pool_entry(const char* name) const;
    int insert_pool(const char* name, const PoolEntry& entry);

    template <typename T>
    static void destroy_object_pool(void* storage);
    template <typename T>
    static void* object_pool_data(void* storage);
    template <typename T>
    static uint32_t object_pool_count(void* storage);
    template <typename T>
    static const void* type_key();
};

class SharedMemoryPoolManager {
public:
    SharedMemoryPoolManager();
    ~SharedMemoryPoolManager();

    int open_pool(const abe_shm_pool_config_t& config, abe_shm_pool_t** out_pool = NULL);
    int close_pool(const char* name);
    void close();

    abe_shm_pool_t* find_pool(const char* name) const;
    int unlink_pool(const char* name);
    int alloc(
        const char* name,
        uint32_t size,
        abe_shm_offset_t* out_offset,
        void** out_ptr);
    int free(const char* name, abe_shm_offset_t offset);
    void* ptr(const char* name, abe_shm_offset_t offset);
    int offset(const char* name, const void* ptr, abe_shm_offset_t* out_offset);
    int reset_pool(const char* name);
    int get_stats(const char* name, abe_shm_pool_stats_t* out_stats);
    uint32_t pool_count() const;

private:
    SharedMemoryPoolManager(const SharedMemoryPoolManager&);
    SharedMemoryPoolManager& operator=(const SharedMemoryPoolManager&);

    typedef std::unordered_map<std::string, abe_shm_pool_t*> PoolMap;

    PoolMap pools_;
};

template <typename T>
int StlMemoryPoolManager::create_pool(
    const char* name,
    uint32_t count,
    T** out_items)
{
    PoolEntry entry;
    ObjectPool<T>* storage;
    T* items;
    uint64_t byte_count;
    uint64_t unit_count;
    int rc;

    if (out_items != NULL) {
        *out_items = NULL;
    }
    if (name == NULL || name[0] == '\0' || count == 0u) {
        return ABE_INVALID_ARG;
    }
    if (has_pool(name)) {
        return ABE_ALREADY_EXISTS;
    }
    if ((uint64_t)sizeof(T) > UINT64_MAX / (uint64_t)count ||
        alignof(T) > alignof(StorageUnit)) {
        return ABE_INVALID_ARG;
    }

    storage = new (std::nothrow) ObjectPool<T>();
    if (storage == NULL) {
        return ABE_NO_MEMORY;
    }

    byte_count = (uint64_t)sizeof(T) * (uint64_t)count;
    unit_count =
        (byte_count + (uint64_t)sizeof(StorageUnit) - 1u) /
        (uint64_t)sizeof(StorageUnit);

    try {
        storage->storage.resize((std::size_t)unit_count);
    } catch (const std::bad_alloc&) {
        delete storage;
        return ABE_NO_MEMORY;
    } catch (...) {
        delete storage;
        return ABE_ERROR;
    }

    storage->item_count = count;
    items = (T*)StlMemoryPoolManager::object_pool_data<T>(storage);
    try {
        while (storage->constructed_count < count) {
            new (&items[storage->constructed_count]) T();
            ++storage->constructed_count;
        }
    } catch (const std::bad_alloc&) {
        StlMemoryPoolManager::destroy_object_pool<T>(storage);
        return ABE_NO_MEMORY;
    } catch (...) {
        StlMemoryPoolManager::destroy_object_pool<T>(storage);
        return ABE_ERROR;
    }

    entry.storage = storage;
    entry.destroy = &StlMemoryPoolManager::destroy_object_pool<T>;
    entry.data = &StlMemoryPoolManager::object_pool_data<T>;
    entry.count = &StlMemoryPoolManager::object_pool_count<T>;
    entry.type_key = StlMemoryPoolManager::type_key<T>();

    rc = insert_pool(name, entry);
    if (rc != ABE_OK) {
        StlMemoryPoolManager::destroy_object_pool<T>(storage);
        return rc;
    }
    if (out_items != NULL) {
        *out_items = (T*)entry.data(entry.storage);
    }
    return ABE_OK;
}

template <typename T>
T* StlMemoryPoolManager::find_pool(const char* name) const
{
    const PoolEntry* entry;

    entry = find_pool_entry(name);
    if (entry == NULL || entry->type_key != StlMemoryPoolManager::type_key<T>()) {
        return NULL;
    }
    return (T*)entry->data(entry->storage);
}

template <typename T>
void StlMemoryPoolManager::destroy_object_pool(void* storage)
{
    ObjectPool<T>* pool;
    T* items;

    pool = (ObjectPool<T>*)storage;
    if (pool == NULL) {
        return;
    }

    items = (T*)StlMemoryPoolManager::object_pool_data<T>(storage);
    while (pool->constructed_count > 0u) {
        --pool->constructed_count;
        items[pool->constructed_count].~T();
    }
    delete pool;
}

template <typename T>
void* StlMemoryPoolManager::object_pool_data(void* storage)
{
    ObjectPool<T>* pool;

    pool = (ObjectPool<T>*)storage;
    if (pool == NULL || pool->storage.empty()) {
        return NULL;
    }
    return (void*)&pool->storage[0];
}

template <typename T>
uint32_t StlMemoryPoolManager::object_pool_count(void* storage)
{
    ObjectPool<T>* pool;

    pool = (ObjectPool<T>*)storage;
    if (pool == NULL) {
        return 0u;
    }
    return pool->item_count;
}

template <typename T>
const void* StlMemoryPoolManager::type_key()
{
    static char key;
    return &key;
}

} /* namespace common */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_MEMORY_POOL_H */
