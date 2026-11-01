#pragma once

#include <charconv>
#include <climits>
#include <cstring>
#include <limits>
#include <system_error>

// NOT ORIGINAL - text read as a number the way the C runtime's atoi / atol / atof read it, written over
// std::from_chars: leading white space skipped, an optional sign, the number up to the first character that can't
// continue it, 0 when there is none (what the code relies on: "abc" is 0), and atoi's limits past its range. The
// same answers for every text (src/tests/parse_check.cpp compares them with the C runtime's own).
namespace text {

namespace number_detail {

// isspace in the "C" locale: space, \t \n \v \f \r
constexpr bool is_space(char c) noexcept { return c == ' ' || (c >= '\t' && c <= '\r'); }

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

// White space, then an optional sign: where the number starts; `negative` set by a '-'.
inline const char* skip_lead(const char* text, bool& negative) noexcept
{
    while (is_space(*text))
        ++text;
    negative = *text == '-';
    if (*text == '+' || *text == '-')
        ++text;
    return text;
}

} // namespace number_detail

// What atoi / atol give (32-bit, base 10): the digits after the white space and sign; INT_MAX / INT_MIN past the
// range (the runtime's strtol limits).
inline int to_int(const char* text) noexcept
{
    bool negative = false;
    const char* first = number_detail::skip_lead(text, negative);
    const char* last = first;
    while (number_detail::is_digit(*last))
        ++last;
    unsigned long long magnitude = 0;
    if (std::from_chars(first, last, magnitude).ec == std::errc::result_out_of_range)
        magnitude = std::numeric_limits<unsigned long long>::max();
    constexpr unsigned long long kMinMagnitude = 1ull + INT_MAX; // INT_MIN's magnitude
    if (negative)
        return magnitude >= kMinMagnitude ? INT_MIN : -static_cast<int>(magnitude);
    return magnitude > static_cast<unsigned long long>(INT_MAX) ? INT_MAX : static_cast<int>(magnitude);
}

// What atof gives: a decimal number with an optional fraction and exponent, a hexadecimal one after "0x", or
// inf / infinity / nan; +-HUGE_VAL past the double's range, 0 below it.
inline double to_double(const char* text) noexcept
{
    bool negative = false;
    const char* first = number_detail::skip_lead(text, negative);
    if (*first == '+' || *first == '-')
        return 0.0; // (a second sign: no number)
    const char* last = first + std::strlen(first);
    double value = 0.0;
    std::from_chars_result read{};
    if (first[0] == '0' && (first[1] == 'x' || first[1] == 'X') &&
        (number_detail::is_digit(first[2]) || (first[2] >= 'a' && first[2] <= 'f') ||
         (first[2] >= 'A' && first[2] <= 'F') ||
         (first[2] == '.' && (number_detail::is_digit(first[3]) || (first[3] >= 'a' && first[3] <= 'f') ||
                              (first[3] >= 'A' && first[3] <= 'F'))))) {
        read = std::from_chars(first + 2, last, value, std::chars_format::hex);
    } else {
        read = std::from_chars(first, last, value, std::chars_format::general);
    }
    if (read.ec == std::errc::invalid_argument)
        return 0.0;
    if (read.ec == std::errc::result_out_of_range) {
        // too large (HUGE_VAL) or too small (0): the sign of the exponent the text spells out, counting the digits
        // before the point as a decimal (or binary, for hex) exponent of their own
        const bool hex = first[1] == 'x' || first[1] == 'X';
        const char* p = hex ? first + 2 : first;
        long whole_digits = 0;
        bool seen_nonzero = false;
        for (; *p != '\0' && *p != '.' && *p != 'e' && *p != 'E' && *p != 'p' && *p != 'P'; ++p) {
            seen_nonzero = seen_nonzero || *p != '0';
            whole_digits += seen_nonzero ? 1 : 0;
        }
        bool large = whole_digits > 0;
        const char* exponent = std::strpbrk(first, hex ? "pP" : "eE");
        if (exponent != nullptr && exponent < read.ptr) {
            bool exponent_negative = false;
            number_detail::skip_lead(exponent + 1, exponent_negative);
            large = !exponent_negative;
        }
        value = large ? std::numeric_limits<double>::infinity() : 0.0;
    }
    return negative ? -value : value;
}

} // namespace text
