#include "engine/file/packs.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checksums.h"
#include "base/core/dbg.h"
#include "engine/memory/mem.h"
#include "engine/file/async.h"
#include "engine/file/file_load.h"
#include "engine/file/archive.h"
#include "engine/file/archive_folder.h"
#include "engine/script/names.h"
#include "formats/pre_format.h"
#include "engine/file/file_system.h"
#include "engine/script/params.h"
#include "engine/script/context.h"
#include "engine/sys/game_config.h"
#include "formats/byte_stream.h"
#include "formats/lzss.h"
#include "engine/gfx/loading_console.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/language.h"

namespace packs {

namespace {
constexpr size_t kAlignMask = 0xF;      // 16-byte alignment
constexpr size_t kPrefixPadding = 0x10; // a block's name is padded to 16 bytes with room for its NUL
constexpr int kSectorMask = 0x7FF;      // buffers are whole 2 KB sectors
constexpr unsigned int kSectorMaskUnsigned = 0x7FFU;
constexpr size_t kPackPathSize = 500;
constexpr int kUsagePauseSpins = 100000; // the original's pause after each usage line

constexpr int kMaxLoadedFiles = 200;
constexpr int kPreloadCapacity = 100;

std::array<LoadedFile, kMaxLoadedFiles>& loaded_files()
{
    static std::array<LoadedFile, kMaxLoadedFiles> loaded_files{};
    return loaded_files;
}

void* allocate_file_memory(unsigned int size) { return memory::alloc_block(size); }
void shrink_file_memory(unsigned int size, void* memory) { memory::trim_block_end(size, memory); }
void free_file_memory(void* memory) { memory::free_malloc(memory); }
// A block from allocate_file_memory, owned.
struct FileMemoryFree {
    void operator()(uint8_t* memory) const noexcept { free_file_memory(memory); }
};
// `size` rounded up to 16 bytes (each packed file's data starts 16-byte aligned).
constexpr size_t aligned16(size_t size) { return (size + kAlignMask) & ~static_cast<size_t>(kAlignMask); }

// A loaded .pre archive: its name, its files' data in one block, each file's record.
struct LoadedArchive {
    std::string name;
    std::unique_ptr<uint8_t, FileMemoryFree> block;
    std::vector<PackedFile> files;
};
std::array<std::unique_ptr<LoadedArchive>, kPreloadCapacity>& archives()
{
    static std::array<std::unique_ptr<LoadedArchive>, kPreloadCapacity> archives{};
    return archives;
}

// Buffers are allocated in whole 2 KB sectors.
unsigned int sector_aligned(unsigned int size) { return (size + kSectorMask) & ~kSectorMaskUnsigned; }

// The pre manager instance finds files inside .pre archives registered with it. (Like the original, this
// assumes it exists.)
void* pre_manager_load_file(const char* name, int* size) { return pre::sole()->ingest_file(name, size, nullptr); }

} // namespace

// 0x005B7180
PackedFile* find_pre_entry(uint32_t checksum)
{
    for (const std::unique_ptr<LoadedArchive>& archive : archives()) {
        if (!archive) {
            continue;
        }
        for (PackedFile& file : archive->files) {
            if (file.checksum == checksum) {
                return &file;
            }
        }
    }
    return nullptr;
}

// 0x005B7C00 (PartyMod's script patching wraps calls to this, and calls it by address)
void* read_in(const char* name, bool async, file::IoCompletionHook callback, void* hook_user_a, void* hook_user_b)
{
    const uint32_t checksum = checksums::hash_from_text(name);
    // Not original - the data overlay (engine/file/data_overlay.h): a file of the data overlay is read from it,
    // before any archive (synchronously, through FileLoad::unseal, which opens the overlay's file).
    // Original behaviour: archives first, then the cache and the data folder.
    std::array<char, file::kOverlayPathSize> overlay_path{};
    const bool overlay = fs::file_system().loose_override(name, overlay_path);

    // Already loaded?
    if (PackedFile* entry = overlay ? nullptr : find_pre_entry(checksum)) {
        ++entry->use_count_;
        return entry->data;
    }
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        LoadedFile& file = core::at(loaded_files(), i);
        if (file.data && file.checksum == checksum) {
            ++file.ref_count_;
            return file.data;
        }
    }

    // The original assumes a free slot exists.
    LoadedFile* slot = nullptr;
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        if (!core::at(loaded_files(), i).data) {
            slot = &core::at(loaded_files(), i);
            break;
        }
    }

    void* data = nullptr;
    int size = 0;
    if (async && !overlay) {
        file::AsyncFileRef* file = file::async_open(name);
        size = file::async_get_file_size(file);
        data = allocate_file_memory(sector_aligned(static_cast<unsigned int>(size)));
        if (callback) {
            file::async_set_callback_mask(file, 1U << file::kOpReadFile); // call back when the read completes
            file::async_set_callback(file, callback, hook_user_a, hook_user_b);
        }
        file::start_background_read(file, data, size);
        file::async_close(file);
        if (data) {
            shrink_file_memory(static_cast<unsigned int>(size), data);
        }
        gfx::loading_console::note_file("read (async)", name, size); // (not original: the loading console)
    } else {
        size = 0;
        data = overlay ? nullptr : pre_manager_load_file(name, &size);
        if (data) {
            gfx::loading_console::note_file("unpack", name, size); // (not original: the loading console)
        }
        if (!data) {
            size = overlay ? 0 : file::cached_file_size(name);
            if (size != 0) {
                data = allocate_file_memory(sector_aligned(static_cast<unsigned int>(size)));
                if (!file::load_cached_file(name, data)) {
                    free_file_memory(data);
                    data = nullptr;
                }
                gfx::loading_console::note_file("read (cache)", name, size); // (not original: the loading console)
            } else {
                file::Hdl file = file::unseal(name, "rb");
                size = static_cast<int>(file::get_file_size(file));
                data = allocate_file_memory(sector_aligned(static_cast<unsigned int>(size)));
                file::get_take_in(data, 1, size, file);
                file::shut(file);
            }
            if (data) {
                shrink_file_memory(static_cast<unsigned int>(size), data);
            }
        }
    }

    *slot = {.data = data, .checksum = checksum, .size = size, .ref_count_ = 1};
    return data;
}

// 0x005B7B00: load `name` into `buffer` (or a new block if null), from a .pre archive or from disk, and set
// `*size` (if given) to its size. Returns the data, or null if it couldn't be loaded.
void* load_file_into(const char* name, int* size, void* buffer)
{
    int file_size = 0;
    // Not original - the data overlay: a file of it is read from there (FileLoad::unseal), not an archive.
    // Original behaviour: archives first, then the cache and the data folder.
    std::array<char, file::kOverlayPathSize> overlay_path{};
    const bool overlay = fs::file_system().loose_override(name, overlay_path);
    void* data = overlay ? nullptr : pre::sole()->ingest_file(name, &file_size, static_cast<uint8_t*>(buffer));
    if (data) {
        gfx::loading_console::note_file("unpack", name, file_size); // (not original: the loading console)
    }
    if (!data) {
        file_size = overlay ? 0 : file::cached_file_size(name);
        if (file_size != 0) {
            data = buffer ? buffer : allocate_file_memory(sector_aligned(static_cast<unsigned int>(file_size)));
            if (!file::load_cached_file(name, data)) {
                if (!buffer) {
                    free_file_memory(data);
                }
                data = nullptr;
            }
        } else {
            file::Hdl file = file::unseal(name, "rb");
            file_size = static_cast<int>(file::get_file_size(file));
            data = buffer ? buffer : allocate_file_memory(sector_aligned(static_cast<unsigned int>(file_size)));
            file::get_take_in(data, 1, file_size, file);
            file::shut(file);
        }
        if (!buffer && data) {
            shrink_file_memory(static_cast<unsigned int>(file_size), data);
        }
    }
    if (size) {
        *size = file_size;
    }
    return data;
}

namespace {

// Whether the archive named is loaded already.
bool archive_loaded(const char* name)
{
    return std::ranges::any_of(archives(), [name](const std::unique_ptr<LoadedArchive>& archive) {
        return archive && _stricmp(archive->name.c_str(), name) == 0;
    });
}

// The first free archive slot (-1: none).
int free_archive_slot()
{
    int slot = -1;
    for (int i = 0; i < kPreloadCapacity; ++i) {
        if (!core::at(archives(), i)) {
            slot = i;
            break;
        }
    }
    return slot;
}

} // namespace

// 0x005B75A0
void load_pre(const char* name)
{
    if (archive_loaded(name)) {
        return; // already loaded
    }
    const int slot = free_archive_slot();

    std::array<char, kPackPathSize> path{};
    text::format_to(path, "pre\\%s", name);
    // The block starts with the archive's name, padded to 16 bytes.
    auto prefix = static_cast<unsigned int>((std::strlen(name) + kPrefixPadding) & ~static_cast<size_t>(kAlignMask));
    const size_t length = std::strlen(path.data());

    // French and German: a complete localised archive (.prf/.prd) if there is one, else a patch for the
    // English archive (.ppf/.ppd).
    uint8_t* patch = nullptr;
    uint32_t patch_size = 0;
    pre_format::PatchHeader patch_header;
    bool patched = false;
    uint8_t* block = nullptr;
    unsigned int file_size = 0;
    unsigned int allocated = 0;
    size_t archive_size = 0; // (not original: the archive's bytes in the block, the bound for its reader)
    bool loaded = false;
    char letter = '\0'; // (the French and German packs' letter)
    if (game_config::get_language() == core::kLanguageFrench) {
        letter = 'f';
    } else if (game_config::get_language() == core::kLanguageGerman) {
        letter = 'd';
    }
    // NOT ORIGINAL - an archive unpacked into a folder (archive_folder.h): built from it, read here as the archive
    if (std::vector<uint8_t> image; archive_folder::build(path.data(), game_config::get_language(), &image)) {
        archive_size = image.size();
        file_size = static_cast<unsigned int>(image.size());
        allocated = ((file_size + kSectorMask) & ~kSectorMaskUnsigned) + prefix;
        block = static_cast<uint8_t*>(allocate_file_memory(allocated));
        std::memcpy(block, name, std::strlen(name) + 1);
        std::memcpy(std::span<uint8_t>(block, allocated).subspan(prefix).data(), image.data(), image.size());
        loaded = true;
        letter = '\0';
    }
    if (letter != '\0') {
        core::at(path, length - 1) = letter;
        if (!file::is_present(path.data())) {
            core::at(path, length - 2) = 'p';
            if (file::is_present(path.data())) {
                file::Hdl file = file::unseal(path.data(), "rb");
                file_size = file::get_file_size(file);
                patch_size = file_size;
                patch = static_cast<uint8_t*>(memory::alloc(file_size));
                file::get_take_in(patch, 1, static_cast<int>(file_size), file);
                file::shut(file);
                core::at(path, length - 2) = 'r';
                core::at(path, length - 1) = 'e';
                patched = true;
                if (file_size != 0) {
                    // A leftover from the console versions' disc cache: always fails on PC, and the original
                    // then carries on patching through a null block - it crashes here. (Patch files aren't
                    // shipped with the PC version.)
                    allocated = ((file_size + kSectorMask) & ~kSectorMaskUnsigned) + prefix;
                    block = static_cast<uint8_t*>(allocate_file_memory(allocated));
                    std::memcpy(block, name, std::strlen(name) + 1);
                    if (!file::load_cached_file(path.data(),
                                                std::span<uint8_t>(block, allocated).subspan(prefix).data())) {
                        free_file_memory(block);
                        block = nullptr;
                    }
                    loaded = true;
                }
            } else {
                core::at(path, length - 2) = 'r';
                core::at(path, length - 1) = 'e';
            }
        }
    }

    if (!loaded) {
        file::Hdl file = file::unseal(path.data(), "rb");
        file_size = file::get_file_size(file);
        util::ByteReader patch_reader(patch, patch_size);
        if (patched && !pre_format::read_patch_header(patch_reader, &patch_header)) {
            patch_header = {};
        }
        const unsigned int needed =
            patched && patch_header.patched_size >= file_size ? patch_header.patched_size : file_size;
        archive_size = needed;
        allocated = ((needed + kSectorMask) & ~kSectorMaskUnsigned) + prefix;
        block = static_cast<uint8_t*>(allocate_file_memory(allocated));
        std::memcpy(block, name, std::strlen(name) + 1);
        file::get_take_in(std::span<uint8_t>(block, allocated).subspan(prefix).data(), 1, static_cast<int>(file_size),
                          file);
        file::shut(file);
    }

    if (!block) {
        // (the console leftover above: the original carried on through the null block and crashed)
        if (patch) {
            memory::dealloc(patch);
        }
        return;
    }
    if (patched) {
        const std::span<uint8_t> data = std::span<uint8_t>(block, allocated).subspan(prefix);
        if (allocated > file_size) {
            std::memset(data.subspan(file_size).data(), 0, allocated - file_size - prefix);
        }
        // Not original - bounded by the block and the patch's size (the original wrote each record where it said).
        if (!pre_format::apply_patch(data.data(), allocated - prefix, patch, patch_size)) {
            debug::printf("%s: a damaged patch\n", path.data());
        }
        memory::dealloc(patch);
    }

    // Unpacked: every file's data uncompressed and 16-byte aligned in one block, so files can be used in place;
    // first the block's size (plus the original's slack), then the copies. Not original - the archive as loaded is
    // read through pre_format's reader, bounded by its size: a damaged archive keeps the files before the damage
    // (the original walked and copied on past the block).
    util::ByteReader archive(std::span<uint8_t>(block, allocated).subspan(prefix).data(), archive_size);
    pre_format::ArchiveHeader old_header;
    if (!pre_format::read_archive_header(archive, &old_header)) {
        old_header = {};
    }
    auto loaded_archive = std::make_unique<LoadedArchive>();
    loaded_archive->name = name;
    size_t data_size = 0;
    {
        util::ByteReader sizing = archive;
        for (int file_index = 0; file_index < old_header.file_count; ++file_index) {
            pre_format::FileRecord record;
            if (!pre_format::read_file_record(sizing, &record)) {
                debug::printf("%s: damaged at file %d of %d\n", path.data(), file_index, old_header.file_count);
                break;
            }
            data_size += aligned16(static_cast<size_t>(record.size));
            PackedFile file;
            file.checksum = record.checksum;
            file.size = record.size;
            file.name.assign(record.name, std::strlen(record.name));
            loaded_archive->files.push_back(std::move(file));
        }
    }
    const size_t allocation = data_size + 0x10 + 0xC00;
    loaded_archive->block.reset(static_cast<uint8_t*>(allocate_file_memory(static_cast<unsigned int>(allocation))));
    void* aligned = loaded_archive->block.get();
    size_t space = allocation;
    const std::span<uint8_t> data(static_cast<uint8_t*>(std::align(0x10, data_size, aligned, space)), data_size);
    size_t offset = 0;
    for (PackedFile& file : loaded_archive->files) {
        pre_format::FileRecord from;
        pre_format::read_file_record(archive, &from); // (read once already)
        const std::span<uint8_t> target = data.subspan(offset, aligned16(static_cast<size_t>(file.size)));
        file.data = target.data();
        if (from.compressed_size != 0) {
            // Not original - unpacked bounded by the file's size (lzss::unpack: decode's bytes for a valid file).
            size_t written = 0;
            if (!lzss::unpack(from.data, static_cast<size_t>(from.compressed_size), target.data(),
                              static_cast<size_t>(from.size), &written)) {
                debug::printf("%s: %s is damaged\n", path.data(), from.name);
            }
            std::ranges::fill(target.subspan(written), uint8_t{0});
        } else {
            std::memcpy(target.data(), from.data, static_cast<size_t>(from.size));
            std::ranges::fill(target.subspan(static_cast<size_t>(from.size)), uint8_t{0});
        }
        offset += target.size();
    }
    free_file_memory(block);
    if (slot < 0) {
        // Not original - with all 100 slots taken the original wrote to slot -1 (the last loaded file's reference
        // count, past the start of its table); the archive isn't kept.
        debug::printf("%s: no room for another archive\n", path.data());
        return;
    }
    core::at(archives(), slot) = std::move(loaded_archive);
}

// --- script functions -----------------------------------------------------------------------------------

namespace {

constexpr uint32_t kHeapParam = 0x42D6DD1E;   // LoadPipPre ... heap = <name>
constexpr uint32_t kHeapTopDown = 0x477FC6DE; // the two heap names it accepts
constexpr uint32_t kHeapBottomUp = 0xC80BF12D;

const char* pre_name(script::Struct* params)
{
    const char* name = nullptr;
    script::fetch_text(params, 0, &name, true);
    return name;
}

} // namespace

// 0x005B7A60: LoadPipPre <name> [heap = ...]
bool cfunc_load_pip_pre(script::Struct* params, script::Context /*context*/)
{
    const char* name = pre_name(params);
    uint32_t heap = 0;
    script::read_hash_value(params, kHeapParam, &heap, false);
    if (heap == kHeapTopDown) {
        memory::enter_heap_scope(memory::top_down_heap());
    } else if (heap == kHeapBottomUp) {
        memory::enter_heap_scope(memory::bottom_up_heap());
    } else {
        heap = 0;
    }
    load_pre(name);
    if (heap != 0U) {
        memory::leave_heap_scope();
    }
    return true;
}

// 0x005B7410: UnLoadPipPre <name>
bool cfunc_un_load_pip_pre(script::Struct* params, script::Context /*context*/) { return unload_pre(pre_name(params)); }

// 0x005B7430: DumpPipPreStatus [ShowPreedFiles] [ShowUnPreedFiles] [ShowOnlyOpenFiles] - debug output
bool cfunc_dump_pip_pre_status(script::Struct* params, script::Context /*context*/)
{
    const bool show_files = script::has_bare_flag(params, "ShowPreedFiles");
    const bool show_loose_files = script::has_bare_flag(params, "ShowUnPreedFiles");
    const bool only_open = script::has_bare_flag(params, "ShowOnlyOpenFiles");
    debug::printf("Currently loaded pre files:\n");
    for (const std::unique_ptr<LoadedArchive>& archive : archives()) {
        if (!archive) {
            continue;
        }
        debug::printf("### %s ###\n", archive->name.c_str());
        if (!show_files) {
            continue;
        }
        for (const PackedFile& entry : archive->files) {
            if (!only_open || entry.use_count_ != 0) {
                debug::printf("Usage:%d  File: %s\n", entry.use_count_, entry.name.c_str());
                for (volatile int wait = 0; wait < kUsagePauseSpins; ++wait) {
                    // the original pauses after each line (presumably so the debug output keeps up)
                }
            }
        }
    }
    if (show_loose_files) {
        debug::printf("Currently loaded un-preed files:\n");
        for (int i = 0; i < kMaxLoadedFiles; ++i) {
            const LoadedFile& file = core::at(loaded_files(), i);
            if (file.data) {
                debug::printf("Usage:%d  File: %s\n", file.ref_count_,
                              script::find_checksum_name_or_unknown(file.checksum));
            }
        }
    }
    return true;
}

// 0x005B7200
void offload(uint32_t checksum)
{
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        LoadedFile& file = core::at(loaded_files(), i);
        if (!file.data || file.checksum != checksum) {
            continue;
        }
        if (file.ref_count_ != 0) {
            --file.ref_count_;
        }
        if (file.ref_count_ == 0) {
            free_file_memory(file.data);
            file = {};
        }
    }
    if (PackedFile* entry = find_pre_entry(checksum)) {
        if (entry->use_count_ != 0) {
            --entry->use_count_;
        }
    }
}

// 0x005B7270
void offload(const char* name) { offload(checksums::hash_from_text(name)); }

// 0x005B7290
int get_file_size(uint32_t checksum)
{
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        const LoadedFile& file = core::at(loaded_files(), i);
        if (file.data && file.checksum == checksum) {
            return file.size;
        }
    }
    const PackedFile* entry = find_pre_entry(checksum);
    return entry ? entry->size : 0;
}

// 0x005B72E0
int get_file_size(const char* name) { return get_file_size(checksums::hash_from_text(name)); }

// 0x005B70F0
FileTables::FileTables()
{
    for (std::unique_ptr<LoadedArchive>& archive : archives()) {
        archive.reset();
    }
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        core::at(loaded_files(), i) = {};
    }
}

// 0x005B7130
bool unload_pre(const char* name)
{
    bool unloaded = false;
    for (std::unique_ptr<LoadedArchive>& archive : archives()) {
        if (archive && _stricmp(archive->name.c_str(), name) == 0) {
            archive.reset();
            unloaded = true;
        }
    }
    return unloaded;
}

// 0x005B7300
const char* take_next_finished_archive(const char* previous)
{
    bool found_previous = previous == nullptr;
    for (const std::unique_ptr<LoadedArchive>& archive : archives()) {
        if (!archive) {
            continue;
        }
        if (found_previous) {
            return archive->name.c_str();
        }
        if (_stricmp(archive->name.c_str(), previous) == 0) {
            found_previous = true;
        }
    }
    return nullptr;
}

// 0x005B7370
bool archive_is_busy(const char* name)
{
    for (const std::unique_ptr<LoadedArchive>& archive : archives()) {
        if (!archive || _stricmp(archive->name.c_str(), name) != 0) {
            continue;
        }
        if (std::ranges::any_of(archive->files, [](const PackedFile& file) { return file.use_count_ != 0; })) {
            return true;
        }
    }
    return false;
}

} // namespace packs
