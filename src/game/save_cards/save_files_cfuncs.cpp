// Script functions (commands) for the save files on the memory card (see save_files_cfuncs.h).

#include "game/save_cards/save_files_cfuncs.h"

#include "engine/core/str.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <span>
#include <string_view>
#include <gsl/assert>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/core/stubs.h"
#include "engine/script/cell.h"
#include "game/goals/goal_manager.h"
#include "game/save_cards/memcard.h"
#include "game/save_cards/memcard_cfuncs.h"
#include "game/save_cards/save_contents.h"
#include "formats/save_file_format.h"
#include "game/save_cards/save_types.h"
#include "engine/script/struct_buffer.h"
#include "engine/audio/music_player.h"
#include "engine/audio/stream_commands.h"
#include "engine/script/cscript.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/sys/game_config.h"
#include "formats/byte_stream.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "game/save_cards/save_cards_state.h"

namespace {

constexpr int kReserveComponentItemBytes = 0x10; // the temporary pools made for a save: item sizes and counts
constexpr int kReserveComponentCount = 50000;
constexpr int kReserveStructItemBytes = 0x08;
constexpr int kReserveStructCount = 50000;
constexpr int kReserveVectorItemBytes = 0x10;
constexpr int kReserveVectorCount = 10000;
constexpr int kMaxSummaryBytes = 100;        // MaxSummaryInfoSize
constexpr int kTypeLetterOffset = 7;      // the type's letter in a file label, after the 7 characters of the folder's name
constexpr int kHardwareUnlisted = 8;         // a platform number the tables don't list (treated like the PC's)

// Names (the checksums of their text).
constexpr uint32_t kActualFileName = 0x13C68F74u; // actual_file_name
constexpr uint32_t kDay = 0x1A5FD66Fu; // day
constexpr uint32_t kFileType = 0x11093FB5u; // FileType
constexpr uint32_t kFileTypeName = 0xFADDC0B8u; // file_type
constexpr uint32_t kHour = 0x8FE1EEB1u; // hour
constexpr uint32_t kIndex = 0x7F8C98FEu; // index
constexpr uint32_t kMainStructure = 0x671C9C00u; // MainStructure
constexpr uint32_t kMainStructureSize = 0x172C1344u; // MainStructureSize
constexpr uint32_t kMaxSummaryInfoSize = 0x6AFD2F7Fu; // MaxSummaryInfoSize
constexpr uint32_t kMaxTotalSize = 0xB35EB1D1u; // MaxTotalSize
constexpr uint32_t kMinutes = 0x5F94B55Cu; // minutes
constexpr uint32_t kMonth = 0x7149EFF9u; // month
constexpr uint32_t kSavevaultdata = 0xBB609E73u; // savevaultdata
constexpr uint32_t kSeconds = 0xD029F619u; // seconds
constexpr uint32_t kSpaceRequired = 0xEB3860C3u; // SpaceRequired
constexpr uint32_t kSummaryInfo = 0x703E60CAu; // SummaryInfo
constexpr uint32_t kSummaryInfoSize = 0x96635475u; // SummaryInfoSize
constexpr uint32_t kTotalFileSize = 0x3D1BCFEDu; // total_file_size
constexpr uint32_t kTotalSize = 0xECBB8262u; // TotalSize
constexpr uint32_t kType = 0x7321A8D6u; // type
constexpr uint32_t kYear = 0x447D8CC8u; // year

} // namespace

namespace save_cards {

namespace {

using script::Script;
using script::Struct;

int os_layer() { return game_config::hardware(); }
SaveCard* the_card() { return save_cards_state().card_hub()->get_storage_slot(0, 0); }


// The console builds' save folder (0x005B5F10, no source yet).
const char* save_folder() { return game_config::get_path_prefix(); }

// The card's delete of a console file or folder entry: on the PC the shared always-true method (0x005BC7F0).
// (the original passed the name's address: not read)
bool delete_card_file(SaveCard* card, const char* /*name*/) { return card->always_true(0); }

// A save file's handle deleted (its scalar deleting destructor).
void DeleteFile(FileLoad* file) { file->teardown(1); }

// The pool pairs the temporary pools belong to: components, structures, vectors.

// A pair's second pool made (as the pair's own creator, 0x0046F110 / 0x0046F190 / 0x0046F210, does).
void create_reserve_pool(memory::PoolPair& pools, int item_size, int count, const char* name)
{
    ++pools.current();
    script::create_pool(pools, item_size, count, name);
    --pools.current();
}
// A pair's second pool deleted: its block and itself, if the pair made it.
void delete_reserve_pool(memory::PoolPair& pools)
{
    ++pools.current();
    if (memory::SlabBank* pool = core::at(pools.pools(), pools.current())) {
        if (core::at(pools.made_here(), pools.current())) {
            memory::Hub::get_pull()->dealloc(pool->memory().data()); // (its destructor body, 0x005BA4E0)
            memory::Hub::get_pull()->dealloc(pool);
        }
        core::at(pools.pools(), pools.current()) = nullptr;
    }
    --pools.current();
}

} // namespace

// 0x005A6EA0
bool delete_mem_card_file(Struct* params, Script*)
{
    if (os_layer() != 4) {
        audio::pause_music(1);
        audio::freeze_stream(1);
    }
    SaveCard* card = the_card();
    if (!card) {
        if (os_layer() != 4) {
            audio::pause_music(-1);
            audio::freeze_stream(-1);
        }
        return false;
    }
    bool deleted = true;
    const char* xbox_directory = nullptr; // (read, not used on the PC)
    params->fetch_text("XBoxDirectoryName", &xbox_directory, 0);
    const char* card_file_name = nullptr;
    params->fetch_text("CardFileName", &card_file_name, 0);
    uint32_t type = 0;
    params->read_hash_value("Type", &type, 0);
    const char* user_file_name = ""; // ""
    params->fetch_text("UserFileName", &user_file_name, 0);

    bool removed = false;
    std::array<char, kSaveFileNameBytes> file{};
    switch (os_layer()) {
    case 1:
    case 2:
    case 3: {
        // the file in its folder: /<folder><file>/<folder><file>, the type from its last letter
        std::array<char, kSavePathBytes> full_name{};
        const char* path = card_file_name;
        if (!path) {
            const char* folder = save_folder();
            make_file_name(user_file_name, file, type);
            text::format_to(full_name, "/%s%s/%s%s", folder, file.data(), folder, file.data());
            path = full_name.data();
        }
        const std::string_view path_text(path);
        const uint32_t file_type = type_from_letter(core::at(path_text, path_text.size() - 1));
        // the folder: the first character, then up to the next '/'
        std::array<char, kSaveDirectoryBytes> directory{};
        core::at(directory, 0) = core::at(path_text, 0);
        int length = 1;
        for (const char c : path_text.substr(1)) {
            if (c == '/')
                break;
            core::at(directory, length++) = c;
        }
        core::at(directory, length) = '\0';
        if (!delete_card_file(card, path))
            deleted = false;
        std::array<char, kSavePathBytes> icon{};
        text::format_to(icon, "%s/icon.sys", directory.data());
        if (!delete_card_file(card, icon.data()))
            deleted = false;
        const char* icon_file = nullptr;
        switch (file_type) {
        case kSaveCreatedGoals:
            icon_file = "goals.ico";
            break;
        case kSaveCat:
            icon_file = "tricks.ico";
            break;
        case kSavePark:
            icon_file = "parked.ico";
            break;
        case kSaveCreatedGraphic:
            icon_file = "grafitti.ico";
            break;
        case kSaveNetworkSettings:
            icon_file = "network.ico";
            break;
        case kSaveOptionsAndPros:
            icon_file = "thps5.ico";
            break;
        default:
            break; // (icon.sys a second time)
        }
        if (icon_file)
            text::format_to(icon, "%s/%s", directory.data(), icon_file); // ("%s/goals.ico" ...)
        if (!delete_card_file(card, icon.data()))
            deleted = false;
        removed = card->remove_file(directory.data());
        break;
    }
    case 4:
    case game_config::kHardwareWin32:
        if (!card_file_name) {
            deleted = false;
            break;
        }
        removed = card->remove_file(card_file_name);
        break;
    case game_config::kHardwareGameCube: {
        if (card_file_name) {
            removed = delete_card_file(card, card_file_name);
            break;
        }
        const char* folder = save_folder();
        make_file_name(user_file_name, file, type);
        std::array<char, kSavePathBytes> full_name{};
        text::format_to(full_name, "%s%s%s%s", folder, file.data(), folder, file.data());
        removed = delete_card_file(card, full_name.data());
        break;
    }
    default:
        deleted = false;
        break;
    }
    if (deleted && !removed)
        deleted = false;
    if (os_layer() != 4) {
        audio::pause_music(-1);
        audio::freeze_stream(-1);
    }
    return deleted;
}

// 0x005A8500
bool get_mem_card_space_required(Struct* params, Script* script)
{
    uint32_t type = 0;
    params->read_hash_value(0u, &type, 0);
    const int size = type_file_size(type);
    script->args().get()->put_int(checksums::Name{kSpaceRequired}, save_blocks(checksums::Name{type}, size)); // SpaceRequired
    return true;
}

// 0x005A8550
bool mem_card_file_exists(Struct* params, Script* script)
{
    SaveCard* card = the_card();
    if (!card)
        return false;
    const char* name = nullptr;
    params->fetch_text("Name", &name, 0);
    uint32_t type = 0;
    params->read_hash_value("Type", &type, 0);
    if (!name || !type)
        return false;
    const char* type_file_name = nullptr; // (only made on the PC and platform 7)
    std::array<char, kSaveDirectoryBytes> path{};
    core::at(path, 0) = '\0';
    switch (os_layer()) {
    case 4:
    case game_config::kHardwareWin32: {
        type_file_name = get_type_file_name(type, name);
        const char* converted = card->convert_filename(type_file_name);
        if (!converted)
            return false;
        text::format_to(path, "%s", converted);
        break;
    }
    case 1:
    case 2:
    case 3:
    case game_config::kHardwareGameCube: {
        const char* folder = save_folder();
        std::array<char, kSaveFileNameBytes> file{};
        make_file_name(name, file, type);
        text::format_to(path, "/%s%s/%s%s", folder, file.data(), folder, file.data());
        break;
    }
    default:
        break;
    }
    FileLoad* file = card->unseal(path.data(), 1, 0);
    if (!file) {
        // (on the PC, the file that wouldn't open is removed - by the type's name made above)
        if (os_layer() == 4)
            card->remove_file(type_file_name);
        return false;
    }
    const int size = static_cast<int>(file->seek_to({.offset = 0, .origin = 2}));
    script->args().get()->put_int("total_file_size", save_blocks(checksums::Name{type}, size));
    file->drain();
    file->shut();
    DeleteFile(file);
    return true;
}

// 0x005A8BB0
bool clear_mem_card_data_for_upload(Struct*, Script*)
{
    Struct*& kept = save_cards_state().kept_params();
    if (kept) {
        kept->get_clear();
        script::delete_struct(kept);
        kept = nullptr;
    }
    save_cards_state().set_kept_type(0);
    return true;
}

// 0x005A8DA0
bool create_temporary_mem_card_pools(Struct*, Script*)
{
    if (core::at(script::component_pools().pools(), script::component_pools().current() + 1))
        return true;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    memory::push_mem_profile("Mem card CComponent");
    create_reserve_pool(script::component_pools(), kReserveComponentItemBytes, kReserveComponentCount, "Reserve CComponent");
    memory::pop_mem_profile();
    memory::push_mem_profile("Mem card CStruct");
    create_reserve_pool(script::struct_pools(), kReserveStructItemBytes, kReserveStructCount, "Reserve CStruct");
    memory::pop_mem_profile();
    memory::push_mem_profile("Mem card CVector");
    create_reserve_pool(script::vector_pools(), kReserveVectorItemBytes, kReserveVectorCount, "Reserve CVector");
    memory::pop_mem_profile();
    memory::Hub::get_pull()->leave_heap_scope();
    return true;
}

// 0x005A9700
bool remove_temporary_mem_card_pools(Struct*, Script*)
{
    delete_reserve_pool(script::component_pools());
    delete_reserve_pool(script::struct_pools());
    delete_reserve_pool(script::vector_pools());
    return true;
}

// 0x005A8BF0
bool get_save_info(Struct* params, Script* script)
{
    uint32_t type = 0;
    params->read_hash_value(kType, &type, 0); // type
    gsl::owner<Struct*> contents = script::spawn_param_bag();
    if (contents)
        contents = new (contents) script::Struct;
    write_save_contents(contents, type);
    const int main_size = static_cast<int>(script::measure_buffer_bytes(contents, 100000));
    gsl::owner<Struct*> summary = script::spawn_param_bag();
    if (summary)
        summary = new (summary) script::Struct;
    Expects(summary != nullptr); // (out of memory: the original went on through null)
    write_summary(contents, *summary, type);
    const int summary_size = static_cast<int>(script::measure_buffer_bytes(summary, 100000));
    Struct* results = script->args().get();
    results->put_int(checksums::Name{kMainStructureSize}, main_size);                       // MainStructureSize
    results->put_int(checksums::Name{kSummaryInfoSize}, summary_size);                    // SummaryInfoSize
    results->put_int(checksums::Name{kMaxSummaryInfoSize}, kMaxSummaryBytes);                             // MaxSummaryInfoSize
    results->put_int(checksums::Name{kTotalSize}, main_size + summary_size + kFileHeaderBytes); // TotalSize
    results->put_int(checksums::Name{kMaxTotalSize}, type_file_size(type));            // MaxTotalSize
    results->append_struct_ref(kSummaryInfo, summary);               // SummaryInfo
    results->append_struct_ref(kMainStructure, contents);              // MainStructure
    return true;
}

// --- the directory listing and saving ----------------------------------------------------------------------

namespace {

constexpr int kMaxFiles = 75;
constexpr uint32_t kCorrupt = 0x492676A6;  // "corrupt"
constexpr uint32_t kFileName = 0xC3F4169A; // "FileName"

// The first bytes of a listed file are read here (0x007CEC58): the header (save_file_format.h) and the start of the
// summary (0x007CEC6C).
std::array<uint8_t, kListedFileBytes>& listed_file()
{
    static std::array<uint8_t, kListedFileBytes> listed_file{};
    return listed_file;
}

// The i-th file of a list (its nodes from the head's next; each file is its own node).
FileLoad* list_entry(memory::ListNode* head, int index)
{
    const memory::ListNode* node = head->next();
    for (int i = 0; i < index; ++i)
        node = node->next();
    return static_cast<FileLoad*>(node->data());
}

// A save file's type from its name: on the consoles its letter after the folder, on the PC its extension.
uint32_t listed_file_type(const FileLoad* file, int folder_length)
{
    switch (os_layer()) {
    case 1:
    case 2:
    case 3:
    case game_config::kHardwareGameCube:
        return type_from_letter(core::at(file->label(), folder_length + kTypeLetterOffset));
    case 4:
    case game_config::kHardwareWin32:
    case kHardwareUnlisted: {
        // (the music and sounds kept going while the files are read)
        audio::tick_frame();
        sound::update_voice_states();
        const std::string_view label(file->label().data());
        const char* extension = label.size() >= 4 ? label.substr(label.size() - 4).data() : label.data();
        if (_stricmp(extension, ".NWS") == 0)
            return kSaveNetworkSettings;
        if (_stricmp(extension, ".SKA") == 0)
            return kSaveOptionsAndPros;
        if (_stricmp(extension, ".PRK") == 0)
            return kSavePark;
        if (_stricmp(extension, ".CAS") == 0)
            return kSaveCas;
        if (_stricmp(extension, ".CAT") == 0)
            return kSaveCat;
        if (_stricmp(extension, ".GOA") == 0)
            return kSaveCreatedGoals;
        if (_stricmp(extension, ".GRF") == 0)
            return kSaveCreatedGraphic;
        return 0;
    }
    default:
        return 0;
    }
}

// The entry marked corrupt, with the damaged-file text as its FileName.
void mark_corrupt(Struct* entry, uint32_t text)
{
    entry->put_name_key(checksums::Name{0u}, kCorrupt);
    entry->append_word(kFileName, script::fetch_text(text));
}

} // namespace

// 0x005A8F70
bool get_mem_card_directory_listing(Struct* params, Script* script)
{
    constexpr uint32_t kDirectoryListing = 0xD8580FB5;
    constexpr uint32_t kFilesLimitReached = 0x4EEC27B5;
    constexpr uint32_t kTotalFiles = 0x56B7A7F6;
    script->args().get()->erase_module(kDirectoryListing);
    script->args().get()->erase_module(kFilesLimitReached);
    script->args().get()->erase_module(kTotalFiles);
    uint32_t wanted_type = 0;
    params->read_hash_value(kFileType, &wanted_type, 0); // FileType
    SaveCard* card = the_card();
    if (!card)
        return true;

    // the card's files (a lists::Foremost<File> on the stack)
    memory::ListNode head;
    head.make_head();
    card->list_save_files("*", &head);

    // into the temporary component and structure pools, if they exist
    bool switched = false;
    if (core::at(script::component_pools().pools(), script::component_pools().current() + 1)) {
        ++script::component_pools().current();
        ++script::struct_pools().current();
        switched = true;
    }
    int listed = 0;
    std::array<Struct*, kMaxFiles> entries{};
    const char* folder = save_folder();
    const int folder_length = static_cast<int>(std::strlen(folder));
    int file_count = 0;
    for (const memory::ListNode* node = head.next(); node->data() != memory::ListNode::head_marker() && node;
         node = node->next())
        ++file_count;
    debug::printf(
        "ScriptGetMemCardDirectoryListing: DirList Start\n"); // "ScriptGetMemCardDirectoryListing: DirList Start\n"
    int game_files = 0;
    for (int i = 0; i < file_count; ++i) {
        FileLoad* file = list_entry(&head, i);
        if (std::memcmp(file->label().data(), folder, static_cast<size_t>(folder_length)) != 0)
            continue;
        ++game_files;
        const uint32_t type = listed_file_type(file, folder_length);
        if (wanted_type && type != wanted_type)
            continue;
        if (listed >= kMaxFiles)
            continue;
        gsl::owner<Struct*> entry = script::spawn_param_bag();
        if (entry)
            entry = new (entry) script::Struct;
        entry->put_name_key(checksums::Name{kFileTypeName}, type);     // file_type
        entry->put_int(checksums::Name{kIndex}, listed);        // index
        entry->put_int(checksums::Name{kYear}, file->year());   // year
        entry->put_int(checksums::Name{kMonth}, file->month());  // month
        entry->put_int(checksums::Name{kDay}, file->day());    // day
        entry->put_int(checksums::Name{kHour}, file->hour());   // hour
        entry->put_int(checksums::Name{kMinutes}, file->minute()); // minutes
        entry->put_int(checksums::Name{kSeconds}, file->second()); // seconds
        std::array<char, kSaveDirectoryBytes> path{};
        if (os_layer() == game_config::kHardwareGameCube)
            text::format_to(path, "%s", file->label().data());
        else
            text::format_to(path, "Save/%s", file->label().data());
        entry->append_word(kActualFileName, path.data()); // actual_file_name
        constexpr uint32_t kDamagedFile = 0x2C7CD632;
        constexpr uint32_t kNgcDamagedFile = 0xFD4AA64E;
        FileLoad* opened = card->unseal(path.data(), 1, 0);
        if (!opened) {
            mark_corrupt(entry, kDamagedFile);
        } else {
            opened->seek_to({.offset = 0, .origin = 0});
            opened->get_take_in(listed_file().data(), static_cast<int>(kListedFileBytes));
            util::ByteReader reader(listed_file().data(), sizeof(listed_file()));
            SaveFileHeader listed_header;
            read_save_file_header(reader, &listed_header);
            const uint8_t* listed_summary = reader.cursor();
            const int summary_size = listed_header.summary_size;
            const bool bad =
                summary_size > kMaxSummarySize ||
                checksums::generate_crc_from_buffer(listed_summary, summary_size) != listed_header.summary_crc;
            const int size = static_cast<int>(opened->seek_to({.offset = 0, .origin = 2}));
            entry->put_int(checksums::Name{kTotalFileSize}, save_blocks(checksums::Name{type}, size)); // total_file_size
            if (bad || listed_header.version != type_version(type)) {
                mark_corrupt(entry, os_layer() == game_config::kHardwareGameCube ? kNgcDamagedFile : kDamagedFile);
            } else {
                gsl::owner<Struct*> summary = script::spawn_param_bag();
                if (summary)
                    summary = new (summary) script::Struct;
                script::decode_from_bytes(summary, listed_summary, reader.remaining());
                entry->append_structure_if_any(summary);
                if (summary) {
                    summary->get_clear();
                    script::delete_struct(summary);
                }
            }
            opened->shut();
            DeleteFile(opened);
        }
        core::at(entries, listed++) = entry;
    }
    if (switched) {
        --script::component_pools().current();
        --script::struct_pools().current();
    }
    // the list's nodes deleted (each file is its own node)
    for (const memory::ListNode* node = head.next(); node->data() != memory::ListNode::head_marker();) {
        const memory::ListNode* next = node->next();
        const memory::ListNode* following = next->is_head() ? nullptr : next;
        static_cast<FileLoad*>(node->data())->teardown(1);
        node = following;
        if (!node)
            break;
    }
    script->args().get()->put_int(checksums::Name{kTotalFiles}, game_files);
    if (game_files >= kMaxFiles)
        script->args().get()->put_name_key(checksums::Name{0u}, kFilesLimitReached);
    if (listed) {
        // newest first (a bubble sort by the dates)
        bool swapped = false;
        for (bool again = true; again; again = swapped) {
            swapped = false;
            for (int j = 0; j < listed - 1; ++j) {
                if (is_later(core::at(entries, j + 1), core::at(entries, j))) {
                    Struct* later = core::at(entries, j + 1);
                    core::at(entries, j + 1) = core::at(entries, j);
                    core::at(entries, j) = later;
                    swapped = true;
                }
            }
        }
        gsl::owner<script::Array*> listing = script::alloc_script_array();
        if (listing)
            listing = new (listing) script::Array;
        listing->configure_size_kind(static_cast<uint32_t>(listed), script::QbValueKind::kStruct);
        for (int j = 0; j < listed; ++j)
            listing->set_cell(static_cast<uint32_t>(j), script::Cell::of(core::at(entries, j)));
        script->args().get()->append_array_ref(kDirectoryListing, listing);
    }
    // the head unlinked (its destructor's body)
    head.prev()->set_next(head.next());
    head.next()->set_prev(head.prev());
    return true;
}

// 0x005A7420: emit_into_buffer's callback while a save is written on the PC: the music and sounds kept going.
void keep_sound_going()
{
    audio::tick_frame();
    sound::update_voice_states();
}

// 0x005A97E0
bool save_to_memory_card(Struct* params, Script*)
{
    if (os_layer() == game_config::kHardwareGameCube)
        create_temporary_mem_card_pools(nullptr, nullptr);
    if (os_layer() != 4) {
        audio::pause_music(1);
        audio::freeze_stream(1);
    }
    bool& out_of_space = save_cards_state().out_of_space();
    out_of_space = false;
    const char* name = ""; // ""
    params->fetch_text("Name", &name, 0);
    uint32_t type = 0;
    params->read_hash_value("Type", &type, 0);
    int vault_data = 0;
    params->fetch_int_value(kSavevaultdata, &vault_data, 0); // savevaultdata
    if (vault_data) {
        type = save_cards_state().kept_type();
    } else if (type == kSaveOptionsAndPros) {
        // the goal manager's totals brought up to date (0x0055C090, no source yet)
        goals::get_objective_board()->level_unload();
    }
    // into the temporary pools, if they exist
    bool switched = false;
    if (core::at(script::component_pools().pools(), script::component_pools().current() + 1)) {
        ++script::component_pools().current();
        ++script::struct_pools().current();
        ++script::vector_pools().current();
        switched = true;
    }
    gsl::owner<Struct*> contents = script::spawn_param_bag();
    if (contents)
        contents = new (contents) script::Struct;
    if (vault_data)
        contents->append_structure_if_any(save_cards_state().kept_params());
    else
        write_save_contents(contents, type);
    gsl::owner<Struct*> summary = script::spawn_param_bag();
    if (summary)
        summary = new (summary) script::Struct;
    Expects(summary != nullptr); // (out of memory: the original went on through null)
    write_summary(contents, *summary, type);
    summary->append_word("filename", name);

    FileLoad* file = nullptr;
    uint8_t* header = nullptr;
    uint8_t* padding = nullptr;
    bool saved = false;
    const int file_size = type_file_size(type);
    const uint32_t main_size = script::measure_buffer_bytes(contents, static_cast<uint32_t>(file_size));
    const uint32_t summary_size = script::measure_buffer_bytes(summary, kMaxSummarySize);
    const uint32_t total = main_size + summary_size + kSaveFileHeaderSize;
    const uint32_t padding_size = static_cast<uint32_t>(file_size) - total;
    if (padding_size != 0 && os_layer() != game_config::kHardwareGameCube) {
        padding = static_cast<uint8_t*>(memory::alloc_block(padding_size));
        if (padding_size > 0)
            std::memset(padding, kSaveFileMask, padding_size);
    }
    SaveCard* card = the_card();
    // (the second test is a card method that's the shared always-true stub, 0x005B9760)
    if (card && stubs::return_true()) {
        std::array<char, kSavePathBytes> path{};
        bool named = true;
        switch (os_layer()) {
        case game_config::kHardwareGameCube: {
            std::array<char, kSavePathBytes> folder_name{};
            make_folder_file_name(type, name, folder_name.data());
            text::format_to(path, "%s%s", folder_name.data(), folder_name.data());
            break;
        }
        case 4:
        case game_config::kHardwareWin32: {
            core::at(path, 0) = '\0';
            text::copy_unbounded(path.data(), get_type_file_name(type, name));
            break;
        }
        default:
            named = false;
            break;
        }
        if (named) {
            file = card->unseal(path.data(), kOpenWriteAlways, file_size);
            out_of_space = card->fault() == 2;
        }
        if (file) {
            header = static_cast<uint8_t*>(memory::alloc_block(total));
            std::memset(header, 0, kSaveFileHeaderSize);
            if (os_layer() != game_config::kHardwareGameCube) {
                SaveFileHeader file_header;
                file_header.version = type_version(type);
                file_header.size = static_cast<int>(total);
                file_header.summary_size = static_cast<int>(summary_size);
                const std::span<uint8_t> body = std::span<uint8_t>(header, total).subspan(kSaveFileHeaderSize);
                const bool pc = os_layer() == 4 || os_layer() == 7;
                script::emit_into_buffer(summary, body.data(), summary_size, false, pc ? keep_sound_going : nullptr);
                file_header.summary_crc = checksums::generate_crc_from_buffer(body.data(), static_cast<int>(summary_size));
                const std::span<uint8_t> main_part = body.subspan(summary_size);
                script::emit_into_buffer(contents, main_part.data(), main_size, false, nullptr);
                if (type == kSaveNetworkSettings) {
                    for (uint8_t& byte : main_part.first(main_size))
                        byte += kSaveFileMask; // (read back with the mask taken off)
                }
                util::ByteWriter header_writer(header, kSaveFileHeaderSize);
                write_save_file_header(header_writer, file_header);
                file_header.checksum = checksums::generate_crc_from_buffer(header, static_cast<int>(total));
                header_writer.seek(0);
                write_save_file_header(header_writer, file_header);
                int written = file->emit_out(header, static_cast<int>(total));
                out_of_space = card->fault() == 2;
                if (written == ints::as_signed(total)) {
                    if (os_layer() == game_config::kHardwareGameCube || padding_size == 0) {
                        saved = true;
                    } else {
                        written = file->emit_out(padding, static_cast<int>(padding_size));
                        out_of_space = card->fault() == 2;
                        saved = written == ints::as_signed(padding_size);
                    }
                }
            }
        }
    }
    if (padding)
        memory::free_malloc(padding);
    if (header)
        memory::free_malloc(header);
    if (file) {
        if (!file->drain())
            saved = false;
        if (!file->shut())
            saved = false;
        DeleteFile(file);
    }
    if (summary) {
        summary->get_clear();
        script::delete_struct(summary);
    }
    if (contents) {
        contents->get_clear();
        script::delete_struct(contents);
    }
    if (switched) {
        --script::component_pools().current();
        --script::struct_pools().current();
        --script::vector_pools().current();
    }
    if (os_layer() != 4) {
        audio::pause_music(-1);
        audio::freeze_stream(-1);
    }
    if (os_layer() == game_config::kHardwareGameCube)
        remove_temporary_mem_card_pools(nullptr, nullptr);
    return saved;
}

} // namespace save_cards
