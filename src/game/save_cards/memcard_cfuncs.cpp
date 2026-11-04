// Script functions (commands) for the memory card.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/save_cards/memcard_cfuncs.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <array>
#include <span>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/core/stubs.h"
#include "game/goals/goal_manager.h"
#include "game/save_cards/memcard.h"
#include "formats/save_file_format.h"
#include "game/save_cards/save_contents.h"
#include "game/save_cards/save_types.h"
#include "engine/audio/music_player.h"
#include "engine/audio/stream_commands.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "engine/sys/game_config.h"
#include "formats/byte_stream.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "game/save_cards/save_cards_state.h"
#include "engine/core/checksums.h"

namespace {

constexpr int kFilesLeftNoLimit = 1000000; // (the files left on a card: always this)
constexpr int kMaxSaveBytes = 500000;  // a save header claiming more is rejected
constexpr int kMaxThps4Files = 75;
constexpr uint32_t kApplyUnknownSection = 0xB39D7CC4U; // text unknown

// Names (the checksums of their text).
constexpr uint32_t kAll = 0xC4E78E22U;                    // all
constexpr uint32_t kApplyCustomSkater = 0x16B506C0U;      // ApplyCustomSkater
constexpr uint32_t kApplyStory = 0x33EC233FU;             // ApplyStory
constexpr uint32_t kApplyStorySkater = 0xDC7EA3CEU;       // ApplyStorySkater
constexpr uint32_t kMaxTHPS4FilesAllowed = 0x7CC9E431U;   // MaxTHPS4FilesAllowed
constexpr uint32_t kPostLoadFromMemoryCard = 0x0EB9402DU; // post_load_from_memory_card
constexpr uint32_t kType = 0x7321A8D6U;                   // type

} // namespace

namespace save_cards {

// PartyMod's free space (code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md): with a card
// (its present flag), the disk's free 16 KB blocks, clamped to 0x7FFE0001 so that scripts can add to it without
// overflowing (the original's count, card->get_free_blocks(), is unclamped).
int32_t free_disk_blocks(SaveCard* card)
{
    if (card->present() == 0U) {
        return 0;
    }
    ULARGE_INTEGER free_space;
    ULARGE_INTEGER total_space;
    if (GetDiskFreeSpaceExA(nullptr, &free_space, &total_space, nullptr) == FALSE) {
        return 0;
    }
    uint64_t blocks = free_space.QuadPart >> kBlockShift;
    constexpr uint64_t kLimit = INT32_MAX - UINT16_MAX * 2; // (a number that leaves room for a bit of maths)
    blocks = std::min(blocks, kLimit);
    return static_cast<int32_t>(blocks);
}

namespace {

constexpr uint32_t kSpaceAvailable = 0x0C37C363; // "SpaceAvailable"
constexpr uint32_t kFilesLeft = 0x0855B2FC;      // "FilesLeft"


} // namespace

// 0x005A6E40. Results for the script: SpaceAvailable (16 KB blocks; 0 until a card is found) and FilesLeft
// (always 1000000). False without a card.
bool get_mem_card_space_available(script::Struct* /*params*/, script::Script* script)
{
    script->args().get()->put_int(checksums::Name{kSpaceAvailable}, 0);
    script->args().get()->put_int(checksums::Name{kFilesLeft}, kFilesLeftNoLimit);
    SaveCard* card = save_cards_state().card_hub()->get_storage_slot(0, 0);
    if (!card) {
        return false;
    }
    // Originally from PartyMod Fixes (its call at 0x005A6E7E): the clamped disk count in place of the original's
    // card->get_free_blocks().
    script->args().get()->put_int(checksums::Name{kSpaceAvailable}, free_disk_blocks(card));
    return true;
}

// --- LoadFromMemoryCard -------------------------------------------------------------------------------------

namespace {

int os_layer() { return game_config::hardware(); }

// Pause (1), unpause (0) or restore (-1) the music and the music streams.
void pause_music(int pause) { audio::pause_music(pause); }
void freeze_stream(int pause) { audio::freeze_stream(pause); }

// A save file: its header (save_file_format.h), then two structures written by the script writer (0x0047A0C0 reads
// one back and returns the end of what it read) - the summary and the contents.
uint8_t* read_structure(script::Struct* into, uint8_t* data, size_t size)
{
    return script::decode_from_bytes(into, data, size);
}

constexpr uint32_t kNoApply = 0x8C6808D4;        // integer parameter: keep the contents in 0x007CECD4 instead
constexpr uint32_t kLoadFailed = 0xFBF9C22A;     // checksum left in the script's parameters on a bad file
constexpr uint32_t kObfuscatedType = 0xCA41692D; // its second structure's bytes are stored plus 0x69
constexpr uint32_t kAlsoApplyType = 0x61A1BC57;  // applied even when kept

// A save file's path on the card, by platform: false when it has none.
bool make_load_path(std::array<char, kSavePathBytes>& path, const char* name, uint32_t type)
{
    core::at(path, 0) = '\0';
    bool named = true;
    switch (os_layer()) {
    case 1:
    case 2:
    case 3: { // console folders: /<dir><file>/<dir><file>
        const char* folder = game_config::get_path_prefix();
        std::array<char, kSaveFileNameBytes> file{};
        make_file_name(name, file, type);
        text::format_to(path, "/%s%s/%s%s", folder, file.data(), folder, file.data());
        break;
    }
    case game_config::kHardwareGameCube: {
        std::array<char, kSavePathBytes> file{};
        make_folder_file_name(type, name, file.data());
        text::format_to(path, "%s%s", file.data(), file.data());
        break;
    }
    case 4:
    case game_config::kHardwareWin32: {
        const char* converted = save_cards::SaveCard::convert_filename(get_type_file_name(type, name));
        if (!converted) {
            named = false;
        } else {
            text::format_to(path, "%s", converted);
        }
        break;
    }
    default:
        named = false;
        break;
    }
    return named;
}

// LoadFromMemoryCard's structures, what it was asked for and by whom.
struct LoadRequest {
    script::Struct* header{};
    script::Struct* contents{};
    uint32_t type{};
    int no_apply{};
    script::Script* script{};
    script::Struct* params{};
};

// An open save file read: the right size, version and header, its contents applied or kept; true when it was
// read (kLoadFailed left in the script's parameters when it wasn't).
bool load_save_file(FileLoad* file, const LoadRequest& request)
{
    script::Struct* header = request.header;
    script::Struct* contents = request.contents;
    const uint32_t type = request.type;
    const int no_apply = request.no_apply;
    script::Script* script = request.script;
    script::Struct* params = request.params;
    uint8_t* data = nullptr;
    bool loaded = false;
    const int size = static_cast<int>(file->seek_to({.offset = 0, .origin = 2}));
    bool bad = size != type_file_size(type);
    if (!bad) {
        data = static_cast<uint8_t*>(memory::alloc_block(size));
        file->seek_to({.offset = 0, .origin = 0});
        if (file->get_take_in(data, size) == size) {
            util::ByteReader reader(data, static_cast<size_t>(size));
            SaveFileHeader file_header;
            read_save_file_header(reader, &file_header);
            // The original also rejected the file unless
            //     Checksums::generate_crc_from_buffer(data, file_header.size) == <the checksum read>
            // (with the checksum field cleared) - the test PartyMod NOPs (its patch at 0x005A8A01).
            // NOT ORIGINAL: a header claiming more bytes than the file has is rejected too (the original
            // then unmasked the NetworkSettings contents past the end of its buffer).
            bad = !reader.ok() || file_header.size > kMaxSaveBytes || file_header.size > size ||
                  file_header.version != type_version(type);
            if (!bad) {
                auto end = static_cast<size_t>(file_header.size);
                const std::span<uint8_t> file_bytes(data, static_cast<size_t>(size));
                uint8_t* rest =
                    read_structure(header, file_bytes.subspan(kSaveFileHeaderSize).data(), end - kSaveFileHeaderSize);
                auto used = static_cast<size_t>(rest - data);
                const size_t count = used < end ? end - used : 0;
                if (type == kObfuscatedType && count != 0) {
                    for (uint8_t& byte : file_bytes.subspan(used, count)) {
                        byte -= kSaveFileMask;
                    }
                }
                read_structure(contents, rest, count);
                if (no_apply == 0) {
                    apply_loaded_contents(type, contents, script, params);
                } else {
                    // (a null structure here would crash, as in the original; the previous one leaks)
                    gsl::owner<script::Struct*> kept = script::new_struct();
                    save_cards_state().kept_params() = kept;
                    kept->append_structure_if_any(contents);
                    save_cards_state().set_kept_type(type);
                    if (type == kAlsoApplyType) {
                        apply_loaded_contents(type, contents, script, nullptr);
                    }
                }
                loaded = true;
                if (no_apply == 0) {
                    script::launch_script(kPostLoadFromMemoryCard, params, nullptr, false, nullptr);
                }
            }
        }
    }
    if (bad) {
        script->args().get()->put_name_key(checksums::Name{0U}, kLoadFailed);
    }
    file->shut();
    file->teardown(1); // delete
    if (data) {
        memory::free_malloc(data);
    }
    return loaded;
}

} // namespace

// 0x005A8730. LoadFromMemoryCard Name=<string> Type=<checksum> [<kNoApply>=<integer>]: reads the save file and
// applies it (or keeps its contents in 0x007CECD4 and its type in 0x007CECD8 when kNoApply is non-zero). True if
// the file was read. A file of the wrong size, too big, of another version or (originally) with a bad checksum
// leaves kLoadFailed in the script's parameters. Originally from PartyMod Fixes: the checksum isn't checked.
bool load_from_memory_card(script::Struct* params, script::Script* script)
{
    if (os_layer() != 4) {
        pause_music(1);
        freeze_stream(1);
    }
    gsl::owner<script::Struct*> header = script::new_struct();   // the file's first structure (read, then dropped)
    gsl::owner<script::Struct*> contents = script::new_struct(); // the second: what's loaded
    const char* name = "";                   // ""
    uint32_t type = 0;
    bool loaded = false;
    int no_apply = 0;
    params->fetch_int_value(kNoApply, &no_apply, 0);
    SaveCard* card = save_cards_state().card_hub()->get_storage_slot(0, 0);
    // (the second test is a card method that's the shared always-true stub, 0x005B9760: always true)
    if (card && stubs::return_true()) {
        params->fetch_text("Name", &name, 0);
        params->read_hash_value("Type", &type, 0);
        std::array<char, kSavePathBytes> path{};
        const bool named = make_load_path(path, name, type);
        FileLoad* file = named ? card->unseal(path.data(), 1, 0) : nullptr;
        if (file) {
            loaded = load_save_file(file, {.header = header,
                                           .contents = contents,
                                           .type = type,
                                           .no_apply = no_apply,
                                           .script = script,
                                           .params = params});
        }
    }
    script::free_struct(header);
    script::free_struct(contents);
    if (os_layer() != 4) {
        pause_music(-1);
        freeze_stream(-1);
    }
    return loaded;
}

// --- the save state flags -----------------------------------------------------------------------------------

// 0x005A6550
bool save_failed_due_to_insufficient_space(script::Struct* /*params*/, script::Script* /*script*/)
{
    return save_cards_state().out_of_space();
}

// 0x005A6560
bool set_sections_to_apply_when_loading(script::Struct* params, script::Script* /*script*/)
{
    debug::printf(
        "ScriptSetSectionsToApplyWhenLoading was called.........................\n"); // "ScriptSetSectionsToApplyWhenLoading was called..."
    uint32_t& sections = sections_to_apply_when_loading();
    sections = 0;
    if (params->has_bare_flag(kApplyCustomSkater)) { // ApplyCustomSkater
        sections |= kSectionCustomSkater;
    }
    if (params->has_bare_flag(kApplyStorySkater)) { // ApplyStorySkater
        sections |= 4;
    }
    if (params->has_bare_flag(kApplyStory)) { // ApplyStory
        sections |= 2;
    }
    if (params->has_bare_flag(kApplyUnknownSection)) { // (name unknown)
        sections |= 1;
    }
    if (params->has_bare_flag(kAll)) { // all
        sections = kAllSections;
    }
    return true;
}

// 0x005A6600
bool get_loaded_custom_skater(script::Struct* /*params*/, script::Script* /*script*/)
{
    return save_cards_state().loaded_custom_skater();
}

uint32_t& sections_to_apply_when_loading()
{
    static uint32_t sections_to_apply_when_loading{};
    return sections_to_apply_when_loading;
}

// --- the card and the saves' summaries ----------------------------------------------------------------------

namespace {

SaveCard* the_card() { return save_cards_state().card_hub()->get_storage_slot(0, 0); }

// The script pools' pairs whose current pool the temporary pools switch: components, structures, vectors.

} // namespace

// 0x005A6D30
bool get_most_recent_save(script::Struct* params, script::Script* script)
{
    script->args().get()->erase_module("MostRecentSave");
    script::Array* saves = nullptr;
    params->fetch_list(0U, &saves, 0);
    if (!saves) {
        return false;
    }
    uint32_t type = 0;
    params->read_hash_value(0U, &type, 0);
    script::Struct* best = nullptr;
    for (uint32_t i = 0; i < saves->extent(); ++i) {
        auto* save = saves->struct_at(i);
        uint32_t file_type = 0;
        save->read_hash_value("file_type", &file_type, 0);
        if (type != 0 && file_type != type) { // (any type when none is asked for)
            continue;
        }
        // a bad save only counts on the PC (4) and platform 5
        if ((save->has_bare_flag("BadVersion") || save->has_bare_flag("Corrupt")) && os_layer() != 4 &&
            os_layer() != game_config::kHardwareGameCube) {
            continue;
        }
        if (!best || is_later(save, best)) {
            best = save;
        }
    }
    if (!best) {
        return false;
    }
    script->args().get()->put_sub_struct("MostRecentSave", best);
    return true;
}

// 0x005A7210
bool format_card(script::Struct* /*params*/, script::Script* /*script*/)
{
    pause_music(1);
    freeze_stream(1);
    bool formatted = false;
    if (the_card()) {
        formatted = stubs::return_false(); // (the card's Format: the shared always-false stub, 0x005B9E50)
    }
    pause_music(-1);
    freeze_stream(-1);
    return formatted;
}

// 0x005A7260
bool card_is_in_slot(script::Struct* /*params*/, script::Script* /*script*/) { return the_card() != nullptr; }

// 0x005A7280
bool card_is_formatted(script::Struct* /*params*/, script::Script* /*script*/)
{
    if (!the_card()) {
        return false;
    }
    return stubs::return_true(); // (the card's IsFormatted: the shared always-true stub, 0x005B9760)
}

// 0x005A72A0
bool get_summary_info(script::Struct* params, script::Script* script)
{
    constexpr uint32_t kVaultData = 0xC78DABF6; // "VaultData": the kept contents' summary
    uint32_t type = 0;
    params->read_hash_value(0U, &type, 0);
    int vault_data = 0;
    params->fetch_int_value(kVaultData, &vault_data, 0);
    if (vault_data != 0) {
        write_summary(save_cards_state().kept_params(), *script->args().get(), save_cards_state().kept_type());
        return true;
    }
    if (type == kSaveOptionsAndPros) {
        // the goal manager's totals brought up to date (0x0055C090, no source yet)
        goals::get_objective_board()->level_unload();
    }
    write_summary(nullptr, *script->args().get(), type);
    return true;
}

// 0x005A7430
bool get_mem_card_data_for_upload(script::Struct* /*params*/, script::Script* script)
{
    script->args().get()->put_name_key(checksums::Name{kType}, save_cards_state().kept_type()); // type
    return true;
}

// 0x005A7450
bool get_max_thps4_files_allowed(script::Struct* /*params*/, script::Script* script)
{
    script->args().get()->put_int(checksums::Name{kMaxTHPS4FilesAllowed}, kMaxThps4Files); // MaxTHPS4FilesAllowed
    return true;
}

// 0x005A7470
bool switch_to_temp_pools_if_they_exist(script::Struct* /*params*/, script::Script* /*script*/)
{
    ++script::component_pools().current();
    ++script::struct_pools().current();
    ++script::vector_pools().current();
    if (core::at(script::component_pools().pools(), script::component_pools().current()) &&
        core::at(script::struct_pools().pools(), script::struct_pools().current()) &&
        core::at(script::vector_pools().pools(), script::vector_pools().current())) {
        return true;
    }
    --script::component_pools().current();
    --script::struct_pools().current();
    --script::vector_pools().current();
    return false;
}

// 0x005A74F0
bool switch_to_regular_pools(script::Struct* /*params*/, script::Script* /*script*/)
{
    if ((script::component_pools().current() != 0) || (script::struct_pools().current() != 0) ||
        (script::vector_pools().current() != 0)) {
        --script::component_pools().current();
        --script::struct_pools().current();
        --script::vector_pools().current();
    }
    return true;
}

} // namespace save_cards
