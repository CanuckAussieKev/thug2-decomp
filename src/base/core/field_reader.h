#pragma once

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>

#include "base/core/checked_index.h"
#include "base/core/ints.h"
#include "base/core/parse_number.h"

// NOT ORIGINAL - text read field by field the way sscanf reads it, without C varargs: each read is one conversion of
// the format, in its order (`%d %f /%15s` -> read(int), read(float), match('/'), read_word(16 bytes)). A field that
// can't be read stops the reading - it and every later one are left as they were, as sscanf leaves them - and
// scanned() is what sscanf returns: the fields read, or -1 when the text ended before the first one.
// The same answers as the C runtime's sscanf_s for every text (src/tests/parse_check.cpp compares them).
namespace text {

class FieldReader {
public:
    explicit FieldReader(std::string_view text) noexcept : text_(text) {}

    // "%d": white space, an optional sign, decimal digits (taken modulo 2^32; past 64 bits as all ones - the runtime's).
    bool read(int& value) noexcept
    {
        if (!begin_field()) {
            return false;
        }
        std::size_t at = at_;
        const bool negative = char_at(at) == '-';
        if (char_at(at) == '+' || char_at(at) == '-') {
            ++at;
        }
        if (!number_detail::is_digit(char_at(at))) {
            return fail_at(at);
        }
        uint64_t magnitude = 0;
        bool overflow = false;
        constexpr uint64_t kMost = std::numeric_limits<uint64_t>::max();
        constexpr uint64_t kBase = 10;
        for (; number_detail::is_digit(char_at(at)); ++at) {
            const auto digit = static_cast<uint64_t>(char_at(at) - '0');
            if (magnitude > (kMost - digit) / kBase) {
                overflow = true;
            } else {
                magnitude = (magnitude * kBase) + digit;
            }
        }
        // (read as a 64-bit number, held at its limits, then its low 32 bits)
        constexpr uint64_t kLargest = std::numeric_limits<int64_t>::max();
        if (overflow || magnitude > kLargest + (negative ? 1 : 0)) {
            magnitude = kLargest + (negative ? 1 : 0);
        }
        if (negative) {
            magnitude = 0 - magnitude;
        }
        value = ints::as_signed(static_cast<uint32_t>(magnitude));
        return take(at);
    }

    // "%f": white space, then a decimal or hexadecimal ("0x") number, inf / infinity or nan / nan(...), with an optional
    // sign - read as far as it goes (no going back: "1e+x" is no number); rounded to a float.
    bool read(float& value) noexcept
    {
        if (!begin_field()) {
            return false;
        }
        std::size_t at = at_;
        const bool negative = char_at(at) == '-';
        if (char_at(at) == '+' || char_at(at) == '-') {
            ++at;
        }
        const std::size_t start = at;
        bool hex = false;
        if (lower(char_at(at)) == 'i') {
            if (!read_infinity(at)) {
                return false;
            }
        } else if (lower(char_at(at)) == 'n') {
            if (!read_nan(at)) {
                return false;
            }
        } else {
            hex = char_at(at) == '0' && lower(char_at(at + 1)) == 'x';
            if (hex) {
                at += 2;
            }
            if (!read_mantissa_and_exponent(at, hex)) {
                return false;
            }
        }
        const std::string_view number = text_.substr(start, at - start);
        constexpr uint32_t kRuntimeNan = 0x7FFFFFFFU; // (the runtime's NaN: every fraction bit set)
        const float magnitude = lower(char_at(start)) == 'n' ? std::bit_cast<float>(kRuntimeNan)
                                                             : to_float(hex ? number.substr(2) : number, hex);
        value = negative ? -magnitude : magnitude;
        return take(at);
    }

    // "%<n>s" with n = out.size() - 1: white space, then up to n characters up to the next white space, terminated.
    bool read_word(std::span<char> out) noexcept
    {
        if (out.empty() || !begin_field()) {
            return false;
        }
        std::size_t at = at_;
        std::size_t length = 0;
        for (; length + 1 < out.size() && char_at(at) != '\0' && !number_detail::is_space(char_at(at)); ++at) {
            core::at(out, length++) = char_at(at);
        }
        core::at(out, length) = '\0';
        return take(at);
    }

    // A character of the format itself: the text's next one must be it (no white space skipped).
    bool match(char literal) noexcept
    {
        if (stopped_) {
            return false;
        }
        if (char_at(at_) != literal) {
            return fail_at(at_);
        }
        ++at_;
        return true;
    }

    // What sscanf returns: the fields read, or -1 when the text ended before the first one.
    [[nodiscard]] int scanned() const noexcept { return fields_ == 0 && ended_ ? -1 : fields_; }

private:
    // "inf" or "infinity" (any case) at `at`; false (failed there) if not.
    bool read_infinity(std::size_t& at) noexcept
    {
        if (!match_word(at, "inf")) {
            return fail_at(at);
        }
        if (lower(char_at(at)) == 'i' && !match_word(at, "inity")) {
            return fail_at(at);
        }
        return true;
    }

    // "nan" or "nan(...)" (any case) at `at`; false (failed there) if not.
    bool read_nan(std::size_t& at) noexcept
    {
        if (!match_word(at, "nan")) {
            return fail_at(at);
        }
        if (char_at(at) == '(') {
            ++at;
            while (is_word_char(char_at(at))) {
                ++at;
            }
            if (char_at(at) != ')') {
                return fail_at(at);
            }
            ++at;
        }
        return true;
    }

    // A number's digits (decimal or hex) with a point and an exponent at `at`; false (failed there) if not.
    bool read_mantissa_and_exponent(std::size_t& at, bool hex) noexcept
    {
        auto is_mantissa_digit = [&](char character) {
            return hex ? is_hex_digit(character) : number_detail::is_digit(character);
        };
        std::size_t digits = 0;
        for (; is_mantissa_digit(char_at(at)); ++at) {
            ++digits;
        }
        if (char_at(at) == '.') {
            ++at;
            for (; is_mantissa_digit(char_at(at)); ++at) {
                ++digits;
            }
        }
        if (digits == 0) {
            return fail_at(at);
        }
        if (lower(char_at(at)) == (hex ? 'p' : 'e')) {
            ++at;
            if (char_at(at) == '+' || char_at(at) == '-') {
                ++at;
            }
            if (!number_detail::is_digit(char_at(at))) {
                return fail_at(at);
            }
            while (number_detail::is_digit(char_at(at))) {
                ++at;
            }
        }
        return true;
    }

    [[nodiscard]] char char_at(std::size_t index) const noexcept { return number_detail::char_at(text_, index); }
    static constexpr char lower(char character) noexcept { return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character; }
    static constexpr bool is_hex_digit(char character) noexcept
    {
        return number_detail::is_digit(character) || (lower(character) >= 'a' && lower(character) <= 'f');
    }
    static constexpr bool is_word_char(char character) noexcept
    {
        return number_detail::is_digit(character) || (lower(character) >= 'a' && lower(character) <= 'z') || character == '_';
    }

    // White space before a field; false (the text ended) when none is left.
    bool begin_field() noexcept
    {
        if (stopped_) {
            return false;
        }
        while (number_detail::is_space(char_at(at_))) {
            ++at_;
        }
        return char_at(at_) != '\0' || fail_at(at_);
    }
    // `word` (any case) at `at`, moving past what matched.
    bool match_word(std::size_t& at, std::string_view word) const noexcept
    {
        for (const char character : word) {
            if (lower(char_at(at)) != character) {
                return false;
            }
            ++at;
        }
        return true;
    }
    // The reading stops at `at`: the text ended there, or a character the field can't have.
    bool fail_at(std::size_t at) noexcept
    {
        stopped_ = true;
        ended_ = char_at(at) == '\0';
        return false;
    }
    bool take(std::size_t at) noexcept
    {
        at_ = at;
        ++fields_;
        return true;
    }
    // The number's text (no sign; a hex one without its "0x") as a float, rounded once; past the float's range
    // infinity or 0 by the sign of its decimal (binary, for hex) exponent.
    static float to_float(std::string_view number, bool hex) noexcept
    {
        float value = 0.0F;
        if (std::from_chars(number_detail::begin_of(number), number_detail::end_of(number), value,
                            hex ? std::chars_format::hex : std::chars_format::general)
                .ec != std::errc::result_out_of_range) {
            return value;
        }
        return is_large(number, hex) ? std::numeric_limits<float>::infinity() : 0.0F;
    }
    // Whether a number out of the float's range is above it: the exponent the text spells out, with the digits before
    // the point (or the zeros after it) counted in (only its sign matters: the range ends far from 1).
    static bool is_large(std::string_view number, bool hex) noexcept
    {
        auto is_digit = [hex](char character) { return hex ? is_hex_digit(character) : number_detail::is_digit(character); };
        auto at = [&](std::size_t index) { return number_detail::char_at(number, index); };
        std::size_t position = 0;
        int64_t whole = 0;
        int64_t lead = 0;
        bool seen = false;
        for (; is_digit(at(position)); ++position) {
            seen = seen || at(position) != '0';
            whole += seen ? 1 : 0;
        }
        if (at(position) == '.') {
            for (++position; is_digit(at(position)); ++position) {
                seen = seen || at(position) != '0';
                lead += seen ? 0 : 1;
            }
        }
        int64_t exponent = 0;
        constexpr int64_t kLimit = 1000000000; // (past any number's range)
        constexpr int64_t kBase = 10;
        if (lower(at(position)) == (hex ? 'p' : 'e')) {
            ++position;
            const bool negative = at(position) == '-';
            if (at(position) == '+' || at(position) == '-') {
                ++position;
            }
            for (; number_detail::is_digit(at(position)); ++position) {
                exponent = std::min((exponent * kBase) + (at(position) - '0'), kLimit);
            }
            exponent = negative ? -exponent : exponent;
        }
        const int64_t scale = hex ? 4 : 1; // (a hex digit: 4 binary places)
        return (whole > 0 ? exponent + (scale * whole) : exponent - (scale * lead)) > 0;
    }

    std::string_view text_;
    std::size_t at_ = 0;
    int fields_ = 0;
    bool stopped_ = false;
    bool ended_ = false;
};

} // namespace text
