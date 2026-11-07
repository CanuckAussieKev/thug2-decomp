// The park editor's park set up from the scripts (palette, bounds, road mask, lights) and kept in compressed
// form (the map that's saved and sent), the gaps attached to pieces, and the memory bar.

#include "game/park_editing/park.h"
#include "base/core/str.h"
#include "formats/park_map_format.h"
#include "formats/byte_stream.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <memory>
#include <span>

#include "base/core/checksums.h"
#include "base/core/dbg.h"
#include "engine/memory/mem.h"
#include "game/park/park_memory.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/park_editing/rail_editor.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/park_editor.h"
#include "base/core/ints.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "game/park_editing/park_editing_state.h"
#include "base/maths/vec4.h"
#include <gsl/pointers>
#include <gsl/assert>

namespace park_editing {

namespace {

constexpr std::uint32_t kAreaSelection = 0xFC26B6F4U;          // area_selection
constexpr std::uint32_t kBlueFlag = 0x40BDFA7EU;               // blue_flag
constexpr std::uint32_t kCANCELGROUND = 0x9A27E74DU;           // CANCEL_GROUND
constexpr std::uint32_t kGapPlacement = 0x2813AB8AU;           // gap_placement
constexpr std::uint32_t kGreenFlag = 0xC893ACF8U;              // green_flag
constexpr std::uint32_t kKingOfHill = 0xA091CE25U;             // king_of_hill
constexpr std::uint32_t kLoadSk5Ed = 0xE8B4B836U;              // Load_Sk5Ed
constexpr std::uint32_t kMaxPlayers = 0xB7E39B53U;             // MaxPlayers
constexpr std::uint32_t kParkEditorGoals = 0xD8EB825EU;        // Park_editor_goals
constexpr std::uint32_t kParkEditorMap = 0x337C5289U;          // Park_editor_map
constexpr std::uint32_t kParkFull = 0x97A8E59CU;               // park_full
constexpr std::uint32_t kParkPercent = 0x6D6C056DU;            // park_percent
constexpr std::uint32_t kParkedUpdatePercentBar = 0xE2508960U; // parked_update_percent_bar
constexpr std::uint32_t kRailPlacement = 0xFFD81C08U;          // rail_placement
constexpr std::uint32_t kRedFlag = 0x74B0F4E9U;                // red_flag
constexpr std::uint32_t kRestart1 = 0x48FA9C7BU;               // restart_1
constexpr std::uint32_t kRestartHorse = 0xE69AEFAFU;           // restart_horse
constexpr std::uint32_t kRestartMulti = 0x419183B2U;           // restart_multi
constexpr std::uint32_t kRiser = 0x0CD75A3FU;                  // Riser
constexpr std::uint32_t kSpecialType = 0xF176A9FDU;            // special_type
constexpr std::uint32_t kYellowFlag = 0x3AE047FCU;             // yellow_flag
constexpr std::size_t kMaxPieceNames = 1000;   // the distinct pieces a metapiece's list can name
constexpr int kGreenFlagSlot = 5;              // restart_pieces_ slots of the coloured flags (red is 4)
constexpr int kBlueFlagSlot = 6;
constexpr int kYellowFlagSlot = 7;
constexpr int kGapCapacity = 32;      // the gaps a saved park lists
constexpr std::size_t kUnnamedParkBytes = 13;  // "Unnamed park" with its terminator
constexpr double kHalf = 0.5;                  // half a cell, half a gap's length
constexpr float kParkGapGridOffset{28.0F};
} // namespace

park_editing::ParkBuilderMode* ParkBuilderMode::sole() { return park_editing_state().park_editor().get(); }

namespace {

memory::Arena* park_heap() { return park_editing::park_heap(); }

// The cell size (120, 48, 120) and the grid's offset in cells used by the gap pieces (28).
float cell_size_x() { return kParkCellSizeX; }
float cell_size_y() { return kParkCellSizeY; }
float cell_size_z() { return kParkCellSizeZ; }
float gap_grid_offset() { return kParkGapGridOffset; }

// The park editor object (0x007CE5D8, 0x7C bytes): the value at +0x78 goes into the compressed map.
uint32_t editor_value78()
{
    return park_editing_state().park_editor()->lighting_script();
}
uint8_t generator_max_players(ParkAssembler* generator) { return static_cast<uint8_t>(generator->get_player_limit()); }

TileTemplate* piece_source(ParkAssembler* generator, uint32_t name) { return generator->get_piece_source(name); }
void get_footprint_size(TileTemplate* piece, CellBox* dims) { piece->get_footprint_size(dims); }
// A new palette metapiece on the park heap.
PieceGroup* new_abstract_meta()
{
    memory::enter_heap_scope(park_heap());
    PieceGroup* meta = new PieceBlueprint;
    memory::leave_heap_scope();
    return meta;
}
// Element `i` of a script array of integers, as a byte.
uint8_t byte_at(script::Array* array, uint32_t index) { return static_cast<uint8_t>(array->fetch_part(index)); }

} // namespace

// A single piece's special_type: a tool, a restart's piece or a flag.
void ParkLayout::read_special_type(script::Struct* desc, PieceGroup* meta, uint32_t single)
{
    uint32_t special = 0;                                   // (left unset if there's none, as in the original)
    if (desc->read_hash_value(kSpecialType, &special, 0)) { // special_type
        switch (special) {
        case kGapPlacement: // gap_placement
            meta->flag_bits() |= PieceGroup::kGap;
            break;
        case kAreaSelection: // area_selection
            meta->flag_bits() |= PieceGroup::kMarqueeTool;
            break;
        case kRailPlacement: // rail_placement
            meta->flag_bits() |= PieceGroup::kRailPlacement;
            break;
        case kRestart1: // restart_1
            core::at(restart_pieces_, 0) = single;
            break;
        case kRestartMulti: // restart_multi
            core::at(restart_pieces_, 1) = single;
            break;
        case kRestartHorse: // restart_horse
            core::at(restart_pieces_, 2) = single;
            break;
        case kKingOfHill: // king_of_hill
            core::at(restart_pieces_, 3) = single;
            break;
        case kRedFlag: // red_flag
            core::at(restart_pieces_, 4) = single;
            meta->flag_bits() |= PieceGroup::kRestartFlag;
            break;
        case kGreenFlag: // green_flag
            core::at(restart_pieces_, kGreenFlagSlot) = single;
            meta->flag_bits() |= PieceGroup::kRestartFlag;
            break;
        case kBlueFlag: // blue_flag
            core::at(restart_pieces_, kBlueFlagSlot) = single;
            meta->flag_bits() |= PieceGroup::kRestartFlag;
            break;
        case kYellowFlag: // yellow_flag
            core::at(restart_pieces_, kYellowFlagSlot) = single;
            meta->flag_bits() |= PieceGroup::kRestartFlag;
            break;
        default:
            break;
        }
    }
}

// A metapiece of several pieces (at their positions and turns), their names added to `piece_names` from `count`
// on; locked to the floor with a riser among them.
void ParkLayout::add_multiple_pieces(PieceGroup* meta, script::Array* pieces, std::span<uint32_t> piece_names,
                                     uint32_t& count)
{
    bool unslidable = false;
    meta->commission(static_cast<int>(pieces->extent()));
    for (uint32_t i = 0; i < pieces->extent(); ++i) {
        auto* entry = pieces->struct_at(i);
        const uint32_t name = entry->step_to_entry(nullptr)->value.low_word(); // the piece's name comes first
        script::Array* at = nullptr;
        entry->fetch_list(0U, &at, 1); // its unnamed [x y z (rot)]
        TileTemplate* source = piece_source(builder_, name);
        core::at(piece_names, count++) = name;
        CellBox pos{};
        pos.assign_value({.cell_x_ = 0, .cell_y_ = 0, .cell_z_ = 0}, {.width = 0, .height = 0, .length = 0});
        get_footprint_size(source, &pos);
        const uint8_t z = byte_at(at, 2);
        const uint8_t y = byte_at(at, 1);
        pos.x = byte_at(at, 0);
        pos.y = static_cast<int8_t>(y);
        pos.z = z;
        const int rot = at->extent() > 3 ? static_cast<int>(at->fetch_part(3)) : 0;
        if (entry->has_bare_flag(kRiser)) {
            unslidable = true;
        }
        meta->insert_tile(source, &pos, rot, entry->has_bare_flag(kRiser));
    }
    if (unslidable) {
        meta->flag_bits() |= PieceGroup::kFloorLocked;
    }
}

void ParkLayout::set_up_abstract_metas()
{
    script::Array* all = script::fetch_list("Ed_standard_metapieces"); // (0x00479090, asserting)
    for (uint32_t i = 0; i < all->extent(); ++i) {
        auto* desc = all->struct_at(i);
        uint32_t single = 0;
        if (desc->read_hash_value("single", &single, 0)) {
            piece_source(builder_, single); // (loads the piece)
        }
    }

    std::array<uint32_t, kMaxPieceNames> piece_names{}; // every piece a metapiece uses
    uint32_t num_piece_names = 0;
    for (uint32_t index = 0; index < all->extent(); ++index) {
        auto* desc = all->struct_at(index);
        PieceGroup* meta = new_abstract_meta();
        uint32_t single = 0;
        script::Array* pieces = nullptr;
        if (desc->read_hash_value("single", &single, 0)) {
            meta->commission(1);
            TileTemplate* source = piece_source(builder_, single);
            CellBox pos{};
            pos.assign_value({.cell_x_ = 0, .cell_y_ = 0, .cell_z_ = 0}, {.width = 0, .height = 0, .length = 0});
            get_footprint_size(source, &pos);
            script::Array* at = nullptr;
            if (desc->fetch_list("pos", &at, 0)) {
                const uint8_t z = byte_at(at, 2);
                const uint8_t y = byte_at(at, 1);
                pos.x = byte_at(at, 0);
                pos.y = static_cast<int8_t>(y);
                pos.z = z;
            }
            meta->insert_tile(source, &pos, 0, false);
            const uint32_t name = source->sector(); // the source's name
            core::at(piece_names, num_piece_names++) = name;
            meta->flag_bits() |= PieceGroup::kSector;
            meta->name_key() = name;
            read_special_type(desc, meta, single);
        } else if (desc->fetch_list("multiple", &pieces, 0)) {
            add_multiple_pieces(meta, pieces, piece_names, num_piece_names);
        } else if (desc->fetch_list("ack", &pieces, 0)) { // made of other palette metapieces
            meta->commission(static_cast<int>(pieces->extent()));
            for (uint32_t i = 0; i < pieces->extent(); ++i) {
                auto* entry = pieces->struct_at(i);
                const uint32_t name = entry->step_to_entry(nullptr)->value.low_word();
                script::Array* at = nullptr;
                entry->fetch_list(0U, &at, 1);
                PieceGroup* part = nullptr;
                for (const MapListNode* node = abstract_metas_; node; node = node->following()) {
                    if (node->get_piece_group()->get_cast()->name_key() == name) {
                        part = node->get_piece_group()->get_cast();
                        break;
                    }
                }
                Expects(part != nullptr);
                CellBox pos = part->cell_footprint(); // (an unknown name crashes, as in the original)
                const uint8_t z = byte_at(at, 2);
                const uint8_t y = byte_at(at, 1);
                pos.x = byte_at(at, 0);
                pos.y = static_cast<int8_t>(y);
                pos.z = z;
                const int rot = at->extent() > 3 ? static_cast<int>(at->fetch_part(3)) : 0;
                core::down_cast<PieceBlueprint>(meta)->add_meta_piece(
                    part, &pos, {.rot = rot, .sets_floor_ = static_cast<uint8_t>(entry->has_bare_flag(kRiser))});
            }
        }
        if (desc->read_hash_value("name", &meta->name_key(), 0)) {
            meta->flag_bits() &= ~PieceGroup::kSector;
        }
        if (desc->has_bare_flag("is_riser")) {
            meta->flag_bits() |= PieceGroup::kBuiltShell;
        }
        if (desc->has_bare_flag("no_rails")) {
            meta->flag_bits() |= PieceGroup::kNoRails;
        }
        MapListNode* node = MapListNode::fabricate();
        node->set_piece_group(meta); // (out of memory crashes, as in the original)
        node->set_following(abstract_metas_);
        abstract_metas_ = node;
        ++num_abstract_metas_;
    }

    // a single-piece palette metapiece for every piece used that hasn't one
    for (uint32_t i = 0; i < num_piece_names; ++i) {
        const uint32_t name = core::at(piece_names, i);
        bool found = false;
        for (const MapListNode* node = abstract_metas_; node; node = node->following()) {
            if (node->get_piece_group()->get_cast()->name_key() == name) {
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }
        PieceGroup* meta = new_abstract_meta();
        meta->commission(1);
        TileTemplate* source = piece_source(builder_, name);
        CellBox pos{};
        pos.assign_value({.cell_x_ = 0, .cell_y_ = 0, .cell_z_ = 0}, {.width = 0, .height = 0, .length = 0});
        get_footprint_size(source, &pos);
        meta->insert_tile(source, &pos, 0, false);
        meta->name_key() = name;
        meta->flag_bits() |= PieceGroup::kSector;
        MapListNode* node = MapListNode::fabricate();
        node->set_piece_group(meta);
        node->set_following(abstract_metas_);
        abstract_metas_ = node;
        ++num_abstract_metas_;
    }
}

void ParkLayout::write_compressed_map()
{
    if ((flag_bits_ & kMapNewerThanPark) != 0U) {
        return;
    }
    ParkMap& map = map_;
    map = ParkMap{};
    script::Array* save_names = script::fetch_list("Ed_Save_Map"); // the palette names, by save index
    const uint32_t name_total = save_names->extent();
    map.set_version(kParkMapVersion);
    map.set_shell_size(static_cast<uint16_t>(shell_size_));
    map.set_reserved_0a(0);
    map.set_area({.x = build_area_.x, .z = build_area_.z, .width = build_area_.w, .length = build_area_.l});
    map.set_theme(editor_value78());
    map.set_max_players(generator_max_players(builder_));
    text::copy_unbounded(map.name().data(), park_name_.data());
    for (int cell_x = 0; cell_x < kFloorSize; ++cell_x) {
        for (int cell_z = 0; cell_z < kFloorSize; ++cell_z) {
            map.set_floor_height((static_cast<size_t>(cell_x) * kFloorSize) + static_cast<size_t>(cell_z),
                                 static_cast<int8_t>(core::at(core::at(floor_, cell_x), cell_z).level_y_));
        }
    }
    // the records after the header and the floor (park_map_format.h). NOT ORIGINAL: no further than the map's end
    // (the original wrote on past it)
    util::ByteWriter out(map.bytes());
    out.seek(kParkMapRecordsStart);

    int num_metas = 0;
    for (const MapListNode* node = concrete_metas_; node; node = node->following()) {
        const PieceGroup* meta = node->get_piece_group()->get_cast();
        const uint32_t flags = meta->flag_bits();
        if (((flags & PieceGroup::kBuiltShell) != 0U) || ((flags & PieceGroup::kFence) != 0U) ||
            ((flags & PieceGroup::kInPark) == 0U)) {
            continue;
        }
        const CellBox at = meta->cell_footprint();
        uint32_t index = 0;
        while (index < name_total && save_names->fetch_part(index) != meta->name_key()) {
            ++index;
        }
        if (index >= name_total) {
            continue;
        }
        ParkMapPiece piece;
        piece.name_index = index;
        piece.x = at.x;
        piece.z = at.z;
        piece.y = at.y;
        piece.quarter_turns = static_cast<uint8_t>(meta->quarter_turns());
        if (!write_park_map_piece(out, piece)) {
            break;
        }
        ++num_metas;
    }
    map.set_piece_count(static_cast<uint16_t>(num_metas));

    std::array<GapDetails*, kGapCapacity> gaps{};
    int num_gaps = 0;
    for (int i = 0; i < kGapCapacity; ++i) {
        int half = 0;
        GapDetails* gap = gap_registry_->get_gap_info(i, &half);
        if (gap && half == 0 && gap->halves_placed() == 2) {
            core::at(gaps, num_gaps++) = gap;
        }
    }
    int gaps_written = 0;
    for (int i = 0; i < num_gaps; ++i) {
        const GapDetails* gap = core::at(gaps, i);
        ParkMapGap record;
        for (int end = 0; end < 2; ++end) {
            core::at(record.ends, end).x = core::at(gap->at_point(), end).x;
            core::at(record.ends, end).y = core::at(gap->at_point(), end).y;
            core::at(record.ends, end).z = core::at(gap->at_point(), end).z;
            core::at(record.ends, end).quarter_turns = static_cast<uint8_t>(core::at(gap->quarter_turns(), end));
            core::at(record.ends, end).reach_left = static_cast<uint8_t>(core::at(gap->reach_left(), end));
            core::at(record.ends, end).reach_right = static_cast<uint8_t>(core::at(gap->reach_right(), end));
        }
        // (the name cut to its 32 bytes: the original's copy ran on into the points, written after it)
        std::memcpy(record.text.data(), gap->text().data(), sizeof(gap->text()));
        core::at(record.text, sizeof(gap->text())) = '\0';
        record.points = static_cast<uint16_t>(gap->points_held());
        record.cancel_flags = gap->cancel_flags();
        if (!write_park_map_gap(out, record)) {
            break;
        }
        ++gaps_written;
    }
    map.set_gap_count(static_cast<uint16_t>(gaps_written));
    map.set_extent(static_cast<uint16_t>(out.position()));
    if ((flag_bits_ & (kMapValid | kMapNewerThanPark)) == 0U) {
        flag_bits_ |= kMapNotSavedLocal;
    }
    flag_bits_ = (flag_bits_ & ~kMapNewerThanPark) | kMapValid | kMapInSync;
    map.set_checksum(park_map_checksum(map.bytes()));
}

void ParkLayout::set_default_bounds()
{
    const int inner = script::fetch_int_value("ed_default_inner_dim"); // (0x004790C0, asserting)
    shell_bounds_.assign_value({.cell_x_ = 1, .cell_y_ = -4, .cell_z_ = 1}, {.width = kShellCells, .height = kShellHeightCells, .length = kShellCells});
    auto corner = static_cast<uint8_t>(((kShellCells - inner) / 2) + 1);
    build_area_.assign_value({.cell_x_ = corner, .cell_y_ = -4, .cell_z_ = corner}, {.width = static_cast<uint8_t>(inner), .height = kShellHeightCells, .length = static_cast<uint8_t>(inner)});
}

void ParkLayout::set_up_road_mask()
{
    script::Array* roads = script::fetch_list("Ed_Roadmask");
    num_blocked_areas_ = roads->extent();
    // (the first four rectangles are set elsewhere; this reads the array from element 4, past its end, as the
    // original does)
    for (uint32_t i = 4; i < num_blocked_areas_ + 4; ++i) {
        auto* rect = roads->array_at(i);
        CellBox& mask = core::at(blocked_areas_, i); // (the original went on past the eighth, over the count itself; Ed_Roadmask has four at most)
        mask.x = byte_at(rect, 0);
        mask.z = byte_at(rect, 1);
        mask.w = byte_at(rect, 2);
        mask.l = byte_at(rect, 3);
        mask.y = 0;
        mask.stretch_vertically();
    }
    num_blocked_areas_ += 4;
}

void ParkLayout::set_up_lights()
{
    script::Array* themes = script::fetch_list("Ed_Light_Info"); // (the original doesn't assert)
    auto* theme = themes->array_at(0);
    auto* info = theme->struct_at(0);
    float ambient_const = 0.0F;
    float falloff_const = 0.0F;
    float cursor_ambience = 0.0F;
    script::VectorValue ambient{};
    script::VectorValue falloff{};
    script::Array* positions = nullptr;
    info->fetch_float("ambient_const", &ambient_const, 1);
    info->fetch_vec("ambient_rgb", &ambient, 1);
    info->fetch_float("falloff_const", &falloff_const, 1);
    info->fetch_vec("falloff_rgb", &falloff, 1);
    info->fetch_float("cursor_ambience", &cursor_ambience, 1);
    info->fetch_list("pos", &positions, 1);
    // The generator's lights and their positions
    builder_->configure_lighting(static_cast<int>(positions->extent()), {.ambient_r_ = ambient.x * ambient_const, .ambient_g_ = ambient.y * ambient_const, .ambient_b_ = ambient.z * ambient_const, .falloff_r_ = falloff.x * falloff_const, .falloff_g_ = falloff.y * falloff_const, .falloff_b_ = falloff.z * falloff_const, .cursor_ambience_ = cursor_ambience});
    for (int i = 0; i < ints::as_signed(positions->extent()); ++i) {
        const script::Vector* v = positions->vector_at(static_cast<uint32_t>(i));
        maths::Vec4 pos;
        pos.x = cell_size_x() * v->x();
        pos.y = cell_size_y() * v->y();
        pos.z = cell_size_z() * v->z();
        pos.w = 1.0F;
        builder_->move_light_source(&pos, i);
    }
}

void ParkLayout::update_percent_bar()
{
    // 0x0059E730: the park's memory use (fills the stats at 0x007CE618, returns them)
    MemoryStats stats_copy{};
    const MemoryStats* use = get_memory_stats(&stats_copy);
    const float full = use->max_usage_; // (its +0x2C: how full, 0-1)
    float percent = full;
    if (full < 0.0F) {
        percent = 0.0F;
    } else if (full > 1.0F) {
        percent = 1.0F;
    }
    gsl::owner<script::Struct*> memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    Expects(params != nullptr);
    params->push_float(checksums::Name{kParkPercent}, percent);  // park_percent (out of memory crashes, as in the original)
    if (get_attributes().max_usage_ >= 1.0F) {                   // (0x007CE644: the park memory stats' largest usage)
        params->put_name_key(checksums::Name{0U}, kParkFull);    // park_full
    }
    script::launch_script(kParkedUpdatePercentBar, params, nullptr, false, nullptr); // parked_update_percent_bar
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

void ParkLayout::write_compressed_map_if_built()
{
    if (park_built_) {
        write_compressed_map();
    }
}

void ParkLayout::export_to_struct(script::Struct* out)
{
    if (park_built_) {
        write_compressed_map();
    }
    flag_bits_ &= ~kMapNotSavedLocal;
    gsl::owner<script::Array*> memory = script::alloc_script_array();
    gsl::owner<script::Array*> map = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    Expects(map != nullptr);
    map->configure_size_kind(kParkMapWords, script::QbValueKind::kInteger); // (out of memory crashes, as in the original)
    std::array<uint32_t, kParkMapWords> words{};
    park_map_to_words(map_.bytes(), words);
    for (uint32_t i = 0; i < kParkMapWords; ++i) {
        map->set_element(i, core::at(words, i));
    }
    out->append_array_ref(kParkEditorMap, map); // Park_editor_map
    gsl::owner<script::Struct*> struct_memory = script::spawn_param_bag();
    gsl::owner<script::Struct*> goals = struct_memory ? memory::construct_in<script::Struct>(struct_memory) : nullptr;
    // the goal editor (module GoalEditor, 0x004E8470) writes the park's goals (level Load_Sk5Ed)
    ObjectiveAuthoring::get_pull()->write_level_goals(kLoadSk5Ed, goals);
    out->append_struct_ref(kParkEditorGoals, goals); // Park_editor_goals
    // the rail editor (module RailEditor, 0x004F4970) writes the park's rails
    RailAuthoring::sole()->write_to_structure(out);
    out->put_int(checksums::Name{kMaxPlayers}, generator_max_players(builder_)); // MaxPlayers
}

void ParkLayout::set_shell_size(int theme)
{
    shell_size_ = theme;
    write_compressed_map();
}

void ParkLayout::clear_compressed_map()
{
    set_default_bounds();
    ParkMap& map = map_;
    map.set_version(kParkMapVersion);
    map.set_shell_size(static_cast<uint16_t>(shell_size_));
    map.set_reserved_0a(0);
    map.set_area({.x = build_area_.x, .z = build_area_.z, .width = build_area_.w, .length = build_area_.l});
    map.set_theme(static_cast<void*>(park_editing_state().park_editor().get()) ? editor_value78() : 0);
    map.set_max_players(2);
    std::memcpy(map.name().data(), "Unnamed park", kUnnamedParkBytes); // "Unnamed park"
    for (size_t cell = 0; cell < kParkMapFloorCells; ++cell) {
        map.set_floor_height(cell, 0);
    }
    map.set_piece_count(0);
    const uint32_t used = kParkMapRecordsStart;
    // (the checksum is taken before the size and the gap count are set, as in the original)
    const std::span<const uint8_t> after_checksum = std::span<const uint8_t>(map.bytes()).subspan(4, used - 4);
    const uint32_t checksum =
        checksums::generate_crc_from_buffer(after_checksum.data(), static_cast<int>(after_checksum.size()));
    map.set_extent(static_cast<uint16_t>(used));
    map.set_checksum(checksum);
    map.set_gap_count(0);
    if ((flag_bits_ & (kMapValid | kMapNewerThanPark)) == 0U) {
        flag_bits_ |= kMapNotSavedLocal;
    }
    flag_bits_ = (flag_bits_ & ~kMapNewerThanPark) | kMapValid | kMapInSync;
}

bool ParkLayout::register_gap_entry(GapDetails* gap)
{
    const int tab = gap_registry_->find_empty_gap_slot();
    if (tab == -1) {
        return false;
    }
    // The trigger's position (x87 arithmetic: x is stored as a float on the way, z isn't). For a rotation past 3
    // the previous half's position is used again (the original's are left unset before the first half).
    maths::Vec4 at = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    for (int half = 0; half < gap->halves_placed(); ++half) {
        CellBox& pos = core::at(gap->at_point(), half);
        pos.w = 1;
        pos.h = 1;
        pos.l = 1;
        const MapListHolder metas(get_intersecting_metas(pos, 0, nullptr, 1U));
        if (!metas.list()) {
            return false;
        }
        PieceGroup* meta = metas.list()->get_piece_group()->get_cast();
        meta->flag_bits() |= PieceGroup::kHighlightable;
        pos = meta->cell_footprint();
        const CellBox footprint = meta->cell_footprint();
        const float length = gap->fetch_magnitude(half);
        const double w = cell_size_x();
        const double cell_length = cell_size_z();
        const double y = static_cast<double>(footprint.y) * cell_size_y();
        int along_x = 0;
        float x = at.x;
        double z = at.z;
        switch (core::at(gap->quarter_turns(), half)) {
        case 0:
            along_x = 1;
            x = static_cast<float>(((static_cast<double>(footprint.w + footprint.x) * w) - (w * kHalf)) - 1.0);
            z = ((static_cast<double>(footprint.z - core::at(gap->reach_right(), half)) * cell_length) +
                 (length * kHalf)) -
                cell_length;
            at.y = static_cast<float>(y);
            break;
        case 1:
            x = static_cast<float>(
                ((static_cast<double>(footprint.x - core::at(gap->reach_right(), half)) * w) + (length * kHalf)) - w);
            z = ((static_cast<double>(footprint.z) * cell_length) - (cell_length * kHalf)) + 1.0;
            at.y = static_cast<float>(y);
            break;
        case 2:
            along_x = 1;
            x = static_cast<float>(((static_cast<double>(footprint.x) * w) - (w * kHalf)) + 1.0);
            z = ((static_cast<double>(footprint.z - core::at(gap->reach_left(), half)) * cell_length) +
                 (length * kHalf)) -
                cell_length;
            at.y = static_cast<float>(y);
            break;
        case 3:
            x = static_cast<float>(
                ((static_cast<double>(footprint.x - core::at(gap->reach_left(), half)) * w) + (length * kHalf)) - w);
            z = ((static_cast<double>(footprint.l + footprint.z) * cell_length) - (cell_length * kHalf)) - 1.0;
            at.y = static_cast<float>(y);
            break;
        default:
            break;
        }
        at.x = static_cast<float>(x - (w * gap_grid_offset()));
        at.z = static_cast<float>(z - (cell_length * gap_grid_offset()));
        // The gap's trigger piece, its length in cells
        void* piece = builder_->make_gap_marker_piece(&at, length, along_x);
        static_cast<ParkTile*>(piece)->set_length(static_cast<float>(length / w));
        if (half == 0) {
            gap_registry_->start_gap(piece, meta, tab);
        } else {
            gap_registry_->end_gap(piece, meta, tab);
        }
    }
    gap->set_slot_index(tab);
    gap_registry_->store_gap_details(tab, gap);
    return true;
}

void ParkLayout::drop_gap_entry(GapDetails* gap)
{
    std::array<PieceGroup*, 2> halves{}; // (unset for a half with nothing there, as in the original)
    for (int half = 0; half < gap->halves_placed(); ++half) {
        const MapListHolder metas(get_intersecting_metas(core::at(gap->at_point(), half), 0, nullptr, 1U));
        if (metas.list()->following()) {
            print_map_list(metas.list());
        }
        for (const MapListNode* node = metas.list(); node; node = node->following()) {
            PieceGroup* meta = node->get_piece_group()->get_cast();
            core::at(halves, half) = meta;
            const CellBox& footprint = meta->cell_footprint();
            if (footprint.x == core::at(gap->at_point(), half).x && footprint.y == core::at(gap->at_point(), half).y &&
                footprint.z == core::at(gap->at_point(), half).z) {
                break;
            }
        }
        PieceGroup* meta = core::at(halves, half);
        meta->spotlight(0, 0);
        meta->flag_bits() &= ~PieceGroup::kHighlightable;
    }
    int index = 0;
    // The gap of the first half, removed
    if (gap_registry_->get_gap_index(core::at(halves, 0), &index)) {
        gap_registry_->drop_gap_entry(index, true);
    }
    gap->set_slot_index(-1);
    gap->set_cancel_flags(static_cast<uint32_t>(script::fetch_int_value(kCANCELGROUND))); // CANCEL_GROUND
}

void ParkLayout::print_map_list(const MapListNode* list)
{
    debug::printf("contents of map list:\n");
    for (const MapListNode* node = list; node; node = node->following()) {
        debug::printf("   address=%p area=", node->piece_group());
        const CellBox& footprint = node->piece_group()->cell_footprint();
        debug::printf("pos=(%d,%d,%d) dims=(%d,%d,%d)\n", footprint.x, static_cast<int>(footprint.y), footprint.z,
                      footprint.w, footprint.h, footprint.l);
    }
}

} // namespace park_editing

