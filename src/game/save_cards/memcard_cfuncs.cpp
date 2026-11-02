// Script functions (commands) for the memory card.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/save_cards/memcard_cfuncs.h"

#include <windows.h>

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

namespace save_cards {

// PartyMod's free space (code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md): with a card
// (its present flag), the disk's free 16 KB blocks, clamped to 0x7FFE0001 so that scripts can add to it without
// overflowing (the original's count, card->get_free_blocks(), is unclamped).
int32_t free_disk_blocks(SaveCard* card)
{
    if (!card->present_)
        return 0;
    ULARGE_INTEGER free_space;
    ULARGE_INTEGER total_space;
    if (!GetDiskFreeSpaceExA(nullptr, &free_space, &total_space, nullptr))
        return 0;
    uint64_t blocks = free_space.QuadPart >> 14;
    constexpr uint64_t kLimit = INT32_MAX - UINT16_MAX * 2; // (a number that leaves room for a bit of maths)
    if (blocks > kLimit)
        blocks = kLimit;
    return static_cast<int32_t>(blocks);
}

namespace {

constexpr uint32_t kSpaceAvailable = 0x0C37C363; // "SpaceAvailable"
constexpr uint32_t kFilesLeft = 0x0855B2FC;      // "FilesLeft"

// The calling script's own parameters (Scripts::CScript +0x14): where commands leave results for it.
script::Struct* result_params(script::Script* script) { return script->args_.get(); }

} // namespace

// 0x005A6E40. Results for the script: SpaceAvailable (16 KB blocks; 0 until a card is found) and FilesLeft
// (always 1000000). False without a card.
bool get_mem_card_space_available(script::Struct*, script::Script* script)
{
    result_params(script)->put_int(kSpaceAvailable, 0);
    result_params(script)->put_int(kFilesLeft, 1000000);
    SaveCard* card = g_the_card->get_storage_slot(0, 0);
    if (!card)
        return false;
    // Originally from PartyMod Fixes (its call at 0x005A6E7E): the clamped disk count in place of the original's
    // card->get_free_blocks().
    result_params(script)->put_int(kSpaceAvailable, free_disk_blocks(card));
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

} // namespace

bool g_out_of_space;
script::Struct* g_kept_params;
uint32_t g_kept_type;
namespace {
uint32_t g_sections_to_apply_when_loading;
} // namespace
bool g_loaded_custom_skater;

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
    uint8_t* data = nullptr;
    bool loaded = false;
    int no_apply = 0;
    params->fetch_int_value(kNoApply, &no_apply, 0);
    SaveCard* card = g_the_card->get_storage_slot(0, 0);
    // (the second test is a card method that's the shared always-true stub, 0x005B9760: always true)
    if (card && stubs::return_true()) {
        params->fetch_text("Name", &name, 0);
        params->read_hash_value("Type", &type, 0);
        std::array<char, 104> path{};
        core::at(path, 0) = '\0';
        bool named = true;
        switch (os_layer()) {
        case 1:
        case 2:
        case 3: { // console folders: /<dir><file>/<dir><file>
            const char* folder = game_config::get_path_prefix();
            std::array<char, 20> file{};
            make_file_name(name, file, type);
            text::format_to(path, "/%s%s/%s%s", folder, file.data(), folder, file.data());
            break;
        }
        case 5: {
            std::array<char, 104> file{};
            make_folder_file_name(type, name, file.data());
            text::format_to(path, "%s%s", file.data(), file.data());
            break;
        }
        case 4:
        case 7: {
            const char* converted = card->convert_filename(get_type_file_name(type, name));
            if (!converted)
                named = false;
            else
                text::format_to(path, "%s", converted);
            break;
        }
        default:
            named = false;
            break;
        }
        FileLoad* file = named ? card->unseal(path.data(), 1, 0) : nullptr;
        if (file) {
            const int size = static_cast<int>(file->seek_to(0, 2));
            bool bad = size != type_file_size(type);
            if (!bad) {
                data = static_cast<uint8_t*>(memory::alloc_block(size));
                file->seek_to(0, 0);
                if (file->get_take_in(data, size) == size) {
                    util::ByteReader reader(data, static_cast<size_t>(size));
                    SaveFileHeader file_header;
                    read_save_file_header(reader, &file_header);
                    // The original also rejected the file unless
                    //     Checksums::generate_crc_from_buffer(data, file_header.size) == <the checksum read>
                    // (with the checksum field cleared) - the test PartyMod NOPs (its patch at 0x005A8A01).
                    // NOT ORIGINAL: a header claiming more bytes than the file has is rejected too (the original
                    // then unmasked the NetworkSettings contents past the end of its buffer).
                    bad = !reader.ok() || file_header.size > 500000 || file_header.size > size ||
                          file_header.version != type_version(type);
                    if (!bad) {
                        auto end = static_cast<size_t>(file_header.size);
                        const std::span<uint8_t> file_bytes(data, static_cast<size_t>(size));
                        uint8_t* rest = read_structure(header, file_bytes.subspan(kSaveFileHeaderSize).data(), end - kSaveFileHeaderSize);
                        auto used = static_cast<size_t>(rest - data);
                        const size_t count = used < end ? end - used : 0;
                        if (type == kObfuscatedType && count != 0) {
                            for (uint8_t& byte : file_bytes.subspan(used, count))
                                byte -= kSaveFileMask;
                        }
                        read_structure(contents, rest, count);
                        if (no_apply == 0) {
                            apply_loaded_contents(type, contents, script, params);
                        } else {
                            // (a null structure here would crash, as in the original; the previous one leaks)
                            gsl::owner<script::Struct*> kept = script::new_struct();
                            g_kept_params = kept;
                            kept->append_structure_if_any(contents);
                            g_kept_type = type;
                            if (type == kAlsoApplyType)
                                apply_loaded_contents(type, contents, script, nullptr);
                        }
                        loaded = true;
                        if (no_apply == 0)
                            script::launch_script(0x0EB9402Du, params, nullptr, false, nullptr);
                    }
                }
            }
            if (bad)
                result_params(script)->put_name_key(0u, kLoadFailed);
            file->shut();
            file->teardown(1); // delete
            if (data)
                memory::free_malloc(data);
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
bool save_failed_due_to_insufficient_space(script::Struct*, script::Script*) { return g_out_of_space; }

// 0x005A6560
bool set_sections_to_apply_when_loading(script::Struct* params, script::Script*)
{
    debug::printf(
        "ScriptSetSectionsToApplyWhenLoading was called.........................\n"); // "ScriptSetSectionsToApplyWhenLoading was called..."
    uint32_t& sections = sections_to_apply_when_loading();
    sections = 0;
    if (params->has_bare_flag(0x16B506C0u)) // ApplyCustomSkater
        sections |= 8;
    if (params->has_bare_flag(0xDC7EA3CEu)) // ApplyStorySkater
        sections |= 4;
    if (params->has_bare_flag(0x33EC233Fu)) // ApplyStory
        sections |= 2;
    if (params->has_bare_flag(0xB39D7CC4u)) // (name unknown)
        sections |= 1;
    if (params->has_bare_flag(0xC4E78E22u)) // all
        sections = 0xFFFFFFFF;
    return true;
}

// 0x005A6600
bool get_loaded_custom_skater(script::Struct*, script::Script*) { return g_loaded_custom_skater; }

uint32_t& sections_to_apply_when_loading() { return g_sections_to_apply_when_loading; }

// --- the card and the saves' summaries ----------------------------------------------------------------------

namespace {

SaveCard* the_card() { return g_the_card->get_storage_slot(0, 0); }

// The script pools' pairs whose current pool the temporary pools switch: components, structures, vectors.
memory::PoolPair& component_pools() { return script::g_component_pools; }
memory::PoolPair& struct_pools() { return script::g_struct_pools; }
memory::PoolPair& vector_pools() { return script::g_vector_pools; }

} // namespace

// 0x005A6D30
bool get_most_recent_save(script::Struct* params, script::Script* script)
{
    result_params(script)->erase_module("MostRecentSave");
    script::Array* saves = nullptr;
    params->fetch_list(0u, &saves, 0);
    if (!saves)
        return false;
    uint32_t type = 0;
    params->read_hash_value(0u, &type, 0);
    script::Struct* best = nullptr;
    for (uint32_t i = 0; i < saves->extent_; ++i) {
        auto* save = saves->struct_at(i);
        uint32_t file_type = 0;
        save->read_hash_value("file_type", &file_type, 0);
        if (type != 0 && file_type != type) // (any type when none is asked for)
            continue;
        // a bad save only counts on the PC (4) and platform 5
        if ((save->has_bare_flag("BadVersion") || save->has_bare_flag("Corrupt")) && os_layer() != 4 && os_layer() != 5)
            continue;
        if (!best || is_later(save, best))
            best = save;
    }
    if (!best)
        return false;
    result_params(script)->put_sub_struct("MostRecentSave", best);
    return true;
}

// 0x005A7210
bool format_card(script::Struct*, script::Script*)
{
    pause_music(1);
    freeze_stream(1);
    bool formatted = false;
    if (the_card())
        formatted = stubs::return_false(); // (the card's Format: the shared always-false stub, 0x005B9E50)
    pause_music(-1);
    freeze_stream(-1);
    return formatted;
}

// 0x005A7260
bool card_is_in_slot(script::Struct*, script::Script*) { return the_card() != nullptr; }

// 0x005A7280
bool card_is_formatted(script::Struct*, script::Script*)
{
    if (!the_card())
        return false;
    return stubs::return_true(); // (the card's IsFormatted: the shared always-true stub, 0x005B9760)
}

// 0x005A72A0
bool get_summary_info(script::Struct* params, script::Script* script)
{
    constexpr uint32_t kVaultData = 0xC78DABF6; // "VaultData": the kept contents' summary
    uint32_t type = 0;
    params->read_hash_value(0u, &type, 0);
    int vault_data = 0;
    params->fetch_int_value(kVaultData, &vault_data, 0);
    if (vault_data) {
        write_summary(g_kept_params, result_params(script), g_kept_type);
        return true;
    }
    if (type == kSaveOptionsAndPros) {
        // the goal manager's totals brought up to date (0x0055C090, no source yet)
        goals::get_objective_board()->level_unload();
    }
    write_summary(nullptr, result_params(script), type);
    return true;
}

// 0x005A7430
bool get_mem_card_data_for_upload(script::Struct*, script::Script* script)
{
    result_params(script)->put_name_key(0x7321A8D6u, g_kept_type); // type
    return true;
}

// 0x005A7450
bool get_max_thps4_files_allowed(script::Struct*, script::Script* script)
{
    result_params(script)->put_int(0x7CC9E431u, 75); // MaxTHPS4FilesAllowed
    return true;
}

// 0x005A7470
bool switch_to_temp_pools_if_they_exist(script::Struct*, script::Script*)
{
    ++component_pools().current;
    ++struct_pools().current;
    ++vector_pools().current;
    if (core::at(component_pools().pools, component_pools().current) && core::at(struct_pools().pools, struct_pools().current) &&
        core::at(vector_pools().pools, vector_pools().current))
        return true;
    --component_pools().current;
    --struct_pools().current;
    --vector_pools().current;
    return false;
}

// 0x005A74F0
bool switch_to_regular_pools(script::Struct*, script::Script*)
{
    if (component_pools().current || struct_pools().current || vector_pools().current) {
        --component_pools().current;
        --struct_pools().current;
        --vector_pools().current;
    }
    return true;
}

} // namespace save_cards
