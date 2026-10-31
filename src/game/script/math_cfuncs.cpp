// Script commands (CFunctions) for maths, 0x00448AE0-0x00448EF0 (see math_cfuncs.h).

#include "game/script/math_cfuncs.h"
#include "game/gfx/d3dx.h"

#include <cmath>
#include <cstdint>

#include "game/maths/mat4.h"
#include "game/maths/vec4.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/gfx/cag_cfuncs.h"

namespace ScriptCommands {

float g_scriptRadiansToDegrees{57.295776f};

namespace {

constexpr uint32_t kSin = 0x483C6C55;          // "sin"
constexpr uint32_t kCos = 0x6146047A;          // "cos"
constexpr uint32_t kTan = 0x85AAF0D8;          // "tan"
constexpr uint32_t kASin = 0x15A3FA3D;         // "asin"
constexpr uint32_t kACos = 0x3CD99212;         // "acos"
constexpr uint32_t kATan = 0xD83566B0;         // "atan"
constexpr uint32_t kSqrt = 0xB86383A7;         // "sqrt"
constexpr uint32_t kNorm = 0x68C32A5F;         // "norm"
constexpr uint32_t kLength = 0xFE82614D;       // "length"
constexpr uint32_t kResult = 0xEC953EEC;       // "result"
constexpr uint32_t kFrom = 0x46E55E8F;         // "From"
constexpr uint32_t kTo = 0x28782D3B;           // "To"
constexpr uint32_t kValue = 0x08296457;        // (name unknown) LinearMap's input
constexpr uint32_t kLowerBound = 0x0EA9E0A8;   // "lowerbound"
constexpr uint32_t kUpperBound = 0xACAECE33;   // "upperbound"
constexpr uint32_t kMin = 0x5E84E22F;          // "min"
constexpr uint32_t kMax = 0x6289DD76;          // "max"
constexpr uint32_t kClamped = 0xCC419A08;      // "Clamped"
constexpr uint32_t kX = 0x7323E97C;            // "x"
constexpr uint32_t kY = 0x0424D9EA;            // "Y"
constexpr uint32_t kZ = 0x9D2D8850;            // "z"
constexpr uint32_t kRx = 0xFDF10F7A;           // "rx"
constexpr uint32_t kRy = 0x8AF63FEC;           // "ry"
constexpr uint32_t kRz = 0x13FF6E56;           // "rz"

float DegreeToRadianConv() { return g_degreesToRadians; } // pi / 180
float ConvertToDegrees() { return g_scriptRadiansToDegrees; } // 180 / pi

// The x87 instructions the originals use (53-bit precision), so the results match bit for bit.
double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
double X87Cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}
double X87Tan(double angle)
{
    double result;
    __asm {
        fld angle
        fptan
        fstp st(0)
        fstp result
    }
    return result;
}
double X87Atan(double value)
{
    double result;
    __asm {
        fld value
        fld1
        fpatan
        fstp result
    }
    return result;
}

// D3DX's matrix product (linked library code; stdcall): out = a * b.
void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The unnamed argument, in degrees for the trigonometry.
float Argument(Scripts::CStruct* params)
{
    float value = 0.0f;
    params->FetchFloat(0u, &value, 1);
    return value;
}

} // namespace

// 0x00448AE0: Sin <degrees>: sin = the sine.
bool __cdecl CFunc_Sin(Scripts::CStruct* params, Scripts::CScript* script)
{
    float angle = Argument(params);
    script->m_args->PushFloat(kSin, static_cast<float>(X87Sin(static_cast<double>(angle) * DegreeToRadianConv())));
    return true;
}

// 0x00448B20: Cos <degrees>: cos = the cosine.
bool __cdecl CFunc_Cos(Scripts::CStruct* params, Scripts::CScript* script)
{
    float angle = Argument(params);
    script->m_args->PushFloat(kCos, static_cast<float>(X87Cos(static_cast<double>(angle) * DegreeToRadianConv())));
    return true;
}

// 0x00448B60: Tan <degrees>: tan = the tangent.
bool __cdecl CFunc_Tan(Scripts::CStruct* params, Scripts::CScript* script)
{
    float angle = Argument(params);
    script->m_args->PushFloat(kTan, static_cast<float>(X87Tan(static_cast<double>(angle) * DegreeToRadianConv())));
    return true;
}

// 0x00448BA0: ASin <value>: asin = the arc sine in degrees (the C runtime's asin).
bool __cdecl CFunc_ASin(Scripts::CStruct* params, Scripts::CScript* script)
{
    float value = Argument(params);
    script->m_args->PushFloat(kASin, static_cast<float>(std::asin(static_cast<double>(value)) * ConvertToDegrees()));
    return true;
}

// 0x00448BE0: ACos <value>: acos = the arc cosine in degrees (the C runtime's acos).
bool __cdecl CFunc_ACos(Scripts::CStruct* params, Scripts::CScript* script)
{
    float value = Argument(params);
    script->m_args->PushFloat(kACos, static_cast<float>(std::acos(static_cast<double>(value)) * ConvertToDegrees()));
    return true;
}

// 0x00448C20: ATan <value>: atan = the arc tangent in degrees.
bool __cdecl CFunc_ATan(Scripts::CStruct* params, Scripts::CScript* script)
{
    float value = Argument(params);
    script->m_args->PushFloat(kATan, static_cast<float>(X87Atan(value) * ConvertToDegrees()));
    return true;
}

// 0x00448C60: Sqrt <value>: sqrt = the square root.
bool __cdecl CFunc_Sqrt(Scripts::CStruct* params, Scripts::CScript* script)
{
    float value = Argument(params);
    script->m_args->PushFloat(kSqrt, static_cast<float>(std::sqrt(static_cast<double>(value))));
    return true;
}

// 0x00448CA0: NormalizeVector <vector>: norm = the vector scaled to length 1 (all four components scaled; a
// zero vector as it is), length = its length (x, y, z).
bool __cdecl CFunc_NormalizeVector(Scripts::CStruct* params, Scripts::CScript* script)
{
    Scripts::VectorValue v = {};
    params->FetchVec(0u, &v, 1);
    // (x87: the sum from z down, unrounded; the length rounded to a float)
    double z2 = static_cast<double>(v.z) * v.z;
    double y2 = static_cast<double>(v.y) * v.y;
    double x2 = static_cast<double>(v.x) * v.x;
    float length = static_cast<float>(std::sqrt(z2 + y2 + x2));
    if (length != 0.0f) {
        double scale = 1.0 / length;
        v.x = static_cast<float>(v.x * scale);
        v.y = static_cast<float>(v.y * scale);
        v.z = static_cast<float>(v.z * scale);
        v.w = static_cast<float>(v.w * scale);
    }
    script->m_args->PutVec3(kNorm, v);
    script->m_args->PushFloat(kLength, length);
    return true;
}

// 0x00448D70: LinearMap result=<name> From=a To=b <value> lowerbound=lo upperbound=hi: <name> = where value
// lies between lo and hi (clamped to 0..1) mapped onto a..b.
bool __cdecl CFunc_LinearMap(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t result = 0;
    params->ReadHashValue(kResult, &result, 1);
    float from = 0.0f, to = 0.0f, value = 0.0f, lower = 0.0f, upper = 0.0f;
    params->FetchFloat(kFrom, &from, 1);
    params->FetchFloat(kTo, &to, 1);
    params->FetchFloat(kValue, &value, 1);
    params->FetchFloat(kLowerBound, &lower, 1);
    params->FetchFloat(kUpperBound, &upper, 1);
    // x87: the fraction stays unrounded (unless clamped)
    double fraction = (static_cast<double>(value) - lower) / (static_cast<double>(upper) - lower);
    if (fraction < 0.0)
        fraction = 0.0;
    else if (fraction > 1.0)
        fraction = 1.0;
    script->m_args->PushFloat(result, static_cast<float>((static_cast<double>(to) - from) * fraction + from));
    return true;
}

// 0x00448E60: Clamp <value> min=lo max=hi: Clamped = the value limited to max, then to min (so min wins when
// min > max).
bool __cdecl CFunc_Clamp(Scripts::CStruct* params, Scripts::CScript* script)
{
    float value = 0.0f, min = 0.0f, max = 0.0f;
    params->FetchFloat(0u, &value, 1);
    params->FetchFloat(kMin, &min, 1);
    params->FetchFloat(kMax, &max, 1);
    float clamped = value > max ? max : value;
    if (clamped < min)
        clamped = min;
    script->m_args->PushFloat(kClamped, clamped);
    return true;
}

// 0x00448EF0: RotateVector x=.. Y=.. z=.. [rx=degrees] [ry=..] [rz=..]: x, Y, z = the point (w = 1) turned about
// the X axis, then Y, then Z (each rotation applied in front: identity, then Rx * m, Ry * m, Rz * m).
bool __cdecl CFunc_RotateVector(Scripts::CStruct* params, Scripts::CScript* script)
{
    Maths::Vec4 point = {0.0f, 0.0f, 0.0f, 1.0f};
    params->FetchFloat(kX, &point.x, 1);
    params->FetchFloat(kY, &point.y, 1);
    params->FetchFloat(kZ, &point.z, 1);
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    params->FetchFloat(kRx, &rx, 0);
    params->FetchFloat(kRy, &ry, 0);
    params->FetchFloat(kRz, &rz, 0);

    Maths::Mat4 m;
    m.ResetToIdentity();
    Maths::Mat4 rotation;
    MatrixMultiply(&m, &Maths::MakePitchMatrix(rotation, rx * DegreeToRadianConv()), &m);
    MatrixMultiply(&m, &Maths::MakeYawMatrix(rotation, ry * DegreeToRadianConv()), &m);
    MatrixMultiply(&m, &Maths::MakeRollMatrix(rotation, rz * DegreeToRadianConv()), &m);
    Maths::Vec4 turned;
    m.Xform(turned, point);
    script->m_args->PushFloat(kX, turned.x);
    script->m_args->PushFloat(kY, turned.y);
    script->m_args->PushFloat(kZ, turned.z);
    return true;
}

} // namespace ScriptCommands
