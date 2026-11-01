// The park manager (park_editing::ParkLayout): making and deleting it, the metapieces in the park, taking it down.

#include "game/park_editing/park.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "game/park/park_memory.h"
#include "game/park_editing/park_editor.h"

namespace park_editing {

namespace {

// The park's cost (0x0059E190 adds a metapiece's, 0x0059E1E0 takes it away).
void add_piece_cost(PieceGroup* meta) { add_piece_dma(meta); }
void remove_piece_cost(PieceGroup* meta) { remove_piece_dma(meta); }

// Whether the generator rebuilds its collision (0x00592090 looks): off while every piece is deleted.
bool& generator_rebuilds_collision() { return g_collision_allowed; }

// The park editor's pools (the clipboards', the lists'...) freed (0x0059E4E0).
void destroy_park_pools() { power_down(); }

} // namespace

// 0x00598E00
ParkLayout::ParkLayout()
{
    build_area_ = CellBox::get_make(0, 0, 0, 0, 0, 0);
    shell_bounds_ = CellBox::get_make(0, 0, 0, 0, 0, 0);
    for (CellBox& area : blocked_areas_)
        area = CellBox::get_make(0, 0, 0, 0, 0, 0);
    void* memory = memory::alloc(0x4A4);
    builder_ = memory ? new (memory) ParkAssembler : nullptr;
    concrete_metas_ = nullptr;
    abstract_metas_ = nullptr;
    num_concrete_metas_ = 0;
    num_abstract_metas_ = 0;
    std::memset(cell_metas_, 0, sizeof(cell_metas_));
    park_built_ = false;
    flag_bits_ = 0;
    field_05_ = false;
    gap_registry_ = nullptr;
    gap_manager_copy_ = nullptr;
    // The shell's blocks of each of the 8 kinds.
    for (int i = 1; i <= 8; ++i) {
        char name[128];
        std::snprintf(name, sizeof(name), "floor_wall_block%d", i);
        shell_blocks_[i - 1][0] = checksums::hash_from_text(name);
        std::snprintf(name, sizeof(name), "wall_block%d", i);
        shell_blocks_[i - 1][1] = checksums::hash_from_text(name);
        std::snprintf(name, sizeof(name), "floor_block%d", i);
        shell_blocks_[i - 1][2] = checksums::hash_from_text(name);
    }
    shell_size_ = 0;
    clear_compressed_map();
    riser_list_ = nullptr;
    park_name_[0] = '\0';
    field_74_ = 0;
    field_78_ = nullptr;
}

// 0x00599BF0
void ParkLayout::spawn_occurrence()
{
    void* memory = memory::alloc(sizeof(ParkLayout));
    sole() = memory ? new (memory) ParkLayout : nullptr;
}

// 0x00599C70
void ParkLayout::discard_occurrence()
{
    if (ParkLayout* manager = sole()) {
        if (manager->field_78_) {
            memory::free_malloc(manager->field_78_);
            manager->field_78_ = nullptr;
        }
        if (ParkAssembler* generator = manager->builder_) {
            generator->teardown();
            memory::dealloc(generator);
        }
        memory::dealloc(manager);
    }
    sole() = nullptr;
}

// 0x00599A20
void ParkLayout::place_meta_piece_in_park(CellBox pos, PieceGroup* meta)
{
    meta->flag_bits_ |= PieceGroup::kInPark;
    meta->cell_footprint_.x = pos.x;
    meta->cell_footprint_.y = pos.y;
    meta->cell_footprint_.z = pos.z;

    // Where its pieces go from: the middle of the metapiece in the world (the grid centred on the origin).
    maths::Vec4 origin;
    origin.x = static_cast<float>((meta->cell_footprint_.w * 0.5 + pos.x) * kGridPitch -
                                  static_cast<double>(kGridPitch) * kGridCells * 0.5);
    origin.z = static_cast<float>((meta->cell_footprint_.l * 0.5 + pos.z) * kGridStepAlong -
                                  static_cast<double>(kGridStepAlong) * kGridCells * 0.5);
    origin.y = static_cast<float>(pos.y * static_cast<double>(kGridStepUp));
    if (meta->flag_bits_ & PieceGroup::kRiserBelow)
        origin.y = static_cast<float>(static_cast<double>(origin.y) - kRiserBelowDrop);
    origin.w = 1.0f; // (the original leaves w as it was on the stack)
    for (uint32_t i = 0; i < meta->filled_slots_; ++i) {
        maths::Vec4 relative;
        meta->get_relative_position(&relative, i);
        auto* piece = static_cast<ParkTile*>(meta->get_piece(i)); // (a placed metapiece's)
        maths::Vec4 position;
        position.x = static_cast<float>(static_cast<double>(relative.x) + origin.x);
        position.z = static_cast<float>(static_cast<double>(relative.z) + origin.z);
        position.y = static_cast<float>(static_cast<double>(relative.y) + origin.y);
        position.w = 1.0f; // (likewise)
        piece->change_place(&position, 1);
        piece->set_quarter_turns((piece->quarter_turns_ + meta->quarter_turns_) & 3, 1);
        builder_->insert_tile(piece);
        builder_->light_piece(piece);
    }
    set_meta_in_cells(meta, 1);
    add_meta_to_cells(meta);
    for (int player = 1; player < 9; ++player) {
        if (meta->name_key_ == restart_pieces_[player - 1]) {
            builder_->plant_restart_point(&origin, meta->quarter_turns_, &pos, player, 0, 0);
            break;
        }
    }
    if (!(meta->flag_bits_ & PieceGroup::kFence) && !(meta->flag_bits_ & PieceGroup::kBuiltShell))
        flag_bits_ &= ~(kMapUpToDate | kMapSaved);
    add_piece_cost(meta);
}

// 0x00599880
uint32_t ParkLayout::free_piece_groups_in_region(CellBox area, uint32_t flags)
{
    int metas = 0;
    uint16_t pieces = 0;
    MapListNode* list = get_intersecting_metas(area, 0, nullptr, (flags >> 1) & 0xFFFFFF01);
    if (list) {
        for (MapListNode* node = list; node; node = node->following_) {
            if ((flags & 4) && (node->get_placed_piece()->flag_bits_ & PieceGroup::kFlag20))
                continue;
            ++metas;
            pieces = static_cast<uint16_t>(pieces + node->get_placed_piece()->filled_slots_);
            free_placed_piece(node->get_placed_piece(), flags);
        }
    }
    MapListNode::free_node_chain(list);
    builder_->clean_up_out_refs();
    return static_cast<uint32_t>(static_cast<uint16_t>(metas)) << 16 | pieces;
}

// 0x00599960
void ParkLayout::highlight_metas_within_area(CellBox area, int on)
{
    MapListHolder metas(collect_pieces_in_region(area));
    for (MapListNode* node = metas.list; node; node = node->following_) {
        PieceGroup* meta = node->get_piece_group();
        if (meta && (meta->flag_bits_ & PieceGroup::kFlag1) && meta->fits_inside_region(&area))
            node->get_placed_piece()->spotlight(on, 0);
    }
}

// 0x0059AE90
void ParkLayout::free_placed_piece(PieceGroup* meta, uint32_t flags)
{
    if (meta->flag_bits_ & PieceGroup::kCursor)
        return;
    if (BuildPointer* cursor = BuildPointer::sole())
        cursor->forget_gap_at(meta);
    int gap = 0;
    if (gap_registry_->get_gap_index(meta, &gap)) {
        int half = 0;
        drop_gap_entry(gap_registry_->get_gap_info(gap, &half));
    }
    // A change to the park the player made: the compressed map is out of date.
    uint32_t meta_flags = meta->flag_bits_;
    if (!(meta_flags & PieceGroup::kFence) && (meta_flags & PieceGroup::kInPark) &&
        !(meta_flags & PieceGroup::kBuiltShell))
        flag_bits_ &= ~(kMapUpToDate | kMapSaved);
    // What stands on it goes too.
    if (!(meta->flag_bits_ & PieceGroup::kBuiltShell) && (flags & 1)) {
        CellBox above = meta->cell_footprint_;
        above.y = static_cast<int8_t>(above.h + above.y);
        above.stretch_vertically();
        free_piece_groups_in_region(above, 3);
    }
    set_meta_in_cells(meta, 0);
    remove_meta_from_cells(meta);
    remove_meta_from_list(meta, &concrete_metas_);
    for (int player = 1; player < 9; ++player) {
        if (meta->name_key_ == restart_pieces_[player - 1]) {
            builder_->unplant_restart_point(&meta->cell_footprint_, player);
            break;
        }
    }
    for (uint32_t i = 0; i < meta->filled_slots_; ++i)
        builder_->destroy_piece(meta->get_descriptor(i)->piece);
    remove_piece_cost(meta);
    delete meta;
    --num_concrete_metas_;
}

// 0x0059B020
void ParkLayout::destroy_concrete_metas(int keep_rails)
{
    generator_rebuilds_collision() = false;
    gap_registry_->clear_gap_entries();
    for (MapListNode* node = concrete_metas_; node;) {
        MapListNode* next = node->following_;
        free_placed_piece(node->get_placed_piece(), 0);
        node = next;
    }
    builder_->destroy_pieces(keep_rails);
    builder_->clear_restarts();
    generator_rebuilds_collision() = true;
    if (keep_rails)
        builder_->rebuild_collision();
}

// 0x0059B090
PieceGroup* ParkLayout::shift_placed_piece(PieceGroup* meta, int dx, int dy, int dz)
{
    CellBox pos = meta->cell_footprint_;
    uint32_t checksum = meta->name_key_;
    int rot = meta->quarter_turns_;
    uint32_t flags = meta->flag_bits_;
    // The gap it's an end of goes with it (the lookup shrinks `pos` to 1x1x1, which the move ignores).
    bool had_gap = false;
    int half = 0;
    GapDetails* gap = get_gap_info(&pos, &half);
    GapDetails moved;
    if (gap) {
        moved = *gap;
        had_gap = true;
        drop_gap_entry(gap);
        CellBox& end = moved.at_point_[half];
        end.x = static_cast<uint8_t>(end.x + dx);
        end.y = static_cast<int8_t>(end.y + dy);
        end.z = static_cast<uint8_t>(end.z + dz);
    }
    free_placed_piece(meta, 0);
    builder_->clean_up_out_refs();
    pos.x = static_cast<uint8_t>(pos.x + dx);
    pos.y = static_cast<int8_t>(pos.y + dy);
    pos.z = static_cast<uint8_t>(pos.z + dz);
    PieceGroup* copy = make_placed_piece(find_piece_template(checksum), 0);
    copy->set_quarter_turns(rot);
    copy->flag_bits_ = flags & ~PieceGroup::kInPark;
    place_meta_piece_in_park(pos, copy);
    if (had_gap)
        register_gap_entry(&moved);
    return copy;
}

// 0x0059BD40
void ParkLayout::teardown(int keep_rails)
{
    if (!park_built_)
        return;
    if (!(flag_bits_ & (kMapUpToDate | kMapSaved)))
        write_compressed_map();
    gap_registry_->clear_gap_entries();
    while (abstract_metas_) {
        PieceGroup* meta = abstract_metas_->get_piece_group();
        remove_meta_from_list(meta, &abstract_metas_);
        delete meta;
    }
    num_abstract_metas_ = 0;
    destroy_concrete_metas(keep_rails);
    builder_->sweep_up(keep_rails);
    park_built_ = false;
    flag_bits_ = (flag_bits_ & ~kMapUpToDate) | kMapSaved;
    field_05_ = false;
    if (GapRegistry* gaps = gap_registry_) {
        gaps->~GapRegistry();
        memory::dealloc(gaps);
    }
    gap_registry_ = nullptr;
    if (riser_list_)
        MapListNode::free_node_chain(riser_list_);
    riser_list_ = nullptr;
    destroy_park_pools();
}

} // namespace park_editing
