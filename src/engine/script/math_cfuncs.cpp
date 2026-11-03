// Script commands (CFunctions) for maths, 0x00448AE0-0x00448EF0 (see math_cfuncs.h).

#include "engine/script/math_cfuncs.h"
#include "platform/d3dx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/cag_cfuncs.h"

namespace commands {


namespace {

constexpr uint32_t kSin = 0x483C6C55;        // "sin"
constexpr uint32_t kCos = 0x6146047A;        // "cos"
constexpr uint32_t kTan = 0x85AAF0D8;        // "tan"
constexpr uint32_t kASin = 0x15A3FA3D;       // "asin"
constexpr uint32_t kACos = 0x3CD99212;       // "acos"
constexpr uint32_t kATan = 0xD83566B0;       // "atan"
constexpr uint32_t kSqrt = 0xB86383A7;       // "sqrt"
constexpr uint32_t kNorm = 0x68C32A5F;       // "norm"
constexpr uint32_t kLength = 0xFE82614D;     // "length"
constexpr uint32_t kResult = 0xEC953EEC;     // "result"
constexpr uint32_t kFrom = 0x46E55E8F;       // "From"
constexpr uint32_t kTo = 0x28782D3B;         // "To"
constexpr uint32_t kValue = 0x08296457;      // (name unknown) LinearMap's input
constexpr uint32_t kLowerBound = 0x0EA9E0A8; // "lowerbound"
constexpr uint32_t kUpperBound = 0xACAECE33; // "upperbound"
constexpr uint32_t kMin = 0x5E84E22F;        // "min"
constexpr uint32_t kMax = 0x6289DD76;        // "max"
constexpr uint32_t kClamped = 0xCC419A08;    // "Clamped"
constexpr uint32_t kX = 0x7323E97C;          // "x"
constexpr uint32_t kY = 0x0424D9EA;          // "Y"
constexpr uint32_t kZ = 0x9D2D8850;          // "z"
constexpr uint32_t kRx = 0xFDF10F7A;         // "rx"
constexpr uint32_t kRy = 0x8AF63FEC;         // "ry"
constexpr uint32_t kRz = 0x13FF6E56;         // "rz"

float degree_to_radian_conv() { return kDegreeToRadianRatio; } // pi / 180
float convert_to_degrees() { return kScriptRadiansToDegrees; } // 180 / pi

// D3DX's matrix product (linked library code; stdcall): out = a * b.
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// The unnamed argument, in degrees for the trigonometry.
float get_argument(script::Struct* params)
{
    float value = 0.0F;
    params->fetch_float(0U, &value, 1);
    return value;
}

} // namespace

// 0x00448AE0: Sin <degrees>: sin = the sine.
bool cfunc_sin(script::Struct* params, script::Script* script)
{
    const float angle = get_argument(params);
    script->args()->push_float(checksums::Name{kSin}, static_cast<float>(std::sin(static_cast<double>(angle) * degree_to_radian_conv())));
    return true;
}

// 0x00448B20: Cos <degrees>: cos = the cosine.
bool cfunc_cos(script::Struct* params, script::Script* script)
{
    const float angle = get_argument(params);
    script->args()->push_float(checksums::Name{kCos}, static_cast<float>(std::cos(static_cast<double>(angle) * degree_to_radian_conv())));
    return true;
}

// 0x00448B60: Tan <degrees>: tan = the tangent.
bool cfunc_tan(script::Struct* params, script::Script* script)
{
    const float angle = get_argument(params);
    script->args()->push_float(checksums::Name{kTan}, static_cast<float>(std::tan(static_cast<double>(angle) * degree_to_radian_conv())));
    return true;
}

// 0x00448BA0: ASin <value>: asin = the arc sine in degrees (the C runtime's asin).
bool cfunc_a_sin(script::Struct* params, script::Script* script)
{
    const float value = get_argument(params);
    script->args()->push_float(checksums::Name{kASin}, static_cast<float>(std::asin(static_cast<double>(value)) * convert_to_degrees()));
    return true;
}

// 0x00448BE0: ACos <value>: acos = the arc cosine in degrees (the C runtime's acos).
bool cfunc_a_cos(script::Struct* params, script::Script* script)
{
    const float value = get_argument(params);
    script->args()->push_float(checksums::Name{kACos}, static_cast<float>(std::acos(static_cast<double>(value)) * convert_to_degrees()));
    return true;
}

// 0x00448C20: ATan <value>: atan = the arc tangent in degrees.
bool cfunc_a_tan(script::Struct* params, script::Script* script)
{
    const float value = get_argument(params);
    script->args()->push_float(checksums::Name{kATan}, static_cast<float>(std::atan(static_cast<double>(value)) * convert_to_degrees()));
    return true;
}

// 0x00448C60: Sqrt <value>: sqrt = the square root.
bool cfunc_sqrt(script::Struct* params, script::Script* script)
{
    const float value = get_argument(params);
    script->args()->push_float(checksums::Name{kSqrt}, static_cast<float>(std::sqrt(static_cast<double>(value))));
    return true;
}

// 0x00448CA0: NormalizeVector <vector>: norm = the vector scaled to length 1 (all four components scaled; a
// zero vector as it is), length = its length (x, y, z).
bool cfunc_normalize_vector(script::Struct* params, script::Script* script)
{
    script::VectorValue v = {};
    params->fetch_vec(0U, &v, 1);
    // (x87: the sum from z down, unrounded; the length rounded to a float)
    const double z2 = static_cast<double>(v.z) * v.z;
    const double y2 = static_cast<double>(v.y) * v.y;
    const double x2 = static_cast<double>(v.x) * v.x;
    auto length = static_cast<float>(std::sqrt(z2 + y2 + x2));
    if (length != 0.0F) {
        const double scale = 1.0 / length;
        v.x = static_cast<float>(v.x * scale);
        v.y = static_cast<float>(v.y * scale);
        v.z = static_cast<float>(v.z * scale);
        v.w = static_cast<float>(v.w * scale);
    }
    script->args()->put_vec3(checksums::Name{kNorm}, v);
    script->args()->push_float(checksums::Name{kLength}, length);
    return true;
}

// 0x00448D70: LinearMap result=<name> From=a To=b <value> lowerbound=lo upperbound=hi: <name> = where value
// lies between lo and hi (clamped to 0..1) mapped onto a..b.
bool cfunc_linear_map(script::Struct* params, script::Script* script)
{
    uint32_t result = 0;
    params->read_hash_value(kResult, &result, 1);
    float from = 0.0F;
    float to = 0.0F;
    float value = 0.0F;
    float lower = 0.0F;
    float upper = 0.0F;
    params->fetch_float(kFrom, &from, 1);
    params->fetch_float(kTo, &to, 1);
    params->fetch_float(kValue, &value, 1);
    params->fetch_float(kLowerBound, &lower, 1);
    params->fetch_float(kUpperBound, &upper, 1);
    // x87: the fraction stays unrounded (unless clamped)
    double fraction = (static_cast<double>(value) - lower) / (static_cast<double>(upper) - lower);
    if (fraction < 0.0)
        fraction = 0.0;
    else if (fraction > 1.0)
        fraction = 1.0;
    script->args()->push_float(checksums::Name{result},
                               static_cast<float>(((static_cast<double>(to) - from) * fraction) + from));
    return true;
}

// 0x00448E60: Clamp <value> min=lo max=hi: Clamped = the value limited to max, then to min (so min wins when
// min > max).
bool cfunc_clamp(script::Struct* params, script::Script* script)
{
    float value = 0.0F;
    float min = 0.0F;
    float max = 0.0F;
    params->fetch_float(0U, &value, 1);
    params->fetch_float(kMin, &min, 1);
    params->fetch_float(kMax, &max, 1);
    float clamped = value > max ? max : value;
    clamped = std::max(clamped, min);
    script->args()->push_float(checksums::Name{kClamped}, clamped);
    return true;
}

// 0x00448EF0: RotateVector x=.. Y=.. z=.. [rx=degrees] [ry=..] [rz=..]: x, Y, z = the point (w = 1) turned about
// the X axis, then Y, then Z (each rotation applied in front: identity, then Rx * m, Ry * m, Rz * m).
bool cfunc_rotate_vector(script::Struct* params, script::Script* script)
{
    maths::Vec4 point = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    params->fetch_float(kX, &point.x, 1);
    params->fetch_float(kY, &point.y, 1);
    params->fetch_float(kZ, &point.z, 1);
    float rx = 0.0F;
    float ry = 0.0F;
    float rz = 0.0F;
    params->fetch_float(kRx, &rx, 0);
    params->fetch_float(kRy, &ry, 0);
    params->fetch_float(kRz, &rz, 0);

    maths::Mat4 m{};
    m.reset_to_identity();
    maths::Mat4 rotation{};
    matrix_multiply(&m, &maths::make_pitch_matrix(rotation, rx * degree_to_radian_conv()), &m);
    matrix_multiply(&m, &maths::make_yaw_matrix(rotation, ry * degree_to_radian_conv()), &m);
    matrix_multiply(&m, &maths::make_roll_matrix(rotation, rz * degree_to_radian_conv()), &m);
    maths::Vec4 turned{};
    m.xform(turned, point);
    script->args()->push_float(checksums::Name{kX}, turned.x);
    script->args()->push_float(checksums::Name{kY}, turned.y);
    script->args()->push_float(checksums::Name{kZ}, turned.z);
    return true;
}

} // namespace commands
