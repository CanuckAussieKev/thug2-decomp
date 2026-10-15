#include "hook/selftest.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include <initializer_list>
#include <new>

#include "game/core/crc.h"
#include "game/core/str.h"
#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/util/bitstream.h"
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
               a.m_bitsFree == b.m_bitsFree && a.m_size == b.m_size && a.m_bitsUsed == b.m_bitsUsed;
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

    for (int free_bits : {32, 20}) {
        uint32_t out_expected[2] = {}, out_actual[2] = {};
        expected.SetOutputData(out_expected, 8);
        actual.SetOutputData(out_actual, 8);
        expected.m_buffer = actual.m_buffer = 0x12345678;
        expected.m_bitsFree = actual.m_bitsFree = free_bits;
        Original<Flush>(0x00401180)(&expected);
        actual.Flush();
        Check(expected.m_cursor - out_expected == actual.m_cursor - out_actual && out_expected[0] == out_actual[0] &&
                  expected.m_buffer == actual.m_buffer && expected.m_bitsFree == actual.m_bitsFree,
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

} // namespace

void RunSelfTests()
{
    TestCrc();
    TestVector();
    TestMatrix();
    TestBitStream();
    TestStringsAndMath();
    Log(g_failures ? "self-tests: %d FAILED" : "self-tests: all passed", g_failures);
}
