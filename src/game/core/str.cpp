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

} // namespace Str

REPLACE(0x00402080, Str::LowerCase);
REPLACE(0x004020B0, Str::UpperCase);
