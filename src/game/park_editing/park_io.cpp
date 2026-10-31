// The park editor's park set up from the scripts (palette, bounds, road mask, lights) and kept in compressed
// form (the map that's saved and sent), the gaps attached to pieces, and the memory bar.

#include "game/park_editing/park.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "game/park/park_memory.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/park_editing/rail_editor.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/park_editor.h"

namespace park_editing {

float g_park_gap_grid_offset{28.0f};
park_editing::ParkBuilderMode* g_park_editor;

namespace {

memory::Arena* park_heap() { return g_park_heap; }

// The cell size (120, 48, 120) and the grid's offset in cells used by the gap pieces (28).
float cell_size_x() { return g_park_cell_size_x; }
float cell_size_y() { return g_park_cell_size_y; }
float cell_size_z() { return g_park_cell_size_z; }
float gap_grid_offset() { return g_park_gap_grid_offset; }

// The park editor object (0x007CE5D8, 0x7C bytes): the value at +0x78 goes into the compressed map.
uint32_t editor_value78()
{
    auto* editor = reinterpret_cast<uint8_t*>(g_park_editor);
    return *reinterpret_cast<uint32_t*>(editor + 0x78);
}
uint8_t generator_max_players(void* generator) { return *(static_cast<uint8_t*>(generator) + 0x20); }

void* piece_source(void* generator, uint32_t name)
{
    return static_cast<ParkAssembler*>(generator)->get_piece_source(name);
}
void get_footprint_size(void* piece, CellBox* dims) { static_cast<ParkTile*>(piece)->get_footprint_size(dims); }
// A new palette metapiece on the park heap.
PieceGroup* new_abstract_meta()
{
    memory::enter_heap_scope(park_heap());
    void* memory = memory::alloc(sizeof(PieceGroup));
    PieceGroup* meta = memory ? static_cast<PieceBlueprint*>(memory)->make_new() : nullptr;
    memory::leave_heap_scope();
    return meta;
}
// Element `i` of a script array of integers, as a byte.
uint8_t byte_at(script::Array* array, uint32_t i) { return static_cast<uint8_t>(array->fetch_part(i)); }

} // namespace

void ParkLayout::set_up_abstract_metas()
{
    script::Array* all = script::fetch_list("Ed_standard_metapieces"); // (0x00479090, asserting)
    for (uint32_t i = 0; i < all->extent_; ++i) {
        auto* desc = reinterpret_cast<script::Struct*>(all->fetch_part(i));
        uint32_t single = 0;
        if (desc->read_hash_value("single", &single, 0))
            piece_source(builder_, single); // (loads the piece)
    }

    uint32_t piece_names[1000]; // every piece a metapiece uses
    uint32_t num_piece_names = 0;
    for (uint32_t n = 0; n < all->extent_; ++n) {
        auto* desc = reinterpret_cast<script::Struct*>(all->fetch_part(n));
        PieceGroup* meta = new_abstract_meta();
        uint32_t single = 0;
        script::Array* pieces = nullptr;
        if (desc->read_hash_value("single", &single, 0)) {
            meta->commission(1);
            void* source = piece_source(builder_, single);
            CellBox pos;
            pos.assign_value(0, 0, 0, 0, 0, 0);
            get_footprint_size(source, &pos);
            script::Array* at = nullptr;
            if (desc->fetch_list("pos", &at, 0)) {
                uint8_t z = byte_at(at, 2);
                uint8_t y = byte_at(at, 1);
                pos.x = byte_at(at, 0);
                pos.y = static_cast<int8_t>(y);
                pos.z = z;
            }
            meta->insert_tile(source, &pos, 0, false);
            uint32_t name = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(source) + 4); // the source's name
            piece_names[num_piece_names++] = name;
            meta->flag_bits_ |= PieceGroup::kSector;
            meta->name_key_ = name;
            uint32_t special;                                     // (left unset if there's none, as in the original)
            if (desc->read_hash_value(0xF176A9FD, &special, 0)) { // special_type
                switch (special) {
                case 0x2813AB8A: // gap_placement
                    meta->flag_bits_ |= PieceGroup::kGap;
                    break;
                case 0xFC26B6F4: // area_selection
                    meta->flag_bits_ |= PieceGroup::kMarqueeTool;
                    break;
                case 0xFFD81C08: // rail_placement
                    meta->flag_bits_ |= PieceGroup::kRailPlacement;
                    break;
                case 0x48FA9C7B: // restart_1
                    restart_pieces_[0] = single;
                    break;
                case 0x419183B2: // restart_multi
                    restart_pieces_[1] = single;
                    break;
                case 0xE69AEFAF: // restart_horse
                    restart_pieces_[2] = single;
                    break;
                case 0xA091CE25: // king_of_hill
                    restart_pieces_[3] = single;
                    break;
                case 0x74B0F4E9: // red_flag
                    restart_pieces_[4] = single;
                    meta->flag_bits_ |= PieceGroup::kRestartFlag;
                    break;
                case 0xC893ACF8: // green_flag
                    restart_pieces_[5] = single;
                    meta->flag_bits_ |= PieceGroup::kRestartFlag;
                    break;
                case 0x40BDFA7E: // blue_flag
                    restart_pieces_[6] = single;
                    meta->flag_bits_ |= PieceGroup::kRestartFlag;
                    break;
                case 0x3AE047FC: // yellow_flag
                    restart_pieces_[7] = single;
                    meta->flag_bits_ |= PieceGroup::kRestartFlag;
                    break;
                }
            }
        } else if (desc->fetch_list("multiple", &pieces, 0)) {
            bool unslidable = false;
            meta->commission(static_cast<int>(pieces->extent_));
            for (uint32_t i = 0; i < pieces->extent_; ++i) {
                auto* entry = reinterpret_cast<script::Struct*>(pieces->fetch_part(i));
                uint32_t name = entry->step_to_entry(nullptr)->checksum; // the piece's name comes first
                script::Array* at = nullptr;
                entry->fetch_list(0u, &at, 1); // its unnamed [x y z (rot)]
                void* source = piece_source(builder_, name);
                piece_names[num_piece_names++] = name;
                CellBox pos;
                pos.assign_value(0, 0, 0, 0, 0, 0);
                get_footprint_size(source, &pos);
                uint8_t z = byte_at(at, 2);
                uint8_t y = byte_at(at, 1);
                pos.x = byte_at(at, 0);
                pos.y = static_cast<int8_t>(y);
                pos.z = z;
                int rot = at->extent_ > 3 ? static_cast<int>(at->fetch_part(3)) : 0;
                if (entry->has_bare_flag(0x0CD75A3Fu))
                    unslidable = true;
                meta->insert_tile(source, &pos, rot, entry->has_bare_flag(0x0CD75A3Fu));
            }
            if (unslidable)
                meta->flag_bits_ |= PieceGroup::kFloorLocked;
        } else if (desc->fetch_list("ack", &pieces, 0)) { // made of other palette metapieces
            meta->commission(static_cast<int>(pieces->extent_));
            for (uint32_t i = 0; i < pieces->extent_; ++i) {
                auto* entry = reinterpret_cast<script::Struct*>(pieces->fetch_part(i));
                uint32_t name = entry->step_to_entry(nullptr)->checksum;
                script::Array* at = nullptr;
                entry->fetch_list(0u, &at, 1);
                PieceGroup* part = nullptr;
                for (MapListNode* node = abstract_metas_; node; node = node->following_) {
                    if (node->get_piece_group()->get_cast()->name_key_ == name) {
                        part = node->get_piece_group()->get_cast();
                        break;
                    }
                }
                CellBox pos = part->cell_footprint_; // (an unknown name crashes, as in the original)
                uint8_t z = byte_at(at, 2);
                uint8_t y = byte_at(at, 1);
                pos.x = byte_at(at, 0);
                pos.y = static_cast<int8_t>(y);
                pos.z = z;
                int rot = at->extent_ > 3 ? static_cast<int>(at->fetch_part(3)) : 0;
                static_cast<PieceBlueprint*>(meta)->add_meta_piece(part, &pos, rot, entry->has_bare_flag(0x0CD75A3Fu));
            }
        }
        if (desc->read_hash_value("name", &meta->name_key_, 0))
            meta->flag_bits_ &= ~PieceGroup::kSector;
        if (desc->has_bare_flag("is_riser"))
            meta->flag_bits_ |= PieceGroup::kBuiltShell;
        if (desc->has_bare_flag("no_rails"))
            meta->flag_bits_ |= PieceGroup::kNoRails;
        MapListNode* node = MapListNode::fabricate();
        node->piece_group_ = meta; // (out of memory crashes, as in the original)
        node->following_ = abstract_metas_;
        abstract_metas_ = node;
        ++num_abstract_metas_;
    }

    // a single-piece palette metapiece for every piece used that hasn't one
    for (uint32_t i = 0; i < num_piece_names; ++i) {
        uint32_t name = piece_names[i];
        bool found = false;
        for (MapListNode* node = abstract_metas_; node; node = node->following_) {
            if (node->get_piece_group()->get_cast()->name_key_ == name) {
                found = true;
                break;
            }
        }
        if (found)
            continue;
        PieceGroup* meta = new_abstract_meta();
        meta->commission(1);
        void* source = piece_source(builder_, name);
        CellBox pos;
        pos.assign_value(0, 0, 0, 0, 0, 0);
        get_footprint_size(source, &pos);
        meta->insert_tile(source, &pos, 0, false);
        meta->name_key_ = name;
        meta->flag_bits_ |= PieceGroup::kSector;
        MapListNode* node = MapListNode::fabricate();
        node->piece_group_ = meta;
        node->following_ = abstract_metas_;
        abstract_metas_ = node;
        ++num_abstract_metas_;
    }
}

void ParkLayout::write_compressed_map()
{
    if (flag_bits_ & kMapNewerThanPark)
        return;
    CompressedMap& map = map_;
    std::memset(&map, 0, sizeof(map));
    script::Array* save_names = script::fetch_list("Ed_Save_Map"); // the palette names, by save index
    uint32_t name_total = save_names->extent_;
    map.rev_ = CompressedMap::kVersion;
    map.shell_size_ = static_cast<uint16_t>(shell_size_);
    map.unknown0a_ = 0;
    map.pos_x_ = build_area_.x;
    map.depth_cell_ = build_area_.z;
    map.w_ = build_area_.w;
    map.l_ = build_area_.l;
    map.style_set_ = editor_value78();
    map.max_players_ = generator_max_players(builder_);
    char* to = map.label_;
    for (const char* from = park_name_; (*to++ = *from++) != '\0';) {
    }
    auto* out = reinterpret_cast<uint8_t*>(map.floor_heights_);
    for (int x = 0; x < kFloorSize; ++x)
        for (int z = 0; z < kFloorSize; ++z)
            *out++ = static_cast<uint8_t>(floor_[x][z].level_y_);

    int num_metas = 0;
    for (MapListNode* node = concrete_metas_; node; node = node->following_) {
        PieceGroup* meta = node->get_piece_group()->get_cast();
        uint32_t flags = meta->flag_bits_;
        if ((flags & PieceGroup::kBuiltShell) || (flags & PieceGroup::kFence) || !(flags & PieceGroup::kInPark))
            continue;
        CellBox at = meta->cell_footprint_;
        uint32_t index = 0;
        while (index < name_total && save_names->fetch_part(index) != meta->name_key_)
            ++index;
        if (index >= name_total)
            continue;
        auto* words = reinterpret_cast<uint32_t*>(out);
        words[0] = ((static_cast<uint32_t>(at.z) << 8 | at.x) << 10) | (index & 0x3FF);
        words[1] = ((static_cast<uint32_t>(static_cast<uint8_t>(at.y)) - 0x10) & 0x1F) << 2 |
                   (static_cast<uint32_t>(meta->quarter_turns_) & 3);
        out += 8;
        ++num_metas;
    }
    map.composite_count_ = static_cast<uint16_t>(num_metas);

    GapDetails* gaps[32];
    int num_gaps = 0;
    for (int i = 0; i < 32; ++i) {
        int half = 0;
        GapDetails* gap = gap_registry_->get_gap_info(i, &half);
        if (gap && half == 0 && gap->halves_placed_ == 2)
            gaps[num_gaps++] = gap;
    }
    map.num_gaps_ = static_cast<uint16_t>(num_gaps);
    for (int i = 0; i < num_gaps; ++i) {
        const GapDetails* gap = gaps[i];
        out[8] = 0;
        out[0] = gap->at_point_[0].x;
        out[2] = static_cast<uint8_t>(gap->at_point_[0].y + 8);
        out[4] = gap->at_point_[0].z;
        out[8] |= static_cast<uint8_t>(gap->quarter_turns_[0]);
        out[6] = static_cast<uint8_t>(gap->reach_left_[0] << 4);
        out[6] = static_cast<uint8_t>((gap->reach_right_[0] & 0xF) | (gap->reach_left_[0] << 4));
        out[1] = gap->at_point_[1].x;
        out[3] = static_cast<uint8_t>(gap->at_point_[1].y + 8);
        out[5] = gap->at_point_[1].z;
        out[8] |= static_cast<uint8_t>(gap->quarter_turns_[1] << 4);
        out[7] = static_cast<uint8_t>((gap->reach_right_[1] & 0xF) | (gap->reach_left_[1] << 4));
        char* text = reinterpret_cast<char*>(out + 0xA);
        for (const char* from = gap->text_; (*text++ = *from++) != '\0';) {
        }
        *reinterpret_cast<uint16_t*>(out + 0x2A) = static_cast<uint16_t>(gap->points_held_);
        *reinterpret_cast<uint32_t*>(out + 0x2C) = gap->cancel_flags_;
        out += 0x30;
    }
    map.extent_ = static_cast<uint16_t>(out - reinterpret_cast<uint8_t*>(&map));
    if (!(flag_bits_ & (kMapValid | kMapNewerThanPark)))
        flag_bits_ |= kMapNotSavedLocal;
    flag_bits_ = (flag_bits_ & ~kMapNewerThanPark) | kMapValid | kMapInSync;
    map.checksum_ = checksums::generate_crc_from_buffer(&map.extent_, sizeof(map) - 4);
}

void ParkLayout::set_default_bounds()
{
    int inner = script::fetch_int_value("ed_default_inner_dim"); // (0x004790C0, asserting)
    shell_bounds_.assign_value(1, -4, 1, 56, 8, 56);
    uint8_t corner = static_cast<uint8_t>((56 - inner) / 2 + 1);
    build_area_.assign_value(corner, -4, corner, static_cast<uint8_t>(inner), 8, static_cast<uint8_t>(inner));
}

void ParkLayout::set_up_road_mask()
{
    script::Array* roads = script::fetch_list("Ed_Roadmask");
    num_blocked_areas_ = roads->extent_;
    // (the first four rectangles are set elsewhere; this reads the array from element 4, past its end, as the
    // original does)
    for (uint32_t i = 4; i < num_blocked_areas_ + 4; ++i) {
        auto* rect = reinterpret_cast<script::Array*>(roads->fetch_part(i));
        CellBox& mask = (&blocked_areas_[0])[i]; // (past the eighth: over the length itself, as in the original)
        *mask.get_byte(0) = byte_at(rect, 0);
        *mask.get_byte(2) = byte_at(rect, 1);
        *mask.get_byte(3) = byte_at(rect, 2);
        *mask.get_byte(5) = byte_at(rect, 3);
        *mask.get_byte(1) = 0;
        mask.stretch_vertically();
    }
    num_blocked_areas_ += 4;
}

void ParkLayout::set_up_lights()
{
    script::Array* themes = script::fetch_list("Ed_Light_Info"); // (the original doesn't assert)
    auto* theme = reinterpret_cast<script::Array*>(themes->fetch_part(0));
    auto* info = reinterpret_cast<script::Struct*>(theme->fetch_part(0));
    float ambient_const, falloff_const, cursor_ambience;
    script::VectorValue ambient, falloff;
    script::Array* positions;
    info->fetch_float("ambient_const", &ambient_const, 1);
    info->fetch_vec("ambient_rgb", &ambient, 1);
    info->fetch_float("falloff_const", &falloff_const, 1);
    info->fetch_vec("falloff_rgb", &falloff, 1);
    info->fetch_float("cursor_ambience", &cursor_ambience, 1);
    info->fetch_list("pos", &positions, 1);
    // The generator's lights and their positions
    builder_->configure_lighting(static_cast<int>(positions->extent_), ambient.x * ambient_const,
                                 ambient.y * ambient_const, ambient.z * ambient_const, falloff.x * falloff_const,
                                 falloff.y * falloff_const, falloff.z * falloff_const, cursor_ambience);
    for (int i = 0; i < static_cast<int>(positions->extent_); ++i) {
        auto* v = reinterpret_cast<float*>(positions->fetch_part(static_cast<uint32_t>(i))); // a CVector (x at +4)
        maths::Vec4 pos;
        pos.x = cell_size_x() * v[1];
        pos.y = cell_size_y() * v[2];
        pos.z = cell_size_z() * v[3];
        pos.w = 1.0f;
        builder_->move_light_source(&pos, i);
    }
}

void ParkLayout::update_percent_bar()
{
    uint8_t stats[0x6C];
    // 0x0059E730: the park's memory use (fills the stats at 0x007CE618, returns them)
    auto* use = reinterpret_cast<uint8_t*>(get_memory_stats(reinterpret_cast<MemoryStats*>(stats)));
    float full;
    std::memcpy(&full, use + 0x2C, 4);
    float percent = full;
    if (full < 0.0f)
        percent = 0.0f;
    else if (full > 1.0f)
        percent = 1.0f;
    script::Struct* memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory->prepare_all() : nullptr;
    params->push_float(0x6D6C056Du, percent);  // park_percent (out of memory crashes, as in the original)
    if (get_attributes().max_usage_ >= 1.0f)   // (0x007CE644: the park memory stats' largest usage)
        params->put_name_key(0u, 0x97A8E59Cu); // park_full
    script::launch_script(0xE2508960, params, nullptr, false, nullptr); // parked_update_percent_bar
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

void ParkLayout::write_compressed_map_if_built()
{
    if (park_built_)
        write_compressed_map();
}

void ParkLayout::export_to_struct(script::Struct* out)
{
    if (park_built_)
        write_compressed_map();
    flag_bits_ &= ~kMapNotSavedLocal;
    script::Array* memory = script::alloc_script_array();
    script::Array* map = memory ? memory->prepare_all() : nullptr;
    map->configure_size_kind(0xEA6, script::kQbvalInteger); // (out of memory crashes, as in the original)
    const auto* words = reinterpret_cast<const uint32_t*>(&map_);
    for (uint32_t i = 0; i < 0xEA6; ++i)
        map->set_element(i, words[i]);
    out->append_array_ref(0x337C5289u, map); // Park_editor_map
    script::Struct* struct_memory = script::spawn_param_bag();
    script::Struct* goals = struct_memory ? struct_memory->prepare_all() : nullptr;
    // the goal editor (module GoalEditor, 0x004E8470) writes the park's goals (level Load_Sk5Ed)
    ObjectiveAuthoring::get_pull()->write_level_goals(0xE8B4B836, goals);
    out->append_struct_ref(0xD8EB825Eu, goals); // Park_editor_goals
    // the rail editor (module RailEditor, 0x004F4970) writes the park's rails
    RailAuthoring::sole()->write_to_structure(out);
    out->put_int(0xB7E39B53u, generator_max_players(builder_)); // MaxPlayers
}

void ParkLayout::set_shell_size(int theme)
{
    shell_size_ = theme;
    write_compressed_map();
}

void ParkLayout::clear_compressed_map()
{
    set_default_bounds();
    CompressedMap& map = map_;
    map.rev_ = CompressedMap::kVersion;
    map.shell_size_ = static_cast<uint16_t>(shell_size_);
    map.unknown0a_ = 0;
    map.pos_x_ = build_area_.x;
    map.depth_cell_ = build_area_.z;
    map.w_ = build_area_.w;
    map.l_ = build_area_.l;
    map.style_set_ = static_cast<void*>(g_park_editor) ? editor_value78() : 0;
    map.max_players_ = 2;
    std::memcpy(map.label_, "Unnamed park", 13); // "Unnamed park"
    std::memset(map.floor_heights_, 0, kFloorSize * kFloorSize);
    map.composite_count_ = 0;
    uint32_t used = offsetof(CompressedMap, floor_heights_) + kFloorSize * kFloorSize;
    // (the checksum is taken before the size and the gap count are set, as in the original)
    uint32_t checksum = checksums::generate_crc_from_buffer(&map.extent_, static_cast<int>(used - 4));
    map.extent_ = static_cast<uint16_t>(used);
    map.checksum_ = checksum;
    map.num_gaps_ = 0;
    if (!(flag_bits_ & (kMapValid | kMapNewerThanPark)))
        flag_bits_ |= kMapNotSavedLocal;
    flag_bits_ = (flag_bits_ & ~kMapNewerThanPark) | kMapValid | kMapInSync;
}

bool ParkLayout::register_gap_entry(GapDetails* gap)
{
    int tab = gap_registry_->find_empty_gap_slot();
    if (tab == -1)
        return false;
    // The trigger's position (x87 arithmetic: x is stored as a float on the way, z isn't). For a rotation past 3
    // the previous half's position is used again (the original's are left unset before the first half).
    maths::Vec4 at = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int half = 0; half < gap->halves_placed_; ++half) {
        CellBox& pos = gap->at_point_[half];
        pos.w = 1;
        pos.h = 1;
        pos.l = 1;
        MapListHolder metas(get_intersecting_metas(pos, 0, nullptr, true));
        if (!metas.list)
            return false;
        PieceGroup* meta = metas.list->get_piece_group()->get_cast();
        meta->flag_bits_ |= PieceGroup::kHighlightable;
        pos = meta->cell_footprint_;
        CellBox d = meta->cell_footprint_;
        float length = gap->fetch_magnitude(half);
        const double w = cell_size_x(), l = cell_size_z();
        const double y = static_cast<double>(d.y) * cell_size_y();
        int along_x = 0;
        float x = at.x;
        double z = at.z;
        switch (gap->quarter_turns_[half]) {
        case 0:
            along_x = 1;
            x = static_cast<float>((static_cast<double>(d.w + d.x) * w - w * 0.5) - 1.0);
            z = (static_cast<double>(d.z - gap->reach_right_[half]) * l + length * 0.5) - l;
            at.y = static_cast<float>(y);
            break;
        case 1:
            x = static_cast<float>((static_cast<double>(d.x - gap->reach_right_[half]) * w + length * 0.5) - w);
            z = (static_cast<double>(d.z) * l - l * 0.5) + 1.0;
            at.y = static_cast<float>(y);
            break;
        case 2:
            along_x = 1;
            x = static_cast<float>((static_cast<double>(d.x) * w - w * 0.5) + 1.0);
            z = (static_cast<double>(d.z - gap->reach_left_[half]) * l + length * 0.5) - l;
            at.y = static_cast<float>(y);
            break;
        case 3:
            x = static_cast<float>((static_cast<double>(d.x - gap->reach_left_[half]) * w + length * 0.5) - w);
            z = (static_cast<double>(d.l + d.z) * l - l * 0.5) - 1.0;
            at.y = static_cast<float>(y);
            break;
        }
        at.x = static_cast<float>(x - w * gap_grid_offset());
        at.z = static_cast<float>(z - l * gap_grid_offset());
        // The gap's trigger piece, its length in cells
        void* piece = builder_->make_gap_marker_piece(&at, length, along_x);
        static_cast<ParkTile*>(piece)->set_length(static_cast<float>(length / w));
        if (half == 0)
            gap_registry_->start_gap(piece, meta, tab);
        else
            gap_registry_->end_gap(piece, meta, tab);
    }
    gap->slot_index_ = tab;
    gap_registry_->store_gap_details(tab, gap);
    return true;
}

void ParkLayout::drop_gap_entry(GapDetails* gap)
{
    PieceGroup* halves[2]; // (unset for a half with nothing there, as in the original)
    for (int half = 0; half < gap->halves_placed_; ++half) {
        MapListHolder metas(get_intersecting_metas(gap->at_point_[half], 0, nullptr, true));
        if (metas.list->following_)
            print_map_list(metas.list);
        for (MapListNode* node = metas.list; node; node = node->following_) {
            PieceGroup* meta = node->get_piece_group()->get_cast();
            halves[half] = meta;
            const CellBox& d = meta->cell_footprint_;
            if (d.x == gap->at_point_[half].x && d.y == gap->at_point_[half].y && d.z == gap->at_point_[half].z)
                break;
        }
        PieceGroup* meta = halves[half];
        meta->spotlight(false, 0);
        meta->flag_bits_ &= ~PieceGroup::kHighlightable;
    }
    int index;
    // The gap of the first half, removed
    if (gap_registry_->get_gap_index(halves[0], &index))
        gap_registry_->drop_gap_entry(index, true);
    gap->slot_index_ = -1;
    gap->cancel_flags_ = static_cast<uint32_t>(script::fetch_int_value(0x9A27E74Du)); // CANCEL_GROUND
}

void ParkLayout::print_map_list(const MapListNode* list)
{
    debug::printf("contents of map list:\n");
    for (const MapListNode* node = list; node; node = node->following_) {
        debug::printf("   address=%p area=", node->piece_group_);
        const CellBox& d = node->piece_group_->cell_footprint_;
        debug::printf("pos=(%d,%d,%d) dims=(%d,%d,%d)\n", d.x, static_cast<int>(d.y), d.z, d.w, d.h, d.l);
    }
}

} // namespace park_editing

using park_editing::ParkLayout;
