#include "hook/selftest.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include <initializer_list>
#include <new>

#include "game/core/crc.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/core/string.h"
#include "game/mth/geometry.h"
#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/random.h"
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
        CheckVector(expected, Mth::CrossProduct(a, b), "CrossProduct");

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
            Vector expected = v, actual = v;
            Original<RotateFn>(0x004060F0)(&expected, &n);
            actual.RotateToPlane(n);
            CheckVector(expected, actual, "Vector::RotateToPlane");
        }
    }
}

// --- late self-tests ----------------------------------------------------------------------------------

void TestLzssLate()
{
    using EncodeFn = int(__cdecl*)(const uint8_t*, uint8_t*, int, bool);
    using DecodeFn = void(__cdecl*)(const uint8_t*, uint8_t*, int);

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

        {
            OriginalCode original(0x004014F0);
            Original<DecodeFn>(0x004014F0)(out_expected, round_expected, expected_size);
        }
        Lzss::Decode(out_actual, round_actual, actual_size);
        Check(std::memcmp(round_expected, round_actual, static_cast<size_t>(size)) == 0, "Lzss::Decode");
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
    int failures = g_failures - g_late_failures_before;
    Log(failures ? "late self-tests: %d FAILED" : "late self-tests: all passed", failures);
}

// One-shot hook: runs the late self-tests, removes itself and continues into the real ParseNetworkParams.
void __cdecl LateSelfTestHook(void* params)
{
    RestoreOriginal(0x004E1220);
    RunLateSelfTests();
    GameFunction<void(__cdecl*)(void*)>(0x004E1220)(params);
}

} // namespace

REPLACE(0x004E1220, LateSelfTestHook);

void RunSelfTests()
{
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
    Log(g_failures ? "self-tests: %d FAILED" : "self-tests: all passed", g_failures);
}
