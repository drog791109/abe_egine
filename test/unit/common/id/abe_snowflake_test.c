#include "abe_snowflake.h"

#include <stdio.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

static int test_id_layout_and_sequence(void)
{
    abe_snowflake_t* generator;
    uint64_t first;
    uint64_t second;
    uint64_t time_ms;
    uint16_t node_id;
    uint16_t sequence;

    generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(37u, &generator) == ABE_SNOWFLAKE_OK);
    TEST_REQUIRE(abe_snowflake_next_at(generator, ABE_SNOWFLAKE_EPOCH_MS + 1234u, &first) ==
        ABE_SNOWFLAKE_OK);
    TEST_REQUIRE(abe_snowflake_next_at(generator, ABE_SNOWFLAKE_EPOCH_MS + 1234u, &second) ==
        ABE_SNOWFLAKE_OK);
    TEST_REQUIRE(second == first + 1u);
    TEST_REQUIRE(abe_snowflake_decode(first, &time_ms, &node_id, &sequence) == ABE_SNOWFLAKE_OK);
    TEST_REQUIRE(time_ms == ABE_SNOWFLAKE_EPOCH_MS + 1234u);
    TEST_REQUIRE(node_id == 37u);
    TEST_REQUIRE(sequence == 0u);
    abe_snowflake_destroy(generator);
    return 0;
}

static int test_clock_rollback_and_limit(void)
{
    abe_snowflake_t* generator;
    uint64_t before;
    uint64_t after;
    uint64_t value;
    uint32_t index;

    generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(1u, &generator) == ABE_SNOWFLAKE_OK);
    TEST_REQUIRE(abe_snowflake_next_at(generator, ABE_SNOWFLAKE_EPOCH_MS + 20u, &before) ==
        ABE_SNOWFLAKE_OK);
    TEST_REQUIRE(abe_snowflake_next_at(generator, ABE_SNOWFLAKE_EPOCH_MS + 19u, &after) ==
        ABE_SNOWFLAKE_OK);
    TEST_REQUIRE(after > before);

    abe_snowflake_destroy(generator);
    generator = NULL;
    TEST_REQUIRE(abe_snowflake_create(2u, &generator) == ABE_SNOWFLAKE_OK);
    for (index = 0u; index <= ABE_SNOWFLAKE_MAX_SEQUENCE; ++index) {
        TEST_REQUIRE(abe_snowflake_next_at(generator, ABE_SNOWFLAKE_EPOCH_MS + 30u, &value) ==
            ABE_SNOWFLAKE_OK);
    }
    TEST_REQUIRE(abe_snowflake_next_at(generator, ABE_SNOWFLAKE_EPOCH_MS + 30u, &value) ==
        ABE_SNOWFLAKE_LIMIT);
    abe_snowflake_destroy(generator);
    return 0;
}

int main(void)
{
    if (test_id_layout_and_sequence() != 0) {
        return 1;
    }
    if (test_clock_rollback_and_limit() != 0) {
        return 1;
    }
    return 0;
}
