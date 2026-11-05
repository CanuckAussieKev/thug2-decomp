#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "base/core/language.h"

namespace file {
class AsyncFileHandle;
}
namespace script {
class Context;
class Struct;
} // namespace script

namespace lists {
class KeyMap;
}

// .pre archives ("pre files", .prx on PC): many data files packed into one, loaded into memory in one go.
// Files inside are found by the checksum of their name and read through the same FileLoad:: calls as files on
// disk (see engine/file/file_load.h), which offer every call to the pre manager first.
namespace pre {

// One loaded archive.
constexpr size_t kEntryNameSize = 128;   // an entry's name as the archive manager builds it
constexpr size_t kFileInfoBytes = 0x14;  // ArchiveFile::FileInfo
constexpr size_t kOpenCountOffset = 0x10; // ArchiveFile's layout
constexpr size_t kBottomUpHeapOffset = 0x14;
constexpr size_t kArchiveFileBytes = 0x18;
constexpr size_t kPendingNameSize = 64;
constexpr size_t kPendingLoadSlots = 8;
constexpr size_t kPresOffset = 0x10; // ArchiveManager's layout
constexpr size_t kHoldLoadsOffset = 0x14;
constexpr size_t kPendingOffset = 0x18;
constexpr size_t kQueuedCountOffset = 0x238;
constexpr size_t kManagerBytes = 0x23C;
constexpr size_t kScriptsArchiveNameLength = 11; // "scripts.pre"
constexpr int kFirstLetterLanguage = core::kLanguageFrench;  // the languages that name their scripts archive by a
constexpr int kLastLetterLanguage = core::kLanguageItalian; // letter: French to Italian
class ArchiveFile {
public:
    ArchiveFile(const ArchiveFile&) = delete;
    ArchiveFile& operator=(const ArchiveFile&) = delete;
    // One file inside the archive. Also the "file handle" the FileLoad:: calls get for it.
    struct FileInfo {
        int compressed_size_; // 0 if stored uncompressed
        uint8_t* data;        // inside the archive's block (compressed, or the file itself)
        uint8_t* unpacked;    // decompressed copy while the file is open, else null
        int position;         // read position while open
        int size;             // uncompressed size
    };

    // `block` is the whole archive as loaded, `size` bytes (the ArchiveFile owns it from now on; 0: not known -
    // the archive's own size word is the bound). `bottom_up` picks the heap that file information and
    // decompressed files are allocated from. The files are read through formats/pre_format.h: a damaged
    // archive keeps the files before the damage.
    ArchiveFile(uint8_t* block, size_t size, bool bottom_up);
    // Frees the block and all file information.
    ~ArchiveFile();

    // Copy (decompressing if needed) the file called `name` to `out`, or to a new memory::alloc_block block if `out`
    // is null. Sets `*size` to its size. Returns the buffer, or null if the file isn't in this archive.
    uint8_t* read_entry_from_archive(const char* name, int* size, uint8_t* out);
    // Make `handle` the active file and return its data, or null if it isn't one of this archive's files.
    uint8_t* find_entry_by_key(FileInfo* handle);
    // Open the file called `name`: make it active and decompress it if needed. Returns its handle or null.
    FileInfo* find_entry_in_archive(const char* name);
    // Read from / seek in the active file. Read returns the number of bytes copied; Seek returns 0, or -1
    // (position unchanged) if the new position would be outside the file. Origins: 0 start, 1 current, 2 end.
    int get_take_in(void* out, unsigned int count);
    // A seek: the offset and where it is from (SEEK_SET / CUR / END).
    struct SeekTo {
        int offset{};
        int origin{};
    };

    int seek_to(SeekTo seek);

    // The fields other code reads and sets.
    [[nodiscard]] lists::KeyMap* files() const { return files_; }
    [[nodiscard]] FileInfo* open_entry() const { return open_entry_; }
    int& open_count() { return open_count_; }
    [[nodiscard]] int open_count() const { return open_count_; }

private:
    uint8_t* block_;
    int entry_count_;
    lists::KeyMap* files_; // FileInfo by name checksum
    FileInfo* open_entry_;
    int open_count_;
    bool bottom_up_heap_;
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(ArchiveFile, open_count_) == kOpenCountOffset && offsetof(ArchiveFile, bottom_up_heap_) == kBottomUpHeapOffset &&
                          sizeof(ArchiveFile) == kArchiveFileBytes,
                      "layout must match the original");
    }
};

// The pre manager: every loaded archive, searched in turn when a file is opened. There is one, created and
// destroyed with reference counting (create_manager / destroy_manager).
class ArchiveManager {
public:
    ArchiveManager();

    // Load the archive pre\<name> (.prx on disk) and add it to the loaded archives, asynchronously if
    // `async` (finishing in finish_load). French and German use a localised archive (.prf/.prd) if there is
    // one, or patch the English one with a .ppf/.ppd. `report_missing` prints an error if the file isn't
    // there; `bottom_up` picks the heap. Does nothing while the script global block_pre_loading is set.
    void load_pre(const char* name, bool async, bool report_missing, bool bottom_up);

    // Load the file `name` from whichever archive has it (see ArchiveFile::read_entry_from_archive).
    uint8_t* ingest_file(const char* name, int* size, uint8_t* out);
    // True if some archive contains `name`.
    bool is_file_present(const char* name);
    // Open `name` from whichever archive has it and make it the active file. Language-specific script
    // archives ("...scripts.pre") are redirected to the current language's version.
    ArchiveFile::FileInfo* open_file(const char* name);
    // Make the file behind `handle` active and return its data, or null if no archive has it.
    uint8_t* find_entry_in_archive(ArchiveFile::FileInfo* handle);

    // Whether the archive `name` is loaded / still waiting to finish loading.
    bool is_loaded(const char* name);
    bool has_finished_loading(const char* name);
    // An archive's data has arrived for pending load `handle`: index it and add it to the loaded archives.
    // The third argument (probably the data's size) isn't used, but callers pass it.
    void finish_load(const file::AsyncFileHandle* handle, uint8_t* data, int size);
    // Unload the archive `name`. With `only_if_loaded` false the name is removed from the table even if no
    // archive is attached to it. Does nothing while pre loading is blocked (see LoadPre).
    void unload_pre(const char* name, bool only_if_loaded);

    struct PendingLoad {
        const file::AsyncFileHandle* handle;
        std::array<char, kPendingNameSize> name;
    };

    // The fields other code reads and sets.
    [[nodiscard]] ArchiveFile* open_archive() const { return open_archive_; }
    [[nodiscard]] lists::KeyMap* pres() const { return pres_; }
    [[nodiscard]] int queued_archive_count() const { return queued_archive_count_; }

private:
    ArchiveFile* open_archive_{};
    ArchiveFile::FileInfo* open_entry_key_{nullptr};
    uint8_t* open_entry_data_{nullptr};
    ArchiveFile::FileInfo* missing_handle_{nullptr}; // the last handle no archive had (a quick "no" next time)
    lists::KeyMap* pres_;                   // ArchiveFile by archive name checksum
    bool hold_archive_loads_{};
    std::array<PendingLoad, kPendingLoadSlots> pending_{};
    int queued_archive_count_{0};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(ArchiveManager, pres_) == kPresOffset && offsetof(ArchiveManager, hold_archive_loads_) == kHoldLoadsOffset &&
                          offsetof(ArchiveManager, pending_) == kPendingOffset &&
                          offsetof(ArchiveManager, queued_archive_count_) == kQueuedCountOffset && sizeof(ArchiveManager) == kManagerBytes,
                      "layout must match the original");
    }
};

// The pre manager, if one exists (set by its constructor), and the one create_manager made - the same object.
ArchiveManager* hub();
ArchiveManager* sole();
// Reference-counted creation: the first call with `create` makes the manager, the last destroy_manager
// deletes it. Returns the instance (null if there is none).
ArchiveManager* create_manager(bool create);
void destroy_manager();

// The archive side of the FileLoad:: calls. Each sets File's "handled" flag: whether the file was in an archive
// (if not, FileLoad:: goes to disk). `count_open` keeps the archive's count of open files.
bool is_present(const char* name);
// A file opened: its name and its fopen mode.
struct OpenRequest {
    const char* name{};
    const char* mode{};
};

ArchiveFile::FileInfo* unseal(OpenRequest open_request, bool count_open);
int shut(ArchiveFile::FileInfo* file, bool count_open);
int get_take_in(void* buffer, int size, int count, ArchiveFile::FileInfo* file);
int emit_out(const void* buffer, int size, int count, ArchiveFile::FileInfo* file);
int seek_to(ArchiveFile::FileInfo* file, int offset, int origin);
int get_file_size(ArchiveFile::FileInfo* file);

// Script functions (in the Function table)
bool cfunc_load_pre_file(script::Struct* params, script::Context context);
bool cfunc_unload_pre_file(script::Struct* params, script::Context context);
bool cfunc_is_load_pre_finished(script::Struct* params, script::Context context);
bool cfunc_all_load_pre_finished(script::Struct* params, script::Context context);
bool cfunc_wait_load_pre(script::Struct* params, script::Context context);
bool cfunc_wait_all_load_pre(script::Struct* params, script::Context context);
bool cfunc_in_pre_file(script::Struct* params, script::Context context);

} // namespace pre
