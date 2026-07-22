#ifndef ABE_MATH_ADAPTER_H
#define ABE_MATH_ADAPTER_H

#include "abe_math.h"

namespace abe {
namespace adapter {
namespace math {

class Vector2d {
public:
    float x;
    float y;

    Vector2d();
    Vector2d(float x_value, float y_value);
    explicit Vector2d(const abe_vec2_t& value);

    static Vector2d zero();

    abe_vec2_t to_c() const;
    void set(float x_value, float y_value);

    Vector2d add(const Vector2d& other) const;
    Vector2d sub(const Vector2d& other) const;
    Vector2d scale(float scale_value) const;
    Vector2d operator*(const Vector2d& other) const;
    Vector2d operator*(float scale_value) const;
    Vector2d operator/(float scale_value) const;

    float dot(const Vector2d& other) const;
    float cross(const Vector2d& other) const;
    float length_sq() const;
    float length() const;
    float distance_sq(const Vector2d& other) const;
    float distance(const Vector2d& other) const;

    Vector2d normalize() const;
    Vector2d lerp(const Vector2d& to_value, float t) const;
    Vector2d clamp(const Vector2d& min_value, const Vector2d& max_value) const;
    int equal_epsilon(const Vector2d& other, float epsilon) const;
};

class Vector3d {
public:
    float x;
    float y;
    float z;

    Vector3d();
    Vector3d(float x_value, float y_value, float z_value);
    explicit Vector3d(const abe_vec3_t& value);

    static Vector3d zero();

    abe_vec3_t to_c() const;
    void set(float x_value, float y_value, float z_value);

    Vector3d add(const Vector3d& other) const;
    Vector3d sub(const Vector3d& other) const;
    Vector3d scale(float scale_value) const;
    Vector3d operator*(const Vector3d& other) const;
    Vector3d operator*(float scale_value) const;
    Vector3d operator/(float scale_value) const;

    float dot(const Vector3d& other) const;
    Vector3d cross(const Vector3d& other) const;
    float length_sq() const;
    float length() const;
    float distance_sq(const Vector3d& other) const;
    float distance(const Vector3d& other) const;

    Vector3d normalize() const;
    Vector3d lerp(const Vector3d& to_value, float t) const;
    Vector3d clamp(const Vector3d& min_value, const Vector3d& max_value) const;
    int equal_epsilon(const Vector3d& other, float epsilon) const;
};

Vector2d operator*(float scale_value, const Vector2d& value);
Vector3d operator*(float scale_value, const Vector3d& value);

float abs_f(float value);
float clamp_f(float value, float min_value, float max_value);
float floor_f(float value);
float round_f(float value);
float sin_f(float radians);
float cos_f(float radians);
float sqrt_f(float value);
float lerp_f(float from_value, float to_value, float t);

} /* namespace math */
} /* namespace adapter */
} /* namespace abe */

#endif /* ABE_MATH_ADAPTER_H */
