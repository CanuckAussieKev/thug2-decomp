// NOT ORIGINAL - thug2_maths_check: the plain C++ that replaced the game's x87 inline assembly (style stage 7)
// compared with that assembly over many inputs. The reference half is the removed assembly itself, kept here only
// (MSVC x86: other compilers build the check without it and say so). Run it with no arguments; it prints, per
// function, how many results differ and the largest difference in units in the last place (ulps) of the stored type.
// It runs twice: at the x87's default 53-bit precision and at 24 bits - what Direct3D 9 sets on the thread that
// creates the device (the game creates it without D3DCREATE_FPU_PRESERVE), i.e. the precision the original's x87
// code ran at in the game.

#include <cmath>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <span>

#include "engine/maths/trig.h"
#include "engine/core/parse_number.h"
#include "engine/core/checked_index.h"

#if defined(_MSC_VER) && defined(_M_IX86)
#include <array>
#include "engine/core/format.h"
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a library's configuration / the compiler-specific macro
#define THUG2_X87_REFERENCE 1
#endif

namespace {

// The reference constants the checks compare with: the game's own values (a quarter turn, 1 / 1000, the 8 added to a
// texture coordinate and its 16 wrap, 180 / pi, the 60 Hz frame rate, the x87 status-word masks) and the ranges the random
// inputs are drawn from.
constexpr float kQuarterTurnRadians = 1.5707963705062866f;
constexpr int kBitsPerByte = 8;
constexpr float kMillisecondsToSeconds = 0.001f;
constexpr double kWibbleLift = 8.0;
constexpr float kWibbleLiftF = 8.0f;
constexpr float kTwoPow32 = 4294967296.0f;
constexpr int kX87AboveMask = 0x4100;
constexpr int kX87LessThanMask = 0x0500;
constexpr int kX87LessThanBit = 0x0100;
constexpr uint64_t kLowSixtyThreeBits = 0x7FFFFFFFFFFFFFFFull;
constexpr uint64_t kTopBit = 0x8000000000000000ull;
constexpr size_t kDoubleBytes = 8;
constexpr double kAngleSpanFactor = 4.0;
constexpr double kPi15 = 3.14159265358979;
constexpr double kWideRange = 1.0e4;
constexpr double kPositiveMin = 1.0e-6;
constexpr double kPositiveMax = 1.0e6;
constexpr float kCoordinateRange = 5000.0f;
constexpr float kSmallRange = 2.0f;
constexpr float kFovMin = 20.0f;
constexpr float kFovMax = 120.0f;
constexpr int kByteMax = 255;
constexpr int kKeyTimeMax = 100000;
constexpr int kVblankHeadroomBits = 20;
constexpr float kNegAsinCosLimit = 0.005f;
constexpr double kTextureWrap = 16.0;
constexpr int kFrameRate = 60;
constexpr float kRadiansToDegrees = 57.29578f;
constexpr float kWeightBias = 0.5f;
constexpr float kSmallScaleA = 20.0f;
constexpr float kSmallScaleB = 50.0f;
constexpr int kRandomSeed = 20261101;

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
double sin_times(double a, double f) { return std::sin(a) * f; }
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
    const double f = static_cast<double>(step.num) / step.den;
    const double rest = 1.0 - f;
    uint32_t color = 0;
    for (int k = 0; k < 4; ++k) {
        const auto value = static_cast<uint32_t>(maths::truncate_to_i64(core::at(to, core::at(kOrder, k)) * f + core::at(from, core::at(kOrder, k)) * rest));
        color = k ? (color << kBitsPerByte | value) : value;
    }
    return color;
}
float wibble_time(uint32_t milliseconds) { return static_cast<float>(static_cast<double>(milliseconds) * kMillisecondsToSeconds); }
float wibble_offset(const Wibble& w, bool store_first)
{
    double value = std::sin(static_cast<double>(w.t) * w.frequency + w.phase) * w.amplitude +
                   static_cast<double>(w.t) * w.velocity + kWibbleLift;
    const auto whole = static_cast<int32_t>(static_cast<uint32_t>(maths::truncate_to_i64(value)) & 0xFFFFFFF0u);
    value -= whole;
    if (store_first)
        value = static_cast<float>(value);
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
    return {.column = static_cast<float>((cosine - sine) * in.reach + in.x),
            .end = static_cast<float>(in.z + static_cast<double>(side))};
}
// park_editor.cpp
Xz turn_move(const Turn& in)
{
    const double sine = static_cast<float>(std::sin(in.angle));
    const double cosine = std::cos(in.angle);
    return {.x = static_cast<float>(cosine * in.b + sine * in.a), .z = static_cast<float>(cosine * in.a - sine * in.b)};
}
// rail_editor_draw.cpp
TurnedY turn_about_y(const TurnY& in)
{
    const double radians = static_cast<double>(in.angle) * in.scale;
    const double sine = static_cast<float>(std::sin(radians));
    const double cosine = std::cos(radians);
    return {.x = static_cast<float>(in.dx * cosine + in.dz * sine), .z = in.dz * cosine - in.dx * sine};
}
// walk_camera.cpp
void turn_by_stick(double angle, Xz* xz)
{
    const double sine = static_cast<float>(std::sin(angle));
    const double cosine = std::cos(angle);
    const double old_x = xz->x, old_z = xz->z;
    xz->x = static_cast<float>(old_z * sine + cosine * old_x);
    xz->z = static_cast<float>(old_z * cosine - sine * old_x);
}
// walk_physics.cpp / walk_movement.cpp
Xz turn_flat(double angle, Xz at)
{
    const double cosine = static_cast<float>(std::cos(angle));
    const double sine = std::sin(angle);
    return {.x = static_cast<float>(cosine * at.x - sine * at.z), .z = static_cast<float>(sine * at.x + cosine * at.z)};
}
float asin_degrees(double value, bool negate, float degrees)
{
    const double angle = std::asin(value);
    return static_cast<float>((negate ? -angle : angle) * degrees);
}
float acos_degrees(Scaled in) { return static_cast<float>(std::fabs(std::acos(in.value) * in.scale)); }
// view_cam.cpp
Fov refresh_fov(const FovIn& in)
{
    const float h_fov = in.angle;
    const float factor = in.factor;
    const float divisor = in.divisor;
    const float default_angle = in.base;
    const float to_half_radians = std::bit_cast<float>(0x3C0EFA35u), to_degrees = std::bit_cast<float>(0x42E52EE0u),
                half = 0.5f, to_radians = std::bit_cast<float>(0x3C8EFA35u);
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
void versine_sine(float angle, double* t, float* s)
{
    *t = 1.0f - std::cos(static_cast<double>(angle));
    *s = static_cast<float>(std::sin(static_cast<double>(angle)));
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
    const float ax = core::at(v.p, 0) + core::at(v.e0, 0) * core::at(v.a, 0), ay = core::at(v.p, 1) + core::at(v.e0, 2) * core::at(v.a, 1), az = core::at(v.p, 2) + core::at(v.e0, 1) * core::at(v.a, 2);
    const float bx = core::at(v.q, 0) + core::at(v.e1, 0) * core::at(v.b, 0), by = core::at(v.q, 1) + core::at(v.e1, 2) * core::at(v.b, 1), bz = core::at(v.q, 2) + core::at(v.e1, 1) * core::at(v.b, 2);
    const float cx = core::at(v.r, 0) + core::at(v.e2, 0) * core::at(v.c, 0), cy = core::at(v.r, 1) + core::at(v.e2, 2) * core::at(v.c, 1), cz = core::at(v.r, 2) + core::at(v.e2, 1) * core::at(v.c, 2);
    core::at(out, 0) = ax * core::at(v.s, 0) + bx * core::at(v.s, 1) + cx * core::at(v.s, 2);
    core::at(out, 1) = ay * core::at(v.s, 0) + by * core::at(v.s, 1) + cy * core::at(v.s, 2);
    core::at(out, 2) = az * core::at(v.s, 0) + bz * core::at(v.s, 1) + cz * core::at(v.s, 2);
}

} // namespace fresh

#ifdef THUG2_X87_REFERENCE

// --- the removed assembly ----------------------------------------------------------------------------------------

// NOLINTBEGIN(misc-const-correctness): the locals the inline x87 assembly reads and writes - clang-tidy doesn't
// see __asm blocks' operands, so it takes every one of them for unchanged
namespace ref {

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
    double r = 0.0;
    __asm { fld a
        fsin
        fstp r }
    return r;
}
double cos(double a)
{
    double r = 0.0;
    __asm { fld a
        fcos
        fstp r }
    return r;
}
double tan(double a)
{
    double r = 0.0;
    __asm { fld a
        fptan
        fstp st(0)
        fstp r }
    return r;
}
double atan(double a)
{
    double r = 0.0;
    __asm { fld a
        fld1
        fpatan
        fstp r }
    return r;
}
double atan2(Yx in)
{
    const double y = in.y;
    const double x = in.x;
    double r = 0.0;
    __asm { fld y
        fld x
        fpatan
        fstp r }
    return r;
}
double sqrt(double a)
{
    double r = 0.0;
    __asm { fld a
        fsqrt
        fstp r }
    return r;
}
double log10(double x)
{
    double r = 0.0;
    __asm { fldlg2
        fld x
        fyl2x
        fstp r }
    return r;
}
double ln(double x)
{
    double r = 0.0;
    __asm { fldln2
        fld x
        fyl2x
        fstp r }
    return r;
}
double asin(double v)
{
    double r = 0.0;
    void* function = asin_x87;
    __asm { fld v
        call function
        fstp r }
    return r;
}
double acos(double v)
{
    double r = 0.0;
    void* function = acos_x87;
    __asm { fld v
        call function
        fstp r }
    return r;
}
float sin_f(double a)
{
    float r = 0.0f;
    __asm { fld a
        fsin
        fstp r }
    return r;
}
float cos_f(double a)
{
    float r = 0.0f;
    __asm { fld a
        fcos
        fstp r }
    return r;
}
float atan2_f(Yx in)
{
    const double y = in.y;
    const double x = in.x;
    float r = 0.0f;
    __asm { fld y
        fld x
        fpatan
        fstp r }
    return r;
}
double sin_times(double a, double f)
{
    double r = 0.0;
    __asm { fld a
        fsin
        fmul f
        fstp r }
    return r;
}
double sin_product(double a, double b)
{
    double r = 0.0;
    __asm { fld a
        fsin
        fld b
        fsin
        fmulp st(1), st
        fstp r }
    return r;
}
double cot(double a)
{
    double r = 0.0;
    __asm { fld a
        fld st(0)
        fcos
        fxch st(1)
        fsin
        fdivp st(1), st
        fstp r }
    return r;
}
double atan2_difference(const AtanPair& in)
{
    const double y1 = in.left.y;
    const double x1 = in.left.x;
    const double y2 = in.right.y;
    const double x2 = in.right.x;
    double r = 0.0;
    __asm { fld y1
        fld x1
        fpatan
        fld y2
        fld x2
        fpatan
        fsubp st(1), st
        fstp r }
    return r;
}
float acos_f(maths::AngleLimit in, bool* above_limit)
{
    const double value = in.value;
    const float limit = in.limit;
    float result = 0.0f;
    void* function = acos_x87;
    uint16_t status = 0;
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
    float angle = 0.0f;
    double inv = 0.0, neg_inv = 0.0;
    uint16_t status = 0;
    void* function = asin_x87;
    const float one = 1.0f, minus_one = -1.0f;
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
    double ra = 0.0, rb = 0.0;
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
    uint16_t status = 0;
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
    uint16_t status = 0;
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
    float result = 0.0f;
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
    const float quarter_turn = 1.5707963705062866f;
    float result = 0.0f;
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
    double scratch = 0;
    __asm {
        fild num
        fild den
        fdivp st(1), st(0)
        fld1
        fsub st(0), st(1)
    }
    for (int k = 0; k < 4; ++k)
    {
        int a = core::at(to, core::at(kOrder, k));
        int b = core::at(from, core::at(kOrder, k));
        int64_t value = 0;
        __asm {
            fild a
            fmul st(0), st(2)
            fild b
            fmul st(0), st(2)
            faddp st(1), st(0)
            fisttp value
        }
        color = k ? (color << kBitsPerByte | static_cast<uint32_t>(value)) : static_cast<uint32_t>(value);
    }
    __asm {
        fstp scratch
        fstp scratch
    }
    return color;
}
float wibble_time(uint32_t milliseconds)
{
    float result = 0.0f;
    float scale = kMillisecondsToSeconds;
    float wrap = kTwoPow32;
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
    const float t = w.t;
    const float frequency = w.frequency;
    const float phase = w.phase;
    const float amplitude = w.amplitude;
    const float velocity = w.velocity;
    float result = 0.0f;
    float eight = kWibbleLiftF;
    float zero = 0.0f;
    int64_t whole = 0;
    int truncated = 0;
    float stored = 0.0f;
    __asm {
        fld t
        fmul frequency
        fadd phase
        fsin
        fmul amplitude
        fld t
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
    std::array<uint64_t, 1> low_part = {{vblanks & kLowSixtyThreeBits}};
    std::array<uint64_t, 1> high_part = {{vblanks & kTopBit}};
    float result = 0.0f;
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
    ProbeTip tip{.column = 0.0f, .end = 0.0f};
    float* last_column = &tip.column;
    float* end_z = &tip.end;
    float angle_f = 0.0f;
    float slot = 0.0f;
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
    const double a = in.a;
    const double b = in.b;
    float sine = 0.0f;
    float result_x = 0.0f;
    float result_z = 0.0f;
    __asm {
        fld angle
        fld st(0)
        fsin
        fstp sine
        fcos
        fld st(0)
        fmul b
        fld sine
        fmul a
        faddp st(1), st
        fstp result_x
        fmul a
        fld sine
        fmul b
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
    float sine = 0.0f;
    float x = 0.0f;
    double z = 0.0;
    float degree_factor = scale;
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
    float* x = &xz->x;
    float* z = &xz->z;
    float sine = 0.0f;
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
    float cosine = 0.0f, rx = 0.0f, rz = 0.0f;
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
    float result = 0.0f;
    void* function = asin_x87;
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
    float result = 0.0f;
    void* function = acos_x87;
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
    const float to_half_radians = std::bit_cast<float>(0x3C0EFA35u), to_degrees = std::bit_cast<float>(0x42E52EE0u),
                half = 0.5f, to_radians = std::bit_cast<float>(0x3C8EFA35u);
    float half_angle = 0.0f;
    __asm {
        fld h_fov
        fmul to_half_radians
        fstp half_angle
    }
    float adjusted = 0.0f, tan_adjusted = 0.0f;
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
    float ratio = 0.0f;
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
    float factor = 0.0f;
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
    double t = 0.0;
    float s = 0.0f;
    const float one = 1.0f;
    __asm {
        fld angle
        fld st(0)
        fcos
        fsubr one
        fstp t
        fsin
        fstp s
    }
    *out_t = t;
    *out_s = s;
}
void skin_memory(const fresh::Skin& v, float* out_floats)
{
    const std::span<float, 3> out(out_floats, 3);
    float e0_0 = core::at(v.e0, 0), e0_4 = core::at(v.e0, 1), e0_8 = core::at(v.e0, 2), e1_0 = core::at(v.e1, 0), e1_4 = core::at(v.e1, 1), e1_8 = core::at(v.e1, 2);
    float e2_0 = core::at(v.e2, 0), e2_4 = core::at(v.e2, 1), e2_8 = core::at(v.e2, 2);
    float a_x = core::at(v.a, 0), ay = core::at(v.a, 1), az = core::at(v.a, 2), b_x = core::at(v.b, 0), by = core::at(v.b, 1), bz = core::at(v.b, 2);
    float c_x = core::at(v.c, 0), cy = core::at(v.c, 1), cz = core::at(v.c, 2);
    float px = core::at(v.p, 0), py = core::at(v.p, 1), pz = core::at(v.p, 2), qx = core::at(v.q, 0), qy = core::at(v.q, 1), qz = core::at(v.q, 2);
    float rx = core::at(v.r, 0), ry = core::at(v.r, 1), rz = core::at(v.r, 2);
    float s0 = core::at(v.s, 0), s1 = core::at(v.s, 1), s2 = core::at(v.s, 2);
    float t0 = 0.0f, t1 = 0.0f, v2 = 0.0f, w1 = 0.0f, w2 = 0.0f, x1 = 0.0f, y1 = 0.0f, z1 = 0.0f, out_x = 0.0f,
          out_y = 0.0f, out_z = 0.0f;
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
    float e0_0 = core::at(v.e0, 0), e0_4 = core::at(v.e0, 1), e0_8 = core::at(v.e0, 2), e0_c = core::at(v.e0, 3), e1_0 = core::at(v.e1, 0), e1_4 = core::at(v.e1, 1),
          e1_8 = core::at(v.e1, 2), e1_c = core::at(v.e1, 3);
    float e2_0 = core::at(v.e2, 0), e2_4 = core::at(v.e2, 1), e2_8 = core::at(v.e2, 2), e2_c = core::at(v.e2, 3);
    float a_x = core::at(v.a, 0), ay = core::at(v.a, 1), az = core::at(v.a, 2), aw = core::at(v.a, 3), b_x = core::at(v.b, 0), by = core::at(v.b, 1), bz = core::at(v.b, 2), bw = core::at(v.b, 3);
    float c_x = core::at(v.c, 0), cy = core::at(v.c, 1), cz = core::at(v.c, 2), cw = core::at(v.c, 3);
    float px = core::at(v.p, 0), py = core::at(v.p, 1), pz = core::at(v.p, 2), pw = core::at(v.p, 3), qx = core::at(v.q, 0), qy = core::at(v.q, 1), qz = core::at(v.q, 2), qw = core::at(v.q, 3);
    float rx = core::at(v.r, 0), ry = core::at(v.r, 1), rz = core::at(v.r, 2), rw = core::at(v.r, 3);
    float s0 = core::at(v.s, 0), s1 = core::at(v.s, 1), s2 = core::at(v.s, 2);
    float cyw = 0.0f, czw = 0.0f, bzw = 0.0f, ayw = 0.0f, azw = 0.0f, x1 = 0.0f, y1 = 0.0f, z1 = 0.0f, out_x = 0.0f,
          out_y = 0.0f, out_z = 0.0f;
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
// NOLINTEND(misc-const-correctness)

// --- the comparison ----------------------------------------------------------------------------------------------

// The distance in ulps between two doubles / floats (NaNs equal to each other; a NaN against a number: a huge value).
uint64_t ulps(double a, double b)
{
    if (std::isnan(a) || std::isnan(b))
        return std::isnan(a) && std::isnan(b) ? 0 : UINT64_MAX;
    int64_t ia = 0, ib = 0;
    std::memcpy(&ia, &a, kDoubleBytes);
    std::memcpy(&ib, &b, kDoubleBytes);
    if (ia < 0)
        ia = INT64_MIN - ia;
    if (ib < 0)
        ib = INT64_MIN - ib;
    return ia > ib ? static_cast<uint64_t>(ia) - static_cast<uint64_t>(ib)
                   : static_cast<uint64_t>(ib) - static_cast<uint64_t>(ia);
}
uint64_t ulps(float a, float b)
{
    if (std::isnan(a) || std::isnan(b))
        return std::isnan(a) && std::isnan(b) ? 0 : UINT64_MAX;
    int32_t ia = 0, ib = 0;
    std::memcpy(&ia, &a, 4);
    std::memcpy(&ib, &b, 4);
    if (ia < 0)
        ia = INT32_MIN - ia;
    if (ib < 0)
        ib = INT32_MIN - ib;
    return static_cast<uint64_t>(ia > ib ? static_cast<int64_t>(ia) - ib : static_cast<int64_t>(ib) - ia);
}

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
        if (distance) {
            ++differing_;
            if (distance > max_ulps_) {
                max_ulps_ = distance;
                worst_input_ = input;
            }
            if (abs_difference > max_abs_)
                max_abs_ = abs_difference;
        }
    }
    void print() const
    {
        text::print("  %-26s %9llu runs  %9llu differ (%6.3f%%)  max %c%llu  |diff| %.3g  (at %.9g)\n", name_,
                    static_cast<unsigned long long>(runs_), static_cast<unsigned long long>(differing_),
                    runs_ ? 100.0 * static_cast<double>(differing_) / static_cast<double>(runs_) : 0.0, unit_,
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
    const uint64_t d = ulps(fresh_value, ref_value);
    tally.add({.distance = d,
               .difference =
                   d == UINT64_MAX ? INFINITY : std::fabs(static_cast<double>(fresh_value) - static_cast<double>(ref_value)),
               .input = input});
}
void compare_bool(Tally& tally, bool a, bool b, double input)
{
    tally.add({.distance = a != b, .difference = static_cast<double>(a != b), .input = input});
}

void run(int count, std::mt19937_64& random)
{
    std::uniform_real_distribution<double> angle(-kAngleSpanFactor * kPi15, kAngleSpanFactor * kPi15);
    std::uniform_real_distribution<double> wide(-kWideRange, kWideRange);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    std::uniform_real_distribution<double> positive(kPositiveMin, kPositiveMax);
    std::uniform_real_distribution<float> coordinate(-kCoordinateRange, kCoordinateRange);
    std::uniform_real_distribution<float> small(-kSmallRange, kSmallRange);
    std::uniform_real_distribution<float> weight(0.0f, 1.0f);
    std::uniform_real_distribution<float> fov(kFovMin, kFovMax);
    std::uniform_int_distribution<int> byte(0, kByteMax);
    std::uniform_int_distribution<int> key_time(0, kKeyTimeMax);
    std::uniform_int_distribution<uint32_t> milliseconds(0, UINT32_MAX);
    std::uniform_int_distribution<uint64_t> vblanks(0, UINT64_MAX >> kVblankHeadroomBits);

    Tally t_sin("sin", 'd'), t_cos("cos", 'd'), t_tan("tan", 'd'), t_atan("atan", 'd'), t_atan2("atan2", 'd'),
        t_sqrt("sqrt", 'd'), t_log10("log10", 'd'), t_ln("ln", 'd'), t_asin("asin (C runtime)", 'd'),
        t_acos("acos (C runtime)", 'd'), t_sin_f("sin -> float", 'f'), t_cos_f("cos -> float", 'f'),
        t_atan2_f("atan2 -> float", 'f'), t_sin_wide("sin, |angle| < 1e4", 'd'), t_sin_times("sin * factor", 'd'),
        t_sin_product("sin * sin", 'd'), t_cot("cot", 'd'), t_atan2_diff("atan2 - atan2", 'd'), t_acos_f("acos_f", 'f'),
        t_acos_f_above("acos_f's comparison", 'b'), t_nac("neg_asin_cos", 'f'),
        t_nac_above("neg_asin_cos's comparison", 'b'), t_nac_inv("neg_asin_cos 1 / cos", 'd'), t_spin("spin_by", 'd'),
        t_cos_above("cos_above", 'b'), t_sin_at_least("sin_at_least", 'b'), t_tan_times("tan_times", 'f'),
        t_atan2_qt("atan2_less_quarter_turn", 'f'), t_blend("blend_key_colors (steps)", 'u'),
        t_wibble_time("wibble_time", 'f'), t_wibble("wibble_offset", 'f'),
        t_wibble_wrap("wibble_offset modulo 16", 'b'), t_vblank("vblank_seconds", 'f'), t_probe("probe_tip", 'f'),
        t_turn_move("turn_move", 'f'), t_turn_y("turn_about_y", 'f'), t_stick("turn_by_stick", 'f'),
        t_flat("turn_flat", 'f'), t_asin_deg("asin_degrees", 'f'), t_acos_deg("acos_degrees", 'f'),
        t_fov("view camera fov", 'f'), t_factor("viewport angle factor", 'f'), t_versine("1 - cos, sin (mat4)", 'f'),
        t_skin_m("skinned position (memory)", 'f'), t_skin_f("skinned position (file)", 'f');

    for (int i = 0; i < count; ++i) {
        const double a = angle(random), b = angle(random), u = unit(random), w = wide(random);
        compare(t_sin, fresh::sin(a), ref::sin(a), a);
        compare(t_cos, fresh::cos(a), ref::cos(a), a);
        compare(t_tan, fresh::tan(a), ref::tan(a), a);
        compare(t_atan, fresh::atan(w), ref::atan(w), w);
        compare(t_atan2, fresh::atan2({.y = w, .x = a}), ref::atan2({.y = w, .x = a}), w);
        const double p = positive(random);
        compare(t_sqrt, fresh::sqrt(p), ref::sqrt(p), p);
        compare(t_log10, fresh::log10(p), ref::log10(p), p);
        compare(t_ln, fresh::ln(p), ref::ln(p), p);
        compare(t_asin, fresh::asin(u), ref::asin(u), u);
        compare(t_acos, fresh::acos(u), ref::acos(u), u);
        compare(t_sin_f, fresh::sin_f(a), ref::sin_f(a), a);
        compare(t_cos_f, fresh::cos_f(a), ref::cos_f(a), a);
        compare(t_atan2_f, fresh::atan2_f({.y = w, .x = a}), ref::atan2_f({.y = w, .x = a}), w);
        compare(t_sin_wide, fresh::sin(w), ref::sin(w), w);
        compare(t_sin_times, fresh::sin_times(a, w), ref::sin_times(a, w), a);
        compare(t_sin_product, fresh::sin_product(a, b), ref::sin_product(a, b), a);
        compare(t_cot, fresh::cot(a), ref::cot(a), a);
        const AtanPair pair{.left = {.y = a, .x = w}, .right = {.y = b, .x = u}};
        compare(t_atan2_diff, fresh::atan2_difference(pair), ref::atan2_difference(pair), a);
        bool above_fresh = false, above_ref = false;
        const auto limit = static_cast<float>(weight(random) * 3.0f);
        compare(t_acos_f, fresh::acos_f({.value = u, .limit = limit}, &above_fresh), ref::acos_f({.value = u, .limit = limit}, &above_ref), u);
        compare_bool(t_acos_f_above, above_fresh, above_ref, u);
        maths::Inverses inv_fresh{.inverse = 0.0, .negative = 0.0};
        maths::Inverses inv_ref{.inverse = 0.0, .negative = 0.0};
        const double s = static_cast<float>(u);
        compare(t_nac, fresh::neg_asin_cos({.value = s, .limit = kNegAsinCosLimit}, &above_fresh, &inv_fresh),
                ref::neg_asin_cos({.value = s, .limit = kNegAsinCosLimit}, &above_ref, &inv_ref), s);
        compare_bool(t_nac_above, above_fresh, above_ref, s);
        compare(t_nac_inv, inv_fresh.inverse, inv_ref.inverse, s);
        compare(t_nac_inv, inv_fresh.negative, inv_ref.negative, s);
        double ra_fresh = 0.0, rb_fresh = 0.0, ra_ref = 0.0, rb_ref = 0.0;
        const double ca = coordinate(random), cb = coordinate(random);
        fresh::spin_by(a, ca, cb, &ra_fresh, &rb_fresh);
        ref::spin_by(a, ca, cb, &ra_ref, &rb_ref);
        compare(t_spin, ra_fresh, ra_ref, a);
        compare(t_spin, rb_fresh, rb_ref, a);
        const auto cosine_limit = static_cast<float>(u);
        compare_bool(t_cos_above, fresh::cos_above({.angle = a, .value = cosine_limit}), ref::cos_above({.angle = a, .value = cosine_limit}), a);
        compare_bool(t_sin_at_least, fresh::sin_at_least({.angle = a, .value = cosine_limit}), ref::sin_at_least({.angle = a, .value = cosine_limit}), a);
        const float fa = static_cast<float>(a * 0.2), fb = small(random);
        compare(t_tan_times, fresh::tan_times({.angle = fa, .value = fb}), ref::tan_times({.angle = fa, .value = fb}), fa);
        const float y = coordinate(random), x = coordinate(random);
        compare(t_atan2_qt, fresh::atan2_less_quarter_turn({.y = y, .x = x}), ref::atan2_less_quarter_turn({.y = y, .x = x}), y);
        std::array<uint8_t, 4> from{};
        std::array<uint8_t, 4> to{};
        for (int k = 0; k < 4; ++k) {
            core::at(from, k) = static_cast<uint8_t>(byte(random));
            core::at(to, k) = static_cast<uint8_t>(byte(random));
        }
        const int den = key_time(random) + 1, num = key_time(random) % den;
        {
            // (the largest step any of the four channels differs by)
            const uint32_t c_fresh = fresh::blend_key_colors({.num = num, .den = den}, from, to),
                           c_ref = ref::blend_key_colors({.num = num, .den = den}, from, to);
            uint64_t steps = 0;
            for (int k = 0; k < 4; ++k) {
                const int d =
                    static_cast<int>((c_fresh >> (8 * k)) & 0xFF) - static_cast<int>((c_ref >> (8 * k)) & 0xFF);
                steps = std::max<uint64_t>(steps, static_cast<uint64_t>(d < 0 ? -d : d));
            }
            t_blend.add({.distance = steps, .difference = static_cast<double>(steps), .input = static_cast<double>(num)});
        }
        const uint32_t ms = milliseconds(random);
        compare(t_wibble_time, fresh::wibble_time(ms), ref::wibble_time(ms), ms);
        const float t = fresh::wibble_time(ms % 3600000u), frequency = small(random), phase = small(random),
                    amplitude = small(random), velocity = small(random);
        const bool store_first = (i & 1) != 0;
        const Wibble wibble{.t = t, .frequency = frequency, .phase = phase, .amplitude = amplitude, .velocity = velocity};
        compare(t_wibble, fresh::wibble_offset(wibble, store_first), ref::wibble_offset(wibble, store_first), t);
        {
            // (the offset is a texture coordinate that wraps every 16: the difference modulo 16)
            const float w_fresh = fresh::wibble_offset(wibble, store_first), w_ref = ref::wibble_offset(wibble, store_first);
            double d = std::fmod(std::fabs(static_cast<double>(w_fresh) - w_ref), kTextureWrap);
            d = std::min(d, kTextureWrap - d);
            t_wibble_wrap.add({.distance = d != 0.0, .difference = d, .input = t});
        }
        const uint64_t vb = vblanks(random);
        compare(t_vblank, fresh::vblank_seconds({.count = vb, .rate = kFrameRate}), ref::vblank_seconds({.count = vb, .rate = kFrameRate}), static_cast<double>(vb));
        const int index = i % 64;
        const Probe probe{.index = index, .x = x, .z = y, .step = 0.09817477f, .reach = 12.0f};
        const ProbeTip tip_fresh = fresh::probe_tip(probe);
        const ProbeTip tip_ref = ref::probe_tip(probe);
        compare(t_probe, tip_fresh.column, tip_ref.column, index);
        compare(t_probe, tip_fresh.end, tip_ref.end, index);
        const Xz move_fresh = fresh::turn_move({.angle = a, .a = u, .b = fb});
        const Xz move_ref = ref::turn_move({.angle = a, .a = u, .b = fb});
        compare(t_turn_move, move_fresh.x, move_ref.x, a);
        compare(t_turn_move, move_fresh.z, move_ref.z, a);
        const auto degrees = static_cast<float>(a * 30.0);
        const TurnY turn_y{.dx = ca, .dz = cb, .angle = degrees, .scale = 0.017453292f};
        const TurnedY y_fresh = fresh::turn_about_y(turn_y);
        const TurnedY y_ref = ref::turn_about_y(turn_y);
        compare(t_turn_y, y_fresh.x, y_ref.x, degrees);
        compare(t_turn_y, static_cast<float>(y_fresh.z), static_cast<float>(y_ref.z), degrees);
        Xz stick_fresh{.x = x, .z = y};
        Xz stick_ref{.x = x, .z = y};
        fresh::turn_by_stick(a, &stick_fresh);
        ref::turn_by_stick(a, &stick_ref);
        compare(t_stick, stick_fresh.x, stick_ref.x, a);
        compare(t_stick, stick_fresh.z, stick_ref.z, a);
        const Xz flat_fresh = fresh::turn_flat(a, {.x = x, .z = y});
        const Xz flat_ref = ref::turn_flat(a, {.x = x, .z = y});
        compare(t_flat, flat_fresh.x, flat_ref.x, a);
        compare(t_flat, flat_fresh.z, flat_ref.z, a);
        compare(t_asin_deg, fresh::asin_degrees(u, store_first, kRadiansToDegrees),
                ref::asin_degrees(u, store_first, kRadiansToDegrees), u);
        const auto fu = static_cast<float>(u);
        compare(t_acos_deg, fresh::acos_degrees({.value = fu, .scale = kRadiansToDegrees}), ref::acos_degrees({.value = fu, .scale = kRadiansToDegrees}), fu);
        const float h_fov = fov(random), default_angle = fov(random), factor = weight(random) + 0.5f,
                    divisor = weight(random) + 0.5f;
        const FovIn fov_in{.angle = h_fov, .factor = factor, .divisor = divisor, .base = default_angle};
        const Fov fov_fresh = fresh::refresh_fov(fov_in);
        const Fov fov_ref = ref::refresh_fov(fov_in);
        compare(t_fov, fov_fresh.adjusted, fov_ref.adjusted, h_fov);
        compare(t_fov, fov_fresh.ratio, fov_ref.ratio, h_fov);
        const FactorIn factor_in{.degrees = h_fov, .base = default_angle, .scale = 0.008726646f};
        compare(t_factor, fresh::angle_factor(factor_in), ref::angle_factor(factor_in), h_fov);
        double vt_fresh = 0.0, vt_ref = 0.0;
        float vs_fresh = 0.0f, vs_ref = 0.0f;
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
        core::at(skin.s, 1) = weight(random) * (1.0f - core::at(skin.s, 0));
        core::at(skin.s, 2) = 1.0f - core::at(skin.s, 0) - core::at(skin.s, 1);
        std::array<float, 3> out_fresh{};
        std::array<float, 3> out_ref{};
        fresh::skin(skin, out_fresh.data());
        ref::skin_memory(skin, out_ref.data());
        for (int k = 0; k < 3; ++k)
            compare(t_skin_m, core::at(out_fresh, k), core::at(out_ref, k), k);
        fresh::skin(skin, out_fresh.data());
        ref::skin_file(skin, out_ref.data());
        for (int k = 0; k < 3; ++k)
            compare(t_skin_f, core::at(out_fresh, k), core::at(out_ref, k), k);
    }
    // The edges: +-1, 0, NaN, outside [-1, 1] for asin / acos.
    const auto edges = std::to_array<double>({1.0, -1.0, 0.0, -0.0, 1.0000001, -1.5, 2.0, NAN, INFINITY, -static_cast<double>(INFINITY)});
    for (const double e : edges) {
        compare(t_asin, fresh::asin(e), ref::asin(e), e);
        compare(t_acos, fresh::acos(e), ref::acos(e), e);
    }

    auto all = std::to_array<const Tally*>({&t_sin,       &t_cos,          &t_tan,       &t_atan,        &t_atan2,     &t_sqrt,
                          &t_log10,     &t_ln,           &t_asin,      &t_acos,        &t_sin_f,     &t_cos_f,
                          &t_atan2_f,   &t_sin_wide,     &t_sin_times, &t_sin_product, &t_cot,       &t_atan2_diff,
                          &t_acos_f,    &t_acos_f_above, &t_nac,       &t_nac_above,   &t_nac_inv,   &t_spin,
                          &t_cos_above, &t_sin_at_least, &t_tan_times, &t_atan2_qt,    &t_blend,     &t_wibble_time,
                          &t_wibble,    &t_wibble_wrap,  &t_vblank,    &t_probe,       &t_turn_move, &t_turn_y,
                          &t_stick,     &t_flat,         &t_asin_deg,  &t_acos_deg,    &t_fov,       &t_factor,
                          &t_versine,   &t_skin_m,       &t_skin_f});
    for (const Tally* tally : all)
        tally->print();
}

#endif // THUG2_X87_REFERENCE

} // namespace

int main(int argc, char** argv)
{
#ifdef THUG2_X87_REFERENCE
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    const int count = argc > 1 ? text::to_int(core::at(arguments, 1)) : 200000;
    // NOLINTNEXTLINE(bugprone-random-generator-seed): the original's seed / a test's fixed sequence
    std::mt19937_64 random(kRandomSeed);
    text::print("x87 at 53 bits (the default control word)\n");
    run(count, random);
    unsigned int previous = 0;
    _controlfp_s(&previous, _PC_24, _MCW_PC);
    text::print("x87 at 24 bits (as Direct3D 9 leaves it on the device's thread)\n");
    run(count, random);
    _controlfp_s(&previous, _PC_53, _MCW_PC);
    return 0;
#else
    (void)argc;
    (void)argv;
    text::print("thug2_maths_check: the x87 reference needs MSVC for 32-bit x86; nothing to compare.\n");
    return 0;
#endif
}
