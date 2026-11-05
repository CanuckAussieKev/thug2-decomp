#pragma once

#include <span>

#include "base/maths/mat4.h"

// NOT ORIGINAL - the vector transforms and the viewing matrices the renderer called D3DX9 for (once in d3dx9_43.dll),
// in plain C++: row vectors, 16-float row-major matrices (maths::Mat4's layout), the right-handed viewing
// functions. Each is the same operations in the same order as the D3DX9 the game ran with - found by comparing with it
// over many inputs (thug2_maths_check's "D3DX9's vector and viewing maths" section). D3DX's vector code is SSE (floats
// throughout); its matrix builders (the look-at's cross and dot products, the projections' quotients) are x87 code,
// worked here as the game's thread runs it: at the FPU's 53 bits, each result rounded once to a float.
// D3DX normalises and divides by w with the processor's estimates (rsqrtss / rcpss) refined by one Newton-Raphson step:
// so does this code where the compiler targets SSE (estimate_reciprocal_sqrt / estimate_reciprocal), which keeps the
// results bit for bit D3DX's on the same processor; without SSE it takes the exact quotient (a last-bit difference).
namespace maths {

using Floats16 = std::span<float, kMatrixFloats>;
using ConstFloats16 = std::span<const float, kMatrixFloats>;
using Floats3 = std::span<float, 3>;
using ConstFloats3 = std::span<const float, 3>;
using Floats4 = std::span<float, 4>;
using ConstFloats4 = std::span<const float, 4>;

// 1 / sqrt(x) and 1 / x as D3DX computes them: the processor's estimate and one Newton-Raphson step.
float estimate_reciprocal_sqrt(float x);
float estimate_reciprocal(float x);

// D3DXVec3Transform: (v, 1) * m, all four components (each sum (x m0 + y m1) + (z m2 + m3)).
void transform_point(Floats4 out, ConstFloats3 v, ConstFloats16 matrix);
// D3DXVec3TransformCoord: (v, 1) * m divided by its w (times w's estimated reciprocal).
void transform_coord(Floats3 out, ConstFloats3 v, ConstFloats16 matrix);
// D3DXVec3TransformNormal: v * m without the translation (each sum (x m0 + y m1) + z m2).
void transform_normal(Floats3 out, ConstFloats3 v, ConstFloats16 matrix);
// D3DXVec4Transform: v * m (each sum (x m0 + y m1) + (z m2 + w m3)).
void transform_vec4(Floats4 out, ConstFloats4 v, ConstFloats16 matrix);
// D3DXVec3Normalize: v at length 1 (v times its length's estimated reciprocal square root); shorter than 2^-23
// (FLT_EPSILON), 0, 0, 0.
void normalize3(Floats3 out, ConstFloats3 v);

// D3DXMatrixTranspose, D3DXMatrixScaling, D3DXMatrixRotationY (about y: cos, 0, -sin / 0, 1, 0 / sin, 0, cos).
void matrix_transpose(Floats16 out, ConstFloats16 matrix);
void matrix_scaling(Floats16 out, float x, float y, float z);
void matrix_rotation_y(Floats16 out, float angle);
// D3DXMatrixInverse: the inverse by the adjugate (the 2x2 sub-determinants of rows 0-1 and 2-3) over the determinant,
// which goes to `determinant` when it isn't null. False (out unchanged) when the determinant is 0. Not D3DX's own
// algorithm: its results differ from D3DX's in the last bits (the renderer's one caller doesn't use them).
bool matrix_inverse(Floats16 out, float* determinant, ConstFloats16 matrix);
// D3DXMatrixLookAtRH: the view from `eye` towards `at` with `up` - z = normalize(eye - at), x = normalize(up x z),
// y = z x x (the cross products at 53 bits); the rows' translation -(axis . eye) (the dot products at 53 bits).
void matrix_look_at_rh(Floats16 out, ConstFloats3 eye, ConstFloats3 at, ConstFloats3 up);
// D3DXMatrixPerspectiveRH: 2 zn / w, 2 zn / h, zf / (zn - zf) (the quotients at 53 bits), -1, zn times that.
void matrix_perspective_rh(Floats16 out, float width, float height, float near_z, float far_z);
// D3DXMatrixOrthoRH: 2 / w, 2 / h, 1 / (zn - zf) (the quotients at 53 bits), zn times that, 1.
void matrix_ortho_rh(Floats16 out, float width, float height, float near_z, float far_z);

// The same for the renderer's flat floats (3 or 4 a vector, as named; 16 a matrix), as D3DX took them.
void transform_point(float* out4, const float* v3, const float* matrix);
void transform_coord(float* out3, const float* v3, const float* matrix);
void transform_normal(float* out3, const float* v3, const float* matrix);
void transform_vec4(float* out4, const float* v4, const float* matrix);
void normalize3(float* out3, const float* v3);
void matrix_transpose(float* out, const float* matrix);
bool matrix_inverse(float* out, float* determinant, const float* matrix);
void matrix_look_at_rh(float* out, const float* eye, const float* at, const float* up);
void matrix_ortho_rh(float* out, float width, float height, float near_z, float far_z);

} // namespace maths
