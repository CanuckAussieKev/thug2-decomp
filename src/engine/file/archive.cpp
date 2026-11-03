#include "engine/file/archive.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <array>
#include <span>
#include <string_view>
#include <gsl/assert>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/file/async.h"
#include "engine/file/file_load.h"
#include "formats/pre_format.h"
#include "engine/lists/key_map.h"
#include "engine/audio/music_api.h"
#include "engine/script/params.h"
#include "engine/script/param_struct.h"
#include "engine/sys/game_config.h"
#include "formats/byte_stream.h"
#include "formats/lzss.h"
#include "platform/stopwatch.h"
#include "engine/gfx/loading_console.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/core/gathered_call.h"
#include "engine/core/language.h"

namespace pre {

static_assert(sizeof(ArchiveFile::FileInfo) == kFileInfoBytes, "allocated as 0x14 bytes by the original");

namespace {

// NOT ORIGINAL grouping - the archive manager's slots and its instance's references. Made on first use, never destroyed
// (archive_slots()).
struct ArchiveSlots {
    ArchiveManager* manager_slot_{};
    ArchiveManager* instance_slot_{};
    int instance_refs_{};
};

ArchiveSlots& archive_slots()
{
    static core::NoDestroy<ArchiveSlots> state;
    return state.get();
}

memory::Arena* get_file_heap(bool bottom_up) { return bottom_up ? memory::bottom_up_heap() : memory::top_down_heap(); }

void move_memory_block(void* out, const void* in, unsigned int count) { std::memcpy(out, in, count); }

// Not original - a compressed file unpacked with both ends bounded: lzss::unpack, the same bytes as the archive's
// own LZSS decoder (0x005B7DF0, the same algorithm as lzss::decode with its own 4 KB ring - replaced) for a valid
// file; a damaged one stops at its size instead of writing past `out`.
void unpack_file(const ArchiveFile::FileInfo& info, uint8_t* out)
{
    if (!lzss::unpack(info.data, static_cast<size_t>(info.compressed_size_), out, static_cast<size_t>(info.size),
                      nullptr)) {
        debug::printf("pre archive: a damaged compressed file\n");
    }
}

} // namespace

// 0x005B8820
ArchiveFile::ArchiveFile(uint8_t* block, size_t size, bool bottom_up) : block_(block), bottom_up_heap_(bottom_up)
{
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    files_ = memory ? memory::construct_in<lists::KeyMap>(memory, 4) : nullptr;

    // Not original - read through pre_format's reader, bounded by the block: a count or size past the end stops
    // at the damage (the original walked on past the block). Valid archives give the same files.
    if (size == 0) {
        size = util::ByteReader(block, pre_format::kHeaderSize).u32();
    }
    util::ByteReader reader(block, size);
    pre_format::ArchiveHeader header;
    entry_count_ = pre_format::read_archive_header(reader, &header) ? header.file_count : 0;
    for (int i = 0; i < entry_count_; ++i) {
        pre_format::FileRecord record;
        if (!pre_format::read_file_record(reader, &record)) {
            debug::printf("pre archive damaged at file %d of %d\n", i, entry_count_);
            entry_count_ = i;
            break;
        }
        auto* info = static_cast<FileInfo*>(memory::carve(sizeof(FileInfo), 1, get_file_heap(bottom_up_heap_)));
        const uint32_t checksum = checksums::hash_from_text(record.name);
        Expects(files_ != nullptr);
        if (files_->fetch_entry(checksum)) {
            memory::dealloc(info); // the same name twice: the first one wins
        } else {
            files_->store_entry(checksum, info);
            info->data = std::span<uint8_t>(block, size).subspan(static_cast<size_t>(record.data - block)).data(); // (our own block, through the reader's const view)
            info->unpacked = nullptr;
            info->position = 0;
            info->compressed_size_ = record.compressed_size;
            info->size = record.size;
        }
    }
    open_entry_ = nullptr;
    open_count_ = 0;
}

// 0x005B9410 (the scalar deleting destructor: frees `this` too when bit 0 of its argument is set)
ArchiveFile::~ArchiveFile()
{
    memory::dealloc(block_);
    files_->run_hook([](void* info, void*) { memory::dealloc(info); }, nullptr);
    files_->empty_table();
    if (lists::KeyMap* files = files_) {
        files->~KeyMap();
        memory::dealloc(files);
    }
}

// 0x005B8230
uint8_t* ArchiveFile::read_entry_from_archive(const char* name, int* size, uint8_t* out)
{
    auto* info = static_cast<FileInfo*>(files_->fetch_entry(checksums::hash_from_text(name)));
    if (!info) {
        return nullptr;
    }
    *size = info->size;
    if (!out) {
        out = static_cast<uint8_t*>(memory::alloc_block(static_cast<unsigned int>(info->size)));
    }
    if (info->unpacked) {
        move_memory_block(out, info->unpacked, static_cast<unsigned int>(info->size));
    } else if (info->compressed_size_ == 0) {
        move_memory_block(out, info->data, static_cast<unsigned int>(info->size));
    } else {
        unpack_file(*info, out);
    }
    return out;
}

// 0x005B8310
uint8_t* ArchiveFile::find_entry_by_key(FileInfo* handle)
{
    files_->set_iter_bucket(-1);
    files_->set_iter_item(nullptr);
    while (files_->step_iterator() && files_->iter_item()) {
        auto* info = static_cast<FileInfo*>(files_->iter_item()->value);
        if (!info) {
            break;
        }
        if (info->data && info == handle) {
            open_entry_ = info;
            return info->compressed_size_ ? info->unpacked : info->data;
        }
    }
    return nullptr;
}

// 0x005B8A00
ArchiveFile::FileInfo* ArchiveFile::find_entry_in_archive(const char* name)
{
    auto* info = static_cast<FileInfo*>(files_->fetch_entry(checksums::hash_from_text(name)));
    if (!info) {
        return nullptr;
    }
    find_entry_by_key(info);
    if (!open_entry_->unpacked && open_entry_->compressed_size_ != 0) {
        memory::push_mem_profile(name);
        open_entry_->unpacked = static_cast<uint8_t*>(
            memory::carve(static_cast<unsigned int>(open_entry_->size), 1, get_file_heap(bottom_up_heap_)));
        memory::pop_mem_profile();
        unpack_file(*open_entry_, open_entry_->unpacked);
    }
    return info;
}

// 0x005B7F00
int ArchiveFile::get_take_in(void* out, unsigned int count)
{
    FileInfo* file = open_entry_;
    auto left = static_cast<unsigned int>(file->size - file->position);
    const unsigned int n = count <= left ? count : left;
    const uint8_t* source = file->compressed_size_ ? file->unpacked : file->data;
    move_memory_block(out, std::span<const uint8_t>(source, static_cast<size_t>(file->size)).subspan(static_cast<size_t>(file->position)).data(), n);
    file->position += static_cast<int>(n);
    return static_cast<int>(n);
}

// 0x005B7F60
int ArchiveFile::seek_to(SeekTo seek)
{
    const auto offset = seek.offset;
    const auto origin = seek.origin;
    FileInfo* file = open_entry_;
    const int previous = file->position;
    if (origin == 0) {
        file->position = offset;
    } else if (origin == 1) {
        file->position += offset;
    } else if (origin == 2) {
        file->position = file->size - offset;
    } else {
        return -1;
    }
    if (file->position >= 0 && file->position <= file->size) {
        return 0;
    }
    file->position = previous;
    return -1;
}

// --- ArchiveManager ---------------------------------------------------------------------------------------------


namespace {

ArchiveManager*& manager_slot() { return archive_slots().manager_slot_; }   // set by the constructor
ArchiveManager*& instance_slot() { return archive_slots().instance_slot_; } // the one create_manager made
int& instance_refs() { return archive_slots().instance_refs_; }
bool& get_handled() { return file::archive_handled(); } // see FileLoad:: - "this call was ours"
int get_language() { return game_config::get_language(); }

// Names inside archives use backslashes. The original copies into a 128-byte buffer with no limit; this
// stops at the buffer's size.
void normalise_name(std::array<char, kEntryNameSize>& out, const char* name)
{
    size_t i = 0;
    const std::string_view text(name);
    for (; i < text.size() && i < sizeof(out) - 1; ++i) {
        core::at(out, i) = core::at(text, i) == '/' ? '\\' : core::at(text, i);
    }
    core::at(out, i) = '\0';
}

// Calls `visit` for each loaded archive, in table order, until it returns something non-null (returned), or
// the table runs out. Uses the table's own iterator, like the original.
template <typename Visit>
auto for_each_pre(lists::KeyMap* pres, Visit visit) -> decltype(visit(static_cast<ArchiveFile*>(nullptr)))
{
    pres->set_iter_bucket(-1);
    pres->set_iter_item(nullptr);
    while (pres->step_iterator() && pres->iter_item() && pres->iter_item()->value) {
        if (auto result = visit(static_cast<ArchiveFile*>(pres->iter_item()->value))) {
            return result;
        }
    }
    return {};
}

} // namespace

ArchiveManager* hub() { return manager_slot(); }
ArchiveManager* sole() { return instance_slot(); }

// 0x005B8D20
ArchiveManager::ArchiveManager()
{
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    pres_ = memory ? memory::construct_in<lists::KeyMap>(memory, 4) : nullptr;
    manager_slot() = this;
}

// 0x005B8E90 (returns the instance in EAX: the caller keeps it)
ArchiveManager* create_manager(bool create)
{
    if (!instance_slot() && create) {
        void* memory = memory::alloc_zeroed(sizeof(ArchiveManager), nullptr, 1);
        instance_slot() = memory ? memory::construct_value_in<ArchiveManager>(memory) : nullptr;
    }
    ++instance_refs();
    return instance_slot();
}

// 0x005B9510
void destroy_manager()
{
    if (instance_refs() > 1) {
        --instance_refs();
        return;
    }
    if (ArchiveManager* manager = instance_slot()) {
        if (lists::KeyMap* pres = manager->pres()) {
            pres->~KeyMap();
            memory::dealloc(pres);
        }
        memory::dealloc(manager);
    }
    instance_slot() = nullptr;
    instance_refs() = 0;
}

namespace {

// The script global block_pre_loading (Scripts::fetch_int_value by name - not decompiled yet).
bool pre_loading_blocked() { return script::fetch_int_value("block_pre_loading") != 0; }
// Always false in this build; only does debugging work when the debug flag is set.
bool skip_synchronous_load() { return audio::disc_mode_active(); }
int time_milliseconds() { return static_cast<int>(frame_clock::get_time()); }
bool debug_checks() { return game_config::from_cd(); }

memory::Arena* load_heap(bool bottom_up) { return bottom_up ? memory::bottom_up_heap() : memory::top_down_heap(); }

// 0x005B8E70 (the async loader's callback for archives; not marked as a function in Ghidra)
// A pre-load finished (the file system's completion hook's arguments, in order).
struct PreLoadDone {
    file::AsyncFileRef* file{};
    int event{};
    int size{};
    void* manager{};
    void* data{};
};

void on_async_pre_loaded(const PreLoadDone& done)
{
    const file::AsyncFileRef* const file = done.file;
    const int event = done.event;
    const int size = done.size;
    void* const manager = done.manager;
    void* const data = done.data;
    if (event == 3) { // finished
        static_cast<ArchiveManager*>(manager)->finish_load(file, static_cast<uint8_t*>(data),
                                                           size);
    }
}
} // namespace

// 0x005B8EE0
void ArchiveManager::load_pre(const char* name, bool async, bool report_missing, bool bottom_up)
{
    hold_archive_loads_ = pre_loading_blocked();
    if (hold_archive_loads_) {
        return;
    }
    if (!file::async_loading_available()) {
        async = false;
    }
    if (!async && skip_synchronous_load()) {
        return;
    }

    std::array<char, kEntryNameSize> path{};
    text::format_to(path, "pre\\%s", name);
    if (std::strrchr(name, '.') && std::strlen(path.data()) > 4) {
        core::at(path, std::strlen(path.data()) - 1) = 'x'; // .pre -> .prx
    }
    const int start = time_milliseconds();

    if (async) {
        if (file::AsyncFileRef* file = file::async_open(path.data())) {
            PendingLoad& pending = core::at(pending_, queued_archive_count_);
            // The original copies with no limit; the field holds 63 characters.
            strncpy_s(pending.name.data(), pending.name.size(), name, _TRUNCATE);
            pending.handle = file;
            ++queued_archive_count_;
            const int size = file::async_get_file_size(file);
            gfx::loading_console::note_file("read (async)", path.data(), size); // (not original: the loading console)
            memory::push_mem_profile(path.data());
            void* data = memory::carve(static_cast<unsigned int>(size), 1, load_heap(bottom_up));
            memory::pop_mem_profile();
            file::async_set_callback(file, &core::gathered<PreLoadDone, &on_async_pre_loaded>, this, data);
            file::start_background_read(file, data, size);
            return;
        }
        // Couldn't start an async load: load it now instead.
    }

    uint8_t* data = nullptr;
    int size = file::cached_file_size(path.data());
    auto block_size = static_cast<size_t>(size); // (not original: the archive's bound for its reader)
    if (size != 0) {
        memory::push_mem_profile(path.data());
        data = static_cast<uint8_t*>(memory::carve(static_cast<unsigned int>(size), 1, load_heap(bottom_up)));
        memory::pop_mem_profile();
        if (!file::load_cached_file(path.data(), data)) {
            debug::printf("pre file %s failed to load quickly.\n", path.data());
        }
    } else {
        // French and German: a complete localised archive (.prf/.prd) if there is one, else a patch for the
        // English archive (.ppf/.ppd).
        const size_t length = std::strlen(path.data());
        uint8_t* patch = nullptr;
        uint32_t patch_size = 0;
        bool patched = false;
        const char letter = get_language() == core::kLanguageFrench ? 'f' : get_language() == core::kLanguageGerman ? 'd' : '\0';
        if (letter) {
            core::at(path, length - 1) = letter;
            if (!file::is_present(path.data())) {
                core::at(path, length - 2) = 'p';
                if (file::is_present(path.data())) {
                    file::Hdl file = file::unseal(path.data(), "rb");
                    patch_size = file::get_file_size(file);
                    patch = static_cast<uint8_t*>(memory::alloc(patch_size));
                    file::get_take_in(patch, 1, static_cast<int>(patch_size), file);
                    file::shut(file);
                    patched = true;
                }
                core::at(path, length - 2) = 'r'; // back to .pre (opened as .prx)
                core::at(path, length - 1) = 'e';
            }
        }

        file::Hdl file = file::unseal(path.data(), "rb");
        if (!file && (report_missing || debug_checks())) {
            debug::printf("couldn't open %s.  Make sure the, if this is a level PRE file, that the name in "
                          "levels.q matches the file on disk\n",
                          path.data());
            return;
        }
        const uint32_t file_size = file::get_file_size(file);
        memory::push_mem_profile(path.data());
        size = static_cast<int>(file_size);
        uint32_t allocated = file_size;
        pre_format::PatchHeader patch_header;
        util::ByteReader patch_reader(patch, patch_size);
        if (patched && pre_format::read_patch_header(patch_reader, &patch_header) &&
            file_size <= patch_header.patched_size) {
            allocated = patch_header.patched_size; // room for the patched archive
        }
        data = static_cast<uint8_t*>(memory::carve(allocated, 1, load_heap(bottom_up)));
        memory::pop_mem_profile();
        file::get_take_in(data, 1, static_cast<int>(file_size), file);
        if (debug_checks() && file_size != util::ByteReader(data, file_size).u32()) {
            debug::printf("%s has incorrect file size\n", path.data());
        }
        file::shut(file);
        if (patched) {
            if (allocated > file_size) {
                std::memset(std::span<uint8_t>(data, allocated).subspan(file_size).data(), 0, allocated - file_size);
            }
            // Not original - bounded by the archive's room and the patch's size (the original wrote each record
            // where it said).
            if (!pre_format::apply_patch(data, allocated, patch, patch_size)) {
                debug::printf("%s: a damaged patch\n", path.data());
            }
            memory::dealloc(patch);
        }
        block_size = allocated;
    }

    debug::printf("load time for file %s size %d is %d ms\n", name, size, time_milliseconds() - start);
    void* memory = memory::alloc_zeroed(sizeof(ArchiveFile), load_heap(bottom_up), 1);
    ArchiveFile* pre = memory ? memory::construct_in<ArchiveFile>(memory, data, block_size, bottom_up) : nullptr;
    pres_->store_entry(checksums::hash_from_text(name), pre);
}

// 0x005B8750
uint8_t* ArchiveManager::ingest_file(const char* name, int* size, uint8_t* out)
{
    std::array<char, 128> path{};
    normalise_name(path, name);
    return for_each_pre(pres_, [&](ArchiveFile* pre) { return pre->read_entry_from_archive(path.data(), size, out); });
}

// 0x005B8390
bool ArchiveManager::is_file_present(const char* name)
{
    std::array<char, 128> path{};
    normalise_name(path, name);
    return for_each_pre(
        pres_, [&](ArchiveFile* pre) { return pre->files()->fetch_entry(checksums::hash_from_text(path.data())) != nullptr; });
}

// 0x005B8AD0
ArchiveFile::FileInfo* ArchiveManager::open_file(const char* name)
{
    std::array<char, 128> path{};
    normalise_name(path, name);
    // "...scripts.pre" -> "...scripts.pr<language letter>"
    const size_t length = std::strlen(path.data());
    if (length > kScriptsArchiveNameLength && std::string_view(path.data(), length).ends_with("scripts.pre")) {
        static const auto kLetters = std::to_array<char>({'f', 's', 'd', 'i'}); // languages 3 to 6
        const int language = get_language();
        core::at(path, length - 1) = language >= kFirstLetterLanguage && language <= kLastLetterLanguage ? core::at(kLetters, language - kFirstLetterLanguage) : 'e';
    }
    return for_each_pre(pres_, [&](ArchiveFile* pre) -> ArchiveFile::FileInfo* {
        ArchiveFile::FileInfo* handle = pre->find_entry_in_archive(path.data());
        if (!handle) {
            return nullptr;
        }
        open_archive_ = pre;
        open_entry_key_ = handle;
        open_entry_data_ = pre->find_entry_by_key(handle);
        return handle;
    });
}

// 0x005B84A0
uint8_t* ArchiveManager::find_entry_in_archive(ArchiveFile::FileInfo* handle)
{
    if (missing_handle_ == handle) {
        return nullptr;
    }
    if (open_entry_key_ == handle) {
        return open_entry_data_;
    }
    uint8_t* data = for_each_pre(pres_, [&](ArchiveFile* pre) -> uint8_t* {
        uint8_t* found = pre->find_entry_by_key(handle);
        if (found) {
            open_archive_ = pre;
            open_entry_key_ = handle;
            open_entry_data_ = found;
        }
        return found;
    });
    if (!data) {
        missing_handle_ = handle;
    }
    return data;
}

// 0x005B8540
bool ArchiveManager::is_loaded(const char* name)
{
    return pres_->fetch_entry(checksums::hash_from_text(name)) != nullptr;
}

// 0x005B7FD0
bool ArchiveManager::has_finished_loading(const char* name)
{
    for (int i = 0; i < queued_archive_count_; ++i) {
        if (std::strcmp(core::at(pending_, i).name.data(), name) == 0) {
            return false;
        }
    }
    return true;
}

// 0x005B8DB0
void ArchiveManager::finish_load(const file::AsyncFileHandle* handle, uint8_t* data, int size)
{
    int i = 0;
    while (i < queued_archive_count_ && core::at(pending_, i).handle != handle) {
        ++i;
    }
    if (i == queued_archive_count_) {
        return;
    }
    void* memory = memory::alloc_zeroed(sizeof(ArchiveFile), memory::top_down_heap(), 1);
    // (`size`: the bytes the read brought in - the file's)
    ArchiveFile* pre =
        memory ? memory::construct_in<ArchiveFile>(memory, data, size > 0 ? static_cast<size_t>(size) : 0, false)
               : nullptr;
    pres_->store_entry(checksums::hash_from_text(core::at(pending_, i).name.data()), pre);
    --queued_archive_count_;
    core::at(pending_, i) = core::at(pending_, queued_archive_count_); // the last pending load takes this one's place
}

// 0x005B9570
void ArchiveManager::unload_pre(const char* name, bool only_if_loaded)
{
    if (hold_archive_loads_) {
        return;
    }
    auto* pre = static_cast<ArchiveFile*>(pres_->fetch_entry(checksums::hash_from_text(name)));
    if (!pre && only_if_loaded) {
        return;
    }
    pres_->erase_entry(checksums::hash_from_text(name));
    if (pre) {
        pre->~ArchiveFile();
        memory::dealloc(pre);
    }
}

// --- the archive side of FileLoad:: -----------------------------------------------------------------------

// 0x005B85A0
bool is_present(const char* name)
{
    get_handled() = hub()->is_file_present(name);
    return get_handled();
}

// 0x005B8C70
ArchiveFile::FileInfo* unseal(OpenRequest open_request, bool count_open)
{
    const auto* const name = open_request.name;
    const auto* const mode = open_request.mode;
    ArchiveFile::FileInfo* handle = hub()->open_file(name);
    if (!handle) {
        get_handled() = false;
        return nullptr;
    }
    if (count_open) {
        ++hub()->open_archive()->open_count();
    }
    // Archives are read-only: any mode other than r/b gives the file up (and leaves it to FileLoad::).
    for (const char c : std::string_view(mode)) {
        if (c != 'r' && c != 'b') {
            ArchiveFile* pre = hub()->open_archive();
            if (pre->open_entry()->unpacked) {
                memory::dealloc(pre->open_entry()->unpacked);
            }
            pre->open_entry()->unpacked = nullptr;
            if (count_open) {
                --pre->open_count();
            }
            get_handled() = false;
            return nullptr;
        }
    }
    hub()->open_archive()->open_entry()->position = 0;
    get_handled() = true;
    return handle;
}

// 0x005B85D0
int shut(ArchiveFile::FileInfo* file, bool count_open)
{
    if (!hub()->find_entry_in_archive(file)) {
        get_handled() = false;
        return 0;
    }
    ArchiveFile* pre = hub()->open_archive();
    if (pre->open_entry()->unpacked) {
        memory::dealloc(pre->open_entry()->unpacked);
    }
    pre->open_entry()->unpacked = nullptr;
    if (count_open) {
        --pre->open_count();
    }
    get_handled() = true;
    return 0;
}

// 0x005B8630
int get_take_in(void* buffer, int size, int count, ArchiveFile::FileInfo* file)
{
    if (!hub()->find_entry_in_archive(file)) {
        get_handled() = false;
        return 0;
    }
    get_handled() = true;
    return hub()->open_archive()->get_take_in(buffer, static_cast<unsigned int>(size * count));
}

// 0x005B8680
int emit_out(const void* /*buffer*/, int /*size*/, int /*count*/, ArchiveFile::FileInfo* file)
{
    hub()->find_entry_in_archive(file);
    get_handled() = false;
    return 0;
}

// 0x005B86A0
int seek_to(ArchiveFile::FileInfo* file, int offset, int origin)
{
    if (!hub()->find_entry_in_archive(file)) {
        get_handled() = false;
        return 0;
    }
    get_handled() = true;
    return hub()->open_archive()->seek_to({.offset = offset, .origin = origin});
}

// 0x005B86E0
int get_file_size(ArchiveFile::FileInfo* file)
{
    if (!hub()->find_entry_in_archive(file)) {
        get_handled() = false;
        return 0;
    }
    get_handled() = true;
    const ArchiveFile::FileInfo* active = hub()->open_archive()->open_entry();
    return active ? active->size : 0;
}

// --- script functions -----------------------------------------------------------------------------------

namespace {

// Flag names in the scripts' LoadPreFile calls (checksums; the names themselves aren't in the exe).
constexpr uint32_t kFlagBottomUpHeap = 0xA44C770F;
constexpr uint32_t kFlagReportMissing = 0x3D92465E;
constexpr uint32_t kFlagAsync = 0x90E07C79;

const char* archive_name(script::Struct* params)
{
    const char* name = nullptr;
    script::fetch_text(params, 0, &name, true);
    return name;
}

} // namespace

// 0x005B9470: LoadPreFile <name> [flags]
bool cfunc_load_pre_file(script::Struct* params, script::Script* /*script*/)
{
    const char* name = archive_name(params);
    ArchiveManager* manager = sole();
    const bool bottom_up = script::has_bare_flag(params, kFlagBottomUpHeap);
    const bool report_missing = script::has_bare_flag(params, kFlagReportMissing);
    const bool async = script::has_bare_flag(params, kFlagAsync);
    memory::enter_heap_scope(bottom_up ? memory::bottom_up_heap() : memory::top_down_heap());
    manager->load_pre(name, async, report_missing, bottom_up);
    memory::leave_heap_scope();
    return true;
}

// 0x005B9600: UnloadPreFile <name> [dont_assert]
bool cfunc_unload_pre_file(script::Struct* params, script::Script* /*script*/)
{
    ArchiveManager* manager = sole();
    const char* name = archive_name(params);
    manager->unload_pre(name, script::has_bare_flag(params, "dont_assert"));
    return true;
}

// 0x005B8160: IsLoadPreFinished <name>
bool cfunc_is_load_pre_finished(script::Struct* params, script::Script* /*script*/)
{
    ArchiveManager* manager = sole();
    return manager->has_finished_loading(archive_name(params));
}

// 0x005B8190: AllLoadPreFinished
bool cfunc_all_load_pre_finished(script::Struct* /*params*/, script::Script* /*script*/)
{
    return sole()->queued_archive_count() == 0;
}

// 0x005B81B0: WaitLoadPre <name>
bool cfunc_wait_load_pre(script::Struct* params, script::Script* /*script*/)
{
    ArchiveManager* manager = sole();
    const char* name = archive_name(params);
    while (!manager->has_finished_loading(name)) {
        file::async_wait_for_io();
    }
    return true;
}

// 0x005B8200: WaitAllLoadPre
bool cfunc_wait_all_load_pre(script::Struct* /*params*/, script::Script* /*script*/)
{
    const ArchiveManager* manager = sole();
    while (manager->queued_archive_count() != 0) {
        file::async_wait_for_io();
    }
    return true;
}

// 0x005B8720: InPreFile <name> - whether the archive is loaded
bool cfunc_in_pre_file(script::Struct* params, script::Script* /*script*/)
{
    return sole()->is_loaded(archive_name(params));
}

} // namespace pre
