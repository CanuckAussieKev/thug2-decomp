// NOT ORIGINAL - thug2_parse_check.exe: text::to_int / text::to_double (engine/core/parse_number.h) against the C
// runtime's atoi / atol / atof over hand-picked texts and many random ones. Exit code 0 when every answer agrees.

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "engine/core/parse_number.h"

namespace {

int g_failures = 0;
int g_checked = 0;

bool same_double(double a, double b)
{
    if (std::isnan(a) || std::isnan(b))
        return std::isnan(a) && std::isnan(b);
    return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b); // (the bits: -0.0 is not 0.0)
}

void check(const std::string& text)
{
    ++g_checked;
    // NOLINTBEGIN(bugprone-unchecked-string-to-number-conversion): the C runtime's own functions, what the helpers
    // are checked against
    const int want_int = std::atoi(text.c_str());
    const long want_long = std::atol(text.c_str());
    const double want_double = std::atof(text.c_str());
    // NOLINTEND(bugprone-unchecked-string-to-number-conversion)
    const int got_int = text::to_int(text.c_str());
    const double got_double = text::to_double(text.c_str());
    if (got_int != want_int || static_cast<long>(got_int) != want_long) {
        if (++g_failures <= 20)
            std::printf("to_int(\"%s\") = %d, atoi %d, atol %ld\n", text.c_str(), got_int, want_int, want_long);
    }
    if (!same_double(got_double, want_double)) {
        if (++g_failures <= 20)
            std::printf("to_double(\"%s\") = %.17g, atof %.17g\n", text.c_str(), got_double, want_double);
    }
}

} // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> fixed = {
        "", " ", "0", "-0", "+0", "1", "-1", "+1", "42", "  42", "\t\n\v\f\r 42", "42abc", "abc", "-", "+", "--1",
        "+-1", "-+1", " - 1", "2147483647", "2147483648", "-2147483648", "-2147483649", "99999999999999999999",
        "-99999999999999999999", "000000000000000000000000000012", "4294967296", "1.5", "-1.5", ".5", "5.", ".",
        "1e3", "1e", "1e+", "1e-3", "-1E-3", "1e309", "-1e309", "1e-330", "-1e-330", "1e-320", "123456789e-330",
        "0.0000000001e320", "inf", "-inf", "INF", "infinity", "Infinity", "-INFINITY", "infinit", "nan", "-nan",
        "NaN", "nan(123)", "nan(", "0x", "0x1", "0X1A", "0x1p4", "0x.8", "0x.", "0xg", "-0x10", "0x1p-1080",
        "0x1p1100", "1,5", "1 5", "\xC3\xA9", "12\x80", "0012", "  -007", "3.14159265358979323846264338327950288",
        "1.7976931348623157e308", "1.7976931348623159e308", "4.9406564584124654e-324", "2.4703282292062328e-324",
        "2.4703282292062327e-324", "+.e5", "0e0", "00.00e00"};
    for (const std::string& text : fixed)
        check(text);
    const int count = argc > 1 ? text::to_int(argv[1]) : 200000;
    // the same texts every run unless another seed is given
    const auto seed = static_cast<std::mt19937::result_type>(argc > 2 ? text::to_int(argv[2]) : 12345);
    std::mt19937 random(seed);
    const char alphabet[] = " \t+-0123456789012345678901234567890123456789..eEexXpPabcfinINFNAN";
    std::uniform_int_distribution<int> length(0, 24);
    std::uniform_int_distribution<int> pick(0, static_cast<int>(sizeof(alphabet)) - 2);
    for (int i = 0; i < count; ++i) {
        std::string text;
        const int n = length(random);
        for (int k = 0; k < n; ++k)
            text += alphabet[pick(random)];
        check(text);
    }
    std::printf("parse check: %d texts, %d differences\n", g_checked, g_failures);
    return g_failures == 0 ? 0 : 1;
}
