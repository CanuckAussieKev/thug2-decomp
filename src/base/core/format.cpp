#include "base/core/format.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "base/core/checked_index.h"

// NOT ORIGINAL - the formatting behind text::format_to / format_runtime_to (base/core/format.h): the C runtime's
// printf conversions, written out so the text is the same on every compiler (src/tests/printf_check.cpp compares it
// with the linked runtime's snprintf).
namespace text {

namespace format_detail {

// (only reached in a constant evaluation, where reaching them is the error; never at run time)
void format_has_too_few_arguments() {}
void format_has_too_many_arguments() {}
void format_conversion_is_not_supported() {}
void format_argument_does_not_match_its_conversion() {}
void format_width_or_precision_argument_is_not_an_int() {}
void format_ends_inside_a_conversion() {}

} // namespace format_detail

namespace {
constexpr size_t kRuntimeLineSize = 256;  // format_runtime's line buffer
constexpr int kFloatDigitsSlack = 32;     // room beyond the digits for the point, the sign and an exponent

constexpr int kDefaultPrecision = 6;
constexpr int kDecimal = 10;
constexpr int kOctal = 8;
constexpr int kHex = 16;
constexpr unsigned kBitsPerByte = 8;
constexpr size_t kModifierChar = 1;  // hh
constexpr size_t kModifierShort = 2; // h
constexpr size_t kModifierInt = 4;   // none, l (32 bits, as on Windows)
constexpr size_t kModifierWide = 8;  // ll, I64, j
constexpr std::string_view kNullText = "(null)";

// One conversion: %[flags][width][.precision][length]conversion.
struct Spec {
    bool left = false;  // '-'
    bool plus = false;  // '+'
    bool space = false; // ' '
    bool alt = false;   // '#'
    bool zero = false;  // '0'
    int width = 0;
    int precision = -1; // -1: none given
    size_t size = kModifierInt;
    bool wide = false; // 'l' before s / c
    char conversion = '\0';
};

// Where the text goes: kept while it fits (one place left for the terminator), counted in full.
class Sink {
public:
    explicit Sink(std::span<char> out) : out_(out) {}
    void put(char character)
    {
        if (length_ + 1 < out_.size()) {
            core::at(out_, length_) = character;
        }
        ++length_;
    }
    void put(std::string_view text)
    {
        for (const char character : text) {
            put(character);
        }
    }
    // A character and how many times it is written.
    struct Fill {
        char c{};
        size_t count{};
    };

    void repeat(Fill fill)
    {
        const auto fill_char = fill.c;
        const auto count = fill.count;
        for (size_t i = 0; i < count; ++i) {
            put(fill_char);
        }
    }
    size_t finish()
    {
        if (!out_.empty()) {
            core::at(out_, std::min(length_, out_.size() - 1)) = '\0';
        }
        return length_;
    }

private:
    std::span<char> out_;
    size_t length_ = 0;
};

// The arguments in order; one that isn't there reads as nothing.
class Args {
public:
    explicit Args(std::span<const FormatArg> args) : args_(args) {}
    const FormatArg* next()
    {
        if (next_ >= args_.size()) {
            return nullptr;
        }
        return &core::at(args_, next_++);
    }

private:
    std::span<const FormatArg> args_;
    size_t next_ = 0;
};

// An integer argument's bits (anything else: 0).
uint64_t integer_bits(const FormatArg* arg)
{
    if (!arg) {
        return 0;
    }
    if (const auto* value = std::get_if<int64_t>(arg)) {
        return static_cast<uint64_t>(*value);
    }
    if (const auto* value = std::get_if<uint64_t>(arg)) {
        return *value;
    }
    return 0;
}

// '*': an int argument.
int int_argument(const FormatArg* arg) { return static_cast<int>(static_cast<uint32_t>(integer_bits(arg))); }

double float_value(const FormatArg* arg)
{
    if (arg) {
        if (const auto* value = std::get_if<double>(arg)) {
            return *value;
        }
    }
    return 0.0;
}

// A pointer argument of any kind as its address (null: none).
uintptr_t pointer_bits(const FormatArg* arg)
{
    if (!arg) {
        return 0;
    }
    if (const auto* value = std::get_if<const void*>(arg)) {
        return std::bit_cast<uintptr_t>(*value);
    }
    if (const auto* value = std::get_if<const char*>(arg)) {
        return std::bit_cast<uintptr_t>(*value);
    }
    if (const auto* value = std::get_if<const wchar_t*>(arg)) {
        return std::bit_cast<uintptr_t>(*value);
    }
    return 0;
}

// `prefix` (a sign, "0x") and `body` padded to the width: spaces before, zeros between (when allowed) or spaces after.
void emit(Sink& sink, const Spec& spec, std::string_view prefix, std::string_view body, bool zeros_allowed)
{
    const size_t length = prefix.size() + body.size();
    const size_t pad = std::cmp_greater(spec.width, length) ? static_cast<size_t>(spec.width) - length : 0;
    if (spec.left) {
        sink.put(prefix);
        sink.put(body);
        sink.repeat({.c = ' ', .count = pad});
    } else if (spec.zero && zeros_allowed) {
        sink.put(prefix);
        sink.repeat({.c = '0', .count = pad});
        sink.put(body);
    } else {
        sink.repeat({.c = ' ', .count = pad});
        sink.put(prefix);
        sink.put(body);
    }
}

void put_integer(Sink& sink, const Spec& spec, const FormatArg* arg)
{
    const unsigned bits = static_cast<unsigned>(spec.size) * kBitsPerByte;
    const uint64_t mask =
        spec.size >= sizeof(uint64_t) ? std::numeric_limits<uint64_t>::max() : (uint64_t{1} << bits) - 1;
    const uint64_t value = integer_bits(arg) & mask;
    const bool is_signed = spec.conversion == 'd' || spec.conversion == 'i';
    const bool negative = is_signed && ((value >> (bits - 1)) & 1) != 0;
    const uint64_t magnitude = negative ? ((~value & mask) + 1) : value;

    int base = kDecimal;
    if (spec.conversion == 'o') {
        base = kOctal;
    } else if (spec.conversion == 'x' || spec.conversion == 'X') {
        base = kHex;
    }
    std::array<char, 2 * sizeof(uint64_t) * kBitsPerByte> digits{};
    const auto converted = std::to_chars(digits.data(), std::next(digits.data(), digits.size()), magnitude, base);
    auto digit_count = static_cast<size_t>(converted.ptr - digits.data());
    if (spec.conversion == 'X') {
        std::transform(digits.begin(), std::next(digits.begin(), static_cast<std::ptrdiff_t>(digit_count)),
                       digits.begin(),
                       [](char character) { return character >= 'a' && character <= 'f' ? static_cast<char>(character - 'a' + 'A') : character; });
    }
    if (magnitude == 0 && spec.precision == 0) {
        digit_count = 0; // (no digits for a zero of precision 0)
    }

    std::string body;
    if (std::cmp_greater(spec.precision, digit_count)) {
        body.assign(static_cast<size_t>(spec.precision) - digit_count, '0');
    }
    body.append(digits.data(), digit_count);
    if (spec.alt && spec.conversion == 'o' && (body.empty() || body.front() != '0')) {
        body.insert(body.begin(), '0');
    }

    std::string_view prefix;
    if (negative) {
        prefix = "-";
    } else if (is_signed && spec.plus) {
        prefix = "+";
    } else if (is_signed && spec.space) {
        prefix = " ";
    } else if (spec.alt && magnitude != 0 && spec.conversion == 'x') {
        prefix = "0x";
    } else if (spec.alt && magnitude != 0 && spec.conversion == 'X') {
        prefix = "0X";
    }
    emit(sink, spec, prefix, body, spec.precision < 0);
}

void put_pointer(Sink& sink, Spec spec, const FormatArg* arg)
{
    // (the runtime's %p: the address in upper-case hex, as many digits as a pointer has)
    spec.conversion = 'X';
    spec.alt = false; // (no "0X" for '#')
    spec.size = sizeof(uintptr_t);
    spec.precision = static_cast<int>(2 * sizeof(uintptr_t));
    const FormatArg address = uint64_t{pointer_bits(arg)};
    put_integer(sink, spec, &address);
}

void put_char(Sink& sink, const Spec& spec, const FormatArg* arg)
{
    const auto character = static_cast<char>(static_cast<uint8_t>(integer_bits(arg)));
    emit(sink, spec, {}, std::string_view(&character, 1), true);
}

void put_text(Sink& sink, const Spec& spec, const FormatArg* arg)
{
    const char* const* text = arg ? std::get_if<const char*>(arg) : nullptr;
    if (!text || !*text) {
        std::string_view shown = kNullText;
        if (spec.precision >= 0) {
            shown = shown.substr(0, static_cast<size_t>(spec.precision));
        }
        emit(sink, spec, {}, shown, true);
        return;
    }
    if (spec.precision < 0) {
        emit(sink, spec, {}, std::string_view(*text), true);
        return;
    }
    // (at most `precision` characters read: the text needn't be terminated within them)
    const std::span<const char> most(*text, static_cast<size_t>(spec.precision));
    size_t length = 0;
    while (length < most.size() && core::at(most, length) != '\0') {
        ++length;
    }
    emit(sink, spec, {}, std::string_view(most.data(), length), true);
}

void put_wide_text(Sink& sink, const Spec& spec, const FormatArg* arg)
{
    const wchar_t* const* text = arg ? std::get_if<const wchar_t*>(arg) : nullptr;
    if (!text || !*text) {
        Spec narrow = spec;
        narrow.wide = false;
        put_text(sink, narrow, nullptr);
        return;
    }
    // (each character as one byte - the "C" locale's; one outside it as '?')
    const std::wstring_view wide(*text);
    std::string shown;
    for (const wchar_t wide_char : wide) {
        if (spec.precision >= 0 && std::cmp_greater_equal(shown.size(), spec.precision)) {
            break;
        }
        const auto code = static_cast<unsigned>(wide_char);
        shown += code <= std::numeric_limits<uint8_t>::max() ? static_cast<char>(code) : '?';
    }
    emit(sink, spec, {}, shown, true);
}

// The digits of |value| by std::to_chars in `form` with `precision` places.
std::string float_digits(double value, std::chars_format form, int precision)
{
    // (the longest: 309 integer digits of DBL_MAX, the point, the places, an exponent)
    std::string text(static_cast<size_t>(precision) + std::numeric_limits<double>::max_exponent10 + kFloatDigitsSlack, '\0');
    const auto converted = std::to_chars(text.data(), std::next(text.data(), static_cast<std::ptrdiff_t>(text.size())),
                                         value, form, precision);
    text.resize(static_cast<size_t>(converted.ptr - text.data()));
    return text;
}

// The exponent of a scientific text ("1.5e+03": 3).
int exponent_of(std::string_view scientific)
{
    const size_t exponent_at = scientific.find('e');
    if (exponent_at == std::string_view::npos) {
        return 0;
    }
    std::string_view digits = scientific.substr(exponent_at + 1);
    const bool negative = !digits.empty() && digits.front() == '-';
    if (!digits.empty() && (digits.front() == '-' || digits.front() == '+')) {
        digits.remove_prefix(1);
    }
    int exponent = 0;
    for (const char digit : digits) {
        exponent = (exponent * kDecimal) + (digit - '0');
    }
    return negative ? -exponent : exponent;
}

// '#' with no places: the point kept ("1." / "1.e+00").
std::string with_point(std::string text)
{
    if (text.find('.') != std::string::npos) {
        return text;
    }
    const size_t exponent_at = text.find('e');
    text.insert(exponent_at == std::string::npos ? text.size() : exponent_at, 1, '.');
    return text;
}

// %g without '#': the places' trailing zeros dropped, and the point if nothing follows it.
std::string without_trailing_zeros(std::string text)
{
    const size_t point = text.find('.');
    if (point == std::string::npos) {
        return text;
    }
    const size_t exponent_at = text.find('e');
    const size_t end = exponent_at == std::string::npos ? text.size() : exponent_at;
    size_t last = end;
    while (last > point + 1 && core::at(text, last - 1) == '0') {
        --last;
    }
    if (last == point + 1) {
        last = point;
    }
    text.erase(last, end - last);
    return text;
}

void put_float(Sink& sink, const Spec& spec, const FormatArg* arg)
{
    const double value = float_value(arg);
    const bool upper = spec.conversion == 'F' || spec.conversion == 'E' || spec.conversion == 'G';
    const bool negative = std::signbit(value);
    std::string_view prefix;
    if (negative) {
        prefix = "-";
    } else if (spec.plus) {
        prefix = "+";
    } else if (spec.space) {
        prefix = " ";
    }

    std::string body;
    const bool finite = std::isfinite(value);
    if (std::isinf(value)) {
        body = "inf";
    } else if (std::isnan(value)) {
        // (the runtime's names: a signalling NaN, the "indefinite" one - negative, no payload - and any other)
        const auto bits = std::bit_cast<uint64_t>(value);
        constexpr uint64_t kQuietBit = uint64_t{1} << 51;
        constexpr uint64_t kPayload = kQuietBit - 1;
        if ((bits & kQuietBit) == 0) {
            body = "nan(snan)";
        } else if (negative && (bits & kPayload) == 0) {
            body = "nan(ind)";
        } else {
            body = "nan";
        }
    } else {
        const double magnitude = std::fabs(value);
        const int precision = spec.precision < 0 ? kDefaultPrecision : spec.precision;
        const char form = static_cast<char>(spec.conversion | ('a' - 'A')); // (lower case)
        if (form == 'f') {
            body = float_digits(magnitude, std::chars_format::fixed, precision);
        } else if (form == 'e') {
            body = float_digits(magnitude, std::chars_format::scientific, precision);
        } else {
            // %g: scientific when the exponent is below -4 or at least the precision, else fixed (C's rule)
            const int significant = precision == 0 ? 1 : precision;
            const int exponent =
                magnitude == 0.0 ? 0
                                 : exponent_of(float_digits(magnitude, std::chars_format::scientific, significant - 1));
            if (exponent < -4 || exponent >= significant) {
                body = float_digits(magnitude, std::chars_format::scientific, significant - 1);
            } else {
                body = float_digits(magnitude, std::chars_format::fixed, significant - 1 - exponent);
            }
            if (!spec.alt) {
                body = without_trailing_zeros(std::move(body));
            }
        }
        if (spec.alt) {
            body = with_point(std::move(body));
        }
    }
    if (upper) {
        std::ranges::transform(body, body.begin(),
                               [](char character) { return character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A') : character; });
    }
    const char form = static_cast<char>(spec.conversion | ('a' - 'A'));
    if (!finite && spec.alt && spec.precision == 0 && (form == 'f' || form == 'e')) {
        // (the runtime's '#' with no places puts its point after the first character of an infinity's or a NaN's
        // text, its minus sign included - a '+' or ' ' comes before: "i.nf", "-.inf", "+i.nf"; and with '0' the
        // zeros go between that minus sign and the point: "-00.inf")
        if (negative) {
            emit(sink, spec, prefix, "." + body, true);
        } else {
            body.insert(1, 1, '.');
            emit(sink, spec, prefix, body, false);
        }
        return;
    }
    emit(sink, spec, prefix, body, finite);
}

// The conversion starting after the '%' at `text`; how many characters it took (0: not one - printed as it is).
size_t read_spec(std::string_view text, Spec& spec, Args& args)
{
    size_t index = 0;
    auto peek = [&](size_t at) { return at < text.size() ? core::at(text, at) : '\0'; };
    for (;; ++index) {
        const char character = peek(index);
        if (character == '-') {
            spec.left = true;
        } else if (character == '+') {
            spec.plus = true;
        } else if (character == ' ') {
            spec.space = true;
        } else if (character == '#') {
            spec.alt = true;
        } else if (character == '0') {
            spec.zero = true;
        } else {
            break;
        }
    }
    if (peek(index) == '*') {
        spec.width = int_argument(args.next());
        if (spec.width < 0) {
            spec.left = true;
            spec.width = spec.width == std::numeric_limits<int>::min() ? std::numeric_limits<int>::max() : -spec.width;
        }
        ++index;
    } else {
        while (peek(index) >= '0' && peek(index) <= '9') {
            spec.width = (spec.width * kDecimal) + (peek(index++) - '0');
        }
    }
    if (peek(index) == '.') {
        ++index;
        spec.precision = 0;
        if (peek(index) == '*') {
            spec.precision = int_argument(args.next());
            if (spec.precision < 0) {
                spec.precision = -1;
            }
            ++index;
        } else {
            while (peek(index) >= '0' && peek(index) <= '9') {
                spec.precision = (spec.precision * kDecimal) + (peek(index++) - '0');
            }
        }
    }
    if (peek(index) == 'h') {
        ++index;
        spec.size = kModifierShort;
        if (peek(index) == 'h') {
            ++index;
            spec.size = kModifierChar;
        }
    } else if (peek(index) == 'l') {
        ++index;
        spec.wide = true;
        if (peek(index) == 'l') {
            ++index;
            spec.size = kModifierWide;
            spec.wide = false;
        }
    } else if (peek(index) == 'I' && peek(index + 1) == '6' && peek(index + 2) == '4') {
        index += 3;
        spec.size = kModifierWide;
    } else if (peek(index) == 'j') {
        ++index;
        spec.size = kModifierWide;
    } else if (peek(index) == 'z') {
        ++index;
        spec.size = sizeof(size_t);
    } else if (peek(index) == 't') {
        ++index;
        spec.size = sizeof(std::ptrdiff_t);
    } else if (peek(index) == 'L') {
        ++index;
    }
    spec.conversion = peek(index);
    if (spec.conversion == '\0') {
        return 0;
    }
    return index + 1;
}

} // namespace

size_t format_runtime_to(std::span<char> out, std::string_view format, std::span<const FormatArg> args)
{
    Sink sink(out);
    Args next_args(args);
    size_t index = 0;
    while (index < format.size()) {
        const char character = core::at(format, index);
        if (character != '%') {
            sink.put(character);
            ++index;
            continue;
        }
        if (index + 1 < format.size() && core::at(format, index + 1) == '%') {
            sink.put('%');
            index += 2;
            continue;
        }
        Spec spec;
        const std::string_view rest = format.substr(index + 1);
        const size_t taken = read_spec(rest, spec, next_args);
        if (taken == 0) {
            sink.put(format.substr(index));
            break;
        }
        switch (spec.conversion) {
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            put_integer(sink, spec, next_args.next());
            break;
        case 'c':
            put_char(sink, spec, next_args.next());
            break;
        case 's':
            if (spec.wide) {
                put_wide_text(sink, spec, next_args.next());
            } else {
                put_text(sink, spec, next_args.next());
            }
            break;
        case 'p':
            put_pointer(sink, spec, next_args.next());
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            put_float(sink, spec, next_args.next());
            break;
        default:
            // (not a conversion the game uses: printed as it is)
            sink.put(format.substr(index, taken + 1));
            break;
        }
        index += taken + 1;
    }
    return sink.finish();
}

std::string format_runtime(std::string_view format, std::span<const FormatArg> args)
{
    std::array<char, kRuntimeLineSize> line{};
    const size_t length = format_runtime_to(line, format, args);
    if (length < line.size()) {
        return {line.data(), length};
    }
    std::string text(length, '\0');
    // (the terminator's place: the string's own)
    format_runtime_to(std::span<char>(text.data(), length + 1), format, args);
    return text;
}

} // namespace text
