#pragma once

#include <windows.h>

// NOT ORIGINAL - Win32's OVERLAPPED block (the control block of an asynchronous ReadFile) set for the next read: its
// offset is a member of an API union, so the block is made whole rather than its offset fields written.
namespace platform {

// `block` set to read from `offset` (its high word 0: the game's files are under 4 GB) with `event` (null: none);
// its status words kept as they are.
[[nodiscard]] inline OVERLAPPED read_from(const OVERLAPPED& block, DWORD offset, HANDLE event) noexcept
{
    return OVERLAPPED{block.Internal, block.InternalHigh, {{.Offset = offset, .OffsetHigh = 0}}, event};
}

} // namespace platform
