#include "abe_error.h"
#include "abe_config.h"
#include "abe_mem_pool.h"
#include "abe_net.h"
#include "abe_pool.h"
#include "abe_shm_pool.h"
#include "abe_time.h"
#include "abe_time_wheel.h"

#include <stdio.h>
#include <string.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

int main(void)
{
    TEST_REQUIRE(ABE_CONFIG_NOT_FOUND == ABE_NOT_FOUND);
    TEST_REQUIRE(ABE_CONFIG_PARSE_ERROR == ABE_PARSE_ERROR);
    TEST_REQUIRE(ABE_CONFIG_TYPE_MISMATCH == ABE_TYPE_MISMATCH);

    TEST_REQUIRE(ABE_MEM_POOL_NO_MEMORY == ABE_NO_MEMORY);
    TEST_REQUIRE(ABE_MEM_POOL_DOUBLE_FREE == ABE_DOUBLE_FREE);
    TEST_REQUIRE(ABE_POOL_OUT_OF_RANGE == ABE_OUT_OF_RANGE);
    TEST_REQUIRE(ABE_POOL_DOUBLE_FREE == ABE_DOUBLE_FREE);

    TEST_REQUIRE(ABE_NET_PACKET_TOO_LARGE == ABE_PACKET_TOO_LARGE);
    TEST_REQUIRE(ABE_NET_INVALID_LENGTH == ABE_INVALID_LENGTH);
    TEST_REQUIRE(ABE_NET_WOULD_BLOCK == ABE_WOULD_BLOCK);

    TEST_REQUIRE(ABE_SHM_POOL_EXISTS == ABE_ALREADY_EXISTS);
    TEST_REQUIRE(ABE_SHM_POOL_NOT_FOUND == ABE_NOT_FOUND);

    TEST_REQUIRE(ABE_TIME_SYSTEM_ERROR == ABE_SYSTEM_ERROR);
    TEST_REQUIRE(ABE_TIME_OVERFLOW == ABE_OVERFLOW);
    TEST_REQUIRE(ABE_TIMER_LIMIT == ABE_LIMIT);

    TEST_REQUIRE(strcmp(abe_status_name(ABE_OK), "ABE_OK") == 0);
    TEST_REQUIRE(strcmp(abe_status_name(ABE_NOT_FOUND), "ABE_NOT_FOUND") == 0);
    TEST_REQUIRE(strcmp(abe_status_name(12345), "ABE_UNKNOWN_STATUS") == 0);
    return 0;
}
