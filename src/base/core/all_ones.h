#pragma once

#include <cstdint>

#include "base/core/address.h"

// NOT ORIGINAL - the pointer whose bits are all ones (32 on x86, 64 on x64): Win32's INVALID_HANDLE_VALUE, and the
// marker the game's list heads hold (memory filled with 0xFF bytes reads as it, so it can't be an object of ours).
namespace platform {

constexpr uintptr_t kAllOnes = ~uintptr_t{0};

template <typename T = void>
T* all_ones_pointer() noexcept
{
    return core::at_address<T>(kAllOnes);
}

} // namespace platform
