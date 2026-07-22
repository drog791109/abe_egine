#include "abe_math.h"

#include <stdint.h>
#include <stdio.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

static int near_f(float a, float b)
{
    return abe_math_abs_f(a - b) <= 0.0001f;
}

static int test_scalar_math(void)
{
    TEST_REQUIRE(near_f(abe_math_abs_f(-3.5f), 3.5f));
    TEST_REQUIRE(abe_math_abs_i32(-7) == 7);
    TEST_REQUIRE(abe_math_abs_i32(INT32_MIN) == INT32_MAX);
    TEST_REQUIRE(abe_math_abs_i64(INT64_MIN) == INT64_MAX);
    TEST_REQUIRE(abe_math_clamp_i32(15, 1, 9) == 9);
    TEST_REQUIRE(abe_math_clamp_i32(-2, 1, 9) == 1);
    TEST_REQUIRE(abe_math_clamp_u32(5u, 1u, 9u) == 5u);
    TEST_REQUIRE(near_f(abe_math_clamp_f(2.5f, 0.0f, 2.0f), 2.0f));
    TEST_REQUIRE(near_f(abe_math_floor_f(2.9f), 2.0f));
    TEST_REQUIRE(near_f(abe_math_round_f(2.5f), 3.0f));
    TEST_REQUIRE(near_f(abe_math_sin_f((float)ABE_MATH_HALF_PI), 1.0f));
    TEST_REQUIRE(near_f(abe_math_cos_f(0.0f), 1.0f));
    TEST_REQUIRE(near_f(abe_math_lerp_f(10.0f, 20.0f, 0.25f), 12.5f));
    return 0;
}

static int test_vec2(void)
{
    abe_vec2_t a;
    abe_vec2_t b;
    abe_vec2_t result;

    a = abe_vec2(3.0f, 4.0f);
    b = abe_vec2(1.0f, 2.0f);
    result = abe_vec2_add(a, b);
    TEST_REQUIRE(abe_vec2_equal_epsilon(result, abe_vec2(4.0f, 6.0f), 0.0001f));
    TEST_REQUIRE(near_f(abe_vec2_dot(a, b), 11.0f));
    TEST_REQUIRE(near_f(abe_vec2_cross(a, b), 2.0f));
    TEST_REQUIRE(near_f(abe_vec2_length(a), 5.0f));
    TEST_REQUIRE(near_f(abe_vec2_distance(a, b), abe_math_sqrt_f(8.0f)));
    result = abe_vec2_normalize(a);
    TEST_REQUIRE(abe_vec2_equal_epsilon(result, abe_vec2(0.6f, 0.8f), 0.0001f));
    result = abe_vec2_clamp(abe_vec2(-1.0f, 9.0f), abe_vec2(0.0f, 0.0f), abe_vec2(5.0f, 5.0f));
    TEST_REQUIRE(abe_vec2_equal_epsilon(result, abe_vec2(0.0f, 5.0f), 0.0001f));
    return 0;
}

static int test_vec3(void)
{
    abe_vec3_t a;
    abe_vec3_t b;
    abe_vec3_t result;

    a = abe_vec3(1.0f, 0.0f, 0.0f);
    b = abe_vec3(0.0f, 1.0f, 0.0f);
    result = abe_vec3_cross(a, b);
    TEST_REQUIRE(abe_vec3_equal_epsilon(result, abe_vec3(0.0f, 0.0f, 1.0f), 0.0001f));
    TEST_REQUIRE(near_f(abe_vec3_dot(a, b), 0.0f));
    result = abe_vec3_add(abe_vec3(1.0f, 2.0f, 3.0f), abe_vec3(4.0f, 5.0f, 6.0f));
    TEST_REQUIRE(abe_vec3_equal_epsilon(result, abe_vec3(5.0f, 7.0f, 9.0f), 0.0001f));
    TEST_REQUIRE(near_f(abe_vec3_length(abe_vec3(2.0f, 3.0f, 6.0f)), 7.0f));
    result = abe_vec3_lerp(abe_vec3(0.0f, 0.0f, 0.0f), abe_vec3(10.0f, 20.0f, 30.0f), 0.5f);
    TEST_REQUIRE(abe_vec3_equal_epsilon(result, abe_vec3(5.0f, 10.0f, 15.0f), 0.0001f));
    return 0;
}

int main(void)
{
    if (test_scalar_math() != 0) {
        return 1;
    }
    if (test_vec2() != 0) {
        return 1;
    }
    if (test_vec3() != 0) {
        return 1;
    }
    return 0;
}
