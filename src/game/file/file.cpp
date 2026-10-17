#include "game/file/file.h"

#include <windows.h>

#include <cstring>

#include "game/core/dbg.h"
#include "game/file/pre.h"
#include "game/front/menu_level.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace File {

namespace {

// While a pre manager exists, each file call goes to it first; it sets the "handled" flag when the file was
// in one of its archives.
bool ArchivesActive() { return Pre::Manager() != nullptr; }
bool& HandledByArchive() { return GameVariable<bool>(0x007D0FC8); }

Pre::PreFile::FileInfo* AsArchiveFile(Handle file) { return static_cast<Pre::PreFile::FileInfo*>(file); }
bool ArchiveExist(const char* name) { return Pre::Exist(name); }
Handle ArchiveOpen(const char* name, const char* mode) { return Pre::Open(name, mode, false); }
int ArchiveClose(Handle file) { return Pre::Close(AsArchiveFile(file), false); }
int ArchiveRead(void* buffer, int size, int count, Handle file)
{
    return Pre::Read(buffer, size, count, AsArchiveFile(file));
}
int ArchiveWrite(const void* buffer, int size, int count, Handle file)
{
    return Pre::Write(buffer, size, count, AsArchiveFile(file));
}
uint32_t ArchiveSeek(Handle file, int offset, int origin)
{
    return static_cast<uint32_t>(Pre::Seek(AsArchiveFile(file), offset, origin));
}
uint32_t ArchiveGetFileSize(Handle file) { return static_cast<uint32_t>(Pre::GetFileSize(AsArchiveFile(file))); }

// Shows the fatal file error screen; never returns.
[[noreturn]] void FatalFileError(DWORD error)
{
    GameFunction<void(__cdecl*)(DWORD)>(0x004D89C0)(error);
    __assume(0);
}

// Never set anywhere in the exe: a leftover that would open files from another drive.
bool UseAlternateDrive() { return GameVariable<int>(0x007D0FD8) != 0; }

} // namespace

void MakeDiskPath(char (&path)[256], const char* name)
{
    std::memcpy(path, "data\\", 6);
    // Paths written for the original development machine.
    if (name[0] == 'c' && name[1] == ':')
        name += 15;
    int length = 5;
    for (; *name && length < 255; ++name)
        path[length++] = *name == '/' ? '\\' : *name;
    path[length] = '\0';

    char* end = path + length;
    char last = end[-1];
    if (last == 'x' && end[-2] == 'e' && end[-3] == 't')
        end[-2] = 'x'; // .tex -> .txx
    if (last == 'e' && end[-2] == 'r' && end[-3] == 'p')
        end[-1] = 'x'; // .pre -> .prx
}

// 0x005B9CB0 (takes `name` in EDX)
Handle OpenOnDisk(const char* name)
{
    char path[256];
    MakeDiskPath(path, name);

    HANDLE file = INVALID_HANDLE_VALUE;
    if (UseAlternateDrive()) {
        path[0] = 'Z';
        file = CreateFileA(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (file == INVALID_HANDLE_VALUE)
        file = CreateFileA(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
        return file;

    DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND) {
        Dbg::Printf("FatalFileError: %x %s\n", error, path);
        FatalFileError(static_cast<DWORD>(-1));
    }
    return nullptr;
}

// 0x005B9E60
Handle Open(const char* name, const char* mode)
{
    // Not original - the menu_level feature (game/front/menu_level.h): a file of the data overlay is opened from it,
    // before any archive (its handle isn't an archive's: Read, Seek, Close... fall through to the disk's).
    // Original behaviour: archives first, then the data folder.
    char overlay[260];
    if (Front::MenuLevel::OverlayPath(name, overlay)) {
        HANDLE file = CreateFileA(overlay, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file != INVALID_HANDLE_VALUE)
            return file;
    }
    if (ArchivesActive()) {
        Handle file = ArchiveOpen(name, mode);
        if (HandledByArchive())
            return file;
    }
    return OpenOnDisk(name);
}

// 0x005B9EA0
int Close(Handle file)
{
    if (ArchivesActive()) {
        int result = ArchiveClose(file);
        if (HandledByArchive())
            return result;
    }
    CloseHandle(file);
    return 0;
}

// 0x005B9EE0
int Read(void* buffer, int size, int count, Handle file)
{
    if (ArchivesActive()) {
        int result = ArchiveRead(buffer, size, count, file);
        if (HandledByArchive())
            return result;
    }
    DWORD bytes_read;
    if (!ReadFile(file, buffer, static_cast<DWORD>(size * count), &bytes_read, nullptr)) {
        DWORD error = GetLastError();
        if (error != ERROR_HANDLE_EOF)
            FatalFileError(error);
    }
    return static_cast<int>(bytes_read);
}

// 0x005B9F60
int Write(const void* buffer, int size, int count, Handle file)
{
    if (ArchivesActive()) {
        int result = ArchiveWrite(buffer, size, count, file);
        if (HandledByArchive())
            return result;
    }
    return 0;
}

// 0x005B9FA0
uint32_t Seek(Handle file, int offset, int origin)
{
    if (ArchivesActive()) {
        uint32_t result = ArchiveSeek(file, offset, origin);
        if (HandledByArchive())
            return result;
    }
    DWORD method = origin == 1 ? FILE_CURRENT : origin != 0 ? FILE_END : FILE_BEGIN;
    return SetFilePointer(file, offset, nullptr, method);
}

// 0x005B9E00
uint32_t GetFileSize(Handle file)
{
    if (ArchivesActive()) {
        uint32_t result = ArchiveGetFileSize(file);
        if (HandledByArchive())
            return result;
    }
    DWORD high = 0;
    return ::GetFileSize(file, &high);
}

// 0x005BA010
bool Exist(const char* name)
{
    // Not original - the menu_level feature: the data overlay's files exist.
    // Original behaviour: archives first, then the data folder.
    char overlay[260];
    if (Front::MenuLevel::OverlayPath(name, overlay))
        return true;
    if (ArchivesActive()) {
        bool result = ArchiveExist(name);
        if (HandledByArchive())
            return result;
    }
    Handle file = OpenOnDisk(name);
    if (file) {
        bool closed_by_archive = false;
        if (ArchivesActive()) {
            ArchiveClose(file);
            closed_by_archive = HandledByArchive();
        }
        if (!closed_by_archive)
            CloseHandle(file);
    }
    return file != nullptr;
}

} // namespace File

namespace {

// 0x005B8040 / 0x005B8050: one-line accessors for the archive manager and the "handled" flag.
bool __cdecl ArchivesActiveAccessor() { return Pre::Manager() != nullptr; }
bool __cdecl HandledByArchiveAccessor() { return GameVariable<bool>(0x007D0FC8); }

File::Handle __cdecl OpenOnDiskFromEdx(const char* name) { return File::OpenOnDisk(name); }

// The original takes its argument in EDX.
__declspec(naked) void OpenOnDiskAdapter()
{
    __asm {
        push edx
        call OpenOnDiskFromEdx
        add esp, 4
        ret
    }
}

} // namespace

REPLACE(0x005B8040, ArchivesActiveAccessor);
REPLACE(0x005B8050, HandledByArchiveAccessor);
REPLACE_UNCHECKED(0x005B9CB0, OpenOnDiskAdapter); // argument in EDX
REPLACE(0x005B9E60, File::Open);
REPLACE(0x005B9EA0, File::Close);
REPLACE(0x005B9EE0, File::Read);
REPLACE(0x005B9F60, File::Write);
REPLACE(0x005B9FA0, File::Seek);
REPLACE(0x005B9E00, File::GetFileSize);
REPLACE(0x005BA010, File::Exist);
