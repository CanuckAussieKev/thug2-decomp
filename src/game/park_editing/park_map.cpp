// The park manager's compressed map (park_editing::ParkLayout): the park read from a script structure, a file or the
// map itself, and a new empty park.

#include "game/park_editing/park.h"
#include "formats/park_map_format.h"
#include "formats/byte_stream.h"
#include "game/park_editing/park_editor.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <array>
#include <vector>

#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "game/park_editing/rail_editor.h"
#include "game/park_editing/goal_editor.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "game/park_editing/park_editing_state.h"
#include "engine/net/player_info.h"

namespace park_editing {

namespace {

constexpr std::size_t kMapNameSize = 64;

constexpr uint16_t kMapVersion = 0x4E24;
constexpr uint32_t kParkEditorGoals = 0xD8EB825E;  // "Park_editor_goals"
constexpr uint32_t kMaxPlayers = 0xB7E39B53;       // "MaxPlayers"
constexpr uint32_t kPremadeParksInfo = 0x9FAE26EE; // "premade_parks_info"
constexpr uint32_t kSlot = 0x53F1DF98;             // "slot"
constexpr uint32_t kName = 0xA1DC81F9;             // "name"
constexpr uint32_t kEdSaveMap = 0xBC29F55A;        // "Ed_Save_Map": the metapieces' names by saved index
constexpr uint32_t kDeadPiece = 0xA0560CBD;        // "Sk6Ed_dead": a saved piece that's gone
constexpr uint32_t kOldDeadPiece = 0xE2730BC0;     // the older parks' dead piece
constexpr uint32_t kEditLevel = 0x7ECA21E5;        // "edit": the park editor's level

// The goal editor (0x004E8470 looks it up): its goals cleared (0x004E8280), read from a structure (0x004EA470).
void* get_goal_editor() { return ObjectiveAuthoring::get_pull(); }
void clear_goals(void* goal_editor) { static_cast<ObjectiveAuthoring*>(goal_editor)->clear_park_goals(); }
void read_goals(void* goal_editor, script::Struct* goals, int flag)
{
    static_cast<ObjectiveAuthoring*>(goal_editor)->read_goals(goals, flag);
}
// The rail editor (0x004F4970 looks it up): its rails read from a structure (0x004F7ED0).
void read_rails(script::Struct* params) { RailAuthoring::sole()->decode_from_struct(params); }
// A flag the rail editor reads (cleared while rails are read outside the park editor's level).

// The park editor's theme.
void set_theme(uint32_t theme) { ParkBuilderMode::sole()->set_daylight_script(theme); }

// The script structure written to a buffer and read back (0x0047A070 sizes it, 0x00479E20 writes it,
// 0x0047A0C0 reads it).
uint32_t structure_buffer_size(script::Struct* structure, uint32_t maximum)
{
    return script::measure_buffer_bytes(structure, maximum);
}
void write_structure(script::Struct* structure, void* buffer, uint32_t size, int a)
{
    script::emit_into_buffer(structure, static_cast<uint8_t*>(buffer), size, a, nullptr); // (the original's callback: 0)
}
void read_structure(script::Struct* structure, void* buffer, size_t size)
{
    script::decode_from_bytes(structure, static_cast<const uint8_t*>(buffer), size);
}

// strcpy (inline in the original: no limit).
void copy_string(std::span<char> dest, const char* source)
{
    size_t at = 0;
    for (const char c : std::string_view(source)) {
        core::at(dest, at++) = c;
    }
    core::at(dest, at) = '\0';
}

} // namespace

// 0x00598FC0
void ParkLayout::decode_from_struct(script::Struct* params, bool apply_to_editor)
{
    void* goal_editor = get_goal_editor();
    clear_goals(goal_editor);
    script::Struct* goals = nullptr;
    if (params->read_sub_struct(kParkEditorGoals, &goals, 0)) {
        read_goals(goal_editor, goals, 1);
    }

    const ParkBuilderMode* editor = ParkBuilderMode::fabricate(false);
    if (editor->editor_state() != kEditLevel) {
        park_editing_state().set_rail_editor_flag(false);
    }
    read_rails(params);
    park_editing_state().set_rail_editor_flag(true);

    int max_players = 2;
    params->fetch_int_value(kMaxPlayers, &max_players, 0);
    sole()->builder_->set_player_cap(max_players);

    const uint16_t map_size = (flag_bits_ & kMapLoaded) ? map_.shell_size() : 0;
    if (park_built_) {
        write_compressed_map();
    }
    script::Array* map = nullptr;
    if (params->fetch_list("Park_editor_map", &map, 1)) {
        // (NOT ORIGINAL: no more words than the array has - the original read a whole map's worth past a short one's
        // end - and the rest of the map zeroed)
        const std::vector<uint32_t> words = script::words_of(map);
        park_map_from_words(map_.bytes(), words);
        if (apply_to_editor) {
            ParkBuilderMode::fabricate(false)->apply_map(&map_, map_size);
            ParkBuilderMode::get_release();
        }
    }
    ParkBuilderMode::get_release();
}

// 0x00599140
void ParkLayout::transfer_park_file(bool save, int slot)
{
    std::array<char, kMapNameSize> name{};
    text::format_to(name, "CustomParks\\custom%d.prk", slot);
    if (save) {
        script::StructPtr params = script::make_struct();
        export_to_struct(params.get());
        const uint32_t size = structure_buffer_size(params.get(), 100000);
        void* buffer = memory::alloc_block(size);
        write_structure(params.get(), buffer, size, 1);
        file::Hdl file = file::unseal(name.data(), "wb");
        file::emit_out(buffer, static_cast<int>(size), 1, file);
        file::shut(file);
        memory::free_malloc(buffer);
        params.reset();
        return;
    }

    file::Hdl file = file::unseal(name.data(), "rb");
    const uint32_t size = file::get_file_size(file);
    void* buffer = memory::alloc_block(size);
    const int read = file::get_take_in(buffer, static_cast<int>(size), 1, file);
    file::shut(file);
    script::StructPtr params = script::make_struct();
    // (Not original: decoded within what was read - the original decoded on to the end marker, past a file cut short)
    read_structure(params.get(), buffer, read > 0 ? static_cast<size_t>(read) : 0);
    memory::free_malloc(buffer);
    decode_from_struct(params.get(), false);
    params.reset();
    flag_bits_ = (flag_bits_ & ~(kMapUpToDate | kFlag8)) | kMapLoaded | kMapSaved;

    // A premade park keeps its name.
    if (script::Array* premade = script::fetch_list(kPremadeParksInfo)) {
        const int count = static_cast<int>(premade->extent());
        for (int i = 0; i < count; ++i) {
            auto* info = premade->struct_at(i);
            if (!info) {
                continue;
            }
            int info_slot = 0;
            info->fetch_int_value(kSlot, &info_slot, 0);
            const char* park_name = nullptr;
            if (slot == info_slot && info->fetch_text(kName, &park_name, 0)) {
                set_park_name(park_name);
            }
        }
    }
    builder_->set_player_cap(online::kPlayerSlotCount);
}

// 0x00599EB0: an older park (gaps of 0x2C bytes, another dead piece).
void ParkLayout::read_from_old_compressed_map() { read_compressed_map(script::fetch_list(kEdSaveMap), false); }

// 0x0059B750
void ParkLayout::read_from_compressed_map()
{
    script::Array* names = script::fetch_list(kEdSaveMap);
    if (map_.version() != kMapVersion) {
        read_from_old_compressed_map();
        builder_->set_player_cap(1);
        return;
    }
    read_compressed_map(names, true);
}

// The two readers' common body (the original has it twice, once for each version). The gap read in starts out
// zeroed here; the original leaves the parts the map doesn't give (+0x4C, and +0x50 in older parks) as they were
// on the stack.
void ParkLayout::read_compressed_map(script::Array* names, bool current)
{
    build_area_.x = map_.area_x();
    build_area_.z = map_.area_z();
    build_area_.w = map_.area_width();
    build_area_.l = map_.area_length();
    shell_size_ = map_.shell_size();
    set_theme(map_.theme());
    builder_->set_player_cap(map_.max_players());
    for (int x = 0; x < kFloorSize; ++x) {
        for (int z = 0; z < kFloorSize; ++z) {
            core::at(core::at(floor_, x), z).level_y_ =
                int{map_.floor_height(static_cast<size_t>((x * kFloorSize) + z))};
            core::at(core::at(floor_, x), z).heights_used_ = 0;
        }
    }

    // the records after the header and the floor (park_map_format.h). NOT ORIGINAL: no further than the map's end
    // (the original read on past it for counts too big)
    util::ByteReader in(map_.bytes());
    in.seek(kParkMapRecordsStart);
    // The metapieces: the name's index, x, z; the height and rotation.
    for (int i = 0; std::cmp_less(i, map_.piece_count()); ++i) {
        ParkMapPiece piece;
        if (!read_park_map_piece(in, &piece)) {
            break;
        }
        const uint32_t checksum = names->fetch_part(piece.name_index);
        if (checksum == (current ? kDeadPiece : kOldDeadPiece)) {
            continue;
        }
        PieceGroup* meta = make_placed_piece(find_piece_template(checksum), 0);
        CellBox pos = CellBox::get_make(0, 0, 0, 0, 0, 0);
        pos.x = piece.x;
        pos.z = piece.z;
        pos.y = piece.y;
        meta->set_quarter_turns(piece.quarter_turns);
        place_meta_piece_in_park(pos, meta);
    }

    // The gaps (0x30 bytes each, 0x2C in older parks): both ends, their sides, the name and score.
    for (int i = 0; std::cmp_less(i, map_.gap_count()); ++i) {
        ParkMapGap record;
        if (!read_park_map_gap(in, &record, current)) {
            break;
        }
        GapDetails info = {};
        for (int end = 0; end < 2; ++end) {
            core::at(info.at_point(), end) = CellBox::get_make(0, 0, 0, 0, 0, 0);
            core::at(info.at_point(), end).x = core::at(record.ends, end).x;
            core::at(info.at_point(), end).y = core::at(record.ends, end).y;
            core::at(info.at_point(), end).z = core::at(record.ends, end).z;
            core::at(info.quarter_turns(), end) = core::at(record.ends, end).quarter_turns;
            core::at(info.reach_left(), end) = core::at(record.ends, end).reach_left;
            core::at(info.reach_right(), end) = core::at(record.ends, end).reach_right;
        }
        // (the name cut to the field: the original copied up to a terminator, past 32 bytes when there was none)
        text::format_to(info.text(), "%s", record.text.data());
        info.set_points_held(record.points);
        info.set_halves_placed(2);
        if (current) {
            info.set_cancel_flags(record.cancel_flags);
        }
        register_gap_entry(&info);
    }
    flag_bits_ = (flag_bits_ & ~kMapSaved) | kMapUpToDate;
}

// 0x0059BE20
void ParkLayout::reset_park()
{
    destroy_concrete_metas(0);
    for (int z = 0; z < kFloorSize; ++z) {
        for (auto& column : floor_) {
            FloorCell& cell = core::at(column, z);
            cell.level_y_ = 0;
            cell.heights_used_ = 0;
            cell.touched_ = false;
            cell.delta_ = 0;
        }
    }
    write_compressed_map();
    if (map_.version() == kMapVersion) {
        copy_string(map_.name(), "unnamed park");
        copy_string(park_name_, "unnamed park");
    }
    flag_bits_ = (flag_bits_ & ~kFlag8) | kMapUpToDate;
}

} // namespace park_editing
