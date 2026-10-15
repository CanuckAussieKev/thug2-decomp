#pragma once

#include <cstdint>

// A variable that lives in the exe's data section. Until every function that uses it has been rewritten,
// the original code and ours must share the same storage, so we refer to it by address:
//
//   static auto& s_nextId = GameVariable<uint32_t>(0x00690FA0);
template <typename T>
T& GameVariable(uintptr_t address)
{
    return *reinterpret_cast<T*>(address);
}
