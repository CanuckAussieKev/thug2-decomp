// NOT ORIGINAL - the vector transforms and viewing matrices in plain C++ (view_transforms.h). Every result is worked out
// into locals first and copied out last, so `out` may be the input (as D3DX allows).

#include "base/maths/view_transforms.h"

#include <algorithm>
#include <array>
#include <cmath> // IWYU pragma: keep - std::sqrt where the processor has no SSE estimate
#include <limits>

// (the processor's estimates where the compiler targets SSE: estimate_reciprocal_sqrt / estimate_reciprocal)
#if defined(__SSE__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1) || defined(_M_X64)
#include <xmmintrin.h>
#endif

#include "base/core/checked_index.h"
#include "base/maths/mat4.h"
#include "base/maths/trig.h"

namespace maths {

namespace {

constexpr int kSide = 4; // rows and columns
// The Newton-Raphson step for 1 / sqrt(x): y (3/2 - x y^2 / 2).
constexpr float kThreeHalves = 1.5F;
constexpr float kHalf = 0.5F;
// The shortest vector normalize3 normalises: length 2^-23 (FLT_EPSILON; D3DX's - shorter ones give 0, 0, 0).
constexpr float kShortestSquared = std::numeric_limits<float>::epsilon() * std::numeric_limits<float>::epsilon();
using Vector3 = std::array<float, 3>;
using Matrix = std::array<float, kMatrixFloats>;

// (row r, column c)
float element(ConstFloats16 matrix, int row, int column) { return core::at(matrix, (row * kSide) + column); }

// The row vector (v, w) times column j of m, as D3DX sums it: (x m0 + y m1) + (z m2 + w m3).
float row_times_column(ConstFloats4 v, ConstFloats16 matrix, int column)
{
    const float p0 = core::at(v, 0) * element(matrix, 0, column);
    const float p1 = core::at(v, 1) * element(matrix, 1, column);
    const float p2 = core::at(v, 2) * element(matrix, 2, column);
    const float p3 = core::at(v, 3) * element(matrix, 3, column);
    return (p0 + p1) + (p2 + p3);
}

// (x, y, z, 1)
std::array<float, 4> homogeneous(ConstFloats3 v) { return {{core::at(v, 0), core::at(v, 1), core::at(v, 2), 1.0F}}; }

float dot3(const Vector3& a, const Vector3& b)
{
    return ((core::at(a, 0) * core::at(b, 0)) + (core::at(a, 1) * core::at(b, 1))) + (core::at(a, 2) * core::at(b, 2));
}

// As D3DX's x87 code works them at the FPU's 53 bits: each product exact, each sum rounded to a double, the result
// rounded once to a float when stored.
float dot3_x87(const Vector3& a, const Vector3& b)
{
    const double sum = ((static_cast<double>(core::at(a, 0)) * core::at(b, 0)) +
                        (static_cast<double>(core::at(a, 1)) * core::at(b, 1))) +
                       (static_cast<double>(core::at(a, 2)) * core::at(b, 2));
    return static_cast<float>(sum);
}
Vector3 cross3_x87(const Vector3& a, const Vector3& b)
{
    // (a1 b1 - a2 b2)
    auto term = [](float a1, float b1, float a2, float b2) {
        return static_cast<float>((static_cast<double>(a1) * b1) - (static_cast<double>(a2) * b2));
    };
    return {{term(core::at(a, 1), core::at(b, 2), core::at(a, 2), core::at(b, 1)),
             term(core::at(a, 2), core::at(b, 0), core::at(a, 0), core::at(b, 2)),
             term(core::at(a, 0), core::at(b, 1), core::at(a, 1), core::at(b, 0))}};
}

Vector3 normalized(const Vector3& v)
{
    Vector3 out{};
    normalize3(out, v);
    return out;
}

} // namespace

float estimate_reciprocal_sqrt(float x)
{
#if defined(__SSE__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1) || defined(_M_X64)
    const float estimate = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
    return estimate * (kThreeHalves - (kHalf * x * estimate * estimate));
#else
    return 1.0F / std::sqrt(x);
#endif
}

float estimate_reciprocal(float x)
{
#if defined(__SSE__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1) || defined(_M_X64)
    const float estimate = _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(x)));
    return (estimate + estimate) - (x * estimate * estimate);
#else
    return 1.0F / x;
#endif
}

void transform_point(Floats4 out, ConstFloats3 v, ConstFloats16 matrix)
{
    const std::array<float, 4> point = homogeneous(v);
    std::array<float, 4> result{};
    for (int j = 0; j < kSide; ++j) {
        core::at(result, j) = row_times_column(point, matrix, j);
    }
    std::ranges::copy(result, out.begin());
}

void transform_coord(Floats3 out, ConstFloats3 v, ConstFloats16 matrix)
{
    const std::array<float, 4> point = homogeneous(v);
    std::array<float, 4> result{};
    for (int j = 0; j < kSide; ++j) {
        core::at(result, j) = row_times_column(point, matrix, j);
    }
    const float reciprocal = estimate_reciprocal(core::at(result, 3));
    Vector3 divided{};
    for (int j = 0; j < 3; ++j) {
        core::at(divided, j) = core::at(result, j) * reciprocal;
    }
    std::ranges::copy(divided, out.begin());
}

void transform_normal(Floats3 out, ConstFloats3 v, ConstFloats16 matrix)
{
    Vector3 result{};
    for (int j = 0; j < 3; ++j) {
        const float p0 = core::at(v, 0) * element(matrix, 0, j);
        const float p1 = core::at(v, 1) * element(matrix, 1, j);
        const float p2 = core::at(v, 2) * element(matrix, 2, j);
        core::at(result, j) = (p0 + p1) + p2;
    }
    std::ranges::copy(result, out.begin());
}

void transform_vec4(Floats4 out, ConstFloats4 v, ConstFloats16 matrix)
{
    std::array<float, 4> result{};
    for (int j = 0; j < kSide; ++j) {
        core::at(result, j) = row_times_column(v, matrix, j);
    }
    std::ranges::copy(result, out.begin());
}

void normalize3(Floats3 out, ConstFloats3 v)
{
    const Vector3 in = {{core::at(v, 0), core::at(v, 1), core::at(v, 2)}};
    const float squared = dot3(in, in);
    if (squared < kShortestSquared) {
        std::ranges::fill(out, 0.0F);
        return;
    }
    const float reciprocal = estimate_reciprocal_sqrt(squared);
    for (int j = 0; j < 3; ++j) {
        core::at(out, j) = core::at(in, j) * reciprocal;
    }
}

void matrix_transpose(Floats16 out, ConstFloats16 matrix)
{
    Matrix result{};
    for (int out_row = 0; out_row < kSide; ++out_row) {
        for (int out_column = 0; out_column < kSide; ++out_column) {
            core::at(result, (out_row * kSide) + out_column) = core::at(matrix, (out_column * kSide) + out_row);
        }
    }
    std::ranges::copy(result, out.begin());
}

void matrix_scaling(Floats16 out, float x, float y, float z)
{
    const Matrix result = {{x, 0.0F, 0.0F, 0.0F, 0.0F, y, 0.0F, 0.0F, 0.0F, 0.0F, z, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    std::ranges::copy(result, out.begin());
}

void matrix_rotation_y(Floats16 out, float angle)
{
    const float sine = runtime32::sin(angle);
    const float cosine = runtime32::cos(angle);
    const Matrix result = {
        {cosine, 0.0F, -sine, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, sine, 0.0F, cosine, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    std::ranges::copy(result, out.begin());
}

bool matrix_inverse(Floats16 out, float* determinant, ConstFloats16 matrix)
{
    auto at = [&](int index) { return core::at(matrix, index); };
    // The 2x2 sub-determinants of rows 0-1 (s) and rows 2-3 (c).
    const float s0 = (at(0) * at(5)) - (at(4) * at(1));
    const float s1 = (at(0) * at(6)) - (at(4) * at(2));
    const float s2 = (at(0) * at(7)) - (at(4) * at(3));
    const float s3 = (at(1) * at(6)) - (at(5) * at(2));
    const float s4 = (at(1) * at(7)) - (at(5) * at(3));
    const float s5 = (at(2) * at(7)) - (at(6) * at(3));
    const float c5 = (at(10) * at(15)) - (at(14) * at(11));
    const float c4 = (at(9) * at(15)) - (at(13) * at(11));
    const float c3 = (at(9) * at(14)) - (at(13) * at(10));
    const float c2 = (at(8) * at(15)) - (at(12) * at(11));
    const float c1 = (at(8) * at(14)) - (at(12) * at(10));
    const float c0 = (at(8) * at(13)) - (at(12) * at(9));
    const float det = (s0 * c5) - (s1 * c4) + (s2 * c3) + (s3 * c2) - (s4 * c1) + (s5 * c0);
    if (determinant != nullptr) {
        *determinant = det;
    }
    if (det == 0.0F) {
        return false;
    }
    const float inv = 1.0F / det;
    const Matrix result = {{((at(5) * c5) - (at(6) * c4) + (at(7) * c3)) * inv,
                            ((-at(1) * c5) + (at(2) * c4) - (at(3) * c3)) * inv,
                            ((at(13) * s5) - (at(14) * s4) + (at(15) * s3)) * inv,
                            ((-at(9) * s5) + (at(10) * s4) - (at(11) * s3)) * inv,
                            ((-at(4) * c5) + (at(6) * c2) - (at(7) * c1)) * inv,
                            ((at(0) * c5) - (at(2) * c2) + (at(3) * c1)) * inv,
                            ((-at(12) * s5) + (at(14) * s2) - (at(15) * s1)) * inv,
                            ((at(8) * s5) - (at(10) * s2) + (at(11) * s1)) * inv,
                            ((at(4) * c4) - (at(5) * c2) + (at(7) * c0)) * inv,
                            ((-at(0) * c4) + (at(1) * c2) - (at(3) * c0)) * inv,
                            ((at(12) * s4) - (at(13) * s2) + (at(15) * s0)) * inv,
                            ((-at(8) * s4) + (at(9) * s2) - (at(11) * s0)) * inv,
                            ((-at(4) * c3) + (at(5) * c1) - (at(6) * c0)) * inv,
                            ((at(0) * c3) - (at(1) * c1) + (at(2) * c0)) * inv,
                            ((-at(12) * s3) + (at(13) * s1) - (at(14) * s0)) * inv,
                            ((at(8) * s3) - (at(9) * s1) + (at(10) * s0)) * inv}};
    std::ranges::copy(result, out.begin());
    return true;
}

void matrix_look_at_rh(Floats16 out, ConstFloats3 eye, ConstFloats3 at, ConstFloats3 up)
{
    const Vector3 from = {{core::at(eye, 0), core::at(eye, 1), core::at(eye, 2)}};
    const Vector3 back = normalized({{core::at(eye, 0) - core::at(at, 0), core::at(eye, 1) - core::at(at, 1),
                                      core::at(eye, 2) - core::at(at, 2)}});
    const Vector3 right = normalized(cross3_x87({{core::at(up, 0), core::at(up, 1), core::at(up, 2)}}, back));
    const Vector3 top = cross3_x87(back, right);
    const Matrix result = {{core::at(right, 0), core::at(top, 0), core::at(back, 0), 0.0F,
                            core::at(right, 1), core::at(top, 1), core::at(back, 1), 0.0F,
                            core::at(right, 2), core::at(top, 2), core::at(back, 2), 0.0F,
                            -dot3_x87(right, from), -dot3_x87(top, from), -dot3_x87(back, from), 1.0F}};
    std::ranges::copy(result, out.begin());
}

void matrix_perspective_rh(Floats16 out, float width, float height, float near_z, float far_z)
{
    // (x87 at 53 bits: the difference exact, each quotient rounded once to a float)
    const double twice_near = 2.0 * near_z;
    const auto depth = static_cast<float>(far_z / (static_cast<double>(near_z) - far_z));
    const Matrix result = {{static_cast<float>(twice_near / width), 0.0F, 0.0F, 0.0F,
                            0.0F, static_cast<float>(twice_near / height), 0.0F, 0.0F,
                            0.0F, 0.0F, depth, -1.0F,
                            0.0F, 0.0F, near_z * depth, 0.0F}};
    std::ranges::copy(result, out.begin());
}

void matrix_ortho_rh(Floats16 out, float width, float height, float near_z, float far_z)
{
    // (x87 at 53 bits: the difference exact, each quotient rounded once to a float)
    const auto depth = static_cast<float>(1.0 / (static_cast<double>(near_z) - far_z));
    const Matrix result = {{static_cast<float>(2.0 / width), 0.0F, 0.0F, 0.0F,
                            0.0F, static_cast<float>(2.0 / height), 0.0F, 0.0F,
                            0.0F, 0.0F, depth, 0.0F,
                            0.0F, 0.0F, near_z * depth, 1.0F}};
    std::ranges::copy(result, out.begin());
}

// --- the flat floats ---------------------------------------------------------------------------------------------

namespace {

Floats3 out3(float* values) { return Floats3(values, 3); }
Floats4 out4(float* values) { return Floats4(values, 4); }
Floats16 out16(float* values) { return Floats16(values, kMatrixFloats); }
ConstFloats3 in3(const float* values) { return ConstFloats3(values, 3); }
ConstFloats4 in4(const float* values) { return ConstFloats4(values, 4); }
ConstFloats16 in16(const float* values) { return ConstFloats16(values, kMatrixFloats); }

} // namespace

void transform_point(float* out4_values, const float* v3, const float* matrix)
{
    transform_point(out4(out4_values), in3(v3), in16(matrix));
}
void transform_coord(float* out3_values, const float* v3, const float* matrix)
{
    transform_coord(out3(out3_values), in3(v3), in16(matrix));
}
void transform_normal(float* out3_values, const float* v3, const float* matrix)
{
    transform_normal(out3(out3_values), in3(v3), in16(matrix));
}
void transform_vec4(float* out4_values, const float* v4, const float* matrix)
{
    transform_vec4(out4(out4_values), in4(v4), in16(matrix));
}
void normalize3(float* out3_values, const float* v3) { normalize3(out3(out3_values), in3(v3)); }
void matrix_transpose(float* out, const float* matrix) { matrix_transpose(out16(out), in16(matrix)); }
bool matrix_inverse(float* out, float* determinant, const float* matrix)
{
    return matrix_inverse(out16(out), determinant, in16(matrix));
}
void matrix_look_at_rh(float* out, const float* eye, const float* at, const float* up)
{
    matrix_look_at_rh(out16(out), in3(eye), in3(at), in3(up));
}
void matrix_ortho_rh(float* out, float width, float height, float near_z, float far_z)
{
    matrix_ortho_rh(out16(out), width, height, near_z, far_z);
}

} // namespace maths
