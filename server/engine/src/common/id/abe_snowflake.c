#include "abe_snowflake.h"

#include "abe_time.h"

#include <pthread.h>
#include <stdlib.h>

#define ABE_SNOWFLAKE_TIMESTAMP_BITS 41u
#define ABE_SNOWFLAKE_NODE_BITS 10u
#define ABE_SNOWFLAKE_SEQUENCE_BITS 12u
#define ABE_SNOWFLAKE_NODE_SHIFT ABE_SNOWFLAKE_SEQUENCE_BITS
#define ABE_SNOWFLAKE_TIMESTAMP_SHIFT \
    (ABE_SNOWFLAKE_NODE_BITS + ABE_SNOWFLAKE_SEQUENCE_BITS)
#define ABE_SNOWFLAKE_MAX_TIMESTAMP ((1ull << ABE_SNOWFLAKE_TIMESTAMP_BITS) - 1ull)

struct abe_snowflake {
    pthread_mutex_t mutex;
    uint16_t node_id;
    uint16_t sequence;
    uint64_t last_time_ms;
    int has_last_time;
};

int abe_snowflake_create(uint16_t node_id, abe_snowflake_t** out_generator)
{
    abe_snowflake_t* generator;

    if (out_generator == NULL || node_id > ABE_SNOWFLAKE_MAX_NODE_ID) {
        return ABE_SNOWFLAKE_INVALID_ARG;
    }

    *out_generator = NULL;
    generator = (abe_snowflake_t*)calloc(1u, sizeof(*generator));
    if (generator == NULL) {
        return ABE_SNOWFLAKE_NO_MEMORY;
    }
    if (pthread_mutex_init(&generator->mutex, NULL) != 0) {
        free(generator);
        return ABE_SNOWFLAKE_NO_MEMORY;
    }

    generator->node_id = node_id;
    *out_generator = generator;
    return ABE_SNOWFLAKE_OK;
}

void abe_snowflake_destroy(abe_snowflake_t* generator)
{
    if (generator == NULL) {
        return;
    }
    (void)pthread_mutex_destroy(&generator->mutex);
    free(generator);
}

int abe_snowflake_next_at(
    abe_snowflake_t* generator,
    uint64_t now_ms,
    uint64_t* out_id)
{
    uint64_t time_ms;
    uint64_t timestamp;
    uint16_t sequence;

    if (generator == NULL || out_id == NULL || now_ms < ABE_SNOWFLAKE_EPOCH_MS) {
        return ABE_SNOWFLAKE_INVALID_ARG;
    }

    (void)pthread_mutex_lock(&generator->mutex);
    time_ms = now_ms;
    if (generator->has_last_time && time_ms < generator->last_time_ms) {
        time_ms = generator->last_time_ms;
    }

    if (!generator->has_last_time || time_ms != generator->last_time_ms) {
        generator->last_time_ms = time_ms;
        generator->sequence = 0u;
        generator->has_last_time = 1;
    } else {
        if (generator->sequence >= ABE_SNOWFLAKE_MAX_SEQUENCE) {
            (void)pthread_mutex_unlock(&generator->mutex);
            return ABE_SNOWFLAKE_LIMIT;
        }
        ++generator->sequence;
    }

    timestamp = time_ms - ABE_SNOWFLAKE_EPOCH_MS;
    if (timestamp > ABE_SNOWFLAKE_MAX_TIMESTAMP) {
        (void)pthread_mutex_unlock(&generator->mutex);
        return ABE_SNOWFLAKE_OVERFLOW;
    }

    sequence = generator->sequence;
    *out_id = (timestamp << ABE_SNOWFLAKE_TIMESTAMP_SHIFT) |
        ((uint64_t)generator->node_id << ABE_SNOWFLAKE_NODE_SHIFT) |
        (uint64_t)sequence;
    (void)pthread_mutex_unlock(&generator->mutex);
    return ABE_SNOWFLAKE_OK;
}

int abe_snowflake_next(abe_snowflake_t* generator, uint64_t* out_id)
{
    return abe_snowflake_next_at(generator, abe_time_real_ms(), out_id);
}

int abe_snowflake_decode(
    uint64_t id,
    uint64_t* out_time_ms,
    uint16_t* out_node_id,
    uint16_t* out_sequence)
{
    uint64_t timestamp;

    if (out_time_ms == NULL || out_node_id == NULL || out_sequence == NULL) {
        return ABE_SNOWFLAKE_INVALID_ARG;
    }

    timestamp = id >> ABE_SNOWFLAKE_TIMESTAMP_SHIFT;
    if (timestamp > ABE_SNOWFLAKE_MAX_TIMESTAMP ||
        timestamp > UINT64_MAX - ABE_SNOWFLAKE_EPOCH_MS) {
        return ABE_SNOWFLAKE_OVERFLOW;
    }

    *out_time_ms = timestamp + ABE_SNOWFLAKE_EPOCH_MS;
    *out_node_id = (uint16_t)((id >> ABE_SNOWFLAKE_NODE_SHIFT) & ABE_SNOWFLAKE_MAX_NODE_ID);
    *out_sequence = (uint16_t)(id & ABE_SNOWFLAKE_MAX_SEQUENCE);
    return ABE_SNOWFLAKE_OK;
}
