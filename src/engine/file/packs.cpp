#include "engine/file/packs.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/file/async.h"
#include "engine/file/file_load.h"
#include "engine/file/archive.h"
#include "frontend/data_overlay.h"
#include "engine/script/params.h"
#include "engine/sys/game_config.h"
#include "engine/util/lzss.h"
#include "engine/gfx/loading_console.h"

namespace packs {

namespace {

constexpr int kMaxLoadedFiles = 200;
constexpr int kPreloadCapacity = 100;

LoadedFile g_loaded_files[kMaxLoadedFiles];
char* g_pre_files[kPreloadCapacity];

LoadedFile* loaded_files() { return g_loaded_files; }
// Each loaded .pre archive's memory starts with the archive's name (see PackLeadIn).
char** pre_files() { return g_pre_files; }

void* allocate_file_memory(unsigned int size) { return memory::alloc_block(size); }
void shrink_file_memory(unsigned int size, void* memory) { memory::trim_block_end(size, memory); }
void free_file_memory(void* memory) { memory::free_malloc(memory); }

// Buffers are allocated in whole 2 KB sectors.
unsigned int sector_aligned(unsigned int size) { return (size + 0x7FF) & ~0x7FFu; }

// The pre manager instance finds files inside .pre archives registered with it. (Like the original, this
// assumes it exists.)
void* pre_manager_load_file(const char* name, int* size)
{
    return pre::sole()->ingest_file(name, size, nullptr);
}

// A loaded .pre archive: its name, padded to 16 bytes, then the archive's own header and its files.
struct PackLeadIn {
    int total_size_;
    int version;
    int num_files_;
};

PackLeadIn* header_of(char* pre)
{
    return reinterpret_cast<PackLeadIn*>(pre + ((std::strlen(pre) + 0x10) & ~0xFu));
}

PreEntry* first_entry(PackLeadIn* header) { return reinterpret_cast<PreEntry*>(header + 1); }

// Archives as stored on disk: the name follows the header directly, the data the name, padded to 4 bytes.
// (Loaded archives are repacked with 16-byte aligned data - see next_entry.)
PreEntry* next_raw_entry(PreEntry* entry)
{
    uint32_t stored = static_cast<uint32_t>(entry->compressed_size_ ? entry->compressed_size_ : entry->size);
    return reinterpret_cast<PreEntry*>(reinterpret_cast<uint8_t*>(entry + 1) + entry->name_size_ + ((stored + 3) & ~3u));
}

PreEntry* next_entry(PreEntry* entry)
{
    uint32_t stored = static_cast<uint32_t>(entry->compressed_size_ ? entry->compressed_size_ : entry->size);
    uintptr_t after_name = (reinterpret_cast<uintptr_t>(entry) + entry->name_size_ + 0x1F) & ~uintptr_t(0xF);
    return reinterpret_cast<PreEntry*>(after_name + ((stored + 3) & ~3u));
}

} // namespace

// 0x005B7180
PreEntry* find_pre_entry(uint32_t checksum)
{
    for (int i = 0; i < kPreloadCapacity; ++i) {
        char* pre = pre_files()[i];
        if (!pre)
            continue;
        PackLeadIn* header = header_of(pre);
        PreEntry* entry = first_entry(header);
        for (int n = 0; n < header->num_files_; ++n, entry = next_entry(entry)) {
            if (entry->checksum == checksum)
                return entry;
        }
    }
    return nullptr;
}

// 0x005B7C00 (PartyMod's script patching wraps calls to this, and calls it by address)
void* read_in(const char* name, bool async, void* callback, void* hook_user_a, void* hook_user_b)
{
    uint32_t checksum = checksums::hash_from_text(name);
    // Not original - the data overlay (frontend/data_overlay.h): a file of the data overlay is read from it,
    // before any archive (synchronously, through FileLoad::unseal, which opens the overlay's file).
    // Original behaviour: archives first, then the cache and the data folder.
    char overlay_path[260];
    bool overlay = frontend::data_overlay::get_overlay_path(name, overlay_path);

    // Already loaded?
    if (PreEntry* entry = overlay ? nullptr : find_pre_entry(checksum)) {
        ++entry->use_count_;
        auto after_name = (reinterpret_cast<uintptr_t>(entry) + entry->name_size_ + 0x1F) & ~uintptr_t(0xF);
        return reinterpret_cast<void*>(after_name);
    }
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        LoadedFile& file = loaded_files()[i];
        if (file.data && file.checksum == checksum) {
            ++file.ref_count_;
            return file.data;
        }
    }

    // The original assumes a free slot exists.
    LoadedFile* slot = nullptr;
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        if (!loaded_files()[i].data) {
            slot = &loaded_files()[i];
            break;
        }
    }

    void* data;
    int size;
    if (async && !overlay) {
        file::AsyncFileRef* file = file::async_open(name);
        size = file::async_get_file_size(file);
        data = allocate_file_memory(sector_aligned(static_cast<unsigned int>(size)));
        if (callback) {
            file::async_set_callback_mask(file, 1u << file::kOpReadFile); // call back when the read completes
            file::async_set_callback(file, reinterpret_cast<file::IoCompletionHook>(callback), hook_user_a,
                                   hook_user_b);
        }
        file::start_background_read(file, data, size);
        file::async_close(file);
        if (data)
            shrink_file_memory(static_cast<unsigned int>(size), data);
        gfx::loading_console::note_file("read (async)", name, size); // (not original: the loading console)
    } else {
        size = 0;
        data = overlay ? nullptr : pre_manager_load_file(name, &size);
        if (data)
            gfx::loading_console::note_file("unpack", name, size); // (not original: the loading console)
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
            if (data)
                shrink_file_memory(static_cast<unsigned int>(size), data);
        }
    }

    *slot = {data, checksum, size, 1};
    return data;
}

// 0x005B7B00: load `name` into `buffer` (or a new block if null), from a .pre archive or from disk, and set
// `*size` (if given) to its size. Returns the data, or null if it couldn't be loaded.
void* load_file_into(const char* name, int* size, void* buffer)
{
    int file_size = 0;
    // Not original - the data overlay: a file of it is read from there (FileLoad::unseal), not an archive.
    // Original behaviour: archives first, then the cache and the data folder.
    char overlay_path[260];
    bool overlay = frontend::data_overlay::get_overlay_path(name, overlay_path);
    void* data = overlay ? nullptr : pre::sole()->ingest_file(name, &file_size, static_cast<uint8_t*>(buffer));
    if (data)
        gfx::loading_console::note_file("unpack", name, file_size); // (not original: the loading console)
    if (!data) {
        file_size = overlay ? 0 : file::cached_file_size(name);
        if (file_size != 0) {
            data = buffer ? buffer : allocate_file_memory(sector_aligned(static_cast<unsigned int>(file_size)));
            if (!file::load_cached_file(name, data)) {
                if (!buffer)
                    free_file_memory(data);
                data = nullptr;
            }
        } else {
            file::Hdl file = file::unseal(name, "rb");
            file_size = static_cast<int>(file::get_file_size(file));
            data = buffer ? buffer : allocate_file_memory(sector_aligned(static_cast<unsigned int>(file_size)));
            file::get_take_in(data, 1, file_size, file);
            file::shut(file);
        }
        if (!buffer && data)
            shrink_file_memory(static_cast<unsigned int>(file_size), data);
    }
    if (size)
        *size = file_size;
    return data;
}

// 0x005B75A0
void load_pre(const char* name)
{
    for (int i = 0; i < kPreloadCapacity; ++i) {
        if (pre_files()[i] && _stricmp(pre_files()[i], name) == 0)
            return; // already loaded
    }
    int slot = -1;
    for (int i = 0; i < kPreloadCapacity; ++i) {
        if (!pre_files()[i]) {
            slot = i;
            break;
        }
    }

    char path[500];
    std::snprintf(path, sizeof(path), "pre\\%s", name);
    // The block starts with the archive's name, padded to 16 bytes.
    unsigned int prefix = static_cast<unsigned int>((std::strlen(name) + 0x10) & ~size_t(0xF));
    size_t length = std::strlen(path);

    // French and German: a complete localised archive (.prf/.prd) if there is one, else a patch for the
    // English archive (.ppf/.ppd).
    uint32_t* patch = nullptr;
    bool patched = false;
    uint8_t* block = nullptr;
    unsigned int file_size = 0, allocated = 0;
    bool loaded = false;
    char letter = game_config::g_language == 3 ? 'f' : game_config::g_language == 5 ? 'd' : '\0';
    if (letter) {
        path[length - 1] = letter;
        if (!file::is_present(path)) {
            path[length - 2] = 'p';
            if (file::is_present(path)) {
                file::Hdl file = file::unseal(path, "rb");
                file_size = file::get_file_size(file);
                patch = static_cast<uint32_t*>(memory::alloc(file_size));
                file::get_take_in(patch, 1, static_cast<int>(file_size), file);
                file::shut(file);
                path[length - 2] = 'r';
                path[length - 1] = 'e';
                patched = true;
                if (file_size != 0) {
                    // A leftover from the console versions' disc cache: always fails on PC, and the original
                    // then carries on patching through a null block - it crashes here. (Patch files aren't
                    // shipped with the PC version.)
                    allocated = ((file_size + 0x7FF) & ~0x7FFu) + prefix;
                    block = static_cast<uint8_t*>(allocate_file_memory(allocated));
                    std::memcpy(block, name, std::strlen(name) + 1);
                    if (!file::load_cached_file(path, block + prefix)) {
                        free_file_memory(block);
                        block = nullptr;
                    }
                    loaded = true;
                }
            } else {
                path[length - 2] = 'r';
                path[length - 1] = 'e';
            }
        }
    }

    if (!loaded) {
        file::Hdl file = file::unseal(path, "rb");
        file_size = file::get_file_size(file);
        unsigned int needed = patched && patch[0] >= file_size ? patch[0] : file_size;
        allocated = ((needed + 0x7FF) & ~0x7FFu) + prefix;
        block = static_cast<uint8_t*>(allocate_file_memory(allocated));
        std::memcpy(block, name, std::strlen(name) + 1);
        file::get_take_in(block + prefix, 1, static_cast<int>(file_size), file);
        file::shut(file);
    }

    if (patched) {
        // Through uintptr_t: on the path above `block` can be null, as in the original.
        auto data = reinterpret_cast<uintptr_t>(block) + prefix;
        if (allocated > file_size)
            std::memset(reinterpret_cast<void*>(data + file_size), 0, allocated - file_size - prefix);
        const auto* p = reinterpret_cast<const uint8_t*>(patch + 2);
        for (uint32_t i = 0; i < patch[1]; ++i) {
            uint32_t offset = reinterpret_cast<const uint32_t*>(p)[0];
            uint32_t count = reinterpret_cast<const uint32_t*>(p)[1];
            p += 8;
            std::memcpy(reinterpret_cast<void*>(data + offset), p, count);
            p += count;
        }
        memory::dealloc(patch);
    }

    // Repack: every file stored uncompressed, its data 16-byte aligned after its header and name, so files
    // can be used in place. First the new size (plus slack), then the copy.
    auto* old_header = reinterpret_cast<PackLeadIn*>(block + ((std::strlen(reinterpret_cast<char*>(block)) + 0x10) &
                                                             ~size_t(0xF)));
    int count = old_header->num_files_;
    uintptr_t new_size = prefix + sizeof(PackLeadIn);
    {
        auto* entry = reinterpret_cast<PreEntry*>(old_header + 1);
        for (int n = count; n != 0; --n) {
            new_size = ((entry->name_size_ + new_size + 0x1F) & ~uintptr_t(0xF)) + ((entry->size + 3) & ~3u);
            entry = next_raw_entry(entry);
        }
    }
    auto* repacked = static_cast<uint8_t*>(allocate_file_memory(static_cast<unsigned int>(new_size + 0xC00)));
    for (unsigned int i = 0; static_cast<int>(i) < static_cast<int>(prefix); ++i)
        repacked[i] = block[i];

    auto* new_header = reinterpret_cast<PackLeadIn*>(repacked + prefix);
    new_header->num_files_ = old_header->num_files_;
    new_header->version = old_header->version;
    auto* from = reinterpret_cast<PreEntry*>(old_header + 1);
    auto* to = reinterpret_cast<PreEntry*>(new_header + 1);
    for (int n = count; n != 0; --n) {
        to->size = from->size;
        to->checksum = from->checksum;
        to->compressed_size_ = 0;
        to->use_count_ = 0;
        to->name_size_ = from->name_size_;
        std::memcpy(to + 1, from + 1, from->name_size_);

        PreEntry* next_from = next_raw_entry(from);
        const uint8_t* source = reinterpret_cast<const uint8_t*>(from + 1) + from->name_size_;
        auto* target = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(to) + to->name_size_ + 0x1F) &
                                                  ~uintptr_t(0xF));
        if (from->compressed_size_) {
            uint8_t* end = lzss::decode(source, target, from->compressed_size_);
            for (uint32_t pad = static_cast<uint32_t>(to->size); pad & 3; ++pad)
                *end++ = 0;
        } else {
            std::memcpy(target, source, ((static_cast<uint32_t>(from->size) + 3) >> 2) * 4);
        }
        to = next_entry(to);
        from = next_from;
    }
    new_header->total_size_ = static_cast<int>(reinterpret_cast<uint8_t*>(to) - reinterpret_cast<uint8_t*>(new_header));
    free_file_memory(block);
    // With all 100 slots taken the original writes to slot -1 (the last loaded file's reference count).
    (&pre_files()[0])[slot] = reinterpret_cast<char*>(repacked);
}

// --- script functions -----------------------------------------------------------------------------------

namespace {

constexpr uint32_t kHeapParam = 0x42D6DD1E;    // LoadPipPre ... heap = <name>
constexpr uint32_t kHeapTopDown = 0x477FC6DE;  // the two heap names it accepts
constexpr uint32_t kHeapBottomUp = 0xC80BF12D;

const char* pre_name(script::Struct* params)
{
    const char* name = nullptr;
    script::fetch_text(params, 0, &name, true);
    return name;
}

} // namespace

// 0x005B7A60: LoadPipPre <name> [heap = ...]
bool __cdecl cfunc_load_pip_pre(script::Struct* params, script::Script* /*script*/)
{
    const char* name = pre_name(params);
    uint32_t heap = 0;
    script::read_hash_value(params, kHeapParam, &heap, false);
    if (heap == kHeapTopDown)
        memory::enter_heap_scope(memory::top_down_heap());
    else if (heap == kHeapBottomUp)
        memory::enter_heap_scope(memory::bottom_up_heap());
    else
        heap = 0;
    load_pre(name);
    if (heap)
        memory::leave_heap_scope();
    return true;
}

// 0x005B7410: UnLoadPipPre <name>
bool __cdecl cfunc_un_load_pip_pre(script::Struct* params, script::Script* /*script*/)
{
    return unload_pre(pre_name(params));
}

// 0x005B7430: DumpPipPreStatus [ShowPreedFiles] [ShowUnPreedFiles] [ShowOnlyOpenFiles] - debug output
bool __cdecl cfunc_dump_pip_pre_status(script::Struct* params, script::Script* /*script*/)
{
    bool show_files = script::has_bare_flag(params, "ShowPreedFiles");
    bool show_loose_files = script::has_bare_flag(params, "ShowUnPreedFiles");
    bool only_open = script::has_bare_flag(params, "ShowOnlyOpenFiles");
    debug::printf("Currently loaded pre files:\n");
    for (int i = 0; i < kPreloadCapacity; ++i) {
        char* pre = pre_files()[i];
        if (!pre)
            continue;
        debug::printf("### %s ###\n", pre);
        if (!show_files)
            continue;
        PackLeadIn* header = header_of(pre);
        PreEntry* entry = first_entry(header);
        for (int n = header->num_files_; n > 0; --n, entry = next_entry(entry)) {
            if (!only_open || entry->use_count_ != 0) {
                debug::printf("Usage:%d  File: %s\n", entry->use_count_, reinterpret_cast<const char*>(entry + 1));
                for (volatile int wait = 0; wait < 100000; ++wait) {
                    // the original pauses after each line (presumably so the debug output keeps up)
                }
            }
        }
    }
    if (show_loose_files) {
        debug::printf("Currently loaded un-preed files:\n");
        for (int i = 0; i < kMaxLoadedFiles; ++i) {
            const LoadedFile& file = loaded_files()[i];
            if (file.data)
                debug::printf("Usage:%d  File: %s\n", file.ref_count_, script::find_checksum_name_or_unknown(file.checksum));
        }
    }
    return true;
}

// 0x005B7200
void offload(uint32_t checksum)
{
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        LoadedFile& file = loaded_files()[i];
        if (!file.data || file.checksum != checksum)
            continue;
        if (file.ref_count_ != 0)
            --file.ref_count_;
        if (file.ref_count_ == 0) {
            free_file_memory(file.data);
            file = {};
        }
    }
    if (PreEntry* entry = find_pre_entry(checksum)) {
        if (entry->use_count_ != 0)
            --entry->use_count_;
    }
}

// 0x005B7270
void offload(const char* name)
{
    offload(checksums::hash_from_text(name));
}

// 0x005B7290
int get_file_size(uint32_t checksum)
{
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        const LoadedFile& file = loaded_files()[i];
        if (file.data && file.checksum == checksum)
            return file.size;
    }
    PreEntry* entry = find_pre_entry(checksum);
    return entry ? entry->size : 0;
}

// 0x005B72E0
int get_file_size(const char* name)
{
    return get_file_size(checksums::hash_from_text(name));
}

// 0x005B70F0
FileTables* FileTables::make_new()
{
    for (int i = 0; i < kPreloadCapacity; ++i)
        pre_files()[i] = nullptr;
    for (int i = 0; i < kMaxLoadedFiles; ++i)
        loaded_files()[i] = {};
    return this;
}

// 0x005B7130
bool unload_pre(const char* name)
{
    bool unloaded = false;
    for (int i = 0; i < kPreloadCapacity; ++i) {
        char*& pre = pre_files()[i];
        if (pre && _stricmp(pre, name) == 0) {
            free_file_memory(pre);
            pre = nullptr;
            unloaded = true;
        }
    }
    return unloaded;
}

// 0x005B7300
const char* take_next_finished_archive(const char* previous)
{
    bool found_previous = previous == nullptr;
    for (int i = 0; i < kPreloadCapacity; ++i) {
        const char* pre = pre_files()[i];
        if (!pre)
            continue;
        if (found_previous)
            return pre;
        if (_stricmp(pre, previous) == 0)
            found_previous = true;
    }
    return nullptr;
}

// 0x005B7370
bool archive_is_busy(const char* name)
{
    for (int i = 0; i < kPreloadCapacity; ++i) {
        char* pre = pre_files()[i];
        if (!pre || _stricmp(pre, name) != 0)
            continue;
        PackLeadIn* header = header_of(pre);
        PreEntry* entry = first_entry(header);
        for (int n = 0; n < header->num_files_; ++n, entry = next_entry(entry)) {
            if (entry->use_count_ != 0)
                return true;
        }
    }
    return false;
}

} // namespace Pip

static_assert(sizeof(packs::PreEntry) == 16 && sizeof(packs::LoadedFile) == 16, "layout must match the original");
