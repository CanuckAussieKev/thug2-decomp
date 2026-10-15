#include "hook/selftest.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

#include <initializer_list>
#include <new>

#include <windows.h>

#include "game/core/crc.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/str.h"
#include "game/core/string.h"
#include "game/file/file.h"
#include "game/file/pip.h"
#include "game/file/pre.h"
#include "game/lst/hashtable.h"
#include "game/mth/geometry.h"
#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/random.h"
#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "game/util/bitstream.h"
#include "game/util/lzss.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"

namespace {

void TestCrc()
{
    auto original = Original<uint32_t(__cdecl*)(const char*)>(0x00401B90);
    const char* inputs[] = {
        "",           "a",          "Wait",       "wait",      "GoalManager_GetActiveGoalId",
        "Skater/Board", "skater\\board", "levels\\nj\\nj.pre", "Mixed CASE with spaces 123",
        "\x80\xC0\xFF high bytes",
    };
    for (const char* s : inputs) {
        uint32_t expected = original(s);
        uint32_t actual = Crc::GenerateCRCFromString(s);
        if (expected != actual)
            Log("  crc(\"%s\"): original %08X, ours %08X", s, expected, actual);
        Check(expected == actual, "Crc::GenerateCRCFromString");
    }
    Check(original(nullptr) == Crc::GenerateCRCFromString(nullptr), "Crc::GenerateCRCFromString(nullptr)");

    auto extend = Original<uint32_t(__cdecl*)(uint32_t, const char*)>(0x00401BE0);
    auto buffer = Original<uint32_t(__cdecl*)(const void*, int)>(0x00401AA0);
    for (const char* s : inputs) {
        uint32_t seed = original("prefix");
        Check(extend(seed, s) == Crc::ExtendCRCWithString(seed, s), "Crc::ExtendCRCWithString");
        int length = static_cast<int>(strlen(s));
        Check(buffer(s, length) == Crc::GenerateCRCFromBuffer(s, length), "Crc::GenerateCRCFromBuffer");
    }
    Check(extend(0x1234, nullptr) == Crc::ExtendCRCWithString(0x1234, nullptr), "Crc::ExtendCRCWithString(null)");
    Check(buffer(nullptr, 4) == Crc::GenerateCRCFromBuffer(nullptr, 4), "Crc::GenerateCRCFromBuffer(null)");
    Check(buffer("abc", -1) == Crc::GenerateCRCFromBuffer("abc", -1), "Crc::GenerateCRCFromBuffer(-1)");

    // ExtendCRCWithBuffer: compare the returned CRC (its only output) from several starting values, including
    // the 0xFFFFFFFF the script code starts its chain with.
    auto extend_buffer = Original<uint32_t(__cdecl*)(const void*, int, uint32_t)>(0x00401AF0);
    for (const char* s : inputs) {
        int length = static_cast<int>(strlen(s));
        for (uint32_t start : {0xFFFFFFFFu, 0u, 0x12345678u}) {
            for (int size : {length, length / 2, 0, -3}) {
                uint32_t expected = extend_buffer(s, size, start);
                uint32_t actual = Crc::ExtendCRCWithBuffer(s, size, start);
                if (expected != actual)
                    Log("  ExtendCRCWithBuffer(\"%s\", %d, %08X): original %08X, ours %08X", s, size, start, expected,
                        actual);
                Check(expected == actual, "Crc::ExtendCRCWithBuffer");
            }
        }
    }
}

// The originals run on the x87 FPU and ours on SSE, so float results can differ in the last bits.
bool Near(float a, float b)
{
    return std::fabs(a - b) <= 1e-6f + 1e-5f * std::fmax(std::fabs(a), std::fabs(b));
}

bool Near(const Mth::Vector& a, const Mth::Vector& b)
{
    return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z) && Near(a.w, b.w);
}

void CheckVector(const Mth::Vector& expected, const Mth::Vector& actual, const char* what)
{
    if (!Near(expected, actual))
        Log("  %s: original (%g %g %g %g), ours (%g %g %g %g)", what, expected.x, expected.y, expected.z,
            expected.w, actual.x, actual.y, actual.z, actual.w);
    Check(Near(expected, actual), what);
}

void CheckFloat(float expected, float actual, const char* what)
{
    if (!Near(expected, actual))
        Log("  %s: original %g, ours %g", what, expected, actual);
    Check(Near(expected, actual), what);
}

// For results where float rounding is legitimately amplified (cancellation, acos near +/-1): compare with an
// absolute tolerance chosen from the size of the inputs, not of the result.
void CheckFloatWithin(float expected, float actual, float tolerance, const char* what)
{
    bool ok = std::fabs(expected - actual) <= tolerance;
    if (!ok)
        Log("  %s: original %g, ours %g (tolerance %g)", what, expected, actual, tolerance);
    Check(ok, what);
}

void CheckVectorWithin(const Mth::Vector& expected, const Mth::Vector& actual, float tolerance, const char* what)
{
    bool ok = std::fabs(expected.x - actual.x) <= tolerance && std::fabs(expected.y - actual.y) <= tolerance &&
              std::fabs(expected.z - actual.z) <= tolerance && std::fabs(expected.w - actual.w) <= tolerance;
    if (!ok)
        Log("  %s: original (%g %g %g %g), ours (%g %g %g %g) (tolerance %g)", what, expected.x, expected.y,
            expected.z, expected.w, actual.x, actual.y, actual.z, actual.w, tolerance);
    Check(ok, what);
}

void TestVector()
{
    using Mth::Vector;
    using Assign = Vector*(__thiscall*)(Vector*, const Vector*);
    using Scale = Vector*(__thiscall*)(Vector*, float);
    using Measure = float(__thiscall*)(const Vector*);
    using Normalize = Vector*(__thiscall*)(Vector*);
    using Binary = Vector*(__cdecl*)(Vector*, const Vector*, const Vector*);
    using VecScalar = Vector*(__cdecl*)(Vector*, const Vector*, float);
    using ScalarVec = Vector*(__cdecl*)(Vector*, float, const Vector*);
    using Unary = Vector*(__cdecl*)(Vector*, const Vector*);
    using Dot = float(__cdecl*)(const Vector*, const Vector*);
    using LineCtor = void*(__thiscall*)(void*, const Vector*, const Vector*);

    uint32_t seed = 12345;
    auto next = [&seed] {
        seed = seed * 1103515245u + 12345u;
        return static_cast<float>(static_cast<int>(seed >> 8) % 20001 - 10000) / 100.0f;
    };

    for (int i = 0; i < 32; ++i) {
        Vector a{next(), next(), next(), next()};
        Vector b{next(), next(), next(), next()};
        float s = next();
        if (i == 0)
            a = {0.0f, 0.0f, 0.0f, 1.0f}; // zero length: Normalize must leave it alone
        Vector expected{}, actual{};

        expected = a; Original<Assign>(0x00402320)(&expected, &b);
        actual = a; actual = b;
        CheckVector(expected, actual, "Vector::operator=");

        expected = a; Original<Assign>(0x00402340)(&expected, &b);
        actual = a; actual += b;
        CheckVector(expected, actual, "Vector::operator+=");

        expected = a; Original<Scale>(0x00402370)(&expected, s);
        actual = a; actual *= s;
        CheckVector(expected, actual, "Vector::operator*=");

        CheckFloat(Original<Measure>(0x004023A0)(&a), a.LengthSqr(), "Vector::LengthSqr");
        CheckFloat(Original<Measure>(0x004023C0)(&a), a.Length(), "Vector::Length");

        expected = a; Original<Normalize>(0x004023F0)(&expected);
        actual = a; actual.Normalize();
        CheckVector(expected, actual, "Vector::Normalize");

        Original<Binary>(0x00402440)(&expected, &a, &b);
        CheckVector(expected, a + b, "operator+(Vector, Vector)");
        Original<Binary>(0x00402470)(&expected, &a, &b);
        CheckVector(expected, a - b, "operator-(Vector, Vector)");
        Original<VecScalar>(0x004024A0)(&expected, &a, s);
        CheckVector(expected, a * s, "operator*(Vector, float)");
        Original<ScalarVec>(0x004024D0)(&expected, s, &a);
        CheckVector(expected, s * a, "operator*(float, Vector)");
        Original<Unary>(0x00402500)(&expected, &a);
        CheckVector(expected, -a, "operator-(Vector)");
        CheckFloat(Original<Dot>(0x00402530)(&a, &b), Mth::DotProduct(a, b), "DotProduct");
        Original<Binary>(0x00402550)(&expected, &a, &b);
        // Each component is a difference of two products of the inputs, which can cancel almost completely:
        // allow rounding relative to the size of those products.
        float product_scale = Mth::Vector{a.x, a.y, a.z, 0}.Length() * Mth::Vector{b.x, b.y, b.z, 0}.Length();
        CheckVectorWithin(expected, Mth::CrossProduct(a, b), 1e-6f * product_scale + 1e-6f, "CrossProduct");

        expected = a; Original<Assign>(0x00406050)(&expected, &b);
        actual = a; actual -= b;
        CheckVector(expected, actual, "Vector::operator-=");

        Vector n = b;
        n.Normalize();
        expected = a; Original<Assign>(0x00406080)(&expected, &n);
        actual = a; actual.ProjectToPlane(n);
        CheckVector(expected, actual, "Vector::ProjectToPlane");
        expected = a; Original<Assign>(0x00406080)(&expected, &expected);
        actual = a; actual.ProjectToPlane(actual);
        CheckVector(expected, actual, "Vector::ProjectToPlane (aliased)");

        alignas(16) unsigned char buffer[sizeof(Mth::Line)] = {};
        Original<LineCtor>(0x00402600)(buffer, &a, &b);
        Mth::Line line(a, b);
        const auto* original_line = reinterpret_cast<const Mth::Line*>(buffer);
        CheckVector(original_line->start, line.start, "Line::Line (start)");
        CheckVector(original_line->end, line.end, "Line::Line (end)");
    }
}

void TestMatrix()
{
    using Mth::Matrix;
    using Rotate = Matrix*(__cdecl*)(Matrix*, float);
    using Assign = Matrix*(__thiscall*)(Matrix*, const Matrix*);
    const float angles[] = {0.0f, 0.5f, -1.25f, 3.14159265f, 10.0f};
    const struct {
        uint32_t address;
        Matrix& (*ours)(Matrix&, float);
        const char* name;
    } rotations[] = {
        {0x00404DD0, Mth::CreateRotateXMatrix, "CreateRotateXMatrix"},
        {0x00404E20, Mth::CreateRotateYMatrix, "CreateRotateYMatrix"},
        {0x00404E70, Mth::CreateRotateZMatrix, "CreateRotateZMatrix"},
    };
    for (const auto& r : rotations) {
        for (float angle : angles) {
            Matrix expected{}, actual{};
            Original<Rotate>(r.address)(&expected, angle);
            r.ours(actual, angle);
            for (int i = 0; i < 4; ++i)
                CheckVector(expected.row[i], actual.row[i], r.name);
        }
    }

    Matrix source{}, expected{}, actual{};
    Mth::CreateRotateYMatrix(source, 0.7f);
    source.row[3] = {1.0f, 2.0f, 3.0f, 4.0f};
    Original<Assign>(0x00404090)(&expected, &source);
    actual = source;
    for (int i = 0; i < 4; ++i)
        CheckVector(expected.row[i], actual.row[i], "Matrix::operator=");
}

void TestBitStream()
{
    using Util::BitStream;
    using Ctor = BitStream*(__thiscall*)(BitStream*);
    using SetData = void(__thiscall*)(BitStream*, void*, int);
    using GetCount = int(__thiscall*)(const BitStream*);
    using Flush = void(__thiscall*)(BitStream*);

    auto same = [](const BitStream& a, const BitStream& b, bool ignore_start) {
        return a.m_cursor == b.m_cursor && (ignore_start || a.m_start == b.m_start) && a.m_buffer == b.m_buffer &&
               a.m_bitsLeft == b.m_bitsLeft && a.m_size == b.m_size && a.m_bitsUsed == b.m_bitsUsed;
    };
    auto garbage = [](BitStream& s) { std::memset(&s, 0xA5, sizeof(s)); };

    alignas(BitStream) unsigned char raw_expected[sizeof(BitStream)], raw_actual[sizeof(BitStream)];
    auto& expected = *reinterpret_cast<BitStream*>(raw_expected);
    auto& actual = *reinterpret_cast<BitStream*>(raw_actual);

    garbage(expected); garbage(actual);
    Original<Ctor>(0x00401050)(&expected);
    new (&actual) BitStream();
    Check(same(expected, actual, true), "BitStream::BitStream"); // original leaves m_start unset

    uint32_t data[4] = {};
    garbage(expected); garbage(actual);
    Original<SetData>(0x00401070)(&expected, data, 16);
    actual.SetInputData(data, 16);
    Check(same(expected, actual, false), "BitStream::SetInputData");

    garbage(expected); garbage(actual);
    Original<SetData>(0x00401090)(&expected, data, 16);
    actual.SetOutputData(data, 16);
    Check(same(expected, actual, false), "BitStream::SetOutputData");

    for (int bits : {0, 1, 7, 8, 9, 31, 32, 33}) {
        expected.m_bitsUsed = actual.m_bitsUsed = bits;
        Check(Original<GetCount>(0x004010C0)(&expected) == actual.GetByteCount(), "BitStream::GetByteCount");
    }

    for (int bits_left : {32, 20}) {
        uint32_t out_expected[2] = {}, out_actual[2] = {};
        expected.SetOutputData(out_expected, 8);
        actual.SetOutputData(out_actual, 8);
        expected.m_buffer = actual.m_buffer = 0x12345678;
        expected.m_bitsLeft = actual.m_bitsLeft = bits_left;
        Original<Flush>(0x00401180)(&expected);
        actual.Flush();
        Check(expected.m_cursor - out_expected == actual.m_cursor - out_actual && out_expected[0] == out_actual[0] &&
                  expected.m_buffer == actual.m_buffer && expected.m_bitsLeft == actual.m_bitsLeft,
              "BitStream::Flush");
    }
}

void TestStringsAndMath()
{
    using Convert = void(__cdecl*)(char*);
    const char* inputs[] = {"", "MiXeD Case 123", "levels\\NJ\\NJ.PRE", "@[`{ edges", "\xC0\xE0 high"};
    for (const char* s : inputs) {
        char expected[64], actual[64];
        strcpy_s(expected, s); strcpy_s(actual, s);
        Original<Convert>(0x00402080)(expected);
        Str::LowerCase(actual);
        Check(strcmp(expected, actual) == 0, "Str::LowerCase");
        strcpy_s(expected, s); strcpy_s(actual, s);
        Original<Convert>(0x004020B0)(expected);
        Str::UpperCase(actual);
        Check(strcmp(expected, actual) == 0, "Str::UpperCase");
    }

    using ClampFn = float(__cdecl*)(float, float, float);
    const float values[][3] = {{5, 0, 10}, {-1, 0, 10}, {11, 0, 10}, {0, 0, 0}, {5, 10, 0}, {-3.5f, -2, -1}};
    for (const auto& v : values)
        CheckFloat(Original<ClampFn>(0x004022C0)(v[0], v[1], v[2]), Mth::Clamp(v[0], v[1], v[2]), "Mth::Clamp");

    using Mth::Vector;
    using PointCtor = Vector*(__thiscall*)(Vector*, float, float, float);
    Vector expected{9, 9, 9, 9};
    Original<PointCtor>(0x004022F0)(&expected, 1.5f, -2.0f, 3.25f);
    CheckVector(expected, {1.5f, -2.0f, 3.25f, 1.0f}, "Vector(x, y, z)");

    using VectorIndex = float*(__thiscall*)(Vector*, int);
    using MatrixIndex = Vector*(__thiscall*)(Mth::Matrix*, int);
    Vector v{1, 2, 3, 4};
    Mth::Matrix m{};
    for (int i = 0; i < 4; ++i) {
        Check(Original<VectorIndex>(0x00402310)(&v, i) == &v[i], "Vector::operator[]");
        Check(Original<MatrixIndex>(0x00404080)(&m, i) == &m[i], "Matrix::operator[]");
    }
}

void TestBatch4()
{
    // Random streams: both versions share the game's state, so run each from the same seed and compare the
    // sequences, then put the game's state back.
    uint32_t saved[6];
    std::memcpy(saved, &GameVariable<uint32_t>(0x00690FC8), sizeof(saved));
    using Seed = void(__cdecl*)(uint32_t);
    using RndFn = int(__cdecl*)(int);
    using PomFn = float(__cdecl*)(float);
    for (uint32_t seed : {0u, 1u, 12345u, 0xDEADBEEFu}) {
        int expected_ints[24], actual_ints[24];
        float expected_floats[8], actual_floats[8];
        Original<Seed>(0x00403DA0)(seed);
        for (int i = 0; i < 8; ++i) {
            expected_ints[i] = Original<RndFn>(0x00403DE0)(100 + i);
            expected_ints[8 + i] = Original<RndFn>(0x00403E30)(7 + i * 1000);
            expected_ints[16 + i] = Original<RndFn>(0x00403DE0)(-50);
            expected_floats[i] = Original<PomFn>(0x00403E80)(2.5f * i);
        }
        Mth::InitialRand(seed);
        for (int i = 0; i < 8; ++i) {
            actual_ints[i] = Mth::Rnd(100 + i);
            actual_ints[8 + i] = Mth::Rnd2(7 + i * 1000);
            actual_ints[16 + i] = Mth::Rnd(-50);
            actual_floats[i] = Mth::PlusOrMinus(2.5f * i);
        }
        Check(std::memcmp(expected_ints, actual_ints, sizeof(expected_ints)) == 0, "Mth::Rnd / Rnd2 sequence");
        for (int i = 0; i < 8; ++i)
            CheckFloat(expected_floats[i], actual_floats[i], "Mth::PlusOrMinus");
    }
    std::memcpy(&GameVariable<uint32_t>(0x00690FC8), saved, sizeof(saved));

    using SignFn = float(__cdecl*)(float);
    for (float v : {-2.0f, -0.0f, 0.0f, 3.5f})
        CheckFloat(Original<SignFn>(0x00405FD0)(v), Mth::Sign(v), "Mth::Sign");

    using Mth::Vector;
    using Turn = Vector*(__thiscall*)(Vector*, int);
    using Convert = void(__thiscall*)(Vector*);
    const Vector source{1.5f, -2.0f, 3.25f, 4.0f};
    for (int turns = -1; turns <= 4; ++turns) {
        Vector expected = source, actual = source;
        Original<Turn>(0x00405EB0)(&expected, turns);
        actual.RotateY90(turns);
        CheckVector(expected, actual, "Vector::RotateY90");
    }
    const struct {
        uint32_t address;
        void (Vector::*ours)();
        const char* name;
    } conversions[] = {
        {0x00405EF0, &Vector::DegreesToRadians, "Vector::DegreesToRadians"},
        {0x00405F20, &Vector::RadiansToDegrees, "Vector::RadiansToDegrees"},
        {0x00405F50, &Vector::FeetToInches, "Vector::FeetToInches"},
        {0x00405F80, &Vector::NegateXRotateX90, "Vector::NegateXRotateX90"},
    };
    for (const auto& c : conversions) {
        Vector expected = source, actual = source;
        Original<Convert>(c.address)(&expected);
        (actual.*c.ours)();
        CheckVector(expected, actual, c.name);
    }

    using CaselessFn = uint32_t(__cdecl*)(const char*, int);
    const char text[] = "Levels/NJ/Skater_Board.PRE";
    for (int size : {-1, 0, 1, 5, static_cast<int>(sizeof(text)) - 1})
        Check(Original<CaselessFn>(0x00401B30)(text, size) == Crc::GenerateCRCCaseless(text, size),
              "Crc::GenerateCRCCaseless");
    Check(Original<CaselessFn>(0x00401B30)(nullptr, 4) == Crc::GenerateCRCCaseless(nullptr, 4),
          "Crc::GenerateCRCCaseless(null)");

    using ClearFn = void*(__cdecl*)(unsigned int, void*);
    for (unsigned int size : {0u, 1u, 3u, 4u, 7u, 13u}) {
        unsigned char expected[16], actual[16];
        std::memset(expected, 0xA5, sizeof(expected));
        std::memset(actual, 0xA5, sizeof(actual));
        void* r1 = Original<ClearFn>(0x00401C90)(size, expected + 1);
        void* r2 = Mem::Clear(size, actual + 1);
        Check(std::memcmp(expected, actual, sizeof(expected)) == 0 && r1 == expected + 1 && r2 == actual + 1,
              "Mem::Clear");
    }
    Check(Original<ClearFn>(0x00401C90)(8, nullptr) == Mem::Clear(8, nullptr), "Mem::Clear(null)");
}

void TestBatch5()
{
    using FindFn = const char*(__cdecl*)(const char*, const char*);
    const char* haystacks[] = {"", "Hello World", "levels\\NJ\\nj_Scripts.qb", "aaab", "abcABCabc"};
    const char* needles[] = {"", "world", "NJ_SCR", "aab", "cab", "x", "ABCABCABCD", "b"};
    for (const char* h : haystacks)
        for (const char* n : needles)
            Check(Original<FindFn>(0x00401FF0)(h, n) == Str::FindCaseless(h, n), "Str::FindCaseless");
}

void TestBatch6()
{
    using Mth::BBox;
    using Mth::Capsule;
    using Mth::Line;
    using Mth::Vector;
    using BBoxCtor = BBox*(__thiscall*)(void*);
    using BBoxAtCtor = BBox*(__thiscall*)(void*, const Vector*);
    using AddPointFn = void(__thiscall*)(BBox*, const Vector*);
    using CapsuleCtor = Capsule*(__thiscall*)(void*, const Vector*, const Vector*, float);
    using CapsuleLineCtor = Capsule*(__thiscall*)(void*, const Line*, float);
    using VolumeFn = float(__thiscall*)(const Capsule*);

    auto check_box = [](const BBox& expected, const BBox& actual, const char* what) {
        CheckVector(expected.min, actual.min, what);
        CheckVector(expected.max, actual.max, what);
    };

    alignas(16) unsigned char raw[sizeof(Capsule) > sizeof(BBox) ? sizeof(Capsule) : sizeof(BBox)];
    BBox* original_box = Original<BBoxCtor>(0x004033E0)(raw);
    check_box(*original_box, BBox(), "BBox::BBox()");

    const Vector points[] = {{1, 2, 3, 1}, {-4, 5, 0.5f, 1}, {2, -7, 9, 1}, {0, 0, 0, 1}};
    BBox expected_box = *original_box, actual_box;
    for (const Vector& p : points) {
        Original<AddPointFn>(0x00403450)(&expected_box, &p);
        actual_box.AddPoint(p);
        check_box(expected_box, actual_box, "BBox::AddPoint");
    }
    check_box(*Original<BBoxAtCtor>(0x00403410)(raw, &points[1]), BBox(points[1]), "BBox::BBox(point)");

    const Vector start{1, 2, 3, 1}, offset{4, -5, 6, 0};
    for (float radius : {0.0f, 0.5f, 12.0f}) {
        const Capsule& a = *Original<CapsuleCtor>(0x00403230)(raw, &start, &offset, radius);
        Capsule b(start, offset, radius);
        CheckVector(a.line.start, b.line.start, "Capsule(start, offset, radius)");
        CheckVector(a.line.end, b.line.end, "Capsule(start, offset, radius)");
        CheckFloat(a.radius, b.radius, "Capsule(start, offset, radius)");
        CheckFloat(Original<VolumeFn>(0x00403390)(&b), b.Volume(), "Capsule::Volume");

        Line line(start, offset);
        const Capsule& c = *Original<CapsuleLineCtor>(0x004032A0)(raw, &line, radius);
        Capsule d(line, radius);
        CheckVector(c.line.start, d.line.start, "Capsule(line, radius)");
        CheckVector(c.line.end, d.line.end, "Capsule(line, radius)");
        CheckFloat(c.radius, d.radius, "Capsule(line, radius)");
    }
}

void TestBatch7()
{
    using Unary = float(__cdecl*)(float);
    for (float v : {-1.0f, -0.75f, -0.3f, 0.0f, 0.2f, 0.5f, 0.9f, 1.0f})
        CheckFloat(Original<Unary>(0x00403EF0)(v), Mth::FastACos(v), "Mth::FastACos");

    using KeepFn = float(__cdecl*)(float, float, float);
    const float cases[][3] = {{10, 0, 3}, {10, 0, -3}, {1, 0, 3}, {-10, 0, 3}, {-1, 0, 3}, {5, 5, 1}, {2, 8, 0}};
    for (const auto& c : cases)
        CheckFloat(Original<KeepFn>(0x00404010)(c[0], c[1], c[2]), Mth::KeepWithin(c[0], c[1], c[2]),
                   "Mth::KeepWithin");

    using Rot2DFn = void(__cdecl*)(float*, float);
    for (float angle : {0.0f, 0.4f, -2.0f}) {
        float expected[4], actual[4];
        Original<Rot2DFn>(0x004058B0)(expected, angle);
        Mth::CreateRotate2D(actual, angle);
        for (int i = 0; i < 4; ++i)
            CheckFloat(expected[i], actual[i], "Mth::CreateRotate2D");
    }

    // The lookup table lives in the exe's globals: save it, compare both versions' output, restore it.
    constexpr uintptr_t kTable = 0x00690FE0;
    constexpr size_t kTableBytes = 119 * 16;
    static unsigned char saved[kTableBytes], expected_table[kTableBytes];
    std::memcpy(saved, reinterpret_cast<void*>(kTable), kTableBytes);
    Original<void(__cdecl*)()>(0x00403F70)();
    std::memcpy(expected_table, reinterpret_cast<void*>(kTable), kTableBytes);
    Mth::InitSinLookupTable();
    const auto* e = reinterpret_cast<const float*>(expected_table);
    const auto* a = reinterpret_cast<const float*>(kTable);
    for (size_t i = 0; i < kTableBytes / 4; ++i)
        CheckFloat(e[i], a[i], "Mth::InitSinLookupTable");
    std::memcpy(reinterpret_cast<void*>(kTable), saved, kTableBytes);

    using Mth::Matrix;
    using RotateLocal = Matrix*(__thiscall*)(Matrix*, float);
    const struct {
        uint32_t address;
        Matrix& (Matrix::*ours)(float);
        const char* name;
    } rotations[] = {
        {0x00405670, &Matrix::RotateXLocal, "Matrix::RotateXLocal"},
        {0x00405710, &Matrix::RotateYLocal, "Matrix::RotateYLocal"},
        {0x004057B0, &Matrix::RotateZLocal, "Matrix::RotateZLocal"},
    };
    Matrix start;
    Mth::CreateRotateYMatrix(start, 0.3f);
    start.row[1].x += 0.25f;
    start.row[3] = {10.0f, -4.0f, 2.5f, 1.0f};
    for (const auto& r : rotations) {
        for (float angle : {0.0f, 0.7f, -1.9f}) {
            Matrix expected = start, actual = start;
            Original<RotateLocal>(r.address)(&expected, angle);
            (actual.*r.ours)(angle);
            for (int i = 0; i < 4; ++i)
                CheckVector(expected.row[i], actual.row[i], r.name);
        }
    }
}

void TestBitStreamReadWrite()
{
    using Util::BitStream;
    using WriteFn = void(__thiscall*)(BitStream*, uint32_t, int);
    using ReadFn = uint32_t(__thiscall*)(BitStream*, int);
    using ReadSignedFn = int32_t(__thiscall*)(BitStream*, int);

    // A mix of widths, including word-straddling writes and full 32-bit values.
    const struct {
        uint32_t value;
        int bits;
    } items[] = {{1, 1},  {0x5, 3},        {0x7F, 7},       {0x1234, 16}, {0xABCDE, 20}, {0, 0},
                 {3, 2},  {0xDEADBEEF, 32}, {0x15, 5},       {0x3FFFF, 18}, {0xFFFFFFFF, 32}, {0x2, 2},
                 {0x1, 9}, {0x7FFFFFFF, 31}, {0x80000000, 32}, {0x55, 8}};

    uint32_t data_expected[16] = {}, data_actual[16] = {};
    BitStream expected, actual;
    expected.SetOutputData(data_expected, sizeof(data_expected));
    actual.SetOutputData(data_actual, sizeof(data_actual));
    for (const auto& item : items) {
        Original<WriteFn>(0x004010D0)(&expected, item.value, item.bits);
        actual.WriteBits(item.value, item.bits);
        Check(expected.m_buffer == actual.m_buffer && expected.m_bitsLeft == actual.m_bitsLeft &&
                  expected.m_bitsUsed == actual.m_bitsUsed &&
                  expected.m_cursor - data_expected == actual.m_cursor - data_actual,
              "BitStream::WriteBits (state)");
    }
    expected.Flush();
    actual.Flush();
    Check(std::memcmp(data_expected, data_actual, sizeof(data_expected)) == 0, "BitStream::WriteBits (data)");

    expected.SetInputData(data_expected, sizeof(data_expected));
    actual.SetInputData(data_expected, sizeof(data_expected));
    for (const auto& item : items) {
        uint32_t a = Original<ReadFn>(0x004011B0)(&expected, item.bits);
        uint32_t b = actual.ReadBits(item.bits);
        Check(a == b && expected.m_buffer == actual.m_buffer && expected.m_bitsLeft == actual.m_bitsLeft,
              "BitStream::ReadBits");
    }

    expected.SetInputData(data_expected, sizeof(data_expected));
    actual.SetInputData(data_expected, sizeof(data_expected));
    for (const auto& item : items) {
        if (item.bits == 0)
            continue; // the signed read of 0 bits reads bit -1 (mod 32): defined, but skip the oddity
        int32_t a = Original<ReadSignedFn>(0x00401260)(&expected, item.bits);
        int32_t b = actual.ReadSignedBits(item.bits);
        Check(a == b, "BitStream::ReadSignedBits");
    }
}

void TestBatch9()
{
    using Mth::Matrix;
    using Mth::Vector;
    using ScaleFn = Matrix*(__thiscall*)(Matrix*, float);
    using EulerFn = Matrix*(__thiscall*)(Matrix*, float, float, float);
    using AxisFn = Matrix*(__cdecl*)(Matrix*, const Vector*, float);
    using RotateFn = Matrix*(__thiscall*)(Matrix*, const Vector*, float);

    auto check_matrix = [](const Matrix& expected, const Matrix& actual, const char* what) {
        for (int i = 0; i < 4; ++i)
            CheckVector(expected.row[i], actual.row[i], what);
    };

    Matrix start;
    Mth::CreateRotateYMatrix(start, 0.6f);
    start.row[3] = {3.0f, -1.0f, 7.0f, 1.0f};

    for (float scale : {1.0f, 2.5f, -0.5f, 0.0f}) {
        Matrix expected = start, actual = start;
        Original<ScaleFn>(0x00404120)(&expected, scale);
        actual *= scale;
        check_matrix(expected, actual, "Matrix::operator*=(float)");
    }

    const float angles[][3] = {{0, 0, 0}, {0.5f, 0, 0}, {0, 0.5f, 0}, {0, 0, 0.5f}, {0.3f, -1.1f, 2.0f}};
    for (const auto& a : angles) {
        Matrix expected{}, actual{};
        Original<EulerFn>(0x004048D0)(&expected, a[0], a[1], a[2]);
        actual.SetFromEulerAngles(a[0], a[1], a[2]);
        check_matrix(expected, actual, "Matrix::SetFromEulerAngles");
    }

    const Vector axes[] = {{1, 0, 0, 0}, {0, 2, 0, 0}, {1, 1, 1, 0}, {-0.3f, 0.8f, 0.2f, 0}, {0, 0, 0, 0}};
    for (const Vector& axis : axes) {
        for (float angle : {0.0f, 0.9f, -2.4f}) {
            Matrix expected{}, actual{};
            Original<AxisFn>(0x00404C60)(&expected, &axis, angle);
            Mth::CreateRotateMatrix(actual, axis, angle);
            check_matrix(expected, actual, "Mth::CreateRotateMatrix");

            expected = start;
            actual = start;
            Original<RotateFn>(0x00405950)(&expected, &axis, angle);
            actual.Rotate(axis, angle);
            check_matrix(expected, actual, "Matrix::Rotate");
        }
    }
}

void TestBatch10()
{
    using Mth::Matrix;
    using Mth::Vector;
    using DetFn = float(__thiscall*)(const Matrix*);
    using AdjFn = Matrix*(__thiscall*)(Matrix*, const Matrix*);
    using InvFn = Matrix*(__thiscall*)(Matrix*);
    using EulerFn = Vector*(__thiscall*)(const Matrix*, Vector*);
    using Atan2Fn = float(__cdecl*)(float, float);

    auto check_matrix = [](const Matrix& expected, const Matrix& actual, const char* what) {
        for (int i = 0; i < 4; ++i)
            CheckVector(expected.row[i], actual.row[i], what);
    };

    // Rotation + translation, a scaled one, and a general (non-orthogonal) one.
    Matrix samples[3];
    samples[0].SetFromEulerAngles(0.4f, -0.9f, 1.3f);
    samples[0].row[3] = {5.0f, -2.0f, 8.0f, 1.0f};
    samples[1].SetFromEulerAngles(-1.1f, 0.2f, 0.7f);
    samples[1] *= 2.5f;
    samples[1].row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    samples[2].row[0] = {2.0f, 0.5f, -1.0f, 0.0f};
    samples[2].row[1] = {0.3f, 3.0f, 0.2f, 0.1f};
    samples[2].row[2] = {-0.4f, 1.0f, 4.0f, 0.0f};
    samples[2].row[3] = {1.0f, 2.0f, 3.0f, 1.0f};

    for (const Matrix& m : samples) {
        CheckFloat(Original<DetFn>(0x004041E0)(&m), m.Determinant(), "Matrix::Determinant");

        Matrix expected{}, actual{};
        Original<AdjFn>(0x004043A0)(&expected, &m);
        actual.Adjugate(m);
        check_matrix(expected, actual, "Matrix::Adjugate");

        expected = m;
        actual = m;
        Original<InvFn>(0x00405600)(&expected);
        actual.Invert();
        check_matrix(expected, actual, "Matrix::Invert");

        Vector angles_expected{}, angles_actual{};
        Original<EulerFn>(0x00404AB0)(&m, &angles_expected);
        m.GetEulerAngles(angles_actual);
        CheckVector(angles_expected, angles_actual, "Matrix::GetEulerAngles");
    }

    // Gimbal lock (Y = +/-90 degrees) takes the other branch of GetEulerAngles.
    for (float y : {1.5707963f, -1.5707963f}) {
        Matrix m;
        m.SetFromEulerAngles(0.3f, y, -0.6f);
        Vector angles_expected{}, angles_actual{};
        Original<EulerFn>(0x00404AB0)(&m, &angles_expected);
        m.GetEulerAngles(angles_actual);
        CheckVector(angles_expected, angles_actual, "Matrix::GetEulerAngles (gimbal lock)");
    }

    const float points[][2] = {{1, 2},  {2, 1},   {-1, 2},  {-2, 1},   {-1, -2}, {-2, -1}, {1, -2},
                               {2, -1}, {0, 1},   {0, -1},  {1, 0},    {-1, 0},  {3, 3},   {-3, 3},
                               {0.001f, 5}, {5, 0.001f}, {-0.5f, -0.5f}};
    for (const auto& p : points)
        CheckFloat(Original<Atan2Fn>(0x004049B0)(p[0], p[1]), Mth::FastAtan2(p[0], p[1]), "Mth::FastAtan2");
}

void TestBatch11()
{
    using Mth::Capsule;
    using Mth::Line;
    using Mth::Vector;
    using DistFn = float(__thiscall*)(const Line*, const Vector*, Vector*);
    using ClosestFn = bool(__cdecl*)(const Line*, const Line*, Vector*, Vector*, float*, float*, bool);
    using ContainsFn = bool(__thiscall*)(const Capsule*, const Vector*, Vector*);

    const Line line({1, 2, 3, 1}, {5, -2, 7, 3});
    const Vector points[] = {{0, 0, 0, 1}, {1, 2, 3, 1}, {3, 0, 5, 2}, {9, -6, 11, 1}, {2, 5, -1, 0}, {5, -2, 7, 3}};
    for (const Vector& p : points) {
        Vector expected_offset{}, actual_offset{};
        float expected = Original<DistFn>(0x00402D60)(&line, &p, &expected_offset);
        float actual = line.DistanceSqrToPoint(p, &actual_offset);
        CheckFloat(expected, actual, "Line::DistanceSqrToPoint");
        CheckVector(expected_offset, actual_offset, "Line::DistanceSqrToPoint (offset)");
        CheckFloat(Original<DistFn>(0x00402D60)(&line, &p, nullptr), line.DistanceSqrToPoint(p),
                   "Line::DistanceSqrToPoint (no offset)");
    }

    const Line others[] = {
        Line({0, 0, 0, 1}, {0, 10, 0, 1}),       // skew
        Line({-3, 4, 1, 1}, {6, 1, 9, 2}),       // general
        Line({2, 2, 4, 1}, {6, -2, 8, 1}),       // parallel to `line`
        Line({1, 1, 1, 1}, {1, 1, 1, 1}),        // degenerate (a point)
        Line({20, 20, 20, 1}, {21, 25, 19, 1}),  // far away: clamping matters
    };
    for (const Line& other : others) {
        for (bool clamp : {false, true}) {
            Vector ea{9, 9, 9, 9}, eb{9, 9, 9, 9}, aa{9, 9, 9, 9}, ab{9, 9, 9, 9};
            float es = 7, et = 7, as = 7, at = 7;
            bool expected = Original<ClosestFn>(0x00402F00)(&line, &other, &ea, &eb, &es, &et, clamp);
            bool actual = Mth::ClosestPointsBetweenLines(line, other, aa, ab, as, at, clamp);
            Check(expected == actual, "ClosestPointsBetweenLines (result)");
            CheckVector(ea, aa, "ClosestPointsBetweenLines (point a)");
            CheckVector(eb, ab, "ClosestPointsBetweenLines (point b)");
            CheckFloat(es, as, "ClosestPointsBetweenLines (s)");
            CheckFloat(et, at, "ClosestPointsBetweenLines (t)");
        }
    }

    const Capsule capsule(line, 1.5f);
    for (const Vector& p : points) {
        Vector expected_offset{8, 8, 8, 8}, actual_offset{8, 8, 8, 8};
        bool expected = Original<ContainsFn>(0x00403330)(&capsule, &p, &expected_offset);
        bool actual = capsule.ContainsPoint(p, &actual_offset);
        Check(expected == actual, "Capsule::ContainsPoint");
        CheckVector(expected_offset, actual_offset, "Capsule::ContainsPoint (offset)");
    }
}

void TestBatch12()
{
    using Mth::Capsule;
    using Mth::Line;
    using Mth::Vector;
    using SegFn = float(__thiscall*)(const Line*, const Line*, float*, float*);
    using CapsuleFn = int(__thiscall*)(const Capsule*, const Line*, float*, float*, float*);

    uint32_t seed = 4242;
    auto next = [&seed] {
        seed = seed * 1103515245u + 12345u;
        return static_cast<float>(static_cast<int>(seed >> 8) % 2001 - 1000) / 100.0f;
    };

    // Random pairs, plus constructed parallel / anti-parallel / collinear / point-like ones.
    struct Pair {
        Vector a_start, a_end, b_start, b_end;
    };
    static Pair pairs[220];
    int count = 0;
    for (; count < 200; ++count)
        pairs[count] = {{next(), next(), next(), 1}, {next(), next(), next(), 1},
                        {next(), next(), next(), 1}, {next(), next(), next(), 1}};
    const Vector base_start{0, 0, 0, 1}, base_end{4, 2, 0, 1};
    const Vector shifts[] = {{0, 0, 3, 0}, {-6, -3, 1, 0}, {6, 3, -1, 0}, {2, 1, 0, 0}, {0, 0, 0, 0}};
    for (const Vector& shift : shifts) {
        pairs[count++] = {base_start, base_end, base_start + shift, base_end + shift}; // parallel
        pairs[count++] = {base_start, base_end, base_end + shift, base_start + shift}; // anti-parallel
    }
    const Vector point{1, 1, 1, 1};
    pairs[count++] = {base_start, base_end, point, point}; // second is a point
    pairs[count++] = {point, point, base_start, base_end}; // first is a point

    for (int i = 0; i < count; ++i) {
        const Line a(pairs[i].a_start, pairs[i].a_end);
        const Line b(pairs[i].b_start, pairs[i].b_end);
        float es = -7, et = -7, as = -7, at = -7;
        float expected = Original<SegFn>(0x00402640)(&a, &b, &es, &et);
        float actual = a.DistanceSqrToLine(b, &as, &at);
        // Tolerance scaled to the input size: the result is a sum of large terms that mostly cancel.
        float scale = 1.0f + (a.start - b.start).LengthSqr() + (a.end - a.start).LengthSqr() +
                      (b.end - b.start).LengthSqr();
        bool ok = std::fabs(expected - actual) <= 2e-5f * scale && std::fabs(es - as) <= 1e-3f &&
                  std::fabs(et - at) <= 1e-3f;
        if (!ok)
            Log("  segment pair %d: original %g (s %g, t %g), ours %g (s %g, t %g)", i, expected, es, et, actual, as,
                at);
        Check(ok, "Line::DistanceSqrToLine");
    }

    const Capsule capsule(Line(base_start, base_end), 1.25f);
    for (int i = 0; i < count; i += 7) {
        const Line segment(pairs[i].b_start, pairs[i].b_end);
        float es = 0, et = 0, ed = 0, as = 0, at = 0, ad = 0;
        int expected = Original<CapsuleFn>(0x004032E0)(&capsule, &segment, &es, &et, &ed);
        int actual = capsule.IntersectsSegment(segment, &as, &at, &ad);
        Check(expected == actual, "Capsule::IntersectsSegment");
    }
}

void TestBatch13()
{
    using Mth::BBox;
    using Mth::Line;
    using Mth::Vector;
    using ContainsFn = int(__thiscall*)(const BBox*, const Vector*);
    using CountFn = int(__thiscall*)(const BBox*, const Vector*, uint32_t*);
    using IntersectFn = bool(__thiscall*)(const BBox*, const Line*, Vector*, Vector*);
    using ClosestFn = void(__thiscall*)(const BBox*, const Vector*, Vector*);

    BBox box;
    box.min = {-2.0f, -1.0f, -3.0f, 1.0f};
    box.max = {4.0f, 5.0f, 2.0f, 1.0f};

    uint32_t seed = 777;
    auto next = [&seed] {
        seed = seed * 1103515245u + 12345u;
        return static_cast<float>(static_cast<int>(seed >> 8) % 1601 - 800) / 100.0f;
    };

    // Random points, plus ones exactly on the faces and corners.
    Vector points[120];
    int count = 0;
    for (; count < 100; ++count)
        points[count] = {next(), next(), next(), 0.5f};
    const Vector special[] = {{4, 5, 2, 1}, {-2, -1, -3, 1}, {4, 0, 0, 1}, {0, 5, 0, 1}, {0, 0, -3, 1},
                              {1, 2, -0.5f, 1}, {3.9f, 2, 0, 1}, {1, -0.9f, 0, 1}, {1, 2, 1.95f, 1}};
    for (const Vector& p : special)
        points[count++] = p;

    for (int i = 0; i < count; ++i) {
        const Vector& p = points[i];
        Check(Original<ContainsFn>(0x004025A0)(&box, &p) == box.ContainsPoint(p), "BBox::ContainsPoint");

        uint32_t expected_axes = 99, actual_axes = 77;
        int expected = Original<CountFn>(0x004034D0)(&box, &p, &expected_axes);
        int actual = box.CountAxesContaining(p, actual_axes);
        Check(expected == actual && expected_axes == actual_axes, "BBox::CountAxesContaining");

        Vector expected_point{9, 9, 9, 9}, actual_point{9, 9, 9, 9};
        Original<ClosestFn>(0x00403C00)(&box, &p, &expected_point);
        box.GetClosestSurfacePoint(p, actual_point);
        CheckVector(expected_point, actual_point, "BBox::GetClosestSurfacePoint");
    }

    // Segments between random points (many cross faces), plus one aimed at each face.
    const Vector centre{1, 2, -0.5f, 1};
    const Vector outside[] = {{10, 2, 0, 1}, {-10, 2, 0, 1}, {1, 12, 0, 1}, {1, -12, 0, 1}, {1, 2, 9, 1}, {1, 2, -9, 1}};
    for (int i = 0; i + 1 < count + 6; i += 2) {
        Vector a = i < count ? points[i] : outside[(i - count) % 6];
        Vector b = i + 1 < count ? points[i + 1] : centre;
        for (int pass = 0; pass < 2; ++pass) {
            const Line line = pass == 0 ? Line(a, b) : Line(b, a);
            Vector eh{9, 9, 9, 9}, en{9, 9, 9, 9}, ah{9, 9, 9, 9}, an{9, 9, 9, 9};
            bool expected = Original<IntersectFn>(0x00403550)(&box, &line, &eh, &en);
            bool actual = box.LineIntersect(line, ah, an);
            Check(expected == actual, "BBox::LineIntersect (result)");
            CheckVector(eh, ah, "BBox::LineIntersect (hit)");
            CheckVector(en, an, "BBox::LineIntersect (normal)");
        }
    }
    for (const Vector& o : outside) {
        const Line line(o, centre);
        Vector eh{}, en{}, ah{}, an{};
        bool expected = Original<IntersectFn>(0x00403550)(&box, &line, &eh, &en);
        bool actual = box.LineIntersect(line, ah, an);
        Check(expected && actual, "BBox::LineIntersect (face-aimed)");
        CheckVector(eh, ah, "BBox::LineIntersect (face-aimed hit)");
        CheckVector(en, an, "BBox::LineIntersect (face-aimed normal)");
    }
}

void TestRotateToPlane()
{
    using Mth::Vector;
    using RotateFn = Vector*(__thiscall*)(Vector*, const Vector*);
    const Vector normals[] = {{0, 1, 0, 0}, {1, 0, 0, 0}, {0.6f, 0.8f, 0, 0}, {0.2f, -0.4f, 0.894427f, 0}};
    const Vector vectors[] = {{3, 4, 5, 1}, {0, 2, 0, 1}, {-1, 0.5f, 2, 0}, {0.6f, 0.8f, 0, 0}, {0, 0, 0, 1}};
    for (const Vector& n : normals) {
        for (const Vector& v : vectors) {
            // Known, accepted difference: a vector lying along the normal projects to (almost) zero. Ours gets
            // exactly zero and uses the fallback direction; the original's extra x87 precision can leave
            // rounding noise, which it then normalises into an arbitrary direction. Nothing to match there.
            if (Mth::CrossProduct(v, n).Length() < 1e-5f * v.Length())
                continue;
            Vector expected = v, actual = v;
            Original<RotateFn>(0x004060F0)(&expected, &n);
            actual.RotateToPlane(n);
            CheckVector(expected, actual, "Vector::RotateToPlane");
        }
    }
}

void TestBatch14()
{
    using Mth::BBox;
    using Mth::Matrix;
    using Mth::Vector;
    using CenterFn = Vector*(__thiscall*)(const BBox*, Vector*);
    using AxisFn = Matrix*(__cdecl*)(Matrix*, int, float);
    using LookAtFn = Matrix*(__cdecl*)(Matrix*, const Vector*, const Vector*, const Vector*);
    using ProjFn = Matrix*(__cdecl*)(Matrix*, float, float, float, float);

    auto check_matrix = [](const Matrix& expected, const Matrix& actual, const char* what) {
        for (int i = 0; i < 4; ++i)
            CheckVector(expected.row[i], actual.row[i], what);
    };

    BBox box;
    box.min = {-2.0f, 1.0f, -7.5f, 1.0f};
    box.max = {6.0f, 3.0f, 0.5f, 1.0f};
    Vector expected_center{9, 9, 9, 9}, actual_center{9, 9, 9, 9};
    Original<CenterFn>(0x00403BB0)(&box, &expected_center);
    box.GetCenter(actual_center);
    CheckVector(expected_center, actual_center, "BBox::GetCenter");

    for (int axis = 0; axis < 3; ++axis) {
        for (float angle : {0.0f, 0.8f, -2.2f}) {
            Matrix expected{}, actual{};
            Original<AxisFn>(0x00404EC0)(&expected, axis, angle);
            Mth::CreateRotateMatrix(actual, axis, angle);
            check_matrix(expected, actual, "Mth::CreateRotateMatrix(axis index)");
        }
    }

    const Vector eyes[] = {{0, 5, -10, 1}, {3, 2, 1, 1}, {-4, 0, 8, 1}};
    const Vector targets[] = {{0, 0, 0, 1}, {3, 2, 1, 1}, {10, -3, 2, 1}};
    const Vector up{0, 1, 0, 0};
    for (const Vector& eye : eyes) {
        for (const Vector& target : targets) {
            Matrix expected{}, actual{};
            Original<LookAtFn>(0x00404F50)(&expected, &eye, &target, &up);
            Mth::CreateLookAtMatrix(actual, eye, target, up);
            check_matrix(expected, actual, "Mth::CreateLookAtMatrix");
        }
    }

    const float params[][4] = {{640, 480, 1, 1000}, {2, 2, -1, 1}, {1.2f, 0.75f, 0.1f, 5000}};
    for (const auto& p : params) {
        Matrix expected{}, actual{};
        Original<ProjFn>(0x00405080)(&expected, p[0], p[1], p[2], p[3]);
        Mth::CreateOrthographicMatrix(actual, p[0], p[1], p[2], p[3]);
        check_matrix(expected, actual, "Mth::CreateOrthographicMatrix");

        float fov = p[0] > 10 ? 1.2f : p[0]; // radians
        Original<ProjFn>(0x004050F0)(&expected, fov, p[1] > 10 ? 0.75f : p[1], p[2], p[3]);
        Mth::CreatePerspectiveMatrix(actual, fov, p[1] > 10 ? 0.75f : p[1], p[2], p[3]);
        check_matrix(expected, actual, "Mth::CreatePerspectiveMatrix");
    }
}

void TestBatch15()
{
    using DigitFn = int(__cdecl*)(const char*);
    for (int value = 0; value < 256; ++value) {
        char c = static_cast<char>(value);
        Check(Original<DigitFn>(0x004020E0)(&c) == Str::DehexifyDigit(&c), "Str::DehexifyDigit");
    }

    using CompareFn = int(__cdecl*)(const char*, const char*);
    const char* words[] = {"", "a", "A", "abc", "ABD", "abcd", "Skater", "skater", "\xE9t\xE9", "zz", "z"};
    for (const char* a : words)
        for (const char* b : words)
            Check(Original<CompareFn>(0x00402120)(a, b) == Str::CompareCaseless(a, b), "Str::CompareCaseless");

    using WordFn = int(__cdecl*)(char*, const char**);
    const char* texts[] = {"hello world", "one\\_two three", "line one\\nnext", "end", "", "  double",
                           "esc\\x here", "\\_lead"};
    for (const char* text : texts) {
        const char* cursor_expected = text;
        const char* cursor_actual = text;
        for (int step = 0; step < 6; ++step) {
            char expected[64] = {}, actual[64] = {};
            int r1 = Original<WordFn>(0x00402170)(expected, &cursor_expected);
            int r2 = Str::NextWord(actual, cursor_actual);
            Check(r1 == r2 && strcmp(expected, actual) == 0 && cursor_expected == cursor_actual, "Str::NextWord");
            if (r1 == 2)
                break;
        }
    }

    using RectFn = Mth::Rect*(__thiscall*)(void*, int, int, int, int);
    int raw[4] = {};
    Mth::Rect* original_rect = Original<RectFn>(0x00404060)(raw, 1, -2, 300, 400);
    Mth::Rect rect(1, -2, 300, 400);
    Check(original_rect == reinterpret_cast<Mth::Rect*>(raw) && raw[0] == rect.left && raw[1] == rect.top &&
              raw[2] == rect.right && raw[3] == rect.bottom,
          "Rect::Rect");

    using Mth::Vector;
    using DirFn = Vector*(__thiscall*)(Vector*, const Vector*);
    using MaxFn = float(__thiscall*)(const Vector*, int*);
    const Vector vectors[] = {{3, 4, 0, 1}, {-1, 2, -3, 0}, {0, 0, 0, 1}, {5, -5, 5, 2}, {1, 1, 0, 0}, {-2, 0.5f, 7, 1}};
    const Vector axis{0.6f, 0, 0.8f, 0.5f};
    for (const Vector& v : vectors) {
        Vector expected = v, actual = v;
        Original<DirFn>(0x004061A0)(&expected, &axis);
        actual.SetDirection(axis);
        CheckVector(expected, actual, "Vector::SetDirection");

        expected = v;
        actual = v;
        Original<DirFn>(0x00406200)(&expected, &axis);
        actual.ProjectOnto(axis);
        CheckVector(expected, actual, "Vector::ProjectOnto");

        int expected_axis = -1, actual_axis = -1;
        float a = Original<MaxFn>(0x00406260)(&v, &expected_axis);
        float b = v.GetMaxAxis(&actual_axis);
        CheckFloat(a, b, "Vector::GetMaxAxis");
        Check(expected_axis == actual_axis, "Vector::GetMaxAxis (axis)");
        CheckFloat(Original<MaxFn>(0x00406260)(&v, nullptr), v.GetMaxAxis(nullptr), "Vector::GetMaxAxis (no axis)");
    }
}

void TestBatch16()
{
    // Number formatting for each separator style. The language setting and output buffer belong to the game:
    // save and restore them.
    using FormatFn = const char*(__cdecl*)(int);
    int& language = GameVariable<int>(0x007CEFA0);
    const int saved_language = language;
    char saved_buffer[36];
    std::memcpy(saved_buffer, &GameVariable<char>(0x00690FA4), sizeof(saved_buffer));
    for (int lang : {0, 3, 4, 6, 9}) {
        language = lang;
        for (int value : {0, 7, 999, 1000, -1000, 123456, -1234567, 2147483647, static_cast<int>(0x80000000u)}) {
            char expected[36];
            strcpy_s(expected, Original<FormatFn>(0x00402200)(value));
            const char* actual = Str::FormatWithSeparators(value);
            if (strcmp(expected, actual) != 0)
                Log("  FormatWithSeparators(%d) lang %d: original \"%s\", ours \"%s\"", value, lang, expected, actual);
            Check(strcmp(expected, actual) == 0, "Str::FormatWithSeparators");
        }
    }
    language = saved_language;
    std::memcpy(&GameVariable<char>(0x00690FA4), saved_buffer, sizeof(saved_buffer));

    // Fast sin/cos read the game's lookup table: fill it for the test, then put the game's copy back.
    constexpr uintptr_t kTable = 0x00690FE0;
    constexpr size_t kTableBytes = 119 * 16;
    static unsigned char saved_table[kTableBytes];
    std::memcpy(saved_table, reinterpret_cast<void*>(kTable), kTableBytes);
    Mth::InitSinLookupTable();
    using TrigFn = float(__cdecl*)(float);
    for (float angle : {0.0f, 0.1f, 0.5f, 0.7853f, 1.2f, 1.5f, 1.5707f}) {
        CheckFloat(Original<TrigFn>(0x00405FF0)(angle), Mth::FastSin(angle), "Mth::FastSin");
        CheckFloat(Original<TrigFn>(0x00406020)(angle), Mth::FastCos(angle), "Mth::FastCos");
    }
    std::memcpy(reinterpret_cast<void*>(kTable), saved_table, kTableBytes);

    using Mth::Matrix;
    using ProductFn = Matrix*(__thiscall*)(Matrix*, const Mth::MatrixProduct*);
    using OrthoFn = Matrix*(__thiscall*)(Matrix*, int);
    Matrix a, b;
    a.SetFromEulerAngles(0.3f, 1.1f, -0.4f);
    a.row[3] = {1, 2, 3, 1};
    b.SetFromEulerAngles(-0.8f, 0.2f, 2.0f);
    const Mth::MatrixProduct product{&a, &b};
    Matrix expected{}, actual{};
    Original<ProductFn>(0x00404100)(&expected, &product);
    actual = product;
    for (int i = 0; i < 4; ++i)
        CheckVector(expected.row[i], actual.row[i], "Matrix::operator=(MatrixProduct)");

    // A slightly skewed rotation, re-orthonormalised about each axis.
    Matrix skewed = a;
    skewed.row[0].y += 0.05f;
    skewed.row[1].z -= 0.08f;
    skewed.row[2].x += 0.12f;
    for (int axis = 0; axis < 3; ++axis) {
        expected = skewed;
        actual = skewed;
        Original<OrthoFn>(0x00405170)(&expected, axis);
        actual.OrthoNormalizeAbout(axis);
        for (int i = 0; i < 4; ++i)
            CheckVector(expected.row[i], actual.row[i], "Matrix::OrthoNormalizeAbout");
    }
}

void TestBatch17()
{
    using Mth::Matrix;
    using Mth::SlerpInterpolator;
    using Mth::Vector;
    using AxisAngleFn = void(__thiscall*)(const Matrix*, Vector*, float*);
    using RotateLocalFn = void(__thiscall*)(Matrix*, const Vector*);
    using CtorFn = SlerpInterpolator*(__thiscall*)(void*, const Matrix*, const Matrix*);
    using SetFn = int(__thiscall*)(SlerpInterpolator*, const Matrix*, const Matrix*);
    using InvertFn = void(__thiscall*)(SlerpInterpolator*);

    // Rotations: none, small, general, and 180 degrees about each axis (the special case).
    Matrix rotations[8];
    rotations[0].SetFromEulerAngles(0, 0, 0);
    rotations[1].SetFromEulerAngles(0.01f, 0.0f, 0.005f);
    rotations[2].SetFromEulerAngles(0.4f, -1.2f, 2.0f);
    rotations[3].SetFromEulerAngles(1.0f, 0.5f, -0.25f);
    Mth::CreateRotateMatrix(rotations[4], 0, 3.14159265f);
    Mth::CreateRotateMatrix(rotations[5], 1, 3.14159265f);
    Mth::CreateRotateMatrix(rotations[6], 2, 3.14159265f);
    const Vector diagonal{1, 1, 0, 0};
    Mth::CreateRotateMatrix(rotations[7], diagonal, 3.14159f);

    for (const Matrix& m : rotations) {
        Vector expected_axis{9, 9, 9, 9}, actual_axis{9, 9, 9, 9};
        float expected_angle = -1, actual_angle = -1;
        Original<AxisAngleFn>(0x00405310)(&m, &expected_axis, &expected_angle);
        m.GetRotationAxisAngle(actual_axis, actual_angle);
        CheckFloat(expected_angle, actual_angle, "Matrix::GetRotationAxisAngle (angle)");
        CheckVector(expected_axis, actual_axis, "Matrix::GetRotationAxisAngle (axis)");
    }

    const Vector angle_sets[] = {{0, 0, 0, 0}, {0.3f, 0, 0, 0}, {0, -0.7f, 1.1f, 0}, {0.2f, 0.4f, -0.6f, 0}};
    for (const Vector& angles : angle_sets) {
        Matrix expected = rotations[2], actual = rotations[2];
        Original<RotateLocalFn>(0x00405850)(&expected, &angles);
        actual.RotateLocal(angles);
        for (int i = 0; i < 4; ++i)
            CheckVector(expected.row[i], actual.row[i], "Matrix::RotateLocal");
    }

    auto same_slerp = [](const SlerpInterpolator& a, const SlerpInterpolator& b, const char* what) {
        for (int i = 0; i < 4; ++i) {
            CheckVector(a.start.row[i], b.start.row[i], what);
            CheckVector(a.end.row[i], b.end.row[i], what);
        }
        CheckVector(a.axis, b.axis, what);
        CheckFloat(a.angle, b.angle, what);
        Check(a.small_rotation == b.small_rotation, what);
    };
    for (int i = 0; i < 8; ++i) {
        const Matrix& from = rotations[2];
        const Matrix& to = rotations[i];
        alignas(16) static unsigned char raw_expected[sizeof(SlerpInterpolator)];
        auto* expected = Original<CtorFn>(0x00405E90)(raw_expected, &from, &to);
        SlerpInterpolator actual(from, to);
        same_slerp(*expected, actual, "SlerpInterpolator::SlerpInterpolator");

        int r1 = Original<SetFn>(0x00405990)(expected, &to, &from);
        int r2 = actual.SetMatrices(to, from);
        Check(r1 == r2, "SlerpInterpolator::SetMatrices (result)");
        same_slerp(*expected, actual, "SlerpInterpolator::SetMatrices");

        Original<InvertFn>(0x004058E0)(expected);
        actual.InvertDirection();
        same_slerp(*expected, actual, "SlerpInterpolator::InvertDirection");
    }
}

void TestBatch18()
{
    using Mth::Matrix;
    using Mth::SlerpInterpolator;
    using Mth::Vector;
    using GetMatrixFn = void(__thiscall*)(const SlerpInterpolator*, Matrix*, float);
    using AngleFn = float(__cdecl*)(const Matrix*, const Vector*, int, int);

    Matrix from, to_small, to_large;
    from.SetFromEulerAngles(0.2f, -0.4f, 0.9f);
    from.row[3] = {1, 2, 3, 1};
    to_small.SetFromEulerAngles(0.205f, -0.4f, 0.905f); // under the 2-degree threshold
    to_small.row[3] = {4, -2, 3, 1};
    to_large.SetFromEulerAngles(-1.0f, 0.8f, 2.5f);
    to_large.row[3] = {-6, 0, 9, 1};

    for (const Matrix* to : {&to_small, &to_large}) {
        SlerpInterpolator slerp(from, *to);
        for (float t : {-0.5f, 0.0f, 0.25f, 0.5f, 0.9f, 1.0f, 1.5f}) {
            Matrix expected{}, actual{};
            Original<GetMatrixFn>(0x00405A20)(&slerp, &expected, t);
            slerp.GetMatrix(actual, t);
            for (int i = 0; i < 4; ++i)
                CheckVector(expected.row[i], actual.row[i], "SlerpInterpolator::GetMatrix");
        }
    }

    Matrix m;
    m.SetFromEulerAngles(0.3f, 1.1f, -0.7f);
    const Vector vectors[] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {-1, 2, 0.5f, 0}, {0.3f, -0.2f, -3, 0}};
    for (const Vector& v : vectors) {
        for (int ref = 0; ref < 3; ++ref) {
            for (int normal = 0; normal < 3; ++normal) {
                if (ref == normal)
                    continue;
                // acos near +/-1 magnifies float rounding of its input, so compare angles to 1e-4 radians.
                CheckFloatWithin(Original<AngleFn>(0x004062D0)(&m, &v, ref, normal),
                                 Mth::GetAngleInPlane(m, v, ref, normal), 1e-4f,
                           "Mth::GetAngleInPlane");
            }
        }
    }
    // A vector along the plane normal has nothing in the plane: both should return 0.
    const Vector along_normal = m.row[2];
    CheckFloat(Original<AngleFn>(0x004062D0)(&m, &along_normal, 0, 2), Mth::GetAngleInPlane(m, along_normal, 0, 2),
               "Mth::GetAngleInPlane (zero)");
}

void TestPool()
{
    using Mem::Pool;
    using AllocateFn = void*(__thiscall*)(Pool*);
    using FreeFn = void(__thiscall*)(Pool*, void*);
    using ContainsFn = bool(__thiscall*)(const Pool*, const void*);

    // A pool built by hand over a static block, so no game allocator is needed: 6 items of 12 bytes.
    constexpr int kItems = 6, kSize = 12;
    alignas(4) static uint8_t block_expected[kItems * kSize], block_actual[kItems * kSize];
    auto build = [](Pool& pool, uint8_t* block) {
        std::memset(&pool, 0, sizeof(pool));
        pool.m_start = block;
        pool.m_end = block + kItems * kSize;
        pool.m_count = kItems;
        pool.m_itemSize = kSize;
        pool.m_freeList = block;
        for (int i = 0; i < kItems; ++i)
            *reinterpret_cast<void**>(block + i * kSize) = i < kItems - 1 ? block + (i + 1) * kSize : nullptr;
    };
    alignas(Pool) static unsigned char raw_expected[sizeof(Pool)], raw_actual[sizeof(Pool)];
    auto& expected = *reinterpret_cast<Pool*>(raw_expected);
    auto& actual = *reinterpret_cast<Pool*>(raw_actual);
    build(expected, block_expected);
    build(actual, block_actual);

    auto same = [&] {
        auto offset = [](const void* p, const uint8_t* base) {
            return p ? static_cast<const uint8_t*>(p) - base : -1;
        };
        return expected.m_used == actual.m_used && expected.m_peakUsed == actual.m_peakUsed &&
               offset(expected.m_freeList, block_expected) == offset(actual.m_freeList, block_actual);
    };

    // Allocate everything (and one more, which must fail), free a few, allocate again.
    void* items_expected[kItems + 1];
    void* items_actual[kItems + 1];
    for (int i = 0; i <= kItems; ++i) {
        items_expected[i] = Original<AllocateFn>(0x005BA3D0)(&expected);
        items_actual[i] = actual.Allocate();
        bool both_null = !items_expected[i] && !items_actual[i];
        bool same_slot = items_expected[i] && items_actual[i] &&
                         static_cast<uint8_t*>(items_expected[i]) - block_expected ==
                             static_cast<uint8_t*>(items_actual[i]) - block_actual;
        Check((both_null || same_slot) && same(), "Pool::Allocate");
    }
    for (int i : {2, 0, 4}) {
        Original<FreeFn>(0x005BA400)(&expected, items_expected[i]);
        actual.Free(items_actual[i]);
        Check(same(), "Pool::Free");
    }
    for (int i = 0; i < 2; ++i) {
        void* a = Original<AllocateFn>(0x005BA3D0)(&expected);
        void* b = actual.Allocate();
        Check(static_cast<uint8_t*>(a) - block_expected == static_cast<uint8_t*>(b) - block_actual && same(),
              "Pool::Allocate (after Free)");
    }

    const uint8_t* probes[] = {block_actual - 1, block_actual, block_actual + 30, block_actual + kItems * kSize - 1,
                               block_actual + kItems * kSize};
    for (const uint8_t* p : probes)
        Check(Original<ContainsFn>(0x005BA3B0)(&actual, p) == actual.Contains(p), "Pool::Contains");
}

// Fake Pip tables, shared by the early and late Pip tests. Install saves the real tables and puts the fakes
// in; RestorePipTables puts the real ones back (the test runner calls it even if the test crashes).
Pip::LoadedFile g_saved_files[200];
char* g_saved_pres[100];
bool g_pip_tables_saved = false;
alignas(16) uint8_t g_archive_a[512], g_archive_b[512];

void RestorePipTables()
{
    if (!g_pip_tables_saved)
        return;
    std::memcpy(&GameVariable<Pip::LoadedFile[200]>(0x007CF198), g_saved_files, sizeof(g_saved_files));
    std::memcpy(&GameVariable<char* [100]>(0x007CFE18), g_saved_pres, sizeof(g_saved_pres));
    g_pip_tables_saved = false;
}

void InstallFakePipTables()
{
    using Pip::LoadedFile;
    using Pip::PreEntry;
    auto& files = GameVariable<LoadedFile[200]>(0x007CF198);
    auto& pres = GameVariable<char* [100]>(0x007CFE18);

    std::memcpy(g_saved_files, files, sizeof(files));
    std::memcpy(g_saved_pres, pres, sizeof(pres));
    g_pip_tables_saved = true;
    std::memset(files, 0, sizeof(files));
    std::memset(pres, 0, sizeof(pres));

    // Two fake archives laid out like loaded .pre files: name, header {size, version, count}, entries.
    auto build = [](uint8_t* block, const char* name, std::initializer_list<PreEntry> entries) {
        std::memset(block, 0, 512);
        std::memcpy(block, name, std::strlen(name) + 1);
        uint8_t* header = block + ((std::strlen(name) + 0x10) & ~0xFu);
        reinterpret_cast<int*>(header)[2] = static_cast<int>(entries.size());
        uint8_t* p = header + 12;
        for (const PreEntry& e : entries) {
            std::memcpy(p, &e, sizeof(e));
            int stored = e.compressedSize ? e.compressedSize : e.size;
            p = reinterpret_cast<uint8_t*>(((reinterpret_cast<uintptr_t>(p) + e.nameSize + 0x1F) & ~uintptr_t(0xF)) +
                                           ((stored + 3) & ~3));
        }
        return reinterpret_cast<char*>(block);
    };
    pres[3] = build(g_archive_a, "skater.pre", {{10, 0, 8, 0, 0x111}, {40, 21, 13, 2, 0x222}, {7, 0, 5, 0, 0x333}});
    pres[50] = build(g_archive_b, "levelname_scripts.pre", {{100, 0, 20, 0, 0x444}, {3, 0, 1, 0, 0x222}});
    files[7] = {reinterpret_cast<void*>(0x1000), 0x555, 1234, 3};
    files[120] = {reinterpret_cast<void*>(0x2000), 0x222, 99, 2};
}

// Checksum-based Pip functions. (The name-based ones call the game's _stricmp, which needs the game's C
// runtime - not initialised yet in DllMain - so they are tested late, in TestPipNamesLate.)
void TestPip()
{
    using Pip::LoadedFile;
    using Pip::PreEntry;
    auto& files = GameVariable<LoadedFile[200]>(0x007CF198);
    InstallFakePipTables();

    using FindFn = PreEntry*(__cdecl*)(uint32_t);
    using SizeFn = int(__cdecl*)(uint32_t);
    using SizeByNameFn = int(__cdecl*)(const char*);
    using UnloadFn = void(__cdecl*)(uint32_t);
    using UnloadByNameFn = void(__cdecl*)(const char*);

    for (uint32_t c : {0x111u, 0x222u, 0x333u, 0x444u, 0x555u, 0x999u}) {
        Check(Original<FindFn>(0x005B7180)(c) == Pip::FindPreEntry(c), "Pip::FindPreEntry");
        Check(Original<SizeFn>(0x005B7290)(c) == Pip::GetFileSize(c), "Pip::GetFileSize");
    }
    for (const char* name : {"skater.pre", "SKATER.PRE", "levelname_scripts.pre", "missing.pre"})
        Check(Original<SizeByNameFn>(0x005B72E0)(name) == Pip::GetFileSize(name), "Pip::GetFileSize(name)");

    // Unload changes the tables: run the original, keep the result, put the tables back, run ours. Reference
    // counts stay above zero so nothing (fake) gets freed.
    static LoadedFile files_before[200], files_expected[200];
    static uint8_t a_before[512], b_before[512], a_expected[512], b_expected[512];
    auto snapshot = [&](LoadedFile* f, uint8_t* a, uint8_t* b) {
        std::memcpy(f, files, sizeof(files));
        std::memcpy(a, g_archive_a, 512);
        std::memcpy(b, g_archive_b, 512);
    };
    auto restore = [&](const LoadedFile* f, const uint8_t* a, const uint8_t* b) {
        std::memcpy(files, f, sizeof(files));
        std::memcpy(g_archive_a, a, 512);
        std::memcpy(g_archive_b, b, 512);
    };
    auto unload_case = [&](auto original, auto ours, const char* what) {
        snapshot(files_before, a_before, b_before);
        original();
        snapshot(files_expected, a_expected, b_expected);
        restore(files_before, a_before, b_before);
        ours();
        Check(std::memcmp(files, files_expected, sizeof(files)) == 0 && std::memcmp(g_archive_a, a_expected, 512) == 0 &&
                  std::memcmp(g_archive_b, b_expected, 512) == 0,
              what);
    };
    for (uint32_t c : {0x222u, 0x555u, 0x111u, 0x999u})
        unload_case([&] { Original<UnloadFn>(0x005B7200)(c); }, [&] { Pip::Unload(c); }, "Pip::Unload");
    unload_case([] { Original<UnloadByNameFn>(0x005B7270)("whatever.tex"); }, [] { Pip::Unload("whatever.tex"); },
                "Pip::Unload(name)");
}

void TestFile()
{
    // Runs in DllMain, before the archive manager exists, so this exercises the disk paths. Only names in
    // folders that exist: any error other than "file not found" would show the fatal file error screen.
    if (GameVariable<void*>(0x007D0FC4) != nullptr) {
        Log("self-test: TestFile skipped (archive manager already exists)");
        return;
    }
    using OpenFn = File::Handle(__cdecl*)(const char*, const char*);
    using CloseFn = int(__cdecl*)(File::Handle);
    using ReadFn = int(__cdecl*)(void*, int, int, File::Handle);
    using WriteFn = int(__cdecl*)(const void*, int, int, File::Handle);
    using SeekFn = uint32_t(__cdecl*)(File::Handle, int, int);
    using SizeFn = uint32_t(__cdecl*)(File::Handle);
    using ExistFn = bool(__cdecl*)(const char*);

    // The game opens data files relative to the current directory and never sets it itself, so it only works
    // when launched with the game folder as the working directory. If it wasn't, the game will hit its own
    // fatal file error later; report it as a failure here, and don't run the file tests - a "path not
    // found" inside DllMain would crash before the game can show anything (0xC0000142 at launch).
    DWORD attributes = GetFileAttributesA("data\\pre");
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        char directory[MAX_PATH] = "?";
        GetCurrentDirectoryA(MAX_PATH, directory);
        Log("Game was started from the wrong folder: data\\pre not found in the working directory (%s)", directory);
        Check(false, "File tests: game started from the wrong folder (working directory must be the game folder)");
        return;
    }

    const char* names[] = {"pre/AP_scripts.prx", "pre\\AP_scripts.pre", "pre/nothere.prx",
                           "c:\\skate5\\data\\pre/APcol.prx", "pre/APcol.prx"};
    for (const char* name : names)
        Check(Original<ExistFn>(0x005BA010)(name) == File::Exist(name), "File::Exist");

    // The game opens files without sharing, so the original and ours can't have the same file open at once:
    // run the whole sequence through one, close the file, then through the other, and compare the records.
    struct Trace {
        bool opened = false;
        uint32_t size = 0;
        std::vector<uint32_t> positions;
        std::vector<int> counts;
        std::vector<uint8_t> bytes;
        int write = 0, close = 0;
        bool operator==(const Trace&) const = default;
    };
    const int seeks[][2] = {{100, 0}, {12, 1}, {-40, 2}, {0, 0}, {16, 7}};
    for (const char* name : names) {
        Trace traces[2];
        for (int which = 0; which < 2; ++which) {
            bool original = which == 0;
            Trace& t = traces[which];
            File::Handle file = original ? Original<OpenFn>(0x005B9E60)(name, "rb") : File::Open(name, "rb");
            t.opened = file != nullptr;
            if (!file)
                continue;
            t.size = original ? Original<SizeFn>(0x005B9E00)(file) : File::GetFileSize(file);
            for (const auto& sk : seeks) {
                t.positions.push_back(original ? Original<SeekFn>(0x005B9FA0)(file, sk[0], sk[1])
                                               : File::Seek(file, sk[0], sk[1]));
                uint8_t buffer[64] = {};
                t.counts.push_back(original ? Original<ReadFn>(0x005B9EE0)(buffer, 4, 16, file)
                                            : File::Read(buffer, 4, 16, file));
                t.bytes.insert(t.bytes.end(), buffer, buffer + sizeof(buffer));
            }
            t.write = original ? Original<WriteFn>(0x005B9F60)("x", 1, 1, file) : File::Write("x", 1, 1, file);
            t.close = original ? Original<CloseFn>(0x005B9EA0)(file) : File::Close(file);
        }
        Check(traces[0] == traces[1], "File::Open/GetFileSize/Seek/Read/Write/Close");
    }
}

// --- late self-tests ----------------------------------------------------------------------------------

// The table's contents as (bucket, position in chain, key, value), so tables with different item addresses
// can be compared.
std::vector<std::tuple<int, int, uint32_t, void*>> DumpHashTable(const Lst::HashTable& table)
{
    std::vector<std::tuple<int, int, uint32_t, void*>> out;
    for (int b = 0; b < 1 << table.m_bits; ++b) {
        int position = 0;
        for (const Lst::HashTable::Item* item = &table.m_buckets[b]; item; item = item->next)
            out.emplace_back(b, position++, item->key, item->value);
    }
    return out;
}

void TestHashTableLate()
{
    using Lst::HashTable;
    using CtorFn = HashTable*(__thiscall*)(void*, int);
    using PutFn = bool(__thiscall*)(HashTable*, const uint32_t*, void*);
    using FlushFn = void(__thiscall*)(HashTable*, const uint32_t*);
    using FlushAllFn = void(__thiscall*)(HashTable*);
    using CallbackFn = void(__thiscall*)(HashTable*, HashTable::Callback, void*);
    using NextFn = bool(__thiscall*)(HashTable*);

    alignas(HashTable) unsigned char raw_expected[sizeof(HashTable)], raw_actual[sizeof(HashTable)];
    HashTable* expected;
    {
        OriginalCode original(0x0049FD90);
        expected = Original<CtorFn>(0x0049FD90)(raw_expected, 2);
    }
    HashTable* actual = new (raw_actual) HashTable(2);
    Check(expected == reinterpret_cast<HashTable*>(raw_expected) && expected->m_bits == actual->m_bits &&
              expected->m_size == actual->m_size && expected->m_flag == actual->m_flag &&
              DumpHashTable(*expected) == DumpHashTable(*actual),
          "HashTable::HashTable");

    auto value = [](int i) { return reinterpret_cast<void*>(static_cast<uintptr_t>(0x1000 + i * 4)); };
    auto same = [&] { return expected->m_size == actual->m_size && DumpHashTable(*expected) == DumpHashTable(*actual); };

    // 4 buckets: keys 1, 5, 9, 13 share one, 2 and 6 another.
    const uint32_t keys[] = {1, 5, 2, 9, 6, 13, 3, 0x80000001u};
    for (int i = 0; i < static_cast<int>(std::size(keys)); ++i) {
        bool e, a;
        {
            OriginalCode original(0x00462740);
            e = Original<PutFn>(0x00462740)(expected, &keys[i], value(i));
        }
        a = actual->PutItem(keys[i], value(i));
        Check(e == a && same(), "HashTable::PutItem");
    }

    auto iterate = [](HashTable* table, bool original) {
        std::vector<uint32_t> seen;
        table->m_iterBucket = -1;
        table->m_iterItem = nullptr;
        for (int guard = 0; guard < 64; ++guard) {
            bool more;
            if (original) {
                OriginalCode code(0x00588190);
                more = Original<NextFn>(0x00588190)(table);
            } else {
                more = table->IterateNext();
            }
            if (!more)
                break;
            seen.push_back(table->m_iterItem->key);
        }
        return seen;
    };
    auto callbacks = [](HashTable* table, bool original) {
        static std::vector<void*>* s_out;
        std::vector<void*> out;
        s_out = &out;
        auto record = [](void* v, void*) { s_out->push_back(v); };
        if (original) {
            OriginalCode code(0x005B8060);
            Original<CallbackFn>(0x005B8060)(table, record, nullptr);
        } else {
            table->HandleCallback(record, nullptr);
        }
        return out;
    };
    auto check_walks = [&](const char* when) {
        Check(iterate(expected, true) == iterate(actual, false), when);
        Check(callbacks(expected, true) == callbacks(actual, false), when);
    };
    check_walks("HashTable::IterateNext/HandleCallback (full)");

    // Remove a bucket's own slot (leaving its chain), a chained item, a missing key, then re-add.
    const uint32_t removals[] = {1, 9, 77, 2, 6, 1};
    for (uint32_t key : removals) {
        {
            OriginalCode original(0x005B80D0);
            Original<FlushFn>(0x005B80D0)(expected, &key);
        }
        actual->FlushItem(key);
        Check(same(), "HashTable::FlushItem");
    }
    check_walks("HashTable::IterateNext/HandleCallback (after FlushItem)");
    {
        uint32_t key = 21;
        {
            OriginalCode original(0x00462740);
            Original<PutFn>(0x00462740)(expected, &key, value(50));
        }
        actual->PutItem(key, value(50));
        Check(same(), "HashTable::PutItem (into emptied slot)");
    }

    {
        OriginalCode original(0x0049FD30);
        Original<FlushAllFn>(0x0049FD30)(expected);
    }
    actual->FlushAllItems();
    Check(same() && actual->m_size == 0, "HashTable::FlushAllItems");
    check_walks("HashTable::IterateNext/HandleCallback (empty)");

    Mem::Free(expected->m_buckets);
    Mem::Free(actual->m_buckets);
}

void TestPipLoad()
{
    if (!GameVariable<void*>(0x007D0FCC)) {
        Log("self-test: Pip::Load skipped (pre-file manager not created yet)");
        return;
    }
    using LoadFn = void*(__cdecl*)(const char*, bool, void*, void*, void*);
    auto& files = GameVariable<Pip::LoadedFile[200]>(0x007CF198);
    auto slot_of = [&](void* data) -> Pip::LoadedFile* {
        for (auto& f : files)
            if (f.data == data)
                return &f;
        return nullptr;
    };
    // A small file that is never in a .pre archive and isn't loaded at this point. Load it with the
    // original, keep a copy, unload, then load it with ours and compare.
    const char* name = "pre/Themes.prx";
    if (Pip::GetFileSize(name) != 0) {
        Log("self-test: Pip::Load skipped (%s already loaded)", name);
        return;
    }
    void* expected;
    {
        OriginalCode original(0x005B7C00);
        expected = Original<LoadFn>(0x005B7C00)(name, false, nullptr, nullptr, nullptr);
    }
    Pip::LoadedFile* expected_slot = slot_of(expected);
    Pip::LoadedFile expected_fields = expected_slot ? *expected_slot : Pip::LoadedFile{};
    std::vector<uint8_t> expected_bytes;
    if (expected && expected_fields.size > 0)
        expected_bytes.assign(static_cast<uint8_t*>(expected), static_cast<uint8_t*>(expected) + expected_fields.size);
    Pip::Unload(name);

    void* actual = Pip::Load(name, false, nullptr, nullptr, nullptr);
    Pip::LoadedFile* actual_slot = slot_of(actual);
    bool ok = expected && actual && expected_slot && actual_slot && expected_slot == actual_slot &&
              actual_slot->checksum == expected_fields.checksum && actual_slot->size == expected_fields.size &&
              actual_slot->refCount == 1 &&
              std::memcmp(actual, expected_bytes.data(), expected_bytes.size()) == 0;
    Check(ok, "Pip::Load");

    // Loading it again just adds a use - through the original and ours.
    {
        OriginalCode original(0x005B7C00);
        Check(Original<LoadFn>(0x005B7C00)(name, false, nullptr, nullptr, nullptr) == actual, "Pip::Load (again, original)");
    }
    Check(Pip::Load(name, false, nullptr, nullptr, nullptr) == actual && actual_slot && actual_slot->refCount == 3,
          "Pip::Load (again)");
    for (int i = 0; i < 3; ++i)
        Pip::Unload(name);
    Check(Pip::GetFileSize(name) == 0, "Pip::Unload (after Load)");
}

void TestPipNamesLate()
{
    using NextFn = const char*(__cdecl*)(const char*);
    using NameBoolFn = bool(__cdecl*)(const char*);
    InstallFakePipTables();
    for (const char* name : {"skater.pre", "SKATER.PRE", "levelname_scripts.pre", "missing.pre"}) {
        bool in_use;
        const char* next;
        {
            OriginalCode a(0x005B7370), b(0x005B7300);
            in_use = Original<NameBoolFn>(0x005B7370)(name);
            next = Original<NextFn>(0x005B7300)(name);
        }
        Check(in_use == Pip::PreFileIsInUse(name), "Pip::PreFileIsInUse");
        Check(next == Pip::GetNextLoadedPre(name), "Pip::GetNextLoadedPre");
    }
    const char* first;
    bool unloaded;
    {
        OriginalCode a(0x005B7300), b(0x005B7130);
        first = Original<NextFn>(0x005B7300)(nullptr);
        // Only a name that matches nothing: a match would free the fake archive.
        unloaded = Original<NameBoolFn>(0x005B7130)("missing.pre");
    }
    Check(first == Pip::GetNextLoadedPre(nullptr), "Pip::GetNextLoadedPre(null)");
    Check(unloaded == Pip::UnloadPre("missing.pre"), "Pip::UnloadPre");
}

// A fake .pre archive in game memory (PreFile frees its block with Mem::Free), with plain, compressed, empty
// and duplicate-name entries. `compressed_source` gets the uncompressed bytes of the compressed entry.
uint8_t* BuildFakeArchive(std::vector<uint8_t>& compressed_source, const std::string& prefix = "")
{
    struct Spec {
        std::string name;
        std::vector<uint8_t> bytes;
        bool compress;
    };
    compressed_source.clear();
    for (int i = 0; i < 700; ++i)
        compressed_source.push_back(static_cast<uint8_t>("skate or die "[i % 13] + (i / 97)));
    std::vector<uint8_t> plain(37);
    for (int i = 0; i < 37; ++i)
        plain[i] = static_cast<uint8_t>(i * 7 + 1);
    const Spec specs[] = {{prefix + "scripts/a.qb", plain, false},
                          {prefix + "models\\b.mdl", compressed_source, true},
                          {prefix + "empty.txt", {}, false},
                          {prefix + "scripts/a.qb", {9, 9, 9}, false},
                          {prefix + "qb\\levelscripts.pre", {1, 2, 3, 4, 5}, false}};

    std::vector<uint8_t> block(12, 0);
    for (const Spec& s : specs) {
        std::vector<uint8_t> stored = s.bytes;
        int compressed_size = 0;
        if (s.compress) {
            std::vector<uint8_t> out(s.bytes.size() * 9 / 8 + 16);
            compressed_size = Lzss::Encode(s.bytes.data(), out.data(), static_cast<int>(s.bytes.size()), false);
            stored.assign(out.begin(), out.begin() + compressed_size);
        }
        int name_size = static_cast<int>((s.name.size() + 1 + 3) & ~size_t(3));
        size_t at = block.size();
        block.resize(at + 16 + name_size + ((stored.size() + 3) & ~size_t(3)), 0);
        int32_t header[4] = {static_cast<int32_t>(s.bytes.size()), compressed_size, name_size,
                             static_cast<int32_t>(Crc::GenerateCRCFromString(s.name.c_str()))};
        std::memcpy(&block[at], header, sizeof(header));
        std::memcpy(&block[at + 16], s.name.c_str(), s.name.size() + 1);
        if (!stored.empty())
            std::memcpy(&block[at + 16 + name_size], stored.data(), stored.size());
    }
    int32_t top[3] = {static_cast<int32_t>(block.size()), 3, static_cast<int32_t>(std::size(specs))};
    std::memcpy(block.data(), top, sizeof(top));
    auto* memory = static_cast<uint8_t*>(Mem::New(static_cast<unsigned int>(block.size())));
    std::memcpy(memory, block.data(), block.size());
    return memory;
}

void TestPreFileLate()
{
    using Pre::PreFile;
    using CtorFn = PreFile*(__thiscall*)(void*, uint8_t*, bool);
    using DtorFn = PreFile*(__thiscall*)(PreFile*, unsigned int);
    using LoadFn = uint8_t*(__thiscall*)(PreFile*, const char*, int*, uint8_t*);
    using OpenFn = PreFile::FileInfo*(__thiscall*)(PreFile*, const char*);
    using ByHandleFn = uint8_t*(__thiscall*)(PreFile*, PreFile::FileInfo*);
    using ReadFn = int(__thiscall*)(PreFile*, void*, unsigned int);
    using SeekFn = int(__thiscall*)(PreFile*, int, int);
    using DecodeFn = void(__cdecl*)(const uint8_t*, uint8_t*, int);

    std::vector<uint8_t> source;
    uint8_t* block_expected = BuildFakeArchive(source);
    uint8_t* block_actual = BuildFakeArchive(source);
    auto* expected = static_cast<PreFile*>(Mem::New(sizeof(PreFile)));
    auto* actual = static_cast<PreFile*>(Mem::New(sizeof(PreFile)));
    Check(Original<CtorFn>(0x005B8820)(expected, block_expected, false) == expected,
          "PreFile::PreFile (returns this)");
    new (actual) PreFile(block_actual, false);

    const char* names[] = {"scripts/a.qb", "models\\b.mdl", "empty.txt", "missing.dat"};
    auto offset = [](const uint8_t* p, const uint8_t* base) { return p ? p - base : -1; };
    bool same_table =
        expected->m_numFiles == actual->m_numFiles && expected->m_bottomUpHeap == actual->m_bottomUpHeap;
    for (const char* name : names) {
        auto* e = static_cast<PreFile::FileInfo*>(expected->m_files->GetItem(Crc::GenerateCRCFromString(name)));
        auto* a = static_cast<PreFile::FileInfo*>(actual->m_files->GetItem(Crc::GenerateCRCFromString(name)));
        same_table = same_table && (e == nullptr) == (a == nullptr) &&
                     (!e || (e->compressedSize == a->compressedSize && e->size == a->size &&
                             offset(e->data, block_expected) == offset(a->data, block_actual) && !e->unpacked &&
                             !a->unpacked && e->position == a->position));
    }
    Check(same_table, "PreFile::PreFile");

    // LoadContainedFile into a buffer of our own, then once allocating.
    for (const char* name : names) {
        uint8_t out_e[800] = {}, out_a[800] = {};
        int size_e = -5, size_a = -5;
        uint8_t* r_e = Original<LoadFn>(0x005B8230)(expected, name, &size_e, out_e);
        uint8_t* r_a = actual->LoadContainedFile(name, &size_a, out_a);
        Check((r_e == out_e) == (r_a == out_a) && (r_e == nullptr) == (r_a == nullptr) && size_e == size_a &&
                  std::memcmp(out_e, out_a, sizeof(out_e)) == 0,
              "PreFile::LoadContainedFile");
    }
    {
        int size = 0;
        uint8_t* fresh = actual->LoadContainedFile("models\\b.mdl", &size, nullptr);
        Check(fresh && size == static_cast<int>(source.size()) &&
                  std::memcmp(fresh, source.data(), source.size()) == 0,
              "PreFile::LoadContainedFile (allocating, decompressed)");
        if (fresh)
            Mem::FreeMalloc(fresh);
    }

    // Open each file, then seek and read through it the same way in both.
    const int seeks[][2] = {{0, 0}, {5, 1}, {10, 2}, {-3, 1}, {100000, 0}, {1, 7}, {0, 2}, {3, 0}};
    for (const char* name : names) {
        PreFile::FileInfo* h_e = Original<OpenFn>(0x005B8A00)(expected, name);
        PreFile::FileInfo* h_a = actual->GetContainedFile(name);
        Check((h_e == nullptr) == (h_a == nullptr), "PreFile::GetContainedFile");
        if (!h_e || !h_a)
            continue;
        uint8_t* d_e = Original<ByHandleFn>(0x005B8310)(expected, h_e);
        uint8_t* d_a = actual->GetContainedFileByHandle(h_a);
        Check((d_e == nullptr) == (d_a == nullptr) && (!d_e || std::memcmp(d_e, d_a, h_a->size) == 0),
              "PreFile::GetContainedFileByHandle");
        for (const auto& sk : seeks) {
            int s_e = Original<SeekFn>(0x005B7F60)(expected, sk[0], sk[1]);
            int s_a = actual->Seek(sk[0], sk[1]);
            uint8_t b_e[64] = {}, b_a[64] = {};
            int n_e = Original<ReadFn>(0x005B7F00)(expected, b_e, 20);
            int n_a = actual->Read(b_a, 20);
            Check(s_e == s_a && n_e == n_a && std::memcmp(b_e, b_a, sizeof(b_e)) == 0 &&
                      h_e->position == h_a->position,
                  "PreFile::Seek/Read");
        }
    }
    PreFile::FileInfo foreign = {};
    Check(Original<ByHandleFn>(0x005B8310)(expected, &foreign) == actual->GetContainedFileByHandle(&foreign),
          "PreFile::GetContainedFileByHandle (not ours)");

    // The decompressor on its own, on the compressed entry's data.
    {
        auto* info = static_cast<PreFile::FileInfo*>(
            actual->m_files->GetItem(Crc::GenerateCRCFromString("models\\b.mdl")));
        std::vector<uint8_t> out_e(source.size() + 32), out_a(source.size() + 32);
        Original<DecodeFn>(0x005B7DF0)(info->data, out_e.data(), info->compressedSize);
        Pre::Decode(info->data, out_a.data(), info->compressedSize);
        Check(out_e == out_a && std::memcmp(out_a.data(), source.data(), source.size()) == 0, "Pre::Decode");
    }

    Check(Original<DtorFn>(0x005B9410)(expected, 1) == expected, "PreFile::~PreFile (returns this)");
    actual->~PreFile();
    Mem::Free(actual);
}

// The pre manager's globals: the manager slot, the created instance and its reference count, and File's
// "handled" flag. Saved by TestPreMgrLate and put back by RestorePreGlobals, even if the test crashes.
struct PreGlobals {
    Pre::PreMgr* slot;
    Pre::PreMgr* instance;
    int refs;
    bool handled;
};
PreGlobals g_saved_pre_globals;
bool g_pre_globals_saved = false;

void SavePreGlobals()
{
    g_saved_pre_globals = {GameVariable<Pre::PreMgr*>(0x007D0FC4), GameVariable<Pre::PreMgr*>(0x007D0FCC),
                           GameVariable<int>(0x007D0FD0), GameVariable<bool>(0x007D0FC8)};
    g_pre_globals_saved = true;
}

void RestorePreGlobals()
{
    if (!g_pre_globals_saved)
        return;
    GameVariable<Pre::PreMgr*>(0x007D0FC4) = g_saved_pre_globals.slot;
    GameVariable<Pre::PreMgr*>(0x007D0FCC) = g_saved_pre_globals.instance;
    GameVariable<int>(0x007D0FD0) = g_saved_pre_globals.refs;
    GameVariable<bool>(0x007D0FC8) = g_saved_pre_globals.handled;
    g_pre_globals_saved = false;
}

// Which archive (by name) a manager's active file belongs to, plus the file's size and offset in its
// archive - comparable between two managers holding equal archives at different addresses.
std::string DescribeActive(Pre::PreMgr* m, Pre::PreFile::FileInfo* handle)
{
    if (!handle)
        return "none";
    const char* archive = "?";
    for (const char* name : {"levels\\one.pre", "two.pre"})
        if (m->m_pres->GetItem(Crc::GenerateCRCFromString(name)) == m->m_activePre)
            archive = name;
    char text[160];
    sprintf_s(text, "%s size %d at %d pos %d open %d", archive, handle->size,
              static_cast<int>(handle->data - m->m_activePre->m_block), handle->position,
              m->m_activePre->m_openCount);
    return text;
}

void TestPreMgrLate()
{
    using Pre::PreFile;
    using Pre::PreMgr;
    using Info = PreFile::FileInfo;
    auto& slot = GameVariable<PreMgr*>(0x007D0FC4);
    auto& instance = GameVariable<PreMgr*>(0x007D0FCC);
    auto& refs = GameVariable<int>(0x007D0FD0);
    auto& handled = GameVariable<bool>(0x007D0FC8);
    SavePreGlobals();

    // --- reference-counted creation
    {
        using CreateFn = PreMgr*(__cdecl*)(bool);
        using DestroyFn = void(__cdecl*)();
        auto run = [&](bool original) {
            std::vector<int> trace;
            instance = nullptr;
            refs = 0;
            auto create = [&](bool c) {
                PreMgr* returned = original ? Original<CreateFn>(0x005B8E90)(c) : Pre::CreateManager(c);
                trace.push_back(returned == instance);
            };
            auto destroy = [&] { original ? Original<DestroyFn>(0x005B9510)() : Pre::DestroyManager(); };
            create(false);
            trace.insert(trace.end(), {instance != nullptr, refs});
            create(true);
            trace.insert(trace.end(), {instance != nullptr, refs, slot == instance});
            create(true);
            trace.insert(trace.end(), {instance != nullptr, refs});
            destroy();
            trace.insert(trace.end(), {instance != nullptr, refs});
            destroy();
            trace.insert(trace.end(), {instance != nullptr, refs});
            destroy();
            trace.insert(trace.end(), {instance != nullptr, refs});
            return trace;
        };
        std::vector<int> expected_trace = run(true);
        Check(expected_trace == run(false), "Pre::CreateManager/DestroyManager");
    }

    // --- two managers, each given the same two archives through pending loads
    using CtorFn = PreMgr*(__thiscall*)(void*);
    using FinishFn = void(__thiscall*)(PreMgr*, int, uint8_t*, int);
    auto* expected = static_cast<PreMgr*>(Mem::Calloc(sizeof(PreMgr), nullptr, 1));
    auto* actual = static_cast<PreMgr*>(Mem::Calloc(sizeof(PreMgr), nullptr, 1));
    Check(Original<CtorFn>(0x005B8D20)(expected) == expected && slot == expected, "PreMgr::PreMgr (original)");
    new (actual) PreMgr();
    Check(slot == actual && actual->m_pres && !actual->m_activeHandle && !actual->m_activeData &&
              !actual->m_missingHandle && actual->m_numPending == 0 && expected->m_numPending == 0,
          "PreMgr::PreMgr");

    std::vector<uint8_t> source;
    for (PreMgr* m : {expected, actual}) {
        m->m_pending[0] = {11, "levels\\one.pre"};
        m->m_pending[1] = {22, "two.pre"};
        m->m_pending[2] = {33, "never.pre"};
        m->m_numPending = 3;
    }
    auto pending_same = [&] {
        bool same = expected->m_numPending == actual->m_numPending;
        for (int i = 0; same && i < expected->m_numPending; ++i)
            same = expected->m_pending[i].handle == actual->m_pending[i].handle &&
                   std::strcmp(expected->m_pending[i].name, actual->m_pending[i].name) == 0;
        return same;
    };
    Original<FinishFn>(0x005B8DB0)(expected, 22, BuildFakeArchive(source, "two/"), 0);
    actual->FinishLoad(22, BuildFakeArchive(source, "two/"), 0);
    Check(pending_same(), "PreMgr::FinishLoad");
    Original<FinishFn>(0x005B8DB0)(expected, 11, BuildFakeArchive(source, "one/"), 0);
    actual->FinishLoad(11, BuildFakeArchive(source, "one/"), 0);
    Check(pending_same(), "PreMgr::FinishLoad (second)");
    {
        // Not a pending handle: nothing happens (and the block stays ours to free).
        uint8_t* stray = BuildFakeArchive(source, "x/");
        Original<FinishFn>(0x005B8DB0)(expected, 99, stray, 0);
        actual->FinishLoad(99, stray, 0);
        Check(pending_same() && expected->m_pres->m_size == actual->m_pres->m_size, "PreMgr::FinishLoad (unknown)");
        Mem::Free(stray);
    }

    using NameBoolFn = bool(__thiscall*)(PreMgr*, const char*);
    for (const char* name : {"two.pre", "levels\\one.pre", "never.pre", "TWO.PRE"}) {
        Check(Original<NameBoolFn>(0x005B8540)(expected, name) == actual->IsLoaded(name), "PreMgr::IsLoaded");
        Check(Original<NameBoolFn>(0x005B7FD0)(expected, name) == actual->IsLoadFinished(name),
              "PreMgr::IsLoadFinished");
    }

    // --- finding files
    using LoadFileFn = uint8_t*(__thiscall*)(PreMgr*, const char*, int*, uint8_t*);
    using OpenFileFn = Info*(__thiscall*)(PreMgr*, const char*);
    using ContainedFn = uint8_t*(__thiscall*)(PreMgr*, Info*);
    const char* files[] = {"one/scripts/a.qb", "two/models/b.mdl", "two\\empty.txt", "one/qb/levelscripts.pre",
                           "missing.dat"};
    for (const char* name : files) {
        uint8_t out_e[800] = {}, out_a[800] = {};
        int size_e = -1, size_a = -1;
        uint8_t* r_e = Original<LoadFileFn>(0x005B8750)(expected, name, &size_e, out_e);
        uint8_t* r_a = actual->LoadFile(name, &size_a, out_a);
        Check((r_e == nullptr) == (r_a == nullptr) && size_e == size_a && std::memcmp(out_e, out_a, 800) == 0,
              "PreMgr::LoadFile");
        Check(Original<NameBoolFn>(0x005B8390)(expected, name) == actual->FileExists(name), "PreMgr::FileExists");
        Info* h_e = Original<OpenFileFn>(0x005B8AD0)(expected, name);
        Info* h_a = actual->OpenFile(name);
        Check(DescribeActive(expected, h_e) == DescribeActive(actual, h_a), "PreMgr::OpenFile");
        if (h_e && h_a) {
            Info foreign = {};
            for (Info* pair : {static_cast<Info*>(nullptr), &foreign, &foreign}) {
                uint8_t* d_e = Original<ContainedFn>(0x005B84A0)(expected, pair ? pair : h_e);
                uint8_t* d_a = actual->GetContainedFile(pair ? pair : h_a);
                Check((d_e == nullptr) == (d_a == nullptr) && (expected->m_missingHandle == &foreign) ==
                                                                  (actual->m_missingHandle == &foreign),
                      "PreMgr::GetContainedFile");
            }
        }
    }

    // --- the File:: side, through the global manager slot: run the same calls on each manager
    auto run_hooks = [&](PreMgr* m, bool original) {
        using ExistFn = bool(__cdecl*)(const char*);
        using OpenFn = Info*(__cdecl*)(const char*, const char*, bool);
        using CloseFn = int(__cdecl*)(Info*, bool);
        using ReadFn = int(__cdecl*)(void*, int, int, Info*);
        using WriteFn = int(__cdecl*)(const void*, int, int, Info*);
        using SeekFn = int(__cdecl*)(Info*, int, int);
        using SizeFn = int(__cdecl*)(Info*);
        slot = m;
        std::vector<std::string> trace;
        auto note = [&](const std::string& what, int value) {
            trace.push_back(what + "=" + std::to_string(value) + (handled ? " handled" : " not handled"));
        };
        for (const char* name : files) {
            note("exist", original ? Original<ExistFn>(0x005B85A0)(name) : Pre::Exist(name));
            Info* h = original ? Original<OpenFn>(0x005B8C70)(name, "rb", true) : Pre::Open(name, "rb", true);
            trace.push_back(std::string("open ") + DescribeActive(m, h) + (handled ? " handled" : ""));
            if (!h)
                continue;
            note("size", original ? Original<SizeFn>(0x005B86E0)(h) : Pre::GetFileSize(h));
            note("seek", original ? Original<SeekFn>(0x005B86A0)(h, 3, 0) : Pre::Seek(h, 3, 0));
            uint8_t buffer[16] = {};
            note("read", original ? Original<ReadFn>(0x005B8630)(buffer, 2, 5, h) : Pre::Read(buffer, 2, 5, h));
            trace.push_back(std::string(reinterpret_cast<char*>(buffer), sizeof(buffer)));
            note("write", original ? Original<WriteFn>(0x005B8680)(buffer, 1, 1, h) : Pre::Write(buffer, 1, 1, h));
            note("close", original ? Original<CloseFn>(0x005B85D0)(h, true) : Pre::Close(h, true));
            trace.push_back("open count " + std::to_string(m->m_activePre->m_openCount));
            Info* w = original ? Original<OpenFn>(0x005B8C70)(name, "wb", true) : Pre::Open(name, "wb", true);
            trace.push_back(std::string("open for writing ") + (w ? "?" : "refused") + (handled ? " handled" : ""));
        }
        return trace;
    };
    std::vector<std::string> trace_expected = run_hooks(expected, true);
    Check(trace_expected == run_hooks(actual, false), "Pre::Exist/Open/Read/Seek/GetFileSize/Write/Close");

    // --- unloading
    using UnloadFn = void(__thiscall*)(PreMgr*, const char*, bool);
    const std::pair<const char*, bool> unloads[] = {{"nothere.pre", true}, {"nothere.pre", false}, {"two.pre", true}};
    for (const auto& [name, only_if_loaded] : unloads) {
        Original<UnloadFn>(0x005B9570)(expected, name, only_if_loaded);
        actual->UnloadPre(name, only_if_loaded);
        Check(expected->m_pres->m_size == actual->m_pres->m_size &&
                  Original<NameBoolFn>(0x005B8540)(expected, "two.pre") == actual->IsLoaded("two.pre"),
              "PreMgr::UnloadPre");
    }
    actual->m_blockPreLoading = expected->m_blockPreLoading = true;
    Original<UnloadFn>(0x005B9570)(expected, "levels\\one.pre", true);
    actual->UnloadPre("levels\\one.pre", true);
    Check(expected->m_pres->m_size == 1 && actual->m_pres->m_size == 1, "PreMgr::UnloadPre (blocked)");

    // Clean up: the remaining archive, the tables, the managers.
    for (PreMgr* m : {expected, actual}) {
        m->m_blockPreLoading = false;
        m->UnloadPre("levels\\one.pre", true);
        m->m_pres->~HashTable();
        Mem::Free(m->m_pres);
        Mem::Free(m);
    }
}

// LoadPre needs the script system (it reads the block_pre_loading global), so this runs once the engine
// has started. A small real archive is loaded (synchronously) by an original and a rewritten manager of its
// own, and the results compared; the managers and archives are freed and the globals restored afterwards.
void TestLoadPreEngineStarted()
{
    using Pre::PreFile;
    using Pre::PreMgr;
    using CtorFn = PreMgr*(__thiscall*)(void*);
    using LoadPreFn = void(__thiscall*)(PreMgr*, const char*, bool, bool, bool);
    SavePreGlobals();

    auto* expected = static_cast<PreMgr*>(Mem::Calloc(sizeof(PreMgr), nullptr, 1));
    auto* actual = static_cast<PreMgr*>(Mem::Calloc(sizeof(PreMgr), nullptr, 1));
    Original<CtorFn>(0x005B8D20)(expected);
    new (actual) PreMgr();

    const char* name = "Themes.pre";
    Original<LoadPreFn>(0x005B8EE0)(expected, name, false, true, false);
    actual->LoadPre(name, false, true, false);

    auto* pre_e = static_cast<PreFile*>(expected->m_pres->GetItem(Crc::GenerateCRCFromString(name)));
    auto* pre_a = static_cast<PreFile*>(actual->m_pres->GetItem(Crc::GenerateCRCFromString(name)));
    bool loaded = pre_e && pre_a && expected->m_blockPreLoading == actual->m_blockPreLoading &&
                  expected->m_pres->m_size == actual->m_pres->m_size;
    Check(loaded || (expected->m_blockPreLoading && actual->m_blockPreLoading), "PreMgr::LoadPre (loaded)");
    if (pre_e && pre_a) {
        int total = *reinterpret_cast<int*>(pre_e->m_block);
        Check(pre_e->m_numFiles == pre_a->m_numFiles && pre_e->m_bottomUpHeap == pre_a->m_bottomUpHeap &&
                  total == *reinterpret_cast<int*>(pre_a->m_block) && total > 0 &&
                  std::memcmp(pre_e->m_block, pre_a->m_block, static_cast<size_t>(total)) == 0,
              "PreMgr::LoadPre (archive contents)");
    }
    // Missing archive, not reported: nothing added.
    Original<LoadPreFn>(0x005B8EE0)(expected, "no_such_archive.pre", false, false, false);
    actual->LoadPre("no_such_archive.pre", false, false, false);
    Check(expected->m_pres->m_size == actual->m_pres->m_size, "PreMgr::LoadPre (missing)");

    for (PreMgr* m : {expected, actual}) {
        m->m_blockPreLoading = false;
        m->UnloadPre(name, true);
        m->UnloadPre("no_such_archive.pre", true);
        m->m_pres->~HashTable();
        Mem::Free(m->m_pres);
        Mem::Free(m);
    }
}

// Pip::LoadPre on real archives (one all-compressed, one mixed): load with the original, keep a copy of the
// repacked block, unload, load with ours and compare byte for byte; then unload again. Late: needs the game's
// C runtime (_stricmp) and memory manager. Skipped if the archive is already loaded.
void TestPipLoadPreLate()
{
    using LoadPreFn = void(__cdecl*)(const char*);
    auto& pres = GameVariable<char* [100]>(0x007CFE18);
    auto find = [&](const char* name) -> char* {
        for (char* pre : pres)
            if (pre && _stricmp(pre, name) == 0)
                return pre;
        return nullptr;
    };
    auto block_size = [](const char* pre) {
        size_t prefix = (std::strlen(pre) + 0x10) & ~size_t(0xF);
        return prefix + static_cast<size_t>(*reinterpret_cast<const int*>(pre + prefix));
    };
    for (const char* name : {"Themes.pre", "ParkEdAnims.pre"}) {
        if (find(name)) {
            Log("self-test: Pip::LoadPre skipped for %s (already loaded)", name);
            continue;
        }
        Original<LoadPreFn>(0x005B75A0)(name);
        char* expected = find(name);
        std::vector<uint8_t> expected_bytes;
        if (expected)
            expected_bytes.assign(expected, expected + block_size(expected));
        Pip::UnloadPre(name);

        Pip::LoadPre(name);
        char* actual = find(name);
        Check(expected && actual && block_size(actual) == expected_bytes.size() &&
                  std::memcmp(actual, expected_bytes.data(), expected_bytes.size()) == 0,
              "Pip::LoadPre (repacked archive)");
        Pip::UnloadPre(name);
        Check(!find(name), "Pip::UnloadPre (after LoadPre)");
    }
}

void TestPoolConstructorLate()
{
    using Mem::Pool;
    using CtorFn = Pool*(__thiscall*)(void*, int, int, const char*);
    const struct {
        int size, count;
        const char* name;
    } cases[] = {{16, 8, "test pool"}, {4, 1, nullptr}, {32, 3, ""}};
    for (const auto& c : cases) {
        alignas(Pool) unsigned char raw_expected[sizeof(Pool)], raw_actual[sizeof(Pool)];
        Pool* expected;
        {
            OriginalCode original(0x005BA420);
            expected = Original<CtorFn>(0x005BA420)(raw_expected, c.size, c.count, c.name);
        }
        Pool* actual = new (raw_actual) Pool(c.size, c.count, c.name);
        bool fields = expected == reinterpret_cast<Pool*>(raw_expected) && expected->m_count == actual->m_count &&
                      expected->m_itemSize == actual->m_itemSize && expected->m_used == actual->m_used &&
                      expected->m_peakUsed == actual->m_peakUsed &&
                      expected->m_end - expected->m_start == actual->m_end - actual->m_start &&
                      std::strcmp(expected->m_name, actual->m_name) == 0;
        // Walk both free lists: same length and the same offsets within their blocks.
        bool lists = true;
        const uint8_t* e = static_cast<const uint8_t*>(expected->m_freeList);
        const uint8_t* a = static_cast<const uint8_t*>(actual->m_freeList);
        for (int steps = 0; (e || a) && steps <= c.count; ++steps) {
            if (!e || !a || e - expected->m_start != a - actual->m_start) {
                lists = false;
                break;
            }
            e = *reinterpret_cast<const uint8_t* const*>(e);
            a = *reinterpret_cast<const uint8_t* const*>(a);
        }
        Check(fields && lists, "Pool::Pool");
        Mem::Free(expected->m_start);
        Mem::Free(actual->m_start);
    }
}

void TestLzssLate()
{
    using EncodeFn = int(__cdecl*)(const uint8_t*, uint8_t*, int, bool);
    using DecodeFn = uint8_t*(__cdecl*)(const uint8_t*, uint8_t*, int);

    // Inputs: empty, tiny, around the 18-byte match limit, repetitive, and pseudo-random text.
    static uint8_t input[6000];
    uint32_t seed = 99;
    for (int i = 0; i < 6000; ++i) {
        seed = seed * 1103515245u + 12345u;
        const char* words[] = {"skate ", "grind ", "kickflip ", "manual ", "revert ", "ollie\n"};
        input[i] = static_cast<uint8_t>(words[(seed >> 16) % 6][i % 5]); // every word has at least 5 chars
    }
    std::memset(input + 4000, 'A', 1500);

    static uint8_t out_expected[7000], out_actual[7000], round_expected[6000], round_actual[6000];
    for (int size : {0, 1, 17, 18, 19, 300, 4096, 6000}) {
        int expected_size, actual_size;
        {
            OriginalCode a(0x00401290), b(0x004013E0), c(0x00401600), d(0x00401700);
            expected_size = Original<EncodeFn>(0x00401700)(input, out_expected, size, false);
        }
        actual_size = Lzss::Encode(input, out_actual, size, false);
        Check(expected_size == actual_size &&
                  std::memcmp(out_expected, out_actual, static_cast<size_t>(actual_size)) == 0,
              "Lzss::Encode");

        uint8_t* expected_end;
        {
            OriginalCode original(0x004014F0);
            expected_end = Original<DecodeFn>(0x004014F0)(out_expected, round_expected, expected_size);
        }
        uint8_t* actual_end = Lzss::Decode(out_actual, round_actual, actual_size);
        Check(std::memcmp(round_expected, round_actual, static_cast<size_t>(size)) == 0, "Lzss::Decode");
        // Callers use the returned end pointer (decompressed size, padding), so it must match too.
        Check(expected_end - round_expected == actual_end - round_actual, "Lzss::Decode (returned end pointer)");
        Check(std::memcmp(input, round_actual, static_cast<size_t>(size)) == 0, "Lzss round trip");
    }
}
// These need the game running far enough for its memory manager to exist, so they run from a one-shot hook
// on ParseNetworkParams (called once by WinMain before the main loop). Originals are reached through
// OriginalCode scopes because the replacements are already installed by then.

void TestMemoryLate()
{
    using NewFn = void*(__cdecl*)(unsigned int);
    using FreeFn = void(__cdecl*)(void*);
    using Calloc1 = void*(__cdecl*)(unsigned int);
    using Calloc2 = void*(__cdecl*)(unsigned int, int);
    using Calloc3 = void*(__cdecl*)(unsigned int, Mem::Heap*, int);

    auto all_zero = [](const void* p, unsigned int size) {
        const auto* bytes = static_cast<const unsigned char*>(p);
        for (unsigned int i = 0; i < size; ++i)
            if (bytes[i])
                return false;
        return true;
    };
    auto release_original = [](void* p) {
        OriginalCode original(0x00401000);
        Original<FreeFn>(0x00401000)(p);
    };

    for (unsigned int size : {1u, 16u, 100u, 4096u}) {
        void* a;
        {
            OriginalCode original(0x004015D0);
            a = Original<NewFn>(0x004015D0)(size);
        }
        void* b = Mem::New(size);
        Check(a && b && a != b, "Mem::New");
        release_original(a);
        Mem::Free(b);

        // Dirty some memory first so a missing clear would show up.
        void* dirty = Mem::New(size);
        std::memset(dirty, 0xCD, size);
        Mem::Free(dirty);

        void* c1;
        void* c2;
        void* c3;
        {
            OriginalCode o1(0x00401CC0), o2(0x00401D00), o3(0x00401D40);
            c1 = Original<Calloc1>(0x00401CC0)(size);
            c2 = Original<Calloc2>(0x00401D00)(size, 1);
            c3 = Original<Calloc3>(0x00401D40)(size, nullptr, 1);
        }
        void* d1 = Mem::Calloc(size);
        void* d2 = Mem::Calloc(size, 1);
        void* d3 = Mem::Calloc(size, nullptr, 1);
        Check(c1 && c2 && c3 && all_zero(c1, size) && all_zero(c2, size) && all_zero(c3, size),
              "Mem::Calloc (original, sanity)");
        Check(d1 && d2 && d3 && all_zero(d1, size) && all_zero(d2, size) && all_zero(d3, size), "Mem::Calloc");
        for (void* p : {c1, c2, c3, d1, d2, d3})
            Mem::Free(p);
    }
}

// Gives the tests access to String's protected CopyRange.
struct StringAccess : Str::String {
    using Str::String::CopyRange;
};

void TestStringLate()
{
    using Str::String;
    using Ctor = String*(__thiscall*)(void*);
    using TextCtor = String*(__thiscall*)(void*, const char*);
    using CopyCtor = String*(__thiscall*)(void*, const String*);
    using Dtor = void(__thiscall*)(String*);
    using AssignText = String*(__thiscall*)(String*, const char*);
    using AssignString = String*(__thiscall*)(String*, const String*);
    using CopyRangeFn = void(__thiscall*)(String*, const char*, int, int);

    int& live = GameVariable<int>(0x00690FA0);
    auto same = [](const String& a, const String& b) {
        bool text = (!a.m_buffer && !b.m_buffer) || (a.m_buffer && b.m_buffer && strcmp(a.m_buffer, b.m_buffer) == 0);
        bool capacity = !a.m_buffer || a.m_capacity == b.m_capacity;
        return text && capacity;
    };

    alignas(String) unsigned char raw_a[sizeof(String)], raw_b[sizeof(String)];
    auto& a = *reinterpret_cast<String*>(raw_a);
    auto& b = *reinterpret_cast<String*>(raw_b);
    const int live_at_start = live;

    const char* texts[] = {nullptr, "", "a", "abc", "Hello, World", "a much longer string than the others"};
    for (const char* text : texts) {
        int before = live;
        {
            OriginalCode original(0x00401F50);
            Original<TextCtor>(0x00401F50)(raw_a, text);
        }
        new (raw_b) String(text);
        Check(same(a, b) && a.m_serial == before && b.m_serial == before + 1 && live == before + 2,
              "String::String(const char*)");

        // Walk both through the same sequence of assignments.
        for (const char* next : texts) {
            {
                OriginalCode original(0x00401ED0);
                Original<AssignText>(0x00401ED0)(&a, next);
            }
            b = next;
            Check(same(a, b), "String::operator=(const char*)");
        }

        alignas(String) unsigned char raw_c[sizeof(String)], raw_d[sizeof(String)];
        {
            OriginalCode original(0x00401FA0);
            Original<CopyCtor>(0x00401FA0)(raw_c, &a);
        }
        new (raw_d) String(b);
        auto& c = *reinterpret_cast<String*>(raw_c);
        auto& d = *reinterpret_cast<String*>(raw_d);
        Check(same(c, d), "String::String(const String&)");

        String source("source text");
        {
            OriginalCode original(0x00401F10);
            Original<AssignString>(0x00401F10)(&c, &source);
        }
        d = source;
        Check(same(c, d), "String::operator=(const String&)");

        const char* range_text = "0123456789";
        const int ranges[][2] = {{0, 9}, {3, 5}, {4, 4}, {6, 2}, {10, 12}};
        for (const auto& r : ranges) {
            {
                OriginalCode original(0x00401DD0);
                Original<CopyRangeFn>(0x00401DD0)(&c, range_text, r[0], r[1]);
            }
            static_cast<StringAccess&>(d).CopyRange(range_text, r[0], r[1]);
            Check(same(c, d), "String::CopyRange");
        }

        int before_destroy = live;
        {
            OriginalCode original(0x00401DA0);
            Original<Dtor>(0x00401DA0)(&c);
        }
        d.~String();
        Check(live == before_destroy - 2, "String::~String");

        {
            OriginalCode original(0x00401DA0);
            Original<Dtor>(0x00401DA0)(&a);
        }
        b.~String();
    }

    {
        int before = live;
        {
            OriginalCode original(0x00401D80);
            Original<Ctor>(0x00401D80)(raw_a);
        }
        new (raw_b) String();
        Check(!a.m_buffer && !b.m_buffer && a.m_serial == before && b.m_serial == before + 1, "String::String()");
        a.~String();
        b.~String();
    }
    Check(live == live_at_start, "String live count restored");
}

} // namespace

// --- registration (each stage runs its tests in this order) ---------------------------------------------

SELFTEST(Early, TestCrc);
SELFTEST(Early, TestVector);
SELFTEST(Early, TestMatrix);
SELFTEST(Early, TestBitStream);
SELFTEST(Early, TestStringsAndMath);
SELFTEST(Early, TestBatch4);
SELFTEST(Early, TestBatch5);
SELFTEST(Early, TestBatch6);
SELFTEST(Early, TestBatch7);
SELFTEST(Early, TestBitStreamReadWrite);
SELFTEST(Early, TestBatch9);
SELFTEST(Early, TestBatch10);
SELFTEST(Early, TestBatch11);
SELFTEST(Early, TestBatch12);
SELFTEST(Early, TestBatch13);
SELFTEST(Early, TestRotateToPlane);
SELFTEST(Early, TestBatch14);
SELFTEST(Early, TestBatch15);
SELFTEST(Early, TestBatch16);
SELFTEST(Early, TestBatch17);
SELFTEST(Early, TestBatch18);
SELFTEST(Early, TestPool);
SELFTEST(Early, TestPip, RestorePipTables);
SELFTEST(Early, TestFile);

SELFTEST(Late, TestMemoryLate);
SELFTEST(Late, TestStringLate);
SELFTEST(Late, TestLzssLate);
SELFTEST(Late, TestPoolConstructorLate);
SELFTEST(Late, TestHashTableLate);
SELFTEST(Late, TestPipNamesLate, RestorePipTables);
SELFTEST(Late, TestPreFileLate);
SELFTEST(Late, TestPipLoadPreLate);
SELFTEST(Late, TestPreMgrLate, RestorePreGlobals);

SELFTEST(EngineStarted, TestPipLoad);
SELFTEST(EngineStarted, TestLoadPreEngineStarted, RestorePreGlobals);
