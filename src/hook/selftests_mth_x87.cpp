// Self-tests for the maths functions whose originals work on the x87 FPU - src/game/mth/matrix.cpp, math.cpp,
// vector.cpp - compared bit for bit with ours (the older tests in selftest.cpp only within a tolerance). Those taking
// the FPU's sine / cosine (FSIN / FCOS): the rotation matrices about x / y / z / an axis number (0x00404DD0,
// 0x00404E20, 0x00404E70, 0x00404EC0) and the methods applying them, from Euler angles and back (0x004048D0,
// 0x00404AB0), the perspective matrix (0x004050F0), the 2D rotation (0x004058B0), the sine table (0x00403F70),
// IsAngleBeyond (0x00406560) and FastAtan2 (0x004049B0, which GetEulerAngles calls) - over special angles (zeros,
// multiples of pi, past 2^63 where FSIN / FCOS leave the angle as it is, infinities, NaN) and random floats of every
// size. And the determinant, adjugate, inverse and rotation axis / angle (0x004041E0, 0x004043A0, 0x00405600,
// 0x00405310), which keep their sums on the FPU, over random and near-180-degree rotation matrices.

#include <cmath>
#include <cstring>

#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using Mth::Matrix;
using Mth::Vector;
using namespace TestStandIns;

float FromBits(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

uint32_t ToBits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// The angles: special ones, then random bit patterns (every exponent, NaNs and infinities included).
struct Angles {
    static constexpr int kRandom = 20000;
    uint32_t seed = 0x2545F491;
    float Next()
    {
        seed = seed * 1664525u + 1013904223u;
        return FromBits(seed);
    }
};

const float kSpecial[] = {
    0.0f, -0.0f, 0.5f, -1.25f, 3.14159265f, -3.14159265f, 1.5707964f, 6.2831855f, 10.0f, 100.0f, 12345.678f, 1.0e10f,
    -3.0e15f, 9.2233720e18f, 1.0e19f, -1.0e30f, 3.4028235e38f, 1.0e-30f, 1.4e-45f, FromBits(0x7F800000),
    FromBits(0xFF800000), FromBits(0x7FC00000), FromBits(0xFFC00001)};

int g_mismatches;

// The C runtime's asin (_CIasin, 0x00623560: the argument and result on the FPU stack) - the game's C runtime isn't
// started offline: a stand-in at its address, for the original's calls and ours.
double __cdecl StandInAsinValue(double value) { return std::asin(value); }
__declspec(naked) void StandInAsin()
{
    __asm {
        sub esp, 8
        fstp qword ptr [esp]
        call StandInAsinValue
        add esp, 8
        ret
    }
}

bool Same(const void* a, const void* b, size_t size, const char* what, float angle)
{
    if (std::memcmp(a, b, size) == 0)
        return true;
    if (++g_mismatches <= 8) {
        const uint32_t* x = static_cast<const uint32_t*>(a);
        const uint32_t* y = static_cast<const uint32_t*>(b);
        for (size_t i = 0; i < size / 4; ++i) {
            if (x[i] != y[i]) {
                Log("  %s(%08X = %g): word %u original %08X ours %08X", what, ToBits(angle), angle,
                    static_cast<unsigned>(i), x[i], y[i]);
                break;
            }
        }
    }
    return false;
}

template <typename Visit>
void ForAngles(Visit visit)
{
    for (float angle : kSpecial)
        visit(angle);
    Angles random;
    for (int i = 0; i < Angles::kRandom; ++i)
        visit(random.Next());
}

void TestTrigRotations()
{
    using Rotate = Matrix*(__cdecl*)(Matrix*, float);
    using RotateAxis = Matrix*(__cdecl*)(Matrix*, int, float);
    using Rotate2D = void(__cdecl*)(float*, float);
    g_mismatches = 0;
    const struct {
        uint32_t address;
        Matrix& (*ours)(Matrix&, float);
        const char* name;
    } rotations[] = {{0x00404DD0, Mth::CreateRotateXMatrix, "CreateRotateXMatrix"},
                     {0x00404E20, Mth::CreateRotateYMatrix, "CreateRotateYMatrix"},
                     {0x00404E70, Mth::CreateRotateZMatrix, "CreateRotateZMatrix"}};
    bool ok = true;
    ForAngles([&](float angle) {
        for (const auto& r : rotations) {
            Matrix expected{}, actual{};
            Original<Rotate>(r.address)(&expected, angle);
            r.ours(actual, angle);
            ok &= Same(&expected, &actual, sizeof(Matrix), r.name, angle);
        }
        for (int axis = 0; axis < 3; ++axis) {
            Matrix expected{}, actual{};
            Original<RotateAxis>(0x00404EC0)(&expected, axis, angle);
            Mth::CreateRotateMatrix(actual, axis, angle);
            ok &= Same(&expected, &actual, sizeof(Matrix), "CreateRotateMatrix(axis index)", angle);
        }
        float expected2[4] = {}, actual2[4] = {};
        Original<Rotate2D>(0x004058B0)(expected2, angle);
        Mth::CreateRotate2D(actual2, angle);
        ok &= Same(expected2, actual2, sizeof(expected2), "CreateRotate2D", angle);
    });
    Check(ok, "the x / y / z / axis-number / 2D rotations, bit for bit");
}

void TestTrigEuler()
{
    using SetEuler = Matrix*(__thiscall*)(Matrix*, float, float, float);
    using GetEuler = Vector*(__thiscall*)(const Matrix*, Vector*);
    g_mismatches = 0;
    StandIns s;
    s.Bytes(0x00623560, Branch(0xE9, 0x00623560, reinterpret_cast<void*>(&StandInAsin)));
    bool set_ok = true, get_ok = true;
    Angles others;
    others.seed = 0x9E3779B9;
    ForAngles([&](float angle) {
        // (each angle as x, y and z in turn, with the others from small random angles)
        float small[2] = {others.Next(), others.Next()};
        for (float& s : small)
            s = static_cast<float>((ToBits(s) & 0xFFFF) / 65536.0 * 12.0 - 6.0);
        const float triples[3][3] = {
            {angle, small[0], small[1]}, {small[0], angle, small[1]}, {small[0], small[1], angle}};
        for (const auto& t : triples) {
            Matrix expected{}, actual{};
            Original<SetEuler>(0x004048D0)(&expected, t[0], t[1], t[2]);
            actual.SetFromEulerAngles(t[0], t[1], t[2]);
            set_ok &= Same(&expected, &actual, sizeof(Matrix), "SetFromEulerAngles", angle);

            // And back, from that matrix and from one with row 0's z out of [-1, 1] or the angle itself.
            for (int variant = 0; variant < 2; ++variant) {
                Matrix m = expected;
                if (variant == 1)
                    m.row[0].z = angle;
                Vector out_expected{9, 9, 9, 9}, out_actual{9, 9, 9, 9};
                Original<GetEuler>(0x00404AB0)(&m, &out_expected);
                m.GetEulerAngles(out_actual);
                get_ok &= Same(&out_expected, &out_actual, sizeof(Vector), "GetEulerAngles", angle);
            }
        }
    });
    Check(set_ok, "SetFromEulerAngles, bit for bit");
    Check(get_ok, "GetEulerAngles, bit for bit");
}

void TestTrigAtan2()
{
    using Atan2 = float(__cdecl*)(float, float);
    g_mismatches = 0;
    bool ok = true;
    Angles others;
    others.seed = 0x3C6EF372;
    ForAngles([&](float y) {
        // (x: random of every size, a small one, the same as y, its negative, zeros)
        const float xs[] = {others.Next(), static_cast<float>((ToBits(others.Next()) & 0xFFFF) / 4096.0 - 8.0), y, -y,
                            0.0f, -0.0f};
        for (float x : xs) {
            float expected = Original<Atan2>(0x004049B0)(y, x);
            float actual = Mth::FastAtan2(y, x);
            ok &= Same(&expected, &actual, 4, "FastAtan2", y);
        }
    });
    Check(ok, "FastAtan2, bit for bit");
}

void TestTrigOthers()
{
    using Perspective = Matrix*(__cdecl*)(Matrix*, float, float, float, float);
    using AngleBeyond = bool(__cdecl*)(const Vector*, const Vector*, float);
    using InitTable = void(__cdecl*)();
    g_mismatches = 0;
    bool perspective_ok = true, beyond_ok = true;
    Angles others;
    others.seed = 0x7F4A7C15;
    ForAngles([&](float angle) {
        const float shapes[][3] = {{1.3333f, 1.0f, 1000.0f}, {0.75f, 0.1f, 5000.0f}, {2.0f, -1.0f, 1.0f}};
        for (const auto& p : shapes) {
            Matrix expected{}, actual{};
            Original<Perspective>(0x004050F0)(&expected, angle, p[0], p[1], p[2]);
            Mth::CreatePerspectiveMatrix(actual, angle, p[0], p[1], p[2]);
            perspective_ok &= Same(&expected, &actual, sizeof(Matrix), "CreatePerspectiveMatrix", angle);
        }
        // IsAngleBeyond: `angle` as the limit in degrees, between random directions.
        Vector a{}, b{};
        for (float* f : {&a.x, &a.y, &a.z, &b.x, &b.y, &b.z})
            *f = static_cast<float>((ToBits(others.Next()) & 0xFFFF) / 32768.0 - 1.0);
        bool expected = Original<AngleBeyond>(0x00406560)(&a, &b, angle);
        bool actual = Mth::IsAngleBeyond(a, b, angle);
        if (expected != actual && ++g_mismatches <= 8)
            Log("  IsAngleBeyond(%08X = %g): original %d ours %d", ToBits(angle), angle, expected, actual);
        beyond_ok &= expected == actual;
    });
    Check(perspective_ok, "CreatePerspectiveMatrix, bit for bit");
    Check(beyond_ok, "IsAngleBeyond");

    // The sine table (0x00690FE0, 119 entries of 16 bytes): the original's, then ours, compared; ours left in.
    constexpr uint32_t kTable = 0x00690FE0, kBytes = 119 * 16;
    static uint8_t original_table[kBytes];
    Original<InitTable>(0x00403F70)();
    std::memcpy(original_table, reinterpret_cast<void*>(kTable), kBytes);
    std::memset(reinterpret_cast<void*>(kTable), 0xA5, kBytes);
    Mth::InitSinLookupTable();
    Check(std::memcmp(original_table, reinterpret_cast<void*>(kTable), kBytes) == 0, "InitSinLookupTable, bit for bit");
}

// The matrix methods that apply those rotations (each original builds the rotation and multiplies with the
// statically linked D3DXMatrixMultiply; ours with Multiply): on a general matrix, about x / y / z on either side and
// about an axis.
void TestTrigMatrixRotations()
{
    using RotateFn = Matrix*(__thiscall*)(Matrix*, float);
    using AxisFn = Matrix*(__thiscall*)(Matrix*, const Vector*, float);
    g_mismatches = 0;
    const struct {
        uint32_t address;
        Matrix& (Matrix::*ours)(float);
        const char* name;
    } rotations[] = {{0x0041A6A0, &Matrix::RotateX, "Matrix::RotateX"},
                     {0x0041A6D0, &Matrix::RotateY, "Matrix::RotateY"},
                     {0x0041A700, &Matrix::RotateZ, "Matrix::RotateZ"},
                     {0x00405670, &Matrix::RotateXLocal, "Matrix::RotateXLocal"},
                     {0x00405710, &Matrix::RotateYLocal, "Matrix::RotateYLocal"},
                     {0x004057B0, &Matrix::RotateZLocal, "Matrix::RotateZLocal"}};
    Matrix start;
    start.SetFromEulerAngles(0.3f, -1.1f, 2.0f);
    start.row[1].x += 0.25f;
    start.row[3] = {10.0f, -4.0f, 2.5f, 1.0f};
    const Vector axis{0.3f, -0.8f, 0.52f, 0.0f};
    bool ok = true;
    ForAngles([&](float angle) {
        for (const auto& r : rotations) {
            Matrix expected = start, actual = start;
            Original<RotateFn>(r.address)(&expected, angle);
            (actual.*r.ours)(angle);
            ok &= Same(&expected, &actual, sizeof(Matrix), r.name, angle);
        }
        Matrix expected = start, actual = start;
        Original<AxisFn>(0x00405950)(&expected, &axis, angle);
        actual.Rotate(axis, angle);
        ok &= Same(&expected, &actual, sizeof(Matrix), "Matrix::Rotate(axis)", angle);
        expected = start, actual = start;
        Original<AxisFn>(0x005772A0)(&expected, &axis, angle);
        actual.RotateLocal(axis, angle);
        ok &= Same(&expected, &actual, sizeof(Matrix), "Matrix::RotateLocal(axis)", angle);
    });
    Check(ok, "the matrix rotation methods, bit for bit");
}

// Random matrices: rotations (from Euler angles; near and at 180 degrees about an axis; with a NaN or an infinity in
// one element) and general ones.
template <typename Visit>
void ForMatrices(Visit visit)
{
    Angles random;
    random.seed = 0x6A09E667;
    auto small = [&] { return static_cast<float>((ToBits(random.Next()) & 0xFFFFFF) / 16777216.0 * 8.0 - 4.0); };
    for (int i = 0; i < 3000; ++i) {
        Matrix m;
        switch (i % 6) {
        case 0:
            m.SetFromEulerAngles(small(), small(), small());
            break;
        case 1: { // about an axis, near 180 degrees
            Vector axis{small(), small(), small(), 0.0f};
            Mth::CreateRotateMatrix(m, axis, 3.14159265f + small() * 1.0e-4f * (i % 3));
            break;
        }
        case 2:
            Mth::CreateRotateMatrix(m, i % 3, 3.14159265f - small() * 1.0e-6f);
            break;
        case 3: { // exactly 180 degrees about x / y / z or a diagonal (ties on the diagonal)
            const Vector diagonals[] = {{1, 1, 1, 0}, {0, 1, 1, 0}, {1, 0, 1, 0}, {1, 1, 0, 0}};
            if ((i / 6) % 2)
                Mth::CreateRotateMatrix(m, diagonals[(i / 12) % 4], 3.14159265f);
            else
                Mth::CreateRotateMatrix(m, i % 3, 3.14159265f);
            break;
        }
        case 4: // a rotation with a NaN or an infinity somewhere
            m.SetFromEulerAngles(small(), small(), small());
            (&m.row[0].x)[(i / 6) % 16] = kSpecial[19 + (i / 6) % 4];
            break;
        default:
            for (Vector& r : m.row)
                r = {small(), small(), small(), small()};
        }
        if (i % 8 == 5)
            m.row[3] = {small() * 100.0f, small(), small(), 1.0f};
        visit(m);
    }
}

void TestMatrixX87()
{
    using DetFn = double(__thiscall*)(const Matrix*);
    using AdjugateFn = Matrix*(__thiscall*)(Matrix*, const Matrix*);
    using InvertFn = Matrix*(__thiscall*)(Matrix*);
    using AxisAngleFn = void(__thiscall*)(const Matrix*, Vector*, float*);
    g_mismatches = 0;
    bool det_ok = true, adjugate_ok = true, invert_ok = true, axis_ok = true;
    ForMatrices([&](const Matrix& m) {
        double expected_det = Original<DetFn>(0x004041E0)(&m), actual_det = m.Determinant();
        det_ok &= Same(&expected_det, &actual_det, 8, "Matrix::Determinant", m.row[0].x);

        Matrix expected{}, actual{};
        Original<AdjugateFn>(0x004043A0)(&expected, &m);
        actual.Adjugate(m);
        adjugate_ok &= Same(&expected, &actual, sizeof(Matrix), "Matrix::Adjugate", m.row[0].x);

        expected = m, actual = m;
        Original<InvertFn>(0x00405600)(&expected);
        actual.Invert();
        invert_ok &= Same(&expected, &actual, sizeof(Matrix), "Matrix::Invert", m.row[0].x);

        Vector expected_axis{9, 9, 9, 9}, actual_axis{9, 9, 9, 9};
        float angles[2] = {-1, -1};
        Original<AxisAngleFn>(0x00405310)(&m, &expected_axis, &angles[0]);
        m.GetRotationAxisAngle(actual_axis, angles[1]);
        axis_ok &= Same(&expected_axis, &actual_axis, sizeof(Vector), "Matrix::GetRotationAxisAngle (axis)",
                        m.row[0].x) &
                   Same(&angles[0], &angles[1], 4, "Matrix::GetRotationAxisAngle (angle)", m.row[0].x);
    });
    Check(det_ok, "Matrix::Determinant, bit for bit");
    Check(adjugate_ok, "Matrix::Adjugate, bit for bit");
    Check(invert_ok, "Matrix::Invert, bit for bit");
    Check(axis_ok, "Matrix::GetRotationAxisAngle, bit for bit");
}

} // namespace

SELFTEST(Early, TestTrigRotations);
SELFTEST(Early, TestTrigMatrixRotations);
SELFTEST(Early, TestTrigEuler);
SELFTEST(Early, TestTrigAtan2);
SELFTEST(Early, TestMatrixX87);
SELFTEST(Early, TestTrigOthers);
