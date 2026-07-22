#include "abe_math.h"

#include <limits.h>
#include <math.h>

float abe_math_abs_f(float value)
{
    return fabsf(value);
}

double abe_math_abs(double value)
{
    return fabs(value);
}

int32_t abe_math_abs_i32(int32_t value)
{
    if (value == INT32_MIN) {
        return INT32_MAX;
    }
    return value < 0 ? -value : value;
}

int64_t abe_math_abs_i64(int64_t value)
{
    if (value == INT64_MIN) {
        return INT64_MAX;
    }
    return value < 0 ? -value : value;
}

float abe_math_clamp_f(float value, float min_value, float max_value)
{
    if (min_value > max_value) {
        float tmp;

        tmp = min_value;
        min_value = max_value;
        max_value = tmp;
    }
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

double abe_math_clamp(double value, double min_value, double max_value)
{
    if (min_value > max_value) {
        double tmp;

        tmp = min_value;
        min_value = max_value;
        max_value = tmp;
    }
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

int32_t abe_math_clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (min_value > max_value) {
        int32_t tmp;

        tmp = min_value;
        min_value = max_value;
        max_value = tmp;
    }
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

uint32_t abe_math_clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value)
{
    if (min_value > max_value) {
        uint32_t tmp;

        tmp = min_value;
        min_value = max_value;
        max_value = tmp;
    }
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

float abe_math_floor_f(float value)
{
    return floorf(value);
}

double abe_math_floor(double value)
{
    return floor(value);
}

float abe_math_round_f(float value)
{
    return roundf(value);
}

double abe_math_round(double value)
{
    return round(value);
}

float abe_math_sin_f(float radians)
{
    return sinf(radians);
}

double abe_math_sin(double radians)
{
    return sin(radians);
}

float abe_math_cos_f(float radians)
{
    return cosf(radians);
}

double abe_math_cos(double radians)
{
    return cos(radians);
}

float abe_math_sqrt_f(float value)
{
    return sqrtf(value);
}

double abe_math_sqrt(double value)
{
    return sqrt(value);
}

float abe_math_lerp_f(float from_value, float to_value, float t)
{
    return from_value + ((to_value - from_value) * t);
}

abe_vec2_t abe_vec2(float x, float y)
{
    abe_vec2_t value;

    value.x = x;
    value.y = y;
    return value;
}

abe_vec2_t abe_vec2_zero(void)
{
    return abe_vec2(0.0f, 0.0f);
}

abe_vec2_t abe_vec2_add(abe_vec2_t a, abe_vec2_t b)
{
    return abe_vec2(a.x + b.x, a.y + b.y);
}

abe_vec2_t abe_vec2_sub(abe_vec2_t a, abe_vec2_t b)
{
    return abe_vec2(a.x - b.x, a.y - b.y);
}

abe_vec2_t abe_vec2_mul(abe_vec2_t a, abe_vec2_t b)
{
    return abe_vec2(a.x * b.x, a.y * b.y);
}

abe_vec2_t abe_vec2_scale(abe_vec2_t value, float scale)
{
    return abe_vec2(value.x * scale, value.y * scale);
}

abe_vec2_t abe_vec2_div_scale(abe_vec2_t value, float scale)
{
    if (scale == 0.0f) {
        return abe_vec2_zero();
    }
    return abe_vec2(value.x / scale, value.y / scale);
}

float abe_vec2_dot(abe_vec2_t a, abe_vec2_t b)
{
    return (a.x * b.x) + (a.y * b.y);
}

float abe_vec2_cross(abe_vec2_t a, abe_vec2_t b)
{
    return (a.x * b.y) - (a.y * b.x);
}

float abe_vec2_length_sq(abe_vec2_t value)
{
    return abe_vec2_dot(value, value);
}

float abe_vec2_length(abe_vec2_t value)
{
    return abe_math_sqrt_f(abe_vec2_length_sq(value));
}

float abe_vec2_distance_sq(abe_vec2_t a, abe_vec2_t b)
{
    return abe_vec2_length_sq(abe_vec2_sub(a, b));
}

float abe_vec2_distance(abe_vec2_t a, abe_vec2_t b)
{
    return abe_math_sqrt_f(abe_vec2_distance_sq(a, b));
}

abe_vec2_t abe_vec2_normalize(abe_vec2_t value)
{
    float length;

    length = abe_vec2_length(value);
    if (length == 0.0f) {
        return abe_vec2_zero();
    }
    return abe_vec2_div_scale(value, length);
}

abe_vec2_t abe_vec2_lerp(abe_vec2_t from_value, abe_vec2_t to_value, float t)
{
    return abe_vec2(
        abe_math_lerp_f(from_value.x, to_value.x, t),
        abe_math_lerp_f(from_value.y, to_value.y, t));
}

abe_vec2_t abe_vec2_clamp(abe_vec2_t value, abe_vec2_t min_value, abe_vec2_t max_value)
{
    return abe_vec2(
        abe_math_clamp_f(value.x, min_value.x, max_value.x),
        abe_math_clamp_f(value.y, min_value.y, max_value.y));
}

int abe_vec2_equal_epsilon(abe_vec2_t a, abe_vec2_t b, float epsilon)
{
    return abe_math_abs_f(a.x - b.x) <= epsilon &&
        abe_math_abs_f(a.y - b.y) <= epsilon;
}

abe_vec3_t abe_vec3(float x, float y, float z)
{
    abe_vec3_t value;

    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}

abe_vec3_t abe_vec3_zero(void)
{
    return abe_vec3(0.0f, 0.0f, 0.0f);
}

abe_vec3_t abe_vec3_add(abe_vec3_t a, abe_vec3_t b)
{
    return abe_vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

abe_vec3_t abe_vec3_sub(abe_vec3_t a, abe_vec3_t b)
{
    return abe_vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

abe_vec3_t abe_vec3_mul(abe_vec3_t a, abe_vec3_t b)
{
    return abe_vec3(a.x * b.x, a.y * b.y, a.z * b.z);
}

abe_vec3_t abe_vec3_scale(abe_vec3_t value, float scale)
{
    return abe_vec3(value.x * scale, value.y * scale, value.z * scale);
}

abe_vec3_t abe_vec3_div_scale(abe_vec3_t value, float scale)
{
    if (scale == 0.0f) {
        return abe_vec3_zero();
    }
    return abe_vec3(value.x / scale, value.y / scale, value.z / scale);
}

float abe_vec3_dot(abe_vec3_t a, abe_vec3_t b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

abe_vec3_t abe_vec3_cross(abe_vec3_t a, abe_vec3_t b)
{
    return abe_vec3(
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x));
}

float abe_vec3_length_sq(abe_vec3_t value)
{
    return abe_vec3_dot(value, value);
}

float abe_vec3_length(abe_vec3_t value)
{
    return abe_math_sqrt_f(abe_vec3_length_sq(value));
}

float abe_vec3_distance_sq(abe_vec3_t a, abe_vec3_t b)
{
    return abe_vec3_length_sq(abe_vec3_sub(a, b));
}

float abe_vec3_distance(abe_vec3_t a, abe_vec3_t b)
{
    return abe_math_sqrt_f(abe_vec3_distance_sq(a, b));
}

abe_vec3_t abe_vec3_normalize(abe_vec3_t value)
{
    float length;

    length = abe_vec3_length(value);
    if (length == 0.0f) {
        return abe_vec3_zero();
    }
    return abe_vec3_div_scale(value, length);
}

abe_vec3_t abe_vec3_lerp(abe_vec3_t from_value, abe_vec3_t to_value, float t)
{
    return abe_vec3(
        abe_math_lerp_f(from_value.x, to_value.x, t),
        abe_math_lerp_f(from_value.y, to_value.y, t),
        abe_math_lerp_f(from_value.z, to_value.z, t));
}

abe_vec3_t abe_vec3_clamp(abe_vec3_t value, abe_vec3_t min_value, abe_vec3_t max_value)
{
    return abe_vec3(
        abe_math_clamp_f(value.x, min_value.x, max_value.x),
        abe_math_clamp_f(value.y, min_value.y, max_value.y),
        abe_math_clamp_f(value.z, min_value.z, max_value.z));
}

int abe_vec3_equal_epsilon(abe_vec3_t a, abe_vec3_t b, float epsilon)
{
    return abe_math_abs_f(a.x - b.x) <= epsilon &&
        abe_math_abs_f(a.y - b.y) <= epsilon &&
        abe_math_abs_f(a.z - b.z) <= epsilon;
}
