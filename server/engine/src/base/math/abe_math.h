#ifndef ABE_MATH_H
#define ABE_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABE_MATH_PI 3.14159265358979323846
#define ABE_MATH_HALF_PI 1.57079632679489661923
#define ABE_MATH_TWO_PI 6.28318530717958647692

typedef struct abe_vec2 {
    float x;
    float y;
} abe_vec2_t;

typedef struct abe_vec3 {
    float x;
    float y;
    float z;
} abe_vec3_t;

float abe_math_abs_f(float value);
double abe_math_abs(double value);
int32_t abe_math_abs_i32(int32_t value);
int64_t abe_math_abs_i64(int64_t value);

float abe_math_clamp_f(float value, float min_value, float max_value);
double abe_math_clamp(double value, double min_value, double max_value);
int32_t abe_math_clamp_i32(int32_t value, int32_t min_value, int32_t max_value);
uint32_t abe_math_clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value);

float abe_math_floor_f(float value);
double abe_math_floor(double value);
float abe_math_round_f(float value);
double abe_math_round(double value);
float abe_math_sin_f(float radians);
double abe_math_sin(double radians);
float abe_math_cos_f(float radians);
double abe_math_cos(double radians);
float abe_math_sqrt_f(float value);
double abe_math_sqrt(double value);
float abe_math_lerp_f(float from_value, float to_value, float t);

abe_vec2_t abe_vec2(float x, float y);
abe_vec2_t abe_vec2_zero(void);
abe_vec2_t abe_vec2_add(abe_vec2_t a, abe_vec2_t b);
abe_vec2_t abe_vec2_sub(abe_vec2_t a, abe_vec2_t b);
abe_vec2_t abe_vec2_mul(abe_vec2_t a, abe_vec2_t b);
abe_vec2_t abe_vec2_scale(abe_vec2_t value, float scale);
abe_vec2_t abe_vec2_div_scale(abe_vec2_t value, float scale);
float abe_vec2_dot(abe_vec2_t a, abe_vec2_t b);
float abe_vec2_cross(abe_vec2_t a, abe_vec2_t b);
float abe_vec2_length_sq(abe_vec2_t value);
float abe_vec2_length(abe_vec2_t value);
float abe_vec2_distance_sq(abe_vec2_t a, abe_vec2_t b);
float abe_vec2_distance(abe_vec2_t a, abe_vec2_t b);
abe_vec2_t abe_vec2_normalize(abe_vec2_t value);
abe_vec2_t abe_vec2_lerp(abe_vec2_t from_value, abe_vec2_t to_value, float t);
abe_vec2_t abe_vec2_clamp(abe_vec2_t value, abe_vec2_t min_value, abe_vec2_t max_value);
int abe_vec2_equal_epsilon(abe_vec2_t a, abe_vec2_t b, float epsilon);

abe_vec3_t abe_vec3(float x, float y, float z);
abe_vec3_t abe_vec3_zero(void);
abe_vec3_t abe_vec3_add(abe_vec3_t a, abe_vec3_t b);
abe_vec3_t abe_vec3_sub(abe_vec3_t a, abe_vec3_t b);
abe_vec3_t abe_vec3_mul(abe_vec3_t a, abe_vec3_t b);
abe_vec3_t abe_vec3_scale(abe_vec3_t value, float scale);
abe_vec3_t abe_vec3_div_scale(abe_vec3_t value, float scale);
float abe_vec3_dot(abe_vec3_t a, abe_vec3_t b);
abe_vec3_t abe_vec3_cross(abe_vec3_t a, abe_vec3_t b);
float abe_vec3_length_sq(abe_vec3_t value);
float abe_vec3_length(abe_vec3_t value);
float abe_vec3_distance_sq(abe_vec3_t a, abe_vec3_t b);
float abe_vec3_distance(abe_vec3_t a, abe_vec3_t b);
abe_vec3_t abe_vec3_normalize(abe_vec3_t value);
abe_vec3_t abe_vec3_lerp(abe_vec3_t from_value, abe_vec3_t to_value, float t);
abe_vec3_t abe_vec3_clamp(abe_vec3_t value, abe_vec3_t min_value, abe_vec3_t max_value);
int abe_vec3_equal_epsilon(abe_vec3_t a, abe_vec3_t b, float epsilon);

#ifdef __cplusplus
}
#endif

#endif /* ABE_MATH_H */
