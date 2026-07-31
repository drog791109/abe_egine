#include "abe_service_memory_pool.h"

#include <new>
#include <stddef.h>

namespace abe {
namespace service {
namespace common {

static bool valid_pool_name(const char* name)
{
    return name != NULL && name[0] != '\0';
}

StlMemoryPoolManager::StlMemoryPoolManager()
{
}

StlMemoryPoolManager::~StlMemoryPoolManager()
{
    close();
}

int StlMemoryPoolManager::destroy_pool(const char* name)
{
    PoolMap::iterator it;

    if (!valid_pool_name(name)) {
        return ABE_INVALID_ARG;
    }

    it = pools_.find(name);
    if (it == pools_.end()) {
        return ABE_NOT_FOUND;
    }

    if (it->second.destroy != NULL) {
        it->second.destroy(it->second.storage);
    }
    pools_.erase(it);
    return ABE_OK;
}

void StlMemoryPoolManager::close()
{
    PoolMap::iterator it;

    it = pools_.begin();
    while (it != pools_.end()) {
        if (it->second.destroy != NULL) {
            it->second.destroy(it->second.storage);
        }
        ++it;
    }
    pools_.clear();
}

void* StlMemoryPoolManager::find_pool(const char* name) const
{
    const PoolEntry* entry;

    entry = find_pool_entry(name);
    if (entry == NULL || entry->data == NULL) {
        return NULL;
    }
    return entry->data(entry->storage);
}

uint32_t StlMemoryPoolManager::item_count(const char* name) const
{
    const PoolEntry* entry;

    entry = find_pool_entry(name);
    if (entry == NULL || entry->count == NULL) {
        return 0u;
    }
    return entry->count(entry->storage);
}

uint32_t StlMemoryPoolManager::pool_count() const
{
    return (uint32_t)pools_.size();
}

bool StlMemoryPoolManager::has_pool(const char* name) const
{
    return find_pool_entry(name) != NULL;
}

const StlMemoryPoolManager::PoolEntry* StlMemoryPoolManager::find_pool_entry(
    const char* name) const
{
    PoolMap::const_iterator it;

    if (!valid_pool_name(name)) {
        return NULL;
    }

    it = pools_.find(name);
    return it == pools_.end() ? NULL : &it->second;
}

int StlMemoryPoolManager::insert_pool(const char* name, const PoolEntry& entry)
{
    std::pair<PoolMap::iterator, bool> inserted;

    if (!valid_pool_name(name) || entry.storage == NULL) {
        return ABE_INVALID_ARG;
    }

    try {
        inserted = pools_.insert(PoolMap::value_type(std::string(name), entry));
    } catch (const std::bad_alloc&) {
        return ABE_NO_MEMORY;
    } catch (...) {
        return ABE_ERROR;
    }

    return inserted.second ? ABE_OK : ABE_ALREADY_EXISTS;
}

SharedMemoryPoolManager::SharedMemoryPoolManager()
{
}

SharedMemoryPoolManager::~SharedMemoryPoolManager()
{
    close();
}

int SharedMemoryPoolManager::open_pool(
    const abe_shm_pool_config_t& config,
    abe_shm_pool_t** out_pool)
{
    abe_shm_pool_t* pool;
    std::string key;
    int rc;

    if (out_pool != NULL) {
        *out_pool = NULL;
    }
    if (!valid_pool_name(config.name)) {
        return ABE_INVALID_ARG;
    }

    key = config.name;
    if (pools_.find(key) != pools_.end()) {
        return ABE_ALREADY_EXISTS;
    }

    pool = NULL;
    rc = abe_shm_pool_open(&config, &pool);
    if (rc != ABE_SHM_POOL_OK) {
        return rc;
    }

    pools_[key] = pool;
    if (out_pool != NULL) {
        *out_pool = pool;
    }
    return ABE_OK;
}

int SharedMemoryPoolManager::close_pool(const char* name)
{
    PoolMap::iterator it;

    if (!valid_pool_name(name)) {
        return ABE_INVALID_ARG;
    }

    it = pools_.find(name);
    if (it == pools_.end()) {
        return ABE_NOT_FOUND;
    }

    abe_shm_pool_close(it->second);
    pools_.erase(it);
    return ABE_OK;
}

void SharedMemoryPoolManager::close()
{
    PoolMap::iterator it;

    it = pools_.begin();
    while (it != pools_.end()) {
        abe_shm_pool_close(it->second);
        ++it;
    }
    pools_.clear();
}

abe_shm_pool_t* SharedMemoryPoolManager::find_pool(const char* name) const
{
    PoolMap::const_iterator it;

    if (!valid_pool_name(name)) {
        return NULL;
    }

    it = pools_.find(name);
    return it == pools_.end() ? NULL : it->second;
}

int SharedMemoryPoolManager::unlink_pool(const char* name)
{
    if (!valid_pool_name(name)) {
        return ABE_INVALID_ARG;
    }
    return abe_shm_pool_unlink(name);
}

int SharedMemoryPoolManager::alloc(
    const char* name,
    uint32_t size,
    abe_shm_offset_t* out_offset,
    void** out_ptr)
{
    abe_shm_pool_t* pool;

    if (out_offset != NULL) {
        *out_offset = ABE_SHM_POOL_INVALID_OFFSET;
    }
    if (out_offset == NULL) {
        return ABE_INVALID_ARG;
    }

    pool = find_pool(name);
    if (pool == NULL) {
        return ABE_NOT_FOUND;
    }
    return abe_shm_pool_alloc(pool, size, out_offset, out_ptr);
}

int SharedMemoryPoolManager::free(const char* name, abe_shm_offset_t offset)
{
    abe_shm_pool_t* pool;

    pool = find_pool(name);
    if (pool == NULL) {
        return ABE_NOT_FOUND;
    }
    return abe_shm_pool_free(pool, offset);
}

void* SharedMemoryPoolManager::ptr(const char* name, abe_shm_offset_t offset)
{
    abe_shm_pool_t* pool;

    pool = find_pool(name);
    if (pool == NULL) {
        return NULL;
    }
    return abe_shm_pool_ptr(pool, offset);
}

int SharedMemoryPoolManager::offset(
    const char* name,
    const void* ptr,
    abe_shm_offset_t* out_offset)
{
    abe_shm_pool_t* pool;

    if (out_offset != NULL) {
        *out_offset = ABE_SHM_POOL_INVALID_OFFSET;
    }
    if (out_offset == NULL) {
        return ABE_INVALID_ARG;
    }

    pool = find_pool(name);
    if (pool == NULL) {
        return ABE_NOT_FOUND;
    }
    return abe_shm_pool_offset(pool, ptr, out_offset);
}

int SharedMemoryPoolManager::reset_pool(const char* name)
{
    abe_shm_pool_t* pool;

    pool = find_pool(name);
    if (pool == NULL) {
        return ABE_NOT_FOUND;
    }
    return abe_shm_pool_reset(pool);
}

int SharedMemoryPoolManager::get_stats(
    const char* name,
    abe_shm_pool_stats_t* out_stats)
{
    abe_shm_pool_t* pool;

    if (out_stats == NULL) {
        return ABE_INVALID_ARG;
    }

    pool = find_pool(name);
    if (pool == NULL) {
        return ABE_NOT_FOUND;
    }
    return abe_shm_pool_get_stats(pool, out_stats);
}

uint32_t SharedMemoryPoolManager::pool_count() const
{
    return (uint32_t)pools_.size();
}

} /* namespace common */
} /* namespace service */
} /* namespace abe */
