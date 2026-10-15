#include "hook/selftest.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
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

int g_failures = 0;

void Check(bool ok, const char* what)
{
    if (!ok) {
        ++g_failures;
        Log("SELFTEST FAIL: %s", what);
    }
}

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

void TestPip()
{
    using Pip::LoadedFile;
    using Pip::PreEntry;
    auto& files = GameVariable<LoadedFile[200]>(0x007CF198);
    auto& pres = GameVariable<char* [100]>(0x007CFE18);

    // Runs before the game has loaded anything, but save and restore the tables anyway.
    static LoadedFile saved_files[200];
    static char* saved_pres[100];
    std::memcpy(saved_files, files, sizeof(files));
    std::memcpy(saved_pres, pres, sizeof(pres));
    std::memset(files, 0, sizeof(files));
    std::memset(pres, 0, sizeof(pres));

    // Two fake archives laid out like loaded .pre files: name, header {size, version, count}, entries.
    alignas(16) static uint8_t archive_a[512], archive_b[512];
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
    pres[3] = build(archive_a, "skater.pre", {{10, 0, 8, 0, 0x111}, {40, 21, 13, 2, 0x222}, {7, 0, 5, 0, 0x333}});
    pres[50] = build(archive_b, "levelname_scripts.pre", {{100, 0, 20, 0, 0x444}, {3, 0, 1, 0, 0x222}});
    files[7] = {reinterpret_cast<void*>(0x1000), 0x555, 1234, 3};
    files[120] = {reinterpret_cast<void*>(0x2000), 0x222, 99, 2};

    using FindFn = PreEntry*(__cdecl*)(uint32_t);
    using SizeFn = int(__cdecl*)(uint32_t);
    using SizeByNameFn = int(__cdecl*)(const char*);
    using NextFn = const char*(__cdecl*)(const char*);
    using NameBoolFn = bool(__cdecl*)(const char*);
    using UnloadFn = void(__cdecl*)(uint32_t);
    using UnloadByNameFn = void(__cdecl*)(const char*);

    for (uint32_t c : {0x111u, 0x222u, 0x333u, 0x444u, 0x555u, 0x999u}) {
        Check(Original<FindFn>(0x005B7180)(c) == Pip::FindPreEntry(c), "Pip::FindPreEntry");
        Check(Original<SizeFn>(0x005B7290)(c) == Pip::GetFileSize(c), "Pip::GetFileSize");
    }
    for (const char* name : {"skater.pre", "SKATER.PRE", "levelname_scripts.pre", "missing.pre"}) {
        Check(Original<NameBoolFn>(0x005B7370)(name) == Pip::PreFileIsInUse(name), "Pip::PreFileIsInUse");
        Check(Original<NextFn>(0x005B7300)(name) == Pip::GetNextLoadedPre(name), "Pip::GetNextLoadedPre");
        Check(Original<SizeByNameFn>(0x005B72E0)(name) == Pip::GetFileSize(name), "Pip::GetFileSize(name)");
    }
    Check(Original<NextFn>(0x005B7300)(nullptr) == Pip::GetNextLoadedPre(nullptr), "Pip::GetNextLoadedPre(null)");
    // Only a name that matches nothing: a match would free the fake archive.
    Check(Original<NameBoolFn>(0x005B7130)("missing.pre") == Pip::UnloadPre("missing.pre"), "Pip::UnloadPre");

    // Unload changes the tables: run the original, keep the result, put the tables back, run ours. Reference
    // counts stay above zero so nothing (fake) gets freed.
    static LoadedFile files_before[200], files_expected[200];
    static uint8_t a_before[512], b_before[512], a_expected[512], b_expected[512];
    auto snapshot = [&](LoadedFile* f, uint8_t* a, uint8_t* b) {
        std::memcpy(f, files, sizeof(files));
        std::memcpy(a, archive_a, 512);
        std::memcpy(b, archive_b, 512);
    };
    auto restore = [&](const LoadedFile* f, const uint8_t* a, const uint8_t* b) {
        std::memcpy(files, f, sizeof(files));
        std::memcpy(archive_a, a, 512);
        std::memcpy(archive_b, b, 512);
    };
    auto unload_case = [&](auto original, auto ours, const char* what) {
        snapshot(files_before, a_before, b_before);
        original();
        snapshot(files_expected, a_expected, b_expected);
        restore(files_before, a_before, b_before);
        ours();
        Check(std::memcmp(files, files_expected, sizeof(files)) == 0 && std::memcmp(archive_a, a_expected, 512) == 0 &&
                  std::memcmp(archive_b, b_expected, 512) == 0,
              what);
    };
    for (uint32_t c : {0x222u, 0x555u, 0x111u, 0x999u})
        unload_case([&] { Original<UnloadFn>(0x005B7200)(c); }, [&] { Pip::Unload(c); }, "Pip::Unload");
    unload_case([] { Original<UnloadByNameFn>(0x005B7270)("whatever.tex"); }, [] { Pip::Unload("whatever.tex"); },
                "Pip::Unload(name)");

    std::memcpy(files, saved_files, sizeof(files));
    std::memcpy(pres, saved_pres, sizeof(pres));
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

    // Paths are relative to the current directory; if data\pre isn't there, a "path not found" error would
    // also go to the fatal error screen.
    DWORD attributes = GetFileAttributesA("data\\pre");
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        Log("self-test: TestFile skipped (no data\\pre folder in the current directory)");
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

void TestPipLoadLate()
{
    if (!GameVariable<void*>(0x007D0FCC)) {
        Log("late self-test: Pip::Load skipped (pre-file manager not created yet)");
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
        Log("late self-test: Pip::Load skipped (%s already loaded)", name);
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

int g_late_failures_before = 0;

void RunLateSelfTests()
{
    g_late_failures_before = g_failures;
    TestMemoryLate();
    TestStringLate();
    TestLzssLate();
    TestPoolConstructorLate();
    TestHashTableLate();
    TestPipLoadLate();
    int failures = g_failures - g_late_failures_before;
    Log(failures ? "late self-tests: %d FAILED" : "late self-tests: all passed", failures);
}

// One-shot hook: runs the late self-tests, removes itself and continues into the real ParseNetworkParams.
void __cdecl LateSelfTestHook(void* params)
{
    RestoreOriginal(0x004E1220);
    if (ConfigInt("selftests", "enabled", 1))
        RunLateSelfTests();
    else
        Log("late self-tests: skipped (thug2-decomp.ini)");
    GameFunction<void(__cdecl*)(void*)>(0x004E1220)(params);
}

} // namespace

REPLACE(0x004E1220, LateSelfTestHook);

void RunSelfTests()
{
    if (!ConfigInt("selftests", "enabled", 1)) {
        Log("self-tests: skipped (thug2-decomp.ini)");
        return;
    }
    TestCrc();
    TestVector();
    TestMatrix();
    TestBitStream();
    TestStringsAndMath();
    TestBatch4();
    TestBatch5();
    TestBatch6();
    TestBatch7();
    TestBitStreamReadWrite();
    TestBatch9();
    TestBatch10();
    TestBatch11();
    TestBatch12();
    TestBatch13();
    TestRotateToPlane();
    TestBatch14();
    TestBatch15();
    TestBatch16();
    TestBatch17();
    TestBatch18();
    TestPool();
    TestPip();
    TestFile();
    Log(g_failures ? "self-tests: %d FAILED" : "self-tests: all passed", g_failures);
}
