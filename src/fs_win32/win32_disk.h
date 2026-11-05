#pragma once

namespace fs {
class Disk;
} // namespace fs

// NOT ORIGINAL - Win32's fs::Disk (engine/file/disk.h; app/ installs it first): each method is the Win32 or C runtime
// call the game made at that place (CreateFileA, ReadFile, _fsopen, FindFirstFileA...), with the same arguments. The one
// place the game's code opens, reads, lists or removes files (tools/file_calls.test.ts) - but the logs, the ini and the
// crash reports, which stay in the platform layer.
namespace fs::win32 {

Disk& disk();

} // namespace fs::win32
