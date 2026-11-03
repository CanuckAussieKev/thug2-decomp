#pragma once

#include <charconv>
#include <climits>
#include <cstring>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

#include "engine/core/checked_index.h"

// NOT ORIGINAL - text read as a number the way the C runtime's atoi / atol / atof read it, written over
// std::from_chars: leading white space skipped, an optional sign, the number up to the first character that can't
// continue it, 0 when there is none (what the code relies on: "abc" is 0), and atoi's limits past its range. The
// same answers for every text (src/tests/parse_check.cpp compares them with the C runtime's own).
namespace text {

namespace number_detail {

// isspace in the "C" locale: space, \t \n \v \f \r
constexpr bool is_space(char c) noexcept { return c == ' ' || (c >= '\t' && c <= '\r'); }

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

// The character at `index` of the text, or the terminator the C text would have there.
constexpr char char_at(std::string_view text, std::size_t index) noexcept
{
    return index < text.size() ? core::at(text, index) : '\0';
}

// The address of the text's first character.
inline const char* begin_of(std::string_view text) noexcept { return text.data(); }

// The address one past the text (the end of a range for std::from_chars).
inline const char* end_of(std::string_view text) noexcept { return text.substr(text.size()).data(); }

// White space, then an optional sign: where the number starts; `negative` set by a '-'.
inline std::string_view skip_lead(std::string_view text, bool& negative) noexcept
{
    while (is_space(char_at(text, 0))) {
        text.remove_prefix(1);
    }
    negative = char_at(text, 0) == '-';
    if (char_at(text, 0) == '+' || char_at(text, 0) == '-') {
        text.remove_prefix(1);
    }
    return text;
}

} // namespace number_detail

// What atoi / atol give (32-bit, base 10): the digits after the white space and sign; INT_MAX / INT_MIN past the
// range (the runtime's strtol limits).
inline int to_int(const char* text) noexcept
{
    bool negative = false;
    const std::string_view first = number_detail::skip_lead(text, negative);
    std::size_t digits = 0;
    while (number_detail::is_digit(number_detail::char_at(first, digits))) {
        ++digits;
    }
    unsigned long long magnitude = 0;
    if (std::from_chars(number_detail::begin_of(first), number_detail::end_of(first.substr(0, digits)), magnitude).ec ==
        std::errc::result_out_of_range) {
        magnitude = std::numeric_limits<unsigned long long>::max();
    }
    constexpr unsigned long long kMinMagnitude = 1ULL + INT_MAX; // INT_MIN's magnitude
    if (negative) {
        return magnitude >= kMinMagnitude ? INT_MIN : -static_cast<int>(magnitude);
    }
    return magnitude > static_cast<unsigned long long>(INT_MAX) ? INT_MAX : static_cast<int>(magnitude);
}

// What atof gives: a decimal number with an optional fraction and exponent, a hexadecimal one after "0x", or
// inf / infinity / nan; +-HUGE_VAL past the double's range, 0 below it.
inline double to_double(const char* text) noexcept
{
    using number_detail::char_at;
    bool negative = false;
    const std::string_view first = number_detail::skip_lead(text, negative);
    if (char_at(first, 0) == '+' || char_at(first, 0) == '-') {
        return 0.0; // (a second sign: no number)
    }
    const char* last = number_detail::end_of(first);
    double value = 0.0;
    std::from_chars_result read{};
    auto is_hex_digit = [&](std::size_t index) {
        const char c = char_at(first, index);
        return number_detail::is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    };
    if (char_at(first, 0) == '0' && (char_at(first, 1) == 'x' || char_at(first, 1) == 'X') &&
        (is_hex_digit(2) || (char_at(first, 2) == '.' && is_hex_digit(3)))) {
        read = std::from_chars(number_detail::begin_of(first.substr(2)), last, value, std::chars_format::hex);
    } else {
        read = std::from_chars(number_detail::begin_of(first), last, value, std::chars_format::general);
    }
    if (read.ec == std::errc::invalid_argument) {
        return 0.0;
    }
    if (read.ec == std::errc::result_out_of_range) {
        // too large (HUGE_VAL) or too small (0): the sign of the exponent the text spells out, counting the digits
        // before the point as a decimal (or binary, for hex) exponent of their own
        const bool hex = char_at(first, 1) == 'x' || char_at(first, 1) == 'X';
        std::size_t p = hex ? 2 : 0;
        long whole_digits = 0;
        bool seen_nonzero = false;
        for (; char_at(first, p) != '\0' && char_at(first, p) != '.' && char_at(first, p) != 'e' &&
               char_at(first, p) != 'E' && char_at(first, p) != 'p' && char_at(first, p) != 'P';
             ++p) {
            seen_nonzero = seen_nonzero || char_at(first, p) != '0';
            whole_digits += seen_nonzero ? 1 : 0;
        }
        bool large = whole_digits > 0;
        const std::size_t exponent = first.find_first_of(hex ? "pP" : "eE");
        if (exponent != std::string_view::npos && std::cmp_less(exponent, read.ptr - number_detail::begin_of(first))) {
            bool exponent_negative = false;
            number_detail::skip_lead(first.substr(exponent + 1), exponent_negative);
            large = !exponent_negative;
        }
        value = large ? std::numeric_limits<double>::infinity() : 0.0;
    }
    return negative ? -value : value;
}

} // namespace text
