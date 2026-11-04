#pragma once

// NOT ORIGINAL - thug2_maths_check's reference for maths::matrix_multiply: D3DXMatrixMultiply (d3dx9_43.dll), in a file
// of its own (windows.h's macros stay out of maths_check.cpp). 16 floats each, row by row; `out` may be a or b.
namespace tests {

void d3dx_matrix_multiply(float* out, const float* a, const float* b);

} // namespace tests
