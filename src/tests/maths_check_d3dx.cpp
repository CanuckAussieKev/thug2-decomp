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

} // namespace tests
