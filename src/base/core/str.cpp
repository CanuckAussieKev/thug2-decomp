#include "base/core/str.h"

#include <cstdio>
#include <cstring>
#include <array>
#include <span>
#include <string_view>

#include "base/core/checked_index.h"
#include "base/core/cursor.h"
#include "base/core/format.h"
#include "base/core/language.h"

namespace text {
namespace {
constexpr size_t kSeparatedNumberSize = 16;
constexpr size_t kDigitsSize = 32;
constexpr int kLetterDigitOffset = 10;    // 'a' is the digit ten
} // namespace

// 0x00402080
void to_lower_text(char* string)
{
    for (char& character : std::span<char>(string, std::strlen(string))) {
        if (character >= 'A' && character <= 'Z') {
            character += 'a' - 'A';
        }
    }
}

// 0x004020B0
void to_capitals(char* string)
{
    for (char& character : std::span<char>(string, std::strlen(string))) {
        if (character >= 'a' && character <= 'z') {
            character -= 'a' - 'A';
        }
    }
}

namespace {

char get_lower(char character) { return (character >= 'A' && character <= 'Z') ? static_cast<char>(character + ('a' - 'A')) : character; }

} // namespace

// 0x00401FF0
const char* find_caseless(CaselessSearch search)
{
    const auto* const haystack = search.haystack;
    const auto* const needle = search.needle;
    if (*needle == '\0') {
        return nullptr;
    }
    const std::string_view hay(haystack);
    const std::string_view wanted(needle);
    const char first = get_lower(*needle);
    for (std::size_t at = 0; at < hay.size(); ++at) {
        if (get_lower(core::at(hay, at)) != first) {
            continue;
        }
        std::size_t matched = 1;
        // (the haystack's terminator ends a partial match, as it differs from the needle's next character)
        while (matched < wanted.size() && at + matched < hay.size() &&
               get_lower(core::at(wanted, matched)) == get_lower(core::at(hay, at + matched))) {
            ++matched;
        }
        if (matched == wanted.size()) {
            return hay.substr(at).data();
        }
    }
    return nullptr;
}

namespace {
std::array<char, kSeparatedNumberSize>& separated_number()
{
    static std::array<char, kSeparatedNumberSize> separated_number{};
    return separated_number;
}
} // namespace

// 0x00402200
const char* format_with_separators(int value, InLanguage in)
{
    core::Cursor<char> out{std::span<char>(separated_number())}; // shared with the original code
    if (value < 0) {
        *out++ = '-';
        value = static_cast<int>(0U - static_cast<unsigned>(value)); // INT_MIN stays negative, as in the original
    }

    std::array<char, kDigitsSize> digits{};
    text::format_to(digits, "%d", value);

    char separator = 0;
    switch (in.language) { // the game's language setting, from the caller
    case core::kLanguageFrench:
        separator = ' ';
        break;
    case core::kLanguageSpanish:
    case core::kLanguageGerman:
    case core::kLanguageItalian:
        separator = '.';
        break;
    default:
        separator = ',';
        break;
    }

    const std::string_view shown(digits.data());
    int remaining = static_cast<int>(shown.size());
    for (const char digit : shown) {
        *out++ = digit;
        if (--remaining == 0) {
            break;
        }
        if (remaining % 3 == 0) {
            *out++ = separator;
        }
    }
    *out = '\0';
    return separated_number().data();
}

// 0x00402120
int compare_caseless(const char* a, const char* b)
{
    const std::string_view left(a);
    const std::string_view right(b);
    for (std::size_t i = 0;; ++i) {
        // (both texts end at their terminators: equal there, or the first to end is the lesser)
        const char ca = i < left.size() ? get_lower(core::at(left, i)) : '\0';
        const char cb = i < right.size() ? get_lower(core::at(right, i)) : '\0';
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
        if (ca == '\0') {
            return 0;
        }
    }
}

// 0x004020E0
int hex_digit_value(char digit)
{
    const char character = digit;
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'v') {
        return character - 'a' + kLetterDigitOffset;
    }
    if (character >= 'A' && character <= 'V') {
        return character - 'A' + kLetterDigitOffset;
    }
    return 0;
}

// 0x00402170
int next_word(std::span<char> word, core::CStringCursor& cursor)
{
    core::Cursor<char> out(word);
    for (;;) {
        const char character = *cursor;
        if (character == '\0') {
            *out = '\0';
            return 2;
        }
        if (character == ' ') {
            break;
        }
        if (character == '\\') {
            ++cursor;
            const char escaped = *cursor;
            if (escaped == '_') {
                *out++ = ' ';
                ++cursor;
            } else if (escaped == 'n') {
                ++cursor;
                *out = '\0';
                return 1;
            } else {
                out[0] = '\\';
                out[1] = escaped;
                out += 2;
                ++cursor;
            }
        } else {
            *out++ = character;
            ++cursor;
        }
    }
    // The terminating space is kept as part of the word.
    out[0] = *cursor;
    ++cursor;
    out[1] = '\0';
    return 0;
}

} // namespace text
