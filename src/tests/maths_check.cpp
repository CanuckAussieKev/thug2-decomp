// NOT ORIGINAL - thug2_maths_check: the plain C++ that replaced the game's x87 inline assembly (style stage 7)
// compared with that assembly over many inputs. The reference half is the removed assembly itself, kept here only
// (MSVC x86: other compilers build the check without it and say so). Run it with no arguments; it prints, per
// function, how many results differ and the largest difference in units in the last place (ulps) of the stored type.
// It runs twice: at the x87's default 53-bit precision and at 24 bits - what Direct3D 9 sets on the thread that
// creates the device (the game creates it without D3DCREATE_FPU_PRESERVE), i.e. the precision the original's x87
// code ran at in the game. Its D3DX half - D3DX9's matrix product, vector transforms and viewing matrices against
// the plain C++ that replaced them - runs on x64 too (once: SSE2 has no precision to set), where there is no x87
// reference. With --write-corpus / --compare-corpus it writes or compares the corpus instead (maths_corpus.h): the
// 32-bit build's inputs and results of the game files' formulas, base/maths and D3DX9, compared on the 64-bit build.

#include <cmath>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <span>
#include <utility>
#include <vector>

#include "tests/no_error_dialogs.h"
#include "base/maths/trig.h"
#include "base/core/parse_number.h"
#include "base/core/checked_index.h"
#include <array>
#include "base/core/format.h"
#include "base/maths/mat4.h"
#include "tests/maths_check_d3dx.h"
#include "base/maths/view_transforms.h"
#include "tests/maths_corpus.h"

namespace {

// The reference constants the checks compare with: the game's own values (a quarter turn, 1 / 1000, the 8 added to a
// texture coordinate and its 16 wrap, 180 / pi, the 60 Hz frame rate, the x87 status-word masks) and the ranges the random
// inputs are drawn from.
constexpr float kQuarterTurnRadians = 1.5707963705062866F;
constexpr int kBitsPerByte = 8;
constexpr float kMillisecondsToSeconds = 0.001F;
constexpr double kWibbleLift = 8.0;
constexpr float kWibbleLiftF = 8.0F;
constexpr float kTwoPow32 = 4294967296.0F;
constexpr int kX87AboveMask = 0x4100;
constexpr int kX87LessThanMask = 0x0500;
constexpr int kX87LessThanBit = 0x0100;
constexpr uint64_t kLowSixtyThreeBits = 0x7FFFFFFFFFFFFFFFULL;
constexpr uint64_t kTopBit = 0x8000000000000000ULL;
constexpr double kAngleSpanFactor = 4.0;
constexpr double kPi15 = 3.14159265358979;
constexpr double kWideRange = 1.0e4;
constexpr double kPositiveMin = 1.0e-6;
constexpr double kPositiveMax = 1.0e6;
constexpr float kCoordinateRange = 5000.0F;
constexpr float kSmallRange = 2.0F;
constexpr float kFovMin = 20.0F;
constexpr float kFovMax = 120.0F;
constexpr int kByteMax = 255;
constexpr int kKeyTimeMax = 100000;
constexpr int kVblankHeadroomBits = 20;
constexpr float kNegAsinCosLimit = 0.005F;
constexpr double kTextureWrap = 16.0;
constexpr int kFrameRate = 60;
constexpr float kRadiansToDegrees = 57.29578F;
constexpr float kWeightBias = 0.5F;
constexpr float kSmallScaleA = 20.0F;
constexpr float kSmallScaleB = 50.0F;
constexpr int kRandomSeed = 20261101;
constexpr float kHalfDegree = 0.008726646F; // radians (the viewport's angle factor)

// --- the new C++ (as in the game's files) ----------------------------------------------------------------------

// The checked functions' arguments and results, as records (their arguments are runs of values of one kind).
struct Yx {
    double y;
    double x;
};
struct YxFloat {
    float y;
    float x;
};
// atan2(left) - atan2(right)
struct AtanPair {
    Yx left;
    Yx right;
};
// An angle and the value its function is compared with or multiplied by.
struct AngleValue {
    double angle;
    float value;
};
struct Fraction {
    int num;
    int den;
};
struct Wibble {
    float t;
    float frequency;
    float phase;
    float amplitude;
    float velocity;
};
struct VBlanks {
    uint64_t count;
    int rate;
};
// The projectile's probe: its index round the circle, the centre (x, z), the step angle and the reach.
struct Probe {
    int index;
    float x;
    float z;
    float step;
    float reach;
};
// Where it ends: the last column's x and the end's z.
struct ProbeTip {
    float column;
    float end;
};
// A turn of (a, b) by an angle.
struct Turn {
    double angle;
    double a;
    double b;
};
struct Xz {
    float x;
    float z;
};
// A turn about y of (dx, dz) by angle * scale.
struct TurnY {
    double dx;
    double dz;
    float angle;
    float scale;
};
struct TurnedY {
    float x;
    double z;
};
// A value and the factor its result is scaled by.
struct Scaled {
    float value;
    float scale;
};
// The view camera's field of view refreshed: the horizontal angle, the aspect factor and divisor, the default angle.
struct FovIn {
    float angle;
    float factor;
    float divisor;
    float base;
};
struct Fov {
    float adjusted;
    float ratio;
};
// The viewport's angle factor: tan(degrees * scale) / tan(base * scale).
struct FactorIn {
    float degrees;
    float base;
    float scale;
};

namespace fresh {

double sin(double a) { return std::sin(a); }
double cos(double a) { return std::cos(a); }
double tan(double a) { return std::tan(a); }
double atan(double a) { return std::atan(a); }
double atan2(Yx in) { return std::atan2(in.y, in.x); }
double sqrt(double a) { return std::sqrt(a); }
double log10(double a) { return std::log10(a); }
double ln(double a) { return std::log(a); }
double asin(double a) { return std::asin(a); }
double acos(double a) { return std::acos(a); }
float sin_f(double a) { return static_cast<float>(std::sin(a)); }
float cos_f(double a) { return static_cast<float>(std::cos(a)); }
float atan2_f(Yx in) { return static_cast<float>(std::atan2(in.y, in.x)); }
double sin_times(double a, double factor) { return std::sin(a) * factor; }
double sin_product(double a, double b) { return std::sin(a) * std::sin(b); }
double cot(double a) { return std::cos(a) / std::sin(a); }
double atan2_difference(const AtanPair& in) { return std::atan2(in.left.y, in.left.x) - std::atan2(in.right.y, in.right.x); }
float acos_f(maths::AngleLimit in, bool* above) { return maths::acos_f(in, above); }
float neg_asin_cos(maths::AngleLimit in, bool* above, maths::Inverses* inverses)
{
    return maths::neg_asin_cos(in, above, inverses);
}
void spin_by(double angle, double a, double b, double* ra, double* rb) { maths::spin_by(angle, a, b, ra, rb); }
bool cos_above(AngleValue in) { return std::cos(in.angle) > in.value; }
bool sin_at_least(AngleValue in) { return !(std::sin(in.angle) < in.value); }

// draw_frame.cpp
float tan_times(AngleValue in) { return static_cast<float>(std::tan(in.angle) * in.value); }
float atan2_less_quarter_turn(YxFloat in)
{
    return static_cast<float>(std::atan2(static_cast<double>(in.y), static_cast<double>(in.x)) - kQuarterTurnRadians);
}
// pc_material.cpp
uint32_t blend_key_colors(Fraction step, std::span<const uint8_t, 4> from, std::span<const uint8_t, 4> to)
{
    static const std::array<int, 4> kOrder = {{3, 0, 1, 2}};
    const double factor = static_cast<double>(step.num) / step.den;
    const double rest = 1.0 - factor;
    uint32_t color = 0;
    for (int k = 0; k < 4; ++k) {
        const auto value = static_cast<uint32_t>(maths::truncate_to_i64((core::at(to, core::at(kOrder, k)) * factor) +
                                                                        (core::at(from, core::at(kOrder, k)) * rest)));
        color = (k != 0) ? (color << kBitsPerByte | value) : value;
    }
    return color;
}
float wibble_time(uint32_t milliseconds) { return static_cast<float>(static_cast<double>(milliseconds) * kMillisecondsToSeconds); }
float wibble_offset(const Wibble& w, bool store_first)
{
    double value = (std::sin((static_cast<double>(w.t) * w.frequency) + w.phase) * w.amplitude) +
                   (static_cast<double>(w.t) * w.velocity) + kWibbleLift;
    const auto whole = static_cast<int32_t>(static_cast<uint32_t>(maths::truncate_to_i64(value)) & 0xFFFFFFF0U);
    value -= whole;
    if (store_first) {
        value = static_cast<float>(value);
    }
    return static_cast<float>(value < 0.0 ? value + kWibbleLift : value - kWibbleLift);
}
// cutscene_play.cpp / cutscene_update.cpp
float vblank_seconds(VBlanks in) { return static_cast<float>(static_cast<double>(in.count) / in.rate); }
// projectile_component.cpp
ProbeTip probe_tip(const Probe& in)
{
    const double angle = static_cast<double>(in.index) * in.step;
    const double cosine = std::cos(angle);
    const double sine = std::sin(static_cast<double>(static_cast<float>(angle)));
    const auto side = static_cast<float>((sine + static_cast<float>(cosine)) * in.reach);
    return {.column = static_cast<float>(((cosine - sine) * in.reach) + in.x),
            .end = static_cast<float>(in.z + static_cast<double>(side))};
}
// park_editor.cpp
Xz turn_move(const Turn& in)
{
    const double sine = static_cast<float>(std::sin(in.angle));
    const double cosine = std::cos(in.angle);
    return {.x = static_cast<float>((cosine * in.b) + (sine * in.a)),
            .z = static_cast<float>((cosine * in.a) - (sine * in.b))};
}
// rail_editor_draw.cpp
TurnedY turn_about_y(const TurnY& in)
{
    const double radians = static_cast<double>(in.angle) * in.scale;
    const double sine = static_cast<float>(std::sin(radians));
    const double cosine = std::cos(radians);
    return {.x = static_cast<float>((in.dx * cosine) + (in.dz * sine)), .z = (in.dz * cosine) - (in.dx * sine)};
}
// walk_camera.cpp
void turn_by_stick(double angle, Xz* xz)
{
    const double sine = static_cast<float>(std::sin(angle));
    const double cosine = std::cos(angle);
    const double old_x = xz->x;
    const double old_z = xz->z;
    xz->x = static_cast<float>((old_z * sine) + (cosine * old_x));
    xz->z = static_cast<float>((old_z * cosine) - (sine * old_x));
}
// walk_physics.cpp / walk_movement.cpp
Xz turn_flat(double angle, Xz at)
{
    const double cosine = static_cast<float>(std::cos(angle));
    const double sine = std::sin(angle);
    return {.x = static_cast<float>((cosine * at.x) - (sine * at.z)),
            .z = static_cast<float>((sine * at.x) + (cosine * at.z))};
}
float asin_degrees(double value, bool negate, float degrees)
{
    const double angle = std::asin(value);
    return static_cast<float>((negate ? -angle : angle) * degrees);
}
float acos_degrees(Scaled in) { return std::fabs(std::acos(in.value) * in.scale); }
// view_cam.cpp
Fov refresh_fov(const FovIn& in)
{
    const float h_fov = in.angle;
    const float factor = in.factor;
    const float divisor = in.divisor;
    const float default_angle = in.base;
    const auto to_half_radians = std::bit_cast<float>(0x3C0EFA35U);
    const auto to_degrees = std::bit_cast<float>(0x42E52EE0U);
    const float half = 0.5F;
    const auto to_radians = std::bit_cast<float>(0x3C8EFA35U);
    const auto half_angle = static_cast<float>(static_cast<double>(h_fov) * to_half_radians);
    const double angle = std::atan(factor * (std::tan(static_cast<double>(half_angle)) / divisor)) * to_degrees;
    const auto tan_adjusted = static_cast<float>(std::tan(angle * to_half_radians));
    return {.adjusted = static_cast<float>(angle),
            .ratio = static_cast<float>(tan_adjusted / std::tan(static_cast<double>(default_angle) * half * to_radians))};
}
// viewport.cpp
float angle_factor(FactorIn in)
{
    return static_cast<float>(std::tan(static_cast<double>(in.degrees) * in.scale) /
                              std::tan(static_cast<double>(in.base) * in.scale));
}
// mat4.cpp
void versine_sine(float angle, double* versine, float* sine)
{
    *versine = 1.0F - std::cos(static_cast<double>(angle));
    *sine = static_cast<float>(std::sin(static_cast<double>(angle)));
}

// pc_mesh_load.cpp: the bone-scaled position, the memory loader's and the file loader's.
struct Skin {
    std::array<float, 4> e0;
    std::array<float, 4> e1;
    std::array<float, 4> e2;
    std::array<float, 4> a;
    std::array<float, 4> b;
    std::array<float, 4> c;
    std::array<float, 4> p;
    std::array<float, 4> q;
    std::array<float, 4> r;
    std::array<float, 3> s;
};
// One formula for both: the original's two loaders differ only in which sums and products their x87 code
// rounded to float.
void skin(const Skin& v, float* out_floats)
{
    const std::span<float, 3> out(out_floats, 3);
    const float ax = core::at(v.p, 0) + (core::at(v.e0, 0) * core::at(v.a, 0));
    const float ay = core::at(v.p, 1) + (core::at(v.e0, 2) * core::at(v.a, 1));
    const float az = core::at(v.p, 2) + (core::at(v.e0, 1) * core::at(v.a, 2));
    const float bx = core::at(v.q, 0) + (core::at(v.e1, 0) * core::at(v.b, 0));
    const float by = core::at(v.q, 1) + (core::at(v.e1, 2) * core::at(v.b, 1));
    const float bz = core::at(v.q, 2) + (core::at(v.e1, 1) * core::at(v.b, 2));
    const float cx = core::at(v.r, 0) + (core::at(v.e2, 0) * core::at(v.c, 0));
    const float cy = core::at(v.r, 1) + (core::at(v.e2, 2) * core::at(v.c, 1));
    const float cz = core::at(v.r, 2) + (core::at(v.e2, 1) * core::at(v.c, 2));
    core::at(out, 0) = (ax * core::at(v.s, 0)) + (bx * core::at(v.s, 1)) + (cx * core::at(v.s, 2));
    core::at(out, 1) = (ay * core::at(v.s, 0)) + (by * core::at(v.s, 1)) + (cy * core::at(v.s, 2));
    core::at(out, 2) = (az * core::at(v.s, 0)) + (bz * core::at(v.s, 1)) + (cz * core::at(v.s, 2));
}

} // namespace fresh

#if defined(_MSC_VER) && defined(_M_IX86) // (the x87 reference: MSVC's inline assembly, 32-bit x86)

// --- the removed assembly ----------------------------------------------------------------------------------------

namespace ref {

// The locals an __asm block writes (fstp r, fistp whole, fnstsw status, through a pointer...): clang-tidy doesn't see
// what inline assembly does with a local, so misc-const-correctness's AllowedTypes names these (.clang-tidy). The
// same types; the locals the assembly only reads are const.
using AsmFloat = float;
using AsmDouble = double;
using AsmInt = int;
using AsmInt64 = int64_t;
using AsmWord16 = uint16_t;

const std::array<unsigned char, 10> kCrtHalfPi = {{0x35, 0xC2, 0x68, 0x21, 0xA2, 0xDA, 0x0F, 0xC9, 0xFF, 0x3F}};
const std::array<unsigned char, 10> kCrtIndefinite = {{0, 0, 0, 0, 0, 0, 0, 0xC0, 0xFF, 0xFF}};

// The game's _CIasin / _CIacos (ECX: 0 asin, 1 acos; the argument and the result in ST0).
__declspec(naked) void arc_sine_cosine()
{
    __asm {
        sub esp, 16
        mov [esp + 12], ecx
        fst qword ptr [esp]
        fstcw word ptr [esp + 8]
        mov eax, [esp + 4]
        and eax, 0x7FF00000
        cmp eax, 0x7FF00000
        je nan_or_infinity
        movzx ecx, word ptr [esp + 8]
        cmp ecx, 0x27F
        je classify
        and ecx, 0x300
        or ecx, 0x7F
        mov [esp + 10], cx
        fldcw word ptr [esp + 10]
    classify:
        cmp eax, 0x3FF00000
        jae one_or_outside
        fld1
        fadd st, st(1)
        fld1
        fsub st, st(2)
        fmulp st(1), st
        fsqrt
        cmp dword ptr [esp + 12], 0
        je arctangent
        fxch st(1)
    arctangent:
        fpatan
        jmp done
    one_or_outside:
        ja outside
        mov eax, [esp + 4]
        and eax, 0x000FFFFF
        or eax, [esp]
        jnz outside
        fstp st(0)
        cmp dword ptr [esp + 12], 0
        jne acos_one
        fld tbyte ptr kCrtHalfPi
        test byte ptr [esp + 7], 0x80
        jz done
        fchs
        jmp done
    acos_one:
        test byte ptr [esp + 7], 0x80
        jz acos_zero
        fldpi
        jmp done
    acos_zero:
        fldz
        jmp done
    done:
        movzx ecx, word ptr [esp + 8]
        cmp ecx, 0x27F
        je finish
        test ecx, 0x20
        jz restore
        fstsw ax
        test eax, 0x20
        jnz to_double
    restore:
        fldcw word ptr [esp + 8]
    finish:
        add esp, 16
        ret
    outside:
        fstp st(0)
        fld tbyte ptr kCrtIndefinite
        jmp to_double
    nan_or_infinity:
        mov eax, [esp + 4]
        and eax, 0x000FFFFF
        or eax, [esp]
        jnz to_double
        fstp st(0)
        fld tbyte ptr kCrtIndefinite
    to_double:
        fstp qword ptr [esp]
        fld qword ptr [esp]
        cmp word ptr [esp + 8], 0x27F
        je finish
        fldcw word ptr [esp + 8]
        jmp finish
    }
}
__declspec(naked) void asin_x87()
{
    __asm {
        xor ecx, ecx
        jmp arc_sine_cosine
    }
}
__declspec(naked) void acos_x87()
{
    __asm {
        mov ecx, 1
        jmp arc_sine_cosine
    }
}

double sin(double a)
{
    AsmDouble result = 0.0;
    __asm { fld a
        fsin
        fstp result }
    return result;
}
double cos(double a)
{
    AsmDouble result = 0.0;
    __asm { fld a
        fcos
        fstp result }
    return result;
}
double tan(double a)
{
    AsmDouble result = 0.0;
    __asm { fld a
        fptan
        fstp st(0)
        fstp result }
    return result;
}
double atan(double a)
{
    AsmDouble result = 0.0;
    __asm { fld a
        fld1
        fpatan
        fstp result }
    return result;
}
double atan2(Yx in)
{
    const double y = in.y;
    const double x = in.x;
    AsmDouble result = 0.0;
    __asm { fld y
        fld x
        fpatan
        fstp result }
    return result;
}
double sqrt(double a)
{
    AsmDouble result = 0.0;
    __asm { fld a
        fsqrt
        fstp result }
    return result;
}
double log10(double x)
{
    AsmDouble result = 0.0;
    __asm { fldlg2
        fld x
        fyl2x
        fstp result }
    return result;
}
double ln(double x)
{
    AsmDouble result = 0.0;
    __asm { fldln2
        fld x
        fyl2x
        fstp result }
    return result;
}
double asin(double v)
{
    AsmDouble result = 0.0;
    const void* function = asin_x87;
    __asm { fld v
        call function
        fstp result }
    return result;
}
double acos(double v)
{
    AsmDouble result = 0.0;
    const void* function = acos_x87;
    __asm { fld v
        call function
        fstp result }
    return result;
}
float sin_f(double a)
{
    AsmFloat result = 0.0F;
    __asm { fld a
        fsin
        fstp result }
    return result;
}
float cos_f(double a)
{
    AsmFloat result = 0.0F;
    __asm { fld a
        fcos
        fstp result }
    return result;
}
float atan2_f(Yx in)
{
    const double y = in.y;
    const double x = in.x;
    AsmFloat result = 0.0F;
    __asm { fld y
        fld x
        fpatan
        fstp result }
    return result;
}
double sin_times(double a, double factor)
{
    AsmDouble result = 0.0;
    __asm { fld a
        fsin
        fmul factor
        fstp result }
    return result;
}
double sin_product(double a, double b)
{
    AsmDouble result = 0.0;
    __asm { fld a
        fsin
        fld b
        fsin
        fmulp st(1), st
        fstp result }
    return result;
}
double cot(double a)
{
    AsmDouble result = 0.0;
    __asm { fld a
        fld st(0)
        fcos
        fxch st(1)
        fsin
        fdivp st(1), st
        fstp result }
    return result;
}
double atan2_difference(const AtanPair& in)
{
    const double y1 = in.left.y;
    const double x1 = in.left.x;
    const double y2 = in.right.y;
    const double x2 = in.right.x;
    AsmDouble result = 0.0;
    __asm { fld y1
        fld x1
        fpatan
        fld y2
        fld x2
        fpatan
        fsubp st(1), st
        fstp result }
    return result;
}
float acos_f(maths::AngleLimit in, bool* above_limit)
{
    const double value = in.value;
    const float limit = in.limit;
    AsmFloat result = 0.0F;
    const void* function = acos_x87;
    AsmWord16 status = 0;
    __asm { fld value
        call function
        fst result
        fcomp limit
        fnstsw status }
    *above_limit = (status & kX87AboveMask) == 0;
    return result;
}
float neg_asin_cos(maths::AngleLimit in, bool* above, maths::Inverses* inverses)
{
    const double value = in.value;
    const float limit = in.limit;
    AsmFloat angle = 0.0F;
    AsmDouble inv = 0.0;
    AsmDouble neg_inv = 0.0;
    AsmWord16 status = 0;
    const void* function = asin_x87;
    const float one = 1.0F;
    const float minus_one = -1.0F;
    __asm {
        fld value
        call function
        fchs
        fst angle
        fcos
        fld st(0)
        fabs
        fcomp limit
        fnstsw status
        fld one
        fdiv st, st(1)
        fstp inv
        fld minus_one
        fdiv st, st(1)
        fstp neg_inv
        fstp st(0)
    }
    *above = (status & kX87AboveMask) == 0;
    inverses->inverse = inv;
    inverses->negative = neg_inv;
    return angle;
}
void spin_by(double angle, double a, double b, double* rotated_a, double* rotated_b)
{
    AsmDouble ra = 0.0;
    AsmDouble rb = 0.0;
    __asm {
        fld angle
        fld st(0)
        fsin
        fxch st(1)
        fcos
        fld a
        fmul st, st(1)
        fld b
        fmul st, st(3)
        faddp st(1), st
        fstp ra
        fmul b
        fxch st(1)
        fmul a
        fsubp st(1), st
        fstp rb
    }
    *rotated_a = ra;
    *rotated_b = rb;
}
bool cos_above(AngleValue in)
{
    const double angle = in.angle;
    const float value = in.value;
    AsmWord16 status = 0;
    __asm { fld angle
        fcos
        fcomp value
        fnstsw status }
    return (status & kX87AboveMask) == 0;
}
bool sin_at_least(AngleValue in)
{
    const double angle = in.angle;
    const float value = in.value;
    AsmWord16 status = 0;
    __asm { fld angle
        fsin
        fcomp value
        fnstsw status }
    return (status & kX87LessThanMask) != kX87LessThanBit;
}
float tan_times(AngleValue in)
{
    const double angle = in.angle;
    const float factor = in.value;
    AsmFloat result = 0.0F;
    __asm { fld angle
        fptan
        fstp st(0)
        fmul factor
        fstp result }
    return result;
}
float atan2_less_quarter_turn(YxFloat in)
{
    const float y = in.y;
    const float x = in.x;
    const float quarter_turn = 1.5707963705062866F;
    AsmFloat result = 0.0F;
    __asm { fld y
        fld x
        fpatan
        fsub quarter_turn
        fstp result }
    return result;
}
uint32_t blend_key_colors(Fraction step, std::span<const uint8_t, 4> from, std::span<const uint8_t, 4> to)
{
    const int num = step.num;
    const int den = step.den;
    static const std::array<int, 4> kOrder = {{3, 0, 1, 2}};
    uint32_t color = 0;
    AsmDouble scratch = 0;
    __asm {
        fild num
        fild den
        fdivp st(1), st(0)
        fld1
        fsub st(0), st(1)
    }
    for (int k = 0; k < 4; ++k)
    {
        const int to_value = core::at(to, core::at(kOrder, k));
        const int from_value = core::at(from, core::at(kOrder, k));
        AsmInt64 value = 0;
        __asm {
            fild to_value
            fmul st(0), st(2)
            fild from_value
            fmul st(0), st(2)
            faddp st(1), st(0)
            fisttp value
        }
        color = (k != 0) ? (color << kBitsPerByte | static_cast<uint32_t>(value)) : static_cast<uint32_t>(value);
    }
    __asm {
        fstp scratch
        fstp scratch
    }
    return color;
}
float wibble_time(uint32_t milliseconds)
{
    AsmFloat result = 0.0F;
    const float scale = kMillisecondsToSeconds;
    const float wrap = kTwoPow32;
    __asm {
        fild milliseconds
        test milliseconds, 0x80000000
        jz positive
        fadd wrap
    positive:
        fmul scale
        fstp result
    }
    return result;
}
float wibble_offset(const Wibble& w, bool store_first)
{
    const float wave_time = w.t;
    const float frequency = w.frequency;
    const float phase = w.phase;
    const float amplitude = w.amplitude;
    const float velocity = w.velocity;
    AsmFloat result = 0.0F;
    const float eight = kWibbleLiftF;
    const float zero = 0.0F;
    AsmInt64 whole = 0;
    AsmInt truncated = 0;
    AsmFloat stored = 0.0F;
    __asm {
        fld wave_time
        fmul frequency
        fadd phase
        fsin
        fmul amplitude
        fld wave_time
        fmul velocity
        faddp st(1), st(0)
        fadd eight
        fld st(0)
        fisttp whole
        mov eax, dword ptr [whole]
        and eax, 0xFFFFFFF0
        mov truncated, eax
        fisub truncated
        cmp store_first, 0
        je compare
        fstp stored
        fld stored
    compare:
        fcom zero
        fnstsw ax
        test ah, 5
        jp notBelow
        fadd eight
        jmp done
    notBelow:
        fsub eight
    done:
        fstp result
    }
    return result;
}
float vblank_seconds(VBlanks in)
{
    const uint64_t vblanks = in.count;
    const int rate = in.rate;
    const std::array<uint64_t, 1> low_part = {{vblanks & kLowSixtyThreeBits}};
    const std::array<uint64_t, 1> high_part = {{vblanks & kTopBit}};
    AsmFloat result = 0.0F;
    __asm {
        fild qword ptr [low_part]
        fild qword ptr [high_part]
        fchs
        faddp st(1), st
        fidiv dword ptr [rate]
        fstp dword ptr [result]
    }
    return result;
}
ProbeTip probe_tip(const Probe& in)
{
    const int index = in.index;
    const float x = in.x;
    const float z = in.z;
    const float step = in.step;
    const float reach = in.reach;
    ProbeTip tip{.column = 0.0F, .end = 0.0F};
    AsmFloat* last_column = &tip.column;
    AsmFloat* end_z = &tip.end;
    AsmFloat angle_f = 0.0F;
    AsmFloat slot = 0.0F;
    __asm {
        fild index
        fmul step
        fst angle_f
        fcos
        fld angle_f
        fsin
        fxch st(1)
        fst slot
        fsub st(0), st(1)
        fxch st(1)
        fadd slot
        fmul reach
        fstp slot
        fmul reach
        fld z
        fadd slot
        fxch st(1)
        fadd x
        mov eax, last_column
        fstp dword ptr [eax]
        mov eax, end_z
        fstp dword ptr [eax]
    }
    return tip;
}
Xz turn_move(const Turn& in)
{
    const double angle = in.angle;
    const double first = in.a;
    const double second = in.b;
    AsmFloat sine = 0.0F;
    AsmFloat result_x = 0.0F;
    AsmFloat result_z = 0.0F;
    __asm {
        fld angle
        fld st(0)
        fsin
        fstp sine
        fcos
        fld st(0)
        fmul second
        fld sine
        fmul first
        faddp st(1), st
        fstp result_x
        fmul first
        fld sine
        fmul second
        fsubp st(1), st
        fstp result_z
    }
    return {.x = result_x, .z = result_z};
}
TurnedY turn_about_y(const TurnY& in)
{
    const double delta_x = in.dx;
    const double delta_z = in.dz;
    const float angle = in.angle;
    const float scale = in.scale;
    AsmFloat sine = 0.0F;
    AsmFloat x = 0.0F;
    AsmDouble z = 0.0;
    const float degree_factor = scale;
    __asm {
        fld angle
        fmul degree_factor
        fld st(0)
        fsin
        fstp sine
        fcos
        fld delta_x
        fmul st(0), st(1)
        fld delta_z
        fmul sine
        faddp st(1), st(0)
        fstp x
        fld delta_z
        fmul st(0), st(1)
        fld delta_x
        fmul sine
        fsubp st(1), st(0)
        fstp z
        fstp st(0)
    }
    return {.x = x, .z = z};
}
void turn_by_stick(double angle, Xz* xz)
{
    AsmFloat* x = &xz->x;
    AsmFloat* z = &xz->z;
    AsmFloat sine = 0.0F;
    __asm {
        mov ecx, x
        mov edx, z
        fld angle
        fld st(0)
        fsin
        fstp sine
        fcos
        fld sine
        fmul dword ptr [ecx]
        fchs
        fld dword ptr [edx]
        fmul st, st(2)
        faddp st(1), st
        fld dword ptr [edx]
        fmul sine
        fxch st(2)
        fmul dword ptr [ecx]
        faddp st(2), st
        fxch st(1)
        fstp dword ptr [ecx]
        fstp dword ptr [edx]
    }
}
Xz turn_flat(double angle, Xz at)
{
    const float x = at.x;
    const float z = at.z;
    AsmFloat cosine = 0.0F;
    AsmFloat rx = 0.0F;
    AsmFloat rz = 0.0F;
    __asm {
        fld angle
        fld st(0)
        fcos
        fstp cosine
        fsin
        fld cosine
        fmul x
        fld st(1)
        fmul z
        fsubp st(1), st
        fstp rx
        fmul x
        fld cosine
        fmul z
        faddp st(1), st
        fstp rz
    }
    return {.x = rx, .z = rz};
}
float asin_degrees(double value, bool negate, float degrees)
{
    AsmFloat result = 0.0F;
    const void* function = asin_x87;
    __asm {
        fld value
        call function
        cmp negate, 0
        je positive
        fchs
    positive:
        fmul degrees
        fstp result
    }
    return result;
}
float acos_degrees(Scaled in)
{
    const float value = in.value;
    const float degrees = in.scale;
    AsmFloat result = 0.0F;
    const void* function = acos_x87;
    __asm {
        fld value
        call function
        fmul degrees
        fabs
        fstp result
    }
    return result;
}
Fov refresh_fov(const FovIn& in)
{
    const float h_fov = in.angle;
    const float factor = in.factor;
    const float divisor = in.divisor;
    const float default_angle = in.base;
    const auto to_half_radians = std::bit_cast<float>(0x3C0EFA35U);
    const auto to_degrees = std::bit_cast<float>(0x42E52EE0U);
    const float half = 0.5F;
    const auto to_radians = std::bit_cast<float>(0x3C8EFA35U);
    AsmFloat half_angle = 0.0F;
    __asm {
        fld h_fov
        fmul to_half_radians
        fstp half_angle
    }
    AsmFloat adjusted = 0.0F;
    AsmFloat tan_adjusted = 0.0F;
    __asm {
        fld factor
        fld half_angle
        fptan
        fstp st(0)
        fdiv divisor
        fmulp st(1), st(0)
        fld1
        fpatan
        fmul to_degrees
        fst adjusted
        fmul to_half_radians
        fptan
        fstp st(0)
        fstp tan_adjusted
    }
    AsmFloat ratio = 0.0F;
    __asm {
        fld default_angle
        fmul half
        fmul to_radians
        fptan
        fstp st(0)
        fdivr tan_adjusted
        fstp ratio
    }
    return {.adjusted = adjusted, .ratio = ratio};
}
float angle_factor(FactorIn in)
{
    const float degrees = in.degrees;
    const float default_angle = in.base;
    const float half_degree_radians = in.scale;
    AsmFloat factor = 0.0F;
    __asm {
        fld degrees
        fmul half_degree_radians
        fptan
        fstp st(0)
        fld default_angle
        fmul half_degree_radians
        fptan
        fstp st(0)
        _emit 0xDE
        _emit 0xF9
        fstp factor
    }
    return factor;
}
void versine_sine(float angle, double* out_t, float* out_s)
{
    AsmDouble versine = 0.0;
    AsmFloat sine = 0.0F;
    const float one = 1.0F;
    __asm {
        fld angle
        fld st(0)
        fcos
        fsubr one
        fstp versine
        fsin
        fstp sine
    }
    *out_t = versine;
    *out_s = sine;
}
void skin_memory(const fresh::Skin& v, float* out_floats)
{
    const std::span<float, 3> out(out_floats, 3);
    AsmFloat e0_0 = core::at(v.e0, 0);
    AsmFloat e0_4 = core::at(v.e0, 1);
    AsmFloat e0_8 = core::at(v.e0, 2);
    AsmFloat e1_0 = core::at(v.e1, 0);
    AsmFloat e1_4 = core::at(v.e1, 1);
    AsmFloat e1_8 = core::at(v.e1, 2);
    AsmFloat e2_0 = core::at(v.e2, 0);
    AsmFloat e2_4 = core::at(v.e2, 1);
    AsmFloat e2_8 = core::at(v.e2, 2);
    AsmFloat a_x = core::at(v.a, 0);
    AsmFloat ay = core::at(v.a, 1);
    AsmFloat az = core::at(v.a, 2);
    AsmFloat b_x = core::at(v.b, 0);
    AsmFloat by = core::at(v.b, 1);
    AsmFloat bz = core::at(v.b, 2);
    AsmFloat c_x = core::at(v.c, 0);
    AsmFloat cy = core::at(v.c, 1);
    AsmFloat cz = core::at(v.c, 2);
    AsmFloat px = core::at(v.p, 0);
    AsmFloat py = core::at(v.p, 1);
    AsmFloat pz = core::at(v.p, 2);
    AsmFloat qx = core::at(v.q, 0);
    AsmFloat qy = core::at(v.q, 1);
    AsmFloat qz = core::at(v.q, 2);
    AsmFloat rx = core::at(v.r, 0);
    AsmFloat ry = core::at(v.r, 1);
    AsmFloat rz = core::at(v.r, 2);
    AsmFloat s0 = core::at(v.s, 0);
    AsmFloat s1 = core::at(v.s, 1);
    AsmFloat s2 = core::at(v.s, 2);
    AsmFloat t0 = 0.0F;
    AsmFloat t1 = 0.0F;
    AsmFloat v2 = 0.0F;
    AsmFloat w1 = 0.0F;
    AsmFloat w2 = 0.0F;
    AsmFloat x1 = 0.0F;
    AsmFloat y1 = 0.0F;
    AsmFloat z1 = 0.0F;
    AsmFloat out_x = 0.0F;
    AsmFloat out_y = 0.0F;
    AsmFloat out_z = 0.0F;
    __asm {
        fld e0_0
        fld e0_8
        fxch st(1)
        fmul a_x
        fstp a_x
        fmul ay
        fld az
        fmul e0_4
        fstp az
        fld e1_0
        fld e1_8
        fxch st(1)
        fmul b_x
        fstp b_x
        fmul by
        fld bz
        fmul e1_4
        fstp bz
        fld e2_0
        fld e2_8
        fxch st(1)
        fmul c_x
        fstp c_x
        fmul cy
        fld cz
        fmul e2_4
        fld px
        fadd a_x
        fstp a_x
        fld py
        fadd st(0), st(4)
        fstp ay
        fld pz
        fadd az
        fstp az
        fld qx
        fadd b_x
        fstp b_x
        fld qy
        fadd st(0), st(3)
        fstp by
        fld qz
        fadd bz
        fstp bz
        fld rx
        fadd c_x
        fstp c_x
        fld ry
        fadd st(0), st(2)
        fstp cy
        fld rz
        fadd st(0), st(1)
        fstp st(4)
        fstp st(0)
        fstp st(0)
        fstp st(0)
        fld c_x
        fmul s2
        fstp t0
        fld cy
        fmul s2
        fstp t1
        fmul s2
        fld b_x
        fmul s1
        fld by
        fmul s1
        fld bz
        fmul s1
        fstp v2
        fld a_x
        fmul s0
        fld ay
        fmul s0
        fstp w1
        fld az
        fmul s0
        fstp w2
        fadd st(0), st(2)
        fstp x1
        fld w1
        fadd st(0), st(1)
        fstp y1
        fstp st(0)
        fstp st(0)
        fld w2
        fadd v2
        fstp z1
        fld x1
        fadd t0
        fld y1
        fadd t1
        fld z1
        fadd st(0), st(3)
        fstp out_z
        fxch st(1)
        fstp out_x
        fstp out_y
        fstp st(0)
    }
    core::at(out, 0) = out_x;
    core::at(out, 1) = out_y;
    core::at(out, 2) = out_z;
}
void skin_file(const fresh::Skin& v, float* out_floats)
{
    const std::span<float, 3> out(out_floats, 3);
    AsmFloat e0_0 = core::at(v.e0, 0);
    AsmFloat e0_4 = core::at(v.e0, 1);
    AsmFloat e0_8 = core::at(v.e0, 2);
    AsmFloat e0_c = core::at(v.e0, 3);
    AsmFloat e1_0 = core::at(v.e1, 0);
    AsmFloat e1_4 = core::at(v.e1, 1);
    AsmFloat e1_8 = core::at(v.e1, 2);
    AsmFloat e1_c = core::at(v.e1, 3);
    AsmFloat e2_0 = core::at(v.e2, 0);
    AsmFloat e2_4 = core::at(v.e2, 1);
    AsmFloat e2_8 = core::at(v.e2, 2);
    AsmFloat e2_c = core::at(v.e2, 3);
    AsmFloat a_x = core::at(v.a, 0);
    AsmFloat ay = core::at(v.a, 1);
    AsmFloat az = core::at(v.a, 2);
    AsmFloat aw = core::at(v.a, 3);
    AsmFloat b_x = core::at(v.b, 0);
    AsmFloat by = core::at(v.b, 1);
    AsmFloat bz = core::at(v.b, 2);
    AsmFloat bw = core::at(v.b, 3);
    AsmFloat c_x = core::at(v.c, 0);
    AsmFloat cy = core::at(v.c, 1);
    AsmFloat cz = core::at(v.c, 2);
    AsmFloat cw = core::at(v.c, 3);
    AsmFloat px = core::at(v.p, 0);
    AsmFloat py = core::at(v.p, 1);
    AsmFloat pz = core::at(v.p, 2);
    AsmFloat pw = core::at(v.p, 3);
    AsmFloat qx = core::at(v.q, 0);
    AsmFloat qy = core::at(v.q, 1);
    AsmFloat qz = core::at(v.q, 2);
    AsmFloat qw = core::at(v.q, 3);
    AsmFloat rx = core::at(v.r, 0);
    AsmFloat ry = core::at(v.r, 1);
    AsmFloat rz = core::at(v.r, 2);
    AsmFloat rw = core::at(v.r, 3);
    AsmFloat s0 = core::at(v.s, 0);
    AsmFloat s1 = core::at(v.s, 1);
    AsmFloat s2 = core::at(v.s, 2);
    AsmFloat cyw = 0.0F;
    AsmFloat czw = 0.0F;
    AsmFloat bzw = 0.0F;
    AsmFloat ayw = 0.0F;
    AsmFloat azw = 0.0F;
    AsmFloat x1 = 0.0F;
    AsmFloat y1 = 0.0F;
    AsmFloat z1 = 0.0F;
    AsmFloat out_x = 0.0F;
    AsmFloat out_y = 0.0F;
    AsmFloat out_z = 0.0F;
    __asm {
        fld e0_0
        fld e0_8
        fxch st(1)
        fmul a_x
        fstp a_x
        fld ay
        fmul st(0), st(1)
        fstp ay
        fstp st(0)
        fld az
        fmul e0_4
        fstp az
        fld aw
        fmul e0_c
        fstp aw
        fld e1_0
        fld e1_8
        fxch st(1)
        fmul b_x
        fstp b_x
        fld by
        fmul st(0), st(1)
        fstp by
        fstp st(0)
        fld bz
        fmul e1_4
        fstp bz
        fld bw
        fmul e1_c
        fstp bw
        fld e2_0
        fld e2_8
        fxch st(1)
        fmul c_x
        fstp c_x
        fmul cy
        fld cz
        fmul e2_4
        fld cw
        fmul e2_c
        fstp cw
        fld px
        fadd a_x
        fstp a_x
        fld py
        fadd ay
        fstp ay
        fld pz
        fadd az
        fstp az
        fld pw
        fadd aw
        fstp aw
        fld qx
        fadd b_x
        fstp b_x
        fld qy
        fadd by
        fstp by
        fld qz
        fadd bz
        fstp bz
        fld qw
        fadd bw
        fstp bw
        fld rx
        fadd c_x
        fstp c_x
        fld ry
        fadd st(0), st(2)
        fstp cy
        fld rz
        fadd st(0), st(1)
        fstp cz
        fstp st(0)
        fstp st(0)
        fld rw
        fadd cw
        fstp cw
        fld c_x
        fmul s2
        fld cy
        fmul s2
        fstp cyw
        fld cz
        fmul s2
        fstp czw
        fld b_x
        fmul s1
        fld by
        fmul s1
        fld bz
        fmul s1
        fstp bzw
        fld a_x
        fmul s0
        fld ay
        fmul s0
        fstp ayw
        fld az
        fmul s0
        fstp azw
        fadd st(0), st(2)
        fstp x1
        fld ayw
        fadd st(0), st(1)
        fstp y1
        fstp st(0)
        fstp st(0)
        fld azw
        fadd bzw
        fstp z1
        fadd x1
        fld y1
        fadd cyw
        fld z1
        fadd czw
        fstp out_z
        fxch st(1)
        fstp out_x
        fstp out_y
    }
    core::at(out, 0) = out_x;
    core::at(out, 1) = out_y;
    core::at(out, 2) = out_z;
}

} // namespace ref

#endif // the x87 reference

// --- the comparison ----------------------------------------------------------------------------------------------

using tests::ulps;

// One function's comparison so far: the runs, how many differed, the largest difference and where.
class Tally {
public:
    Tally(const char* name, char unit) : name_(name), unit_(unit) {}
    // One comparison: the distance (ulps, or 1 for a disagreement), the absolute difference, and the input.
    struct Sample {
        uint64_t distance;
        double difference;
        double input;
    };
    void add(Sample sample)
    {
        const uint64_t distance = sample.distance;
        const double abs_difference = sample.difference;
        const double input = sample.input;
        ++runs_;
        if (distance != 0U) {
            ++differing_;
            if (distance > max_ulps_) {
                max_ulps_ = distance;
                worst_input_ = input;
            }
            max_abs_ = std::max(max_abs_, abs_difference);
        }
    }
    void print() const
    {
        text::print("  %-26s %9llu runs  %9llu differ (%6.3f%%)  max %c%llu  |diff| %.3g  (at %.9g)\n", name_,
                    static_cast<unsigned long long>(runs_), static_cast<unsigned long long>(differing_),
                    (runs_ != 0U) ? 100.0 * static_cast<double>(differing_) / static_cast<double>(runs_) : 0.0, unit_,
                    static_cast<unsigned long long>(max_ulps_), max_abs_, worst_input_);
    }

private:
    const char* name_;
    char unit_; // 'd' double ulps, 'f' float ulps, 'b' disagreeing booleans, 'u' differing integers
    uint64_t runs_ = 0, differing_ = 0, max_ulps_ = 0;
    double max_abs_ = 0, worst_input_ = 0;
};

template <typename T>
void compare(Tally& tally, T fresh_value, T ref_value, double input)
{
    const uint64_t ulp_distance = ulps(fresh_value, ref_value);
    tally.add({.distance = ulp_distance,
               .difference = ulp_distance == UINT64_MAX
                                 ? INFINITY
                                 : std::fabs(static_cast<double>(fresh_value) - static_cast<double>(ref_value)),
               .input = input});
}
void compare_bool(Tally& tally, bool a, bool b, double input)
{
    tally.add({.distance = static_cast<uint64_t>(a != b), .difference = static_cast<double>(a != b), .input = input});
}

#if defined(_MSC_VER) && defined(_M_IX86) // (the x87 reference: MSVC's inline assembly, 32-bit x86)

void run(int count, std::mt19937_64& random)
{
    std::uniform_real_distribution<double> angle(-kAngleSpanFactor * kPi15, kAngleSpanFactor * kPi15);
    std::uniform_real_distribution<double> wide(-kWideRange, kWideRange);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    std::uniform_real_distribution<double> positive(kPositiveMin, kPositiveMax);
    std::uniform_real_distribution<float> coordinate(-kCoordinateRange, kCoordinateRange);
    std::uniform_real_distribution<float> small(-kSmallRange, kSmallRange);
    std::uniform_real_distribution<float> weight(0.0F, 1.0F);
    std::uniform_real_distribution<float> fov(kFovMin, kFovMax);
    std::uniform_int_distribution<int> byte(0, kByteMax);
    std::uniform_int_distribution<int> key_time(0, kKeyTimeMax);
    std::uniform_int_distribution<uint32_t> milliseconds(0, UINT32_MAX);
    std::uniform_int_distribution<uint64_t> vblanks(0, UINT64_MAX >> kVblankHeadroomBits);

    Tally t_sin("sin", 'd');
    Tally t_cos("cos", 'd');
    Tally t_tan("tan", 'd');
    Tally t_atan("atan", 'd');
    Tally t_atan2("atan2", 'd');
    Tally t_sqrt("sqrt", 'd');
    Tally t_log10("log10", 'd');
    Tally t_ln("ln", 'd');
    Tally t_asin("asin (C runtime)", 'd');
    Tally t_acos("acos (C runtime)", 'd');
    Tally t_sin_f("sin -> float", 'f');
    Tally t_cos_f("cos -> float", 'f');
    Tally t_atan2_f("atan2 -> float", 'f');
    Tally t_sin_wide("sin, |angle| < 1e4", 'd');
    Tally t_sin_times("sin * factor", 'd');
    Tally t_sin_product("sin * sin", 'd');
    Tally t_cot("cot", 'd');
    Tally t_atan2_diff("atan2 - atan2", 'd');
    Tally t_acos_f("acos_f", 'f');
    Tally t_acos_f_above("acos_f's comparison", 'b');
    Tally t_nac("neg_asin_cos", 'f');
    Tally t_nac_above("neg_asin_cos's comparison", 'b');
    Tally t_nac_inv("neg_asin_cos 1 / cos", 'd');
    Tally t_spin("spin_by", 'd');
    Tally t_cos_above("cos_above", 'b');
    Tally t_sin_at_least("sin_at_least", 'b');
    Tally t_tan_times("tan_times", 'f');
    Tally t_atan2_qt("atan2_less_quarter_turn", 'f');
    Tally t_blend("blend_key_colors (steps)", 'u');
    Tally t_wibble_time("wibble_time", 'f');
    Tally t_wibble("wibble_offset", 'f');
    Tally t_wibble_wrap("wibble_offset modulo 16", 'b');
    Tally t_vblank("vblank_seconds", 'f');
    Tally t_probe("probe_tip", 'f');
    Tally t_turn_move("turn_move", 'f');
    Tally t_turn_y("turn_about_y", 'f');
    Tally t_stick("turn_by_stick", 'f');
    Tally t_flat("turn_flat", 'f');
    Tally t_asin_deg("asin_degrees", 'f');
    Tally t_acos_deg("acos_degrees", 'f');
    Tally t_fov("view camera fov", 'f');
    Tally t_factor("viewport angle factor", 'f');
    Tally t_versine("1 - cos, sin (mat4)", 'f');
    Tally t_skin_m("skinned position (memory)", 'f');
    Tally t_skin_f("skinned position (file)", 'f');

    for (int i = 0; i < count; ++i) {
        const double first_angle = angle(random);
        const double second_angle = angle(random);
        const double u = unit(random);
        const double w = wide(random);
        compare(t_sin, fresh::sin(first_angle), ref::sin(first_angle), first_angle);
        compare(t_cos, fresh::cos(first_angle), ref::cos(first_angle), first_angle);
        compare(t_tan, fresh::tan(first_angle), ref::tan(first_angle), first_angle);
        compare(t_atan, fresh::atan(w), ref::atan(w), w);
        compare(t_atan2, fresh::atan2({.y = w, .x = first_angle}), ref::atan2({.y = w, .x = first_angle}), w);
        const double positive_value = positive(random);
        compare(t_sqrt, fresh::sqrt(positive_value), ref::sqrt(positive_value), positive_value);
        compare(t_log10, fresh::log10(positive_value), ref::log10(positive_value), positive_value);
        compare(t_ln, fresh::ln(positive_value), ref::ln(positive_value), positive_value);
        compare(t_asin, fresh::asin(u), ref::asin(u), u);
        compare(t_acos, fresh::acos(u), ref::acos(u), u);
        compare(t_sin_f, fresh::sin_f(first_angle), ref::sin_f(first_angle), first_angle);
        compare(t_cos_f, fresh::cos_f(first_angle), ref::cos_f(first_angle), first_angle);
        compare(t_atan2_f, fresh::atan2_f({.y = w, .x = first_angle}), ref::atan2_f({.y = w, .x = first_angle}), w);
        compare(t_sin_wide, fresh::sin(w), ref::sin(w), w);
        compare(t_sin_times, fresh::sin_times(first_angle, w), ref::sin_times(first_angle, w), first_angle);
        compare(t_sin_product, fresh::sin_product(first_angle, second_angle),
                ref::sin_product(first_angle, second_angle), first_angle);
        compare(t_cot, fresh::cot(first_angle), ref::cot(first_angle), first_angle);
        const AtanPair pair{.left = {.y = first_angle, .x = w}, .right = {.y = second_angle, .x = u}};
        compare(t_atan2_diff, fresh::atan2_difference(pair), ref::atan2_difference(pair), first_angle);
        bool above_fresh = false;
        bool above_ref = false;
        const auto limit = static_cast<float>(weight(random) * 3.0F);
        compare(t_acos_f, fresh::acos_f({.value = u, .limit = limit}, &above_fresh), ref::acos_f({.value = u, .limit = limit}, &above_ref), u);
        compare_bool(t_acos_f_above, above_fresh, above_ref, u);
        maths::Inverses inv_fresh{.inverse = 0.0, .negative = 0.0};
        maths::Inverses inv_ref{.inverse = 0.0, .negative = 0.0};
        const double sample = static_cast<float>(u);
        compare(t_nac, fresh::neg_asin_cos({.value = sample, .limit = kNegAsinCosLimit}, &above_fresh, &inv_fresh),
                ref::neg_asin_cos({.value = sample, .limit = kNegAsinCosLimit}, &above_ref, &inv_ref), sample);
        compare_bool(t_nac_above, above_fresh, above_ref, sample);
        compare(t_nac_inv, inv_fresh.inverse, inv_ref.inverse, sample);
        compare(t_nac_inv, inv_fresh.negative, inv_ref.negative, sample);
        double ra_fresh = 0.0;
        double rb_fresh = 0.0;
        double ra_ref = 0.0;
        double rb_ref = 0.0;
        const double ca = coordinate(random);
        const double cb = coordinate(random);
        fresh::spin_by(first_angle, ca, cb, &ra_fresh, &rb_fresh);
        ref::spin_by(first_angle, ca, cb, &ra_ref, &rb_ref);
        compare(t_spin, ra_fresh, ra_ref, first_angle);
        compare(t_spin, rb_fresh, rb_ref, first_angle);
        const auto cosine_limit = static_cast<float>(u);
        compare_bool(t_cos_above, fresh::cos_above({.angle = first_angle, .value = cosine_limit}),
                     ref::cos_above({.angle = first_angle, .value = cosine_limit}), first_angle);
        compare_bool(t_sin_at_least, fresh::sin_at_least({.angle = first_angle, .value = cosine_limit}),
                     ref::sin_at_least({.angle = first_angle, .value = cosine_limit}), first_angle);
        const auto fa = static_cast<float>(first_angle * 0.2);
        const float fb = small(random);
        compare(t_tan_times, fresh::tan_times({.angle = fa, .value = fb}), ref::tan_times({.angle = fa, .value = fb}), fa);
        const float y = coordinate(random);
        const float x = coordinate(random);
        compare(t_atan2_qt, fresh::atan2_less_quarter_turn({.y = y, .x = x}), ref::atan2_less_quarter_turn({.y = y, .x = x}), y);
        std::array<uint8_t, 4> from{};
        std::array<uint8_t, 4> to{};
        for (int k = 0; k < 4; ++k) {
            core::at(from, k) = static_cast<uint8_t>(byte(random));
            core::at(to, k) = static_cast<uint8_t>(byte(random));
        }
        const int den = key_time(random) + 1;
        const int num = key_time(random) % den;
        {
            // (the largest step any of the four channels differs by)
            const uint32_t c_fresh = fresh::blend_key_colors({.num = num, .den = den}, from, to);
            const uint32_t c_ref = ref::blend_key_colors({.num = num, .den = den}, from, to);
            uint64_t steps = 0;
            for (int k = 0; k < 4; ++k) {
                const int difference =
                    static_cast<int>((c_fresh >> (8 * k)) & 0xFF) - static_cast<int>((c_ref >> (8 * k)) & 0xFF);
                steps = std::max<uint64_t>(steps, static_cast<uint64_t>(difference < 0 ? -difference : difference));
            }
            t_blend.add({.distance = steps, .difference = static_cast<double>(steps), .input = static_cast<double>(num)});
        }
        const uint32_t ms = milliseconds(random);
        compare(t_wibble_time, fresh::wibble_time(ms), ref::wibble_time(ms), ms);
        const float wave_time = fresh::wibble_time(ms % 3600000U);
        const float frequency = small(random);
        const float phase = small(random);
        const float amplitude = small(random);
        const float velocity = small(random);
        const bool store_first = (i & 1) != 0;
        const Wibble wibble{
            .t = wave_time, .frequency = frequency, .phase = phase, .amplitude = amplitude, .velocity = velocity};
        compare(t_wibble, fresh::wibble_offset(wibble, store_first), ref::wibble_offset(wibble, store_first),
                wave_time);
        {
            // (the offset is a texture coordinate that wraps every 16: the difference modulo 16)
            const float w_fresh = fresh::wibble_offset(wibble, store_first);
            const float w_ref = ref::wibble_offset(wibble, store_first);
            double wrap_difference = std::fmod(std::fabs(static_cast<double>(w_fresh) - w_ref), kTextureWrap);
            wrap_difference = std::min(wrap_difference, kTextureWrap - wrap_difference);
            t_wibble_wrap.add({.distance = static_cast<uint64_t>(wrap_difference != 0.0),
                               .difference = wrap_difference,
                               .input = wave_time});
        }
        const uint64_t vb = vblanks(random);
        compare(t_vblank, fresh::vblank_seconds({.count = vb, .rate = kFrameRate}), ref::vblank_seconds({.count = vb, .rate = kFrameRate}), static_cast<double>(vb));
        const int index = i % 64;
        const Probe probe{.index = index, .x = x, .z = y, .step = 0.09817477F, .reach = 12.0F};
        const ProbeTip tip_fresh = fresh::probe_tip(probe);
        const ProbeTip tip_ref = ref::probe_tip(probe);
        compare(t_probe, tip_fresh.column, tip_ref.column, index);
        compare(t_probe, tip_fresh.end, tip_ref.end, index);
        const Xz move_fresh = fresh::turn_move({.angle = first_angle, .a = u, .b = fb});
        const Xz move_ref = ref::turn_move({.angle = first_angle, .a = u, .b = fb});
        compare(t_turn_move, move_fresh.x, move_ref.x, first_angle);
        compare(t_turn_move, move_fresh.z, move_ref.z, first_angle);
        const auto degrees = static_cast<float>(first_angle * 30.0);
        const TurnY turn_y{.dx = ca, .dz = cb, .angle = degrees, .scale = 0.017453292F};
        const TurnedY y_fresh = fresh::turn_about_y(turn_y);
        const TurnedY y_ref = ref::turn_about_y(turn_y);
        compare(t_turn_y, y_fresh.x, y_ref.x, degrees);
        compare(t_turn_y, static_cast<float>(y_fresh.z), static_cast<float>(y_ref.z), degrees);
        Xz stick_fresh{.x = x, .z = y};
        Xz stick_ref{.x = x, .z = y};
        fresh::turn_by_stick(first_angle, &stick_fresh);
        ref::turn_by_stick(first_angle, &stick_ref);
        compare(t_stick, stick_fresh.x, stick_ref.x, first_angle);
        compare(t_stick, stick_fresh.z, stick_ref.z, first_angle);
        const Xz flat_fresh = fresh::turn_flat(first_angle, {.x = x, .z = y});
        const Xz flat_ref = ref::turn_flat(first_angle, {.x = x, .z = y});
        compare(t_flat, flat_fresh.x, flat_ref.x, first_angle);
        compare(t_flat, flat_fresh.z, flat_ref.z, first_angle);
        compare(t_asin_deg, fresh::asin_degrees(u, store_first, kRadiansToDegrees),
                ref::asin_degrees(u, store_first, kRadiansToDegrees), u);
        const auto fu = static_cast<float>(u);
        compare(t_acos_deg, fresh::acos_degrees({.value = fu, .scale = kRadiansToDegrees}), ref::acos_degrees({.value = fu, .scale = kRadiansToDegrees}), fu);
        const float h_fov = fov(random);
        const float default_angle = fov(random);
        const float factor = weight(random) + 0.5F;
        const float divisor = weight(random) + 0.5F;
        const FovIn fov_in{.angle = h_fov, .factor = factor, .divisor = divisor, .base = default_angle};
        const Fov fov_fresh = fresh::refresh_fov(fov_in);
        const Fov fov_ref = ref::refresh_fov(fov_in);
        compare(t_fov, fov_fresh.adjusted, fov_ref.adjusted, h_fov);
        compare(t_fov, fov_fresh.ratio, fov_ref.ratio, h_fov);
        const FactorIn factor_in{.degrees = h_fov, .base = default_angle, .scale = 0.008726646F};
        compare(t_factor, fresh::angle_factor(factor_in), ref::angle_factor(factor_in), h_fov);
        double vt_fresh = 0.0;
        double vt_ref = 0.0;
        float vs_fresh = 0.0F;
        float vs_ref = 0.0F;
        fresh::versine_sine(fa, &vt_fresh, &vs_fresh);
        ref::versine_sine(fa, &vt_ref, &vs_ref);
        compare(t_versine, static_cast<float>(vt_fresh), static_cast<float>(vt_ref), fa);
        compare(t_versine, vs_fresh, vs_ref, fa);
        fresh::Skin skin{};
        for (int k = 0; k < 4; ++k) {
            core::at(skin.e0, k) = weight(random) + kWeightBias;
            core::at(skin.e1, k) = weight(random) + kWeightBias;
            core::at(skin.e2, k) = weight(random) + kWeightBias;
            core::at(skin.a, k) = small(random) * kSmallScaleA;
            core::at(skin.b, k) = small(random) * kSmallScaleA;
            core::at(skin.c, k) = small(random) * kSmallScaleA;
            core::at(skin.p, k) = small(random) * kSmallScaleB;
            core::at(skin.q, k) = small(random) * kSmallScaleB;
            core::at(skin.r, k) = small(random) * kSmallScaleB;
        }
        core::at(skin.s, 0) = weight(random);
        core::at(skin.s, 1) = weight(random) * (1.0F - core::at(skin.s, 0));
        core::at(skin.s, 2) = 1.0F - core::at(skin.s, 0) - core::at(skin.s, 1);
        std::array<float, 3> out_fresh{};
        std::array<float, 3> out_ref{};
        fresh::skin(skin, out_fresh.data());
        ref::skin_memory(skin, out_ref.data());
        for (int k = 0; k < 3; ++k) {
            compare(t_skin_m, core::at(out_fresh, k), core::at(out_ref, k), k);
        }
        fresh::skin(skin, out_fresh.data());
        ref::skin_file(skin, out_ref.data());
        for (int k = 0; k < 3; ++k) {
            compare(t_skin_f, core::at(out_fresh, k), core::at(out_ref, k), k);
        }
    }
    // The edges: +-1, 0, NaN, outside [-1, 1] for asin / acos.
    const auto edges = std::to_array<double>({1.0, -1.0, 0.0, -0.0, 1.0000001, -1.5, 2.0, NAN, INFINITY, -static_cast<double>(INFINITY)});
    for (const double edge : edges) {
        compare(t_asin, fresh::asin(edge), ref::asin(edge), edge);
        compare(t_acos, fresh::acos(edge), ref::acos(edge), edge);
    }

    auto all = std::to_array<const Tally*>({&t_sin,       &t_cos,          &t_tan,       &t_atan,        &t_atan2,     &t_sqrt,
                          &t_log10,     &t_ln,           &t_asin,      &t_acos,        &t_sin_f,     &t_cos_f,
                          &t_atan2_f,   &t_sin_wide,     &t_sin_times, &t_sin_product, &t_cot,       &t_atan2_diff,
                          &t_acos_f,    &t_acos_f_above, &t_nac,       &t_nac_above,   &t_nac_inv,   &t_spin,
                          &t_cos_above, &t_sin_at_least, &t_tan_times, &t_atan2_qt,    &t_blend,     &t_wibble_time,
                          &t_wibble,    &t_wibble_wrap,  &t_vblank,    &t_probe,       &t_turn_move, &t_turn_y,
                          &t_stick,     &t_flat,         &t_asin_deg,  &t_acos_deg,    &t_fov,       &t_factor,
                          &t_versine,   &t_skin_m,       &t_skin_f});
    for (const Tally* tally : all) {
        tally->print();
    }
}

#endif // the x87 reference

// NOT ORIGINAL - D3DX9's matrix product (d3dx9_43.dll, which the game called until stage 11) against the plain C++
// that replaced it, maths::matrix_multiply: random matrices of the game's coordinate, unit and tiny (denormal
// product) scales, each also multiplied in place (the result written over a, and over b).
void matrix_product(int count, std::mt19937_64& random)
{
    constexpr float kTinyScale = 1.0e-20F;
    using Matrix = std::array<float, maths::kMatrixFloats>;
    std::uniform_real_distribution<float> coordinate(-kCoordinateRange, kCoordinateRange);
    std::uniform_real_distribution<float> unit(-1.0F, 1.0F);
    Tally t_product("matrix_multiply (D3DX)", 'f');
    Tally t_over_a("matrix_multiply, out = a", 'f');
    Tally t_over_b("matrix_multiply, out = b", 'f');
    for (int index = 0; index < count; ++index) {
        Matrix left{};
        Matrix right{};
        const int scale = index % 3; // coordinates times unit, unit times unit, tiny times unit
        for (int k = 0; k < maths::kMatrixFloats; ++k) {
            const float drawn = scale == 0 ? coordinate(random) : unit(random);
            core::at(left, k) = scale == 2 ? drawn * kTinyScale : drawn;
            core::at(right, k) = unit(random);
        }
        Matrix want{};
        tests::d3dx_matrix_multiply(want.data(), left.data(), right.data());
        Matrix want_a = left;
        tests::d3dx_matrix_multiply(want_a.data(), want_a.data(), right.data());
        Matrix want_b = right;
        tests::d3dx_matrix_multiply(want_b.data(), left.data(), want_b.data());
        Matrix fresh{};
        maths::matrix_multiply(fresh.data(), left.data(), right.data());
        Matrix over_a = left;
        maths::matrix_multiply(over_a.data(), over_a.data(), right.data());
        Matrix over_b = right;
        maths::matrix_multiply(over_b.data(), left.data(), over_b.data());
        for (int k = 0; k < maths::kMatrixFloats; ++k) {
            compare(t_product, core::at(fresh, k), core::at(want, k), k);
            compare(t_over_a, core::at(over_a, k), core::at(want_a, k), k);
            compare(t_over_b, core::at(over_b, k), core::at(want_b, k), k);
        }
    }
    t_product.print();
    t_over_a.print();
    t_over_b.print();
}

// NOT ORIGINAL - D3DX9's vector transforms and viewing matrices (d3dx9_43.dll, which the renderer once called for
// them) against the plain C++ that replaced them (base/maths/view_transforms.h): coordinates, unit vectors and
// matrices of the game's scales, viewing parameters of its ranges. Run with the x87 at 53 bits (the game's thread) and at
// 24: D3DX's x87 code (the matrix builders) rounds as the control word says; its SSE code doesn't care.
void view_maths(int count, std::mt19937_64& random)
{
    using Matrix = std::array<float, maths::kMatrixFloats>;
    using Vector3 = std::array<float, 3>;
    using Vector4 = std::array<float, 4>;
    constexpr float kAngleRange = 7.0F;
    constexpr float kUpTilt = 0.2F;
    constexpr float kSizeMin = 0.01F;
    constexpr float kSizeMax = 2000.0F;
    constexpr float kNearMin = 0.1F;
    constexpr float kNearMax = 20.0F;
    constexpr float kOrthoNearMin = -100.0F;
    constexpr float kDepthMin = 1.0F;
    constexpr float kDepthMax = 100000.0F;
    constexpr float kTinyScale = 1.0e-20F;
    constexpr int kTranslationFirst = 12; // a matrix's translation row: floats 12-14
    constexpr int kTranslationEnd = 15;
    constexpr std::array<int, 3> kLastColumn = {{3, 7, 11}}; // a matrix's last column but its w
    std::uniform_real_distribution<float> coordinate(-kCoordinateRange, kCoordinateRange);
    std::uniform_real_distribution<float> unit(-1.0F, 1.0F);
    std::uniform_real_distribution<float> angle(-kAngleRange, kAngleRange);
    std::uniform_real_distribution<float> tilt(-kUpTilt, kUpTilt);
    std::uniform_real_distribution<float> size(kSizeMin, kSizeMax);
    std::uniform_real_distribution<float> near_z(kNearMin, kNearMax);
    std::uniform_real_distribution<float> ortho_near(kOrthoNearMin, kNearMax);
    std::uniform_real_distribution<float> depth(kDepthMin, kDepthMax);
    // A matrix: a unit-scale 3x3, a translation of coordinates, and (half of them) the affine last column 0, 0, 0, 1 -
    // else a projective one with a positive w.
    auto draw_matrix = [&](int index) {
        Matrix drawn{};
        for (int k = 0; k < maths::kMatrixFloats; ++k) {
            core::at(drawn, k) = (k >= kTranslationFirst && k < kTranslationEnd) ? coordinate(random) : unit(random);
        }
        if (index % 2 == 0) {
            for (const int slot : kLastColumn) {
                core::at(drawn, slot) = 0.0F;
            }
            core::at(drawn, kTranslationEnd) = 1.0F;
        } else {
            core::at(drawn, kTranslationEnd) = size(random);
        }
        return drawn;
    };
    auto compare_all = [](Tally& tally, std::span<const float> fresh, std::span<const float> want, double input) {
        for (size_t k = 0; k < fresh.size(); ++k) {
            compare(tally, core::at(fresh, k), core::at(want, k), input);
        }
    };
    Tally t_point("vec3_transform", 'f');
    Tally t_coord("vec3_transform_coord", 'f');
    Tally t_normal("vec3_transform_normal", 'f');
    Tally t_vec4("vec4_transform", 'f');
    Tally t_normalize("vec3_normalize", 'f');
    Tally t_transpose("matrix_transpose", 'f');
    Tally t_scaling("matrix_scaling", 'f');
    Tally t_rotation("matrix_rotation_y", 'f');
    Tally t_inverse("matrix_inverse", 'f');
    Tally t_determinant("matrix_inverse determinant", 'f');
    Tally t_invertible("matrix_inverse invertible", 'b');
    Tally t_look("matrix_look_at_rh", 'f');
    Tally t_perspective("matrix_perspective_rh", 'f');
    Tally t_ortho("matrix_ortho_rh", 'f');
    for (int index = 0; index < count; ++index) {
        const Matrix matrix = draw_matrix(index);
        const int scale = index % 3; // coordinates, unit vectors, tiny ones
        auto draw = [&]() {
            const float drawn = scale == 0 ? coordinate(random) : unit(random);
            return scale == 2 ? drawn * kTinyScale : drawn;
        };
        const Vector3 v = {{draw(), draw(), draw()}};
        const Vector4 v4 = {{draw(), draw(), draw(), unit(random)}};
        Vector4 fresh4{};
        Vector4 want4{};
        Vector3 fresh3{};
        Vector3 want3{};
        maths::transform_point(fresh4, v, matrix);
        tests::d3dx_vec3_transform(want4, v, matrix);
        compare_all(t_point, fresh4, want4, core::at(v, 0));
        maths::transform_coord(fresh3, v, matrix);
        tests::d3dx_vec3_transform_coord(want3, v, matrix);
        compare_all(t_coord, fresh3, want3, core::at(v, 0));
        maths::transform_normal(fresh3, v, matrix);
        tests::d3dx_vec3_transform_normal(want3, v, matrix);
        compare_all(t_normal, fresh3, want3, core::at(v, 0));
        maths::transform_vec4(fresh4, v4, matrix);
        tests::d3dx_vec4_transform(want4, v4, matrix);
        compare_all(t_vec4, fresh4, want4, core::at(v4, 0));
        maths::normalize3(fresh3, v);
        tests::d3dx_vec3_normalize(want3, v);
        compare_all(t_normalize, fresh3, want3, core::at(v, 0));

        Matrix fresh{};
        Matrix want{};
        maths::matrix_transpose(fresh, matrix);
        tests::d3dx_matrix_transpose(want, matrix);
        compare_all(t_transpose, fresh, want, core::at(matrix, 0));
        maths::matrix_scaling(fresh, core::at(v, 0), core::at(v, 1), core::at(v, 2));
        tests::d3dx_matrix_scaling(want, core::at(v, 0), core::at(v, 1), core::at(v, 2));
        compare_all(t_scaling, fresh, want, core::at(v, 0));
        const float turn = angle(random);
        maths::matrix_rotation_y(fresh, turn);
        tests::d3dx_matrix_rotation_y(want, turn);
        compare_all(t_rotation, fresh, want, turn);
        float fresh_determinant = 0.0F;
        float want_determinant = 0.0F;
        fresh = {};
        want = {};
        const bool fresh_made = maths::matrix_inverse(fresh, &fresh_determinant, matrix);
        const bool want_made = tests::d3dx_matrix_inverse(want, &want_determinant, matrix);
        compare_bool(t_invertible, fresh_made, want_made, core::at(matrix, 0));
        compare(t_determinant, fresh_determinant, want_determinant, core::at(matrix, 0));
        compare_all(t_inverse, fresh, want, core::at(matrix, 0));

        const Vector3 eye = {{coordinate(random), coordinate(random), coordinate(random)}};
        const Vector3 at = {{coordinate(random), coordinate(random), coordinate(random)}};
        const Vector3 up = index % 2 == 0 ? Vector3{{0.0F, 1.0F, 0.0F}} : Vector3{{tilt(random), 1.0F, tilt(random)}};
        maths::matrix_look_at_rh(fresh, eye, at, up);
        tests::d3dx_matrix_look_at_rh(want, {.eye = eye, .at = at, .up = up});
        compare_all(t_look, fresh, want, core::at(eye, 0));
        const float width = size(random);
        const float height = size(random);
        const float zn = near_z(random);
        const float zf = zn + depth(random);
        maths::matrix_perspective_rh(fresh, width, height, zn, zf);
        tests::d3dx_matrix_perspective_rh(want, width, height, zn, zf);
        compare_all(t_perspective, fresh, want, zn);
        const float ortho_zn = ortho_near(random);
        const float ortho_zf = ortho_zn + depth(random);
        maths::matrix_ortho_rh(fresh, width, height, ortho_zn, ortho_zf);
        tests::d3dx_matrix_ortho_rh(want, width, height, ortho_zn, ortho_zf);
        compare_all(t_ortho, fresh, want, ortho_zn);
    }
    for (const Tally* tally : {&t_point, &t_coord, &t_normal, &t_vec4, &t_normalize, &t_transpose, &t_scaling,
                               &t_rotation, &t_inverse, &t_determinant, &t_invertible, &t_look, &t_perspective,
                               &t_ortho}) {
        tally->print();
    }
}

// NOT ORIGINAL - the corpus's cases for the functions above (the game files' formulas, fresh::): their inputs as run()
// draws them and their results, written by the 32-bit build and compared by the 64-bit one (maths_corpus.h).
void corpus_fresh(tests::Corpus& corpus, tests::CorpusDraws draws)
{
    const int count = draws.count;
    std::mt19937_64 random(draws.seed);
    std::uniform_real_distribution<double> angle(-kAngleSpanFactor * kPi15, kAngleSpanFactor * kPi15);
    std::uniform_real_distribution<double> wide(-kWideRange, kWideRange);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    std::uniform_real_distribution<double> positive(kPositiveMin, kPositiveMax);
    std::uniform_real_distribution<float> coordinate(-kCoordinateRange, kCoordinateRange);
    std::uniform_real_distribution<float> small(-kSmallRange, kSmallRange);
    std::uniform_real_distribution<float> weight(0.0F, 1.0F);
    std::uniform_real_distribution<float> fov(kFovMin, kFovMax);
    std::uniform_int_distribution<int32_t> byte(0, kByteMax);
    std::uniform_int_distribution<int32_t> key_time(0, kKeyTimeMax);
    std::uniform_int_distribution<uint32_t> milliseconds(0, UINT32_MAX);
    std::uniform_int_distribution<uint64_t> vblanks(0, UINT64_MAX >> kVblankHeadroomBits);
    auto draw = [&](auto& distribution) { return corpus.in(distribution(random)); };

    corpus.section("game formulas");
    tests::CorpusGroup group(corpus,
                             "(a) the game files' formulas (the C++ that replaced their x87 code): this build vs the "
                             "32-bit build");
    auto& t_sin = group.tally("sin", 'd');
    auto& t_cos = group.tally("cos", 'd');
    auto& t_tan = group.tally("tan", 'd');
    auto& t_atan = group.tally("atan", 'd');
    auto& t_atan2 = group.tally("atan2", 'd');
    auto& t_sqrt = group.tally("sqrt", 'd');
    auto& t_log10 = group.tally("log10", 'd');
    auto& t_ln = group.tally("ln", 'd');
    auto& t_asin = group.tally("asin (C runtime)", 'd');
    auto& t_acos = group.tally("acos (C runtime)", 'd');
    auto& t_sin_f = group.tally("sin -> float", 'f');
    auto& t_cos_f = group.tally("cos -> float", 'f');
    auto& t_atan2_f = group.tally("atan2 -> float", 'f');
    auto& t_sin_wide = group.tally("sin, |angle| < 1e4", 'd');
    auto& t_sin_times = group.tally("sin * factor", 'd');
    auto& t_sin_product = group.tally("sin * sin", 'd');
    auto& t_cot = group.tally("cot", 'd');
    auto& t_atan2_diff = group.tally("atan2 - atan2", 'd');
    auto& t_cos_above = group.tally("cos_above", 'b');
    auto& t_sin_at_least = group.tally("sin_at_least", 'b');
    auto& t_tan_times = group.tally("tan_times", 'f');
    auto& t_atan2_qt = group.tally("atan2_less_quarter_turn", 'f');
    auto& t_blend = group.tally("blend_key_colors", 'u');
    auto& t_wibble_time = group.tally("wibble_time", 'f');
    auto& t_wibble = group.tally("wibble_offset", 'f');
    auto& t_vblank = group.tally("vblank_seconds", 'f');
    auto& t_probe = group.tally("probe_tip", 'f');
    auto& t_turn_move = group.tally("turn_move", 'f');
    auto& t_turn_y = group.tally("turn_about_y", 'f');
    auto& t_turn_y_z = group.tally("turn_about_y's z (double)", 'd');
    auto& t_stick = group.tally("turn_by_stick", 'f');
    auto& t_flat = group.tally("turn_flat", 'f');
    auto& t_asin_deg = group.tally("asin_degrees", 'f');
    auto& t_acos_deg = group.tally("acos_degrees", 'f');
    auto& t_fov = group.tally("view camera fov", 'f');
    auto& t_factor = group.tally("viewport angle factor", 'f');
    auto& t_versine = group.tally("1 - cos (double), mat4", 'd');
    auto& t_versine_sine = group.tally("sin -> float, mat4", 'f');
    auto& t_skin = group.tally("skinned position", 'f');
    for (int i = 0; i < count; ++i) {
        const double first_angle = draw(angle);
        const double second_angle = draw(angle);
        const double u = draw(unit);
        const double w = draw(wide);
        const double positive_value = draw(positive);
        corpus.out(t_sin, fresh::sin(first_angle), first_angle);
        corpus.out(t_cos, fresh::cos(first_angle), first_angle);
        corpus.out(t_tan, fresh::tan(first_angle), first_angle);
        corpus.out(t_atan, fresh::atan(w), w);
        corpus.out(t_atan2, fresh::atan2({.y = w, .x = first_angle}), w);
        corpus.out(t_sqrt, fresh::sqrt(positive_value), positive_value);
        corpus.out(t_log10, fresh::log10(positive_value), positive_value);
        corpus.out(t_ln, fresh::ln(positive_value), positive_value);
        corpus.out(t_asin, fresh::asin(u), u);
        corpus.out(t_acos, fresh::acos(u), u);
        corpus.out(t_sin_f, fresh::sin_f(first_angle), first_angle);
        corpus.out(t_cos_f, fresh::cos_f(first_angle), first_angle);
        corpus.out(t_atan2_f, fresh::atan2_f({.y = w, .x = first_angle}), w);
        corpus.out(t_sin_wide, fresh::sin(w), w);
        corpus.out(t_sin_times, fresh::sin_times(first_angle, w), first_angle);
        corpus.out(t_sin_product, fresh::sin_product(first_angle, second_angle), first_angle);
        corpus.out(t_cot, fresh::cot(first_angle), first_angle);
        const AtanPair pair{.left = {.y = first_angle, .x = w}, .right = {.y = second_angle, .x = u}};
        corpus.out(t_atan2_diff, fresh::atan2_difference(pair), first_angle);
        const auto cosine_limit = static_cast<float>(u);
        corpus.out(t_cos_above, fresh::cos_above({.angle = first_angle, .value = cosine_limit}), first_angle);
        corpus.out(t_sin_at_least, fresh::sin_at_least({.angle = first_angle, .value = cosine_limit}), first_angle);
        const auto fa = static_cast<float>(first_angle * 0.2);
        const float fb = draw(small);
        corpus.out(t_tan_times, fresh::tan_times({.angle = fa, .value = fb}), fa);
        const float y = draw(coordinate);
        const float x = draw(coordinate);
        corpus.out(t_atan2_qt, fresh::atan2_less_quarter_turn({.y = y, .x = x}), y);
        std::array<uint8_t, 4> from{};
        std::array<uint8_t, 4> to{};
        for (int k = 0; k < 4; ++k) {
            core::at(from, k) = static_cast<uint8_t>(draw(byte));
            core::at(to, k) = static_cast<uint8_t>(draw(byte));
        }
        const int32_t den = draw(key_time) + 1;
        const int32_t num = draw(key_time) % den;
        corpus.out(t_blend, fresh::blend_key_colors({.num = num, .den = den}, from, to), num);
        const uint32_t ms = draw(milliseconds);
        corpus.out(t_wibble_time, fresh::wibble_time(ms), ms);
        const float wave_time = fresh::wibble_time(ms % 3600000U);
        const float frequency = draw(small);
        const float phase = draw(small);
        const float amplitude = draw(small);
        const float velocity = draw(small);
        const Wibble wibble{
            .t = wave_time, .frequency = frequency, .phase = phase, .amplitude = amplitude, .velocity = velocity};
        corpus.out(t_wibble, fresh::wibble_offset(wibble, (i & 1) != 0), wave_time);
        const uint64_t vb = draw(vblanks);
        corpus.out(t_vblank, fresh::vblank_seconds({.count = vb, .rate = kFrameRate}), static_cast<double>(vb));
        const int index = i % 64;
        const ProbeTip tip = fresh::probe_tip({.index = index, .x = x, .z = y, .step = 0.09817477F, .reach = 12.0F});
        corpus.out(t_probe, tip.column, index);
        corpus.out(t_probe, tip.end, index);
        const Xz move = fresh::turn_move({.angle = first_angle, .a = u, .b = fb});
        corpus.out(t_turn_move, move.x, first_angle);
        corpus.out(t_turn_move, move.z, first_angle);
        const double ca = draw(coordinate);
        const double cb = draw(coordinate);
        const auto degrees = static_cast<float>(first_angle * 30.0);
        const TurnedY turned = fresh::turn_about_y({.dx = ca, .dz = cb, .angle = degrees, .scale = 0.017453292F});
        corpus.out(t_turn_y, turned.x, degrees);
        corpus.out(t_turn_y_z, turned.z, degrees);
        Xz stick{.x = x, .z = y};
        fresh::turn_by_stick(first_angle, &stick);
        corpus.out(t_stick, stick.x, first_angle);
        corpus.out(t_stick, stick.z, first_angle);
        const Xz flat = fresh::turn_flat(first_angle, {.x = x, .z = y});
        corpus.out(t_flat, flat.x, first_angle);
        corpus.out(t_flat, flat.z, first_angle);
        corpus.out(t_asin_deg, fresh::asin_degrees(u, (i & 1) != 0, kRadiansToDegrees), u);
        const auto fu = static_cast<float>(u);
        corpus.out(t_acos_deg, fresh::acos_degrees({.value = fu, .scale = kRadiansToDegrees}), fu);
        const float h_fov = draw(fov);
        const float default_angle = draw(fov);
        const float factor = draw(weight) + 0.5F;
        const float divisor = draw(weight) + 0.5F;
        const Fov view =
            fresh::refresh_fov({.angle = h_fov, .factor = factor, .divisor = divisor, .base = default_angle});
        corpus.out(t_fov, view.adjusted, h_fov);
        corpus.out(t_fov, view.ratio, h_fov);
        corpus.out(t_factor, fresh::angle_factor({.degrees = h_fov, .base = default_angle, .scale = kHalfDegree}),
                   h_fov);
        double versine = 0.0;
        float sine = 0.0F;
        fresh::versine_sine(fa, &versine, &sine);
        corpus.out(t_versine, versine, fa);
        corpus.out(t_versine_sine, sine, fa);
        fresh::Skin skin{};
        for (int k = 0; k < 4; ++k) {
            core::at(skin.e0, k) = draw(weight) + kWeightBias;
            core::at(skin.e1, k) = draw(weight) + kWeightBias;
            core::at(skin.e2, k) = draw(weight) + kWeightBias;
            core::at(skin.a, k) = draw(small) * kSmallScaleA;
            core::at(skin.b, k) = draw(small) * kSmallScaleA;
            core::at(skin.c, k) = draw(small) * kSmallScaleA;
            core::at(skin.p, k) = draw(small) * kSmallScaleB;
            core::at(skin.q, k) = draw(small) * kSmallScaleB;
            core::at(skin.r, k) = draw(small) * kSmallScaleB;
        }
        core::at(skin.s, 0) = draw(weight);
        core::at(skin.s, 1) = draw(weight) * (1.0F - core::at(skin.s, 0));
        core::at(skin.s, 2) = 1.0F - core::at(skin.s, 0) - core::at(skin.s, 1);
        std::array<float, 3> skinned{};
        fresh::skin(skin, skinned.data());
        corpus.outs<float>(t_skin, skinned, core::at(skin.s, 0));
    }
    corpus.section("game formulas end");
}

// NOT ORIGINAL - the corpus: `--write-corpus <file> [53|24] [count]` on the 32-bit build (the x87 at that precision while
// the cases run - 24 is the game's thread under Direct3D 9), `--compare-corpus <file>` on any build. Exit code 0 when
// the comparison read the corpus in step (differences are reported, not failed), 1 otherwise.
constexpr int kCorpusCount = 20000;
constexpr uint64_t kBaseMathsSeed = 10; // the cases' seeds after the corpus's own
constexpr uint64_t kD3dxSeed = 20;
constexpr uint32_t kFullPrecision = 53;
constexpr uint32_t kGamePrecision = 24;

void corpus_cases(tests::Corpus& corpus)
{
    const int count = static_cast<int>(corpus.header().count);
    const uint64_t seed = corpus.header().seed;
    corpus_fresh(corpus, {.count = count, .seed = seed});
    tests::corpus_base_maths(corpus, {.count = count, .seed = seed + kBaseMathsSeed});
    tests::corpus_d3dx(corpus, {.count = count, .seed = seed + kD3dxSeed});
}

int write_corpus(std::span<char*> arguments)
{
    const char* path = core::at(arguments, 2);
    const uint32_t precision =
        arguments.size() > 3 ? static_cast<uint32_t>(text::to_int(core::at(arguments, 3))) : kFullPrecision;
    const int count = arguments.size() > 4 ? text::to_int(core::at(arguments, 4)) : kCorpusCount;
    if (precision != kFullPrecision && precision != kGamePrecision) {
        text::print_to(stderr, "thug2_maths_check: the precision is 53 or 24\n");
        return 1;
    }
    tests::Corpus corpus = tests::Corpus::to_write(
        {.count = static_cast<uint32_t>(count), .seed = kRandomSeed, .precision_bits = precision});
#if defined(_MSC_VER) && defined(_M_IX86)
    unsigned int previous = 0;
    _controlfp_s(&previous, precision == kGamePrecision ? _PC_24 : _PC_53, _MCW_PC);
#else
    if (precision != kFullPrecision) {
        text::print_to(stderr, "thug2_maths_check: only the 32-bit x86 build has an x87 precision to set\n");
        return 1;
    }
#endif
    corpus_cases(corpus);
#if defined(_MSC_VER) && defined(_M_IX86)
    _controlfp_s(&previous, _PC_53, _MCW_PC);
#endif
    if (!tests::save_corpus_file(path, corpus.bytes())) {
        return 1;
    }
    text::print("thug2_maths_check: wrote %s (%d inputs per case, x87 at %u bits, %llu bytes)\n", path, count,
                precision, static_cast<unsigned long long>(corpus.bytes().size()));
    return 0;
}

int compare_corpus(std::span<char*> arguments)
{
    const char* path = core::at(arguments, 2);
    std::vector<uint8_t> bytes;
    tests::Corpus corpus = tests::Corpus::to_write({.count = 0, .seed = 0, .precision_bits = 0});
    if (!tests::load_corpus_file(path, &bytes) || !tests::Corpus::to_compare(std::move(bytes), &corpus)) {
        return 1;
    }
#if defined(_MSC_VER) && defined(_M_X64)
    // (a third argument, --fma3-off: the x64 C runtime's maths without its FMA3 code paths)
    const bool fma3_off = arguments.size() > 3 && std::strcmp(core::at(arguments, 3), "--fma3-off") == 0;
    if (fma3_off) {
        _set_FMA3_enable(0);
    }
#else
    const bool fma3_off = false;
#endif
    text::print(
        "thug2_maths_check: this build (%s%s) against %s (the 32-bit build, x87 at %u bits, %u inputs per case)\n",
        sizeof(void*) == sizeof(uint64_t) ? "64-bit" : "32-bit", fma3_off ? ", the C runtime's FMA3 paths off" : "",
        path, corpus.header().precision_bits, corpus.header().count);
    corpus_cases(corpus);
    text::print("inputs drawn here that differ from the corpus's: %llu\n",
                static_cast<unsigned long long>(corpus.drifted_inputs()));
    if (!corpus.in_step()) {
        text::print_to(stderr, "thug2_maths_check: the corpus didn't match this build's cases (out of step)\n");
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    if (arguments.size() > 2 && std::strcmp(core::at(arguments, 1), "--write-corpus") == 0) {
        return write_corpus(arguments);
    }
    if (arguments.size() > 2 && std::strcmp(core::at(arguments, 1), "--compare-corpus") == 0) {
        return compare_corpus(arguments);
    }
    const int count = argc > 1 ? text::to_int(core::at(arguments, 1)) : 200000;
    // (a second argument: another seed, for another sequence; the default's results are the ones compared)
    const int seed = argc > 2 ? text::to_int(core::at(arguments, 2)) : kRandomSeed;
#if defined(_MSC_VER) && defined(_M_IX86) // (the x87 reference: MSVC's inline assembly, 32-bit x86)
    std::mt19937_64 random(static_cast<uint64_t>(seed));
    text::print("x87 at 53 bits (the default control word)\n");
    run(count, random);
    unsigned int previous = 0;
    _controlfp_s(&previous, _PC_24, _MCW_PC);
    text::print("x87 at 24 bits (as Direct3D 9 leaves it on the device's thread)\n");
    run(count, random);
    _controlfp_s(&previous, _PC_53, _MCW_PC);
#else
    text::print("thug2_maths_check: the x87 reference needs MSVC for 32-bit x86; only D3DX9 is compared.\n");
#endif
    text::print("D3DX9's matrix product (d3dx9_43.dll's SSE version) against the plain C++\n");
    std::mt19937_64 matrix_random(static_cast<uint64_t>(seed)); // (its own sequence, from the start)
    matrix_product(count, matrix_random);
    // (the vector and viewing maths; one sequence, from the start, for each precision)
#if defined(_MSC_VER) && defined(_M_IX86)
    text::print("D3DX9's vector and viewing maths against the plain C++, x87 at 53 bits (the game's thread)\n");
    std::mt19937_64 view_random(static_cast<uint64_t>(seed));
    view_maths(count, view_random);
    _controlfp_s(&previous, _PC_24, _MCW_PC);
    text::print("D3DX9's vector and viewing maths against the plain C++, x87 at 24 bits\n");
    std::mt19937_64 view_random_24(static_cast<uint64_t>(seed));
    view_maths(count, view_random_24);
    _controlfp_s(&previous, _PC_53, _MCW_PC);
#else
    text::print("D3DX9's vector and viewing maths against the plain C++ (SSE2)\n");
    std::mt19937_64 view_random(static_cast<uint64_t>(seed));
    view_maths(count, view_random);
#endif
    return 0;
}
