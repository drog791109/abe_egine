#include "abe_math_adapter.h"

#include "../../abe_test.h"

namespace math = abe::adapter::math;

static bool near_f(float a, float b)
{
    return math::abs_f(a - b) <= 0.0001f;
}

static int test_vector2d(void)
{
    math::Vector2d a(3.0f, 4.0f);
    math::Vector2d b(1.0f, 2.0f);
    math::Vector2d result;

    result = a.add(b);
    TEST_REQUIRE(result.equal_epsilon(math::Vector2d(4.0f, 6.0f), 0.0001f));
    result = a * b;
    TEST_REQUIRE(result.equal_epsilon(math::Vector2d(3.0f, 8.0f), 0.0001f));
    result = a * 2.0f;
    TEST_REQUIRE(result.equal_epsilon(math::Vector2d(6.0f, 8.0f), 0.0001f));
    result = 0.5f * a;
    TEST_REQUIRE(result.equal_epsilon(math::Vector2d(1.5f, 2.0f), 0.0001f));
    result = a / 2.0f;
    TEST_REQUIRE(result.equal_epsilon(math::Vector2d(1.5f, 2.0f), 0.0001f));
    result = a / 0.0f;
    TEST_REQUIRE(result.equal_epsilon(math::Vector2d::zero(), 0.0001f));
    TEST_REQUIRE(near_f(a.dot(b), 11.0f));
    TEST_REQUIRE(near_f(a.cross(b), 2.0f));
    TEST_REQUIRE(near_f(a.length(), 5.0f));
    TEST_REQUIRE(near_f(a.distance(b), math::sqrt_f(8.0f)));

    result = a.normalize();
    TEST_REQUIRE(result.equal_epsilon(math::Vector2d(0.6f, 0.8f), 0.0001f));

    result = math::Vector2d(-1.0f, 9.0f).clamp(
        math::Vector2d(0.0f, 0.0f),
        math::Vector2d(5.0f, 5.0f));
    TEST_REQUIRE(result.equal_epsilon(math::Vector2d(0.0f, 5.0f), 0.0001f));

    result.set(8.0f, 10.0f);
    TEST_REQUIRE(result.x == 8.0f && result.y == 10.0f);
    TEST_REQUIRE(math::Vector2d(result.to_c()).equal_epsilon(result, 0.0001f));
    return ABE_TEST_STATUS_OK;
}

static int test_vector3d(void)
{
    math::Vector3d a(1.0f, 0.0f, 0.0f);
    math::Vector3d b(0.0f, 1.0f, 0.0f);
    math::Vector3d result;

    result = a.cross(b);
    TEST_REQUIRE(result.equal_epsilon(math::Vector3d(0.0f, 0.0f, 1.0f), 0.0001f));
    TEST_REQUIRE(near_f(a.dot(b), 0.0f));

    result = math::Vector3d(1.0f, 2.0f, 3.0f).add(math::Vector3d(4.0f, 5.0f, 6.0f));
    TEST_REQUIRE(result.equal_epsilon(math::Vector3d(5.0f, 7.0f, 9.0f), 0.0001f));
    result = math::Vector3d(1.0f, 2.0f, 3.0f) * math::Vector3d(4.0f, 5.0f, 6.0f);
    TEST_REQUIRE(result.equal_epsilon(math::Vector3d(4.0f, 10.0f, 18.0f), 0.0001f));
    result = math::Vector3d(2.0f, 3.0f, 6.0f) * 2.0f;
    TEST_REQUIRE(result.equal_epsilon(math::Vector3d(4.0f, 6.0f, 12.0f), 0.0001f));
    result = 0.5f * math::Vector3d(2.0f, 4.0f, 6.0f);
    TEST_REQUIRE(result.equal_epsilon(math::Vector3d(1.0f, 2.0f, 3.0f), 0.0001f));
    result = math::Vector3d(2.0f, 4.0f, 6.0f) / 2.0f;
    TEST_REQUIRE(result.equal_epsilon(math::Vector3d(1.0f, 2.0f, 3.0f), 0.0001f));
    result = math::Vector3d(2.0f, 4.0f, 6.0f) / 0.0f;
    TEST_REQUIRE(result.equal_epsilon(math::Vector3d::zero(), 0.0001f));
    TEST_REQUIRE(near_f(math::Vector3d(2.0f, 3.0f, 6.0f).length(), 7.0f));

    result = math::Vector3d(0.0f, 0.0f, 0.0f).lerp(
        math::Vector3d(10.0f, 20.0f, 30.0f),
        0.5f);
    TEST_REQUIRE(result.equal_epsilon(math::Vector3d(5.0f, 10.0f, 15.0f), 0.0001f));

    result.set(7.0f, 8.0f, 9.0f);
    TEST_REQUIRE(result.x == 7.0f && result.y == 8.0f && result.z == 9.0f);
    TEST_REQUIRE(math::Vector3d(result.to_c()).equal_epsilon(result, 0.0001f));
    return ABE_TEST_STATUS_OK;
}

static int test_scalar_math(void)
{
    TEST_REQUIRE(near_f(math::clamp_f(2.5f, 0.0f, 2.0f), 2.0f));
    TEST_REQUIRE(near_f(math::floor_f(2.9f), 2.0f));
    TEST_REQUIRE(near_f(math::round_f(2.5f), 3.0f));
    TEST_REQUIRE(near_f(math::sin_f((float)ABE_MATH_HALF_PI), 1.0f));
    TEST_REQUIRE(near_f(math::cos_f(0.0f), 1.0f));
    TEST_REQUIRE(near_f(math::lerp_f(10.0f, 20.0f, 0.25f), 12.5f));
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_scalar_math() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_vector2d() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_vector3d() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
