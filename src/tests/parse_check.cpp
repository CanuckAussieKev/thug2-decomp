// NOT ORIGINAL - thug2_parse_check.exe: text::to_int / text::to_double (base/core/parse_number.h) against the C
// runtime's atoi / atol / atof, and text::FieldReader (base/core/field_reader.h) against its sscanf_s with the formats
// the game reads, over hand-picked texts and many random ones. Exit code 0 when every answer agrees.

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <array>

#include "tests/no_error_dialogs.h"
#include "base/core/parse_number.h"
#include "base/core/checked_index.h"
#include "base/core/field_reader.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"

namespace {

// Test sizes: how many mismatches are printed, how many fields the float-field format reads, the destination buffers of
// the string-field format, the random texts' limits.
constexpr int kMaxReportedFailures = 20;
constexpr size_t kFloatFieldCount = 6;
constexpr size_t kLastFloatField = 5;
constexpr size_t kNameBufferA = 16;
constexpr size_t kNameBufferB = 24;
constexpr size_t kNameBufferC = 8;
constexpr int kMaxTextLength = 24;
constexpr int kMaxWordsPerText = 9;
constexpr int kMaxWordLength = 9;
constexpr int kMaxGap = 5;

// NOT ORIGINAL grouping - parse_check.cpp's state (were globals). Made on first use, never destroyed
// (parse_check_state()).
struct ParseCheckState {
    int failures_ = 0;
    int checked_ = 0;
};

ParseCheckState& parse_check_state()
{
    static core::NoDestroy<ParseCheckState> state;
    return state.get();
}

bool same_double(double a, double b)
{
    if (std::isnan(a) || std::isnan(b)) {
        return std::isnan(a) && std::isnan(b);
    }
    return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b); // (the bits: -0.0 is not 0.0)
}

void check(const std::string& text)
{
    ++parse_check_state().checked_;
    // (the C runtime's own functions: what the helpers are checked against)
    const int want_int = std::atoi(text.c_str());
    const long want_long = std::atol(text.c_str());
    const double want_double = std::atof(text.c_str());
    const int got_int = text::to_int(text.c_str());
    const double got_double = text::to_double(text.c_str());
    if (got_int != want_int || static_cast<long>(got_int) != want_long) {
        if (++parse_check_state().failures_ <= kMaxReportedFailures) {
            text::print("to_int(\"%s\") = %d, atoi %d, atol %ld\n", text.c_str(), got_int, want_int, want_long);
        }
    }
    if (!same_double(got_double, want_double)) {
        if (++parse_check_state().failures_ <= kMaxReportedFailures) {
            text::print("to_double(\"%s\") = %.17g, atof %.17g\n", text.c_str(), got_double, want_double);
        }
    }
}

constexpr int kUnset = -7; // (a field the reading leaves as it was)
constexpr float kUnsetFloat = -7.0F;

bool same_float(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }

void report_fields(const char* format, const std::string& text, int got, int want)
{
    if (++parse_check_state().failures_ <= kMaxReportedFailures) {
        text::print("FieldReader \"%s\" on \"%s\": %d fields, sscanf_s %d (or a field differs)\n", format, text.c_str(),
                    got, want);
    }
}

// The fields of `text` read by text::FieldReader and by sscanf_s with the formats the game uses.
void check_fields(const std::string& text)
{
    ++parse_check_state().checked_;
    const char* source = text.c_str();
    // (the C runtime's sscanf_s: the reference the reader is checked against)
    {
        int want = kUnset;
        int got = kUnset;
        const int want_count = sscanf_s(source, "%d", &want);
        text::FieldReader in(text);
        in.read(got);
        if (in.scanned() != want_count || got != want) {
            report_fields("%d", text, in.scanned(), want_count);
        }
    }
    {
        float want = kUnsetFloat;
        float got = kUnsetFloat;
        const int want_count = sscanf_s(source, "%f", &want);
        text::FieldReader in(text);
        in.read(got);
        if (in.scanned() != want_count || !same_float(got, want)) {
            report_fields("%f", text, in.scanned(), want_count);
        }
    }
    {
        std::array<int, 2> want_i{kUnset, kUnset};
        std::array<int, 2> got_i = want_i;
        std::array<float, kFloatFieldCount> want_f{};
        want_f.fill(kUnsetFloat);
        std::array<float, kFloatFieldCount> got_f = want_f;
        const int want_count = sscanf_s(source, "%d %f %f %f %f %f %d %f", &core::at(want_i, 0), &core::at(want_f, 0),
                                        &core::at(want_f, 1), &core::at(want_f, 2), &core::at(want_f, 3),
                                        &core::at(want_f, 4), &core::at(want_i, 1), &core::at(want_f, 5));
        text::FieldReader in(text);
        (void)(in.read(core::at(got_i, 0)) && in.read(core::at(got_f, 0)) && in.read(core::at(got_f, 1)) &&
               in.read(core::at(got_f, 2)) && in.read(core::at(got_f, 3)) && in.read(core::at(got_f, 4)) &&
               in.read(core::at(got_i, 1)) && in.read(core::at(got_f, kLastFloatField)));
        bool same = in.scanned() == want_count && got_i == want_i;
        for (size_t i = 0; i < got_f.size(); ++i) {
            same = same && same_float(core::at(got_f, i), core::at(want_f, i));
        }
        if (!same) {
            report_fields("%d %f %f %f %f %f %d %f", text, in.scanned(), want_count);
        }
    }
    {
        int want_number = kUnset;
        int want_visible = kUnset;
        float want_seconds = kUnsetFloat;
        int got_number = kUnset;
        int got_visible = kUnset;
        float got_seconds = kUnsetFloat;
        const int want_count = sscanf_s(source, "%d %f %d", &want_number, &want_seconds, &want_visible);
        text::FieldReader in(text);
        (void)(in.read(got_number) && in.read(got_seconds) && in.read(got_visible));
        if (in.scanned() != want_count || got_number != want_number || got_visible != want_visible ||
            !same_float(got_seconds, want_seconds)) {
            report_fields("%d %f %d", text, in.scanned(), want_count);
        }
    }
    {
        std::array<char, kNameBufferA> want_a{'?'};
        std::array<char, kNameBufferB> want_b{'?'};
        std::array<char, kNameBufferC> want_c{'?'};
        std::array<char, kNameBufferA> got_a = want_a;
        std::array<char, kNameBufferB> got_b = want_b;
        std::array<char, kNameBufferC> got_c = want_c;
        const int want_count =
            sscanf_s(source, "/%15s %23s %7s", want_a.data(), static_cast<unsigned>(want_a.size()), want_b.data(),
                     static_cast<unsigned>(want_b.size()), want_c.data(), static_cast<unsigned>(want_c.size()));
        text::FieldReader in(text);
        (void)(in.match('/') && in.read_word(got_a) && in.read_word(got_b) && in.read_word(got_c));
        // (the C texts: the debug runtime's sscanf_s fills the rest of each buffer)
        auto same_text = [](std::span<const char> a, std::span<const char> b) {
            return std::string_view(a.data()) == std::string_view(b.data());
        };
        if (in.scanned() != want_count || !same_text(got_a, want_a) || !same_text(got_b, want_b) ||
            !same_text(got_c, want_c)) {
            report_fields("/%15s %23s %7s", text, in.scanned(), want_count);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
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
    // (texts for the field reader: numbers cut short, inf / nan spellings, float ties, words after a '/')
    const std::vector<std::string> fixed_fields = {
        "1e+x 5", "infx", "infinit 3", "nan(abc) 3", "nan(a b) 3", "nan()", "0x 3", "0x1p", "0x1p3", "-.", "+.5",
        "1.5e3x", "1e40", "1e-50", "0x1.8", "in", "n", "1.", "1.e2", "0x.8p1", "3.4028235e38", "3.4028236e38",
        "1.401298464e-45", "7e-46", "7.1e-46", "16777217", "1.00000005960464477539", "1.0000000596046448",
        "0x1.000001p0", "0x1.0000018p0", "0x1p-150", "0x1.8p-149", "12345678901234567890", "-12345678901234567890",
        "18446744073709551615", "18446744073709551616", "-4294967295", "0x10", "  7x", "1e999999999999",
        "1e-999999999999", "0.000000000000000000000000000000000001e-20", "100000000000000000000000000000000000000000",
        "/", "/abc", "/ abc", "/abcdefghijklmnopqrstu v w", "/a b c d", " /a", "/a  ", "/a\tb\nc", "/tod 12 x",
        "/weather 3", "1 2.5 3 4 5 6 1 0.5", "1 2.5", "3 1.25 0", "3 1.25 x", "3 x"};
    for (const std::string& text : fixed) {
        check(text);
        check_fields(text);
    }
    for (const std::string& text : fixed_fields) {
        check(text);
        check_fields(text);
    }
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    const int count = argc > 1 ? text::to_int(core::at(arguments, 1)) : 200000;
    // the same texts every run unless another seed is given
    const auto seed = static_cast<std::mt19937::result_type>(argc > 2 ? text::to_int(core::at(arguments, 2)) : 12345);
    std::mt19937 random(seed);
    const auto alphabet = std::to_array(" \t+-0123456789012345678901234567890123456789..eEexXpPabcfinINFNAN");
    std::uniform_int_distribution<int> length(0, kMaxTextLength);
    std::uniform_int_distribution<int> pick(0, static_cast<int>(sizeof(alphabet)) - 2);
    for (int i = 0; i < count; ++i) {
        std::string text;
        const int item_count = length(random);
        for (int k = 0; k < item_count; ++k) {
            text += core::at(alphabet, pick(random));
        }
        check(text);
        check_fields(text);
    }
    // texts of several fields: words of the same characters (and some others) between white space, some after a '/'
    const auto word_alphabet = std::to_array("+-0123456789012345678901234567890123456789..eEexXpPabcfinINFNAN()_/z");
    std::uniform_int_distribution<int> words(0, kMaxWordsPerText);
    std::uniform_int_distribution<int> word_length(0, kMaxWordLength);
    std::uniform_int_distribution<int> pick_word(0, static_cast<int>(sizeof(word_alphabet)) - 2);
    std::uniform_int_distribution<int> gap(0, kMaxGap);
    for (int i = 0; i < count; ++i) {
        std::string text = gap(random) == 0 ? "/" : "";
        const int item_count = words(random);
        for (int word = 0; word < item_count; ++word) {
            if (word > 0 || gap(random) == 0) {
                text += gap(random) == 0 ? "\t " : " ";
            }
            const int letters = word_length(random);
            for (int k = 0; k < letters; ++k) {
                text += core::at(word_alphabet, pick_word(random));
            }
        }
        check_fields(text);
    }
    text::print("parse check: %d texts, %d differences\n", parse_check_state().checked_, parse_check_state().failures_);
    return parse_check_state().failures_ == 0 ? 0 : 1;
}
