#include "game/save_cards/memcard.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <cstring>
#include <array>

#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "game/save_cards/save_types.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "game/save_cards/save_cards_state.h"

namespace save_cards {

namespace {

constexpr size_t kPathBufferBytes = 128;
constexpr size_t kFilenameBufferBytes = 320;
constexpr int kCardPresent = 0x75; // what the save device's presence is set to
constexpr int kHighWordShift = 32;

// NOT ORIGINAL grouping - memcard.cpp's state (the original's globals). Made on first use, never destroyed
// (memcard_state()).
struct MemcardState {
    std::array<char, kPathBufferBytes> path_buffer_{};
    int memcard_references_{};
    std::array<char, kFilenameBufferBytes> filename_buffer_{};
};

MemcardState& memcard_state()
{
    static core::NoDestroy<MemcardState> state;
    return state.get();
}

// The shared path buffers.
std::array<char, kPathBufferBytes>& path_buffer() { return memcard_state().path_buffer_; }
std::array<char, kFilenameBufferBytes>& filename_buffer() { return memcard_state().filename_buffer_; }

int& ref_tally() { return memcard_state().memcard_references_; }

// A new (zeroed) file object: its own one-item list, no handle yet.
FileLoad* new_file(fs::DiskFile* handle, SaveCard* card)
{
    auto* file = static_cast<FileLoad*>(memory::alloc_zeroed(sizeof(FileLoad)));
    if (!file) {
        return nullptr;
    }
    file->set_hdl(handle);
    file->list_link().make_node(file);
    file->set_save_device(card);
    return file;
}

} // namespace

// 0x005A8E80
FileLoad* FileLoad::teardown(unsigned int flags)
{
    list_link_.uncouple();
    if ((flags & 1) != 0U) {
        memory::Hub::dealloc(this);
    }
    return this;
}

// 0x005BC800
uint32_t FileLoad::seek_to(SeekTo seek)
{
    const auto offset = seek.offset;
    const auto origin = seek.origin;
    fs::Origin from = fs::Origin::kEnd; // (0: from the start, 1: from here, anything else: from the end)
    if (origin == 0) {
        from = fs::Origin::kBegin;
    } else if (origin == 1) {
        from = fs::Origin::kCurrent;
    }
    return fs::disk().seek(hdl_, offset, from);
}

// 0x005BC840
bool FileLoad::drain()
{
    fs::disk().flush(hdl_);
    return true;
}

// 0x005BC850
int FileLoad::emit_out(const void* data, int size)
{
    uint32_t written = 0;
    return fs::disk().write(hdl_, data, static_cast<uint32_t>(size), &written) ? static_cast<int>(written) : 0;
}

// 0x005BC880
int FileLoad::get_take_in(void* buffer, int size)
{
    uint32_t read = 0;
    return fs::disk().read(hdl_, buffer, static_cast<uint32_t>(size), &read, nullptr) ? static_cast<int>(read) : 0;
}

// 0x005BC8B0
bool FileLoad::shut() { return fs::disk().close(hdl_); }

// 0x005BC9B0
FileLoad* SaveCard::unseal(const char* name, int mode, int /*unused*/)
{
    fault_ = 0;
    // (with '/' turned into '\')
    auto& path = path_buffer();
    const std::string_view text(name);
    for (size_t i = 0; i <= text.size(); ++i) {
        const char character = i < text.size() ? core::at(text, i) : '\0';
        core::at(path, i) = character == '/' ? '\\' : character;
    }
    fs::OpenSpec spec{.access = fs::Access::kRead, .sharing = fs::Sharing::kNone,
                      .disposition = fs::Disposition::kOpenExisting, .hint = fs::Hint::kNormal};
    switch (mode) {
    case 1:
        spec.access = fs::Access::kRead;
        break;
    case 2:
        spec.access = fs::Access::kWrite;
        break;
    case 3:
        spec.access = fs::Access::kReadWrite;
        break;
    case 4:
        spec.access = fs::Access::kWrite;
        spec.disposition = fs::Disposition::kCreateNew;
        break;
    case kOpenWriteAlways:
        spec.access = fs::Access::kWrite;
        spec.disposition = fs::Disposition::kOpenAlways;
        break;
    default:
        return nullptr;
    }
    fs::DiskFile* handle = fs::disk().open(path.data(), spec);
    if (handle == fs::no_file()) {
        // (the error is always 0 here: this never happens)
        if (fault_ == kErrorDiskFull) {
            fault_ = 2;
        }
        return nullptr;
    }
    FileLoad* file = new_file(handle, this);
    fs::FileStat attributes; // (asked for, not used)
    fs::disk().stat(path.data(), &attributes);
    return file;
}

// 0x005BC770
bool SaveCard::remove_file(const char* name) { return fs::disk().remove(name); }

// 0x005BC740
const char* SaveCard::convert_filename(const char* name)
{
    std::memcpy(filename_buffer().data(), name, std::strlen(name) + 1);
    return filename_buffer().data();
}

// 0x005BC790
int SaveCard::get_free_blocks() const
{
    if (present_ == 0U) {
        return 0;
    }
    uint64_t available = 0;
    if (!fs::disk().free_space(&available)) {
        return 0;
    }
    const auto low = static_cast<uint32_t>(available);
    const auto high = static_cast<uint32_t>(available >> kHighWordShift);
    return static_cast<int>((low >> kBlockShift) + (high << kHighPartBlockShift));
}

// 0x005BCAE0
bool SaveCard::list_save_files(const char* pattern, memory::ListNode* list)
{
    const char* path = path_buffer().data();
    text::format_to(path_buffer(), "Save\\%s", pattern); // (the original: up to MAX_PATH into the 128 bytes)
    fs::list(path, [&](const fs::Found& found) {
        if (found.folder) {
            return true;
        }
        FileLoad* file = new_file(nullptr, this);
        // (no bounds: a long name runs into the fields after it, as in the original)
        std::memcpy(file->label().data(), found.name, std::strlen(found.name) + 1);
        fs::LocalTime time;
        fs::disk().local_time(found.write_time, &time);
        file->set_year(time.year);
        file->set_month(static_cast<uint8_t>(time.month));
        file->set_day(static_cast<uint8_t>(time.day));
        file->set_hour(static_cast<uint8_t>(time.hour));
        file->set_minute(static_cast<uint8_t>(time.minute));
        file->set_second(static_cast<uint8_t>(time.second));
        file->set_extent(found.size_low);
        file->set_unknown48(0);
        // At the end of the list.
        file->list_link().set_prev(list->prev());
        file->list_link().set_next(list);
        list->prev()->set_next(&file->list_link());
        list->set_prev(&file->list_link());
        return true;
    });
    return true;
}

// 0x005BC7F0
bool SaveCard::always_true(int /*unused*/) { return true; }

// 0x005BC8D0
Hub::Hub()
{
    for (int port = 0; port < 4; ++port) {
        for (int slot = 0; slot < 2; ++slot) {
            core::at(core::at(slots_, port), slot).slot = slot;
            core::at(core::at(slots_, port), slot).port = port;
        }
    }
    save_device_.set_present(kCardPresent);
}

// 0x005BC730 (one card, whichever port and slot)
SaveCard* Hub::get_storage_slot(int /*unused*/, int /*unused*/) { return &save_device_; }

// 0x005BC900
Hub* create_manager(bool create)
{
    if (!save_cards_state().card_hub() && create) {
        if (void* memory = memory::alloc_zeroed(sizeof(Hub), nullptr, 1)) {
            gsl::owner<save_cards::Hub*> made{
                memory::construct_in<Hub>(memory)}; // (its block's: release_manager frees it)
            save_cards_state().set_card_hub(made);
            ++ref_tally();
            return save_cards_state().card_hub();
        }
        save_cards_state().set_card_hub(nullptr);
    }
    ++ref_tally();
    return save_cards_state().card_hub();
}

// 0x005BC950
void release_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    if (save_cards_state().card_hub()) {
        memory::Hub::dealloc(save_cards_state().card_hub());
    }
    save_cards_state().set_card_hub(nullptr);
    ref_tally() = 0;
}

} // namespace save_cards
