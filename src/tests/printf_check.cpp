// NOT ORIGINAL - thug2_printf_check.exe: text::format (engine/core/format.h) compared with the linked C runtime's
// snprintf over every conversion the game uses, each flag, width, precision and length modifier, and the edge values
// (limits, zero, -0, halves at every precision, huge and tiny floats, infinities, NaNs, null texts). Prints each
// difference (at most 40) and the totals; exit code 1 if any.
#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/checked_index.h"
#include "engine/core/format.h"

namespace {

constexpr size_t kBufferSize = 2048;
constexpr int kMaxShown = 40;

struct Totals {
    long long compared = 0;
    long long differ = 0;
};

Totals& totals()
{
    static Totals counts;
    return counts;
}

// The C runtime's text: the reference this check compares with (so the one C vararg call this program makes).
template <typename... Args>
std::string reference(const std::string& format, Args... args)
{
    std::array<char, kBufferSize> buffer{};
    const int length = std::snprintf(buffer.data(), buffer.size(), format.c_str(), args...);
    if (length < 0) {
        return "<error>";
    }
    return {buffer.data(), static_cast<size_t>(length)};
}

template <typename... Args>
std::string ours(const std::string& format, Args... args)
{
    const std::array<text::FormatArg, sizeof...(Args)> packed{text::format_detail::make_arg(args)...};
    std::array<char, kBufferSize> buffer{};
    const size_t length = text::format_runtime_to(buffer, format, packed);
    std::string made(buffer.data(), std::min(length, buffer.size() - 1));
    // the runtime's own form (format_runtime) and a cut-short buffer must agree with it too
    if (text::format_runtime(format, packed) != made) {
        return "<format_runtime differs>";
    }
    std::array<char, 4> small{};
    const size_t small_length = text::format_runtime_to(small, format, packed);
    const size_t kept = std::min(length, small.size() - 1);
    if (small_length != length || std::string_view(small.data(), kept) != std::string_view(made).substr(0, kept) ||
        core::at(small, kept) != '\0') {
        return "<cut text differs>";
    }
    return made;
}

template <typename... Args>
void compare(const std::string& format, Args... args)
{
    ++totals().compared;
    const std::string expected = reference(format, args...);
    const std::string made = ours(format, args...);
    if (expected == made) {
        return;
    }
    if (++totals().differ <= kMaxShown) {
        std::string shown_expected;
        std::string shown_made;
        for (const char character : expected) {
            shown_expected += character == '\0' ? '@' : character;
        }
        for (const char character : made) {
            shown_made += character == '\0' ? '@' : character;
        }
        std::fputs(text::format("DIFF \"%s\": runtime [%s] ours [%s]\n", format.c_str(), shown_expected.c_str(),
                                shown_made.c_str())
                       .c_str(),
                   stdout);
    }
}

constexpr auto kFlags =
    std::to_array<std::string_view>({"", "-", "+", " ", "#", "0", "-0", "+0", " 0", "#0", "-#", "+ ", "-+#0"});
constexpr auto kWidths = std::to_array<std::string_view>({"", "1", "5", "12", "*"});
constexpr auto kPrecisions = std::to_array<std::string_view>({"", ".", ".0", ".1", ".2", ".3", ".5", ".12", ".*"});
constexpr auto kStarWidths = std::to_array<int>({7, -9});
constexpr auto kStarPrecisions = std::to_array<int>({4, -2});

// `format` with the '*' arguments it wants (a width of "*", a precision of ".*"), then `value`.
template <typename Value>
void compare_with_stars(const std::string& format, bool star_width, bool star_precision, Value value)
{
    if (star_width && star_precision) {
        for (const int w : kStarWidths) {
            for (const int precision : kStarPrecisions) {
                compare(format, w, precision, value);
            }
        }
    } else if (star_width) {
        for (const int w : kStarWidths) {
            compare(format, w, value);
        }
    } else if (star_precision) {
        for (const int precision : kStarPrecisions) {
            compare(format, precision, value);
        }
    } else {
        compare(format, value);
    }
}

// A length modifier and the conversions it is checked with.
struct Conversions {
    std::string_view modifier;
    std::string_view letters;
};

// Every flag x width x precision of the modifier + each conversion over `values`.
template <typename Value>
void grid(Conversions conversions_of, const std::vector<Value>& values)
{
    const std::string_view modifier = conversions_of.modifier;
    const std::string_view conversions = conversions_of.letters;
    for (const char conversion : conversions) {
        for (const std::string_view flags : kFlags) {
            for (const std::string_view width : kWidths) {
                for (const std::string_view precision : kPrecisions) {
                    std::string format = "<%";
                    format += flags;
                    format += width;
                    format += precision;
                    format += modifier;
                    format += conversion;
                    format += ">";
                    for (const Value& value : values) {
                        compare_with_stars(format, width == "*", precision == ".*", value);
                    }
                }
            }
        }
    }
}

void check_integers()
{
    const std::vector<int> ints = {0, 1, -1, 7, -42, 255, 0x80, 65535, 123456789, INT_MIN, INT_MAX, -32768, 'A'};
    const std::vector<unsigned> unsigneds = {0U, 1U, 0xFFFFFFFFU, 0x80000000U, 0xDEADBEEFU, 4096U};
    grid({.modifier = "", .letters = "diouxXc"}, ints);
    grid({.modifier = "", .letters = "diouxX"}, unsigneds);
    grid({.modifier = "h", .letters = "diouxX"}, ints);
    grid({.modifier = "hh", .letters = "diouxX"}, ints);
    grid({.modifier = "l", .letters = "diouxX"}, ints);
    grid({.modifier = "z", .letters = "diouxX"}, unsigneds);
    const std::vector<long long> wides = {0LL, 1LL, -1LL, LLONG_MIN, LLONG_MAX, 0x123456789ALL, -9876543210LL};
    grid({.modifier = "ll", .letters = "diouxX"}, wides);
    grid({.modifier = "I64", .letters = "diouxX"}, wides);
    const std::vector<unsigned long long> unsigned_wides = {0ULL, ULLONG_MAX, 0x8000000000000000ULL};
    grid({.modifier = "ll", .letters = "diouxX"}, unsigned_wides);
}

// The values the floating-point formats are checked with: ties, tiny and huge numbers, the specials.
constexpr auto kFloatCases = std::to_array<double>({0.0,
                                                    -0.0,
                                                    1.0,
                                                    -1.0,
                                                    0.5,
                                                    1.5,
                                                    2.5,
                                                    -2.5,
                                                    0.125,
                                                    0.375,
                                                    0.25,
                                                    0.05,
                                                    1e-5,
                                                    0.0001,
                                                    0.00001234,
                                                    123.456,
                                                    999999.5,
                                                    1e15,
                                                    1e16,
                                                    1e17,
                                                    123456789012345678.0,
                                                    9.9999995,
                                                    0.1,
                                                    1.0 / 3.0,
                                                    2.0 / 3.0,
                                                    2.675,
                                                    DBL_MAX,
                                                    DBL_MIN,
                                                    std::numeric_limits<double>::denorm_min(),
                                                    1e300,
                                                    -1e-300,
                                                    static_cast<double>(3.14159265F),
                                                    static_cast<double>(0.1F),
                                                    std::numeric_limits<double>::infinity(),
                                                    -std::numeric_limits<double>::infinity(),
                                                    std::numeric_limits<double>::quiet_NaN(),
                                                    -std::numeric_limits<double>::quiet_NaN()});

// The values of the game's own formats (checked with compare below).
constexpr unsigned long long kIndefiniteNan = 0xFFF8000000000000ULL;
constexpr unsigned long long kSignallingNan = 0x7FF0000000000001ULL;
constexpr unsigned long long kPayloadNan = 0x7FF8000000000123ULL;
constexpr int kMaxTiePlaces = 6;
constexpr int kTieCount = 20;
constexpr double kHalf = 0.5;
constexpr int kBinaryTieCount = 64;
constexpr int kPercentArg = 5;
constexpr double kThreeDecimalsB = 2.5;
constexpr double kThreeDecimalsC = 0.125;
constexpr unsigned kHexWordArg = 0xBEEFU;
constexpr double kPiShort = 3.14159;
constexpr int kPlusFlagArg = 7;
constexpr int kSpaceFlagArg = 8;
constexpr int kOctalAlternateArg = 8;
constexpr int kHexAlternateArg = 255;
constexpr int kHexAlternateUpperArg = 255;
constexpr int kStarWidth = 6;
constexpr int kStarWidthArg = 42;
constexpr double kRoundHalf = 0.5;
constexpr double kRoundOneAndHalf = 1.5;
constexpr double kRoundTwoAndHalf = 2.5;
constexpr double kRoundThreeAndHalf = 3.5;
constexpr std::ptrdiff_t kPtrdiffArg = 5;
constexpr size_t kSizeArg = 7;
constexpr unsigned long kUnsignedLongArg = 9UL;
constexpr long kLongArg = 9L;
constexpr double kPlacesValueA = 1.25;
constexpr double kPlacesValueB = 0.5;
constexpr double kPlacesValueC = 1234.5678;
constexpr double kPlacesValueD = 0.0625;
constexpr double kPlacesValueE = 2.0;
constexpr double kPlacesValueF = 1e-7;
constexpr int kTrickPoints = 540;
constexpr unsigned kTotalKilobytes = 4194303U;
constexpr unsigned kFreeKilobytes = 12U;
constexpr unsigned kClockMinutes = 7U;
constexpr unsigned kMaxUnsigned = 4294967295U;
constexpr double kRoundTwoPlaces = 0.005;
constexpr double kRoundThreePlaces = 1.0005;
constexpr unsigned kShortHexArg = 0x1FU;
constexpr unsigned kWordHexArg = 0xDEADBEEFU;
std::vector<double> float_values()
{
    // (the signalling NaN must reach the formats untouched: it is built in a list of its own and goes first)
    std::vector<double> values = {
        std::bit_cast<double>(kIndefiniteNan), // the "indefinite" NaN
        std::bit_cast<double>(kSignallingNan), // a signalling NaN
        std::bit_cast<double>(kPayloadNan)};
    values.insert(values.end(), kFloatCases.begin(), kFloatCases.end());
    // halves at each precision (exact ties and near ties)
    for (int places = 0; places <= kMaxTiePlaces; ++places) {
        const double step = std::pow(10.0, -places);
        for (int k = 0; k < kTieCount; ++k) {
            values.push_back((k + kHalf) * step);
        }
    }
    for (int k = 0; k < kBinaryTieCount; ++k) {
        values.push_back(std::ldexp(static_cast<double>((2 * k) + 1), -k / 4)); // binary fractions: exact ties
    }
    return values;
}

void check_floats()
{
    const std::vector<double> values = float_values();
    grid({.modifier = "", .letters = "fFeEgG"}, values);
    grid({.modifier = "L", .letters = "fg"}, values);
    // a sweep of magnitudes (10^-30 .. 10^30) and precisions (0 .. 17)
    constexpr int kSteps = 200000;
    constexpr double kLowestPower = -30.0;
    constexpr double kPowers = 60.0;
    constexpr int kPlaces = 18;
    for (int i = 0; i < kSteps; ++i) {
        const double power = kLowestPower + (kPowers * static_cast<double>(i) / kSteps);
        const double value = std::pow(10.0, power) * ((i & 1) != 0 ? -1.0 : 1.0);
        const int precision = (i * 7) % kPlaces;
        compare("%.*f", precision, value);
        compare("%.*e", precision, value);
        compare("%.*g", precision, value);
        compare("%.*f", precision, static_cast<double>(static_cast<float>(value)));
    }
}

void check_texts()
{
    const std::vector<const char*> texts = {"", "a", "abc", "hello world", "0123456789abcdef", nullptr};
    grid({.modifier = "", .letters = "s"}, texts);
    const std::vector<const wchar_t*> wides = {L"", L"abc", L"wide text", nullptr};
    grid({.modifier = "l", .letters = "s"}, wides);
    std::array<int, 4> object{};
    const std::vector<const void*> pointers = {nullptr, object.data(), &object.back(), &totals()};
    grid({.modifier = "", .letters = "p"}, pointers);
    // the game's own forms
    compare("100%%");
    compare("%%d %d %%", kPercentArg);
    compare("%s=%d (%.3f,%.3f,%.3f) %08X %p", "name", -3, 1.0, kThreeDecimalsB, -kThreeDecimalsC, kHexWordArg, static_cast<const void*>(&object));
    compare("%-20s|%5.1f|%+d|% i|%#o|%#x|%#X", "left", kPiShort, kPlusFlagArg, kSpaceFlagArg, kOctalAlternateArg, kHexAlternateArg, kHexAlternateUpperArg);
    compare("%0*d", kStarWidth, kStarWidthArg);
    compare("%.0f %.0f %.0f %.0f", kRoundHalf, kRoundOneAndHalf, kRoundTwoAndHalf, kRoundThreeAndHalf);
    compare("%c%c%c", 'a', 0, 'b');
    compare("%td %zu %lu %ld %li %lld %llu", (-kPtrdiffArg), size_t{kSizeArg}, kUnsignedLongArg, -kLongArg, 3L, -1LL,
            2ULL);
    compare("%ls", L"wide");
    // round 4b's forms: places chosen by an argument, colour / join pieces as texts, wsprintfA's integers
    for (const int places : {-2, 0, 1, 2, 3, 6}) {
        compare("(%.*f, %.*f, %.*f)|%.*f,%.*f,%.*f", places, kPlacesValueA, places, -kPlacesValueB, places, kPlacesValueC, places, kPlacesValueD,
                places, -kPlacesValueE, places, kPlacesValueF);
    }
    compare("%s%s\\_%d\\_%s%s%s", "\\c3", "BS", kTrickPoints, "Kickflip", "\\c0", "\\_+ ");
    compare("%s%s%s ", "", "Ollie", "");
    compare("Collision %s (%d piece%s)", "off", 1, "");
    compare("\\c%d%s%s", 2, "> ", "name");
    compare("%4d total kb of physical memory.\n%4d  free kb of physical memory.\n", kTotalKilobytes, kFreeKilobytes);
    compare("%.2d:%.2d", 0U, kClockMinutes);
    compare("%u %.2f %.3f %d 0x%04X 0x%08X\n", kMaxUnsigned, -kRoundTwoPlaces, kRoundThreePlaces, -1, kShortHexArg, kWordHexArg);
}

} // namespace

int main()
{
    check_integers();
    check_floats();
    check_texts();
    std::fputs(text::format("printf check: %lld compared, %lld differ\n", totals().compared, totals().differ).c_str(),
               stdout);
    return totals().differ == 0 ? 0 : 1;
}
