#include "hook/selftest.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
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

} // namespace

void RunSelfTests()
{
    TestCrc();
    TestVector();
    TestMatrix();
    Log(g_failures ? "self-tests: %d FAILED" : "self-tests: all passed", g_failures);
}
