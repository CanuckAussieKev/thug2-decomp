#include "game/core/str.h"

#include <cstdio>
#include <cstring>

#include "game/sys/game_config.h"

namespace Str {

// 0x00402080
void LowerCase(char* string)
{
    for (; *string; ++string) {
        if (*string >= 'A' && *string <= 'Z')
            *string += 'a' - 'A';
    }
}

// 0x004020B0
void UpperCase(char* string)
{
    for (; *string; ++string) {
        if (*string >= 'a' && *string <= 'z')
            *string -= 'a' - 'A';
    }
}

namespace {

char Lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

} // namespace

// 0x00401FF0
const char* FindCaseless(const char* haystack, const char* needle)
{
    if (*needle == '\0')
        return nullptr;
    char first = Lower(*needle);
    for (; *haystack; ++haystack) {
        if (Lower(*haystack) != first)
            continue;
        int i = 1;
        while (needle[i] != '\0' && Lower(needle[i]) == Lower(haystack[i]))
            ++i;
        if (needle[i] == '\0')
            return haystack;
    }
    return nullptr;
}

char g_separatedNumber[16];

// 0x00402200
const char* FormatWithSeparators(int value)
{
    char* const buffer = g_separatedNumber; // shared with the original code
    char* out = buffer;
    if (value < 0) {
        *out++ = '-';
        value = static_cast<int>(0u - static_cast<unsigned>(value)); // INT_MIN stays negative, as in the original
    }

    char digits[32];
    snprintf(digits, sizeof(digits), "%d", value);

    char separator;
    switch (GameConfig::g_language) { // the game's language setting
    case 3:
        separator = ' ';
        break;
    case 4:
    case 5:
    case 6:
        separator = '.';
        break;
    default:
        separator = ',';
        break;
    }

    int remaining = static_cast<int>(strlen(digits));
    for (const char* digit = digits; remaining != 0;) {
        *out++ = *digit++;
        if (--remaining == 0)
            break;
        if (remaining % 3 == 0)
            *out++ = separator;
    }
    *out = '\0';
    return buffer;
}

// 0x00402120
int CompareCaseless(const char* a, const char* b)
{
    for (;; ++a, ++b) {
        char ca = Lower(*a), cb = Lower(*b);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (ca == '\0')
            return 0;
    }
}

// 0x004020E0
int DehexifyDigit(const char* digit)
{
    char c = *digit;
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'v')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'V')
        return c - 'A' + 10;
    return 0;
}

// 0x00402170
int NextWord(char* out, const char*& cursor)
{
    for (;;) {
        char c = *cursor;
        if (c == '\0') {
            *out = '\0';
            return 2;
        }
        if (c == ' ')
            break;
        if (c == '\\') {
            ++cursor;
            char escaped = *cursor;
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
            *out++ = c;
            ++cursor;
        }
    }
    // The terminating space is kept as part of the word.
    out[0] = *cursor++;
    out[1] = '\0';
    return 0;
}

} // namespace Str
