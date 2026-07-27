#ifndef ABE_SNOWFLAKE_H
#define ABE_SNOWFLAKE_H

#include "abe_error.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABE_SNOWFLAKE_EPOCH_MS 1704067200000ull
#define ABE_SNOWFLAKE_MAX_NODE_ID 1023u
#define ABE_SNOWFLAKE_MAX_SEQUENCE 4095u

typedef struct abe_snowflake abe_snowflake_t;

typedef enum abe_snowflake_status {
    ABE_SNOWFLAKE_OK = ABE_OK,
    ABE_SNOWFLAKE_INVALID_ARG = ABE_INVALID_ARG,
    ABE_SNOWFLAKE_NO_MEMORY = ABE_NO_MEMORY,
    ABE_SNOWFLAKE_LIMIT = ABE_LIMIT,
    ABE_SNOWFLAKE_OVERFLOW = ABE_OVERFLOW
} abe_snowflake_status_t;

/*
 * node_id must be unique among every running service process in every zone.
 * The generated 64-bit ID contains 41 bits of milliseconds since
 * ABE_SNOWFLAKE_EPOCH_MS, 10 bits of node_id, and 12 bits of sequence.
 */
int abe_snowflake_create(uint16_t node_id, abe_snowflake_t** out_generator);
void abe_snowflake_destroy(abe_snowflake_t* generator);

/*
 * Generates an ID using process-clamped Unix real time. A clock rollback does
 * not make duplicate IDs. ABE_SNOWFLAKE_LIMIT means this node used all 4096
 * sequence values for the current millisecond; retry on a later tick.
 */
int abe_snowflake_next(abe_snowflake_t* generator, uint64_t* out_id);

/* Test and deterministic replay helper. now_ms is Unix epoch milliseconds. */
int abe_snowflake_next_at(
    abe_snowflake_t* generator,
    uint64_t now_ms,
    uint64_t* out_id);

int abe_snowflake_decode(
    uint64_t id,
    uint64_t* out_time_ms,
    uint16_t* out_node_id,
    uint16_t* out_sequence);

#ifdef __cplusplus
}
#endif

#endif /* ABE_SNOWFLAKE_H */
