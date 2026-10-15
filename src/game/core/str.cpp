#include "game/core/str.h"

#include "hook/hook.h"

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

} // namespace Str

REPLACE(0x00401FF0, Str::FindCaseless);
REPLACE(0x00402080, Str::LowerCase);
REPLACE(0x004020B0, Str::UpperCase);
