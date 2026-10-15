#include "game/core/mem.h"

#include <cstring>

#include "hook/hook.h"

namespace Mem {

// 0x00401C90
void* Clear(unsigned int size, void* dest)
{
    if (dest)
        std::memset(dest, 0, size);
    return dest;
}

} // namespace Mem

REPLACE(0x00401C90, Mem::Clear);
