#pragma once

#include "base/maths/mat4.h"
#include <span>

// NOT ORIGINAL - thug2_maths_check's references for maths::matrix_multiply (D3DXMatrixMultiply, d3dx9_43.dll) and the
// vector and viewing maths (base/maths/view_transforms.h), in a file of its own (windows.h's macros stay out of maths_check.cpp). 16 floats each, row by row; `out` may be a or b.
namespace tests {

void d3dx_matrix_multiply(float* out, const float* a, const float* b);

// NOT ORIGINAL - the references for base/maths/view_transforms.h: D3DX9's own functions (`out` never an input). A
// matrix is maths::Mat4's 16 floats.
using Matrix16 = std::span<float, maths::kMatrixFloats>;
using ConstMatrix16 = std::span<const float, maths::kMatrixFloats>;
using Vector3Out = std::span<float, 3>;
using Vector3In = std::span<const float, 3>;
using Vector4Out = std::span<float, 4>;
using Vector4In = std::span<const float, 4>;
void d3dx_vec3_transform(Vector4Out out, Vector3In v, ConstMatrix16 matrix);
void d3dx_vec3_transform_coord(Vector3Out out, Vector3In v, ConstMatrix16 matrix);
void d3dx_vec3_transform_normal(Vector3Out out, Vector3In v, ConstMatrix16 matrix);
void d3dx_vec4_transform(Vector4Out out, Vector4In v, ConstMatrix16 matrix);
void d3dx_vec3_normalize(Vector3Out out, Vector3In v);
void d3dx_matrix_transpose(Matrix16 out, ConstMatrix16 matrix);
void d3dx_matrix_scaling(Matrix16 out, float x, float y, float z);
void d3dx_matrix_rotation_y(Matrix16 out, float angle);
// The determinant goes to *determinant; false when D3DX found none (out left as it was).
bool d3dx_matrix_inverse(Matrix16 out, float* determinant, ConstMatrix16 matrix);
// The viewer's position, where it looks and its up.
struct LookAt {
    Vector3In eye;
    Vector3In at;
    Vector3In up;
};
void d3dx_matrix_look_at_rh(Matrix16 out, LookAt look);
void d3dx_matrix_perspective_rh(Matrix16 out, float width, float height, float near_z, float far_z);
void d3dx_matrix_ortho_rh(Matrix16 out, float width, float height, float near_z, float far_z);

} // namespace tests
