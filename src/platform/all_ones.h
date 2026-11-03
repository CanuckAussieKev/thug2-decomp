#pragma once

#include <cstdint>

#include "engine/core/pointer_cell.h"

// NOT ORIGINAL - the pointer whose 32 bits are all ones (x86): Win32's INVALID_HANDLE_VALUE, and the marker the
// game's list heads hold (memory filled with 0xFF bytes reads as it, so it can't be an object of ours).
namespace platform {

constexpr uint32_t kAllOnes = 0xFFFFFFFFU;

template <typename T = void>
T* all_ones_pointer() noexcept
{
    return cell::pointer<T>(kAllOnes);
}

} // namespace platform
