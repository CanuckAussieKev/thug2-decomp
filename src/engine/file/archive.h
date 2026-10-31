#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

namespace ListKit {
class KeyMap;
}

// .pre archives ("pre files", .prx on PC): many data files packed into one, loaded into memory in one go.
// Files inside are found by the checksum of their name and read through the same FileLoad:: calls as files on
// disk (see engine/file/file_load.h), which offer every call to the pre manager first.
namespace Pre {

// Decompress `size` bytes of LZSS data (the archive variant: 4 KB window in a global ring buffer) from `in`
// into `out`. A negative or absurd size (32 MB or more) hangs, as the original's assertion does.
void Decode(const uint8_t* in, uint8_t* out, int size);

// One loaded archive.
class ArchiveFile {
public:
    // One file inside the archive. Also the "file handle" the FileLoad:: calls get for it.
    struct FileInfo {
        int compressedSize;   // 0 if stored uncompressed
        uint8_t* data;        // inside the archive's block (compressed, or the file itself)
        uint8_t* unpacked;    // decompressed copy while the file is open, else null
        int position;         // read position while open
        int size;             // uncompressed size
    };

    // `block` is the whole archive as loaded (the ArchiveFile owns it from now on). `bottom_up` picks the heap
    // that file information and decompressed files are allocated from.
    ArchiveFile(uint8_t* block, bool bottom_up);
    // Frees the block and all file information.
    ~ArchiveFile();

    // Copy (decompressing if needed) the file called `name` to `out`, or to a new MemSys::AllocBlock block if `out`
    // is null. Sets `*size` to its size. Returns the buffer, or null if the file isn't in this archive.
    uint8_t* ReadEntryFromArchive(const char* name, int* size, uint8_t* out);
    // Make `handle` the active file and return its data, or null if it isn't one of this archive's files.
    uint8_t* FindEntryByKey(FileInfo* handle);
    // Open the file called `name`: make it active and decompress it if needed. Returns its handle or null.
    FileInfo* FindEntryInArchive(const char* name);
    // Read from / seek in the active file. Read returns the number of bytes copied; Seek returns 0, or -1
    // (position unchanged) if the new position would be outside the file. Origins: 0 start, 1 current, 2 end.
    int TakeIn(void* out, unsigned int count);
    int SeekTo(int offset, int origin);

    uint8_t* m_block;
    int m_entryCount;
    ListKit::KeyMap* m_files; // FileInfo by name checksum
    FileInfo* m_openEntry;
    int m_openCount;
    bool m_bottomUpHeap;
};

// The pre manager: every loaded archive, searched in turn when a file is opened. There is one, created and
// destroyed with reference counting (CreateManager / DestroyManager).
class ArchiveManager {
public:
    ArchiveManager();

    // Load the archive pre\<name> (.prx on disk) and add it to the loaded archives, asynchronously if
    // `async` (finishing in FinishLoad). French and German use a localised archive (.prf/.prd) if there is
    // one, or patch the English one with a .ppf/.ppd. `report_missing` prints an error if the file isn't
    // there; `bottom_up` picks the heap. Does nothing while the script global block_pre_loading is set.
    void LoadPre(const char* name, bool async, bool report_missing, bool bottom_up);

    // Load the file `name` from whichever archive has it (see ArchiveFile::ReadEntryFromArchive).
    uint8_t* IngestFile(const char* name, int* size, uint8_t* out);
    // True if some archive contains `name`.
    bool IsFilePresent(const char* name);
    // Open `name` from whichever archive has it and make it the active file. Language-specific script
    // archives ("...scripts.pre") are redirected to the current language's version.
    ArchiveFile::FileInfo* OpenFile(const char* name);
    // Make the file behind `handle` active and return its data, or null if no archive has it.
    uint8_t* FindEntryInArchive(ArchiveFile::FileInfo* handle);

    // Whether the archive `name` is loaded / still waiting to finish loading.
    bool IsLoaded(const char* name);
    bool HasFinishedLoading(const char* name);
    // An archive's data has arrived for pending load `handle`: index it and add it to the loaded archives.
    // The third argument (probably the data's size) isn't used, but callers pass it.
    void FinishLoad(int handle, uint8_t* data, int size);
    // Unload the archive `name`. With `only_if_loaded` false the name is removed from the table even if no
    // archive is attached to it. Does nothing while pre loading is blocked (see LoadPre).
    void UnloadPre(const char* name, bool only_if_loaded);

    struct PendingLoad {
        int handle;
        char name[64];
    };

    ArchiveFile* m_openArchive;
    ArchiveFile::FileInfo* m_openEntryKey;
    uint8_t* m_openEntryData;
    ArchiveFile::FileInfo* m_missingHandle; // the last handle no archive had (a quick "no" next time)
    ListKit::KeyMap* m_pres;             // ArchiveFile by archive name checksum
    bool m_holdArchiveLoads;
    PendingLoad m_pending[8];
    int m_queuedArchiveCount;
};

// The pre manager, if one exists (set by its constructor), and the one CreateManager made - the same object.
ArchiveManager* Hub();
ArchiveManager* Sole();
// Reference-counted creation: the first call with `create` makes the manager, the last DestroyManager
// deletes it. Returns the instance (null if there is none).
ArchiveManager* CreateManager(bool create);
void DestroyManager();

// The archive side of the FileLoad:: calls. Each sets File's "handled" flag: whether the file was in an archive
// (if not, FileLoad:: goes to disk). `count_open` keeps the archive's count of open files.
bool IsPresent(const char* name);
ArchiveFile::FileInfo* Unseal(const char* name, const char* mode, bool count_open);
int Shut(ArchiveFile::FileInfo* file, bool count_open);
int TakeIn(void* buffer, int size, int count, ArchiveFile::FileInfo* file);
int EmitOut(const void* buffer, int size, int count, ArchiveFile::FileInfo* file);
int SeekTo(ArchiveFile::FileInfo* file, int offset, int origin);
int GetFileSize(ArchiveFile::FileInfo* file);

// Script functions (in the CFunction table)
bool __cdecl CFunc_LoadPreFile(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_UnloadPreFile(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_IsLoadPreFinished(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_AllLoadPreFinished(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_WaitLoadPre(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_WaitAllLoadPre(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_InPreFile(Scripts::CStruct* params, Scripts::CScript* script);

} // namespace Pre
