#include "abe_math_adapter.h"

namespace abe {
namespace adapter {
namespace math {

Vector2d::Vector2d()
    : x(0.0f),
      y(0.0f)
{
}

Vector2d::Vector2d(float x_value, float y_value)
    : x(x_value),
      y(y_value)
{
}

Vector2d::Vector2d(const abe_vec2_t& value)
    : x(value.x),
      y(value.y)
{
}

Vector2d Vector2d::zero()
{
    return Vector2d(abe_vec2_zero());
}

abe_vec2_t Vector2d::to_c() const
{
    return abe_vec2(x, y);
}

void Vector2d::set(float x_value, float y_value)
{
    x = x_value;
    y = y_value;
}

Vector2d Vector2d::add(const Vector2d& other) const
{
    return Vector2d(abe_vec2_add(to_c(), other.to_c()));
}

Vector2d Vector2d::sub(const Vector2d& other) const
{
    return Vector2d(abe_vec2_sub(to_c(), other.to_c()));
}

Vector2d Vector2d::scale(float scale_value) const
{
    return Vector2d(abe_vec2_scale(to_c(), scale_value));
}

Vector2d Vector2d::operator*(const Vector2d& other) const
{
    return Vector2d(abe_vec2_mul(to_c(), other.to_c()));
}

Vector2d Vector2d::operator*(float scale_value) const
{
    return Vector2d(abe_vec2_scale(to_c(), scale_value));
}

Vector2d Vector2d::operator/(float scale_value) const
{
    return Vector2d(abe_vec2_div_scale(to_c(), scale_value));
}

float Vector2d::dot(const Vector2d& other) const
{
    return abe_vec2_dot(to_c(), other.to_c());
}

float Vector2d::cross(const Vector2d& other) const
{
    return abe_vec2_cross(to_c(), other.to_c());
}

float Vector2d::length_sq() const
{
    return abe_vec2_length_sq(to_c());
}

float Vector2d::length() const
{
    return abe_vec2_length(to_c());
}

float Vector2d::distance_sq(const Vector2d& other) const
{
    return abe_vec2_distance_sq(to_c(), other.to_c());
}

float Vector2d::distance(const Vector2d& other) const
{
    return abe_vec2_distance(to_c(), other.to_c());
}

Vector2d Vector2d::normalize() const
{
    return Vector2d(abe_vec2_normalize(to_c()));
}

Vector2d Vector2d::lerp(const Vector2d& to_value, float t) const
{
    return Vector2d(abe_vec2_lerp(to_c(), to_value.to_c(), t));
}

Vector2d Vector2d::clamp(const Vector2d& min_value, const Vector2d& max_value) const
{
    return Vector2d(abe_vec2_clamp(to_c(), min_value.to_c(), max_value.to_c()));
}

int Vector2d::equal_epsilon(const Vector2d& other, float epsilon) const
{
    return abe_vec2_equal_epsilon(to_c(), other.to_c(), epsilon);
}

Vector3d::Vector3d()
    : x(0.0f),
      y(0.0f),
      z(0.0f)
{
}

Vector3d::Vector3d(float x_value, float y_value, float z_value)
    : x(x_value),
      y(y_value),
      z(z_value)
{
}

Vector3d::Vector3d(const abe_vec3_t& value)
    : x(value.x),
      y(value.y),
      z(value.z)
{
}

Vector3d Vector3d::zero()
{
    return Vector3d(abe_vec3_zero());
}

abe_vec3_t Vector3d::to_c() const
{
    return abe_vec3(x, y, z);
}

void Vector3d::set(float x_value, float y_value, float z_value)
{
    x = x_value;
    y = y_value;
    z = z_value;
}

Vector3d Vector3d::add(const Vector3d& other) const
{
    return Vector3d(abe_vec3_add(to_c(), other.to_c()));
}

Vector3d Vector3d::sub(const Vector3d& other) const
{
    return Vector3d(abe_vec3_sub(to_c(), other.to_c()));
}

Vector3d Vector3d::scale(float scale_value) const
{
    return Vector3d(abe_vec3_scale(to_c(), scale_value));
}

Vector3d Vector3d::operator*(const Vector3d& other) const
{
    return Vector3d(abe_vec3_mul(to_c(), other.to_c()));
}

Vector3d Vector3d::operator*(float scale_value) const
{
    return Vector3d(abe_vec3_scale(to_c(), scale_value));
}

Vector3d Vector3d::operator/(float scale_value) const
{
    return Vector3d(abe_vec3_div_scale(to_c(), scale_value));
}

float Vector3d::dot(const Vector3d& other) const
{
    return abe_vec3_dot(to_c(), other.to_c());
}

Vector3d Vector3d::cross(const Vector3d& other) const
{
    return Vector3d(abe_vec3_cross(to_c(), other.to_c()));
}

float Vector3d::length_sq() const
{
    return abe_vec3_length_sq(to_c());
}

float Vector3d::length() const
{
    return abe_vec3_length(to_c());
}

float Vector3d::distance_sq(const Vector3d& other) const
{
    return abe_vec3_distance_sq(to_c(), other.to_c());
}

float Vector3d::distance(const Vector3d& other) const
{
    return abe_vec3_distance(to_c(), other.to_c());
}

Vector3d Vector3d::normalize() const
{
    return Vector3d(abe_vec3_normalize(to_c()));
}

Vector3d Vector3d::lerp(const Vector3d& to_value, float t) const
{
    return Vector3d(abe_vec3_lerp(to_c(), to_value.to_c(), t));
}

Vector3d Vector3d::clamp(const Vector3d& min_value, const Vector3d& max_value) const
{
    return Vector3d(abe_vec3_clamp(to_c(), min_value.to_c(), max_value.to_c()));
}

int Vector3d::equal_epsilon(const Vector3d& other, float epsilon) const
{
    return abe_vec3_equal_epsilon(to_c(), other.to_c(), epsilon);
}

Vector2d operator*(float scale_value, const Vector2d& value)
{
    return value * scale_value;
}

Vector3d operator*(float scale_value, const Vector3d& value)
{
    return value * scale_value;
}

float abs_f(float value)
{
    return abe_math_abs_f(value);
}

float clamp_f(float value, float min_value, float max_value)
{
    return abe_math_clamp_f(value, min_value, max_value);
}

float floor_f(float value)
{
    return abe_math_floor_f(value);
}

float round_f(float value)
{
    return abe_math_round_f(value);
}

float sin_f(float radians)
{
    return abe_math_sin_f(radians);
}

float cos_f(float radians)
{
    return abe_math_cos_f(radians);
}

float sqrt_f(float value)
{
    return abe_math_sqrt_f(value);
}

float lerp_f(float from_value, float to_value, float t)
{
    return abe_math_lerp_f(from_value, to_value, t);
}

} /* namespace math */
} /* namespace adapter */
} /* namespace abe */
