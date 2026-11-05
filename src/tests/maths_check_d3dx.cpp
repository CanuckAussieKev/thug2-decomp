#include "tests/maths_check_d3dx.h"

#include <cstddef>
#include <cstring>

#include <windows.h>

#include <d3dx9.h>

namespace tests {

void d3dx_matrix_multiply(float* out, const float* a, const float* b)
{
    constexpr std::size_t kMatrixBytes = 64;
    D3DXMATRIX left;
    D3DXMATRIX right;
    std::memcpy(static_cast<float*>(left), a, kMatrixBytes);
    std::memcpy(static_cast<float*>(right), b, kMatrixBytes);
    D3DXMATRIX product;
    D3DXMatrixMultiply(&product, &left, &right);
    std::memcpy(out, static_cast<const float*>(product), kMatrixBytes);
}

namespace {

constexpr std::size_t kMatrixBytes = 64;
constexpr std::size_t kVector3Bytes = 12;
constexpr std::size_t kVector4Bytes = 16;

D3DXMATRIX matrix_of(ConstMatrix16 matrix)
{
    D3DXMATRIX out;
    std::memcpy(static_cast<float*>(out), matrix.data(), kMatrixBytes);
    return out;
}
D3DXVECTOR3 vector3_of(Vector3In v)
{
    D3DXVECTOR3 out;
    std::memcpy(static_cast<float*>(out), v.data(), kVector3Bytes);
    return out;
}
D3DXVECTOR4 vector4_of(Vector4In v)
{
    D3DXVECTOR4 out;
    std::memcpy(static_cast<float*>(out), v.data(), kVector4Bytes);
    return out;
}
void put(Matrix16 out, const D3DXMATRIX& matrix) { std::memcpy(out.data(), static_cast<const float*>(matrix), kMatrixBytes); }
void put(Vector3Out out, const D3DXVECTOR3& v) { std::memcpy(out.data(), static_cast<const float*>(v), kVector3Bytes); }
void put(Vector4Out out, const D3DXVECTOR4& v) { std::memcpy(out.data(), static_cast<const float*>(v), kVector4Bytes); }

} // namespace

void d3dx_vec3_transform(Vector4Out out, Vector3In v, ConstMatrix16 matrix)
{
    const D3DXVECTOR3 in = vector3_of(v);
    const D3DXMATRIX by = matrix_of(matrix);
    D3DXVECTOR4 result;
    D3DXVec3Transform(&result, &in, &by);
    put(out, result);
}
void d3dx_vec3_transform_coord(Vector3Out out, Vector3In v, ConstMatrix16 matrix)
{
    const D3DXVECTOR3 in = vector3_of(v);
    const D3DXMATRIX by = matrix_of(matrix);
    D3DXVECTOR3 result;
    D3DXVec3TransformCoord(&result, &in, &by);
    put(out, result);
}
void d3dx_vec3_transform_normal(Vector3Out out, Vector3In v, ConstMatrix16 matrix)
{
    const D3DXVECTOR3 in = vector3_of(v);
    const D3DXMATRIX by = matrix_of(matrix);
    D3DXVECTOR3 result;
    D3DXVec3TransformNormal(&result, &in, &by);
    put(out, result);
}
void d3dx_vec4_transform(Vector4Out out, Vector4In v, ConstMatrix16 matrix)
{
    const D3DXVECTOR4 in = vector4_of(v);
    const D3DXMATRIX by = matrix_of(matrix);
    D3DXVECTOR4 result;
    D3DXVec4Transform(&result, &in, &by);
    put(out, result);
}
void d3dx_vec3_normalize(Vector3Out out, Vector3In v)
{
    const D3DXVECTOR3 in = vector3_of(v);
    D3DXVECTOR3 result;
    D3DXVec3Normalize(&result, &in);
    put(out, result);
}
void d3dx_matrix_transpose(Matrix16 out, ConstMatrix16 matrix)
{
    const D3DXMATRIX in = matrix_of(matrix);
    D3DXMATRIX result;
    D3DXMatrixTranspose(&result, &in);
    put(out, result);
}
void d3dx_matrix_scaling(Matrix16 out, float x, float y, float z)
{
    D3DXMATRIX result;
    D3DXMatrixScaling(&result, x, y, z);
    put(out, result);
}
void d3dx_matrix_rotation_y(Matrix16 out, float angle)
{
    D3DXMATRIX result;
    D3DXMatrixRotationY(&result, angle);
    put(out, result);
}
bool d3dx_matrix_inverse(Matrix16 out, float* determinant, ConstMatrix16 matrix)
{
    const D3DXMATRIX in = matrix_of(matrix);
    D3DXMATRIX result = matrix_of(out);
    const bool made = D3DXMatrixInverse(&result, determinant, &in) != nullptr;
    put(out, result);
    return made;
}
void d3dx_matrix_look_at_rh(Matrix16 out, LookAt look)
{
    const D3DXVECTOR3 from = vector3_of(look.eye);
    const D3DXVECTOR3 to = vector3_of(look.at);
    const D3DXVECTOR3 top = vector3_of(look.up);
    D3DXMATRIX result;
    D3DXMatrixLookAtRH(&result, &from, &to, &top);
    put(out, result);
}
void d3dx_matrix_perspective_rh(Matrix16 out, float width, float height, float near_z, float far_z)
{
    D3DXMATRIX result;
    D3DXMatrixPerspectiveRH(&result, width, height, near_z, far_z);
    put(out, result);
}
void d3dx_matrix_ortho_rh(Matrix16 out, float width, float height, float near_z, float far_z)
{
    D3DXMATRIX result;
    D3DXMatrixOrthoRH(&result, width, height, near_z, far_z);
    put(out, result);
}

} // namespace tests
