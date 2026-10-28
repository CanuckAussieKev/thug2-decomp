// The particle systems' vector helpers: see particle_maths.h. The maths follows the original's x87 code (53-bit
// precision: double where it keeps values in registers, float where it stores them).

#include "game/gfx/particle_maths.h"

#include <bit>

#include "hook/hook.h"

namespace Nx {

namespace {

float Store(double value) { return static_cast<float>(value); }

// A float passed through the x87 (FLD / FSTP): a signalling NaN comes out quiet.
float ThroughX87(float value)
{
    uint32_t bits = std::bit_cast<uint32_t>(value);
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu))
        bits |= 0x00400000u;
    return std::bit_cast<float>(bits);
}

// A component's value at the quadratic's turning point (t^2 coefficient `square`, t coefficient `linear`) - at 0
// unless `square` is positive.
double Lowest(float square, float linear, float start)
{
    double t = 0.0;
    if (square > 0.0f)
        t = static_cast<double>(linear) / square * -0.5;
    return (static_cast<double>(square) * t + linear) * t + start;
}

} // namespace

// 0x00494F10
Mth::Vector* __cdecl QuadraticLowest(Mth::Vector* out, const sQuadraticPoints* points)
{
    const Mth::Vector& a = points->start;
    const Mth::Vector& b = points->p1;
    const Mth::Vector& c = points->p2;
    double t1 = points->t1;
    double t2 = points->t2;
    // The end's offset scaled by t1, the mid's by t2 (z through a float each time, x through one, y kept).
    double cx = static_cast<double>(c.x) - a.x;
    double cy = static_cast<double>(c.y) - a.y;
    float cz = Store(static_cast<double>(c.z) - a.z);
    float cxT = Store(cx * t1);
    double cyT = cy * t1;
    double czT = static_cast<double>(cz) * t1;
    double bx = static_cast<double>(b.x) - a.x;
    double by = static_cast<double>(b.y) - a.y;
    float bz = Store(static_cast<double>(b.z) - a.z);
    float bxT = Store(bx * t2);
    double byT = by * t2;
    float bzT = Store(static_cast<double>(bz) * t2);
    float scale = Store(1.0 / ((t2 - t1) * t1 * t2));
    // The t^1 coefficients: the offsets scaled again (t1 / t2) and differenced.
    double cxTT = static_cast<double>(cxT) * t1;
    double cyTT = cyT * t1;
    float czTT = Store(czT * t1);
    double bxTT = static_cast<double>(bxT) * t2;
    float byTT = Store(byT * t2);
    float bzTT = Store(static_cast<double>(bzT) * t2);
    float dx = Store(bxTT - cxTT);
    double dy = static_cast<double>(byTT) - cyTT;
    double dz = static_cast<double>(bzTT) - czTT;
    float linearX = Store(static_cast<double>(dx) * scale);
    float linearY = Store(dy * scale);
    float linearZ = Store(dz * scale);
    // The t^2 coefficients: the once-scaled offsets differenced.
    float ex = Store(static_cast<double>(cxT) - bxT);
    float ey = Store(cyT - byT);
    double ez = czT - bzT;
    float squareX = Store(static_cast<double>(ex) * scale);
    float squareY = Store(static_cast<double>(ey) * scale);
    float squareZ = Store(ez * scale);
    out->w = 0.0f;
    double x = Lowest(squareX, linearX, a.x);
    double y = Lowest(squareY, linearY, a.y);
    double z = Lowest(squareZ, linearZ, a.z);
    out->x = Store(x);
    out->y = Store(y);
    out->z = Store(z);
    return out;
}

// The register adapter: out in ECX (kept, and EDX), the points on the stack (the caller pops them).
__declspec(naked) void QuadraticLowestStub()
{
    __asm {
        push ecx
        push edx
        lea eax, [esp + 12]
        push eax
        push ecx
        call QuadraticLowest
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// 0x00495130
Mth::Vector* __cdecl MinVector(Mth::Vector* out, const Mth::Vector* a, const Mth::Vector* b)
{
    Mth::Vector result = *a;
    if (b->x < a->x)
        result.x = b->x;
    if (b->y < a->y)
        result.y = b->y;
    if (b->z < a->z)
        result.z = b->z;
    result.w = ThroughX87(b->w < a->w ? b->w : a->w);
    *out = result;
    return out;
}

// 0x004951B0
Mth::Vector* __cdecl MaxVector(Mth::Vector* out, const Mth::Vector* a, const Mth::Vector* b)
{
    Mth::Vector result = *a;
    if (b->x > a->x)
        result.x = b->x;
    if (b->y > a->y)
        result.y = b->y;
    if (b->z > a->z)
        result.z = b->z;
    result.w = ThroughX87(b->w > a->w ? b->w : a->w);
    *out = result;
    return out;
}

// The register adapters: a in ECX, b in EDX, out in ESI.
__declspec(naked) void MinVectorStub()
{
    __asm {
        push edx
        push ecx
        push esi
        call MinVector
        add esp, 12
        ret
    }
}
__declspec(naked) void MaxVectorStub()
{
    __asm {
        push edx
        push ecx
        push esi
        call MaxVector
        add esp, 12
        ret
    }
}

} // namespace Nx

REPLACE_UNCHECKED(0x00494F10, Nx::QuadraticLowestStub);
REPLACE_UNCHECKED(0x00495130, Nx::MinVectorStub);
REPLACE_UNCHECKED(0x004951B0, Nx::MaxVectorStub);
