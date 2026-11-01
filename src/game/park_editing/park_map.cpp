// The park manager's compressed map (park_editing::ParkLayout): the park read from a script structure, a file or the
// map itself, and a new empty park.

#include "game/park_editing/park.h"
#include "formats/park_map_format.h"
#include "formats/byte_stream.h"
#include "game/park_editing/park_editor.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "game/park_editing/rail_editor.h"
#include "game/park_editing/goal_editor.h"

namespace park_editing {

bool g_rail_editor_flag{true};

namespace {

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
bool& rail_editor_flag() { return g_rail_editor_flag; }

// The park editor's theme.
void set_theme(uint32_t theme) { ParkBuilderMode::sole()->set_daylight_script(theme); }

// The script structure written to a buffer and read back (0x0047A070 sizes it, 0x00479E20 writes it,
// 0x0047A0C0 reads it).
uint32_t structure_buffer_size(script::Struct* structure, uint32_t maximum)
{
    return script::measure_buffer_bytes(structure, maximum);
}
void write_structure(script::Struct* structure, void* buffer, uint32_t size, int a, int b)
{
    script::emit_into_buffer(structure, static_cast<uint8_t*>(buffer), size, a, reinterpret_cast<void (*)()>(b));
}
void read_structure(script::Struct* structure, void* buffer, size_t size)
{
    script::decode_from_bytes(structure, static_cast<const uint8_t*>(buffer), size);
}

// strcpy (inline in the original: no limit).
void copy_string(char* dest, const char* source)
{
    while ((*dest++ = *source++) != '\0') {
    }
}

script::Struct* spawn_param_table()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory::construct_in<script::Struct>(memory) : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

} // namespace

// 0x00598FC0
void ParkLayout::decode_from_struct(script::Struct* params, bool apply_to_editor)
{
    void* goal_editor = get_goal_editor();
    clear_goals(goal_editor);
    script::Struct* goals = nullptr;
    if (params->read_sub_struct(kParkEditorGoals, &goals, 0))
        read_goals(goal_editor, goals, 1);

    const ParkBuilderMode* editor = ParkBuilderMode::fabricate(false);
    if (editor->cur_phase_ != kEditLevel)
        rail_editor_flag() = false;
    read_rails(params);
    rail_editor_flag() = true;

    int max_players = 2;
    params->fetch_int_value(kMaxPlayers, &max_players, 0);
    sole()->builder_->set_player_cap(max_players);

    const uint16_t map_size = (flag_bits_ & kMapLoaded) ? map_.shell_size_ : 0;
    if (park_built_)
        write_compressed_map();
    script::Array* map = nullptr;
    if (params->fetch_list("Park_editor_map", &map, 1)) {
        // (NOT ORIGINAL: no more words than the array has - the original read a whole map's worth past a short one's
        // end - and the rest of the map zeroed)
        park_map_from_words(&map_, map->find_array_value(), map->extent_);
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
    char name[64];
    std::snprintf(name, sizeof(name), "CustomParks\\custom%d.prk", slot);
    if (save) {
        script::Struct* params = spawn_param_table();
        export_to_struct(params);
        const uint32_t size = structure_buffer_size(params, 100000);
        void* buffer = memory::alloc_block(size);
        write_structure(params, buffer, size, 1, 0);
        file::Hdl file = file::unseal(name, "wb");
        file::emit_out(buffer, static_cast<int>(size), 1, file);
        file::shut(file);
        memory::free_malloc(buffer);
        delete_structure(params);
        return;
    }

    file::Hdl file = file::unseal(name, "rb");
    const uint32_t size = file::get_file_size(file);
    void* buffer = memory::alloc_block(size);
    const int read = file::get_take_in(buffer, static_cast<int>(size), 1, file);
    file::shut(file);
    script::Struct* params = spawn_param_table();
    // (Not original: decoded within what was read - the original decoded on to the end marker, past a file cut short)
    read_structure(params, buffer, read > 0 ? static_cast<size_t>(read) : 0);
    memory::free_malloc(buffer);
    decode_from_struct(params, false);
    delete_structure(params);
    flag_bits_ = (flag_bits_ & ~(kMapUpToDate | kFlag8)) | kMapLoaded | kMapSaved;

    // A premade park keeps its name.
    if (script::Array* premade = script::fetch_list(kPremadeParksInfo)) {
        const int count = static_cast<int>(premade->extent_);
        for (int i = 0; i < count; ++i) {
            auto* info = reinterpret_cast<script::Struct*>(premade->fetch_part(i));
            if (!info)
                continue;
            int info_slot = 0;
            info->fetch_int_value(kSlot, &info_slot, 0);
            const char* park_name = nullptr;
            if (slot == info_slot && info->fetch_text(kName, &park_name, 0))
                set_park_name(park_name);
        }
    }
    builder_->set_player_cap(8);
}

// 0x00599EB0: an older park (gaps of 0x2C bytes, another dead piece).
void ParkLayout::read_from_old_compressed_map() { read_compressed_map(script::fetch_list(kEdSaveMap), false); }

// 0x0059B750
void ParkLayout::read_from_compressed_map()
{
    script::Array* names = script::fetch_list(kEdSaveMap);
    if (map_.rev_ != kMapVersion) {
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
    build_area_.x = map_.pos_x_;
    build_area_.z = map_.depth_cell_;
    build_area_.w = map_.w_;
    build_area_.l = map_.l_;
    shell_size_ = map_.shell_size_;
    set_theme(map_.style_set_);
    builder_->set_player_cap(map_.max_players_);
    for (int x = 0; x < kFloorSize; ++x) {
        for (int z = 0; z < kFloorSize; ++z) {
            floor_[x][z].level_y_ = int{map_.floor_heights_[x * kFloorSize + z]};
            floor_[x][z].heights_used_ = 0;
        }
    }

    // the records after the header and the floor (park_map_format.h). NOT ORIGINAL: no further than the map's end
    // (the original read on past it for counts too big)
    util::ByteReader in(&map_, sizeof(map_));
    in.seek(kParkMapRecordsStart);
    // The metapieces: the name's index, x, z; the height and rotation.
    for (int i = 0; std::cmp_less(i, map_.composite_count_); ++i) {
        ParkMapPiece piece;
        if (!read_park_map_piece(in, &piece))
            break;
        const uint32_t checksum = names->fetch_part(piece.name_index);
        if (checksum == (current ? kDeadPiece : kOldDeadPiece))
            continue;
        PieceGroup* meta = make_placed_piece(find_piece_template(checksum), 0);
        CellBox pos = CellBox::get_make(0, 0, 0, 0, 0, 0);
        pos.x = piece.x;
        pos.z = piece.z;
        pos.y = piece.y;
        meta->set_quarter_turns(piece.quarter_turns);
        place_meta_piece_in_park(pos, meta);
    }

    // The gaps (0x30 bytes each, 0x2C in older parks): both ends, their sides, the name and score.
    for (int i = 0; std::cmp_less(i, map_.num_gaps_); ++i) {
        ParkMapGap record;
        if (!read_park_map_gap(in, &record, current))
            break;
        GapDetails info = {};
        for (int end = 0; end < 2; ++end) {
            info.at_point_[end] = CellBox::get_make(0, 0, 0, 0, 0, 0);
            info.at_point_[end].x = record.ends[end].x;
            info.at_point_[end].y = record.ends[end].y;
            info.at_point_[end].z = record.ends[end].z;
            info.quarter_turns_[end] = record.ends[end].quarter_turns;
            info.reach_left_[end] = record.ends[end].reach_left;
            info.reach_right_[end] = record.ends[end].reach_right;
        }
        // (the name cut to the field: the original copied up to a terminator, past 32 bytes when there was none)
        std::snprintf(info.text_, sizeof(info.text_), "%s", record.text);
        info.points_held_ = record.points;
        info.halves_placed_ = 2;
        if (current)
            info.cancel_flags_ = record.cancel_flags;
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
            FloorCell& cell = column[z];
            cell.level_y_ = 0;
            cell.heights_used_ = 0;
            cell.touched_ = false;
            cell.delta_ = 0;
        }
    }
    write_compressed_map();
    if (map_.rev_ == kMapVersion) {
        copy_string(map_.label_, "unnamed park");
        copy_string(park_name_, "unnamed park");
    }
    flag_bits_ = (flag_bits_ & ~kFlag8) | kMapUpToDate;
}

} // namespace park_editing
