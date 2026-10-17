#include "game/core/stubs.h"
#include "game/mc/memcard.h"
#include "game/lst/search.h"
#include "game/front/screen_element.h"
#include "game/obj/smtptr.h"
#include "game/sio/device.h"
#include "game/sys/registry.h"
#include "game/sys/timer.h"
#include "game/script/eval.h"
#include "game/script/load.h"
#include "game/script/names.h"
#include "game/script/parse.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "hook/selftest.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

#include <initializer_list>
#include <new>

#include <windows.h>

#include "game/core/crc.h"
#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/str.h"
#include "game/core/string.h"
#include "game/file/asset_bundle.h"
#include "game/file/async.h"
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
#include "game/sio/sdl_input.h"
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

// --- async file system (pool and callback queue), with fake handles -------------------------------------

struct AsyncGlobals {
    File::AsyncFileHandle* pool[16];
    int in_use, busy;
    bool completed;
    uint8_t lists[2 * 16 * 12];
    int counts[2], current;
};
AsyncGlobals g_saved_async;
bool g_async_saved = false;

AsyncGlobals ReadAsyncGlobals()
{
    AsyncGlobals g;
    std::memcpy(g.pool, &GameVariable<File::AsyncFileHandle* [16]>(0x007CF138), sizeof(g.pool));
    g.in_use = GameVariable<int>(0x007CF178);
    g.busy = GameVariable<int>(0x007CF17C);
    g.completed = GameVariable<bool>(0x007CF180);
    std::memcpy(g.lists, &GameVariable<uint8_t[2 * 16 * 12]>(0x007CEFB8), sizeof(g.lists));
    std::memcpy(g.counts, &GameVariable<int[2]>(0x007CF184), sizeof(g.counts));
    g.current = GameVariable<int>(0x007CF18C);
    return g;
}

void WriteAsyncGlobals(const AsyncGlobals& g)
{
    std::memcpy(&GameVariable<File::AsyncFileHandle* [16]>(0x007CF138), g.pool, sizeof(g.pool));
    GameVariable<int>(0x007CF178) = g.in_use;
    GameVariable<int>(0x007CF17C) = g.busy;
    GameVariable<bool>(0x007CF180) = g.completed;
    std::memcpy(&GameVariable<uint8_t[2 * 16 * 12]>(0x007CEFB8), g.lists, sizeof(g.lists));
    std::memcpy(&GameVariable<int[2]>(0x007CF184), g.counts, sizeof(g.counts));
    GameVariable<int>(0x007CF18C) = g.current;
}

void RestoreAsyncGlobals()
{
    if (g_async_saved)
        WriteAsyncGlobals(g_saved_async);
    g_async_saved = false;
}

// A fake platform handle: its vtable's Init just counts.
struct FakeAsyncMethods {
    bool PlatInit()
    {
        ++reinterpret_cast<File::AsyncFileHandle*>(this)->m_unknown1C;
        return true;
    }
};
std::vector<std::string>* g_async_trace = nullptr;
alignas(16) uint8_t g_fake_handles[2][16][0xA0];

void __cdecl RecordAsyncCallback(File::AsyncFileHandle* file, int event, int value, void* arg0, void* arg1)
{
    int set = reinterpret_cast<uint8_t*>(file) >= g_fake_handles[1][0] ? 1 : 0;
    int index = static_cast<int>((reinterpret_cast<uint8_t*>(file) - g_fake_handles[set][0]) / 0xA0);
    char line[96];
    sprintf_s(line, "callback handle %d event %d value %d args %p %p", index, event, value, arg0, arg1);
    g_async_trace->push_back(line);
}

void TestAsyncFileSystem()
{
    using File::AsyncFileHandle;
    using GetHandleFn = AsyncFileHandle*(__cdecl*)();
    using FreeHandleFn = bool(__cdecl*)(AsyncFileHandle*);
    using AddCallbackFn = void(__cdecl*)(AsyncFileHandle*, int, int);
    using ExecuteFn = void(__cdecl*)();
    using IoCallbackFn = void(__thiscall*)(AsyncFileHandle*, int, bool, int, int);
    using SetCallbackFn = void(__thiscall*)(AsyncFileHandle*, File::AsyncCallback, void*, void*);
    using IsBusyFn = bool(__thiscall*)(AsyncFileHandle*);
    using BusyFn = bool(__cdecl*)();

    g_saved_async = ReadAsyncGlobals();
    g_async_saved = true;
    static void* fake_vtable[19] = {};
    fake_vtable[AsyncFileHandle::kPlatInit] = CodeAddress(&FakeAsyncMethods::PlatInit);

    auto run = [&](int set, bool original) {
        std::vector<std::string> trace;
        g_async_trace = &trace;
        std::memset(g_fake_handles[set], 0, sizeof(g_fake_handles[set]));
        AsyncGlobals g = {};
        for (int i = 0; i < 16; ++i) {
            auto* file = reinterpret_cast<AsyncFileHandle*>(g_fake_handles[set][i]);
            file->m_vtable = fake_vtable;
            g.pool[i] = file;
        }
        WriteAsyncGlobals(g);
        auto index_of = [&](AsyncFileHandle* file) {
            return file ? static_cast<int>((reinterpret_cast<uint8_t*>(file) - g_fake_handles[set][0]) / 0xA0) : -1;
        };
        auto get = [&] { return original ? Original<GetHandleFn>(0x005B6AE0)() : File::AsyncGetHandle(); };
        auto note_state = [&](const char* step) {
            AsyncGlobals now = ReadAsyncGlobals();
            std::string line = std::string(step) + ": in use " + std::to_string(now.in_use) + " busy " +
                               std::to_string(now.busy) + " lists " + std::to_string(now.counts[0]) + "/" +
                               std::to_string(now.counts[1]) + " current " + std::to_string(now.current) + " pool";
            for (AsyncFileHandle* file : now.pool) {
                line += " " + std::to_string(index_of(file)) + ":" + std::to_string(file->m_busyCount) + "/" +
                        std::to_string(file->m_operation) + "/" + std::to_string(file->m_result) + "/" +
                        std::to_string(file->m_unknown1C) + "/" + std::to_string(file->m_fileSize);
            }
            trace.push_back(line);
        };

        AsyncFileHandle* a = get();
        AsyncFileHandle* b = get();
        AsyncFileHandle* c = get();
        note_state("three handles");
        g.pool[3]->m_busyCount = 1; // the next free one is still busy: a later idle one is used instead
        AsyncFileHandle* d = get();
        trace.push_back("fourth " + std::to_string(index_of(d)));
        note_state("past a busy handle");
        bool freed = original ? Original<FreeHandleFn>(0x005B6590)(b) : File::AsyncFreeHandle(b);
        trace.push_back(std::string("freed ") + (freed ? "yes" : "no"));
        note_state("freed one");

        for (AsyncFileHandle* file : {a, c}) {
            if (original)
                Original<SetCallbackFn>(0x005B6380)(file, RecordAsyncCallback, file, &trace);
            else
                file->SetCallback(RecordAsyncCallback, file, &trace);
        }
        a->m_busyCount = 2;
        c->m_busyCount = 1;
        d->m_busyCount = 1;
        GameVariable<int>(0x007CF17C) = 4;
        auto io = [&](AsyncFileHandle* file, int event, bool error, int value) {
            if (original)
                Original<IoCallbackFn>(0x005B69F0)(file, event, error, value, 0);
            else
                file->IoCallback(event, error, value, 0);
        };
        io(a, 3, false, 77);  // queued: a has a callback
        io(d, 1, false, 5);   // no callback: counted done now
        io(c, 6, true, -1);   // an error: no callback
        if (original)
            Original<AddCallbackFn>(0x005B65D0)(a, 1, 9);
        else
            File::AsyncAddCallback(a, 1, 9);
        c->m_callbackMask = 1u << 3;
        c->m_busyCount = 1;
        io(c, 1, false, 3);   // not in c's mask
        note_state("completions");
        trace.push_back(std::string("busy ") + ((original ? Original<BusyFn>(0x005B6580)() : File::AsyncBusy()) ? "yes" : "no") +
                        " a " + ((original ? Original<IsBusyFn>(0x005B6730)(a) : a->IsBusy()) ? "yes" : "no"));
        if (original)
            Original<ExecuteFn>(0x005B68C0)();
        else
            File::AsyncExecuteCallbacks();
        note_state("after callbacks");
        g_async_trace = nullptr;
        // The callbacks recorded their own pointers as arg0: compare them as indices instead.
        for (std::string& line : trace) {
            for (int i = 0; i < 16; ++i) {
                char pointer[32];
                sprintf_s(pointer, "%p", static_cast<void*>(g_fake_handles[set][i]));
                for (size_t at; (at = line.find(pointer)) != std::string::npos;)
                    line.replace(at, std::strlen(pointer), "handle" + std::to_string(i));
            }
            char trace_pointer[32];
            sprintf_s(trace_pointer, "%p", static_cast<void*>(&trace));
            for (size_t at; (at = line.find(trace_pointer)) != std::string::npos;)
                line.replace(at, std::strlen(trace_pointer), "trace");
        }
        return trace;
    };
    std::vector<std::string> expected = run(0, true);
    std::vector<std::string> actual = run(1, false);
    Check(expected == actual, "File::Async pool and callbacks");
    if (expected != actual) {
        for (size_t i = 0; i < expected.size() || i < actual.size(); ++i) {
            if (i >= expected.size() || i >= actual.size() || expected[i] != actual[i])
                Log("  async step %u: original '%s' ours '%s'", static_cast<unsigned>(i),
                    i < expected.size() ? expected[i].c_str() : "-", i < actual.size() ? actual[i].c_str() : "-");
        }
    }
}

// The PC async handle on a real file: an original handle (original base methods, the game's PC vtable) and
// ours (our base methods, a copy of that vtable pointing at our PC methods) open, read and close
// pre\Themes.prx, synchronously and with overlapped I/O; results, sizes, counters and data are compared.
bool g_saved_overlapped = false;
int g_saved_async_busy = 0;
bool g_async_io_saved = false;

void RestoreAsyncIoState()
{
    if (!g_async_io_saved)
        return;
    File::AsyncUseOverlappedIo() = g_saved_overlapped;
    GameVariable<int>(0x007CF17C) = g_saved_async_busy;
    g_async_io_saved = false;
}

void TestPcAsyncFileHandle()
{
    using File::AsyncFileHandle;
    using CtorFn = AsyncFileHandle*(__thiscall*)(void*);
    using OpenFn = bool(__thiscall*)(AsyncFileHandle*, const char*, bool, int);
    using ReadFn = int(__thiscall*)(AsyncFileHandle*, void*, int, int);
    using CloseFn = bool(__thiscall*)(AsyncFileHandle*);
    using SizeFn = int(__thiscall*)(AsyncFileHandle*);

    DWORD attributes = GetFileAttributesA("data\\pre\\Themes.prx");
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        Log("self-test: TestPcAsyncFileHandle skipped (data\\pre\\Themes.prx not found)");
        return;
    }
    g_saved_overlapped = File::AsyncUseOverlappedIo();
    g_saved_async_busy = GameVariable<int>(0x007CF17C);
    g_async_io_saved = true;

    // Our handles' vtable: the game's PC vtable with our methods in the slots we rewrote.
    static void* our_vtable[19];
    std::memcpy(our_vtable, reinterpret_cast<void*>(0x006583D8), sizeof(our_vtable));
    File::PcAsyncMethods ours = File::PcAsyncFileHandleMethods();
    our_vtable[AsyncFileHandle::kIoCallback] = ours.io_callback;
    our_vtable[AsyncFileHandle::kPlatInit] = ours.init;
    our_vtable[AsyncFileHandle::kPlatOpen] = ours.open;
    our_vtable[AsyncFileHandle::kPlatClose] = ours.close;
    our_vtable[AsyncFileHandle::kPlatIsDone] = ours.is_done;
    our_vtable[AsyncFileHandle::kPlatIsBusy] = ours.is_busy;
    our_vtable[AsyncFileHandle::kPlatIsEOF] = ours.is_eof;
    our_vtable[AsyncFileHandle::kPlatLoad] = ours.load;
    our_vtable[AsyncFileHandle::kPlatRead] = ours.read;

    // Unbuffered reads need sector-aligned memory.
    auto* buffer = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    alignas(16) static uint8_t handles[2][0xA0];

    auto run = [&](bool original, bool overlapped) {
        std::vector<std::string> trace;
        File::AsyncUseOverlappedIo() = overlapped;
        GameVariable<int>(0x007CF17C) = 0;
        auto* file = reinterpret_cast<AsyncFileHandle*>(handles[original ? 0 : 1]);
        std::memset(file, 0, 0xA0);
        if (original)
            Original<CtorFn>(0x005B69A0)(file);
        else
            File::ConstructAsyncFileHandle(file);
        file->m_vtable = original ? reinterpret_cast<void**>(0x006583D8) : our_vtable;
        reinterpret_cast<HANDLE*>(handles[original ? 0 : 1] + 0x88)[0] = INVALID_HANDLE_VALUE;
        file->Virtual<bool>(AsyncFileHandle::kPlatInit);

        auto note = [&](const char* step, int value) {
            trace.push_back(std::string(step) + " " + std::to_string(value) + ": result " +
                            // after an open the result is the Windows file handle: only whether it's set
                            std::to_string(step[0] == 'o' || step[0] == 's' ? (file->m_result != 0) : file->m_result) +
                            " size " +
                            std::to_string(file->m_fileSize) + " position " + std::to_string(file->m_position) +
                            " busy " + std::to_string(file->m_busyCount) + "/" +
                            std::to_string(GameVariable<int>(0x007CF17C)) + " operation " +
                            std::to_string(file->m_operation) + " eof " +
                            std::to_string(file->Virtual<bool>(AsyncFileHandle::kPlatIsEOF)) + " pending " +
                            std::to_string(file->Virtual<bool>(AsyncFileHandle::kPlatIsBusy)));
        };
        bool opened = original ? Original<OpenFn>(0x005B67B0)(file, "pre/Themes.prx", true, 100)
                               : file->Open("pre/Themes.prx", true, 100);
        note("open", opened);
        if (!opened)
            return trace;
        int size = original ? Original<SizeFn>(0x005B63A0)(file) : file->GetFileSize();
        note("size", size);
        std::memset(buffer, 0xCD, 0x4000);
        int read = original ? Original<ReadFn>(0x005B6740)(file, buffer, 1, size)
                            : file->Read(buffer, 1, size);
        note("read", read);
        uint32_t sum = 0;
        for (int i = 0; i < 0x4000; ++i)
            sum = sum * 31 + buffer[i];
        trace.push_back("data checksum " + std::to_string(sum));
        bool closed = original ? Original<CloseFn>(0x005B6840)(file) : file->Close();
        note("close", closed);
        return trace;
    };
    for (bool overlapped : {false, true}) {
        std::vector<std::string> expected = run(true, overlapped);
        std::vector<std::string> actual = run(false, overlapped);
        Check(expected == actual && expected.size() > 1,
              overlapped ? "PC async handle (overlapped I/O)" : "PC async handle (synchronous I/O)");
        for (size_t i = 0; i < expected.size() || i < actual.size(); ++i) {
            if (i >= expected.size() || i >= actual.size() || expected[i] != actual[i])
                Log("  pc async step %u: original '%s' ours '%s'", static_cast<unsigned>(i),
                    i < expected.size() ? expected[i].c_str() : "-", i < actual.size() ? actual[i].c_str() : "-");
        }
    }
    VirtualFree(buffer, 0, MEM_RELEASE);
}

// The async loader module's reference counting, with no module (so nothing is created or destroyed): the
// same create/release sequence through the originals and ours must leave the same counts.
void* g_saved_loader_module = nullptr;
uint32_t g_saved_loader_refs = 0;
bool g_loader_module_saved = false;

void RestoreLoaderModule()
{
    if (!g_loader_module_saved)
        return;
    GameVariable<void*>(0x007CF190) = g_saved_loader_module;
    GameVariable<uint32_t>(0x007CF194) = g_saved_loader_refs;
    g_loader_module_saved = false;
}

void TestAsyncLoaderModuleRefs()
{
    g_saved_loader_module = GameVariable<void*>(0x007CF190);
    g_saved_loader_refs = GameVariable<uint32_t>(0x007CF194);
    g_loader_module_saved = true;

    auto run = [](bool original) {
        std::vector<uint32_t> counts;
        GameVariable<void*>(0x007CF190) = nullptr;
        for (uint32_t start : {0u, 1u, 3u}) {
            GameVariable<uint32_t>(0x007CF194) = start;
            for (int step = 0; step < 6; ++step) {
                if (step < 2) {
                    // Returns the module: with none, null (the start-up code keeps it).
                    void* module = original ? Original<void*(__cdecl*)(bool)>(0x005B6CA0)(false)
                                            : File::CreateAsyncLoaderModule(false);
                    counts.push_back(module == nullptr ? 0xFFFFu : reinterpret_cast<uint32_t>(module));
                } else if (original) {
                    Original<void(__cdecl*)()>(0x005B6620)();
                } else {
                    File::DestroyAsyncLoaderModule();
                }
                counts.push_back(GameVariable<uint32_t>(0x007CF194));
            }
        }
        return counts;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Check(expected == actual, "async loader module reference counts");
    Check(GameVariable<void*>(0x007CF190) == nullptr, "async loader module: no module created");
    RestoreLoaderModule();
}

// The asset bundle lookups on a fake bundle: the originals and ours must find the same entries and assets.
void TestAssetBundleLookups()
{
    using File::AssetBundle;
    static AssetBundle bundle;
    std::memset(&bundle, 0, sizeof(bundle));
    bundle.m_count = 5;
    const uint32_t keys[5][2] = {{1, 10}, {2, 10}, {1, 20}, {2, 10}, {3, 30}};
    for (int i = 0; i < 5; ++i) {
        bundle.m_entries[i] = {static_cast<uint32_t>(i * 0x100), static_cast<uint32_t>(i + 1), keys[i][0], keys[i][1]};
        bundle.m_assets[i] = reinterpret_cast<void*>(0x1000 + i);
    }
    bundle.m_loaded = true;

    using FindFn = void*(__thiscall*)(AssetBundle*, uint32_t, uint32_t, int);
    using HasFn = bool(__thiscall*)(AssetBundle*, uint32_t, uint32_t);
    using IndexFn = void*(__thiscall*)(AssetBundle*, int);
    using LoadedFn = bool(__thiscall*)(AssetBundle*);
    bool same = Original<LoadedFn>(0x005B6D50)(&bundle) == bundle.IsLoaded();
    for (int i = 0; i < 5; ++i) {
        same = same && Original<IndexFn>(0x005B6D60)(&bundle, i) == bundle.GetAsset(i);
        same = same && Original<IndexFn>(0x005B6D70)(&bundle, i) == bundle.GetEntry(i);
    }
    for (uint32_t name = 0; name <= 4; ++name) {
        for (uint32_t type : {0u, 10u, 20u, 30u}) {
            same = same && Original<FindFn>(0x005B6D80)(&bundle, name, type, 0) == bundle.FindEntry(name, type, 0);
            same = same && Original<FindFn>(0x005B6DC0)(&bundle, name, type, 0) == bundle.FindAsset(name, type, 0);
            same = same && Original<HasFn>(0x005B6E60)(&bundle, name, type) == bundle.HasAsset(name, type);
        }
    }
    Check(same, "asset bundle lookups");
    Check(bundle.FindAsset(2, 10, 0) == reinterpret_cast<void*>(0x1001) && !bundle.HasAsset(3, 10),
          "asset bundle: first match, name and type both compared");
}

// Splitting a bundle file (made here) into its assets, original vs ours: the same result, entries and asset
// contents; then FreeAsset on both.
void TestAssetBundleSplitLate()
{
    using File::AssetBundle;
    using CtorFn = AssetBundle*(__thiscall*)(void*);
    using HeapsFn = void(__thiscall*)(AssetBundle*, Mem::Heap*, Mem::Heap*);
    using SplitFn = bool(__thiscall*)(AssetBundle*, int, int);
    using FreeFn = bool(__thiscall*)(AssetBundle*, uint32_t, uint32_t);
    using DestroyFn = AssetBundle*(__thiscall*)(AssetBundle*, unsigned int);

    // Header, 3 entries, then the data (sizes not multiples of 4).
    const uint32_t sizes[3] = {13, 40, 7};
    const int count = 3;
    const int dataStart = 8 + count * 16;
    const int fileSize = dataStart + 13 + 40 + 7;
    auto make_file = [&](AssetBundle* bundle) {
        Mem::PushContext(Mem::BottomUpHeap());
        auto* data = static_cast<uint8_t*>(Mem::Malloc(fileSize));
        Mem::PopContext();
        auto* words = reinterpret_cast<uint32_t*>(data);
        words[0] = 0x12345678;
        words[1] = count;
        uint32_t offset = dataStart;
        for (int i = 0; i < count; ++i) {
            uint32_t* entry = words + 2 + i * 4;
            entry[0] = offset;
            entry[1] = sizes[i];
            entry[2] = 0xA0 + i;
            entry[3] = i == 1 ? 0xBB : 0xCC;
            for (uint32_t b = 0; b < sizes[i]; ++b)
                data[offset + b] = static_cast<uint8_t>(i * 50 + b);
            offset += sizes[i];
        }
        bundle->m_fileData = data;
        bundle->m_loadedData = data;
    };

    auto run = [&](bool original) {
        std::vector<std::string> trace;
        auto* memory = Mem::New(sizeof(AssetBundle));
        AssetBundle* bundle = original ? Original<CtorFn>(0x005B6CF0)(memory) : new (memory) AssetBundle;
        if (original)
            Original<HeapsFn>(0x005B6D30)(bundle, Mem::BottomUpHeap(), Mem::TopDownHeap());
        else
            bundle->SetHeaps(Mem::BottomUpHeap(), Mem::TopDownHeap());
        make_file(bundle);
        bool split = original ? Original<SplitFn>(0x005B6EF0)(bundle, 0, fileSize) : bundle->Split(0, fileSize);
        trace.push_back("split " + std::to_string(split) + " count " + std::to_string(bundle->m_count) +
                        " loaded " + std::to_string(bundle->m_loaded) + " file " +
                        std::to_string(bundle->m_fileData != nullptr) + std::to_string(bundle->m_loadedData != nullptr));
        for (int i = 0; i < bundle->m_count; ++i) {
            const AssetBundle::Entry& entry = bundle->m_entries[i];
            std::string line = "entry " + std::to_string(entry.offset) + " " + std::to_string(entry.size) + " " +
                               std::to_string(entry.name) + " " + std::to_string(entry.type) + ":";
            auto* asset = static_cast<uint8_t*>(bundle->m_assets[i]);
            for (uint32_t b = 0; asset && b < entry.size; ++b)
                line += " " + std::to_string(asset[b]);
            trace.push_back(line);
        }
        bool freed = original ? Original<FreeFn>(0x005B6E00)(bundle, 0xA1, 0xBB) : bundle->FreeAsset(0xA1, 0xBB);
        bool again = original ? Original<FreeFn>(0x005B6E00)(bundle, 0xA1, 0xBB) : bundle->FreeAsset(0xA1, 0xBB);
        bool wrong = original ? Original<FreeFn>(0x005B6E00)(bundle, 0xA0, 0xBB) : bundle->FreeAsset(0xA0, 0xBB);
        trace.push_back("free " + std::to_string(freed) + std::to_string(again) + std::to_string(wrong) + " asset1 " +
                        std::to_string(bundle->m_assets[1] != nullptr));
        // Free the rest, then the bundle (the destructor doesn't free assets).
        for (int i = 0; i < bundle->m_count; ++i) {
            if (bundle->m_assets[i])
                Mem::FreeMalloc(bundle->m_assets[i]);
        }
        if (original)
            Original<DestroyFn>(0x005B6EA0)(bundle, 1);
        else
            bundle->Destroy(1);
        return trace;
    };
    std::vector<std::string> expected = run(true);
    std::vector<std::string> actual = run(false);
    Check(expected == actual && expected.size() == 5, "asset bundle split");
    for (size_t i = 0; i < expected.size() || i < actual.size(); ++i) {
        if (i >= expected.size() || i >= actual.size() || expected[i] != actual[i])
            Log("  bundle step %u: original '%s' ours '%s'", static_cast<unsigned>(i),
                i < expected.size() ? expected[i].c_str() : "-", i < actual.size() ? actual[i].c_str() : "-");
    }
}

// --- the allocator layer (regions, allocators, block headers) ---
// Each scenario runs in its own arena, once through the originals and once through ours; the arenas are
// compared with every pointer into the arena turned into an offset.
constexpr size_t kArenaSize = 0x3000;
alignas(16) uint8_t g_alloc_arenas[2][kArenaSize];
int g_fake_free_calls = 0;
uintptr_t g_fake_freed_header = 0;

struct FakeAllocatorFree {
    void Free(Mem::BlockHeader* header)
    {
        ++g_fake_free_calls;
        g_fake_freed_header = reinterpret_cast<uintptr_t>(header);
    }
};

std::vector<uint32_t> RebasedArena(const uint8_t* arena, size_t size)
{
    std::vector<uint32_t> words(size / 4);
    std::memcpy(words.data(), arena, size);
    auto base = reinterpret_cast<uint32_t>(arena);
    for (uint32_t& word : words) {
        if (word >= base && word < base + size)
            word = 0xA0000000u + (word - base);
    }
    return words;
}

uint32_t Rebased(const void* pointer, const uint8_t* arena)
{
    auto value = reinterpret_cast<uint32_t>(pointer);
    auto base = reinterpret_cast<uint32_t>(arena);
    return value >= base && value < base + kArenaSize ? 0xA0000000u + (value - base) : value;
}

void TestAllocatorLayer()
{
    using Mem::Allocator;
    using Mem::Region;
    using RegionCtor = Region*(__thiscall*)(void*, uint8_t*, uint8_t*);
    using AllocatorCtor = Allocator*(__thiscall*)(void*, Region*, int, const char*);
    using RegionAllocate = uint8_t*(__thiscall*)(Region*, Allocator*, int, int);
    using RegionAvailable = int(__thiscall*)(Region*);
    using RegionRemove = void(__thiscall*)(Region*, Allocator*);
    using AllocatorDestroy = void(__thiscall*)(Allocator*);

    static void* fake_vtable[9];
    fake_vtable[8] = CodeAddress(&FakeAllocatorFree::Free);

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        uint8_t* arena = g_alloc_arenas[original ? 0 : 1];
        std::memset(arena, 0xCD, kArenaSize);
        uint8_t* start = arena + 0x1000;
        uint8_t* end = arena + 0x2000;
        void* region_memory = arena;
        void* up_memory = arena + 0x100;
        void* down_memory = arena + 0x200;
        const char* name = "test";

        Region* region = original ? Original<RegionCtor>(0x005BC5F0)(region_memory, start, end)
                                  : new (region_memory) Region(start, end);
        trace.push_back(Rebased(region, arena));
        Allocator* up = original ? Original<AllocatorCtor>(0x005BA260)(up_memory, region, 1, name)
                                 : new (up_memory) Allocator(region, 1, name);
        Allocator* down = original ? Original<AllocatorCtor>(0x005BA260)(down_memory, region, -1, name)
                                   : new (down_memory) Allocator(region, -1, name);
        trace.push_back(Rebased(up, arena));
        trace.push_back(Rebased(down, arena));

        auto allocate = [&](Allocator* allocator, int size) {
            uint8_t* result = original ? Original<RegionAllocate>(0x005BC560)(region, allocator, size, 0)
                                       : region->Allocate(allocator, size, 0);
            trace.push_back(Rebased(result, arena));
            int available = original ? Original<RegionAvailable>(0x005BC530)(region) : region->MemAvailable();
            trace.push_back(static_cast<uint32_t>(available));
        };
        allocate(up, 0x100);
        allocate(down, 0x200);
        allocate(up, 0x2000);  // too big: fails
        allocate(down, 0xE01); // one byte too many: fails
        allocate(down, 0xE00); // exactly fills
        allocate(up, 1);       // full: fails
        allocate(down, -0x400); // giving back
        allocate(up, 0x10);

        // Take the down allocator away: the up one can now reach the end.
        if (original) {
            Original<AllocatorDestroy>(0x005BA140)(down);
        } else {
            down->DestroyBody();
        }
        allocate(up, 0x300);
        // Exactly to the end (the up side's limit), then one more byte.
        allocate(up, original ? Original<RegionAvailable>(0x005BC530)(region) : region->MemAvailable());
        allocate(up, 1);
        if (original)
            Original<RegionRemove>(0x005BC510)(region, up);
        else
            region->RemoveAllocator(up);
        trace.push_back(static_cast<uint32_t>(original ? Original<RegionAvailable>(0x005BC530)(region)
                                                       : region->MemAvailable()));

        // Block headers: a block at +0x2800 owned by a fake allocator whose free (slot 8) counts calls.
        auto* fake_allocator = reinterpret_cast<Allocator*>(arena + 0x2400);
        fake_allocator->m_vtable = fake_vtable;
        uint8_t* block = arena + 0x2810;
        auto* header = reinterpret_cast<Mem::BlockHeader*>(block - 0x10);
        header->allocator = fake_allocator;
        header->size = 0x123;
        header->marker = 0;
        g_fake_free_calls = 0;
        void* marked = original ? Original<void*(__cdecl*)(void*)>(0x005BA080)(block) : Mem::MarkBlockInUse(block);
        trace.push_back(Rebased(marked, arena));
        trace.push_back(header->marker);
        trace.push_back(Rebased(original ? Original<void*(__cdecl*)(void*)>(0x005BA090)(block)
                                         : static_cast<void*>(Mem::GetBlockHeader(block)),
                                arena));
        trace.push_back(original ? Original<uint32_t(__cdecl*)(void*)>(0x005BA0A0)(block) : Mem::GetBlockSize(block));
        for (int i = 0; i < 2; ++i) {
            void* owner = original ? Original<void*(__cdecl*)(void*)>(0x005BA0B0)(block)
                                   : static_cast<void*>(Mem::FreeBlock(block));
            trace.push_back(Rebased(owner, arena));
            trace.push_back(static_cast<uint32_t>(g_fake_free_calls));
            trace.push_back(Rebased(reinterpret_cast<void*>(g_fake_freed_header), arena));
            trace.push_back(header->marker);
        }

        std::vector<uint32_t> image = RebasedArena(arena, kArenaSize);
        trace.insert(trace.end(), image.begin(), image.end());
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Check(expected == actual, "allocator layer: regions, allocators, block headers");
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  allocator layer word %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// The context stack and an AllocRegion, on the real memory manager: original vs ours.
void TestAllocatorContextsLate()
{
    using Mem::Allocator;
    using Mem::AllocRegion;
    using Mem::Region;
    using RegionCtor = Region*(__thiscall*)(void*, uint8_t*, uint8_t*);
    using AllocatorCtor = Allocator*(__thiscall*)(void*, Region*, int, const char*);
    using Method = void(__thiscall*)(Allocator*);
    using AllocRegionCtor = AllocRegion*(__thiscall*)(void*, uint32_t);
    using AllocRegionDtor = AllocRegion*(__thiscall*)(AllocRegion*, unsigned int);

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        uint8_t* arena = g_alloc_arenas[original ? 0 : 1];
        std::memset(arena, 0, kArenaSize);
        Region* region = original ? Original<RegionCtor>(0x005BC5F0)(arena, arena + 0x1000, arena + 0x2000)
                                  : new (arena) Region(arena + 0x1000, arena + 0x2000);
        Allocator* allocator = original ? Original<AllocatorCtor>(0x005BA260)(arena + 0x100, region, 1, "test")
                                        : new (arena + 0x100) Allocator(region, 1, "test");
        auto shape = [&]() {
            // Depth of the stack, whether the current context is the newest, whether it's the base one.
            uint32_t depth = 0;
            for (Mem::ListNode* node = allocator->m_contexts.next; node != &allocator->m_contexts && depth < 10;
                 node = node->next)
                ++depth;
            trace.push_back(depth);
            trace.push_back(allocator->m_context == allocator->m_contexts.next->data);
            trace.push_back(allocator->m_context == &allocator->m_baseContext);
            trace.push_back(allocator->m_context ? reinterpret_cast<uint32_t>(allocator->m_context->freeList) : 99);
        };
        shape();
        for (int i = 0; i < 2; ++i) {
            if (original)
                Original<Method>(0x005BA1E0)(allocator);
            else
                allocator->PushContext();
            shape();
        }
        for (int i = 0; i < 2; ++i) {
            if (original)
                Original<Method>(0x005BA360)(allocator);
            else
                allocator->PopContext();
            shape();
        }
        if (original)
            Original<Method>(0x005BA140)(allocator);
        else
            allocator->DestroyBody();
        trace.push_back(region->m_allocators[0] == nullptr);

        // An AllocRegion of 0x1234 bytes (rounded to 0x1230), then destroyed without freeing the object.
        auto* memory = arena + 0x2800;
        AllocRegion* owned = original ? Original<AllocRegionCtor>(0x005BC620)(memory, 0x1234)
                                      : new (memory) AllocRegion(0x1234);
        trace.push_back(owned == reinterpret_cast<AllocRegion*>(memory));
        trace.push_back(static_cast<uint32_t>(owned->m_end - owned->m_start));
        trace.push_back(static_cast<uint32_t>(owned->m_minAvailable));
        trace.push_back(owned->m_start != nullptr);
        trace.push_back(reinterpret_cast<uint32_t>(owned->m_vtable));
        AllocRegion* destroyed = original ? Original<AllocRegionDtor>(0x005BC700)(owned, 0)
                                          : reinterpret_cast<AllocRegionDtor>(0x005BC700)(owned, 0); // (the game address runs ours)
        trace.push_back(destroyed == owned);
        trace.push_back(reinterpret_cast<uint32_t>(owned->m_vtable));
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Check(expected == actual, "allocator contexts and AllocRegion");
    for (size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
        if (expected[i] != actual[i])
            Log("  allocator contexts step %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i],
                actual[i]);
    }
}

// Mem::Heap on a region in an arena: the same script of allocations, frees and resizes through the original
// methods and through ours (each run's heaps get a copy of the heap vtable pointing at one or the other), then
// the arenas compared word for word with pointers rebased. Free looks a heap up in the memory manager: before
// the game sets the manager up, a zeroed stand-in (no heaps) is used.
alignas(16) uint8_t g_fake_memory_manager[0x600];
void* g_saved_memory_manager = nullptr;
bool g_memory_manager_swapped = false;

void RestoreMemoryManager()
{
    if (!g_memory_manager_swapped)
        return;
    GameVariable<void*>(0x007D1800) = g_saved_memory_manager;
    g_memory_manager_swapped = false;
}

void TestHeap()
{
    using Mem::BlockHeader;
    using Mem::Heap;
    using Mem::Region;
    using HeapCtor = Heap*(__thiscall*)(void*, Region*, int, const char*);

    if (!GameVariable<void*>(0x007D1800)) {
        std::memset(g_fake_memory_manager, 0, sizeof(g_fake_memory_manager));
        g_saved_memory_manager = nullptr;
        GameVariable<void*>(0x007D1800) = g_fake_memory_manager;
        g_memory_manager_swapped = true;
    }

    static void* vtables[2][9];
    std::memcpy(vtables[0], reinterpret_cast<void*>(0x0065848C), sizeof(vtables[0]));
    std::memcpy(vtables[1], reinterpret_cast<void*>(0x0065848C), sizeof(vtables[1]));
    const uint32_t addresses[6] = {0x005BA780, 0x005BA4F0, 0x005BA9A0, 0x005BAA20, 0x005BAAE0, 0x005BA5A0};
    for (int i = 0; i < 6; ++i)
        vtables[0][3 + i] = Original<void*>(addresses[i]);
    vtables[1][3] = CodeAddress(&Heap::Allocate);
    vtables[1][4] = CodeAddress(&Heap::LargestFree);
    vtables[1][5] = CodeAddress(&Heap::ReallocateDown);
    vtables[1][6] = CodeAddress(&Heap::ReallocateUp);
    vtables[1][7] = CodeAddress(&Heap::ReallocateShrink);
    vtables[1][8] = CodeAddress(&Heap::Free);

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        uint8_t* arena = g_alloc_arenas[original ? 0 : 1];
        std::memset(arena, 0xCD, kArenaSize);
        void** vtable = vtables[original ? 0 : 1];
        auto* region = new (arena) Region(arena + 0x400, arena + 0x2000);
        auto* spare = new (arena + 0x20) Region(arena + 0x2000, arena + 0x2C00);
        auto make_heap = [&](void* memory, Region* in, int direction) {
            Heap* heap = original ? Original<HeapCtor>(0x005BA520)(memory, in, direction, "test")
                                  : new (memory) Heap(in, direction, "test");
            heap->m_vtable = vtable;
            return heap;
        };
        Heap* up = make_heap(arena + 0x100, region, 1);
        Heap* down = make_heap(arena + 0x200, region, -1);
        Heap* other = make_heap(arena + 0x300, spare, 1);

        auto put = [&](const void* pointer) { trace.push_back(Rebased(pointer, arena)); };
        auto alloc = [&](Heap* heap, uint32_t size, bool assert_on_fail = false) {
            void* block = heap->VirtualAllocate(size, assert_on_fail);
            put(block);
            return block;
        };
        auto release = [&](Heap* heap, void* block) { heap->VirtualFree(BlockHeader::Of(block)); };
        auto call = [&](Heap* heap, int slot, uint32_t size, void* block) {
            void* result = reinterpret_cast<void*(__thiscall*)(Heap*, uint32_t, void*)>(heap->m_vtable[slot])(
                heap, size, block);
            put(result);
            return result;
        };
        auto largest = [&](Heap* heap) {
            trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(Heap*)>(heap->m_vtable[4])(heap));
        };

        // Growing up: allocate, free in the middle, reuse with a split, merge, give back at the top.
        void* a = alloc(up, 0x30);
        void* b = alloc(up, 0x100);
        void* c = alloc(up, 0x10);
        void* d = alloc(up, 0x55);
        release(up, b);
        void* e = alloc(up, 0x20); // from b's space, split
        largest(up);
        release(up, d);            // at the top: back to the region
        release(up, c);
        release(up, e);            // merges with its neighbours
        largest(up);
        void* f = alloc(up, 0x40);
        void* g = alloc(up, 0x40);
        release(up, f);
        release(up, g);
        a = call(up, 7, 0x5, a);          // shrink: the end becomes free
        a = call(up, 6, 0x200, a);        // grow up in place (or not)
        void* h = alloc(up, 0x18);
        call(up, 6, 0x10, h);             // already big enough
        // Two equally good holes (and a bigger one): the first of the smallest is taken.
        void* hole1 = alloc(up, 0x40);
        alloc(up, 0x10);
        void* hole2 = alloc(up, 0x40);
        alloc(up, 0x10);
        void* hole3 = alloc(up, 0x80);
        alloc(up, 0x10);
        release(up, hole3);
        release(up, hole2);
        release(up, hole1);
        alloc(up, 0x40);
        alloc(up, 0x40);

        // Growing down: allocate, grow down, free.
        void* x = alloc(down, 0x40);
        void* y = alloc(down, 0x20);
        y = call(down, 5, 0x80, y);       // grows into the space just below
        largest(down);
        release(down, x);
        release(down, y);
        largest(down);

        // Running out: no fallback, then falling back on `other` (keeping 0x100 bytes clear in it).
        alloc(up, 0x10000);
        up->m_useFallbacks = 1;
        up->m_fallbacks[0] = nullptr;
        up->m_fallbacks[1] = other;
        up->m_fallbackReserve[1] = 0x100;
        for (int i = 2; i < 5; ++i)
            up->m_fallbacks[i] = nullptr;
        up->m_fallbackReserve[0] = 0;
        alloc(up, 0x1C00);                // too big for both: fails
        alloc(up, static_cast<uint32_t>(region->MemAvailable()) - 0x100); // nearly fills the region
        alloc(up, 0x200);                 // doesn't fit any more: from `other`
        alloc(up, 0xB00);                 // would leave `other` under its reserve: fails
        trace.push_back(static_cast<uint32_t>(GameVariable<int>(0x007D0FDC)));

        for (Heap* heap : {up, down, other})
            heap->m_vtable = reinterpret_cast<void**>(0x0065848C);
        std::vector<uint32_t> image = RebasedArena(arena, kArenaSize);
        trace.insert(trace.end(), image.begin(), image.end());
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Check(expected == actual, "heap: allocate, free, merge, resize, fall back");
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  heap word %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
    RestoreMemoryManager();
}

// The memory manager on a stand-in: a Manager in an arena (installed as the game's manager for the test),
// with a region and two heaps; the same script through the original manager functions and ours, then the
// arenas compared word for word with pointers rebased (heap names compared as text).
constexpr size_t kManagerArenaSize = 0x8000;
alignas(16) uint8_t g_manager_arenas[2][kManagerArenaSize];
void* g_saved_manager_for_test = nullptr;
bool g_manager_test_swapped = false;

void RestoreManagerForTest()
{
    if (!g_manager_test_swapped)
        return;
    GameVariable<void*>(0x007D1800) = g_saved_manager_for_test;
    g_manager_test_swapped = false;
}

void TestMemoryManager()
{
    using Mem::Allocator;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestMemoryManager skipped (the memory manager is already set up)");
        return;
    }
    g_saved_manager_for_test = nullptr;
    g_manager_test_swapped = true;

    const char* name = "test";
    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        uint8_t* arena = g_manager_arenas[original ? 0 : 1];
        std::memset(arena, 0, kManagerArenaSize);
        auto put = [&](const void* pointer) {
            auto value = reinterpret_cast<uint32_t>(pointer);
            auto base = reinterpret_cast<uint32_t>(arena);
            trace.push_back(value >= base && value <= base + kManagerArenaSize ? 0xA0000000u + (value - base) : value);
        };
        auto text = [&](const char* string) {
            uint32_t hash = 0;
            for (; string && *string; ++string)
                hash = hash * 31 + static_cast<uint8_t>(*string);
            trace.push_back(hash);
        };

        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x800, arena + kManagerArenaSize);
        auto* up = new (arena + 0x620) Heap(region, 1, name);
        auto* down = new (arena + 0x6C0) Heap(region, -1, name);
        manager->m_heaps[0] = up;
        manager->m_heaps[1] = down;
        manager->m_heapCount = 2;
        manager->m_mainRegion = region;
        manager->m_topDownHeap = down;
        manager->m_bottomUpHeap = up;

        // The functions under test: originals or ours.
        auto push = [&](Allocator* heap) {
            if (original)
                Original<void(__thiscall*)(Manager*, Allocator*)>(0x005BB0E0)(manager, heap);
            else
                manager->PushContext(heap);
        };
        auto pop = [&]() {
            if (original)
                Original<void(__thiscall*)(Manager*)>(0x005BB110)(manager);
            else
                manager->PopContext();
        };
        using Alloc3 = void*(__thiscall*)(Manager*, uint32_t, int, Allocator*);
        using Realloc3 = void*(__thiscall*)(Manager*, uint32_t, void*, Allocator*);
        auto allocate = [&](uint32_t size, Allocator* heap) {
            void* block = original ? Original<Alloc3>(0x005BAFC0)(manager, size, 1, heap)
                                   : manager->Allocate(size, 1, heap);
            put(block);
            return block;
        };
        auto free_block = [&](void* block) {
            if (original)
                Original<void(__thiscall*)(Manager*, void*)>(0x005BABA0)(manager, block);
            else
                manager->Free(block);
        };
        auto delete_heap = [&](Allocator* heap) {
            if (original)
                Original<void(__thiscall*)(Manager*, Allocator*)>(0x005BABC0)(manager, heap);
            else
                manager->DeleteHeap(heap);
        };
        auto manager_call = [&](uint32_t address, void (Manager::*ours)()) {
            if (original)
                Original<void(__thiscall*)(Manager*)>(address)(manager);
            else
                (manager->*ours)();
        };

        // Allocation through the manager and the global functions.
        push(up);
        put(manager->m_current);
        void* a = allocate(0x40, nullptr);
        void* b = original ? Original<Alloc3>(0x005BB000)(manager, 0x30, 1, down)
                           : manager->AllocateZeroed(0x30, 1, down);
        put(b);
        void* c = original ? Original<void*(__cdecl*)(unsigned int)>(0x005BBAE0)(0x20) : Mem::Malloc(0x20);
        put(c);
        trace.push_back(original ? Original<uint32_t(__thiscall*)(Manager*)>(0x005BB050)(manager)
                                 : manager->Available());
        trace.push_back(original ? Original<uint32_t(__cdecl*)()>(0x005BBB10)() : Mem::MallocAvailable());
        a = original ? Original<Realloc3>(0x005BB0A0)(manager, 0x10, a, nullptr)
                     : manager->ReallocateShrink(0x10, a, nullptr);
        put(a);
        c = original ? Original<void*(__cdecl*)(unsigned int, void*)>(0x005BBB30)(0x8, c)
                     : Mem::ReallocateShrink(0x8, c);
        put(c);
        b = original ? Original<Realloc3>(0x005BB060)(manager, 0x80, b, down)
                     : manager->ReallocateUp(0x80, b, down);
        put(b);
        std::memset(c, 0x5A, 8);
        c = original ? Original<void*(__cdecl*)(void*, unsigned int)>(0x005BBBA0)(c, 0x40) : Mem::Realloc(c, 0x40);
        put(c);
        trace.push_back(original ? Original<unsigned int(__cdecl*)(void*)>(0x005BBB80)(c) : Mem::MallocSize(c));
        trace.push_back(original ? Original<unsigned int(__cdecl*)(void*)>(0x005BBB80)(nullptr)
                                 : Mem::MallocSize(nullptr));
        void* d = original ? Original<void*(__cdecl*)(void*, unsigned int)>(0x005BBBA0)(nullptr, 0x10)
                           : Mem::Realloc(nullptr, 0x10);
        put(d);
        free_block(a);
        free_block(nullptr);
        if (original)
            Original<void(__cdecl*)(void*)>(0x005BBB60)(b);
        else
            Mem::FreeMalloc(b);
        text(original ? Original<const char*(__thiscall*)(Manager*)>(0x005BB150)(manager) : manager->CurrentHeapName());
        push(down);
        put(manager->m_current);
        pop();
        put(manager->m_current); // back to `up`
        pop();
        put(manager->m_current);
        trace.push_back(static_cast<uint32_t>(manager->m_contextDepth));
        push(up);

        // The registry.
        using HeapFn = Allocator*(__thiscall*)(Manager*, Allocator*);
        put(original ? Original<Allocator*(__thiscall*)(Manager*)>(0x005BAC10)(manager) : manager->FirstHeap());
        manager->m_heaps[2] = reinterpret_cast<Allocator*>(arena + 0x90); // past the end: never returned
        for (Allocator* heap : {static_cast<Allocator*>(up), static_cast<Allocator*>(down),
                                static_cast<Allocator*>(nullptr)})
            put(original ? Original<HeapFn>(0x005BAC20)(manager, heap) : manager->NextHeap(heap));
        manager->m_heaps[2] = nullptr;

        // Heaps and regions as real blocks from `up`, so deleting them frees them.
        auto make_region = [&](uint32_t size) {
            auto* memory = static_cast<uint8_t*>(manager->Allocate(0x18 + size, 1, up));
            return new (memory) Region(memory + 0x20, memory + 0x18 + size);
        };
        auto make_heap = [&](Region* in) {
            Heap* heap = new (manager->Allocate(sizeof(Heap), 1, up)) Heap(in, 1, name);
            manager->m_heaps[manager->m_heapCount++] = heap;
            return heap;
        };

        // Lookups by name.
        Manager::NamedHeap& named = manager->m_namedHeaps[2];
        named.region = make_region(0x100);
        named.heap = make_heap(named.region);
        named.name = 0x12345678;
        std::memcpy(named.text, "Named", 6);
        named.used = true;
        manager->m_frontEndHeap = reinterpret_cast<Heap*>(arena + 0x10);
        manager->m_skaterHeaps[2] = reinterpret_cast<Heap*>(arena + 0x20);
        manager->m_skaterGeomHeaps[3] = reinterpret_cast<Heap*>(arena + 0x30);
        manager->m_themeHeap = reinterpret_cast<Heap*>(arena + 0x40);
        manager->m_debugHeap = reinterpret_cast<Heap*>(arena + 0x50);
        const uint32_t names[] = {0x8FDB68AF, 0x0C7800B0, 0x62F3A0F3, 0x0FA33D9B, 0xE3551D2E, 0x96D29D93, 0xFCD5166B,
                                  0x90020867, 0x25D71469, 0x7ED56B49, 0x70CB0238, 0xC3909393, 0xB497A305, 0x2D9EF2BF,
                                  0x5A99C229, 0x572A9F4C, 0x202DAFDA, 0xB924FE60, 0xCE23CEF6, 0x08682D24, 0x17ECB880,
                                  0x12345678, 0x0BADF00D};
        for (uint32_t checksum : names) {
            put(original ? Original<void*(__thiscall*)(Manager*, uint32_t)>(0x005BAF00)(manager, checksum)
                         : static_cast<void*>(manager->FindNamedHeap(checksum)));
            put(original ? Original<Heap*(__thiscall*)(Manager*, uint32_t, int)>(0x005BAF50)(manager, checksum, 0)
                         : manager->GetNamedHeap(checksum, 0));
            put(original ? Original<Heap*(__thiscall*)(Manager*, uint32_t)>(0x005BB310)(manager, checksum)
                         : manager->GetHeap(checksum));
            text(original ? Original<const char*(__thiscall*)(Manager*, uint32_t)>(0x005BB160)(manager, checksum)
                          : manager->GetHeapName(checksum));
        }
        manager->m_frontEndHeap = nullptr;
        manager->m_skaterHeaps[2] = nullptr;
        manager->m_skaterGeomHeaps[3] = nullptr;
        manager->m_themeHeap = nullptr;
        manager->m_debugHeap = nullptr;

        // Deleting.
        using DeleteNamedFn = bool(__thiscall*)(Manager*, uint32_t, int);
        for (int i = 0; i < 2; ++i)
            trace.push_back(original ? Original<DeleteNamedFn>(0x005BAF70)(manager, 0x12345678, 0)
                                     : manager->DeleteNamedHeap(0x12345678, 0));
        Region* spare_region = make_region(0x80);
        Heap* spare = make_heap(spare_region);
        delete_heap(spare);
        delete_heap(nullptr);
        manager->m_netMiscRegion = make_region(0x80);
        manager->m_netMiscHeap = make_heap(manager->m_netMiscRegion);
        manager_call(0x005BAC50, &Manager::DeleteNetMiscHeap);
        manager->m_cutsceneRegion = make_region(0x100);
        manager->m_cutsceneTopDownHeap = new (manager->Allocate(sizeof(Heap), 1, up)) Heap(manager->m_cutsceneRegion, -1, name);
        manager->m_heaps[manager->m_heapCount++] = manager->m_cutsceneTopDownHeap;
        manager->m_cutsceneBottomUpHeap = make_heap(manager->m_cutsceneRegion);
        manager_call(0x005BAC90, &Manager::DeleteCutsceneHeaps);
        manager->m_skaterRegions[2] = make_region(0x80);
        manager->m_skaterHeaps[2] = make_heap(manager->m_skaterRegions[2]);
        manager->m_skaterGeomRegions[5] = make_region(0x80);
        manager->m_skaterGeomHeaps[5] = make_heap(manager->m_skaterGeomRegions[5]);
        manager_call(0x005BACF0, &Manager::DeleteExtraSkaterHeaps);
        manager->m_frontEndRegion = make_region(0x80);
        manager->m_frontEndHeap = make_heap(manager->m_frontEndRegion);
        manager->m_skaterRegions[0] = make_region(0x80);
        manager->m_skaterHeaps[0] = make_heap(manager->m_skaterRegions[0]);
        manager->m_skaterRegions[4] = make_region(0x80); // an extra one: deleted too
        manager->m_skaterHeaps[4] = make_heap(manager->m_skaterRegions[4]);
        manager_call(0x005BADD0, &Manager::DeleteStandardHeaps);
        trace.push_back(static_cast<uint32_t>(manager->m_heapCount));
        manager_call(0x005BBE50, &Manager::DestroyMainHeaps);

        std::vector<uint32_t> image(kManagerArenaSize / 4);
        std::memcpy(image.data(), arena, kManagerArenaSize);
        for (uint32_t& word : image) {
            auto base = reinterpret_cast<uint32_t>(arena);
            if (word >= base && word <= base + kManagerArenaSize) // (a region's end is one past the arena)
                word = 0xA0000000u + (word - base);
        }
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    RestoreManagerForTest();
    Check(expected == actual, "memory manager: allocation, context stack, registry, lookups, deleting heaps");
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  manager word %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// Creating the manager's heaps on a stand-in manager in a 12 MB arena (installed as the game's manager):
// the original creators and ours, then ShutDownManager; the arena is compared page by page (pointers rebased,
// heap names as text). The constructor itself isn't tested here: it allocates from the game's CRT and builds
// the main heaps in their fixed static storage.
bool g_saved_manager_ready = false;

void RestoreManagerInitTest()
{
    if (!g_manager_test_swapped)
        return;
    GameVariable<void*>(0x007D1800) = g_saved_manager_for_test;
    GameVariable<bool>(0x007CC979) = g_saved_manager_ready;
    g_manager_test_swapped = false;
}

void TestMemoryManagerHeapCreation()
{
    using Mem::Allocator;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestMemoryManagerHeapCreation skipped (the memory manager is already set up)");
        return;
    }
    constexpr size_t kSize = 12 * 1024 * 1024;
    g_saved_manager_for_test = nullptr;
    g_saved_manager_ready = GameVariable<bool>(0x007CC979);
    g_manager_test_swapped = true;

    auto run = [&](bool original, uint8_t* arena) {
        std::vector<uint32_t> trace;
        auto rebased = [&](const void* pointer) {
            auto value = reinterpret_cast<uint32_t>(pointer);
            auto base = reinterpret_cast<uint32_t>(arena);
            return value >= base && value <= base + kSize ? 0xA0000000u + (value - base) : value;
        };
        std::memset(arena, 0, kSize);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + kSize);
        auto* top = new (arena + 0x620) Heap(region, -1, "Top Down");
        auto* bottom = new (arena + 0x6C0) Heap(region, 1, "BottomUp");
        manager->m_heaps[0] = top;
        manager->m_heaps[1] = bottom;
        manager->m_heapCount = 2;
        manager->m_mainRegion = region;
        manager->m_topDownHeap = top;
        manager->m_bottomUpHeap = bottom;
        manager->PushContext(bottom);

        auto snapshot = [&]() {
            // Heap names as text, then the arena as page hashes of the rebased words.
            for (int i = 0; i < manager->m_heapCount; ++i) {
                uint32_t hash = 0;
                for (const char* c = manager->m_heaps[i] ? manager->m_heaps[i]->m_name : nullptr; c && *c; ++c)
                    hash = hash * 31 + static_cast<uint8_t>(*c);
                trace.push_back(hash);
            }
            std::vector<const char*> names(manager->m_heapCount);
            for (int i = 0; i < manager->m_heapCount; ++i) {
                if (manager->m_heaps[i]) {
                    names[i] = manager->m_heaps[i]->m_name;
                    manager->m_heaps[i]->m_name = nullptr;
                }
            }
            auto base = reinterpret_cast<uint32_t>(arena);
            const auto* words = reinterpret_cast<const uint32_t*>(arena);
            for (size_t page = 0; page < kSize / 0x1000; ++page) {
                uint32_t hash = 2166136261u;
                for (size_t i = page * 0x400; i < (page + 1) * 0x400; ++i) {
                    uint32_t word = words[i];
                    if (word >= base && word <= base + kSize)
                        word = 0xA0000000u + (word - base);
                    hash = (hash ^ word) * 16777619u;
                }
                trace.push_back(hash);
            }
            for (int i = 0; i < manager->m_heapCount; ++i) {
                if (manager->m_heaps[i])
                    manager->m_heaps[i]->m_name = names[i];
            }
        };

        using Method = void(__thiscall*)(Manager*);
        auto call = [&](uint32_t address, void (Manager::*ours)()) {
            if (original)
                Original<Method>(address)(manager);
            else
                (manager->*ours)();
        };
        call(0x005BBE80, &Manager::InitStandardHeaps);
        manager->PushContext(top); // (they make their heaps with the bottom-up heap current anyway)
        call(0x005BB560, &Manager::InitNetMiscHeap);
        call(0x005BB560, &Manager::InitNetMiscHeap); // already there: nothing
        call(0x005BB660, &Manager::InitInternetHeaps);
        manager->PopContext();
        if (original) {
            Original<void(__thiscall*)(Manager*, uint32_t)>(0x005BB780)(manager, 0x10000);
            Original<void(__thiscall*)(Manager*, int)>(0x005BB820)(manager, 3);
            Original<void(__thiscall*)(Manager*, uint32_t, uint32_t, const char*, int)>(0x005BB9E0)(manager, 0xABCD,
                                                                                                0x2000, "Mine", 0);
            trace.push_back(rebased(Original<Heap*(__thiscall*)(Manager*, Region*, int, const char*)>(0x005BB4E0)(
                manager, manager->m_themeRegion, -1, "Extra")));
        } else {
            manager->InitCutsceneHeaps(0x10000);
            manager->InitExtraSkaterHeaps(3);
            manager->InitNamedHeap(0xABCD, 0x2000, "Mine", 0);
            trace.push_back(rebased(manager->AddHeap(manager->m_themeRegion, -1, "Extra")));
        }
        trace.push_back(static_cast<uint32_t>(manager->m_heapCount));
        trace.push_back(static_cast<uint32_t>(manager->m_contextDepth));
        snapshot();
        // Deleted heaps stay in freed memory with their names: give them all one name in the arena first (the
        // literal names differ between the game's and ours).
        std::memcpy(arena + 0x5F0, "heap", 5);
        for (int i = 0; i < manager->m_heapCount; ++i) {
            if (manager->m_heaps[i])
                manager->m_heaps[i]->m_name = reinterpret_cast<const char*>(arena + 0x5F0);
        }
        if (original)
            Original<void(__cdecl*)()>(0x005BC340)();
        else
            Mem::ShutDownManager();
        trace.push_back(GameVariable<void*>(0x007D1800) == nullptr);
        GameVariable<void*>(0x007D1800) = manager; // (for the snapshot's sake: nothing reads it)
        snapshot();
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    auto* arenas = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * kSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!arenas) {
        Log("self-test: TestMemoryManagerHeapCreation skipped (no memory for the arenas)");
        RestoreManagerInitTest();
        return;
    }
    std::vector<uint32_t> expected = run(true, arenas);
    std::vector<uint32_t> actual = run(false, arenas + kSize);
    VirtualFree(arenas, 0, MEM_RELEASE);
    RestoreManagerInitTest();
    Check(expected == actual, "memory manager: creating heaps, shutting down");
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  heap creation entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// Script::CStruct lookups on handmade structures and a stand-in symbol table: every getter, the searches and
// the flag checks, through the originals and ours, over the same data (they only read it).
Script::CSymbolTableEntry** g_saved_symbol_table = nullptr;
bool g_symbol_table_swapped = false;

void RestoreSymbolTable()
{
    if (!g_symbol_table_swapped)
        return;
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = g_saved_symbol_table;
    g_symbol_table_swapped = false;
}

#pragma warning(push)
#pragma warning(disable : 4456) // (the getter macro's `key`, shadowed for the by-name getters)
void TestScriptStructLookups()
{
    using namespace Script;
    static CSymbolTableEntry* table[0x1000];
    static CSymbolTableEntry symbols[6];
    static CComponent components[32];
    static CStruct structs[6];
    static CPair pair = {0, 1.5f, -2.0f};
    static CVector vector = {0, 3.0f, 4.0f, 5.0f};
    std::memset(table, 0, sizeof(table));
    std::memset(symbols, 0, sizeof(symbols));
    std::memset(components, 0, sizeof(components));
    std::memset(structs, 0, sizeof(structs));

    const char* const strings[] = {"speed", "angle", "text", "global_int", "alias", "global_float", "label", "local",
                                   "where", "dir", "sub", "list", "count", "unknown_name", "flag_a", "flag_g",
                                   "included", "missing"};
    constexpr int kNames = sizeof(strings) / sizeof(strings[0]);
    uint32_t n[kNames];
    for (int i = 0; i < kNames; ++i)
        n[i] = Crc::GenerateCRCFromString(strings[i]);
    enum { SPEED, ANGLE, TEXT, GINT, ALIAS, GFLOAT, LABEL, LOCAL, WHERE, DIR, SUB, LIST, COUNT, UNKNOWN, FLAGA, FLAGG,
           INCLUDED, MISSING };

    // Symbols: an included structure, an integer, an alias of it, a float; plus a bucket collision.
    auto add_symbol = [&](int i, uint32_t name, ESymbolType type, uint32_t value) {
        symbols[i].name = name;
        symbols[i].type = type;
        symbols[i].raw = value;
        CSymbolTableEntry*& bucket = table[name & 0xFFF];
        symbols[i].next = bucket;
        bucket = &symbols[i];
    };
    CStruct* global = &structs[1];
    CStruct* sub = &structs[2];
    CStruct* subsub = &structs[3];
    add_symbol(0, n[INCLUDED], ESYMBOLTYPE_STRUCTURE, reinterpret_cast<uint32_t>(global));
    add_symbol(1, n[GINT], ESYMBOLTYPE_INTEGER, 77);
    add_symbol(2, n[ALIAS], ESYMBOLTYPE_NAME, n[GINT]);
    float half = 0.5f;
    uint32_t half_bits;
    std::memcpy(&half_bits, &half, 4);
    add_symbol(3, n[GFLOAT], ESYMBOLTYPE_FLOAT, half_bits);
    add_symbol(4, n[INCLUDED] + 0x1000, ESYMBOLTYPE_INTEGER, 5); // same bucket as INCLUDED
    add_symbol(5, n[MISSING] ^ 0x5000, ESYMBOLTYPE_NAME, 0x12345); // an alias of nothing

    int used = 0;
    auto add = [&](CStruct* to, ESymbolType type, uint32_t name, uint32_t value) {
        CComponent* component = &components[used++];
        component->typeAndFlag = static_cast<uint8_t>(type << 1);
        component->name = name;
        component->raw = value;
        CComponent** link = &to->m_head;
        while (*link)
            link = &(*link)->next;
        *link = component;
    };
    float f15 = 1.5f, f25 = 2.5f;
    uint32_t f15_bits, f25_bits;
    std::memcpy(&f15_bits, &f15, 4);
    std::memcpy(&f25_bits, &f25, 4);
    // The included global structure.
    add(global, ESYMBOLTYPE_INTEGER, n[SPEED], 5);
    add(global, ESYMBOLTYPE_NAME, 0, n[FLAGG]);
    add(global, ESYMBOLTYPE_FLOAT, n[ANGLE], f25_bits);
    add(global, ESYMBOLTYPE_STRING, n[TEXT], reinterpret_cast<uint32_t>("global text"));
    // Nested structures.
    add(subsub, ESYMBOLTYPE_INTEGER, n[COUNT], 1);
    add(sub, ESYMBOLTYPE_STRUCTURE, n[SUB], reinterpret_cast<uint32_t>(subsub));
    add(sub, ESYMBOLTYPE_NAME, 0, n[FLAGA]);
    add(sub, ESYMBOLTYPE_NAME, 0, n[INCLUDED]); // last: an include isn't the unnamed checksum
    // The main structure.
    CStruct* main = &structs[0];
    add(main, ESYMBOLTYPE_INTEGER, n[SPEED], 1);
    add(main, ESYMBOLTYPE_NAME, 0, n[INCLUDED]);
    add(main, ESYMBOLTYPE_INTEGER, n[SPEED], 3);
    add(main, ESYMBOLTYPE_NAME, n[GINT], n[GINT]);
    add(main, ESYMBOLTYPE_NAME, n[ALIAS], n[ALIAS]);
    add(main, ESYMBOLTYPE_NAME, n[GFLOAT], n[GFLOAT]);
    add(main, ESYMBOLTYPE_NAME, 0, n[FLAGA]);
    add(main, ESYMBOLTYPE_STRING, n[LABEL], reinterpret_cast<uint32_t>("hello"));
    add(main, ESYMBOLTYPE_LOCALSTRING, n[LOCAL], reinterpret_cast<uint32_t>("local text"));
    add(main, ESYMBOLTYPE_PAIR, n[WHERE], reinterpret_cast<uint32_t>(&pair));
    add(main, ESYMBOLTYPE_VECTOR, n[DIR], reinterpret_cast<uint32_t>(&vector));
    add(main, ESYMBOLTYPE_STRUCTURE, n[SUB], reinterpret_cast<uint32_t>(sub));
    add(main, ESYMBOLTYPE_ARRAY, n[LIST], 0xA11A7);
    add(main, ESYMBOLTYPE_INTEGER, n[COUNT], 9);
    add(main, ESYMBOLTYPE_NAME, n[UNKNOWN], n[UNKNOWN]);
    add(main, ESYMBOLTYPE_FLOAT, n[ANGLE], f15_bits);
    add(main, ESYMBOLTYPE_NAME, n[TEXT], n[MISSING] ^ 0x5000);
    // For IsSubsetOf: two small integer structures.
    add(&structs[4], ESYMBOLTYPE_INTEGER, n[SPEED], 1);
    add(&structs[5], ESYMBOLTYPE_INTEGER, n[COUNT], 2);
    add(&structs[5], ESYMBOLTYPE_INTEGER, n[SPEED], 1);

    g_saved_symbol_table = GameVariable<CSymbolTableEntry**>(0x006F3624);
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;
    g_symbol_table_swapped = true;

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        auto out_value = [&](bool result, const void* out, size_t size) {
            trace.push_back(result);
            uint32_t words[4] = {};
            std::memcpy(words, out, size);
            for (size_t i = 0; i < size / 4; ++i)
                trace.push_back(words[i]);
        };
        auto symbol = [&](uint32_t name) {
            trace.push_back(reinterpret_cast<uint32_t>(original ? Original<CSymbolTableEntry*(__cdecl*)(uint32_t)>(
                                                                      0x00478CF0)(name)
                                                                : LookUpSymbol(name)));
        };
        for (int i = 0; i < kNames; ++i)
            symbol(n[i]);
        symbol(n[INCLUDED] + 0x1000);
        symbol(n[MISSING] ^ 0x5000);

        for (CStruct* s : {main, global, sub, &structs[4]}) {
            std::vector<uint32_t> query(n, n + kNames);
            query.push_back(0);
            for (int q = 0; q <= kNames; ++q) {
                uint32_t name = query[q];
                const char* text = q < kNames ? strings[q] : "";
                for (uint32_t type = 0; type <= 13; ++type) {
                    uint32_t value = 0xCCCCCCCC;
                    bool r = original ? Original<bool(__thiscall*)(CStruct*, uint32_t, uint32_t, void*)>(0x004764B0)(
                                            s, name, type, &value)
                                      : s->Search(name, type, &value);
                    out_value(r, &value, 4);
                }
#define GETTER(address, Type, call_ours, ...)                                                                        \
    {                                                                                                                  \
        Type value{};                                                                                                  \
        std::memset(&value, 0xCC, sizeof(value));                                                                      \
        bool r = original ? Original<bool(__thiscall*)(CStruct*, __VA_ARGS__)>(address)(s, key, &value, flag)          \
                          : call_ours;                                                                                 \
        out_value(r, &value, sizeof(value));                                                                           \
    }
                for (int flag = 0; flag < 2; ++flag) {
                    uint32_t key = name;
                    GETTER(0x00476590, const char*, s->GetString(key, &value, flag), uint32_t, const char**, int)
                    GETTER(0x00476630, int, s->GetInteger(key, &value, flag), uint32_t, int*, int)
                    GETTER(0x004766D0, float, s->GetFloat(key, &value, flag), uint32_t, float*, int)
                    GETTER(0x004767C0, CStruct*, s->GetStructure(key, &value, flag), uint32_t, CStruct**, int)
                    GETTER(0x00476860, CArray*, s->GetArray(key, &value, flag), uint32_t, CArray**, int)
                    GETTER(0x00476950, uint32_t, s->GetChecksum(key, &value, flag), uint32_t, uint32_t*, int)
                    GETTER(0x00476A30, uint32_t, s->GetChecksumOrStringChecksum(key, &value, flag), uint32_t,
                           uint32_t*, int)
                    GETTER(0x00476FB0, VectorValue, s->GetVector(key, &value, flag), uint32_t, VectorValue*, int)
                    {
                        bool b = flag != 0;
                        GETTER(0x00476C60, const char*, reinterpret_cast<bool(__thiscall*)(CStruct*, uint32_t,
                                                                                           const char**, bool)>(
                                                            0x00476C60)(s, key, &value, b),
                               uint32_t, const char**, bool)
                        GETTER(0x00476CB0, int, s->GetInteger(key, &value, b), uint32_t, int*, bool)
                        GETTER(0x00476D50, float, s->GetFloat(key, &value, b), uint32_t, float*, bool)
                        GETTER(0x00476E70, CStruct*, s->GetStructure(key, &value, b), uint32_t, CStruct**, bool)
                        GETTER(0x00476EC0, uint32_t, s->GetChecksum(key, &value, b), uint32_t, uint32_t*, bool)
                        GETTER(0x00476F10, CArray*, s->GetArray(key, &value, b), uint32_t, CArray**, bool)
                        GETTER(0x00477040, VectorValue, s->GetVector(key, &value, b), uint32_t, VectorValue*, bool)
                        GETTER(0x00476DF0, CPair, s->GetPair(key, &value, b), uint32_t, CPair*, bool)
                    }
                    if (q < kNames) {
                        const char* key2 = text;
                        {
                            const char* key = key2;
                            GETTER(0x004765F0, const char*, s->GetString(key, &value, flag), const char*, const char**,
                                   int)
                            GETTER(0x00476680, int, s->GetInteger(key, &value, flag), const char*, int*, int)
                            GETTER(0x00476720, float, s->GetFloat(key, &value, flag), const char*, float*, int)
                            GETTER(0x00476810, CStruct*, s->GetStructure(key, &value, flag), const char*, CStruct**,
                                   int)
                            GETTER(0x004768B0, CArray*, s->GetArray(key, &value, flag), const char*, CArray**, int)
                            GETTER(0x00476A00, uint32_t, s->GetChecksum(key, &value, flag), const char*, uint32_t*,
                                   int)
                            GETTER(0x00477010, VectorValue, s->GetVector(key, &value, flag), const char*,
                                   VectorValue*, int)
                            bool b = flag != 0;
                            GETTER(0x00476C80, const char*, reinterpret_cast<bool(__thiscall*)(
                                                                CStruct*, const char*, const char**, bool)>(0x00476C80)(
                                                                s, key, &value, b),
                                   const char*, const char**, bool)
                            GETTER(0x00476D00, int, s->GetInteger(key, &value, b), const char*, int*, bool)
                            GETTER(0x00476DA0, float, s->GetFloat(key, &value, b), const char*, float*, bool)
                            GETTER(0x00476EE0, uint32_t, s->GetChecksum(key, &value, b), const char*, uint32_t*, bool)
                            GETTER(0x00476F60, CArray*, s->GetArray(key, &value, b), const char*, CArray**, bool)
                            GETTER(0x00477060, VectorValue, s->GetVector(key, &value, b), const char*, VectorValue*,
                                   bool)
                        }
                    }
                }
#undef GETTER
                {
                    float x = -1, y = -1;
                    bool r = original ? Original<bool(__thiscall*)(CStruct*, uint32_t, float*, float*, int)>(
                                            0x00476770)(s, name, &x, &y, 1)
                                      : s->GetPair(name, &x, &y, 1);
                    float xy[2] = {x, y};
                    out_value(r, xy, 8);
                }
                using Find = CComponent*(__thiscall*)(CStruct*, uint32_t);
                using Has = bool(__thiscall*)(CStruct*, uint32_t);
                using HasNamed = bool(__thiscall*)(CStruct*, const char*);
                trace.push_back(reinterpret_cast<uint32_t>(original ? Original<Find>(0x00476A90)(s, name)
                                                                    : s->FindComponent(name)));
                trace.push_back(reinterpret_cast<uint32_t>(original ? Original<Find>(0x00476430)(s, name)
                                                                    : s->FindLastComponent(name)));
                trace.push_back(original ? Original<Has>(0x00476AF0)(s, name) : s->ContainsComponentNamed(name));
                trace.push_back(original ? Original<Has>(0x00476B40)(s, name) : s->ContainsFlag(name));
                trace.push_back(original ? Original<HasNamed>(0x00476B10)(s, text) : s->ContainsComponentNamed(text));
                trace.push_back(original ? Original<HasNamed>(0x00476BA0)(s, text) : s->ContainsFlag(text));
            }
            for (CStruct* other : {main, global, sub, subsub, &structs[4], &structs[5], static_cast<CStruct*>(nullptr)})
                trace.push_back(original ? Original<bool(__thiscall*)(CStruct*, CStruct*)>(0x00476BC0)(s, other)
                                         : s->References(other));
            CComponent* walk = nullptr;
            for (int i = 0; i < 4; ++i) {
                walk = original ? Original<CComponent*(__thiscall*)(CStruct*, CComponent*)>(0x00476490)(s, walk)
                                : s->GetNextComponent(walk);
                trace.push_back(reinterpret_cast<uint32_t>(walk));
            }
            trace.push_back(original ? Original<bool(__thiscall*)(CStruct*)>(0x00476420)(s) : s->IsEmpty());
        }
        for (CStruct* a : {&structs[4], &structs[5]})
            for (CStruct* b : {&structs[4], &structs[5]})
                trace.push_back(original ? Original<bool(__thiscall*)(CStruct*, CStruct*)>(0x00476C10)(a, b)
                                         : a->IsSubsetOf(b));
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    RestoreSymbolTable();
    Check(expected == actual && expected.size() > 1000, "script structures: lookups and getters");
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  struct lookups entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
    if (expected.size() != actual.size())
        Log("  struct lookups: %u entries vs %u", static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
}
#pragma warning(pop)

// The script structures' write side - strings, the pooled objects, cleaning up, copying and removing
// components - on a stand-in manager in an arena (with a script heap; the script pools emptied so everything
// comes from the arena), the same script through the originals and ours, arenas compared.
uint8_t g_saved_script_pools[5][16];
bool g_saved_system_ready = false;
uint8_t g_saved_permanent_strings[0x18];
bool g_script_write_swapped = false;
const uint32_t kScriptPoolAddresses[5] = {0x006F2338, 0x006F3614, 0x006F22B4, 0x006F3644, 0x006F3654};

void RestoreScriptWriteTest()
{
    if (!g_script_write_swapped)
        return;
    for (int i = 0; i < 5; ++i)
        std::memcpy(reinterpret_cast<void*>(kScriptPoolAddresses[i]), g_saved_script_pools[i], 16);
    std::memcpy(reinterpret_cast<void*>(0x006F3570), g_saved_permanent_strings, sizeof(g_saved_permanent_strings));
    GameVariable<bool>(0x007CC979) = g_saved_system_ready;
    GameVariable<void*>(0x007D1800) = g_saved_manager_for_test;
    g_script_write_swapped = false;
}

void TestScriptStructWriting()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptStructWriting skipped (the memory manager is already set up)");
        return;
    }
    for (int i = 0; i < 5; ++i)
        std::memcpy(g_saved_script_pools[i], reinterpret_cast<void*>(kScriptPoolAddresses[i]), 16);
    std::memcpy(g_saved_permanent_strings, reinterpret_cast<void*>(0x006F3570), sizeof(g_saved_permanent_strings));
    g_saved_system_ready = GameVariable<bool>(0x007CC979);
    g_saved_manager_for_test = nullptr;
    g_script_write_swapped = true;
    for (uint32_t address : kScriptPoolAddresses)
        std::memset(reinterpret_cast<void*>(address), 0, 16);
    GameVariable<bool>(0x007CC979) = true;

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        uint8_t* arena = g_manager_arenas[original ? 0 : 1];
        std::memset(arena, 0, kManagerArenaSize);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + kManagerArenaSize - 0x1800);
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        auto* scriptRegion = new (arena + 0x6C0) Region(arena + kManagerArenaSize - 0x1800, arena + kManagerArenaSize);
        auto* scriptHeap = new (arena + 0x6E0) Heap(scriptRegion, 1, "script");
        manager->m_heaps[0] = heap;
        manager->m_heaps[1] = scriptHeap;
        manager->m_heapCount = 2;
        manager->m_scriptHeap = scriptHeap;
        manager->PushContext(heap);

        // The functions under test.
        auto create_string = [&](const char* s) {
            return original ? Original<char*(__cdecl*)(const char*)>(0x004763A0)(s) : CreateString(s);
        };
        auto free_string = [&](const char* s) {
            if (original)
                Original<void(__cdecl*)(const char*)>(0x00476340)(s);
            else
                FreeString(s);
        };
        auto copy = [&](CComponent* to, CComponent* from, int reference) {
            if (original)
                Original<void(__cdecl*)(CComponent*, CComponent*, int)>(0x004773E0)(to, from, reference);
            else
                CopyComponent(to, from, reference);
        };
        auto clean = [&](CComponent* c) {
            if (original)
                Original<void(__cdecl*)(CComponent*)>(0x004772A0)(c);
            else
                CleanUpComponent(c);
        };
        auto clear = [&](CStruct* s) {
            if (original)
                Original<void(__thiscall*)(CStruct*)>(0x00477090)(s);
            else
                s->Clear();
        };
        auto new_struct = [&]() {
            CStruct* s = original ? Original<CStruct*(__cdecl*)(unsigned)>(0x00409030)(8) : NewStruct();
            return s->Init();
        };
        auto new_array = [&]() {
            CArray* a = original ? Original<CArray*(__cdecl*)(unsigned)>(0x00408F90)(12) : NewArray();
            return original ? Original<CArray*(__thiscall*)(CArray*)>(0x0046CAA0)(a) : a->Init();
        };
        auto put = [&](const void* p) {
            auto value = reinterpret_cast<uint32_t>(p);
            auto base = reinterpret_cast<uint32_t>(arena);
            trace.push_back(value >= base && value <= base + kManagerArenaSize ? 0xA0000000u + (value - base) : value);
        };

        // Components (from the heap: freeing them goes back to it).
        auto component = [&](ESymbolType type, uint32_t name, uint32_t value) {
            auto* c = static_cast<CComponent*>(Mem::Malloc(sizeof(CComponent)));
            c->typeAndFlag = static_cast<uint8_t>(type << 1);
            c->name = name;
            c->raw = value;
            c->next = nullptr;
            c->unknown1 = 0;
            c->unknown2 = 0;
            return c;
        };
        auto append = [&](CStruct* s, CComponent* c) {
            CComponent** link = &s->m_head;
            while (*link)
                link = &(*link)->next;
            *link = c;
        };
        CStruct* main = new_struct();
        append(main, component(ESYMBOLTYPE_INTEGER, 1, 42));
        append(main, component(ESYMBOLTYPE_STRING, 2, reinterpret_cast<uint32_t>(create_string("hello"))));
        append(main, component(ESYMBOLTYPE_LOCALSTRING, 3, reinterpret_cast<uint32_t>(create_string("local"))));
        auto* pair = static_cast<CPair*>(Mem::New(sizeof(CPair)));
        *pair = {0, 1.5f, 2.5f};
        append(main, component(ESYMBOLTYPE_PAIR, 4, reinterpret_cast<uint32_t>(pair)));
        auto* vector = static_cast<CVector*>(Mem::New(sizeof(CVector)));
        *vector = {0, 1.0f, 2.0f, 3.0f};
        append(main, component(ESYMBOLTYPE_VECTOR, 5, reinterpret_cast<uint32_t>(vector)));
        Mem::PushContext(scriptHeap);
        auto* code = static_cast<uint8_t*>(Mem::Malloc(23));
        Mem::PopContext();
        for (int i = 0; i < 23; ++i)
            code[i] = static_cast<uint8_t>(i * 7);
        CComponent* script = component(ESYMBOLTYPE_QSCRIPT, 6, reinterpret_cast<uint32_t>(code));
        script->unknown2 = 23;
        append(main, script);
        CStruct* inner = new_struct();
        append(inner, component(ESYMBOLTYPE_FLOAT, 7, 0x3F800000));
        append(inner, component(ESYMBOLTYPE_STRING, 8, reinterpret_cast<uint32_t>(create_string("inner"))));
        append(main, component(ESYMBOLTYPE_STRUCTURE, 9, reinterpret_cast<uint32_t>(inner)));
        CArray* strings = new_array();
        strings->m_type = ESYMBOLTYPE_STRING;
        strings->m_size = 3;
        strings->m_elements = static_cast<uint32_t*>(Mem::Malloc(12));
        for (int i = 0; i < 3; ++i)
            strings->m_elements[i] = i == 1 ? 0 : reinterpret_cast<uint32_t>(create_string(i ? "two" : "zero"));
        append(main, component(ESYMBOLTYPE_ARRAY, 10, reinterpret_cast<uint32_t>(strings)));
        CArray* single = new_array();
        single->m_type = ESYMBOLTYPE_INTEGER;
        single->m_size = 1;
        single->m_single = 99;
        append(main, component(ESYMBOLTYPE_ARRAY, 11, reinterpret_cast<uint32_t>(single)));
        CArray* structs = new_array();
        structs->m_type = ESYMBOLTYPE_STRUCTURE;
        structs->m_size = 2;
        structs->m_elements = static_cast<uint32_t*>(Mem::Malloc(8));
        for (int i = 0; i < 2; ++i) {
            CStruct* element = new_struct();
            append(element, component(ESYMBOLTYPE_INTEGER, 12, static_cast<uint32_t>(i)));
            structs->m_elements[i] = reinterpret_cast<uint32_t>(element);
        }
        append(main, component(ESYMBOLTYPE_ARRAY, 13, reinterpret_cast<uint32_t>(structs)));
        append(main, component(ESYMBOLTYPE_NAME, 0, 0x1234));
        append(main, component(ESYMBOLTYPE_INTEGER, 1, 43)); // a second "1"

        // Array accessors.
        for (CArray* a : {strings, single, structs})
            put(original ? Original<uint32_t*(__thiscall*)(CArray*)>(0x0046CB00)(a) : a->GetArrayPointer());

        // Copy every component, deep and by reference.
        CStruct* copies = new_struct();
        for (CComponent* c = main->m_head; c; c = c->next) {
            for (int reference = 0; reference < 2; ++reference) {
                CComponent* to = component(ESYMBOLTYPE_INTEGER, 0, 0);
                to->typeAndFlag |= 1; // the flag bit is kept
                copy(to, c, reference);
                trace.push_back(to->typeAndFlag);
                trace.push_back(to->name);
                trace.push_back(to->unknown2);
                if (to->Type() == ESYMBOLTYPE_STRING || to->Type() == ESYMBOLTYPE_LOCALSTRING) {
                    uint32_t hash = 0;
                    for (const char* s = to->string; *s; ++s)
                        hash = hash * 31 + static_cast<uint8_t>(*s);
                    trace.push_back(hash);
                }
                append(copies, to);
            }
        }
        // Copy onto a component that already has a value (it's cleaned up first).
        CComponent* reused = component(ESYMBOLTYPE_STRING, 50, reinterpret_cast<uint32_t>(create_string("old")));
        copy(reused, main->m_head->next, 0);
        append(copies, reused);

        // Removing, cleaning up, clearing.
        auto remove = [&](CStruct* s, uint32_t name) {
            if (original)
                Original<void(__thiscall*)(CStruct*, uint32_t)>(0x004776B0)(s, name);
            else
                s->RemoveComponent(name);
        };
        remove(main, 1);  // both of them
        remove(main, 77); // none
        if (original) {
            Original<void(__thiscall*)(CStruct*, uint32_t, uint8_t)>(0x00477790)(main, 9, ESYMBOLTYPE_INTEGER);
            Original<void(__thiscall*)(CStruct*, uint32_t, uint8_t)>(0x00477790)(main, 9, ESYMBOLTYPE_STRUCTURE);
            Original<void(__thiscall*)(CStruct*, const char*)>(0x00477770)(copies, "nothing");
        } else {
            main->RemoveComponentOfType(9, ESYMBOLTYPE_INTEGER);
            main->RemoveComponentOfType(9, ESYMBOLTYPE_STRUCTURE);
            copies->RemoveComponent("nothing");
        }
        clean(main->m_head);
        trace.push_back(main->m_head->typeAndFlag);
        clear(copies);
        clear(main);
        put(main->m_head);

        // Strings: the permanent cache.
        auto* block = reinterpret_cast<char*>(arena + 0x900);
        GameVariable<char*>(0x006F3574) = block;
        GameVariable<char*>(0x006F3578) = block;
        GameVariable<uint32_t>(0x006F357C) = 0x100;
        GameVariable<void*>(0x006F3580) = arena + 0xA00;
        GameVariable<uint32_t>(0x006F3584) = 0;
        GameVariable<bool>(0x006F3570) = true;
        for (const char* s : {"alpha", "beta", "alpha", "Alpha"})
            put(create_string(s));
        GameVariable<bool>(0x006F3570) = false;
        free_string(block);      // in the block: kept
        free_string(block + 6);
        put(GameVariable<char*>(0x006F3578));
        trace.push_back(GameVariable<uint32_t>(0x006F3584));
        std::memset(reinterpret_cast<void*>(0x006F3570), 0, 0x18);

        // The pooled objects and an array clear.
        CArray* a = new_array();
        a->m_size = 1;
        a->m_type = ESYMBOLTYPE_INTEGER;
        a->m_single = 5;
        if (original)
            Original<void(__thiscall*)(CArray*)>(0x0046CAB0)(a);
        else
            a->Clear();
        trace.push_back(a->m_size);
        trace.push_back(a->m_single);
        trace.push_back(a->m_type);
        if (original) {
            Original<void(__cdecl*)(CArray*)>(0x00408FD0)(a);
            Original<void(__cdecl*)(CStruct*)>(0x00409070)(main);
        } else {
            DeleteArray(a);
            DeleteStruct(main);
        }

        std::vector<uint32_t> image(kManagerArenaSize / 4);
        std::memcpy(image.data(), arena, kManagerArenaSize);
        auto base = reinterpret_cast<uint32_t>(arena);
        for (uint32_t& word : image) {
            if (word >= base && word <= base + kManagerArenaSize)
                word = 0xA0000000u + (word - base);
        }
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    RestoreScriptWriteTest();
    Check(expected == actual, "script structures: strings, copying, cleaning up, removing");
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  struct writing entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// Adding components (every adder, the replacement rules, appending, assigning, expanding includes) on a
// stand-in manager with a script heap and a stand-in symbol table; originals vs ours, arenas compared.
void TestScriptStructAdding()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptStructAdding skipped (the memory manager is already set up)");
        return;
    }
    for (int i = 0; i < 5; ++i)
        std::memcpy(g_saved_script_pools[i], reinterpret_cast<void*>(kScriptPoolAddresses[i]), 16);
    std::memcpy(g_saved_permanent_strings, reinterpret_cast<void*>(0x006F3570), sizeof(g_saved_permanent_strings));
    g_saved_system_ready = GameVariable<bool>(0x007CC979);
    g_saved_manager_for_test = nullptr;
    g_script_write_swapped = true;
    g_saved_symbol_table = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_symbol_table_swapped = true;
    for (uint32_t address : kScriptPoolAddresses)
        std::memset(reinterpret_cast<void*>(address), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F3570), 0, sizeof(g_saved_permanent_strings));
    GameVariable<bool>(0x007CC979) = true;
    static CSymbolTableEntry* table[0x1000];
    static CSymbolTableEntry symbol;
    static CStruct global;
    static CComponent globalComponents[2];
    std::memset(table, 0, sizeof(table));
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        uint8_t* arena = g_manager_arenas[original ? 0 : 1];
        std::memset(arena, 0, kManagerArenaSize);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + kManagerArenaSize - 0x1800);
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        auto* scriptRegion = new (arena + 0x6C0) Region(arena + kManagerArenaSize - 0x1800, arena + kManagerArenaSize);
        auto* scriptHeap = new (arena + 0x6E0) Heap(scriptRegion, 1, "script");
        manager->m_heaps[0] = heap;
        manager->m_heaps[1] = scriptHeap;
        manager->m_heapCount = 2;
        manager->m_scriptHeap = scriptHeap;
        manager->PushContext(heap);

        // A global structure {count=7, flag} included by name 0x6000.
        std::memset(&global, 0, sizeof(global));
        std::memset(globalComponents, 0, sizeof(globalComponents));
        globalComponents[0] = {ESYMBOLTYPE_INTEGER << 1, 0, 0, 0x60, {7}, &globalComponents[1]};
        globalComponents[1].typeAndFlag = ESYMBOLTYPE_NAME << 1;
        globalComponents[1].checksum = 0x61;
        global.m_head = &globalComponents[0];
        symbol = {};
        symbol.type = ESYMBOLTYPE_STRUCTURE;
        symbol.name = 0x6000;
        symbol.structure = &global;
        table[0x6000 & 0xFFF] = &symbol;

        auto new_struct = [&]() { return NewStruct()->Init(); };
        CStruct* s = new_struct();
        float f15 = 1.5f;
        uint32_t f15_bits;
        std::memcpy(&f15_bits, &f15, 4);
        VectorValue v = {1.0f, 2.0f, 3.0f, 4.0f};
        const uint8_t code[5] = {1, 2, 3, 4, 5};
        CStruct* inner = new_struct();
        CArray* array = NewArray()->Init();
#define CALL(address, Signature, ours, ...)                                                                           \
    if (original)                                                                                                      \
        Original<Signature>(address)(__VA_ARGS__);                                                                     \
    else                                                                                                               \
        ours;
        using S = CStruct*;
        CALL(0x004779D0, void(__thiscall*)(S, uint32_t, const char*), s->AddString(1u, "one"), s, 1u, "one")
        CALL(0x00477A90, void(__thiscall*)(S, const char*, const char*), s->AddString("named", "two"), s, "named",
             "two")
        CALL(0x00477AC0, void(__thiscall*)(S, uint32_t, const char*), s->AddLocalString(2, "local"), s, 2u, "local")
        CALL(0x00478390, void(__thiscall*)(S, uint32_t, uint32_t, const char*), s->AddString(3, 4u, "typed"), s, 3u,
             4u, "typed")
        CALL(0x00477B80, void(__thiscall*)(S, uint32_t, int), s->AddInteger(4u, 40), s, 4u, 40)
        CALL(0x00477C30, void(__thiscall*)(S, const char*, int), s->AddInteger("count", 41), s, "count", 41)
        CALL(0x00477C60, void(__thiscall*)(S, uint32_t, float), s->AddFloat(4u, 2.5f), s, 4u, 2.5f) // replaces 4
        CALL(0x00477D10, void(__thiscall*)(S, const char*, float), s->AddFloat("speed", 3.5f), s, "speed", 3.5f)
        CALL(0x00477D40, void(__thiscall*)(S, uint32_t, uint32_t), s->AddChecksum(0u, 0x77u), s, 0u, 0x77u)
        CALL(0x00477D40, void(__thiscall*)(S, uint32_t, uint32_t), s->AddChecksum(0u, 0x78u), s, 0u, 0x78u)
        CALL(0x00477D40, void(__thiscall*)(S, uint32_t, uint32_t), s->AddChecksum(0u, 0x77u), s, 0u, 0x77u)
        CALL(0x00477DF0, void(__thiscall*)(S, const char*, uint32_t), s->AddChecksum("mode", 0x99u), s, "mode", 0x99u)
        CALL(0x00477E20, void(__thiscall*)(S, uint32_t, float, float, float), s->AddVector(5u, 1.0f, 2.0f, 3.0f), s,
             5u, 1.0f, 2.0f, 3.0f)
        CALL(0x00477F40, void(__thiscall*)(S, const char*, float, float, float), s->AddVector("pos", 4.0f, 5.0f, 6.0f),
             s, "pos", 4.0f, 5.0f, 6.0f)
        CALL(0x00477F70, void(__thiscall*)(S, uint32_t, VectorValue), s->AddVector(6u, v), s, 6u, v)
        CALL(0x00477F90, void(__thiscall*)(S, const char*, VectorValue), s->AddVector("dir", v), s, "dir", v)
        CALL(0x00477FC0, void(__thiscall*)(S, uint32_t, float, float), s->AddPair(7u, 0.5f, 0.25f), s, 7u, 0.5f, 0.25f)
        CALL(0x004780D0, void(__thiscall*)(S, uint32_t, CArray*), s->AddArrayPointer(8u, array), s, 8u, array)
        CALL(0x004781B0, void(__thiscall*)(S, uint32_t, CStruct*), s->AddStructurePointer(9u, inner), s, 9u, inner)
        CALL(0x00478290, void(__thiscall*)(S, uint32_t, const uint8_t*, uint32_t), s->AddScript(10u, code, 5u), s,
             10u, code, 5u)
        for (uint32_t type = 0; type <= 14; ++type) {
            uint32_t value = type == 11 ? reinterpret_cast<uint32_t>(new_struct())
                             : type == 12 ? reinterpret_cast<uint32_t>(NewArray()->Init())
                             : type == 2  ? f15_bits
                                          : 0x100 + type;
            CALL(0x004783C0, void(__thiscall*)(S, uint32_t, uint32_t, uint32_t), s->AddComponentOfType(0x200 + type, type,
                                                                                                      value),
                 s, 0x200 + type, type, value)
        }
        CALL(0x00478670, void(__thiscall*)(S, uint32_t, CStruct*), s->AddStructure(11u, s), s, 11u, s)
        CALL(0x00478770, void(__thiscall*)(S, const char*, CStruct*), s->AddStructure("copy", inner), s, "copy", inner)
        CALL(0x00478180, void(__thiscall*)(S, const char*, CArray*), s->AddArrayPointer("list", NewArray()->Init()), s,
             "list", NewArray()->Init())
        CALL(0x00478260, void(__thiscall*)(S, const char*, CStruct*), s->AddStructurePointer("sub", new_struct()), s,
             "sub", new_struct())
        s->AddInteger(0x300u, 0x78); // named, with the flag's value: not a flag
        CALL(0x00477860, void(__thiscall*)(S, uint32_t), s->RemoveFlag(0x78u), s, 0x78u)
        CALL(0x00477860, void(__thiscall*)(S, uint32_t), s->RemoveFlag(0x12345u), s, 0x12345u)
        s->AddChecksum(0u, 0x6000u); // include the global structure

        // Appending, assigning, expanding.
        CStruct* t = new_struct();
        t->AddInteger(4u, 1);
        CALL(0x00478480, CStruct*(__thiscall*)(S, CStruct*), t->AppendStructure(s), t, s)
        CALL(0x00478540, void(__thiscall*)(S, CStruct*), t->AppendStructureIfAny(nullptr), t, nullptr)
        CStruct* u = new_struct();
        CALL(0x00478650, CStruct*(__thiscall*)(S, CStruct*), u->Assign(s), u, s)
        CALL(0x00478650, CStruct*(__thiscall*)(S, CStruct*), u->Assign(u), u, u)
        CStruct* w = new_struct();
        CALL(0x00478560, void(__thiscall*)(S, CStruct*, int), s->ExpandInto(w, 0), s, w, 0)
#undef CALL
        for (CStruct* each : {s, t, u, w}) {
            uint32_t count = 0;
            for (CComponent* c = each->m_head; c; c = c->next)
                ++count;
            trace.push_back(count);
        }

        std::vector<uint32_t> image(kManagerArenaSize / 4);
        std::memcpy(image.data(), arena, kManagerArenaSize);
        auto base = reinterpret_cast<uint32_t>(arena);
        for (uint32_t& word : image) {
            if (word >= base && word <= base + kManagerArenaSize)
                word = 0xA0000000u + (word - base);
        }
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    RestoreScriptWriteTest();
    RestoreSymbolTable();
    Check(expected == actual, "script structures: adding, appending, expanding");
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  struct adding entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

void RestoreScriptAddingTest()
{
    RestoreScriptWriteTest();
    RestoreSymbolTable();
}

// Arrays (sizing, elements, resizing, deep copies of every element type) and structure copies, on a stand-in
// manager with a script heap; originals vs ours, arenas compared.
void TestScriptArrays()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptArrays skipped (the memory manager is already set up)");
        return;
    }
    for (int i = 0; i < 5; ++i)
        std::memcpy(g_saved_script_pools[i], reinterpret_cast<void*>(kScriptPoolAddresses[i]), 16);
    std::memcpy(g_saved_permanent_strings, reinterpret_cast<void*>(0x006F3570), sizeof(g_saved_permanent_strings));
    g_saved_system_ready = GameVariable<bool>(0x007CC979);
    g_saved_manager_for_test = nullptr;
    g_script_write_swapped = true;
    g_saved_symbol_table = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_symbol_table_swapped = true;
    int savedCopyCount = GameVariable<int>(0x006F3610);
    int savedCopyPeak = GameVariable<int>(0x0067DA30);
    for (uint32_t address : kScriptPoolAddresses)
        std::memset(reinterpret_cast<void*>(address), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F3570), 0, sizeof(g_saved_permanent_strings));
    GameVariable<bool>(0x007CC979) = true;
    static CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        uint8_t* arena = g_manager_arenas[original ? 0 : 1];
        std::memset(arena, 0, kManagerArenaSize);
        // (dirty script heap memory, so new elements must really be zeroed)
        std::memset(arena + kManagerArenaSize - 0x2800, 0xCD, 0x2800);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + kManagerArenaSize - 0x2800);
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        auto* scriptRegion = new (arena + 0x6C0) Region(arena + kManagerArenaSize - 0x2800, arena + kManagerArenaSize);
        auto* scriptHeap = new (arena + 0x6E0) Heap(scriptRegion, 1, "script");
        manager->m_heaps[0] = heap;
        manager->m_heaps[1] = scriptHeap;
        manager->m_heapCount = 2;
        manager->m_scriptHeap = scriptHeap;
        manager->PushContext(heap);
        GameVariable<int>(0x006F3610) = 3;
        GameVariable<int>(0x0067DA30) = 3;

        auto new_array = [&]() { return NewArray()->Init(); };
        auto put = [&](uint32_t value) { trace.push_back(value); };
        auto put_float = [&](float value) {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            trace.push_back(bits);
        };
        using A = CArray*;
        auto set_size = [&](A a, uint32_t size, ESymbolType type) {
            if (original)
                Original<void(__thiscall*)(A, uint32_t, uint32_t)>(0x0046CC50)(a, size, type);
            else
                a->SetSizeAndType(size, type);
        };
        auto set = [&](A a, uint32_t index, uint32_t value) {
            if (original)
                Original<void(__thiscall*)(A, uint32_t, uint32_t)>(0x0046CB10)(a, index, value);
            else
                a->SetElement(index, value);
        };
        auto set_float = [&](A a, uint32_t index, float value) {
            if (original)
                Original<void(__thiscall*)(A, uint32_t, float)>(0x0046CB40)(a, index, value);
            else
                a->SetFloat(index, value);
        };
        auto get = [&](A a, uint32_t index) {
            return original ? Original<uint32_t(__thiscall*)(A, uint32_t)>(0x0046CC30)(a, index) : a->GetElement(index);
        };
        auto get_string = [&](A a, uint32_t index, bool local) {
            if (original)
                return Original<const char*(__thiscall*)(A, uint32_t)>(local ? 0x0046CB70 : 0x0046CBB0)(a, index);
            return local ? a->GetLocalString(index) : a->GetString(index);
        };
        auto get_float = [&](A a, uint32_t index) {
            return original ? Original<float(__thiscall*)(A, uint32_t)>(0x0046CBF0)(a, index) : a->GetFloat(index);
        };
        auto copy = [&](A to, A from) {
            if (original)
                Original<void(__cdecl*)(A, A)>(0x00478A60)(to, from);
            else
                CopyArray(to, from);
        };
        auto new_pair = [&](float x, float y) {
            CPair* pair = original ? Original<CPair*(__thiscall*)(CPair*)>(0x0047AC00)(
                                         static_cast<CPair*>(Original<void*(__cdecl*)(unsigned)>(0x0046D410)(12)))
                                   : NewPair()->Init();
            pair->x = x;
            pair->y = y;
            return pair;
        };
        auto new_vector = [&](float x, float y, float z) {
            CVector* vector = original ? Original<CVector*(__thiscall*)(CVector*)>(0x0047ABF0)(static_cast<CVector*>(
                                             Original<void*(__cdecl*)(unsigned)>(0x0046D3D0)(16)))
                                       : NewVector()->Init();
            vector->x = x;
            vector->y = y;
            vector->z = z;
            return vector;
        };

        // Integers: sizes 0, 1, 4; resizing; float conversion.
        A ints = new_array();
        set_size(ints, 0, ESYMBOLTYPE_INTEGER);
        put(ints->m_type);
        set_size(ints, 1, ESYMBOLTYPE_INTEGER);
        set(ints, 0, 42);
        put(get(ints, 0));
        put_float(get_float(ints, 0));
        set_size(ints, 4, ESYMBOLTYPE_INTEGER);
        for (uint32_t i = 0; i < 4; ++i)
            set(ints, i, static_cast<uint32_t>(-3 + 5 * static_cast<int>(i)));
        put_float(get_float(ints, 0));
        if (original)
            Original<void(__thiscall*)(A, uint32_t)>(0x0046CCF0)(ints, 6);
        else
            ints->Resize(6);
        if (original)
            Original<void(__thiscall*)(A, uint32_t)>(0x0046CCF0)(ints, 6);
        else
            ints->Resize(6);
        set(ints, 5, 99);

        A floats = new_array();
        set_size(floats, 3, ESYMBOLTYPE_FLOAT);
        for (uint32_t i = 0; i < 3; ++i)
            set_float(floats, i, 0.5f + i);
        put_float(get_float(floats, 2));
        A oneFloat = new_array();
        set_size(oneFloat, 1, ESYMBOLTYPE_FLOAT);
        set_float(oneFloat, 0, 7.25f);
        put_float(get_float(oneFloat, 0));

        A strings = new_array();
        set_size(strings, 3, ESYMBOLTYPE_STRING);
        set(strings, 0, reinterpret_cast<uint32_t>(CreateString("alpha")));
        set(strings, 2, reinterpret_cast<uint32_t>(CreateString("gamma")));
        put(reinterpret_cast<uint32_t>(get_string(strings, 2, false)));
        A locals = new_array();
        set_size(locals, 1, ESYMBOLTYPE_LOCALSTRING);
        set(locals, 0, reinterpret_cast<uint32_t>(CreateString("local")));
        put(reinterpret_cast<uint32_t>(get_string(locals, 0, true)));

        A pairs = new_array();
        set_size(pairs, 2, ESYMBOLTYPE_PAIR);
        set(pairs, 1, reinterpret_cast<uint32_t>(new_pair(1.5f, -2.0f)));
        A vectors = new_array();
        set_size(vectors, 2, ESYMBOLTYPE_VECTOR);
        set(vectors, 0, reinterpret_cast<uint32_t>(new_vector(1.0f, 2.0f, 3.0f)));
        set(vectors, 1, reinterpret_cast<uint32_t>(new_vector(4.0f, 5.0f, 6.0f)));

        A structs = new_array();
        set_size(structs, 2, ESYMBOLTYPE_STRUCTURE);
        CStruct* inner = NewStruct()->Init();
        inner->AddInteger(0x10u, 5);
        inner->AddString(0x11u, "text");
        set(structs, 0, reinterpret_cast<uint32_t>(inner));

        A nested = new_array();
        set_size(nested, 3, ESYMBOLTYPE_ARRAY);
        set(nested, 0, reinterpret_cast<uint32_t>(ints));
        set(nested, 2, reinterpret_cast<uint32_t>(strings));

        A names = new_array();
        set_size(names, 2, ESYMBOLTYPE_NAME);
        set(names, 0, 0x1234u);
        set(names, 1, 0x5678u);
        A scripts = new_array();
        set_size(scripts, 2, ESYMBOLTYPE_QSCRIPT);
        set(scripts, 0, 0xABCDu);

        // Deep copies, also over arrays that already hold something.
        for (A source : {ints, floats, oneFloat, strings, locals, pairs, vectors, structs, nested, names, scripts}) {
            A dest = new_array();
            copy(dest, source);
            put(dest->m_size);
            put(dest->m_type);
        }
        A reused = new_array();
        copy(reused, nested);
        copy(reused, structs);
        copy(reused, oneFloat);

        // Structure copies.
        CStruct* s = NewStruct();
        if (original)
            Original<CStruct*(__thiscall*)(CStruct*, CStruct*)>(0x00478AB0)(s, inner);
        else
            s->InitCopy(inner);
        CStruct* self = NewStruct();
        if (original)
            Original<CStruct*(__thiscall*)(CStruct*, CStruct*)>(0x00478AB0)(self, self);
        else
            self->InitCopy(self);
        put(static_cast<uint32_t>(GameVariable<int>(0x006F3610)));
        put(static_cast<uint32_t>(GameVariable<int>(0x0067DA30)));

        std::vector<uint32_t> image(kManagerArenaSize / 4);
        std::memcpy(image.data(), arena, kManagerArenaSize);
        auto base = reinterpret_cast<uint32_t>(arena);
        for (uint32_t& word : image) {
            if (word >= base && word <= base + kManagerArenaSize)
                word = 0xA0000000u + (word - base);
        }
        for (uint32_t& word : trace) {
            if (word >= base && word <= base + kManagerArenaSize)
                word = 0xA0000000u + (word - base);
        }
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    GameVariable<int>(0x006F3610) = savedCopyCount;
    GameVariable<int>(0x0067DA30) = savedCopyPeak;
    RestoreScriptWriteTest();
    RestoreSymbolTable();
    Check(expected == actual, "script arrays: sizing, elements, copying");
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  array entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// The symbol table (creating, adding, linking, unlinking, iterating), global values by checksum and name,
// comparing components, adding array copies - on a stand-in manager; originals vs ours, arenas compared.
void TestScriptSymbols()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptSymbols skipped (the memory manager is already set up)");
        return;
    }
    for (int i = 0; i < 5; ++i)
        std::memcpy(g_saved_script_pools[i], reinterpret_cast<void*>(kScriptPoolAddresses[i]), 16);
    std::memcpy(g_saved_permanent_strings, reinterpret_cast<void*>(0x006F3570), sizeof(g_saved_permanent_strings));
    g_saved_system_ready = GameVariable<bool>(0x007CC979);
    g_saved_manager_for_test = nullptr;
    g_script_write_swapped = true;
    g_saved_symbol_table = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_symbol_table_swapped = true;
    uint8_t savedSymbolPools[16];
    std::memcpy(savedSymbolPools, reinterpret_cast<void*>(0x006F362C), 16);
    uint32_t savedIterator[2] = {GameVariable<uint32_t>(0x006F363C), GameVariable<uint32_t>(0x006F3640)};
    for (uint32_t address : kScriptPoolAddresses)
        std::memset(reinterpret_cast<void*>(address), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F362C), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F3570), 0, sizeof(g_saved_permanent_strings));
    GameVariable<bool>(0x007CC979) = true;

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        uint8_t* arena = g_manager_arenas[original ? 0 : 1];
        std::memset(arena, 0, kManagerArenaSize);
        std::memset(arena + 0x1000, 0xCD, kManagerArenaSize - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + kManagerArenaSize - 0x2800);
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        auto* scriptRegion = new (arena + 0x6C0) Region(arena + kManagerArenaSize - 0x2800, arena + kManagerArenaSize);
        auto* scriptHeap = new (arena + 0x6E0) Heap(scriptRegion, 1, "script");
        manager->m_heaps[0] = heap;
        manager->m_heaps[1] = scriptHeap;
        manager->m_heapCount = 2;
        manager->m_scriptHeap = scriptHeap;
        manager->PushContext(heap);
        GameVariable<uint32_t>(0x006F363C) = 0x1111;
        GameVariable<uint32_t>(0x006F3640) = 0x2222;
        auto put = [&](uint32_t value) { trace.push_back(value); };
        auto put_float = [&](float value) {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            trace.push_back(bits);
        };
        using E = CSymbolTableEntry*;

        // The table, and symbols of every kind (two sharing a bucket).
        if (original)
            Original<void(__cdecl*)()>(0x00478C90)();
        else
            CreateSymbolTable();
        auto create = [&](uint32_t name, ESymbolType type, uint32_t value) {
            E entry = original ? Original<E(__cdecl*)(uint32_t)>(0x00479110)(name) : CreateSymbol(name);
            entry->type = type;
            entry->raw = value;
            return entry;
        };
        float f = 2.75f, big = -1.0e9f;
        uint32_t fBits, bigBits;
        std::memcpy(&fBits, &f, 4);
        std::memcpy(&bigBits, &big, 4);
        CStruct* globalStruct = NewStruct()->Init();
        globalStruct->AddInteger(0x40u, 4);
        CArray* globalArray = NewArray()->Init();
        globalArray->SetSizeAndType(3, ESYMBOLTYPE_INTEGER);
        for (uint32_t i = 0; i < 3; ++i)
            globalArray->SetElement(i, 10 + i);
        const uint32_t kInt = Crc::GenerateCRCFromString("an_int");
        const uint32_t kFloat = Crc::GenerateCRCFromString("a_float");
        const uint32_t kString = Crc::GenerateCRCFromString("a_string");
        const uint32_t kStruct = Crc::GenerateCRCFromString("a_struct");
        const uint32_t kArray = Crc::GenerateCRCFromString("an_array");
        const uint32_t kAlias = Crc::GenerateCRCFromString("an_alias");
        const uint32_t kBig = Crc::GenerateCRCFromString("big");
        create(kInt, ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(-17));
        create(kFloat, ESYMBOLTYPE_FLOAT, fBits);
        create(kBig, ESYMBOLTYPE_FLOAT, bigBits);
        create(kString, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>(CreateString("hello")));
        create(kStruct, ESYMBOLTYPE_STRUCTURE, reinterpret_cast<uint32_t>(globalStruct));
        create(kArray, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(globalArray));
        create(kAlias, ESYMBOLTYPE_NAME, kFloat);
        E sameBucket1 = create(0x00012777, ESYMBOLTYPE_INTEGER, 1);
        E sameBucket2 = create(0x00034777, ESYMBOLTYPE_INTEGER, 2);
        E sameBucket3 = create(0x00056777, ESYMBOLTYPE_INTEGER, 3);
        E last = create(0xFFFFFFFF, ESYMBOLTYPE_INTEGER, 4);

        // Unlinking (middle, head, missing) and relinking.
        auto unlink = [&](E entry) {
            if (original)
                Original<void(__cdecl*)(E)>(0x00478D60)(entry);
            else
                UnlinkSymbol(entry);
        };
        auto link = [&](E entry) {
            if (original)
                Original<void(__cdecl*)(E)>(0x00478DC0)(entry);
            else
                LinkSymbol(entry);
        };
        unlink(sameBucket2);
        unlink(sameBucket3);
        unlink(sameBucket3);
        link(sameBucket2);
        static CSymbolTableEntry loose;
        loose = {};
        loose.name = 0x777;
        unlink(&loose);
        put(original ? reinterpret_cast<uint32_t>(Original<E(__cdecl*)(uint32_t)>(0x00478CC0)(0x00034777))
                     : reinterpret_cast<uint32_t>(FindSymbol(0x00034777)));
        put(original ? reinterpret_cast<uint32_t>(Original<E(__cdecl*)(uint32_t)>(0x00478CC0)(0x00056777))
                     : reinterpret_cast<uint32_t>(FindSymbol(0x00056777)));
        (void)sameBucket1;
        (void)last;

        // Iterating: every symbol in order, then past the end, then again.
        for (int pass = 0; pass < 2; ++pass) {
            E entry = nullptr;
            int count = 0;
            do {
                entry = original ? Original<E(__cdecl*)(E)>(0x00478E50)(entry) : GetNextSymbol(entry);
                put(reinterpret_cast<uint32_t>(entry));
                put(GameVariable<uint32_t>(0x006F363C));
                put(GameVariable<uint32_t>(0x006F3640));
            } while (entry && ++count < 64);
        }

        // Global values, by checksum and by name.
        const char* names[] = {"an_int", "a_float", "big", "a_string", "a_struct", "an_array", "an_alias", "missing"};
        for (const char* name : names) {
            uint32_t checksum = Crc::GenerateCRCFromString(name);
            if (original) {
                put(static_cast<uint32_t>(Original<int(__cdecl*)(uint32_t)>(0x00478F60)(checksum)));
                put(static_cast<uint32_t>(Original<int(__cdecl*)(const char*, int)>(0x004790C0)(name, 0)));
                put_float(Original<float(__cdecl*)(uint32_t)>(0x00478F00)(checksum));
                put_float(Original<float(__cdecl*)(const char*)>(0x00478F30)(name));
                put(Original<uint32_t(__cdecl*)(uint32_t)>(0x00478F90)(checksum));
                put(Original<uint32_t(__cdecl*)(const char*)>(0x00478FB0)(name));
                put(reinterpret_cast<uint32_t>(Original<const char*(__cdecl*)(uint32_t)>(0x00478FD0)(checksum)));
                put(reinterpret_cast<uint32_t>(Original<const char*(__cdecl*)(const char*)>(0x00478FF0)(name)));
                put(reinterpret_cast<uint32_t>(Original<CStruct*(__cdecl*)(uint32_t)>(0x00479020)(checksum)));
                put(reinterpret_cast<uint32_t>(Original<CStruct*(__cdecl*)(const char*)>(0x00479040)(name)));
                put(reinterpret_cast<uint32_t>(Original<CArray*(__cdecl*)(uint32_t)>(0x00479070)(checksum)));
                put(reinterpret_cast<uint32_t>(Original<CArray*(__cdecl*)(const char*)>(0x00479090)(name)));
            } else {
                put(static_cast<uint32_t>(GetInteger(checksum)));
                put(static_cast<uint32_t>(GetInteger(name)));
                put_float(GetFloat(checksum));
                put_float(GetFloat(name));
                put(GetSymbolValue(checksum));
                put(GetSymbolValue(name));
                put(reinterpret_cast<uint32_t>(GetString(checksum)));
                put(reinterpret_cast<uint32_t>(GetString(name)));
                put(reinterpret_cast<uint32_t>(GetStructure(checksum)));
                put(reinterpret_cast<uint32_t>(GetStructure(name)));
                put(reinterpret_cast<uint32_t>(GetArray(checksum)));
                put(reinterpret_cast<uint32_t>(GetArray(name)));
            }
        }

        // Comparing components: every pair from a varied set.
        static CComponent components[20];
        static CPair pairs[3];
        static CVector vectors[3];
        static char strings[3][8];
        std::memset(components, 0, sizeof(components));
        pairs[0] = {0, 1.0f, 2.0f};
        pairs[1] = {0, 1.0f, 2.0f};
        pairs[2] = {0, 1.0f, 2.5f};
        vectors[0] = {0, 1.0f, 2.0f, 3.0f};
        vectors[1] = {0, 1.0f, 2.0f, 3.0f};
        vectors[2] = {0, 1.0f, 2.0f, -3.0f};
        std::memcpy(strings[0], "abc", 4);
        std::memcpy(strings[1], "abc", 4);
        std::memcpy(strings[2], "abd", 4);
        float nan = std::numeric_limits<float>::quiet_NaN();
        float three = 3.0f, minus17 = -17.0f;
        uint32_t nanBits, threeBits, minus17Bits;
        std::memcpy(&nanBits, &nan, 4);
        std::memcpy(&threeBits, &three, 4);
        std::memcpy(&minus17Bits, &minus17, 4);
        auto make = [&](int i, ESymbolType type, uint32_t value) {
            components[i].typeAndFlag = static_cast<uint8_t>(type << 1);
            components[i].raw = value;
        };
        make(0, ESYMBOLTYPE_INTEGER, 3);
        make(1, ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(-17));
        make(2, ESYMBOLTYPE_FLOAT, threeBits);
        make(3, ESYMBOLTYPE_FLOAT, fBits); // equal to the alias's 2.75
        make(4, ESYMBOLTYPE_FLOAT, nanBits);
        make(5, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>(strings[0]));
        make(6, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>(strings[1]));
        make(7, ESYMBOLTYPE_LOCALSTRING, reinterpret_cast<uint32_t>(strings[2]));
        make(8, ESYMBOLTYPE_PAIR, reinterpret_cast<uint32_t>(&pairs[0]));
        make(9, ESYMBOLTYPE_PAIR, reinterpret_cast<uint32_t>(&pairs[2]));
        make(10, ESYMBOLTYPE_VECTOR, reinterpret_cast<uint32_t>(&vectors[0]));
        make(11, ESYMBOLTYPE_VECTOR, reinterpret_cast<uint32_t>(&vectors[2]));
        make(12, ESYMBOLTYPE_NAME, 0x1234);
        make(13, ESYMBOLTYPE_NAME, kInt);   // stands for -17
        make(14, ESYMBOLTYPE_NAME, kAlias); // stands for 2.75 (through the alias)
        make(15, ESYMBOLTYPE_QSCRIPT, 0x5000);
        make(16, ESYMBOLTYPE_STRUCTURE, reinterpret_cast<uint32_t>(globalStruct));
        make(17, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(globalArray));
        make(18, ESYMBOLTYPE_FLOAT, minus17Bits);
        make(19, ESYMBOLTYPE_NAME, kStruct); // stands for the global structure
        static CComponent copies[20];
        std::memcpy(copies, components, sizeof(components));
        copies[6].raw = reinterpret_cast<uint32_t>(strings[1]);
        copies[8].raw = reinterpret_cast<uint32_t>(&pairs[1]);
        copies[10].raw = reinterpret_cast<uint32_t>(&vectors[1]);
        CArray* otherArray = NewArray()->Init();
        copies[17].raw = reinterpret_cast<uint32_t>(otherArray);
        for (CComponent* set : {components, copies}) {
            for (int i = 0; i < 20; ++i) {
                uint32_t bits = 0;
                for (int j = 0; j < 20; ++j) {
                    bool equal = original
                                     ? Original<bool(__thiscall*)(CComponent*, CComponent*)>(0x004791A0)(&components[i],
                                                                                                      &set[j])
                                     : ComponentsEqual(&components[i], &set[j]);
                    bits |= (equal ? 1u : 0u) << j;
                }
                put(bits);
            }
        }

        // Adding array copies.
        CStruct* s = NewStruct()->Init();
        if (original) {
            Original<void(__thiscall*)(CStruct*, uint32_t, CArray*)>(0x00478B00)(s, 1u, globalArray);
            Original<void(__thiscall*)(CStruct*, const char*, CArray*)>(0x00478C50)(s, "list", globalArray);
            Original<void(__thiscall*)(CStruct*, uint32_t, CArray*)>(0x00478B00)(s, 1u, otherArray);
        } else {
            s->AddArray(1u, globalArray);
            s->AddArray("list", globalArray);
            s->AddArray(1u, otherArray);
        }
        std::vector<uint32_t> image(kManagerArenaSize / 4);
        std::memcpy(image.data(), arena, kManagerArenaSize);
        auto base = reinterpret_cast<uint32_t>(arena);
        auto relocate = [&](uint32_t& word) {
            if (word >= base && word <= base + kManagerArenaSize)
                word = 0xA0000000u + (word - base);
        };
        for (uint32_t& word : image)
            relocate(word);
        for (uint32_t& word : trace)
            relocate(word);
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    std::memcpy(reinterpret_cast<void*>(0x006F362C), savedSymbolPools, 16);
    GameVariable<uint32_t>(0x006F363C) = savedIterator[0];
    GameVariable<uint32_t>(0x006F3640) = savedIterator[1];
    RestoreScriptWriteTest();
    RestoreSymbolTable();
    Check(expected == actual, "script symbols: table, global values, comparing components");
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  symbol entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// The expression evaluator: ~90 expressions covering every operator / type combination, precedence,
// parentheses, every error and both stack overflows, fed through the original evaluator (with all its
// functions original) and ours; results, errors, stack state and arenas compared.
void TestScriptExpressions()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptExpressions skipped (the memory manager is already set up)");
        return;
    }
    for (int i = 0; i < 5; ++i)
        std::memcpy(g_saved_script_pools[i], reinterpret_cast<void*>(kScriptPoolAddresses[i]), 16);
    std::memcpy(g_saved_permanent_strings, reinterpret_cast<void*>(0x006F3570), sizeof(g_saved_permanent_strings));
    g_saved_system_ready = GameVariable<bool>(0x007CC979);
    g_saved_manager_for_test = nullptr;
    g_script_write_swapped = true;
    g_saved_symbol_table = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_symbol_table_swapped = true;
    uint8_t savedResult[16];
    std::memcpy(savedResult, reinterpret_cast<void*>(0x006F2348), 16);
    for (uint32_t address : kScriptPoolAddresses)
        std::memset(reinterpret_cast<void*>(address), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F3570), 0, sizeof(g_saved_permanent_strings));
    GameVariable<bool>(0x007CC979) = true;
    static CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;

    // Tokens: a value (index into the value table), an operator, or a parenthesis.
    struct Token {
        char kind; // 'v', 'o', '(', ')'
        int value;
    };
    enum {
        I2, I3, I4, I7, IM7, I0, F15, F25, FTINY, FNAN, N1, N2, S_AB, S_CD, S_LONG, L_X, L_Y, P1, P2, V1, V2,
        ST1, ST2, STP, NAME_A, NAME_ZZ, ARR_I, ARR_EMPTY, ARR_N, ARR_P, I1, F0, S_50, V3, V4, NAME_C, ARR_S, ARR_SP,
        S_49, ARR_F, ARR_L, ARR_PAIR, ARR_V, ARR_A, ARR_STRUCTS, NAME_INT, NAME_F, ST_SUB, kValueCount
    };
    using T = std::vector<Token>;
    auto v = [](int i) { return Token{'v', i}; };
    auto o = [](int op) { return Token{'o', op}; };
    const Token open{'(', 0}, close{')', 0};
    const int ADD = OPERATOR_ADD, SUB = OPERATOR_MINUS, MUL = OPERATOR_MULTIPLY, DIV = OPERATOR_DIVIDE,
              DOT = OPERATOR_DOT, EQ = OPERATOR_EQUALS, LT = OPERATOR_LESS_THAN, GT = OPERATOR_GREATER_THAN,
              OR = OPERATOR_OR, AND = OPERATOR_AND, ELEM = OPERATOR_ARRAY_ELEMENT;
    std::vector<T> expressions = {
        {v(I2), o(ADD), v(I3), o(MUL), v(I4)},
        {open, v(I2), o(ADD), v(I3), close, o(MUL), v(I4)},
        {v(I2), o(MUL), v(I3), o(ADD), v(I4)},
        {v(I7), o(SUB), v(I3), o(SUB), v(I2)},
        {v(I2), o(ADD), v(F15)}, {v(F15), o(ADD), v(I2)}, {v(F15), o(ADD), v(F25)},
        {v(I2), o(SUB), v(F15)}, {v(F15), o(SUB), v(I2)}, {v(F15), o(SUB), v(F25)},
        {v(I2), o(ADD), v(S_AB)}, {v(F15), o(ADD), v(S_AB)}, {v(F15), o(SUB), v(S_AB)}, {v(I2), o(SUB), v(S_AB)},
        {v(N1), o(ADD), v(I3)}, {v(N1), o(ADD), v(N2)}, {v(N1), o(ADD), v(F15)}, {v(N1), o(SUB), v(I3)},
        {v(S_AB), o(ADD), v(S_CD)}, {v(L_X), o(ADD), v(L_Y)}, {v(S_AB), o(ADD), v(L_X)}, {v(L_X), o(ADD), v(S_AB)},
        {v(S_LONG), o(ADD), v(S_LONG)}, {v(S_LONG), o(ADD), v(S_AB)},
        {v(P1), o(ADD), v(P2)}, {v(P1), o(SUB), v(P2)}, {v(P1), o(ADD), v(I2)}, {v(P1), o(SUB), v(I2)},
        {v(V1), o(ADD), v(V2)}, {v(V1), o(SUB), v(V2)}, {v(V1), o(ADD), v(P1)}, {v(V1), o(SUB), v(P1)},
        {v(V1), o(MUL), v(V2)}, {v(V1), o(DOT), v(V2)}, {v(P1), o(DOT), v(P2)}, {v(V1), o(DOT), v(P1)},
        {v(P1), o(DOT), v(V1)}, {v(I2), o(DOT), v(I3)},
        {v(I3), o(MUL), v(I4)}, {v(IM7), o(MUL), v(F15)}, {v(I3), o(MUL), v(V1)}, {v(I3), o(MUL), v(P1)},
        {v(I3), o(MUL), v(S_AB)}, {v(F15), o(MUL), v(I3)}, {v(F15), o(MUL), v(F25)}, {v(F25), o(MUL), v(V2)},
        {v(F25), o(MUL), v(P2)}, {v(F25), o(MUL), v(S_AB)}, {v(P1), o(MUL), v(I3)}, {v(P1), o(MUL), v(F25)},
        {v(P1), o(MUL), v(P2)}, {v(V1), o(MUL), v(I3)}, {v(V1), o(MUL), v(F25)}, {v(V1), o(MUL), v(P1)},
        {v(S_AB), o(MUL), v(I2)},
        {v(I7), o(DIV), v(I2)}, {v(IM7), o(DIV), v(I2)}, {v(I7), o(DIV), v(I0)}, {v(I7), o(DIV), v(FTINY)},
        {v(I7), o(DIV), v(F0)}, {v(I7), o(DIV), v(FNAN)}, {v(I7), o(DIV), v(F25)}, {v(F25), o(DIV), v(I2)},
        {v(F25), o(DIV), v(F15)}, {v(I7), o(DIV), v(S_AB)}, {v(F25), o(DIV), v(P1)}, {v(P1), o(DIV), v(I2)},
        {v(P1), o(DIV), v(F25)}, {v(P1), o(DIV), v(V1)}, {v(V1), o(DIV), v(I2)}, {v(V1), o(DIV), v(F25)},
        {v(V1), o(DIV), v(V2)}, {v(S_AB), o(DIV), v(I2)},
        {v(ST1), o(ADD), v(ST2)}, {v(ST1), o(ADD), v(STP)}, {v(STP), o(ADD), v(ST1)}, {v(ST1), o(ADD), v(I2)},
        {v(ST1), o(SUB), v(NAME_A)}, {v(ST1), o(SUB), v(ARR_N)}, {v(ST1), o(SUB), v(ARR_I)},
        {v(ST1), o(SUB), v(ARR_EMPTY)}, {v(ST1), o(SUB), v(ST2)}, {v(ST1), o(SUB), v(I2)},
        {v(ST1), o(DOT), v(NAME_A)}, {v(ST1), o(DOT), v(NAME_ZZ)}, {v(STP), o(DOT), v(NAME_A)},
        {v(ST1), o(DOT), v(I2)}, {v(STP), o(SUB), v(NAME_A)},
        {v(ARR_I), o(ADD), v(ARR_N)}, {v(ARR_EMPTY), o(ADD), v(ARR_I)}, {v(ARR_I), o(ADD), v(ARR_EMPTY)},
        {v(ARR_P), o(ADD), v(ARR_P)}, {v(ARR_I), o(ADD), v(I2)}, {v(ARR_I), o(SUB), v(ARR_I)},
        {v(ARR_I), o(ELEM), v(I1)}, {v(ARR_I), o(ELEM), v(I7)}, {v(ARR_I), o(ELEM), v(F15)}, {v(I2), o(ELEM), v(I1)},
        {v(ARR_P), o(ELEM), v(I0)}, {v(ARR_N), o(ELEM), v(I1)},
        {v(I2), o(EQ), v(I2)}, {v(I2), o(EQ), v(F25)}, {v(S_AB), o(EQ), v(S_AB)}, {v(I2), o(LT), v(I3)},
        {v(F25), o(GT), v(I2)}, {v(I3), o(LT), v(I2)},
        {v(I2), o(OR), v(I4)}, {v(I2), o(AND), v(I4)}, {v(F15), o(AND), v(I3)}, {v(FNAN), o(OR), v(I0)},
        {v(S_AB), o(OR), v(I0)}, {v(I0), o(AND), v(F0)},
        {v(I2), o(ADD), v(I3), o(EQ), v(I4), o(ADD), v(I1)},
        {v(I2), o(6), v(I3)}, {v(I2), v(I3)}, {o(ADD)}, {v(I2), o(ADD)}, {open, v(I2), o(ADD), v(I3)},
        {v(I2), o(ADD), v(I3), close}, {}, {v(I2)}, {v(STP)}, {v(ARR_P)},
        {v(S_LONG), o(ADD), v(S_50)}, {v(S_LONG), o(ADD), v(S_49)}, {v(S_LONG), o(ADD), v(S_AB), o(ADD), v(S_AB)}, {v(V3), o(DOT), v(V4)},
        {v(V4), o(DOT), v(V3)}, {v(ARR_I), o(ADD), v(ARR_S)}, {v(STP), o(DOT), v(NAME_C)},
        {v(ST2), o(DOT), v(NAME_C)}, {v(ARR_SP), o(ELEM), v(I0)}, {v(ARR_S), o(ELEM), v(I1)},
        {v(ARR_I), o(ELEM), v(I3)}, {v(ARR_I), o(ELEM), v(I2)},
        {v(ARR_F), o(ELEM), v(I1)}, {v(ARR_L), o(ELEM), v(I0)}, {v(ARR_PAIR), o(ELEM), v(I1)},
        {v(ARR_V), o(ELEM), v(I0)}, {v(ARR_A), o(ELEM), v(I0)}, {v(ARR_STRUCTS), o(ELEM), v(I0)},
        {v(ARR_SP), o(ELEM), v(I0), o(DOT), v(NAME_A)},
        {v(I2), o(LT), v(F25)}, {v(F25), o(LT), v(I2)}, {v(F15), o(LT), v(F25)}, {v(F25), o(LT), v(F15)},
        {v(FNAN), o(LT), v(I2)}, {v(I2), o(LT), v(FNAN)}, {v(FNAN), o(GT), v(F15)}, {v(F15), o(GT), v(FNAN)},
        {v(I2), o(GT), v(F25)}, {v(F25), o(GT), v(I2)}, {v(F15), o(GT), v(F25)}, {v(I3), o(GT), v(I2)},
        {v(I2), o(GT), v(I2)}, {v(I2), o(LT), v(I2)}, {v(F15), o(LT), v(F15)}, {v(F15), o(GT), v(F15)},
        {v(S_AB), o(LT), v(S_LONG)}, {v(S_LONG), o(LT), v(S_AB)}, {v(S_AB), o(GT), v(S_CD)},
        {v(S_LONG), o(GT), v(S_AB)}, {v(L_X), o(LT), v(L_Y)}, {v(L_Y), o(GT), v(L_X)}, {v(S_AB), o(LT), v(L_Y)},
        {v(ST_SUB), o(LT), v(ST1)}, {v(ST1), o(LT), v(ST_SUB)}, {v(ST1), o(GT), v(ST_SUB)},
        {v(ST_SUB), o(GT), v(ST1)}, {v(STP), o(GT), v(ST_SUB)}, {v(ST1), o(LT), v(I2)},
        {v(NAME_INT), o(LT), v(I7)}, {v(NAME_INT), o(GT), v(I7)}, {v(I2), o(LT), v(NAME_F)},
        {v(NAME_F), o(GT), v(I2)}, {v(NAME_INT), o(EQ), v(I4)}, {v(N1), o(LT), v(N2)},
        {v(P1), o(LT), v(P2)}, {v(V1), o(GT), v(V2)}, {v(IM7), o(LT), v(I2)}, {v(I2), o(LT), v(IM7)},
        {v(IM7), o(GT), v(I2)}, {v(S_AB), o(LT), v(S_CD)}, {v(S_CD), o(GT), v(S_AB)}, {v(S_LONG), o(GT), v(L_X)},
    };
    // Both stack overflows: 21 levels of "1 + (".
    {
        T deep;
        for (int i = 0; i < 21; ++i) {
            deep.push_back(v(I1));
            deep.push_back(o(ADD));
            deep.push_back(open);
        }
        expressions.push_back(deep);
        T valuesOnly;
        for (int i = 0; i < 21; ++i) {
            valuesOnly.push_back(open);
            valuesOnly.push_back(v(I1));
        }
        expressions.push_back(valuesOnly);
        T operators;
        for (int i = 0; i < 21; ++i) {
            operators.push_back(open);
            operators.push_back(v(I1));
            operators.push_back(o(SUB));
        }
        expressions.push_back(operators);
    }

    static const uint32_t kEvaluatorFunctions[] = {0x0046D160, 0x0046D180, 0x0046D1A0, 0x0046D280, 0x0046D380,
                                                   0x0046D1C0, 0x0046D220, 0x0046D250, 0x0046D260, 0x0046D2B0,
                                                   0x0046D310, 0x0046D370, 0x0046EB70, 0x0046EBF0, 0x0046EC40,
                                                   0x0046D450, 0x00479410, 0x004795A0, 0x0047A910};
    alignas(16) static uint8_t evaluatorMemory[0x200];
    static CComponent values[kValueCount];
    static CPair pairs[2];
    static CVector vectors[2];
    static char longString[160];
    static char string50[60];
    static CVector bigVectors[2];

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        uint8_t* arena = g_manager_arenas[original ? 0 : 1];
        std::memset(arena, 0, kManagerArenaSize);
        std::memset(arena + 0x1000, 0xCD, kManagerArenaSize - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + kManagerArenaSize - 0x2800);
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        auto* scriptRegion = new (arena + 0x6C0) Region(arena + kManagerArenaSize - 0x2800, arena + kManagerArenaSize);
        auto* scriptHeap = new (arena + 0x6E0) Heap(scriptRegion, 1, "script");
        manager->m_heaps[0] = heap;
        manager->m_heaps[1] = scriptHeap;
        manager->m_heapCount = 2;
        manager->m_scriptHeap = scriptHeap;
        manager->PushContext(heap);
        std::memset(reinterpret_cast<void*>(0x006F2348), 0, 16);

        // The values.
        std::memset(values, 0, sizeof(values));
        auto set = [&](int i, ESymbolType type, uint32_t raw) {
            values[i].typeAndFlag = static_cast<uint8_t>(type << 1);
            values[i].raw = raw;
        };
        auto bits = [](float f) {
            uint32_t b;
            std::memcpy(&b, &f, 4);
            return b;
        };
        set(I2, ESYMBOLTYPE_INTEGER, 2);
        set(I3, ESYMBOLTYPE_INTEGER, 3);
        set(I4, ESYMBOLTYPE_INTEGER, 4);
        set(I7, ESYMBOLTYPE_INTEGER, 7);
        set(IM7, ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(-7));
        set(I0, ESYMBOLTYPE_INTEGER, 0);
        set(I1, ESYMBOLTYPE_INTEGER, 1);
        set(F15, ESYMBOLTYPE_FLOAT, bits(1.5f));
        set(F25, ESYMBOLTYPE_FLOAT, bits(-2.5f));
        set(FTINY, ESYMBOLTYPE_FLOAT, bits(-1.0e-7f));
        set(F0, ESYMBOLTYPE_FLOAT, bits(0.0f));
        set(FNAN, ESYMBOLTYPE_FLOAT, bits(std::numeric_limits<float>::quiet_NaN()));
        set(N1, ESYMBOLTYPE_NAME, 0x10000000);
        set(N2, ESYMBOLTYPE_NAME, 0x00000123);
        set(S_AB, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>("ab"));
        set(S_CD, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>("cd"));
        std::memset(longString, 'q', 150);
        longString[150] = 0;
        set(S_LONG, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>(longString));
        std::memset(string50, 'r', 50);
        string50[50] = 0;
        set(S_50, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>(string50)); // 150 + 50 + 1: one too many
        set(S_49, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>(string50 + 1)); // 150 + 49 + 1 = 200: just fits
        // Dot products whose result depends on the order the terms are added in.
        bigVectors[0] = {0, 1.0f, 1073741824.0f, 1073741824.0f};
        bigVectors[1] = {0, 1.0f, 1073741824.0f, -1073741824.0f};
        set(V3, ESYMBOLTYPE_VECTOR, reinterpret_cast<uint32_t>(&bigVectors[0]));
        set(V4, ESYMBOLTYPE_VECTOR, reinterpret_cast<uint32_t>(&bigVectors[1]));
        set(L_X, ESYMBOLTYPE_LOCALSTRING, reinterpret_cast<uint32_t>("x"));
        set(L_Y, ESYMBOLTYPE_LOCALSTRING, reinterpret_cast<uint32_t>("yy"));
        pairs[0] = {0, 1.25f, -3.0f};
        pairs[1] = {0, 0.5f, 7.0f};
        vectors[0] = {0, 1.0f, 2.0f, 3.5f};
        vectors[1] = {0, -4.0f, 0.25f, 6.0f};
        set(P1, ESYMBOLTYPE_PAIR, reinterpret_cast<uint32_t>(&pairs[0]));
        set(P2, ESYMBOLTYPE_PAIR, reinterpret_cast<uint32_t>(&pairs[1]));
        set(V1, ESYMBOLTYPE_VECTOR, reinterpret_cast<uint32_t>(&vectors[0]));
        set(V2, ESYMBOLTYPE_VECTOR, reinterpret_cast<uint32_t>(&vectors[1]));
        CStruct* st1 = NewStruct()->Init();
        st1->AddInteger(0xA0u, 5);
        st1->AddFloat(0xA1u, 2.0f);
        st1->AddChecksum(0u, 0xA2u); // a flag
        st1->AddString(0xA3u, "s");
        CStruct* st2 = NewStruct()->Init();
        st2->AddFloat(0xA0u, 1.0f);  // same name, other type: stays
        st2->AddFloat(0xA1u, 9.0f);  // same name and type: removed
        st2->AddChecksum(0u, 0xA2u); // the flag: removed
        st2->AddInteger(0xB0u, 1);
        st2->AddStructure(0xC0u, st1); // a structure member
        set(ST1, ESYMBOLTYPE_STRUCTURE, reinterpret_cast<uint32_t>(st1));
        set(ST2, ESYMBOLTYPE_STRUCTURE, reinterpret_cast<uint32_t>(st2));
        set(STP, ESYMBOLTYPE_STRUCTUREPOINTER, reinterpret_cast<uint32_t>(st2));
        set(NAME_A, ESYMBOLTYPE_NAME, 0xA0u);
        set(NAME_ZZ, ESYMBOLTYPE_NAME, 0xFFu);
        set(NAME_C, ESYMBOLTYPE_NAME, 0xC0u);
        CArray* arrI = NewArray()->Init();
        arrI->SetSizeAndType(3, ESYMBOLTYPE_INTEGER);
        for (uint32_t i = 0; i < 3; ++i)
            arrI->SetElement(i, 0xA0u + i);
        CArray* arrN = NewArray()->Init();
        arrN->SetSizeAndType(2, ESYMBOLTYPE_NAME);
        arrN->SetElement(0, 0xA3u);
        arrN->SetElement(1, 0xA2u);
        CArray* arrE = NewArray()->Init();
        set(ARR_I, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(arrI));
        set(ARR_N, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(arrN));
        set(ARR_EMPTY, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(arrE));
        set(ARR_P, ESYMBOLTYPE_ARRAYPOINTER, reinterpret_cast<uint32_t>(arrN));
        CArray* arrS = NewArray()->Init();
        arrS->SetSizeAndType(2, ESYMBOLTYPE_STRING);
        arrS->SetElement(0, reinterpret_cast<uint32_t>(CreateString("e0")));
        arrS->SetElement(1, reinterpret_cast<uint32_t>(CreateString("e1")));
        set(ARR_S, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(arrS));
        CArray* arrStructs = NewArray()->Init();
        arrStructs->SetSizeAndType(1, ESYMBOLTYPE_STRUCTURE);
        CStruct* element = NewStruct()->Init();
        element->AddInteger(0xD0u, 8);
        arrStructs->SetElement(0, reinterpret_cast<uint32_t>(element));
        set(ARR_SP, ESYMBOLTYPE_ARRAYPOINTER, reinterpret_cast<uint32_t>(arrStructs));
        set(ARR_STRUCTS, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(arrStructs));
        CArray* arrF = NewArray()->Init();
        arrF->SetSizeAndType(2, ESYMBOLTYPE_FLOAT);
        arrF->SetFloat(0, 0.5f);
        arrF->SetFloat(1, -8.25f);
        set(ARR_F, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(arrF));
        CArray* arrL = NewArray()->Init();
        arrL->SetSizeAndType(1, ESYMBOLTYPE_LOCALSTRING);
        arrL->SetElement(0, reinterpret_cast<uint32_t>(CreateString("loc")));
        set(ARR_L, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(arrL));
        CArray* arrPair = NewArray()->Init();
        arrPair->SetSizeAndType(2, ESYMBOLTYPE_PAIR);
        CPair* pairElement = NewPair()->Init();
        pairElement->x = 3.0f;
        pairElement->y = 4.0f;
        arrPair->SetElement(1, reinterpret_cast<uint32_t>(pairElement));
        arrPair->SetElement(0, reinterpret_cast<uint32_t>(pairElement));
        set(ARR_PAIR, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(arrPair));
        CArray* arrV = NewArray()->Init();
        arrV->SetSizeAndType(1, ESYMBOLTYPE_VECTOR);
        CVector* vectorElement = NewVector()->Init();
        vectorElement->x = 7.0f;
        vectorElement->y = 8.0f;
        vectorElement->z = 9.0f;
        arrV->SetElement(0, reinterpret_cast<uint32_t>(vectorElement));
        set(ARR_V, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(arrV));
        CArray* arrA = NewArray()->Init();
        arrA->SetSizeAndType(1, ESYMBOLTYPE_ARRAY);
        arrA->SetElement(0, reinterpret_cast<uint32_t>(arrI));
        set(ARR_A, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(arrA));
        CStruct* sub = NewStruct()->Init();
        sub->AddInteger(0xA0u, 5);
        set(ST_SUB, ESYMBOLTYPE_STRUCTURE, reinterpret_cast<uint32_t>(sub));
        // Names standing for global values.
        static CSymbolTableEntry intSymbol, floatSymbol;
        intSymbol = {};
        intSymbol.type = ESYMBOLTYPE_INTEGER;
        intSymbol.name = 0x00000E01;
        intSymbol.integer = 4;
        floatSymbol = {};
        floatSymbol.type = ESYMBOLTYPE_FLOAT;
        floatSymbol.name = 0x00000E02;
        floatSymbol.floatValue = 2.5f;
        table[0xE01] = &intSymbol;
        table[0xE02] = &floatSymbol;
        set(NAME_INT, ESYMBOLTYPE_NAME, 0xE01u);
        set(NAME_F, ESYMBOLTYPE_NAME, 0xE02u);

        std::vector<OriginalCode*> guards;
        if (original) {
            for (uint32_t address : kEvaluatorFunctions)
                guards.push_back(new OriginalCode(address));
        }
        auto* e = reinterpret_cast<CExpressionEvaluator*>(evaluatorMemory);
        std::memset(evaluatorMemory, 0xEE, sizeof(evaluatorMemory));
        using E = CExpressionEvaluator*;
        if (original)
            Original<E(__thiscall*)(E)>(0x0046D380)(e);
        else
            e->Init();
        for (const T& expression : expressions) {
            if (original)
                Original<void(__thiscall*)(E)>(0x0046D220)(e);
            else
                e->ClearIfNeeded();
            if (original)
                Original<void(__thiscall*)(E, const uint8_t*)>(0x0046D250)(e, reinterpret_cast<const uint8_t*>(1));
            else
                e->SetTokenPointer(reinterpret_cast<const uint8_t*>(1));
            for (const Token& token : expression) {
                if (e->m_error)
                    break;
                switch (token.kind) {
                case 'v':
                    if (original)
                        Original<void(__thiscall*)(E, const CComponent*)>(0x0046D310)(e, &values[token.value]);
                    else
                        e->Input(&values[token.value]);
                    break;
                case 'o':
                    if (original)
                        Original<void(__thiscall*)(E, int)>(0x0046EB70)(e, token.value);
                    else
                        e->Input(token.value);
                    break;
                case '(':
                    if (original)
                        Original<void(__thiscall*)(E)>(0x0046D370)(e);
                    else
                        e->OpenParenthesis();
                    break;
                default:
                    if (original)
                        Original<void(__thiscall*)(E)>(0x0046EBF0)(e);
                    else
                        e->CloseParenthesis();
                    break;
                }
            }
            CComponent result = {};
            bool ok = false;
            if (!e->m_error) {
                ok = original ? Original<bool(__thiscall*)(E, CComponent*)>(0x0046EC40)(e, &result)
                              : e->GetResult(&result);
            }
            trace.push_back(ok ? 1u : 0u);
            trace.push_back(reinterpret_cast<uint32_t>(e->m_error));
            trace.push_back(static_cast<uint32_t>(e->m_valueTop));
            trace.push_back(static_cast<uint32_t>(e->m_operatorTop));
            trace.push_back(e->m_hasOperators ? 1u : 0u);
            trace.push_back(result.typeAndFlag);
            trace.push_back(result.raw);
            switch (result.Type()) {
            case ESYMBOLTYPE_PAIR:
                trace.push_back(bits(result.pair->x));
                trace.push_back(bits(result.pair->y));
                break;
            case ESYMBOLTYPE_VECTOR:
                trace.push_back(bits(result.vector->x));
                trace.push_back(bits(result.vector->y));
                trace.push_back(bits(result.vector->z));
                break;
            case ESYMBOLTYPE_STRING:
            case ESYMBOLTYPE_LOCALSTRING:
                for (const char* c = result.string; *c; ++c)
                    trace.push_back(static_cast<uint8_t>(*c));
                break;
            case ESYMBOLTYPE_STRUCTURE:
                for (CComponent* c = result.structure->m_head; c; c = c->next) {
                    trace.push_back(c->typeAndFlag);
                    trace.push_back(c->name);
                    trace.push_back(c->Type() == ESYMBOLTYPE_STRING ? 0 : c->raw);
                }
                break;
            case ESYMBOLTYPE_ARRAY:
                trace.push_back(result.array->m_size);
                trace.push_back(result.array->m_type);
                for (uint32_t i = 0; i < result.array->m_size; ++i)
                    trace.push_back(result.array->GetElement(i));
                break;
            default:
                break;
            }
            CleanUpComponent(&result);
        }
        if (original)
            Original<void(__thiscall*)(E)>(0x0046D1C0)(e);
        else
            e->Clear();
        for (OriginalCode* guard : guards)
            delete guard;
        trace.insert(trace.end(), reinterpret_cast<uint32_t*>(evaluatorMemory),
                     reinterpret_cast<uint32_t*>(evaluatorMemory + sizeof(evaluatorMemory)));
        trace.insert(trace.end(), reinterpret_cast<uint32_t*>(0x006F2348), reinterpret_cast<uint32_t*>(0x006F2358));

        std::vector<uint32_t> image(kManagerArenaSize / 4);
        std::memcpy(image.data(), arena, kManagerArenaSize);
        auto base = reinterpret_cast<uint32_t>(arena);
        auto relocate = [&](uint32_t& word) {
            if (word >= base && word <= base + kManagerArenaSize)
                word = 0xA0000000u + (word - base);
        };
        for (uint32_t& word : image)
            relocate(word);
        for (uint32_t& word : trace)
            relocate(word);
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    std::memcpy(reinterpret_cast<void*>(0x006F2348), savedResult, 16);
    RestoreScriptWriteTest();
    RestoreSymbolTable();
    Check(expected == actual, "script expressions: every operator, precedence, errors");
    if (expected.size() != actual.size())
        Log("  expression trace sizes: original %u ours %u", static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  expression entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// Checksum names (adding, clashes, a bucket running over, finding, "Unknown(...)", parsing a .chk text) on
// a stand-in table, and registering script functions as symbols; originals vs ours.
std::vector<uint32_t>* g_parsed_names = nullptr;
void __cdecl RecordParsedName(uint32_t checksum, const char* name)
{
    g_parsed_names->push_back(checksum);
    for (const char* c = name; *c; ++c)
        g_parsed_names->push_back(static_cast<uint8_t>(*c));
    g_parsed_names->push_back(0xFFFFFFFF);
}

void TestScriptChecksumNames()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptChecksumNames skipped (the memory manager is already set up)");
        return;
    }
    for (int i = 0; i < 5; ++i)
        std::memcpy(g_saved_script_pools[i], reinterpret_cast<void*>(kScriptPoolAddresses[i]), 16);
    std::memcpy(g_saved_permanent_strings, reinterpret_cast<void*>(0x006F3570), sizeof(g_saved_permanent_strings));
    g_saved_system_ready = GameVariable<bool>(0x007CC979);
    g_saved_manager_for_test = nullptr;
    g_script_write_swapped = true;
    g_saved_symbol_table = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_symbol_table_swapped = true;
    uint8_t savedSymbolPools[16];
    std::memcpy(savedSymbolPools, reinterpret_cast<void*>(0x006F362C), 16);
    uint32_t savedNames[3];
    std::memcpy(savedNames, reinterpret_cast<void*>(0x006F232C), 12);
    bool savedEnabled = GameVariable<bool>(0x007CEFAC);
    char savedUnknown[32];
    std::memcpy(savedUnknown, reinterpret_cast<void*>(0x006F22C8), 32);
    for (uint32_t address : kScriptPoolAddresses)
        std::memset(reinterpret_cast<void*>(address), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F362C), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F3570), 0, sizeof(g_saved_permanent_strings));
    GameVariable<bool>(0x007CC979) = true;
    static CSymbolTableEntry* table[0x1000];
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;
    std::vector<uint8_t> nameTable(0x500000);
    std::vector<char> nameBlock(0x10000);

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        g_parsed_names = &trace;
        std::memset(table, 0, sizeof(table));
        std::memset(nameTable.data(), 0, nameTable.size());
        std::memset(nameBlock.data(), 0, nameBlock.size());
        GameVariable<void*>(0x006F232C) = nameTable.data();
        GameVariable<char*>(0x006F2330) = nameBlock.data();
        GameVariable<char*>(0x006F2334) = nameBlock.data();
        uint8_t* arena = g_manager_arenas[original ? 0 : 1];
        std::memset(arena, 0, kManagerArenaSize);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + kManagerArenaSize);
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        manager->m_heaps[0] = heap;
        manager->m_heapCount = 1;
        manager->PushContext(heap);

        auto add = [&](uint32_t checksum, const char* name) {
            if (original)
                Original<void(__cdecl*)(uint32_t, const char*)>(0x0046CF60)(checksum, name);
            else
                AddChecksumName(checksum, name);
        };
        auto find = [&](uint32_t checksum) {
            return original ? Original<const char*(__cdecl*)(uint32_t)>(0x0046D0C0)(checksum)
                            : FindChecksumName(checksum);
        };
        auto find_or_unknown = [&](uint32_t checksum) {
            return original ? Original<const char*(__cdecl*)(uint32_t)>(0x0046D110)(checksum)
                            : FindChecksumNameOrUnknown(checksum);
        };
        auto put_name = [&](const char* name) {
            trace.push_back(name ? 1 : 0);
            if (name) {
                trace.push_back(static_cast<uint32_t>(name - nameBlock.data()));
                for (const char* c = name; *c; ++c)
                    trace.push_back(static_cast<uint8_t>(*c));
            }
        };
        for (bool enabled : {false, true}) {
            GameVariable<bool>(0x007CEFAC) = enabled;
            add(0x00010005, "alpha");
            add(0x00020005, "beta");   // same bucket
            // (adding a known checksum and the "Unknown(...)" text use the game's C runtime: late test)
            add(0x12345678, "");
            for (uint32_t i = 0; i < 12; ++i) {
                char name[8] = {'n', static_cast<char>('a' + i), 0};
                add(0x00300007 + (i << 16), name); // 12 in one bucket: runs into the next
            }
            for (uint32_t checksum : {0x00010005u, 0x00020005u, 0x00030005u, 0x12345678u, 0x00300007u, 0x00390007u,
                                      0x003A0007u, 0x003B0007u, 0x00000008u}) {
                put_name(find(checksum));
                if (enabled && find(checksum))
                    put_name(find_or_unknown(checksum));
            }
            trace.push_back(static_cast<uint32_t>(GameVariable<char*>(0x006F2334) - nameBlock.data()));
        }
        // Parsing (odd characters in the digits count as their codes; an empty name).
        const char text[] = "0x0000ABCD first\r\n0xFFFFFFFF Second_Name\r\n0x12zZ5678 odd\r\n0x00000001 \r\n";
        if (original)
            Original<void(__cdecl*)(const char*, int, void (*)(uint32_t, const char*))>(0x0046CD90)(
                text, static_cast<int>(sizeof(text) - 1), RecordParsedName);
        else
            ParseChecksumNames(text, static_cast<int>(sizeof(text) - 1), RecordParsedName);

        // Registering.
        static const CFunctionEntry functions[] = {{"FuncOne", reinterpret_cast<void*>(0x1111)},
                                                   {"FuncTwo", reinterpret_cast<void*>(0x2222)}};
        static const char* const members[] = {"MemberA", "MemberB", "MemberC"};
        static const uint32_t records[] = {0xAAAA0001, 1, 2, 3, 0xBBBB0002, 4, 5, 6, 0, 0, 0, 0};
        if (original) {
            Original<void(__cdecl*)(const CFunctionEntry*, uint32_t)>(0x0046EFD0)(functions, 2);
            Original<void(__cdecl*)(const char* const*, uint32_t)>(0x0046F020)(members, 3);
            Original<void(__cdecl*)(void*, const uint32_t*, int)>(0x0046F060)(nullptr, records, 16);
        } else {
            RegisterCFunctions(functions, 2);
            RegisterMemberFunctions(members, 3);
            RegisterMemberFunctionTable(nullptr, records, 16);
        }
        for (CSymbolTableEntry* entry = GetNextSymbol(nullptr); entry; entry = GetNextSymbol(entry)) {
            trace.push_back(entry->name);
            trace.push_back(entry->unknown0);
            trace.push_back(entry->type);
            trace.push_back(entry->raw);
        }
        trace.push_back(static_cast<uint32_t>(GameVariable<char*>(0x006F2334) - nameBlock.data()));
        for (size_t i = 0; i < 0x2000; ++i) // the first buckets
            trace.push_back(reinterpret_cast<uint32_t*>(nameTable.data())[i]);
        for (size_t i = 0; i < 0x40; ++i)
            trace.push_back(reinterpret_cast<uint32_t*>(nameBlock.data())[i]);
        GameVariable<void*>(0x007D1800) = nullptr;
        g_parsed_names = nullptr;
        // (pointers into the stand-in blocks, relative)
        auto blockBase = reinterpret_cast<uint32_t>(nameBlock.data());
        for (uint32_t& word : trace) {
            if (word >= blockBase && word < blockBase + nameBlock.size())
                word = 0xB0000000u + (word - blockBase);
            if (word >= reinterpret_cast<uint32_t>(arena) && word < reinterpret_cast<uint32_t>(arena) + kManagerArenaSize)
                word = 0xA0000000u + (word - reinterpret_cast<uint32_t>(arena));
        }
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    GameVariable<bool>(0x007CEFAC) = savedEnabled;
    std::memcpy(reinterpret_cast<void*>(0x006F232C), savedNames, 12);
    std::memcpy(reinterpret_cast<void*>(0x006F22C8), savedUnknown, 32);
    std::memcpy(reinterpret_cast<void*>(0x006F362C), savedSymbolPools, 16);
    RestoreScriptWriteTest();
    RestoreSymbolTable();
    Check(expected == actual, "checksum names and script function registration");
    if (expected.size() != actual.size())
        Log("  names trace sizes: original %u ours %u", static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  names entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// The checksum-name paths that use the game's C runtime (in the game, after it has started): adding a
// checksum that's already known (the original compares the names) and the "Unknown(0x...)" text.
void TestScriptChecksumNamesLate()
{
    using namespace Script;
    uint32_t savedNames[3];
    std::memcpy(savedNames, reinterpret_cast<void*>(0x006F232C), 12);
    bool savedEnabled = GameVariable<bool>(0x007CEFAC);
    char savedUnknown[32];
    std::memcpy(savedUnknown, reinterpret_cast<void*>(0x006F22C8), 32);
    std::vector<uint8_t> nameTable(0x500000);
    std::vector<char> nameBlock(0x1000);
    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        std::memset(nameTable.data(), 0, nameTable.size());
        std::memset(nameBlock.data(), 0, nameBlock.size());
        GameVariable<void*>(0x006F232C) = nameTable.data();
        GameVariable<char*>(0x006F2330) = nameBlock.data();
        GameVariable<char*>(0x006F2334) = nameBlock.data();
        GameVariable<bool>(0x007CEFAC) = true;
        using AddFn = void(__cdecl*)(uint32_t, const char*);
        using FindFn = const char*(__cdecl*)(uint32_t);
        auto add = [&](uint32_t checksum, const char* name) {
            if (original)
                Original<AddFn>(0x0046CF60)(checksum, name);
            else
                AddChecksumName(checksum, name);
        };
        add(0x00010005, "alpha");
        add(0x00010005, "ALPHA");
        add(0x00010005, "other");
        for (uint32_t checksum : {0x00010005u, 0x00020005u, 0xDEADBEEFu, 0u}) {
            const char* name = original ? Original<FindFn>(0x0046D110)(checksum) : FindChecksumNameOrUnknown(checksum);
            for (const char* c = name; *c; ++c)
                trace.push_back(static_cast<uint8_t>(*c));
            trace.push_back(0xFFFFFFFF);
        }
        trace.push_back(static_cast<uint32_t>(GameVariable<char*>(0x006F2334) - nameBlock.data()));
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    GameVariable<bool>(0x007CEFAC) = savedEnabled;
    std::memcpy(reinterpret_cast<void*>(0x006F232C), savedNames, 12);
    std::memcpy(reinterpret_cast<void*>(0x006F22C8), savedUnknown, 32);
    Check(expected == actual, "checksum names: known checksums and Unknown(...)");
}

// The .qb token helpers over a handmade token stream (every token kind), pre-processing names into function
// references, the .qb checksum-name table, the script cache, stored-random shuffles, pool creation and the
// permanent string heap; originals vs ours.
alignas(16) uint8_t g_qb_arenas[2][0x20000];

void TestScriptTokens()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptTokens skipped (the memory manager is already set up)");
        return;
    }
    // Globals touched: saved and put back.
    struct Saved {
        uint32_t address;
        uint32_t size;
        uint8_t bytes[32];
    };
    Saved saved[] = {{0x006F2368, 4}, {0x006F2358, 4},  {0x00690FC8, 24}, {0x006F2578, 16},
                     {0x006F3560, 16}, {0x006F357C, 16}, {0x006F3588, 4},  {0x007CEFAC, 1},
                     {0x007CC979, 1}};
    for (Saved& s : saved)
        std::memcpy(s.bytes, reinterpret_cast<void*>(s.address), s.size);
    g_saved_symbol_table = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_symbol_table_swapped = true;
    static CSymbolTableEntry* table[0x1000];
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;
    GameVariable<bool>(0x007CEFAC) = false;
    GameVariable<bool>(0x007CC979) = true;

    // The token stream: every token kind with its data.
    std::vector<uint8_t> stream;
    auto u8 = [&](uint32_t b) { stream.push_back(static_cast<uint8_t>(b)); };
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            u8(v >> (8 * i));
    };
    for (uint32_t t = 1; t <= 0x4A; ++t) {
        switch (t) {
        case 0x02: case 0x16: case 0x17: case 0x18: case 0x1A: case 0x2E: case 0x43: case 0x44: case 0x46:
            u8(t), u32(0x11223344 + t);
            break;
        case 0x1B: case 0x1C:
            u8(t), u32(3), u8('a'), u8('b'), u8(0);
            break;
        case 0x1E:
            u8(t), u32(1), u32(2), u32(3);
            break;
        case 0x1F:
            u8(t), u32(1), u32(2);
            break;
        case 0x2B:
            u8(t), u32(0xABCD), u8('n'), u8('m'), u8(0);
            break;
        case 0x2F: case 0x37: case 0x40: case 0x41:
            u8(t), u32(2);
            for (int i = 0; i < 12; ++i)
                u8(i);
            break;
        case 0x47: case 0x48: case 0x49:
            u8(t), u8(1), u8(2);
            break;
        case 0x10: case 0x19: case 0x1D: case 0x2A: case 0x31: case 0x4A:
            break; // (tokens that don't step: tried on their own below)
        default:
            u8(t);
            break;
        }
    }
    const size_t tokenCount = stream.size();
    // Statements: nested brackets, names that are / aren't functions, a short jump, the end.
    const uint8_t statements[] = {
        0x01, 0x16, 0x01, 0x00, 0x00, 0xC0,                               // CFunction
        0x03, 0x16, 0x09, 0, 0, 0, 0x01, 0x05, 0x17, 1, 0, 0, 0, 0x06, 0x04, // { name [ 1 ] }
        0x01, 0x16, 0x02, 0x00, 0x00, 0xC0,                               // member function with a table
        0x0E, 0x17, 2, 0, 0, 0, 0x0F, 0x02, 9, 0, 0, 0,                   // ( 2 ) + line number
        0x16, 0x03, 0x00, 0x00, 0xC0, 0x01,                               // member function without
        0x16, 0x04, 0x00, 0x00, 0xC0, 0x01,                               // a name that isn't a function
        0x47, 5, 0, 0x39, 0x01,                                           // short jump, then 0x39
        0x47, 5, 0, 0x39, 0x16, 0x01, 0x00, 0x00, 0xC0, 0x01,             // ... and a call after it
        0x47, 5, 0, 0x16, 0x01, 0x00, 0x00, 0xC0, 0x01,                   // short jump, then a call
        0x16, 0x01, 0x00, 0x00, 0xC0, 0x03, 0x01, 0x04, 0x01,             // a call with a structure
        0x24, 0x02, 7, 0, 0, 0, 0x01};

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        uint8_t* arena = g_qb_arenas[original ? 0 : 1];
        std::memset(arena, 0, sizeof(g_qb_arenas[0]));
        std::memset(arena + 0x1000, 0xCD, sizeof(g_qb_arenas[0]) - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + sizeof(g_qb_arenas[0]));
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        manager->m_heaps[0] = heap;
        manager->m_heapCount = 1;
        manager->m_scriptHeap = heap;
        manager->PushContext(heap);
        auto base = reinterpret_cast<uint32_t>(arena);
        auto relocate = [&](uint32_t word) {
            return word >= base && word <= base + sizeof(g_qb_arenas[0]) ? 0xA0000000u + (word - base) : word;
        };
        using P = const uint8_t*;

        // Skipping each token, and the lone tokens that don't step.
        for (size_t i = 0; i < tokenCount;) {
            P next = original ? Original<P(__cdecl*)(P)>(0x0046F690)(&stream[i]) : SkipToken(&stream[i]);
            trace.push_back(static_cast<uint32_t>(next - stream.data()));
            if (next == &stream[i])
                break;
            i = static_cast<size_t>(next - stream.data());
        }
        static const uint8_t kLoneTokens[] = {0x00, 0x10, 0x19, 0x1D, 0x2A, 0x31, 0x4A, 0x80, 0xFF};
        for (uint8_t lone : kLoneTokens) {
            P next = original ? Original<P(__cdecl*)(P)>(0x0046F690)(&lone) : SkipToken(&lone);
            trace.push_back(static_cast<uint32_t>(next - &lone));
        }
        // Reading and writing.
        uint8_t bytes[8] = {0x81, 0x92, 0xA3, 0xB4, 0xC5, 0, 0, 0};
        trace.push_back(original ? Original<uint32_t(__cdecl*)(P)>(0x0046F7E0)(bytes + 1) : Read4Bytes(bytes + 1));
        trace.push_back(original ? Original<uint16_t(__cdecl*)(P)>(0x0046F800)(bytes + 3) : Read2Bytes(bytes + 3));
        uint8_t* end = original ? Original<uint8_t*(__cdecl*)(uint8_t*, uint32_t)>(0x0046F810)(bytes + 1, 0xDEADBEEF)
                                : Write4Bytes(bytes + 1, 0xDEADBEEF);
        trace.push_back(static_cast<uint32_t>(end - bytes));
        end = original ? Original<uint8_t*(__cdecl*)(uint8_t*, uint16_t)>(0x0046F840)(bytes + 5, 0x1357)
                       : Write2Bytes(bytes + 5, 0x1357);
        trace.push_back(static_cast<uint32_t>(end - bytes));
        trace.push_back(Read4Bytes(bytes) ^ Read4Bytes(bytes + 4));

        // Statements.
        std::vector<uint8_t> code(statements, statements + sizeof(statements));
        P start = code.data();
        for (size_t at = 0; at < code.size(); ++at) {
            P p = start + at;
            trace.push_back(static_cast<uint32_t>(
                (original ? Original<P(__cdecl*)(P)>(0x0046F850)(p) : SkipEndOfLines(p)) - start));
        }
        bool statementStart = true;
        int depth = 0;
        for (P p = start; *p != 0x24; p = SkipToken(p)) {
            if (statementStart) {
                trace.push_back(static_cast<uint32_t>(
                    (original ? Original<P(__cdecl*)(P)>(0x0046FB30)(p) : SkipToEndOfStatement(p)) - start));
            }
                if (*p == 0x03 || *p == 0x05 || *p == 0x0E)
                ++depth;
            if (*p == 0x04 || *p == 0x06 || *p == 0x0F)
                --depth;
            statementStart = depth == 0 && (*p == 0x01 || *p == 0x02);
            trace.push_back(static_cast<uint32_t>(
                (original ? Original<P(__cdecl*)(P)>(0x0046F920)(p) : SkipToEndOfScript(p)) - start));
            trace.push_back(original ? Original<uint32_t(__cdecl*)(P)>(0x0046F960)(p) : CalculateScriptChecksum(p));
        }
        // A statement ended by a 0x00 token.
        static const uint8_t kEndedByZero[] = {0x16, 1, 2, 3, 4, 0x17, 1, 0, 0, 0, 0x00, 0x24};
        trace.push_back(static_cast<uint32_t>(
            (original ? Original<P(__cdecl*)(P)>(0x0046FB30)(kEndedByZero) : SkipToEndOfStatement(kEndedByZero)) -
            kEndedByZero));
        // Pre-processing: 0xC0000001 a C function, 0xC0000002 a member function with a table entry,
        // 0xC0000003 one without, 0xC0000004 an integer.
        static CSymbolTableEntry symbols[4];
        std::memset(table, 0, sizeof(table));
        const ESymbolType types[4] = {ESYMBOLTYPE_CFUNCTION, ESYMBOLTYPE_MEMBERFUNCTION,
                                      ESYMBOLTYPE_MEMBERFUNCTION, ESYMBOLTYPE_INTEGER};
        const uint32_t values[4] = {0x00401000, 0x00402000, 0, 5};
        for (int i = 0; i < 4; ++i) {
            symbols[i] = {};
            symbols[i].name = 0xC0000001 + i;
            symbols[i].type = types[i];
            symbols[i].raw = values[i];
            table[(0xC0000001 + i) & 0xFFF] = &symbols[i];
        }
        if (original)
            Original<void(__cdecl*)(uint8_t*)>(0x0046FF00)(code.data());
        else
            PreProcessScript(code.data());
        for (uint8_t b : code)
            trace.push_back(b);

        // The .qb checksum-name table.
        auto* names = static_cast<uint8_t*>(Mem::Malloc(0x1000 * 12));
        std::memset(names, 0, 0x1000 * 12);
        GameVariable<void*>(0x006F2368) = names;
        auto add = [&](uint32_t checksum, const char* name) {
            if (original)
                Original<void(__cdecl*)(uint32_t, const char*)>(0x0046FA60)(checksum, name);
            else
                AddScriptChecksumName(checksum, name);
        };
        add(0x00000123, "first");
        add(0x00001123, "second"); // same bucket
        add(0x00002123, "third");
        add(0x00000123, "again");  // added again
        add(0x00000FFF, "last");
        for (uint32_t checksum : {0x123u, 0x1123u, 0x2123u, 0x3123u, 0xFFFu, 0x0u, 0x7u}) {
            const char* name = original ? Original<const char*(__cdecl*)(uint32_t)>(0x0046F9B0)(checksum)
                                        : FindScriptChecksumName(checksum);
            trace.push_back(name ? relocate(reinterpret_cast<uint32_t>(name)) : 0);
            if (name)
                trace.push_back(static_cast<uint8_t>(name[0]));
        }
        if (original)
            Original<void(__cdecl*)()>(0x0046F9F0)();
        else
            FreeScriptChecksumNames();
        trace.push_back(GameVariable<uint32_t>(0x006F2368));
        trace.push_back(static_cast<uint32_t>(
            original ? Original<const char*(__cdecl*)(uint32_t)>(0x0046F9B0)(0x123) != nullptr
                     : FindScriptChecksumName(0x123) != nullptr));

        // The script cache.
        static uint32_t cache[3][0x5C / 4];
        std::memset(cache, 0, sizeof(cache));
        cache[0][0] = 7;
        cache[1][0] = 8;
        cache[2][0] = 7;
        cache[0][0x58 / 4] = reinterpret_cast<uint32_t>(cache[1]);
        cache[1][0x58 / 4] = reinterpret_cast<uint32_t>(cache[2]);
        GameVariable<void*>(0x006F2358) = cache[0];
        if (original)
            Original<void(__cdecl*)(uint32_t)>(0x0046F870)(7);
        else
            ForgetStoredRandomsOf(7);
        GameVariable<void*>(0x006F2358) = nullptr;
        for (auto& entry : cache)
            trace.push_back(entry[0]);

        // Stored-random shuffles (the same random state for both).
        GameVariable<uint32_t>(0x00690FC8) = 0x12345;
        GameVariable<uint32_t>(0x00690FCC) = 0x6789;
        GameVariable<uint32_t>(0x00690FD0) = 0xABC;
        GameVariable<uint32_t>(0x00690FD4) = 0xDEF01;
        GameVariable<uint32_t>(0x00690FD8) = 0x2345;
        GameVariable<uint32_t>(0x00690FDC) = 0x678;
        alignas(4) static uint8_t random[0x60];
        auto* stored = reinterpret_cast<CStoredRandom*>(random);
        static const uint16_t kCounts[] = {1, 2, 5, 9};
        for (uint16_t count : kCounts) {
            std::memset(random, 0, sizeof(random));
            stored->m_count = count;
            for (uint8_t i = 0; i < count; ++i)
                stored->m_order[i] = i;
            for (int round = 0; round < 6; ++round) {
                bool avoid = round % 2 == 0;
                if (count == 1)
                    avoid = false;
                if (original)
                    Original<void(__thiscall*)(CStoredRandom*, bool)>(0x0046F8A0)(stored, avoid);
                else
                    stored->Shuffle(avoid);
                for (uint8_t i = 0; i < count; ++i)
                    trace.push_back(stored->m_order[i]);
            }
        }

        // Pools and the permanent string heap.
        for (uint32_t address : {0x006F2578u, 0x006F3560u})
            std::memset(reinterpret_cast<void*>(address), 0, 16);
        GameVariable<int>(0x006F2578) = 1; // the second slot
        if (original) {
            Original<void(__cdecl*)(int, const char*)>(0x0046F4A0)(5, "random pool");
            Original<void(__cdecl*)(int, const char*)>(0x0046F090)(3, "cache pool");
            Original<void(__cdecl*)(uint32_t, uint32_t)>(0x00476210)(0x100, 9);
        } else {
            CreatePool(GameVariable<Mem::PoolPair>(0x006F2578), 0x60, 5, "random pool");
            CreatePool(GameVariable<Mem::PoolPair>(0x006F3560), 0x14, 3, "cache pool");
            AllocatePermanentStringHeap(0x100, 9);
        }
        for (uint32_t address : {0x006F2578u, 0x006F3560u, 0x006F357Cu}) {
            for (int i = 0; i < 4; ++i)
                trace.push_back(relocate(reinterpret_cast<uint32_t*>(address)[i]));
        }
        trace.push_back(GameVariable<uint32_t>(0x006F3588));

        std::vector<uint32_t> image(sizeof(g_qb_arenas[0]) / 4);
        std::memcpy(image.data(), arena, sizeof(g_qb_arenas[0]));
        for (uint32_t& word : image)
            word = relocate(word);
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes, s.size);
    RestoreSymbolTable();
    Check(expected == actual, "script tokens, pre-processing, .qb names, pools");
    if (expected.size() != actual.size())
        Log("  token trace sizes: original %u ours %u", static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  token entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// Symbol bookkeeping for the loader: removing symbols of every type (and a kept one), pending removals and
// replacements, the pool frees (pooled and not) and the .qb name table allocation; originals vs ours.
// A stand-in script cache: an empty hash table (so dropping a script finds nothing), for the code that tells
// the cache about changed scripts - through the game's method, which needs the object in ECX.
void* StandInScriptCache()
{
    static uint32_t entries[16 * 3];
    static uint32_t header[2];
    static uint32_t cache[8];
    std::memset(entries, 0, sizeof(entries));
    header[0] = 4; // 1 << 4 buckets
    header[1] = reinterpret_cast<uint32_t>(entries);
    std::memset(cache, 0, sizeof(cache));
    cache[0x14 / 4] = reinterpret_cast<uint32_t>(header);
    return cache;
}

void TestScriptSymbolBookkeeping()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptSymbolBookkeeping skipped (the memory manager is already set up)");
        return;
    }
    struct Saved {
        uint32_t address;
        uint32_t size;
        uint8_t bytes[32];
    };
    Saved saved[] = {{0x006F2368, 4},  {0x006F3528, 4},  {0x006F3558, 4},  {0x006F2578, 16}, {0x006F362C, 16},
                     {0x006F3644, 16}, {0x006F3654, 16}, {0x006F2338, 16}, {0x006F3614, 16}, {0x006F22B4, 16},
                     {0x006F3570, 24}, {0x007CC979, 1},  {0x006F2564, 1}};
    for (Saved& s : saved)
        std::memcpy(s.bytes, reinterpret_cast<void*>(s.address), s.size);
    std::vector<uint8_t> pendingSaved(0x40);
    std::memcpy(pendingSaved.data(), reinterpret_cast<void*>(0x006F2588), pendingSaved.size());
    g_saved_symbol_table = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_symbol_table_swapped = true;
    static CSymbolTableEntry* table[0x1000];
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;
    GameVariable<bool>(0x007CC979) = true;
    GameVariable<void*>(0x006F3558) = StandInScriptCache();

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        for (Saved& s : saved) {
            if (s.address != 0x007CC979 && s.address != 0x006F3558)
                std::memset(reinterpret_cast<void*>(s.address), 0, s.size);
        }
        std::memset(table, 0, sizeof(table));
        uint8_t* arena = g_qb_arenas[original ? 0 : 1];
        std::memset(arena, 0, sizeof(g_qb_arenas[0]));
        std::memset(arena + 0x1000, 0xCD, sizeof(g_qb_arenas[0]) - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + 0x10000);
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        auto* debugRegion = new (arena + 0x6C0) Region(arena + 0x10000, arena + sizeof(g_qb_arenas[0]));
        auto* debugHeap = new (arena + 0x6E0) Heap(debugRegion, 1, "debug");
        manager->m_heaps[0] = heap;
        manager->m_heaps[1] = debugHeap;
        manager->m_heapCount = 2;
        manager->m_scriptHeap = heap;
        manager->m_debugHeap = debugHeap;
        manager->PushContext(heap);
        auto base = reinterpret_cast<uint32_t>(arena);
        auto relocate = [&](uint32_t word) {
            return word >= base && word <= base + sizeof(g_qb_arenas[0]) ? 0xA0000000u + (word - base) : word;
        };
        using S = CSymbolTableEntry*;
        auto remove = [&](S symbol) {
            if (original)
                Original<void(__cdecl*)(S)>(0x004711D0)(symbol);
            else
                CleanUpAndRemoveSymbol(symbol);
        };
        // The .qb name table, from each heap choice.
        for (bool debug : {false, true}) {
            if (original)
                Original<void(__cdecl*)(bool)>(0x00470010)(debug);
            else
                AllocateScriptChecksumNames(debug);
            trace.push_back(relocate(GameVariable<uint32_t>(0x006F2368)));
            Mem::FreeMalloc(GameVariable<void*>(0x006F2368));
        }
        // Symbols of every type.
        auto symbol = [&](uint32_t name, ESymbolType type, uint32_t value) {
            S entry = CreateSymbol(name);
            entry->type = type;
            entry->raw = value;
            return entry;
        };
        CStruct* structure = NewStruct()->Init();
        structure->AddInteger(1u, 2);
        CArray* array = NewArray()->Init();
        array->SetSizeAndType(2, ESYMBOLTYPE_STRING);
        array->SetElement(0, reinterpret_cast<uint32_t>(CreateString("x")));
        array->SetElement(1, reinterpret_cast<uint32_t>(CreateString("y")));
        S symbols[] = {
            symbol(0x101, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>(CreateString("text"))),
            symbol(0x102, ESYMBOLTYPE_LOCALSTRING, reinterpret_cast<uint32_t>(CreateString("local"))),
            symbol(0x103, ESYMBOLTYPE_PAIR, reinterpret_cast<uint32_t>(NewPair()->Init())),
            symbol(0x104, ESYMBOLTYPE_VECTOR, reinterpret_cast<uint32_t>(NewVector()->Init())),
            symbol(0x105, ESYMBOLTYPE_QSCRIPT, reinterpret_cast<uint32_t>(Mem::Malloc(24))),
            symbol(0x106, ESYMBOLTYPE_STRUCTURE, reinterpret_cast<uint32_t>(structure)),
            symbol(0x107, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(array)),
            symbol(0x108, ESYMBOLTYPE_STRUCTURE, 0),
            symbol(0x109, ESYMBOLTYPE_INTEGER, 5),
            symbol(0x10A, ESYMBOLTYPE_CFUNCTION, 0x00401000),
            symbol(0x10B, ESYMBOLTYPE_NAME, 0x1234),
            symbol(0x10C, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>(CreateString("kept"))),
        };
        symbols[11]->unknown0 |= 4; // kept
        for (S entry : symbols)
            remove(entry);
        trace.push_back(symbols[11]->raw != 0);
        // Pending removals and replacements.
        S gone = symbol(0x201, ESYMBOLTYPE_INTEGER, 1);
        S old = symbol(0x202, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>(CreateString("old")));
        S replacement = symbol(0x202, ESYMBOLTYPE_INTEGER, 9);
        UnlinkSymbol(replacement);
        (void)old;
        if (original) {
            Original<void(__cdecl*)(S)>(0x004727B0)(gone);
            Original<void(__cdecl*)(S)>(0x004727F0)(replacement);
            trace.push_back(GameVariable<uint32_t>(0x006F3528));
            Original<void(__cdecl*)()>(0x00472830)();
        } else {
            RemoveSymbolLater(gone);
            ReplaceSymbolLater(replacement);
            trace.push_back(GameVariable<uint32_t>(0x006F3528));
            ProcessPendingSymbols();
        }
        trace.push_back(GameVariable<uint32_t>(0x006F3528));
        for (S entry = GetNextSymbol(nullptr); entry; entry = GetNextSymbol(entry)) {
            trace.push_back(entry->name);
            trace.push_back(entry->type);
            trace.push_back(relocate(entry->raw));
        }
        // Frees: from a pool, and not.
        CreatePool(GameVariable<Mem::PoolPair>(0x006F2578), 0x60, 3, "random");
        void* pooled = GameVariable<Mem::PoolPair>(0x006F2578).Allocate(0x60);
        CreatePool(GameVariable<Mem::PoolPair>(0x006F3644), 0x0C, 3, "pairs");
        CreatePool(GameVariable<Mem::PoolPair>(0x006F3654), 0x10, 3, "vectors");
        void* pooledPair = GameVariable<Mem::PoolPair>(0x006F3644).Allocate(0x0C);
        void* pooledVector = GameVariable<Mem::PoolPair>(0x006F3654).Allocate(0x10);
        void* loose[4];
        for (void*& block : loose)
            block = Mem::Malloc(16);
        if (original) {
            Original<void(__cdecl*)(void*)>(0x00470070)(pooled);
            Original<void(__cdecl*)(void*)>(0x00470070)(loose[0]);
            Original<void(__cdecl*)(void*)>(0x0044C1D0)(loose[1]);
            Original<void(__cdecl*)(void*)>(0x0044C230)(loose[2]);
            Original<void(__cdecl*)(void*)>(0x004700D0)(loose[3]);
            Original<void(__cdecl*)(void*)>(0x0044C1D0)(pooledPair);
            Original<void(__cdecl*)(void*)>(0x0044C230)(pooledVector);
        } else {
            FreeStoredRandom(pooled);
            FreeStoredRandom(loose[0]);
            FreePair(loose[1]);
            FreeVector(loose[2]);
            FreeSymbol(static_cast<S>(loose[3]));
            FreePair(pooledPair);
            FreeVector(pooledVector);
        }
        if (original) {
            Original<void(__cdecl*)()>(0x0046FFF0)();
            trace.push_back(GameVariable<uint8_t>(0x006F2564));
            Original<void(__cdecl*)()>(0x00470000)();
        } else {
            SetScriptFlag6F2564();
            trace.push_back(GameVariable<uint8_t>(0x006F2564));
            ClearScriptFlag6F2564();
        }
        trace.push_back(GameVariable<uint8_t>(0x006F2564));
        for (int i = 0; i < 4; ++i)
            trace.push_back(relocate(reinterpret_cast<uint32_t*>(0x006F2588)[i]));

        std::vector<uint32_t> image(sizeof(g_qb_arenas[0]) / 4);
        std::memcpy(image.data(), arena, sizeof(g_qb_arenas[0]));
        for (uint32_t& word : image)
            word = relocate(word);
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes, s.size);
    std::memcpy(reinterpret_cast<void*>(0x006F2588), pendingSaved.data(), pendingSaved.size());
    RestoreSymbolTable();
    Check(expected == actual, "script symbol bookkeeping");
    if (expected.size() != actual.size())
        Log("  bookkeeping trace sizes: original %u ours %u", static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  bookkeeping entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// --- the .qb parser -------------------------------------------------------------------------------------------

// A token-stream builder for the parser test.
struct QbBuilder {
    std::vector<uint8_t> bytes;
    QbBuilder& u8(uint32_t b)
    {
        bytes.push_back(static_cast<uint8_t>(b));
        return *this;
    }
    QbBuilder& u16(uint32_t v) { return u8(v).u8(v >> 8); }
    QbBuilder& u32(uint32_t v) { return u8(v).u8(v >> 8).u8(v >> 16).u8(v >> 24); }
    QbBuilder& f32(float f)
    {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return u32(bits);
    }
    QbBuilder& eol() { return u8(0x01); }
    QbBuilder& line(uint32_t n) { return u8(0x02).u32(n); }
    QbBuilder& name(uint32_t c) { return u8(0x16).u32(c); }
    QbBuilder& integer(int v) { return u8(0x17).u32(static_cast<uint32_t>(v)); }
    QbBuilder& hex(uint32_t v) { return u8(0x18).u32(v); }
    QbBuilder& real(float f) { return u8(0x1A).f32(f); }
    QbBuilder& text(uint8_t token, const char* s)
    {
        uint32_t length = static_cast<uint32_t>(std::strlen(s)) + 1;
        u8(token).u32(length);
        for (uint32_t i = 0; i < length; ++i)
            u8(static_cast<uint8_t>(s[i]));
        return *this;
    }
    QbBuilder& string(const char* s) { return text(0x1B, s); }
    QbBuilder& local(const char* s) { return text(0x1C, s); }
    QbBuilder& vector(float x, float y, float z) { return u8(0x1E).f32(x).f32(y).f32(z); }
    QbBuilder& pair(float x, float y) { return u8(0x1F).f32(x).f32(y); }
    QbBuilder& assign(uint32_t c) { return name(c).u8(0x07); }
    QbBuilder& param(uint32_t c) { return u8(0x2D).u8(0x16).u32(c); }
    QbBuilder& all() { return u8(0x2C); }
    QbBuilder& range(uint8_t token, float a, float b) { return u8(token).pair(a, b); }
    QbBuilder& script(uint32_t c) { return u8(0x23).u8(0x16).u32(c); }
    // A random block (token 0x2F/0x37/0x40/0x41) over `branches`, each branch a builder of its own.
    QbBuilder& random(uint8_t token, const std::vector<std::pair<int, QbBuilder>>& branches)
    {
        uint32_t n = static_cast<uint32_t>(branches.size());
        u8(token).u32(n);
        for (auto& branch : branches)
            u16(static_cast<uint32_t>(branch.first));
        size_t offsets = bytes.size();
        for (uint32_t i = 0; i < n; ++i)
            u32(0);
        std::vector<size_t> jumps;
        for (uint32_t i = 0; i < n; ++i) {
            size_t at = offsets + 4 * i;
            uint32_t offset = static_cast<uint32_t>(bytes.size() - at - 4);
            std::memcpy(&bytes[at], &offset, 4);
            bytes.insert(bytes.end(), branches[i].second.bytes.begin(), branches[i].second.bytes.end());
            jumps.push_back(bytes.size());
            u8(0x2E).u32(0); // jump past the rest
        }
        for (size_t at : jumps) {
            uint32_t offset = static_cast<uint32_t>(bytes.size() - at - 5);
            std::memcpy(&bytes[at + 1], &offset, 4);
        }
        return *this;
    }
};

uint8_t __cdecl ParserTestFunction(Script::CStruct* params, Script::CScript* script)
{
    int value = 0;
    params->GetInteger(0xF001u, &value, 0);
    return static_cast<uint8_t>(value + (params->ContainsFlag(0xF002u) ? 10 : 0) +
                                (script == reinterpret_cast<Script::CScript*>(0x00ABC000) ? 100 : 0));
}

// Calling the register-argument entry points (the game's address: ours through the stub; or the original).
const uint8_t* CallAddParameterRegisters(void* code, const uint8_t* token, uint32_t name, Script::CStruct* dest,
                                         Script::CStruct* args)
{
    const uint8_t* result;
    __asm {
        mov ecx, token
        mov edx, name
        push args
        push dest
        call code
        add esp, 8
        mov result, eax
    }
    return result;
}
const uint8_t* CallParseRandomRangeRegisters(void* code, const uint8_t* token, Script::CComponent* component,
                                             uint32_t deterministic)
{
    const uint8_t* result;
    __asm {
        push ebx
        mov eax, token
        mov ebx, deterministic
        push component
        call code
        add esp, 4
        pop ebx
        mov result, eax
    }
    return result;
}
const uint8_t* CallSkipValueRegisters(void* code, const uint8_t* token)
{
    const uint8_t* result;
    __asm {
        mov eax, token
        call code
        mov result, eax
    }
    return result;
}
void* CallFindStoredRandomRegisters(void* code, const uint8_t* randomTable, uint32_t randomKind, uint32_t itemCount)
{
    void* result;
    __asm {
        push ebx
        push esi
        push edi
        mov ebx, randomTable
        mov edi, randomKind
        mov esi, itemCount
        call code
        pop edi
        pop esi
        pop ebx
        mov result, eax
    }
    return result;
}

// Everything a parsed value holds, written out (pointers into the arena made relative by the caller).
void DescribeComponent(std::vector<uint32_t>& out, const Script::CComponent* c, int depth);
void DescribeStruct(std::vector<uint32_t>& out, const Script::CStruct* s, int depth)
{
    out.push_back(0x57000000u);
    if (!s || depth > 8)
        return;
    for (const Script::CComponent* c = s->m_head; c; c = c->next)
        DescribeComponent(out, c, depth + 1);
    out.push_back(0x57FFFFFFu);
}
void DescribeArray(std::vector<uint32_t>& out, Script::CArray* a, int depth)
{
    using namespace Script;
    out.push_back(0xA7000000u | a->m_type);
    out.push_back(a->m_size);
    for (uint32_t i = 0; i < a->m_size; ++i) {
        uint32_t e = a->GetElement(i);
        switch (a->m_type) {
        case ESYMBOLTYPE_STRING: case ESYMBOLTYPE_LOCALSTRING:
            for (const char* p = reinterpret_cast<const char*>(e); *p; ++p)
                out.push_back(static_cast<uint8_t>(*p));
            break;
        case ESYMBOLTYPE_PAIR:
            out.push_back(reinterpret_cast<uint32_t*>(e)[1]);
            out.push_back(reinterpret_cast<uint32_t*>(e)[2]);
            break;
        case ESYMBOLTYPE_VECTOR:
            for (int k = 1; k <= 3; ++k)
                out.push_back(reinterpret_cast<uint32_t*>(e)[k]);
            break;
        case ESYMBOLTYPE_STRUCTURE:
            DescribeStruct(out, reinterpret_cast<CStruct*>(e), depth + 1);
            break;
        case ESYMBOLTYPE_ARRAY:
            DescribeArray(out, reinterpret_cast<CArray*>(e), depth + 1);
            break;
        default:
            out.push_back(e);
            break;
        }
    }
}
void DescribeComponent(std::vector<uint32_t>& out, const Script::CComponent* c, int depth)
{
    using namespace Script;
    out.push_back(c->typeAndFlag);
    out.push_back(c->name);
    out.push_back(c->unknown2);
    switch (c->Type()) {
    case ESYMBOLTYPE_STRING: case ESYMBOLTYPE_LOCALSTRING:
        for (const char* p = c->string; *p; ++p)
            out.push_back(static_cast<uint8_t>(*p));
        break;
    case ESYMBOLTYPE_PAIR:
        out.push_back(reinterpret_cast<const uint32_t*>(c->pair)[1]);
        out.push_back(reinterpret_cast<const uint32_t*>(c->pair)[2]);
        break;
    case ESYMBOLTYPE_VECTOR:
        for (int k = 1; k <= 3; ++k)
            out.push_back(reinterpret_cast<const uint32_t*>(c->vector)[k]);
        break;
    case ESYMBOLTYPE_STRUCTURE:
        DescribeStruct(out, c->structure, depth);
        break;
    case ESYMBOLTYPE_ARRAY:
        DescribeArray(out, c->array, depth);
        break;
    case ESYMBOLTYPE_QSCRIPT:
        for (uint32_t i = 0; i < c->unknown2; ++i)
            out.push_back(reinterpret_cast<const uint8_t*>(c->raw)[i]);
        break;
    default:
        out.push_back(c->raw);
        break;
    }
}

// The parser: structures, arrays, expressions (with C function calls and global values), call arguments,
// parameter references, random blocks of every kind, jumps, scripts and the register entry points - through
// the original parser (all its functions original) and ours; the results, positions and arenas compared.
void TestScriptParser()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptParser skipped (the memory manager is already set up)");
        return;
    }
    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {{0x006F2370, 0x200, {}}, {0x006F2568, 16, {}},  {0x006F2348, 16, {}},
                                {0x006F2358, 12, {}},    {0x006F2578, 16, {}},  {0x006F3530, 4, {}},
                                {0x006F3538, 4, {}},     {0x00690FC8, 24, {}},  {0x007CC979, 1, {}},
                                {0x006F3570, 24, {}},    {0x006F2338, 16, {}},  {0x006F3614, 16, {}},
                                {0x006F22B4, 16, {}},    {0x006F3644, 16, {}},  {0x006F3654, 16, {}}};
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    g_saved_symbol_table = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_symbol_table_swapped = true;
    static CSymbolTableEntry* table[0x1000];
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;

    // Names used: 0xE0xx parameters, 0xC0xx globals.
    const uint32_t G_INT = 0xC001, G_FLOAT = 0xC002, G_STRUCT = 0xC003, G_ARRAY = 0xC004, G_ALIAS = 0xC005,
                   G_FUNC = 0xC006, G_MEMBER = 0xC007, G_STRING = 0xC008, G_SCRIPT = 0xC009, G_DANGLING = 0xC00A;
    std::vector<QbBuilder> structures, arrays, expressions, calls;
    auto add = [](std::vector<QbBuilder>& list) -> QbBuilder& {
        list.emplace_back();
        return list.back();
    };
    {
        QbBuilder& b = add(structures);
        b.u8(0x03).eol().line(3).assign(0xE001).integer(-5).eol();
        b.assign(0xE002).real(2.5f).u8(0x09).assign(0xE003).string("text").assign(0xE004).local("loc").eol();
        b.assign(0xE005).vector(1.0f, 2.0f, 3.0f).assign(0xE006).pair(4.0f, 5.0f).assign(0xE007).name(0x77).eol();
        b.name(0xE008).eol();                                     // a flag
        b.assign(0xE009).u8(0x03).assign(0xE00A).integer(1).u8(0x04).eol(); // a nested structure
        b.u8(0x03).assign(0xE00B).integer(2).u8(0x04).eol();      // merged
        b.assign(0xE00C).u8(0x05).integer(1).integer(2).u8(0x06).eol();
        // (no hex integers as parameters: the original never moves past one)
        b.assign(0x9D2D0915).vector(0.0f, 0.001f, 0.0f).eol();    // dropped
        b.assign(0x9D2D0915).vector(0.0f, 0.5f, 0.0f).eol();      // kept
        b.assign(0xE00E).u8(0x0E).integer(2).u8(0x0B).integer(3).u8(0x0D).integer(4).u8(0x0F).eol();
        b.u8(0x0E).integer(7).u8(0x0F).eol();                     // an unnamed expression
        b.u8(0x0E).name(G_STRUCT).u8(0x0B).name(G_STRUCT).u8(0x0F).eol(); // a structure expression: merged
        b.all().eol().assign(0xE00F).all().eol().param(0xE101).eol().assign(0xE010).param(0xE102).eol();
        b.param(0xE103).eol().param(0xE1FF).eol();                // <struct>: merged; missing
        b.assign(0xE011).range(0x30, 1.0f, 6.0f).eol().assign(0xE012).range(0x38, 0.5f, 1.5f).eol();
        b.assign(0xE013).range(0x30, 6.0f, -3.0f).eol();
        QbBuilder one, two, three;
        one.integer(111);
        two.integer(222);
        three.integer(333);
        b.assign(0xE014).random(0x2F, {{1, one}, {3, two}, {2, three}}).eol();
        b.assign(0xE015).random(0x37, {{1, one}, {1, two}}).eol();
        b.assign(0xE01E).random(0x2F, {{3, one}, {-1, two}, {2, three}}).eol(); // a negative weight
        b.assign(0xE01F).range(0x38, 2.0f, 5.0f).eol();
        b.assign(0xE022).vector(0.0f, 0.0f, 0.0f).eol();         // kept: another name
        b.name(0xE023).eol().u8(0x07).eol().integer(5).eol();      // line ends around the '='
        for (int i = 0; i < 4; ++i) {
            b.assign(0xE016 + i).random(0x40, {{1, one}, {1, two}, {1, three}}).eol();
            b.assign(0xE01A + i).random(0x41, {{1, one}, {1, two}, {1, three}}).eol();
        }
        b.assign(0xE020).u8(0x49).u16(7).integer(1).integer(9).eol(); // a short jump over "1"
        b.assign(0xE021).script(0xE0F0).integer(1).u8(0x24).line(4).eol();
        b.script(0xE0F1).eol().integer(2).u8(0x24).eol();         // an unnamed script: named by itself
        b.u8(0x04);
    }
    {
        QbBuilder& b = add(structures); // a call's arguments, then a ')' and an end of line
        b.u8(0x03).name(0xE030).assign(0xE031).integer(4).u8(0x04);
    }
    auto array = [&](std::function<void(QbBuilder&)> body) {
        QbBuilder& b = add(arrays);
        b.u8(0x05);
        body(b);
        b.u8(0x06);
    };
    array([](QbBuilder& b) { b.integer(1).eol().integer(-2).hex(3); });
    array([](QbBuilder& b) { b.integer(1).real(2.5f).integer(3); });
    array([](QbBuilder& b) { b.real(2.5f).integer(1).integer(-7); });
    array([](QbBuilder& b) { b.name(1).line(5).name(2); });
    array([](QbBuilder& b) { b.string("a").eol().string("bc"); });
    array([](QbBuilder& b) { b.local("x").local("yz"); });
    array([](QbBuilder& b) { b.vector(1, 2, 3).vector(4, 5, 6); });
    array([](QbBuilder& b) { b.pair(1, 2).eol().pair(3, 4); });
    array([](QbBuilder& b) {
        b.u8(0x03).assign(0xE040).integer(1).u8(0x04).eol().u8(0x09).u8(0x03).name(0xE041).u8(0x04);
    });
    array([](QbBuilder& b) { b.u8(0x05).integer(1).u8(0x06).line(2).u8(0x05).string("s").u8(0x06); });
    array([](QbBuilder&) {});
    array([](QbBuilder& b) { b.eol().eol(); });
    {
        QbBuilder& b = add(arrays); // (not arrays: other values through ParseValue)
        b.script(0xE0F3).integer(1).u8(0x24).eol();
        add(arrays).all();
        add(arrays).param(0xE103);
    }
    array([](QbBuilder& b) {
        QbBuilder a, c;
        a.integer(5);
        c.integer(6);
        b.random(0x2F, {{1, a}, {1, c}}).integer(7);
    });
    auto expression = [&](std::function<void(QbBuilder&)> body) {
        QbBuilder& b = add(expressions);
        b.u8(0x0E);
        body(b);
        b.u8(0x0F).eol();
    };
    expression([](QbBuilder& b) { b.integer(2).u8(0x0B).integer(3).u8(0x0D).integer(4); });
    expression([](QbBuilder& b) {
        b.u8(0x0E).integer(2).u8(0x0B).integer(3).u8(0x0F).u8(0x0D).integer(4);
    });
    expression([](QbBuilder& b) { b.integer(10).integer(-3); });          // 10 -3: a subtraction
    expression([](QbBuilder& b) { b.real(1.5f).real(-0.5f); });
    expression([](QbBuilder& b) { b.integer(10).integer(3); });           // ends at the 3
    expression([](QbBuilder& b) { b.integer(10).integer(0); });           // ends at the 0
    expression([](QbBuilder& b) { b.name(G_INT).u8(0x0B).name(G_FLOAT); });
    expression([&](QbBuilder& b) { b.name(G_STRUCT).u8(0x08).name(0xE0A0); }); // a member: not a global
    expression([&](QbBuilder& b) { b.name(G_STRUCT).u8(0x08).name(G_INT); });
    expression([&](QbBuilder& b) { b.name(G_ARRAY).u8(0x05).integer(1); });
    expression([&](QbBuilder& b) { b.name(G_ALIAS); });
    expression([&](QbBuilder& b) { b.name(G_STRING).u8(0x0B).string("!"); });
    expression([&](QbBuilder& b) { b.name(G_FUNC).assign(0xF001).integer(3).name(0xF002); });
    expression([&](QbBuilder& b) { b.name(G_FUNC).u8(0x0F).u8(0x0B).integer(1).u8(0x0E); });
    expression([&](QbBuilder& b) { b.name(G_MEMBER).integer(5); });
    expression([&](QbBuilder& b) { b.name(0xBEEF); });                    // an unknown name
    expression([&](QbBuilder& b) { b.param(0xE101).u8(0x0B).integer(1); });
    expression([&](QbBuilder& b) { b.param(0xE103).u8(0x08).name(0xE200); });
    expression([&](QbBuilder& b) { b.param(0xE104).u8(0x0B).integer(1); });
    expression([&](QbBuilder& b) { b.param(0xE105).u8(0x0B).param(0xE105); });
    expression([&](QbBuilder& b) { b.param(0xE103); });
    expression([&](QbBuilder& b) { b.param(0xE1FF).integer(4); });
    expression([&](QbBuilder& b) { b.all().u8(0x08).name(0xE101); });
    expression([&](QbBuilder& b) { b.u8(0x03).assign(1).integer(2).u8(0x04).u8(0x0B).u8(0x03).name(3).u8(0x04); });
    expression([&](QbBuilder& b) { b.vector(1, 2, 3).u8(0x0D).vector(0, 1, 0); });
    expression([&](QbBuilder& b) { b.range(0x30, 1.0f, 3.0f).u8(0x0B).integer(1); });
    expression([&](QbBuilder& b) { b.integer(1).u8(0x32).integer(0).u8(0x33).integer(1); });
    expression([&](QbBuilder& b) { b.integer(4).u8(0x14).integer(3).u8(0x07).integer(1); });
    expression([&](QbBuilder& b) {
        QbBuilder a, c;
        a.integer(5);
        c.integer(6);
        b.random(0x2F, {{1, a}, {1, c}}).u8(0x0B).integer(1);
    });
    auto call = [&](std::function<void(QbBuilder&)> body) {
        QbBuilder& b = add(calls);
        body(b);
    };
    call([](QbBuilder& b) { b.name(0xE050).assign(0xE051).integer(1).string("s").eol(); });
    call([](QbBuilder& b) { b.u8(0x03).name(1).u8(0x04).all().u8(0x09).param(0xE101).u8(0x0F); });
    call([](QbBuilder& b) {
        b.assign(0xE052).u8(0x0E).integer(1).u8(0x0B).integer(1).u8(0x0F).u8(0x0E).name(G_STRUCT).u8(0x0F);
        b.u8(0x0E).integer(0).u8(0x07).integer(1).u8(0x0F).line(1);
    });
    call([](QbBuilder& b) { b.assign(0xE053).param(0xE102).u8(0x00); });

    static const uint32_t kParserFunctions[] = {0x00470C60, 0x00470BB0, 0x0046F7A0, 0x0046FD60, 0x0046FBD0,
                                                0x00479730, 0x004706F0, 0x00470130, 0x00470F30, 0x00471860,
                                                0x00471B00, 0x004712B0, 0x004728C0, 0x004728D0};

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        for (Saved& s : saved) {
            if (s.address != 0x006F2370)
                std::memset(reinterpret_cast<void*>(s.address), 0, s.size);
        }
        GameVariable<bool>(0x007CC979) = true;
        GameVariable<uint32_t>(0x006F3530) = 0x00ABC000; // the current script
        GameVariable<uint32_t>(0x00690FC8) = 0x12345;
        GameVariable<uint32_t>(0x00690FCC) = 0x6789;
        GameVariable<uint32_t>(0x00690FD0) = 0xABC;
        GameVariable<uint32_t>(0x00690FD4) = 0xDEF01;
        GameVariable<uint32_t>(0x00690FD8) = 0x2345;
        GameVariable<uint32_t>(0x00690FDC) = 0x678;
        std::memset(table, 0, sizeof(table));
        uint8_t* arena = g_qb_arenas[original ? 0 : 1];
        std::memset(arena, 0, sizeof(g_qb_arenas[0]));
        std::memset(arena + 0x1000, 0xCD, sizeof(g_qb_arenas[0]) - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + sizeof(g_qb_arenas[0]));
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        manager->m_heaps[0] = heap;
        manager->m_heapCount = 1;
        manager->m_scriptHeap = heap;
        manager->PushContext(heap);
        reinterpret_cast<CExpressionEvaluator*>(0x006F2370)->Init();
        auto base = reinterpret_cast<uint32_t>(arena);
        auto relocate = [&](uint32_t word) {
            return word >= base && word <= base + sizeof(g_qb_arenas[0]) ? 0xA0000000u + (word - base) : word;
        };

        // The globals.
        static CSymbolTableEntry symbols[10];
        CStruct* globalStruct = NewStruct()->Init();
        globalStruct->AddInteger(0xE0A0u, 5);
        globalStruct->AddInteger(G_INT, 6);
        CArray* globalArray = NewArray()->Init();
        globalArray->SetSizeAndType(2, ESYMBOLTYPE_INTEGER);
        globalArray->SetElement(0, 40);
        globalArray->SetElement(1, 41);
        struct {
            uint32_t name;
            ESymbolType type;
            uint32_t raw;
        } globals[] = {{G_INT, ESYMBOLTYPE_INTEGER, 7},
                       {G_FLOAT, ESYMBOLTYPE_FLOAT, 0x40200000},
                       {G_STRUCT, ESYMBOLTYPE_STRUCTURE, reinterpret_cast<uint32_t>(globalStruct)},
                       {G_ARRAY, ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(globalArray)},
                       {G_ALIAS, ESYMBOLTYPE_NAME, G_INT},
                       {G_FUNC, ESYMBOLTYPE_CFUNCTION, reinterpret_cast<uint32_t>(&ParserTestFunction)},
                       {G_MEMBER, ESYMBOLTYPE_MEMBERFUNCTION, 0},
                       {G_STRING, ESYMBOLTYPE_STRING, reinterpret_cast<uint32_t>("glob")},
                       {G_SCRIPT, ESYMBOLTYPE_QSCRIPT, 0},
                       {G_DANGLING, ESYMBOLTYPE_NAME, 0xDEAD}}; // (names a missing symbol)
        for (int i = 0; i < 10; ++i) {
            symbols[i] = {};
            symbols[i].name = globals[i].name;
            symbols[i].type = globals[i].type;
            symbols[i].raw = globals[i].raw;
            table[globals[i].name & 0xFFF] = &symbols[i];
        }
        // The caller's parameters.
        CStruct* args = NewStruct()->Init();
        args->AddInteger(0xE101u, 11);
        args->AddString(0xE102u, "arg");
        CStruct* argStruct = NewStruct()->Init();
        argStruct->AddInteger(0xE200u, 12);
        args->AddStructurePointer(0xE103u, argStruct);
        args->AddChecksum(0xE104u, G_INT); // a name standing for a global
        args->AddChecksum(0xE105u, G_DANGLING);

        std::vector<OriginalCode*> guards;
        if (original) {
            for (uint32_t address : kParserFunctions)
                guards.push_back(new OriginalCode(address));
        }
        using P = const uint8_t*;
        auto position = [&](const QbBuilder& b, P p) { trace.push_back(static_cast<uint32_t>(p - b.bytes.data())); };
        // (four passes: the random blocks' stored states are used again; after the second, they're given up
        // by their script and taken by another)
        for (int pass = 0; pass < 4; ++pass) {
            CStruct* passArgs = pass % 2 == 0 ? args : nullptr;
            if (pass == 2) {
                ForgetStoredRandomsOf(0x00ABC000);
                GameVariable<uint32_t>(0x006F3530) = 0x00ABD000;
            }
            for (const QbBuilder& b : structures) {
                CStruct* dest = NewStruct()->Init();
                P end = original ? Original<P(__cdecl*)(CStruct*, P, CStruct*)>(0x00471860)(dest, b.bytes.data(), passArgs)
                                 : ParseStructure(dest, b.bytes.data(), passArgs);
                position(b, end);
                DescribeStruct(trace, dest, 0);
            }
            for (const QbBuilder& b : arrays) {
                CComponent value = {};
                P end = original ? Original<P(__cdecl*)(P, CStruct*, CComponent*)>(0x004706F0)(b.bytes.data(), passArgs, &value)
                                 : ParseValue(b.bytes.data(), passArgs, &value);
                position(b, end);
                DescribeComponent(trace, &value, 0);
            }
            for (const QbBuilder& b : expressions) {
                CComponent value = {};
                P end = original ? Original<P(__cdecl*)(P, CStruct*, CComponent*)>(0x004712B0)(b.bytes.data(), passArgs, &value)
                                 : ParseExpression(b.bytes.data(), passArgs, &value);
                position(b, end);
                DescribeComponent(trace, &value, 0);
                trace.push_back(reinterpret_cast<uint32_t>(reinterpret_cast<CExpressionEvaluator*>(0x006F2370)->m_error));
            }
            for (const QbBuilder& b : calls) {
                CStruct* dest = NewStruct()->Init();
                P end = original ? Original<P(__cdecl*)(CStruct*, P, CStruct*)>(0x00470F30)(dest, b.bytes.data(), passArgs)
                                 : ParseArguments(dest, b.bytes.data(), passArgs);
                position(b, end);
                DescribeStruct(trace, dest, 0);
            }
        }
        // The register entry points: our stubs or the originals.
        {
            QbBuilder b;
            b.integer(9).range(0x30, 2.0f, 1000.0f).u8(0x03).name(1).u8(0x04).name(0xE300).param(0xE101).all();
            void* addParameter = original ? OriginalFunction(0x00470130) : reinterpret_cast<void*>(&AddParameterStub);
            void* randomRange = original ? OriginalFunction(0x0046FBD0) : reinterpret_cast<void*>(&ParseRandomRangeStub);
            void* skipValue = original ? OriginalFunction(0x0046FD60) : reinterpret_cast<void*>(&SkipValueStub);
            void* findStored = original ? OriginalFunction(0x0046F7A0) : reinterpret_cast<void*>(&FindStoredRandomStub);
            CStruct* dest = NewStruct()->Init();
            P p = b.bytes.data();
            for (uint32_t name : {0xE301u, 0xE302u, 0xE303u, 0u, 0xE304u, 0xE305u}) {
                p = CallAddParameterRegisters(addParameter, p, name, dest, args);
                position(b, p);
            }
            DescribeStruct(trace, dest, 0);
            for (uint32_t deterministic : {0u, 1u, 0x100u}) {
                CComponent value = {};
                position(b, CallParseRandomRangeRegisters(randomRange, b.bytes.data() + 6, &value, deterministic));
                DescribeComponent(trace, &value, 0);
            }
            for (size_t at : {size_t(0), size_t(5), size_t(15)})
                position(b, CallSkipValueRegisters(skipValue, b.bytes.data() + at));
            // (the stored randoms the structure test made)
            for (CStoredRandom* stored = GameVariable<CStoredRandom*>(0x006F2358); stored; stored = stored->m_next) {
                trace.push_back(relocate(reinterpret_cast<uint32_t>(
                    CallFindStoredRandomRegisters(findStored, stored->m_table, stored->m_type, stored->m_count))));
                trace.push_back(relocate(reinterpret_cast<uint32_t>(
                    CallFindStoredRandomRegisters(findStored, stored->m_table, 0x99, stored->m_count))));
            }
        }
        for (OriginalCode* guard : guards)
            delete guard;
        for (int i = 0; i < 3; ++i)
            trace.push_back(relocate(reinterpret_cast<uint32_t*>(0x006F2358)[i]));

        for (uint32_t& word : trace)
            word = relocate(word);
        std::vector<uint32_t> image(sizeof(g_qb_arenas[0]) / 4);
        std::memcpy(image.data(), arena, sizeof(g_qb_arenas[0]));
        for (uint32_t& word : image)
            word = relocate(word);
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    RestoreSymbolTable();
    Check(expected == actual, "script parser: structures, arrays, expressions, arguments, random blocks");
    if (expected.size() != actual.size())
        Log("  parser trace sizes: original %u ours %u", static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  parser entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

alignas(16) uint8_t g_load_arenas[2][0x80000];

const uint8_t* CallParseSymbolDefinitionRegisters(void* code, const uint8_t* token, uint32_t symbolName,
                                                  const char* fileName)
{
    const uint8_t* result;
    __asm {
        mov ecx, token
        push fileName
        push symbolName
        call code
        add esp, 8
        mov result, eax
    }
    return result;
}
void* CallCreateScriptSymbolRegisters(void* code, uint32_t scriptSize, const uint8_t* scriptCode, uint32_t symbolName,
                                      uint32_t checksum, const char* fileName)
{
    void* result;
    __asm {
        push ebx
        mov eax, scriptSize
        mov ebx, scriptCode
        push fileName
        push checksum
        push symbolName
        call code
        add esp, 12
        pop ebx
        mov result, eax
    }
    return result;
}

// Loading .qb files: defining every kind of symbol and compressed scripts, the checksum-name pre-scan (both
// tables), debug sections (with checksum names off and on), redefinitions (plain, temporary + pending, an
// unchanged script), restarting changed scripts, and the two register entry points - original vs ours.
uint8_t __cdecl LoadingFileProbe(Script::CStruct*, Script::CScript*)
{
    return static_cast<uint8_t>(GameVariable<uint32_t>(0x006F2364) * 7 + 1);
}

void TestScriptLoading()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptLoading skipped (the memory manager is already set up)");
        return;
    }
    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {{0x006F2370, 0x200, {}}, {0x006F2568, 16, {}}, {0x006F2348, 16, {}},
                                {0x006F2358, 16, {}},    {0x006F2578, 16, {}}, {0x006F3530, 4, {}},
                                {0x006F3538, 4, {}},     {0x00690FC8, 24, {}}, {0x007CC979, 1, {}},
                                {0x006F3570, 24, {}},    {0x006F2338, 16, {}}, {0x006F3614, 16, {}},
                                {0x006F22B4, 16, {}},    {0x006F3644, 16, {}}, {0x006F3654, 16, {}},
                                {0x006F362C, 16, {}},    {0x006F3528, 4, {}},  {0x006F2588, 0x80, {}},
                                {0x006F3558, 4, {}},     {0x006F232C, 12, {}}, {0x007CEFAC, 1, {}},
                                {0x006F363C, 8, {}}};
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    g_saved_symbol_table = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_symbol_table_swapped = true;
    static CSymbolTableEntry* table[0x1000];
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;
    std::vector<uint8_t> nameTable(0x500000);
    std::vector<char> nameBlock(0x4000);

    // The .qb files.
    auto scriptBody = [](QbBuilder& b, int variant, bool compressible) {
        b.eol().name(0xF100).assign(0xF101).integer(variant).eol();
        for (int i = 0; i < (compressible ? 30 : 1); ++i)
            b.name(0xF102).assign(0xF103).integer(1).eol();
        b.u8(0x24).line(9);
    };
    auto file = [&](int variant) {
        QbBuilder b;
        // (checksum names: the checksum, then the text)
        b.u8(0x2B).u32(0xD001);
        for (const char* c = "first_name"; *c; ++c)
            b.u8(static_cast<uint8_t>(*c));
        b.u8(0);
        b.u8(0x2B).u32(0xD002);
        for (const char* c = "second"; *c; ++c)
            b.u8(static_cast<uint8_t>(*c));
        b.u8(0);
        b.eol().line(1);
        b.name(0xD010).eol().u8(0x07).integer(variant).eol();
        b.name(0xD011).u8(0x07).real(1.5f).line(2);
        b.name(0xD012).u8(0x07).string("text").eol();
        b.name(0xD013).u8(0x07).local("loc").eol();
        b.name(0xD014).u8(0x07).vector(1, 2, 3).eol();
        b.name(0xD015).u8(0x07).pair(4, 5).eol();
        b.name(0xD016).u8(0x07).name(0xD010).eol();
        b.name(0xD017).u8(0x07).u8(0x03).assign(0xE001).integer(variant).name(0xE002).u8(0x04).eol();
        b.name(0xD018).u8(0x07).u8(0x05).integer(1).integer(variant).u8(0x06).eol();
        b.name(0xD019).u8(0x07).u8(0x0E).integer(2).u8(0x0B).integer(variant).u8(0x0F).eol();
        b.script(0xD020);
        scriptBody(b, 1, true);   // the same each time
        b.script(0xD021);
        scriptBody(b, variant, true);
        b.script(0xD022);
        scriptBody(b, variant, false); // too small to compress
        b.u8(0x45).u8(0x16).u32(0x70CB0238).eol(); // a debug section
        b.name(0xD030).u8(0x07).integer(30).eol();
        b.script(0xD031);
        scriptBody(b, variant, false);
        b.u8(0x45).u8(0x16).u32(0x0FA33D9B).eol(); // its end
        b.name(0xD032).u8(0x07).string("after").eol();
        b.name(0xD050 + static_cast<uint32_t>(variant)).u8(0x07).integer(variant).eol(); // new in each file
        b.script(0xD060 + static_cast<uint32_t>(variant));
        scriptBody(b, variant, false);
        b.name(0xD040).u8(0x07).u8(0x0E).name(0xD0F0).u8(0x0F).eol(); // calls a C function while loading
        b.u8(0x2B).u32(0xD003);
        b.u8('x').u8(0);
        b.eol().u8(0x00);
        return b;
    };
    QbBuilder files[3] = {file(1), file(2), file(3)};

    static const uint32_t kLoaderFunctions[] = {0x00472420, 0x00472240, 0x0046FE40, 0x0046ED20,
                                                0x0046ED90, 0x00472C50};
    alignas(4) static uint8_t scripts[3][0x100];
    static uint32_t frames[2][0x30 / 4];

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        for (Saved& s : saved) {
            if (s.address != 0x006F2370)
                std::memset(reinterpret_cast<void*>(s.address), 0, s.size);
        }
        GameVariable<bool>(0x007CC979) = true;
        GameVariable<void*>(0x006F3558) = StandInScriptCache();
        std::memset(table, 0, sizeof(table));
        std::memset(nameTable.data(), 0, nameTable.size());
        std::memset(nameBlock.data(), 0, nameBlock.size());
        GameVariable<void*>(0x006F232C) = nameTable.data();
        GameVariable<char*>(0x006F2330) = nameBlock.data();
        GameVariable<char*>(0x006F2334) = nameBlock.data();
        uint8_t* arena = g_load_arenas[original ? 0 : 1];
        std::memset(arena, 0, sizeof(g_load_arenas[0]));
        std::memset(arena + 0x1000, 0xCD, sizeof(g_load_arenas[0]) - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + 0x60000);
        auto* heap = new (arena + 0x620) Heap(region, 1, "script");
        auto* debugRegion = new (arena + 0x6C0) Region(arena + 0x60000, arena + sizeof(g_load_arenas[0]));
        auto* debugHeap = new (arena + 0x6E0) Heap(debugRegion, 1, "debug");
        manager->m_heaps[0] = heap;
        manager->m_heaps[1] = debugHeap;
        manager->m_heapCount = 2;
        manager->m_scriptHeap = heap;
        manager->m_debugHeap = debugHeap;
        manager->m_topDownHeap = heap;
        manager->PushContext(heap);
        reinterpret_cast<CExpressionEvaluator*>(0x006F2370)->Init();
        auto base = reinterpret_cast<uint32_t>(arena);
        auto relocate = [&](uint32_t word) {
            return word >= base && word <= base + sizeof(g_load_arenas[0]) ? 0xA0000000u + (word - base) : word;
        };
        auto dump = [&] {
            for (CSymbolTableEntry* s = GetNextSymbol(nullptr); s; s = GetNextSymbol(s)) {
                trace.push_back(s->name);
                trace.push_back(s->unknown0);
                trace.push_back(s->type);
                trace.push_back(s->unknown8);
                CComponent c = {};
                c.typeAndFlag = static_cast<uint8_t>(s->type << 1);
                c.raw = s->raw;
                if (s->type == ESYMBOLTYPE_QSCRIPT) {
                    auto* block = reinterpret_cast<const uint32_t*>(s->raw);
                    trace.insert(trace.end(), block, block + 3);
                    for (uint32_t i = 0; i < block[2]; ++i)
                        trace.push_back(reinterpret_cast<const uint8_t*>(block + 3)[i]);
                } else {
                    DescribeComponent(trace, &c, 0);
                }
            }
            trace.push_back(GameVariable<uint32_t>(0x006F3528));
            trace.push_back(GameVariable<uint32_t>(0x006F2364));
            for (uint32_t checksum : {0xD001u, 0xD002u, 0xD003u, Crc::GenerateCRCFromString(R"(scripts\second.qb)")}) {
                const char* name = FindScriptChecksumName(checksum);
                trace.push_back(name ? static_cast<uint8_t>(name[0]) : 0xFFFFFFFFu);
                name = FindChecksumName(checksum);
                trace.push_back(name ? static_cast<uint8_t>(name[0]) : 0xFFFFFFFFu);
            }
        };

        // A C function scripts can call while loading: it sees which file is loading.
        static CSymbolTableEntry probe;
        probe = {};
        probe.name = 0xD0F0;
        probe.type = ESYMBOLTYPE_CFUNCTION;
        probe.raw = reinterpret_cast<uint32_t>(&LoadingFileProbe);
        table[0xD0F0 & 0xFFF] = &probe;

        std::vector<OriginalCode*> guards;
        if (original) {
            for (uint32_t address : kLoaderFunctions)
                guards.push_back(new OriginalCode(address));
        }
        using ParseQBFn = void(__cdecl*)(const char*, const uint8_t*, int, int, int);
        auto parse = [&](int which, int useQbNames, int temporary) {
            const char* name = which == 0 ? "scripts\\first.qb" : which == 1 ? "scripts\\second.qb" : "third.qb";
            if (original)
                Original<ParseQBFn>(0x00472420)(name, files[which].bytes.data(), 0, useQbNames, temporary);
            else
                ParseQB(name, files[which].bytes.data(), 0, useQbNames, temporary);
            dump();
        };
        parse(0, 0, 0);
        parse(0, 0, 0);     // again: replaced, the unchanged script kept
        parse(1, 1, 1);     // temporary
        ProcessPendingSymbols();
        dump();
        // The debug section kept, names recorded (once: adding a known name uses the game's C runtime).
        GameVariable<bool>(0x007CEFAC) = true;
        parse(1, 0, 0);
        GameVariable<bool>(0x007CEFAC) = false;
        parse(2, 1, 0);
        if (original)
            Original<ParseQBFn>(0x0046ED90)("again.qb", files[0].bytes.data(), 0, 0, 0);
        else
            LoadQBData("again.qb", files[0].bytes.data(), 0, 0, 0);
        dump();

        // Restarting: scripts running other scripts only (none restarted), and which scripts refer to what.
        std::memset(scripts, 0, sizeof(scripts));
        std::memset(frames, 0, sizeof(frames));
        for (int i = 0; i < 3; ++i) {
            *reinterpret_cast<uint8_t**>(scripts[i]) = i < 2 ? scripts[i + 1] : nullptr;
            *reinterpret_cast<uint32_t*>(scripts[i] + 0xC8) = 0xAAA0u + static_cast<uint32_t>(i);
            *reinterpret_cast<uint32_t*>(scripts[i] + 0x30) = i;
            *reinterpret_cast<uint32_t**>(scripts[i] + 0x94) = frames[0];
        }
        frames[0][0] = 0xBBB0;
        frames[1][0] = 0xBBB1;
        GameVariable<uint8_t*>(0x006F3538) = scripts[0];
        for (int i = 0; i < 3; ++i) {
            for (uint32_t name : {0xAAA0u, 0xAAA2u, 0xBBB0u, 0xBBB1u, 0xCCCCu}) {
                bool refers = original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x00472C50)(scripts[i], name)
                                       : ScriptRefersTo(reinterpret_cast<CScript*>(scripts[i]), name);
                trace.push_back(refers ? 1u : 0u);
            }
        }
        if (original)
            Original<void(__cdecl*)()>(0x0046ED20)();
        else
            RestartChangedScripts();
        GameVariable<uint8_t*>(0x006F3538) = nullptr;
        dump();

        // The register entry points.
        {
            QbBuilder b;
            b.integer(77).u8(0x03).assign(1).integer(2).u8(0x04).u8(0x05).real(1.0f).u8(0x06).u8(0x0E).integer(3)
                .u8(0x0F).eol();
            void* define = original ? OriginalFunction(0x00472240) : reinterpret_cast<void*>(&ParseSymbolDefinitionStub);
            void* create = original ? OriginalFunction(0x0046FE40) : reinterpret_cast<void*>(&CreateScriptSymbolStub);
            const uint8_t* p = b.bytes.data();
            for (uint32_t name : {0xD101u, 0xD102u, 0xD103u, 0xD104u}) {
                p = CallParseSymbolDefinitionRegisters(define, p, name, "regs.qb");
                trace.push_back(static_cast<uint32_t>(p - b.bytes.data()));
            }
            for (uint32_t size : {4u, 200u}) {
                std::vector<uint8_t> code(size, 0x01);
                trace.push_back(relocate(reinterpret_cast<uint32_t>(
                    CallCreateScriptSymbolRegisters(create, size, code.data(), 0xD110u + size, 0x1234u, "regs.qb"))));
            }
            dump();
        }
        for (OriginalCode* guard : guards)
            delete guard;

        for (uint32_t& word : trace)
            word = relocate(word);
        std::vector<uint32_t> image(sizeof(g_load_arenas[0]) / 4);
        std::memcpy(image.data(), arena, sizeof(g_load_arenas[0]));
        for (uint32_t& word : image) {
            word = relocate(word);
            // (uninitialised 16-bit fields - a symbol's unknown2 - can hold the top half of a stale pointer
            // into this run's arena)
            uint32_t high = word >> 16;
            if (high >= (base >> 16) && high <= ((base + sizeof(g_load_arenas[0])) >> 16))
                word = (word & 0xFFFF) | ((0xA000u + high - (base >> 16)) << 16);
        }
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    RestoreSymbolTable();
    Check(expected == actual, "script loading: .qb files, redefinitions, restarts");
    if (expected.size() != actual.size())
        Log("  loading trace sizes: original %u ours %u", static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  loading entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// A stand-in for an object owned by the memory-card holder: its virtual destructor records the call.
struct McOwnedStandIn {
    void** vtable;
    int destroyed;
    void* __thiscall Destroy(unsigned int flags)
    {
        destroyed = 100 + static_cast<int>(flags);
        return this;
    }
};

// The PC memory cards (files under Save\ in a scratch folder - never the game's): opening in every mode, read,
// write, seek, flush, close, delete, names, free space, the manager's references; the list node and owner
// destructors, the pool lookup and the folded stubs - original vs ours. The directory listing's original
// uses the game's C runtime (sprintf), so ours is checked against what Windows reports.
void TestMemoryCards()
{
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestMemoryCards skipped (the memory manager is already set up)");
        return;
    }
    char oldDirectory[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, oldDirectory);
    char folder[MAX_PATH];
    GetTempPathA(MAX_PATH, folder);
    std::snprintf(folder + std::strlen(folder), 64, "thug2-mc-test-%lu", GetCurrentProcessId());
    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {{0x007D1810, 0x100, {}}, {0x007D1890, 8, {}}, {0x007CC979, 1, {}},
                                {0x007D1808, 8, {}}};
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    auto removeFolder = [&] {
        for (const char* name : {"Save\\a.sav", "Save\\b.sav", "Save\\new.sav", "Save\\always.sav", "Save\\x.tmp"}) {
            std::string path = std::string(folder) + "\\" + name;
            DeleteFileA(path.c_str());
        }
        RemoveDirectoryA((std::string(folder) + "\\Save\\dir.sav").c_str());
        RemoveDirectoryA((std::string(folder) + "\\Save").c_str());
        RemoveDirectoryA(folder);
    };
    // The same files, with the same dates, for each run.
    auto makeFolder = [&] {
        removeFolder();
        CreateDirectoryA(folder, nullptr);
        SetCurrentDirectoryA(folder);
        CreateDirectoryA("Save", nullptr);
        CreateDirectoryA("Save\\dir.sav", nullptr);
        SYSTEMTIME time = {2004, 9, 2, 14, 13, 45, 30, 0};
        FILETIME fileTime;
        SystemTimeToFileTime(&time, &fileTime);
        for (const char* name : {"Save\\a.sav", "Save\\b.sav", "Save\\x.tmp"}) {
            HANDLE h = CreateFileA(name, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD written;
            WriteFile(h, name, static_cast<DWORD>(std::strlen(name)) * (name[5] == 'b' ? 3 : 1), &written, nullptr);
            SetFileTime(h, nullptr, nullptr, &fileTime);
            CloseHandle(h);
            fileTime.dwLowDateTime += 0x10000000; // different dates
        }
    };

    static uint8_t arenas[2][0x10000];
    std::vector<uint32_t> blocks[2];
    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        makeFolder();
        for (Saved& s : saved)
            std::memset(reinterpret_cast<void*>(s.address), 0, s.size);
        GameVariable<bool>(0x007CC979) = true;
        uint8_t* arena = arenas[original ? 0 : 1];
        std::memset(arena, 0, sizeof(arenas[0]));
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + sizeof(arenas[0]));
        auto* heap = new (arena + 0x620) Heap(region, 1, "test");
        manager->m_heaps[0] = heap;
        manager->m_heapCount = 1;
        manager->PushContext(heap);
        auto base = reinterpret_cast<uint32_t>(arena);
        auto relocate = [&](uint32_t word) {
            return word >= base && word <= base + sizeof(arenas[0]) ? 0xA0000000u + (word - base) : word;
        };
        using M = Mc::Manager*;
        using C = Mc::Card*;
        using F = Mc::File*;

        // The manager.
        auto create = [&](bool make) {
            return original ? Original<M(__cdecl*)(bool)>(0x005BC900)(make) : Mc::CreateManager(make);
        };
        auto release = [&] {
            if (original)
                Original<void(__cdecl*)()>(0x005BC950)();
            else
                Mc::ReleaseManager();
            trace.push_back(relocate(GameVariable<uint32_t>(0x007D1890)));
            trace.push_back(GameVariable<uint32_t>(0x007D1894));
        };
        trace.push_back(relocate(reinterpret_cast<uint32_t>(create(false))));
        trace.push_back(GameVariable<uint32_t>(0x007D1894));
        release();
        M mc = create(true);
        trace.push_back(relocate(reinterpret_cast<uint32_t>(mc)));
        trace.push_back(relocate(reinterpret_cast<uint32_t>(create(true))));
        trace.push_back(GameVariable<uint32_t>(0x007D1894));
        C card = original ? Original<C(__thiscall*)(M, int, int)>(0x005BC730)(mc, 3, 1) : mc->GetCard(3, 1);
        trace.push_back(relocate(reinterpret_cast<uint32_t>(card)));
        trace.push_back(card->m_present);

        // Files.
        std::vector<F> files;
        auto open = [&](const char* name, int mode) {
            F file = original ? Original<F(__thiscall*)(C, const char*, int, int)>(0x005BC9B0)(card, name, mode, 7)
                              : card->Open(name, mode, 7);
            trace.push_back(file ? 1u : 0u);
            if (file)
                files.push_back(file);
            return file;
        };
        for (int mode = 0; mode <= 7; ++mode) {
            if (F file = open("Save/a.sav", mode)) {
                uint8_t buffer[16] = {};
                int count = original ? Original<int(__thiscall*)(F, void*, int)>(0x005BC880)(file, buffer, 16)
                                     : file->Read(buffer, 16);
                trace.push_back(static_cast<uint32_t>(count));
                trace.push_back(buffer[0] | buffer[5] << 8);
                count = original ? Original<int(__thiscall*)(F, const void*, int)>(0x005BC850)(file, "Z", 1)
                                 : file->Write("Z", 1); // (fails on read-only files)
                trace.push_back(static_cast<uint32_t>(count));
                if (original)
                    Original<bool(__thiscall*)(F)>(0x005BC8B0)(file);
                else
                    file->Close();
            }
        }
        // (the path as converted: '/' to '\\')
        trace.insert(trace.end(), reinterpret_cast<uint32_t*>(0x007D1810), reinterpret_cast<uint32_t*>(0x007D1820));
        open("Save\\missing.sav", 1);
        trace.insert(trace.end(), reinterpret_cast<uint32_t*>(0x007D1810), reinterpret_cast<uint32_t*>(0x007D1830));
        // Opened twice at once: no sharing.
        if (F first = open("Save\\b.sav", 1)) {
            if (F second = open("Save\\b.sav", 1)) {
                if (original)
                    Original<bool(__thiscall*)(F)>(0x005BC8B0)(second);
                else
                    second->Close();
            }
            if (original)
                Original<bool(__thiscall*)(F)>(0x005BC8B0)(first);
            else
                first->Close();
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (F file = open("Save\\new.sav", 4)) { // a new file (the second time: it exists)
                int count = original ? Original<int(__thiscall*)(F, const void*, int)>(0x005BC850)(file, "hello world", 11)
                                     : file->Write("hello world", 11);
                trace.push_back(static_cast<uint32_t>(count));
                for (int origin : {0, 1, 2, 5}) {
                    DWORD at = original ? Original<DWORD(__thiscall*)(F, LONG, int)>(0x005BC800)(file, -3 + origin, origin)
                                        : file->Seek(-3 + origin, origin);
                    trace.push_back(at);
                }
                trace.push_back(original ? Original<bool(__thiscall*)(F)>(0x005BC840)(file) : file->Flush());
                trace.push_back(original ? Original<bool(__thiscall*)(F)>(0x005BC8B0)(file) : file->Close());
            }
        }
        if (F file = open("Save/always.sav", 6)) {
            trace.push_back(original ? Original<int(__thiscall*)(F, void*, int)>(0x005BC880)(file, nullptr, 0)
                                     : file->Read(nullptr, 0)); // (write-only: fails)
            trace.push_back(original ? Original<bool(__thiscall*)(F)>(0x005BC8B0)(file) : file->Close());
            trace.push_back(original ? Original<bool(__thiscall*)(F)>(0x005BC8B0)(file) : file->Close()); // again
        }
        for (const char* name : {"Save\\x.tmp", "Save\\x.tmp", "nothing"})
            trace.push_back(original ? Original<bool(__thiscall*)(C, const char*)>(0x005BC770)(card, name)
                                     : card->RemoveFile(name));
        const char* converted = original ? Original<const char*(__thiscall*)(C, const char*)>(0x005BC740)(card, "Card/File")
                                         : card->ConvertFilename("Card/File");
        trace.push_back(reinterpret_cast<uint32_t>(converted));
        trace.push_back(static_cast<uint32_t>(std::strcmp(converted, "Card/File")));
        for (uint8_t present : {uint8_t(0), uint8_t(0x75)}) {
            card->m_present = present;
            int free = original ? Original<int(__thiscall*)(C)>(0x005BC790)(card) : card->GetFreeBlocks();
            blocks[original ? 0 : 1].push_back(static_cast<uint32_t>(free));
            trace.push_back(free != 0);
        }
        trace.push_back(original ? Original<bool(__thiscall*)(C, int)>(0x005BC7F0)(card, 3) : card->AlwaysTrue(3));
        // Handles differ between the runs: cleared before comparing.
        for (F file : files)
            file->m_handle = nullptr;

        release();
        release();
        trace.push_back(GameVariable<uint32_t>(0x007D1890));
        trace.push_back(GameVariable<uint32_t>(0x007D1894));

        // List nodes of other types, the owner's destructor, the region's, the pool lookup.
        static Mem::ListNode nodes[3];
        for (void** vtable : {reinterpret_cast<void**>(0x0065797C), reinterpret_cast<void**>(0x00658A70)}) {
            for (int i = 0; i < 3; ++i)
                nodes[i] = {nullptr, &nodes[i], i, &nodes[(i + 1) % 3], &nodes[(i + 2) % 3]};
            uint32_t address = vtable == reinterpret_cast<void**>(0x0065797C) ? 0x005BC990 : 0x005BCD30;
            if (original)
                Original<void(__thiscall*)(Mem::ListNode*)>(address)(&nodes[1]);
            else
                nodes[1].UnlinkAs(vtable);
            for (Mem::ListNode& n : nodes) {
                trace.push_back(reinterpret_cast<uint32_t>(n.vtable));
                trace.push_back(static_cast<uint32_t>(n.next - nodes));
                trace.push_back(static_cast<uint32_t>(n.prev - nodes));
            }
        }
        static McOwnedStandIn owned;
        static void* ownedVtable[1];
        auto destroy = &McOwnedStandIn::Destroy;
        std::memcpy(&ownedVtable[0], &destroy, sizeof(void*));
        owned = {ownedVtable, 0};
        for (unsigned int flags : {0u, 1u}) {
            auto* holder = static_cast<void**>(Mem::Calloc(8));
            holder[1] = flags ? &owned : nullptr;
            void* result = original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x005BCD00)(holder, flags)
                                    : reinterpret_cast<void*>(reinterpret_cast<Sio::Device*>(holder)->Destroy(flags));
            trace.push_back(relocate(reinterpret_cast<uint32_t>(result)));
            trace.push_back(static_cast<uint32_t>(owned.destroyed));
        }
        static uint8_t regionBytes[0x18];
        std::memset(regionBytes, 0xEE, sizeof(regionBytes));
        if (original)
            Original<void(__thiscall*)(void*)>(0x005BC4D0)(regionBytes);
        else
            reinterpret_cast<Region*>(regionBytes)->DestroyBody();
        trace.insert(trace.end(), reinterpret_cast<uint32_t*>(regionBytes), reinterpret_cast<uint32_t*>(regionBytes + 0x18));
        GameVariable<uint32_t>(0x007D1808) = 0x11112222;
        GameVariable<uint32_t>(0x007D180C) = 0x33334444;
        for (int i = 0; i < 2; ++i) // (only 2 pools: past them is the path buffer)
            trace.push_back(reinterpret_cast<uint32_t>(original ? Original<Mem::Pool*(__cdecl*)(int)>(0x005BC3B0)(i)
                                                                : Mem::PoolManager::GetPool(i)));
        // The folded stubs (source only: the originals are what runs in the game).
        trace.push_back(original ? Original<bool(__cdecl*)()>(0x005B9760)() : Stubs::ReturnTrue());
        trace.push_back(original ? Original<bool(__cdecl*)()>(0x005B6570)() : Stubs::ReturnTrue());
        trace.push_back(original ? Original<bool(__cdecl*)()>(0x005B9E50)() : Stubs::ReturnFalse());
        trace.push_back(static_cast<uint32_t>(original ? Original<int(__cdecl*)()>(0x005BA1D0)() : Stubs::ReturnZero()));
        if (original)
            Original<void(__stdcall*)(int)>(0x005BAB90)(5);
        else
            Stubs::IgnoreArgument(5);

        for (uint32_t& word : trace)
            word = relocate(word);
        std::vector<uint32_t> image(sizeof(arenas[0]) / 4);
        std::memcpy(image.data(), arena, sizeof(arenas[0]));
        for (uint32_t& word : image)
            word = relocate(word);
        trace.insert(trace.end(), image.begin(), image.end());
        // (after the arena is taken: it allocates in it)
        // The directory listing (ours only), against Windows.
        if (!original) {
            Mc::File list = {};
            list.m_node.next = list.m_node.prev = &list.m_node;
            bool ok = card->GetFileList("*.sav", &list);
            std::vector<std::string> got, want;
            for (Mem::ListNode* n = list.m_node.next; n != &list.m_node; n = n->next) {
                auto* f = reinterpret_cast<F>(n);
                char line[128];
                std::snprintf(line, sizeof(line), "%s %u %u-%u-%u %u:%u:%u %p", f->m_name, f->m_size, f->m_year,
                              f->m_month, f->m_day, f->m_hour, f->m_minute, f->m_second, f->m_handle);
                got.push_back(line);
            }
            WIN32_FIND_DATAA found;
            HANDLE search = FindFirstFileA("Save\\*.sav", &found);
            do {
                if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                FILETIME local;
                SYSTEMTIME t;
                FileTimeToLocalFileTime(&found.ftLastWriteTime, &local);
                FileTimeToSystemTime(&local, &t);
                char line[128];
                std::snprintf(line, sizeof(line), "%s %u %u-%u-%u %u:%u:%u %p", found.cFileName, found.nFileSizeLow,
                              t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, nullptr);
                want.push_back(line);
            } while (FindNextFileA(search, &found));
            FindClose(search);
            Check(ok && got == want && got.size() >= 3, "memory cards: directory listing (ours vs Windows)");
        }
        GameVariable<void*>(0x007D1800) = nullptr;
        SetCurrentDirectoryA(oldDirectory);
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    SetCurrentDirectoryA(oldDirectory);
    removeFolder();
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    Check(expected == actual, "memory cards: files, manager, nodes, stubs");
    // (free space: the same disk moments apart - within 1024 blocks)
    Check(blocks[0].size() == 2 && blocks[1].size() == 2 && blocks[0][0] == 0 && blocks[1][0] == 0 &&
              (blocks[0][1] > blocks[1][1] ? blocks[0][1] - blocks[1][1] : blocks[1][1] - blocks[0][1]) < 1024,
          "memory cards: free space");
    if (expected.size() != actual.size())
        Log("  memory card trace sizes: original %u ours %u", static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  memory card entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// The registry settings are tested under a stand-in HKEY_CURRENT_USER (a scratch key), never the real one.
struct RegistryStandIn {
    HKEY root = nullptr;
    bool ok = false; // false: the redirection failed - touch nothing
    char name[64];
    RegistryStandIn()
    {
        std::snprintf(name, sizeof(name), "Software\\thug2-decomp-test-%lu", GetCurrentProcessId());
        if (RegCreateKeyExA(HKEY_CURRENT_USER, name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &root, nullptr) !=
            ERROR_SUCCESS)
            return;
        ok = RegOverridePredefKey(HKEY_CURRENT_USER, root) == ERROR_SUCCESS;
    }
    ~RegistryStandIn()
    {
        if (!root)
            return;
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegDeleteTreeA(root, nullptr); // (through the scratch key's own handle)
        RegCloseKey(root);
        RegDeleteKeyA(HKEY_CURRENT_USER, name);
    }
    // Empty the scratch key between runs.
    void Clear() { RegDeleteTreeA(root, nullptr); }
    // Every value under the stand-in, as "key\value=data" lines.
    std::vector<std::string> Dump()
    {
        std::vector<std::string> lines;
        std::vector<std::string> keys = {""};
        for (size_t k = 0; k < keys.size(); ++k) {
            HKEY key;
            if (RegOpenKeyExA(root, keys[k].empty() ? nullptr : keys[k].c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
                continue;
            char child[256];
            for (DWORD i = 0; RegEnumKeyA(key, i, child, sizeof(child)) == ERROR_SUCCESS; ++i)
                keys.push_back(keys[k].empty() ? child : keys[k] + "\\" + child);
            char value[256];
            DWORD valueSize = sizeof(value), type = 0, data = 0, dataSize = sizeof(data);
            for (DWORD i = 0; RegEnumValueA(key, i, value, &valueSize, nullptr, &type, reinterpret_cast<BYTE*>(&data),
                                            &dataSize) == ERROR_SUCCESS;
                 ++i) {
                char line[600];
                std::snprintf(line, sizeof(line), "%s\\%s=%lu:%lu", keys[k].c_str(), value, type, data);
                lines.push_back(line);
                valueSize = sizeof(value);
                dataSize = sizeof(data);
            }
            RegCloseKey(key);
        }
        return lines;
    }
};

// Leftovers that were skipped before the goal was the whole game's source: the default constructors of the
// sphere and the capsule, CArray::SetType, the permanent-string switches, the script string slots, the
// registry defaults (key present) and the jumps that stand for functions of their own.
void TestLeftovers()
{
    // Constructors: the bytes and the returned pointer.
    for (int which = 0; which < 2; ++which) {
        alignas(16) uint8_t original[0x30], ours[0x30];
        std::memset(original, 0xEE, sizeof(original));
        std::memset(ours, 0xEE, sizeof(ours));
        void* returned = Original<void*(__thiscall*)(void*)>(which ? 0x00403220 : 0x00403210)(original);
        void* constructed = which ? static_cast<void*>(new (ours) Mth::Capsule()) : new (ours) Mth::Sphere();
        Check(returned == original && constructed == ours && std::memcmp(original, ours, sizeof(ours)) == 0,
              which ? "leftovers: Capsule()" : "leftovers: Sphere()");
    }
    {
        uint8_t original[12], ours[12];
        std::memset(original, 0x5A, sizeof(original));
        std::memset(ours, 0x5A, sizeof(ours));
        Original<void(__thiscall*)(void*, Script::ESymbolType)>(0x0046CAF0)(original, Script::ESymbolType(0x0D));
        reinterpret_cast<Script::CArray*>(ours)->SetType(Script::ESymbolType(0x0D));
        Check(std::memcmp(original, ours, sizeof(ours)) == 0, "leftovers: CArray::SetType");
    }
    // The permanent-string switches (with no table: nothing is freed).
    {
        uint8_t saved[0x18];
        std::memcpy(saved, reinterpret_cast<void*>(0x006F3570), sizeof(saved));
        std::vector<uint32_t> results[2];
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            std::memset(reinterpret_cast<void*>(0x006F3570), 0, sizeof(saved));
            GameVariable<uint32_t>(0x006F3574) = 0x1234;
            for (int step = 0; step < 3; ++step) {
                if (step == 1) {
                    if (original)
                        Original<void(__cdecl*)()>(0x00476300)();
                    else
                        Script::BeginPermanentStrings();
                } else {
                    if (original)
                        Original<void(__cdecl*)()>(0x00476310)();
                    else
                        Script::EndPermanentStrings();
                }
                for (int i = 0; i < 6; ++i)
                    results[run].push_back(GameVariable<uint32_t>(0x006F3570 + 4 * i));
            }
        }
        std::memcpy(reinterpret_cast<void*>(0x006F3570), saved, sizeof(saved));
        Check(results[0] == results[1], "leftovers: Begin/EndPermanentStrings (no table)");
    }
    // The script string slots: where they are; the writing (ours - the original's sprintf is the game's,
    // compared in the Late test).
    {
        bool same = true;
        for (int index : {0, 1, 5, 31})
            same = same && Original<char*(__cdecl*)(int)>(0x00476390)(index) == Script::GetScriptString(index);
        char saved[128];
        std::memcpy(saved, Script::GetScriptString(3), sizeof(saved));
        int written = Script::SetScriptString(3, "50%% off");
        same = same && written == 7 && std::strcmp(Script::GetScriptString(3), "50% off") == 0;
        std::memcpy(Script::GetScriptString(3), saved, sizeof(saved));
        Check(same, "leftovers: script string slots");
    }
    // The jumps: they run what they jump to.
    {
        const char* text = "Leftover Jumps";
        Check(Original<uint32_t(__cdecl*)(const char*)>(0x00472E60)(text) == Crc::GenerateCRCFromString(text) &&
                  Original<bool(__cdecl*)()>(0x004504F0)() == Stubs::ReturnFalse() &&
                  Original<int(__cdecl*)()>(0x0047AE10)() == Stubs::ReturnZero(),
              "leftovers: jumps to rewritten functions");
        uint32_t elements[2] = {0x11, 0x22};
        uint8_t array[12] = {};
        auto* a = reinterpret_cast<Script::CArray*>(array);
        a->m_type = Script::ESymbolType(1);
        a->m_size = 2;
        a->m_elements = elements;
        Check(Original<uint32_t(__thiscall*)(Script::CArray*, uint32_t)>(0x0041CB80)(a, 1) == a->GetElement(1),
              "leftovers: CArray element jump");
    }
    // Registry defaults, with the settings key present (the path without sprintf).
    {
        RegistryStandIn registry;
        if (!registry.ok) {
            Check(false, "leftovers: registry stand-in (couldn't redirect HKEY_CURRENT_USER)");
            return;
        }
        std::vector<std::string> dumps[2];
        for (int run = 0; run < 2; ++run) {
            HKEY settings;
            RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Activision\\Tony Hawk's Underground 2\\Settings", 0, nullptr,
                            0, KEY_ALL_ACCESS, nullptr, &settings, nullptr);
            DWORD present = 77;
            RegSetValueExA(settings, "Present", 0, REG_DWORD, reinterpret_cast<BYTE*>(&present), sizeof(present));
            RegCloseKey(settings);
            for (const char* name : {"Present", "Missing", "Missing"}) {
                uint32_t value = name[0] == 'P' ? 5 : 9;
                if (run == 0)
                    Original<void(__cdecl*)(const char*, uint32_t)>(0x004E1810)(name, value);
                else
                    Sys::WriteDefaultSetting(name, value);
            }
            dumps[run] = registry.Dump();
            registry.Clear();
        }
        Check(dumps[0] == dumps[1] && dumps[1].size() == 2, "leftovers: registry defaults (key present)");
    }
}

// The originals that use the game's C runtime: the script string slots and the registry defaults without
// the settings key.
void TestLeftoversLate()
{
    char saved[128];
    std::memcpy(saved, Script::GetScriptString(4), sizeof(saved));
    bool same = true;
    for (const char* text : {"", "plain", "100%% sure", "a much longer string than most, still under 128"}) {
        int original = Original<int(__cdecl*)(int, const char*)>(0x00476370)(4, text);
        std::string originalText = Script::GetScriptString(4);
        std::memset(Script::GetScriptString(4), 0, 128);
        int ours = Script::SetScriptString(4, text);
        same = same && original == ours && originalText == Script::GetScriptString(4);
    }
    std::memcpy(Script::GetScriptString(4), saved, sizeof(saved));
    Check(same, "leftovers (late): SetScriptString");

    RegistryStandIn registry;
    if (!registry.ok) {
        Check(false, "leftovers (late): registry stand-in (couldn't redirect HKEY_CURRENT_USER)");
        return;
    }
    std::vector<std::string> dumps[2];
    for (int run = 0; run < 2; ++run) {
        for (const char* name : {"First", "Second"}) {
            if (run == 0)
                Original<void(__cdecl*)(const char*, uint32_t)>(0x004E1810)(name, 3);
            else
                Sys::WriteDefaultSetting(name, 3);
        }
        dumps[run] = registry.Dump();
        registry.Clear();
    }
    Check(dumps[0] == dumps[1] && dumps[1].size() == 2, "leftovers (late): registry defaults (no key)");
}

// --- Sio ------------------------------------------------------------------------------------------------------

// What the stand-in force-feedback effects, keyboard and manager are asked to do.
// A jump written over a function's first five bytes for the length of a test.
struct TestDetour {
    uint8_t* at;
    uint8_t saved[5];
    TestDetour(uint32_t address, const void* to) : at(reinterpret_cast<uint8_t*>(address))
    {
        DWORD old;
        VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(saved, at, 5);
        at[0] = 0xE9;
        int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (address + 5));
        std::memcpy(at + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), at, 5);
    }
    ~TestDetour()
    {
        std::memcpy(at, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), at, 5);
    }
};

std::vector<uint32_t> g_sio_log;

HRESULT __stdcall SioEffectSetParameters(void* self, const uint32_t* effect, DWORD flags)
{
    const auto* direction = reinterpret_cast<const int32_t*>(effect[9]);
    g_sio_log.insert(g_sio_log.end(), {0x5E7u, reinterpret_cast<uint32_t>(self) & 0xF, effect[0], effect[1], effect[7],
                                        static_cast<uint32_t>(flags), static_cast<uint32_t>(direction[0]),
                                        static_cast<uint32_t>(direction[1]), *reinterpret_cast<const uint32_t*>(effect[12])});
    return 0;
}
HRESULT __stdcall SioEffectStop(void* self)
{
    g_sio_log.insert(g_sio_log.end(), {0x5709u, reinterpret_cast<uint32_t>(self) & 0xF});
    return 0;
}
HRESULT __stdcall SioUnused(void*) { return 0; }
// PartyMod's motor control (Sio::SetSdlActuators), which both the original (through PartyMod's jump at
// 0x005F3A50, written in) and ours (Platform::SetActuators) reach: the levels recorded instead of driving an SDL
// controller.
void __cdecl SioStandInSetActuators(int port, uint16_t high, uint16_t low)
{
    g_sio_log.insert(g_sio_log.end(), {0x5E7u, static_cast<uint32_t>(port), high, low});
}

// A keyboard whose GetDeviceState results are scripted.
std::vector<HRESULT> g_sio_keyboard_results;
int g_sio_keyboard_calls;
HRESULT __stdcall SioKeyboardAcquire(void*)
{
    g_sio_log.push_back(0xACC);
    HRESULT result = g_sio_keyboard_results.empty() ? 0 : g_sio_keyboard_results.front();
    if (!g_sio_keyboard_results.empty())
        g_sio_keyboard_results.erase(g_sio_keyboard_results.begin());
    return result;
}
HRESULT __stdcall SioKeyboardGetDeviceState(void*, DWORD size, void* buffer)
{
    g_sio_log.insert(g_sio_log.end(), {0x6E7u, static_cast<uint32_t>(size),
                                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer) - 0x007D1C20)});
    std::memset(buffer, 0x40 + ++g_sio_keyboard_calls, 4);
    HRESULT result = g_sio_keyboard_results.empty() ? 0 : g_sio_keyboard_results.front();
    if (!g_sio_keyboard_results.empty())
        g_sio_keyboard_results.erase(g_sio_keyboard_results.begin());
    return result;
}

void* __fastcall SioManagerDestroy(void* self, void*, unsigned int flags)
{
    g_sio_log.insert(g_sio_log.end(), {0xDE57u, reinterpret_cast<uint32_t>(self) & 0xFF, flags});
    return self;
}

// The device methods (motors traced at Sio::SetSdlActuators; pads present or not through the
// key binding table), the keyboard's state reading (a stand-in DirectInput keyboard) and typed characters, the
// manager's references, Lst::Search's destructor - original vs ours. Device::Process is PartyMod's (SDL) and
// the constructor calls the game's rand: see TestSioLate.
void TestSio()
{
    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {{0x007D6668, 8, {}},   {0x007D42C8, 0x18, {}},  {0x007D6318, 8, {}},
                                {0x007D6770, 0x260, {}}, {0x007D69D4, 4, {}}, {0x00684FC4, 4, {}},
                                {0x007D1C20, 0x200, {}}, {0x007D1E20, 0xC, {}}, {0x007D1BE0, 0x40, {}}};
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    // The original as PartyMod leaves it: the pad read at the start of ActivateActuator (push 0; call
    // 0x005BD200 at 0x005BDA31) is NOPed in the game - offline it would run the menu code.
    uint8_t activateBytes[7];
    auto* activateCall = reinterpret_cast<uint8_t*>(0x005BDA31);
    DWORD oldProtection;
    VirtualProtect(activateCall, 7, PAGE_EXECUTE_READWRITE, &oldProtection);
    std::memcpy(activateBytes, activateCall, 7);
    bool alreadyPatched = activateBytes[0] == 0x90;
    std::memset(activateCall, 0x90, 7);
    // The motors: PartyMod's jump at 0x005F3A50 to Sio::SetSdlActuators (as the game has it), and that recorded.
    TestDetour partyModMotors(0x005F3A50, reinterpret_cast<const void*>(&Sio::SetSdlActuators));
    TestDetour motors(reinterpret_cast<uint32_t>(&Sio::SetSdlActuators), reinterpret_cast<const void*>(&SioStandInSetActuators));
    static void* effectVtable[10];
    for (void*& slot : effectVtable)
        slot = reinterpret_cast<void*>(&SioUnused);
    effectVtable[6] = reinterpret_cast<void*>(&SioEffectSetParameters);
    effectVtable[8] = reinterpret_cast<void*>(&SioEffectStop);
    alignas(16) static void* effects[2][4];
    effects[0][0] = effectVtable;
    effects[1][0] = effectVtable;

    using Sio::Device;
    using D = Device*;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        std::vector<uint32_t>& trace = traces[run];
        g_sio_log.clear();
        alignas(16) static uint8_t devices[2][0x100];
        for (int port = 0; port < 2; ++port) {
            GameVariable<void*>(0x007D6668 + 4 * port) = effects[port];
            GameVariable<int>(0x007D42C8 + 4 * port) = port; // port 1: a one-motor device
            // Each motor's scale (the effect's range); zero offline, which would hide the levels.
            GameVariable<int>(0x007D42D0 + 8 * port) = 7000 + 1000 * port;
            GameVariable<int>(0x007D42D4 + 8 * port) = 3000 + 500 * port;
            GameVariable<void*>(0x007D6318 + 4 * port) = nullptr; // no joysticks
        }
        auto setBindings = [](int port, bool any) {
            auto* table = reinterpret_cast<int*>(0x007D6770 + port * 0x130);
            for (int i = 0; i < 0x4C; ++i)
                table[i] = -1;
            if (any)
                table[22] = 0x1C; // (the platform layer checks the first 23: the last one counts too)
        };
        auto call = [&](uint32_t address, D d, void (Device::*ours)()) {
            if (original)
                Original<void(__thiscall*)(D)>(address)(d);
            else
                (d->*ours)();
        };
        int variant = 0;
        for (int state : {0, 1, 2, 5, 3})
            for (uint32_t caps : {0u, 2u, 6u, 4u})
                for (int count : {0, 1, 2, 3})
                    for (int pauses : {0, 1, 2}) {
                        ++variant;
                        int port = variant & 1;
                        setBindings(port, (variant / 2) % 3 != 0);
                        auto* d = reinterpret_cast<D>(devices[port]);
                        for (int i = 0; i < 0x100; ++i)
                            devices[port][i] = static_cast<uint8_t>(i * 7 + variant);
                        d->m_port = port;
                        d->m_state = state;
                        d->m_nextState = 9;
                        d->m_capabilities = caps;
                        d->m_numActuators = count;
                        d->m_pauseCount = pauses;
                        d->m_actuatorsDisabled = static_cast<uint8_t>((variant / 3) & 1);
                        d->m_actuatorMax[0] = 200;
                        d->m_actuatorMax[1] = 255;
                        d->m_actuatorMax[2] = 13;
                        switch (variant % 11) {
                        case 0: call(0x005BCC70, d, &Device::Acquire); break;
                        case 1: call(0x005BD8B0, d, &Device::Detect); break;
                        case 2: call(0x005BCCC0, d, &Device::Settle); break;
                        case 3: call(0x005BDAD0, d, &Device::DisableActuators); break;
                        case 4: call(0x005BDB30, d, &Device::ResetActuators); break;
                        case 5: call(0x005BDB60, d, &Device::PauseActuators); break;
                        case 6: call(0x005BDBD0, d, &Device::UnpauseActuators); break;
                        case 7: call(0x005BDC60, d, &Device::StopActuators); break;
                        case 8: call(0x005BCCB0, d, &Device::EnableActuators); break;
                        case 9:
                            trace.push_back(original ? Original<bool(__thiscall*)(D)>(0x005BCC60)(d) : d->IsPluggedIn());
                            break;
                        default:
                            for (int actuator : {-1, 0, 1, 2, 3}) {
                                for (int percent : {0, 37, 50, 100, 250, -10}) {
                                    d->m_actuatorsDisabled = static_cast<uint8_t>(percent == 250 && actuator == 1);
                                    if (original)
                                        Original<void(__thiscall*)(D, int, int)>(0x005BDA20)(d, actuator, percent);
                                    else
                                        d->ActivateActuator(actuator, percent);
                                }
                            }
                        }
                        trace.insert(trace.end(), reinterpret_cast<uint32_t*>(devices[port]),
                                     reinterpret_cast<uint32_t*>(devices[port] + 0x100));
                    }
        // Pauses nest; unpausing restores the levels (with motors that are going).
        {
            auto* d = reinterpret_cast<D>(devices[0]);
            std::memset(devices[0], 0, sizeof(devices[0]));
            d->m_state = Device::kReady;
            d->m_capabilities = Device::kHasActuators;
            d->m_numActuators = 2;
            d->m_actuatorDirect[0] = 90;
            d->m_actuatorDirect[1] = 30;
            for (uint32_t address : {0x005BDB60u, 0x005BDB60u, 0x005BDBD0u, 0x005BDBD0u, 0x005BDBD0u}) {
                if (original)
                    Original<void(__thiscall*)(D)>(address)(d);
                else if (address == 0x005BDB60)
                    d->PauseActuators();
                else
                    d->UnpauseActuators();
                trace.insert(trace.end(), reinterpret_cast<uint32_t*>(devices[0]),
                             reinterpret_cast<uint32_t*>(devices[0] + 0x100));
            }
        }

        // The keyboard: no device, then a stand-in one (lost, then re-acquired or not).
        static void* keyboardVtable[10];
        for (void*& slot : keyboardVtable)
            slot = reinterpret_cast<void*>(&SioUnused);
        keyboardVtable[7] = reinterpret_cast<void*>(&SioKeyboardAcquire);
        keyboardVtable[9] = reinterpret_cast<void*>(&SioKeyboardGetDeviceState);
        static void* keyboard[1] = {keyboardVtable};
        auto read = [&] {
            if (original)
                Original<void(__cdecl*)()>(0x005BDD60)();
            else
                Sio::ReadKeyboard();
            trace.push_back(GameVariable<uint32_t>(0x00684FC4));
            trace.push_back(GameVariable<uint32_t>(0x007D1E28));
            trace.insert(trace.end(), reinterpret_cast<uint32_t*>(0x007D1C20), reinterpret_cast<uint32_t*>(0x007D1C20) + 2);
            trace.insert(trace.end(), reinterpret_cast<uint32_t*>(0x007D1D20), reinterpret_cast<uint32_t*>(0x007D1D20) + 2);
        };
        g_sio_keyboard_calls = 0;
        std::memset(reinterpret_cast<void*>(0x007D1C20), 0x77, 0x200);
        GameVariable<int>(0x00684FC4) = 2;
        GameVariable<void*>(0x007D69D4) = nullptr;
        GameVariable<uint32_t>(0x007D1E28) = 5;
        read();
        read();
        GameVariable<void*>(0x007D69D4) = keyboard;
        GameVariable<int>(0x00684FC4) = 2;
        g_sio_keyboard_results = {0, static_cast<HRESULT>(0x8007001E), 0, 0, static_cast<HRESULT>(0x8007000C),
                                  static_cast<HRESULT>(0x80004005), static_cast<HRESULT>(0x80070005), 0};
        for (int i = 0; i < 5; ++i)
            read();

        // Typed characters.
        for (uint32_t count : {0u, 1u, 5u, 16u}) {
            GameVariable<uint32_t>(0x007D1E28) = count;
            for (int i = 0; i < 16; ++i)
                GameVariable<int>(0x007D1BE0 + 4 * i) = 0x4100 + 'a' + i;
            char out[20];
            std::memset(out, '#', sizeof(out));
            int got = original ? Original<int(__cdecl*)(char*)>(0x005BDD30)(out) : Sio::GetKeyboardChars(out);
            trace.push_back(static_cast<uint32_t>(got));
            trace.insert(trace.end(), reinterpret_cast<uint32_t*>(out), reinterpret_cast<uint32_t*>(out + 20));
            if (original)
                Original<void(__cdecl*)()>(0x005BDE20)();
            else
                Sio::ClearKeyboardChars();
            trace.push_back(GameVariable<uint32_t>(0x007D1E28));
        }

        // The manager's references (an existing instance - its constructor isn't ours yet - and none).
        static void* managerVtable[1] = {reinterpret_cast<void*>(&SioManagerDestroy)};
        alignas(16) static void* manager[2] = {managerVtable, nullptr};
        for (bool exists : {true, false}) {
            GameVariable<void*>(0x007D1E20) = exists ? manager : nullptr;
            GameVariable<uint32_t>(0x007D1E24) = 0;
            for (int step = 0; step < 7; ++step) {
                if (step < 3) {
                    bool create = exists && step != 1; // (never actually making one: no constructor offline)
                    void* got = original ? Original<void*(__cdecl*)(bool)>(0x005BE670)(create)
                                         : static_cast<void*>(Sio::CreateManager(create));
                    trace.push_back(got == manager ? 1u : reinterpret_cast<uint32_t>(got));
                } else if (original) {
                    Original<void(__cdecl*)()>(0x005BDCF0)();
                } else {
                    Sio::ReleaseManager();
                }
                trace.push_back(GameVariable<void*>(0x007D1E20) == manager ? 1u : GameVariable<uint32_t>(0x007D1E20));
                trace.push_back(GameVariable<uint32_t>(0x007D1E24));
            }
        }

        // Lst::Search's destructors (not freeing).
        {
            uint32_t object[2] = {0x12345678, 0x9ABC};
            if (original)
                Original<void(__thiscall*)(void*)>(0x005BDE30)(object);
            else
                reinterpret_cast<Lst::SearchDestructors*>(object)->DestroyBody();
            trace.insert(trace.end(), object, object + 2);
            object[0] = 0x12345678;
            void* result = original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x005BDE40)(object, 0)
                                    : reinterpret_cast<Lst::SearchDestructors*>(object)->Destroy(0);
            trace.push_back(result == object);
            trace.insert(trace.end(), object, object + 2);
        }
        trace.insert(trace.end(), g_sio_log.begin(), g_sio_log.end());
    }
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    if (!alreadyPatched)
        std::memcpy(activateCall, activateBytes, 7);
    VirtualProtect(activateCall, 7, oldProtection, &oldProtection);
    Check(traces[0] == traces[1], "sio: devices, keyboard, manager references");
    if (traces[0].size() != traces[1].size())
        Log("  sio trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i) {
        if (traces[0][i] != traces[1][i]) {
            Log("  sio entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    }
}

// In the game (the constructor's original uses the game's rand; the memory manager is up): the constructor
// (all but the random retry delay, which must be 0-63), destroying with and without freeing, and
// Lst::Search's freeing destructor.
void TestSioLate()
{
    using Sio::Device;
    bool same = true;
    for (int run = 0; run < 3; ++run) {
        alignas(16) uint8_t original[0x100], ours[0x100];
        std::memset(original, 0x3C + run, sizeof(original));
        std::memset(ours, 0x3C + run, sizeof(ours));
        auto* a = Original<Device*(__thiscall*)(void*, int, int, int)>(0x005BD920)(original, run, run + 4, run * 3);
        auto* b = new (ours) Device(run, run + 4, run * 3);
        same = same && a == reinterpret_cast<Device*>(original) && b == reinterpret_cast<Device*>(ours);
        auto* nodeA = reinterpret_cast<Mem::ListNode*>(a->m_node);
        auto* nodeB = reinterpret_cast<Mem::ListNode*>(b->m_node);
        same = same && nodeA && nodeB && nodeA->vtable == nodeB->vtable && nodeA->data == a && nodeB->data == b &&
               nodeA->priority == nodeB->priority && nodeA->next == nodeA && nodeB->next == nodeB &&
               nodeA->prev == nodeA && nodeB->prev == nodeB;
        same = same && a->m_unpluggedRetry >= 0 && a->m_unpluggedRetry < 64 && b->m_unpluggedRetry >= 0 &&
               b->m_unpluggedRetry < 64;
        a->m_unpluggedRetry = b->m_unpluggedRetry = 0;
        a->m_node = b->m_node = nullptr;
        same = same && std::memcmp(original, ours, sizeof(ours)) == 0;
        a->m_node = nodeA;
        b->m_node = nodeB;
        Original<void*(__thiscall*)(void*, unsigned int)>(0x005BCD00)(a, 0);
        b->Destroy(0);
        a->m_node = b->m_node = nullptr; // (each still points at its own, now freed, node)
        same = same && std::memcmp(original, ours, sizeof(ours)) == 0;
    }
    for (int run = 0; run < 2; ++run) {
        void* memory = Mem::Calloc(0xF0);
        void* object = Mem::Calloc(8);
        auto* device = new (memory) Device(1, 1, 0);
        auto* plain = static_cast<Lst::SearchDestructors*>(object);
        plain->m_vtable = reinterpret_cast<void**>(0x00658AA4);
        if (run == 0) {
            Original<void*(__thiscall*)(void*, unsigned int)>(0x005BCD00)(device, 1);
            Original<void*(__thiscall*)(void*, unsigned int)>(0x005BDE40)(plain, 1);
        } else {
            device->Destroy(1);
            plain->Destroy(1);
        }
    }
    Check(same, "sio (late): Device constructor and destructor");
}

void* __fastcall SioStandInDestroy(void* self, void*, unsigned int flags)
{
    g_sio_log.insert(g_sio_log.end(), {0xDE57u, reinterpret_cast<uint32_t>(self) & 0xFFF, flags});
    return self;
}

// Typed keys, and the device manager: looking devices up, pausing/unpausing them all (motors traced as in
// TestSio), processing an empty list, the list node's destructor, the manager's destructor (stand-in devices
// and task record being deleted) - original vs ours. The constructor (devices call the game's rand): see
// TestSioManagerLate.
void TestSioManager()
{
    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {{0x007CE46E, 2, {}}, {0x007CCDF8, 1, {}}, {0x007D1BE0, 0x40, {}},
                                {0x007D1E28, 4, {}}, {0x007D6668, 8, {}}, {0x007D42C8, 0x18, {}},
                                {0x007D6A38, 4, {}}};
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    static void* effectVtable[10];
    for (void*& slot : effectVtable)
        slot = reinterpret_cast<void*>(&SioUnused);
    effectVtable[6] = reinterpret_cast<void*>(&SioEffectSetParameters);
    effectVtable[8] = reinterpret_cast<void*>(&SioEffectStop);
    alignas(16) static void* effects[2][4] = {{effectVtable}, {effectVtable}};
    // The motors: PartyMod's jump at 0x005F3A50 to Sio::SetSdlActuators (as the game has it), and that recorded.
    TestDetour partyModMotors(0x005F3A50, reinterpret_cast<const void*>(&Sio::SetSdlActuators));
    TestDetour motors(reinterpret_cast<uint32_t>(&Sio::SetSdlActuators), reinterpret_cast<const void*>(&SioStandInSetActuators));
    static void* standInVtable[4] = {reinterpret_cast<void*>(&SioStandInDestroy), nullptr,
                                     reinterpret_cast<void*>(&SioStandInDestroy), nullptr};

    using Sio::Device;
    using Sio::Manager;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        std::vector<uint32_t>& trace = traces[run];
        g_sio_log.clear();

        // Typed keys.
        for (int globals = 0; globals < 8; ++globals) {
            GameVariable<bool>(0x007CE46E) = (globals & 1) != 0;
            GameVariable<bool>(0x007CCDF8) = (globals & 2) != 0;
            GameVariable<bool>(0x007CE46F) = (globals & 4) != 0;
            for (uint32_t start : {0u, 14u, 15u}) {
                GameVariable<uint32_t>(0x007D1E28) = start;
                std::memset(reinterpret_cast<void*>(0x007D1BE0), 0xCC, 0x40);
                for (int key : {-2, -1, 0, 8, 13, 27, 0x22, 0x25, 0x3A, 0x3B, 0x41, 0x5B, 0x5C, 0x5D, 0x61, 0x7C, 0x7D,
                                0x7E, 0x7F, 300}) {
                    for (int flags : {0, 0x40000000, -5, 1}) {
                        if (original)
                            Original<void(__cdecl*)(int, int)>(0x005BDE70)(key, flags);
                        else
                            Sio::KeyInput(key, flags);
                        uint32_t count = GameVariable<uint32_t>(0x007D1E28);
                        trace.push_back(count);
                        trace.push_back(GameVariable<uint32_t>(0x007D1BE0 + 4 * ((count + 15) & 15))); // the last one
                        if (GameVariable<uint32_t>(0x007D1E28) == 15)
                            GameVariable<uint32_t>(0x007D1E28) = start; // (keep queueing)
                    }
                }
                trace.insert(trace.end(), reinterpret_cast<uint32_t*>(0x007D1BE0),
                             reinterpret_cast<uint32_t*>(0x007D1BE0) + 16);
            }
        }

        // A stand-in manager with three devices (ports 0, 1, 0; slots 0, 0, 1).
        for (int port = 0; port < 2; ++port) {
            GameVariable<void*>(0x007D6668 + 4 * port) = effects[port];
            GameVariable<int>(0x007D42C8 + 4 * port) = port;
            GameVariable<int>(0x007D42D0 + 8 * port) = 7000 + 1000 * port;
            GameVariable<int>(0x007D42D4 + 8 * port) = 3000 + 500 * port;
        }
        alignas(16) static uint8_t devices[3][0x100];
        static Mem::ListNode nodes[3];
        alignas(16) static uint8_t managerBytes[0x20];
        static uint32_t task[8];
        auto build = [&](int count) {
            std::memset(managerBytes, 0, sizeof(managerBytes));
            std::memset(devices, 0, sizeof(devices));
            auto* manager = reinterpret_cast<Manager*>(managerBytes);
            Mem::ListNode* head = &manager->m_devices;
            *head = {reinterpret_cast<void**>(0x00658A70), reinterpret_cast<void*>(-1), 0, head, head};
            manager->m_vtable = reinterpret_cast<void**>(0x12345678); // (the destructor sets its own)
            task[0] = reinterpret_cast<uint32_t>(standInVtable);
            manager->m_task = task;
            for (int i = 0; i < count; ++i) {
                for (int b = 0; b < 0x100; ++b)
                    devices[i][b] = static_cast<uint8_t>(b * 5 + i);
                auto* d = reinterpret_cast<Device*>(devices[i]);
                d->m_vtable = reinterpret_cast<void**>(standInVtable);
                d->m_port = i == 1 ? 1 : 0;
                d->m_slot = i == 2 ? 1 : 0;
                d->m_index = 10 + i;
                d->m_state = i == 2 ? Device::kIdle : Device::kReady;
                d->m_capabilities = Device::kHasActuators;
                d->m_numActuators = 2;
                d->m_pauseCount = i == 1 ? 1 : 0;
                d->m_node = &nodes[i];
                nodes[i] = {reinterpret_cast<void**>(0x00658A70), d, 0, head, head->prev};
                head->prev->next = &nodes[i];
                head->prev = &nodes[i];
            }
            return manager;
        };
        auto dumpDevices = [&] {
            for (auto& d : devices)
                trace.insert(trace.end(), reinterpret_cast<uint32_t*>(d), reinterpret_cast<uint32_t*>(d + 0x100));
        };
        auto relocate = [&](void* p) -> uint32_t {
            for (int i = 0; i < 3; ++i)
                if (p == devices[i])
                    return 0xD0 + i;
            return reinterpret_cast<uint32_t>(p);
        };
        for (int count : {0, 1, 3}) {
            Manager* manager = build(count);
            for (int port = -1; port <= 2; ++port)
                for (int slot = 0; slot <= 2; ++slot)
                    trace.push_back(relocate(original
                                                 ? Original<Device*(__thiscall*)(Manager*, int, int)>(0x005BE490)(manager, port, slot)
                                                 : manager->GetDevice(port, slot)));
            for (int index = 9; index <= 13; ++index)
                trace.push_back(relocate(original ? Original<Device*(__thiscall*)(Manager*, int)>(0x005BE4F0)(manager, index)
                                                  : manager->GetDeviceByIndex(index)));
            for (uint32_t address : {0x005BE550u, 0x005BE550u, 0x005BE5E0u, 0x005BE5E0u, 0x005BE5E0u}) {
                if (original)
                    Original<void(__thiscall*)(Manager*)>(address)(manager);
                else if (address == 0x005BE550)
                    manager->PauseDevices();
                else
                    manager->UnpauseDevices();
                dumpDevices();
            }
            if (count == 0) {
                task[7] = reinterpret_cast<uint32_t>(&manager->m_devices);
                if (original) {
                    Original<void(__thiscall*)(Manager*)>(0x005BE3F0)(manager);
                    Original<void(__cdecl*)(void*)>(0x005BE140)(task);
                } else {
                    manager->ProcessDevices();
                    Sio::ProcessTask(task);
                }
                task[7] = 0;
            }
            // The destructor (the player state call skipped: no network block offline).
            GameVariable<void*>(0x007D6A38) = nullptr;
            void* result = original ? Original<void*(__thiscall*)(Manager*, unsigned int)>(0x005BE6C0)(manager, 0)
                                    : manager->Destroy(0);
            trace.push_back(result == manager);
            trace.insert(trace.end(), reinterpret_cast<uint32_t*>(managerBytes),
                         reinterpret_cast<uint32_t*>(managerBytes + 0x1C));
            for (uint32_t& word : trace)
                if (word == reinterpret_cast<uint32_t>(&manager->m_devices))
                    word = 0xEAD;
        }
        // The list node's destructor (not freeing).
        {
            Mem::ListNode ring[3];
            for (int i = 0; i < 3; ++i)
                ring[i] = {nullptr, &ring[i], i, &ring[(i + 1) % 3], &ring[(i + 2) % 3]};
            void* result = original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x005BE110)(&ring[1], 0)
                                    : Sio::DestroyDeviceNode(&ring[1], 0);
            trace.push_back(result == &ring[1]);
            for (Mem::ListNode& n : ring) {
                trace.push_back(reinterpret_cast<uint32_t>(n.vtable));
                trace.push_back(static_cast<uint32_t>(n.next - ring));
                trace.push_back(static_cast<uint32_t>(n.prev - ring));
            }
        }
        trace.insert(trace.end(), g_sio_log.begin(), g_sio_log.end());
    }
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    Check(traces[0] == traces[1], "sio: typed keys, device manager");
    if (traces[0].size() != traces[1].size())
        Log("  sio manager trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i) {
        if (traces[0][i] != traces[1][i]) {
            Log("  sio manager entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    }
}

// In the game: the manager's constructor (its devices, list and task), then its freeing destructor - original
// vs ours. The player state set-up both run is skipped (0x007D6A38 cleared meanwhile) so the game's state
// isn't touched twice.
void TestSioManagerLate()
{
    using Sio::Device;
    using Sio::Manager;
    void* network = GameVariable<void*>(0x007D6A38);
    GameVariable<void*>(0x007D6A38) = nullptr;
    std::vector<uint32_t> results[2];
    for (int run = 0; run < 2; ++run) {
        void* memory = Mem::Calloc(0x1C, nullptr, 1);
        auto* manager = run == 0 ? Original<Manager*(__thiscall*)(void*)>(0x005BE1F0)(memory) : new (memory) Manager();
        std::vector<uint32_t>& r = results[run];
        r.push_back(manager == memory);
        r.push_back(reinterpret_cast<uint32_t>(manager->m_vtable));
        r.push_back(reinterpret_cast<uint32_t>(manager->m_devices.vtable));
        r.push_back(reinterpret_cast<uint32_t>(manager->m_devices.data));
        r.push_back(static_cast<uint32_t>(manager->m_devices.priority));
        auto* task = static_cast<uint32_t*>(manager->m_task);
        r.push_back(task != nullptr);
        if (task) {
            r.push_back(task[0]);
            r.push_back(task[6] == 0x005BE140 || task[6] == reinterpret_cast<uint32_t>(&Sio::ProcessTask));
            r.push_back(task[7] == reinterpret_cast<uint32_t>(&manager->m_devices));
        }
        Lst::Search<Device> search;
        for (Device* d = search.FirstItem(&manager->m_devices); d; d = search.NextItem()) {
            r.push_back(d->m_node->data == d);
            uint8_t copy[0xF0];
            std::memcpy(copy, d, sizeof(copy));
            reinterpret_cast<Device*>(copy)->m_node = nullptr;
            reinterpret_cast<Device*>(copy)->m_unpluggedRetry = 0;
            r.insert(r.end(), reinterpret_cast<uint32_t*>(copy), reinterpret_cast<uint32_t*>(copy + 0xF0));
        }
        r.push_back(reinterpret_cast<uint32_t>(manager->m_devices.prev->next) == reinterpret_cast<uint32_t>(&manager->m_devices));
        if (run == 0)
            Original<void*(__thiscall*)(void*, unsigned int)>(0x005BE6C0)(manager, 1);
        else
            manager->Destroy(1);
    }
    GameVariable<void*>(0x007D6A38) = network;
    Check(results[0] == results[1], "sio (late): device manager constructor");
}

// A stand-in screen element manager: the element lookup (0x004AAE20) searches the hash table at +8, then the
// one at +0xC; each has one bucket here, chaining the stand-in elements.
struct StandInElements {
    struct Item {
        uint32_t key;
        void* value;
        Item* next;
    };
    struct Table {
        int bits;
        Item* buckets;
    };
    alignas(16) uint8_t elements[4][0x50];
    Item items[4];
    Item empty;
    Table first;
    Table second;
    uint8_t manager[0x10];

    // Element i is `id` (absent if !present), hidden (0x400000) or inside a hidden parent (element 3) as asked.
    void Set(const std::vector<std::tuple<uint32_t, bool, int>>& wanted)
    {
        std::memset(elements, 0, sizeof(elements));
        std::memset(items, 0, sizeof(items));
        empty = {0, nullptr, nullptr};
        Item* last = nullptr;
        for (size_t i = 0; i < wanted.size(); ++i) {
            auto [id, present, state] = wanted[i];
            auto* e = reinterpret_cast<uint32_t*>(elements[i]);
            e[0] = 0x00777777;                                   // (vtable: unused)
            e[2] = state == 1 ? 0x400000u | 0x13 : 0x13;           // hidden itself
            e[0x4C / 4] = state == 2 ? reinterpret_cast<uint32_t>(elements[3]) : 0; // or its parent is
            if (!present)
                continue;
            items[i] = {id, elements[i], nullptr};
            if (last)
                last->next = &items[i];
            last = &items[i];
        }
        reinterpret_cast<uint32_t*>(elements[3])[2] = 0x400000; // the hidden parent
        first = {0, last ? &items[0] : &empty};
        // (the second table: always empty)
        second = {0, &empty};
        std::memset(manager, 0, sizeof(manager));
        *reinterpret_cast<Table**>(manager + 8) = &first;
        *reinterpret_cast<Table**>(manager + 0xC) = &second;
        GameVariable<void*>(0x00701440) = manager;
        GameVariable<void*>(0x006F0FC0) = manager; // (the lookup uses this one's tables)
    }
    // Every element's smart pointer list must be empty again.
    uint32_t Pointers() const
    {
        uint32_t any = 0;
        for (const auto& e : elements)
            any |= reinterpret_cast<const uint32_t*>(e)[1];
        return any;
    }
};

// Smart pointers (add, remove, release), IsHidden, the F-key handler and the key names over stand-in screen
// elements - original vs ours.
void TestSioKeyNames()
{
    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {{0x00701440, 4, {}},     {0x006F0FC0, 4, {}}, {0x007CE46C, 4, {}},   {0x007CCDF8, 1, {}},
                                {0x007D6A2C, 1, {}},     {0x007D6770, 0x60, {}}, {0x007D42F8 - 0x1E, 0x1E * 0x42, {}},
                                {0x007D19D8, 0x200, {}}, {0x007D1BE0, 0x40, {}}, {0x007D1E28, 4, {}}};
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    // Key names "K<code>" for codes -1 to 0x40; bindings 8-18 are codes 0x20-0x2A.
    for (int code = -1; code <= 0x40; ++code) {
        auto* name = reinterpret_cast<wchar_t*>(0x007D42F8 + code * 0x1E); // (30 bytes each)
        std::swprintf(name, 15, L"K%02X%s", code & 0xFF, code == 0x22 ? L"_long_name" : L"");
    }
    for (int i = 0; i < 24; ++i)
        GameVariable<int>(0x007D6770 + 4 * i) = i >= 8 && i <= 18 ? 0x20 + (i - 8) : -1;

    static StandInElements standIn;
    using Obj::CRefCounted;
    using P = Obj::CSmtPtr<CRefCounted>;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        std::vector<uint32_t>& trace = traces[run];

        // Smart pointers on one object: add three, remove the middle, the first, release the last.
        {
            alignas(16) static uint8_t object[8];
            static P pointers[4];
            std::memset(object, 0, sizeof(object));
            std::memset(pointers, 0xEE, sizeof(pointers));
            auto* o = reinterpret_cast<CRefCounted*>(object);
            auto dump = [&] {
                auto rel = [&](const void* p) {
                    for (int i = 0; i < 4; ++i)
                        if (p == &pointers[i])
                            return 0x50u + i;
                    return reinterpret_cast<uint32_t>(p);
                };
                trace.push_back(rel(o->mp_smartPointers));
                for (P& p : pointers) {
                    trace.push_back(rel(p.mp_prev));
                    trace.push_back(rel(p.mp_next));
                    trace.push_back(p.mp_object == reinterpret_cast<CRefCounted*>(object) ? 1u
                                                                                           : reinterpret_cast<uint32_t>(p.mp_object));
                }
            };
            for (int i = 0; i < 3; ++i) {
                pointers[i].mp_object = o;
                pointers[i].mp_prev = nullptr;
                if (original)
                    Original<void(__thiscall*)(void*, P*)>(0x004637C0)(o, &pointers[i]);
                else
                    o->AddSmartPointer(&pointers[i]);
                dump();
            }
            for (int i : {1, 2}) {
                if (original)
                    Original<void(__thiscall*)(void*, P*)>(0x004637E0)(o, &pointers[i]);
                else
                    o->RemoveSmartPointer(&pointers[i]);
                dump();
            }
            for (int i : {0, 0, 3}) {
                pointers[3].mp_object = nullptr;
                if (original)
                    Original<void(__thiscall*)(P*)>(0x00416DD0)(&pointers[i]);
                else
                    pointers[i].Release();
                dump();
            }
        }

        // IsHidden: itself, through parents, neither.
        {
            alignas(16) static uint8_t chain[4][0x50];
            std::memset(chain, 0, sizeof(chain));
            for (int i = 0; i < 3; ++i)
                reinterpret_cast<uint32_t*>(chain[i])[0x4C / 4] = reinterpret_cast<uint32_t>(chain[i + 1]);
            for (uint32_t hiddenAt = 0; hiddenAt <= 4; ++hiddenAt) {
                for (int i = 0; i < 4; ++i)
                    reinterpret_cast<uint32_t*>(chain[i])[2] = static_cast<uint32_t>(i) == hiddenAt ? 0x400000u : 0xBFFFFFu;
                for (auto& e : chain)
                    trace.push_back(original ? Original<bool(__thiscall*)(void*)>(0x004A8F40)(e)
                                             : reinterpret_cast<Front::CScreenElement*>(e)->IsHidden());
            }
        }

        // The F-key handler (F5 with the server list shown runs a script: not offline).
        uint32_t refresh = Crc::GenerateCRCFromString("menu_network_select_refresh");
        for (int shown = 0; shown < 4; ++shown) {
            standIn.Set({{refresh, shown != 0, shown == 1 ? 0 : shown == 2 ? 1 : 2}});
            for (int globals = 0; globals < 8; ++globals) {
                GameVariable<bool>(0x007CE46E) = (globals & 1) != 0;
                GameVariable<bool>(0x007CCDF8) = (globals & 2) != 0;
                GameVariable<bool>(0x007CE46F) = (globals & 4) != 0;
                std::memset(reinterpret_cast<void*>(0x007D1BE0), 0, 0x40);
                GameVariable<uint32_t>(0x007D1E28) = (globals & 1) ? 15 : 13; // (15: the queue is full)
                for (int key : {0x25, 0x26, 0x27, 0x28, 0x29, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x41}) {
                    if (key == 0x74 && shown == 1)
                        continue;
                    for (int flags : {0, -1}) {
                        if (original)
                            Original<void(__cdecl*)(int, int)>(0x005BDF30)(key, flags);
                        else
                            Sio::SpecialKeyInput(key, flags);
                        uint32_t count = GameVariable<uint32_t>(0x007D1E28);
                        trace.push_back(count);
                        trace.push_back(GameVariable<uint32_t>(0x007D1BE0 + 4 * ((count + 15) & 15)));
                        if (count == 15 && !(globals & 1))
                            GameVariable<uint32_t>(0x007D1E28) = 13;
                    }
                }
            }
            trace.push_back(standIn.Pointers());
        }

    }
    // Ours against worked expectations (the original's needs the game's C runtime: see TestSioKeyNamesLate).
    {
        auto ch = [](UINT scan) { return static_cast<char>(MapVirtualKeyA(MapVirtualKeyA(scan, 1), 2)); };
        std::string sx = {ch(0x1F), '/', ch(0x2D)};
        std::string zc = {ch(0x2C), '/', ch(0x2E)};
        uint32_t select = Crc::GenerateCRCFromString("select_skater_hmenu");
        uint32_t piece = Crc::GenerateCRCFromString("piece_menu_hmenu");
        struct Case {
            bool menu, typing, pieceShown;
            bool otherMenu, quit;
            int button;
            std::string expected;
        };
        std::vector<Case> cases = {
            {true, false, false, false, false, 0, "K23"},  {true, false, false, false, false, 1, "K22_long_name"},
            {true, false, false, false, false, 3, "K20"},  {true, false, false, false, false, 9, "K39"},
            {true, false, false, false, false, 8, "K01"},  {true, false, false, false, false, 21, "K2A"},
            {true, false, false, false, false, 22, "K1C"}, {true, false, false, false, false, 24, "K12"},
            {true, false, false, false, false, 29, "K03"}, {true, false, false, false, false, 5, "K00"},
            {true, false, false, false, false, 0x12, "K28+K29"}, {true, false, false, false, false, 0x14, sx},
            {true, false, false, false, false, 0x13, zc},  {false, false, false, false, false, 9, "K25"},
            {false, false, false, false, false, 0x14, "K00"}, {false, false, false, false, false, 24, "K00"},
            {true, true, false, false, false, 0, "KFF"},   {true, true, false, false, false, 22, "K1C"},
            {true, true, false, false, false, 23, "K01"},  {true, true, false, false, false, 0x13, "K00"},
            {true, false, true, false, false, 9, "K25"},   {true, false, true, false, false, 0x14, "K00"},
            // the other menu flag counts too; quitting ignores the piece menu
            {false, false, false, true, false, 9, "K39"},  {true, false, true, false, true, 9, "K39"},
        };
        bool ok = true;
        for (const Case& c : cases) {
            standIn.Set({{select, true, 0}, {piece, c.pieceShown, 0}});
            GameVariable<bool>(0x007CE46C) = c.menu;
            GameVariable<bool>(0x007CE46F) = c.otherMenu;
            GameVariable<bool>(0x007CE46E) = c.typing;
            GameVariable<bool>(0x007D6A2C) = c.quit;
            std::string got = Sio::GetButtonKeyName(c.button);
            if (got != c.expected) {
                Log("  key name %d (menu %d typing %d piece %d): \"%s\", expected \"%s\"", c.button, c.menu, c.typing,
                    c.pieceShown, got.c_str(), c.expected.c_str());
                ok = false;
            }
        }
        Check(ok && standIn.Pointers() == 0, "sio: key names (ours vs expected)");
    }
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    Check(traces[0] == traces[1], "sio: smart pointers, IsHidden, special keys");
    if (traces[0].size() != traces[1].size())
        Log("  key name trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i) {
        if (traces[0][i] != traces[1][i]) {
            Log("  key name entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    }
}

// In the game (the original uses the game's C runtime: wcstombs, sprintf): the key names over stand-in screen
// elements, original vs ours.
void TestSioKeyNamesLate()
{
    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {{0x00701440, 4, {}},     {0x006F0FC0, 4, {}}, {0x007CE46C, 4, {}},   {0x007CCDF8, 1, {}},
                                {0x007D6A2C, 1, {}},     {0x007D6770, 0x60, {}}, {0x007D42F8 - 0x1E, 0x1E * 0x42, {}},
                                {0x007D19D8, 0x200, {}}, {0x007D1BE0, 0x40, {}}, {0x007D1E28, 4, {}}};
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    // Key names "K<code>" for codes -1 to 0x40; bindings 8-18 are codes 0x20-0x2A.
    for (int code = -1; code <= 0x40; ++code) {
        auto* name = reinterpret_cast<wchar_t*>(0x007D42F8 + code * 0x1E); // (30 bytes each)
        std::swprintf(name, 15, L"K%02X%s", code & 0xFF, code == 0x22 ? L"_long_name" : L"");
    }
    for (int i = 0; i < 24; ++i)
        GameVariable<int>(0x007D6770 + 4 * i) = i >= 8 && i <= 18 ? 0x20 + (i - 8) : -1;

    static StandInElements standIn;
    using Obj::CRefCounted;
    using P = Obj::CSmtPtr<CRefCounted>;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        std::vector<uint32_t>& trace = traces[run];

        // Key names.
        uint32_t select = Crc::GenerateCRCFromString("select_skater_hmenu");
        uint32_t piece = Crc::GenerateCRCFromString("piece_menu_hmenu");
        for (int scene = 0; scene < 6; ++scene) {
            // 0: none; 1: piece menu shown; 2: piece menu hidden; 3: both shown; 4: piece menu in a hidden parent;
            // 5: select skater only
            standIn.Set({{select, scene == 3 || scene == 5, 0},
                         {piece, scene >= 1 && scene <= 4 && scene != 5, scene == 2 ? 1 : scene == 4 ? 2 : 0}});
            for (int globals = 0; globals < 16; ++globals) {
                GameVariable<bool>(0x007CE46C) = (globals & 1) != 0;
                GameVariable<bool>(0x007CE46F) = (globals & 2) != 0;
                GameVariable<bool>(0x007CE46E) = (globals & 4) != 0;
                GameVariable<bool>(0x007D6A2C) = (globals & 8) != 0;
                for (int button = -1; button <= 31; ++button) {
                    std::memset(reinterpret_cast<void*>(0x007D19D8), 0x5A, 0x200);
                    const char* name = original ? Original<const char*(__cdecl*)(int)>(0x005BCD50)(button)
                                                : Sio::GetButtonKeyName(button);
                    trace.push_back(reinterpret_cast<uint32_t>(name));
                    trace.insert(trace.end(), reinterpret_cast<const uint32_t*>(0x007D19D8),
                                 reinterpret_cast<const uint32_t*>(0x007D19D8) + 12);
                    trace.insert(trace.end(), reinterpret_cast<const uint32_t*>(0x007D1AD8),
                                 reinterpret_cast<const uint32_t*>(0x007D1AD8) + 8);
                    trace.push_back(reinterpret_cast<const uint8_t*>(0x007D1BD7)[0]);
                }
            }
            trace.push_back(standIn.Pointers());
        }
    }
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    Check(traces[0] == traces[1], "sio (late): key names");
    if (traces[0].size() != traces[1].size())
        Log("  key name trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i) {
        if (traces[0][i] != traces[1][i]) {
            Log("  key name entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    }
}

// A jump written over a game function's start for the length of a test (restored when it goes).

// The platform layer, scripted: the pad state, the message pump and the key check (calls recorded).
Sio::PadState g_pad;
bool g_pad_key_down;
std::vector<uint32_t> g_pad_log;
void __cdecl StandInReadPad(int port, Sio::PadState* state)
{
    g_pad_log.push_back(0x9AD00u + static_cast<uint32_t>(port));
    *state = g_pad;
}
void __cdecl StandInPump() { g_pad_log.push_back(0x9E5); }
bool __cdecl StandInKeyDown(int code)
{
    g_pad_log.push_back(0xD0D00u + static_cast<uint32_t>(code));
    return g_pad_key_down;
}

// The original pad read (not run with PartyMod), over the menus it checks (stand-in screen elements), the
// flags, ports, levels and scripted pad states - original vs ours.
void TestSioReadData()
{
    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {{0x00701440, 4, {}}, {0x006F0FC0, 4, {}}, {0x007CE46C, 4, {}}, {0x007CCDF8, 1, {}},
                                {0x007D6A2C, 1, {}}, {0x00696D95, 1, {}}, {0x006F1158, 1, {}}, {0x007CE5D8, 4, {}},
                                {0x007CE4B4, 4, {}}, {0x007D6A3C, 8, {}}, {0x0068E244, 4, {}}, {0x007D6318, 0x10, {}},
                                {0x007D6770, 0x260, {}}, {0x007D1BE0, 0x40, {}}, {0x007D1E28, 4, {}}};
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    TestDetour readPad(0x005F5BC0, reinterpret_cast<void*>(&StandInReadPad));
    TestDetour pump(0x005F56E0, reinterpret_cast<void*>(&StandInPump));
    TestDetour keyDown(0x005F30E0, reinterpret_cast<void*>(&StandInKeyDown));
    for (int i = 0; i < 4; ++i)
        GameVariable<void*>(0x007D6318 + 4 * i) = nullptr; // no joysticks: present through key bindings
    static StandInElements standIn;
    alignas(16) static uint8_t frontEnd[0x20]; // the object whose +0x1C is the front end's port
    alignas(16) static uint8_t levels[2][0x48];
    std::memset(levels, 0, sizeof(levels));
    reinterpret_cast<uint32_t*>(levels[0])[0x40 / 4] = 0x7ECA21E5; // the park editor
    reinterpret_cast<uint32_t*>(levels[1])[0x40 / 4] = 0x12345678;
    uint32_t select = Crc::GenerateCRCFromString("select_skater_hmenu");
    uint32_t piece = Crc::GenerateCRCFromString("piece_menu_hmenu");
    const uint32_t editor = 0x31631B98;

    struct Pad {
        uint16_t buttons;
        uint8_t p[8];
        int16_t sticks[4];
    };
    std::vector<Pad> pads = {
        {0x0000, {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}},
        {0xFFFF, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, {32767, -32768, -1, 1}},
        {0x0010, {0x1F, 0x20, 0x21, 0x80, 0x81, 0x20, 0x81, 0x21}, {255, -255, 256, -256}},
        {0x00E5, {0x20, 0x1F, 0x20, 0x1F, 0x21, 0x21, 0x80, 0x80}, {-32767, 32766, 127, -128}},
        {0x0102, {0x21, 0x00, 0x22, 0x20, 0x00, 0x00, 0x21, 0x81}, {1000, -1000, 0, 32767}},
        {0x000C, {0x00, 0x30, 0x00, 0x30, 0x00, 0x00, 0x20, 0x20}, {-200, 200, -32768, 0}},
        {0x0020, {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}}, // (only the first byte has a button down)
    };

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        std::vector<uint32_t>& trace = traces[run];
        alignas(16) static uint8_t device[0x100];
        int variant = 0;
        auto read = [&](int port, bool keyboardControl) {
            ++variant;
            for (int i = 0; i < 0x100; ++i)
                device[i] = static_cast<uint8_t>(i * 3 + variant);
            auto* d = reinterpret_cast<Sio::Device*>(device);
            d->m_port = port;
            d->m_keyboardControl = keyboardControl;
            g_pad_log.clear();
            GameVariable<int>(0x007D6A3C) = 0x55;
            GameVariable<int>(0x007D6A40) = 0x55;
            GameVariable<int>(0x0068E244) = 0x55;
            if (original)
                Original<void(__thiscall*)(void*, int)>(0x005BD200)(device, 7);
            else
                d->ReadData(7);
            trace.insert(trace.end(), reinterpret_cast<uint32_t*>(device), reinterpret_cast<uint32_t*>(device + 0x100));
            trace.push_back(GameVariable<uint32_t>(0x007D6A3C));
            trace.push_back(GameVariable<uint32_t>(0x007D6A40));
            trace.push_back(GameVariable<uint32_t>(0x0068E244));
            trace.insert(trace.end(), g_pad_log.begin(), g_pad_log.end());
            trace.push_back(standIn.Pointers());
        };
        auto setPad = [&](const Pad& pad) {
            std::memset(&g_pad, 0x6B, sizeof(g_pad));
            g_pad.buttons = pad.buttons;
            std::memcpy(g_pad.pressure, pad.p, 8);
            std::memcpy(g_pad.sticks, pad.sticks, 8);
        };
        auto setBindings = [](bool any) {
            for (int port = 0; port < 2; ++port) { // (two ports' tables: beyond them are other globals)
                auto* table = reinterpret_cast<int*>(0x007D6770 + port * 0x130);
                for (int i = 0; i < 0x4C; ++i)
                    table[i] = -1;
                if (any)
                    table[5] = 0x1C;
            }
        };
        auto setChars = [](bool enter) {
            GameVariable<uint32_t>(0x007D1E28) = 3;
            GameVariable<int>(0x007D1BE0) = 'a';
            GameVariable<int>(0x007D1BE4) = enter ? 0x19 : 'b';
            GameVariable<int>(0x007D1BE8) = 'c';
        };

        // The menus, flags and ports: who reads and in which mode.
        struct Scene {
            int select, piece; // 0 absent, 1 shown, 2 hidden
        };
        std::vector<Scene> scenes = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2}, {1, 1}, {2, 1}, {1, 2}};
        for (const Scene& scene : scenes) {
            standIn.Set({{select, scene.select != 0, scene.select == 2 ? 1 : 0},
                         {piece, scene.piece != 0, scene.piece == 2 ? 1 : 0},
                         {editor, (variant & 1) != 0, 0}});
            for (int flags : {0, 1, 2, 4, 5, 6, 8, 9, 16, 17, 32, 33, 64}) {
                GameVariable<bool>(0x007CE46C) = (flags & 1) != 0;
                GameVariable<bool>(0x007CE46F) = (flags & 2) != 0;
                GameVariable<bool>(0x007CE46E) = (flags & 4) != 0;
                GameVariable<bool>(0x007D6A2C) = (flags & 8) != 0;
                GameVariable<bool>(0x00696D95) = (flags & 16) != 0;
                GameVariable<bool>(0x006F1158) = (flags & 32) != 0;
                for (int port = 0; port < 3; ++port) {
                    for (int menuPort = 0; menuPort < 2; ++menuPort) {
                        reinterpret_cast<int*>(frontEnd)[0x1C / 4] = menuPort;
                        GameVariable<void*>(0x007CE4B4) = frontEnd;
                        GameVariable<void*>(0x007CE5D8) = (variant % 3) == 2 ? nullptr : levels[variant % 3];
                        setBindings((variant % 5) != 0);
                        setChars((variant % 4) == 0);
                        setPad(pads[static_cast<size_t>(variant) % pads.size()]);
                        g_pad_key_down = (variant % 3) == 0;
                        read(port, (variant % 7) < 3);
                    }
                }
            }
        }
        // The bits: every pad state over the levels (and the park editor's element or not), the keyboard
        // controlling the menus or not, typed Enter or not, Enter held or not.
        GameVariable<bool>(0x007CE46C) = false;
        GameVariable<bool>(0x007CE46F) = false;
        GameVariable<bool>(0x007D6A2C) = false;
        GameVariable<bool>(0x00696D95) = false;
        GameVariable<bool>(0x006F1158) = false;
        setBindings(true);
        for (const Pad& pad : pads) {
            setPad(pad);
            for (int level = 0; level < 4; ++level) {
                standIn.Set({{editor, level == 3, 0}});
                GameVariable<void*>(0x007CE5D8) = level == 2 ? nullptr : levels[level == 3 ? 0 : level];
                for (int menuState = 0; menuState < 3; ++menuState) { // playing, in a menu, typing
                    GameVariable<bool>(0x007CE46F) = menuState == 1;
                    GameVariable<bool>(0x007CE46E) = menuState == 2;
                    for (int keys = 0; keys < 4; ++keys) {
                        setChars((keys & 1) != 0);
                        g_pad_key_down = (keys & 2) != 0;
                        read(0, keys == 3 || keys == 0);
                    }
                }
            }
        }
        // No element manager: the park editor's element isn't looked up.
        GameVariable<void*>(0x00701440) = nullptr;
        GameVariable<void*>(0x007CE5D8) = levels[0];
        read(0, false);
    }
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    Check(traces[0] == traces[1], "sio: the original pad read");
    if (traces[0].size() != traces[1].size())
        Log("  pad read trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i) {
        if (traces[0][i] != traces[1][i]) {
            Log("  pad read entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    }
}

// The stand-in idle function for WaitForVblank: counts a vblank on its third call.
bool g_timer_original;
int g_timer_idle_calls;
void __cdecl TimerIdle()
{
    if (++g_timer_idle_calls % 3 == 0) {
        if (g_timer_original)
            Original<void(__cdecl*)()>(0x005BE700)();
        else
            Tmr::CountVblank();
    }
}

// The game clock: reset (first time and later), init (all but the C runtime clock and the time stamp
// counter), vblanks and frames over stalls and wraps, the frame length, slow motion, milliseconds at large
// counts and other rates, waiting for a vblank (with a stand-in idle function), the flags - original vs ours.
void TestTimer()
{
    struct Saved {
        uint32_t address;
        uint32_t size;
        std::vector<uint8_t> bytes;
    };
    std::vector<Saved> saved = {{0x007D1E30, 0xC0, {}}, {0x00684FB4, 0x20, {}}, {0x006FA7D8, 4, {}}};
    for (Saved& s : saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_timer_original = original;
        std::vector<uint32_t>& trace = traces[run];
        auto call = [&](uint32_t address, void (*ours)()) {
            if (original)
                Original<void(__cdecl*)()>(address)();
            else
                ours();
        };
        auto dump = [&] {
            trace.insert(trace.end(), reinterpret_cast<uint32_t*>(0x007D1E30), reinterpret_cast<uint32_t*>(0x007D1EA0));
            trace.insert(trace.end(), reinterpret_cast<uint32_t*>(0x007D1EB8), reinterpret_cast<uint32_t*>(0x007D1EEC));
            trace.insert(trace.end(), reinterpret_cast<uint32_t*>(0x00684FB4), reinterpret_cast<uint32_t*>(0x00684FD4));
        };
        auto pushDouble = [&](double d) {
            uint64_t bits;
            std::memcpy(&bits, &d, 8);
            trace.push_back(static_cast<uint32_t>(bits));
            trace.push_back(static_cast<uint32_t>(bits >> 32));
        };
        auto pushFloat = [&](float f) {
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            trace.push_back(bits);
        };
        auto queries = [&] {
            uint64_t v = original ? Original<uint64_t(__cdecl*)()>(0x005BE8E0)() : Tmr::GetVblanks();
            uint64_t r = original ? Original<uint64_t(__cdecl*)()>(0x005BE8D0)() : Tmr::GetRenderFrame();
            trace.insert(trace.end(), {static_cast<uint32_t>(v), static_cast<uint32_t>(v >> 32), static_cast<uint32_t>(r),
                                       static_cast<uint32_t>(r >> 32)});
            trace.push_back(original ? Original<uint32_t(__cdecl*)()>(0x005BE970)() : Tmr::GetTime());
            pushDouble(original ? Original<double(__cdecl*)()>(0x005BE910)() : Tmr::GetFrameSeconds());
            pushDouble(original ? Original<double(__cdecl*)()>(0x005BEAA0)() : Tmr::GetSlomoFrameSeconds());
            pushFloat(original ? Original<float(__cdecl*)()>(0x005BE8F0)() : Tmr::GetSlomo());
        };

        std::memset(reinterpret_cast<void*>(0x007D1E30), 0x3C, 0xC0);
        GameVariable<float>(0x00684FC8) = 1.0f;
        GameVariable<float>(0x00684FCC) = 1.0f;
        GameVariable<bool>(0x007D1EEA) = false;
        GameVariable<int>(0x00684FB4) = 50;
        call(0x005BE9B0, Tmr::Reset);
        dump();
        GameVariable<int>(0x00684FB4) = 60; // (already set: kept)
        call(0x005BE9B0, Tmr::Reset);
        dump();
        call(0x005BEA50, Tmr::Init);
        dump();
        queries();

        // Frames of various lengths: 0 to 13 vblanks, then the counters near their 32-bit limits.
        int lengths[] = {1, 2, 0, 3, 4, 5, 10, 11, 1, 13, 2, 7, 0, 0, 1, 6};
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1) {
                GameVariable<uint64_t>(0x007D1EC8) = 0xFFFFFFFDull;
                GameVariable<uint64_t>(0x007D1ED0) = 0x1FFFFFFFEull;
                GameVariable<uint64_t>(0x007D1ED8) = 0xFFFFFFFFull;
                GameVariable<uint64_t>(0x007D1E88) = 0xFFFFFFF0ull; // (a frame "longer" than 10)
            }
            for (int length : lengths) {
                for (int v = 0; v < length; ++v)
                    call(0x005BE700, Tmr::CountVblank);
                call(0x005BE740, Tmr::OncePerRender);
                if (length == 5) {
                    if (original)
                        Original<void(__cdecl*)(float)>(0x005BE900)(0.25f);
                    else
                        Tmr::SetSlomo(0.25f);
                }
                if (length == 13) {
                    if (original)
                        Original<void(__cdecl*)(float)>(0x005BE900)(3.0f);
                    else
                        Tmr::SetSlomo(3.0f);
                }
                queries(); // (slow motion only changes at the next update)
                call(0x005BE7F0, Tmr::UpdateFrameLength);
                dump();
                queries();
            }
        }
        // Milliseconds at other rates and large counts (the low 32 bits only).
        for (float rate : {60.0f, 50.0f, 59.94f}) {
            GameVariable<float>(0x00684FD0) = rate;
            for (uint64_t vblanks : {0ull, 1ull, 3599ull, 0x7FFFFFFFull, 0x80000000ull, 0xFFFFFFFFull, 0x100000005ull}) {
                GameVariable<uint64_t>(0x007D1EC8) = vblanks;
                trace.push_back(original ? Original<uint32_t(__cdecl*)()>(0x005BE970)() : Tmr::GetTime());
            }
        }
        // History outside the usual range (negative, huge): the frame length's clamping.
        for (int a : {-1, 0, 4, 10, 11, 0x7FFFFFFF}) {
            int* history = reinterpret_cast<int*>(0x007D1E78);
            history[0] = a;
            history[1] = 0x7FFFFFFF;
            for (int rest : {0, 4}) { // (4: large enough not to be lifted to the minimum of 1)
                history[2] = rest;
                history[3] = rest;
                call(0x005BE7F0, Tmr::UpdateFrameLength);
                dump();
            }
        }
        // Waiting for a vblank: the idle function runs until one passes; none set: just the check.
        g_timer_idle_calls = 0;
        GameVariable<void*>(0x006FA7D8) = reinterpret_cast<void*>(&TimerIdle);
        call(0x005BE920, Tmr::WaitForVblank);
        trace.push_back(static_cast<uint32_t>(g_timer_idle_calls));
        dump();
        // The flags.
        for (int flags = 0; flags < 4; ++flags) {
            GameVariable<uint8_t>(0x007D1EE8) = static_cast<uint8_t>(flags & 1 ? 7 : 0);
            GameVariable<uint8_t>(0x007D1EE9) = static_cast<uint8_t>(flags & 2 ? 9 : 0);
            call(0x005BE7C0, Tmr::SetFlags);
            trace.push_back(GameVariable<uint16_t>(0x007D1EE8));
        }
    }
    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    Check(traces[0] == traces[1], "timer: vblanks, frames, frame length, slow motion, time");
    if (traces[0].size() != traces[1].size())
        Log("  timer trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i) {
        if (traces[0][i] != traces[1][i]) {
            Log("  timer entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    }
    // The time stamp counter only moves forward.
    uint64_t a = Original<uint64_t(__cdecl*)()>(0x005BE6F0)();
    uint64_t b = Tmr::ReadCpuTimeStamp();
    uint64_t c = Original<uint64_t(__cdecl*)()>(0x005BE6F0)();
    Check(a <= b && b <= c && c > a, "timer: time stamp counter");
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
SELFTEST(Early, TestAsyncFileSystem, RestoreAsyncGlobals);
SELFTEST(Early, TestPcAsyncFileHandle, RestoreAsyncIoState);
SELFTEST(Early, TestAsyncLoaderModuleRefs, RestoreLoaderModule);
SELFTEST(Early, TestAssetBundleLookups);
SELFTEST(Early, TestAllocatorLayer);
SELFTEST(Early, TestHeap, RestoreMemoryManager);
SELFTEST(Early, TestMemoryManager, RestoreManagerForTest);
SELFTEST(Early, TestMemoryManagerHeapCreation, RestoreManagerInitTest);
SELFTEST(Early, TestScriptStructLookups, RestoreSymbolTable);
SELFTEST(Early, TestScriptStructWriting, RestoreScriptWriteTest);
SELFTEST(Early, TestScriptStructAdding, RestoreScriptAddingTest);
SELFTEST(Early, TestScriptArrays, RestoreScriptAddingTest);
SELFTEST(Early, TestScriptSymbols, RestoreScriptAddingTest);
SELFTEST(Early, TestScriptExpressions, RestoreScriptAddingTest);
SELFTEST(Early, TestScriptChecksumNames, RestoreScriptAddingTest);
SELFTEST(Late, TestScriptChecksumNamesLate);
SELFTEST(Early, TestScriptTokens);
SELFTEST(Early, TestScriptSymbolBookkeeping);
SELFTEST(Early, TestScriptParser);
SELFTEST(Early, TestScriptLoading);
SELFTEST(Early, TestMemoryCards);
SELFTEST(Early, TestLeftovers);
SELFTEST(Early, TestSio);
SELFTEST(Early, TestSioManager);
SELFTEST(Early, TestSioKeyNames);
SELFTEST(Early, TestSioReadData);
SELFTEST(Early, TestTimer);

SELFTEST(Late, TestMemoryLate);
SELFTEST(Late, TestStringLate);
SELFTEST(Late, TestLzssLate);
SELFTEST(Late, TestAssetBundleSplitLate);
SELFTEST(Late, TestAllocatorContextsLate);
SELFTEST(Late, TestLeftoversLate);
SELFTEST(Late, TestSioLate);
SELFTEST(Late, TestSioManagerLate);
SELFTEST(Late, TestSioKeyNamesLate);
SELFTEST(Late, TestPoolConstructorLate);
SELFTEST(Late, TestHashTableLate);
SELFTEST(Late, TestPipNamesLate, RestorePipTables);
SELFTEST(Late, TestPreFileLate);
SELFTEST(Late, TestPipLoadPreLate);
SELFTEST(Late, TestPreMgrLate, RestorePreGlobals);

SELFTEST(EngineStarted, TestPipLoad);
SELFTEST(EngineStarted, TestLoadPreEngineStarted, RestorePreGlobals);
