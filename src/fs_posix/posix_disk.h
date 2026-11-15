#pragma once

// NOT ORIGINAL - the macOS (arm64) port's fs::Disk (engine/file/disk.h), over POSIX: it stands in for
// src/fs_win32/win32_disk.h (CreateFileA, ReadFile, _fsopen, FindFirstFileA...) with open, pread, fopen, opendir,
// keeping the Win32 results the game reads. app/ installs it first, as it installs the Win32 one on Windows.
namespace fs {
class Disk;
} // namespace fs

namespace fs::posix {

Disk& disk();

} // namespace fs::posix
