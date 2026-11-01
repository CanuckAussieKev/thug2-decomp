// The park editor's map of metapieces: the lists of metapieces, the bucket of each cell, what stands in an
// area, the floor slide, and the floor under placed pieces.

#include <cstdint>

#include "game/park_editing/park.h"

#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "game/park/park_memory.h"

namespace park_editing {

memory::PoolPair g_node_pools;

namespace {

memory::PoolPair& node_pools() { return g_node_pools; }

// The generator's piece making.
TileTemplate* get_piece_source(ParkAssembler* generator, uint32_t name) { return generator->get_piece_source(name); }
ParkTile* create_piece(ParkAssembler* generator, TileTemplate* source, uint32_t flags)
{
    return generator->create_piece(source, flags);
}

// One floor slide step for a cell or metapiece at `current`.
int scroll_offset(uint32_t flags, int current, int height)
{
    if (flags & 1) {
        if (flags & 2)
            return height - current;
        return (current != height) - 1;
    }
    return ((flags & 2) ? 2 : 0) - 1;
}

} // namespace

// --- CMapListNode and PieceGroup (game code outside this range, or not rewritten yet) ----------------------------

MapListNode* MapListNode::fabricate()
{
    auto* node = node_pools().allocate<MapListNode>();
    if (node) {
        node->dy_ = 0;
        node->piece_group_ = nullptr;
        node->following_ = nullptr;
    }
    return node;
}

void MapListNode::dealloc(void* node) { node_pools().dealloc(node); }

void __fastcall MapListNode::free_node_chain(MapListNode* list)
{
    MapListNode* node = list;
    while (node) {
        MapListNode* next = node->following_;
        dealloc(node);
        node = next;
    }
}

void MapListNode::audit_soundness(const MapListNode* list)
{
    if (!list)
        return;
    for (const MapListNode* node = list->following_; node; node = node->following_) {
        for (const MapListNode* later = node; later; later = later->following_) {
        }
    }
}

// --- ParkLayout ------------------------------------------------------------------------------------------------

PieceGroup* ParkLayout::make_placed_piece(PieceGroup* abstract_meta, uint32_t piece_flags)
{
    memory::enter_heap_scope(park_heap());
    PieceGroup* meta = new PlacedPieceGroup;
    memory::leave_heap_scope();

    int count = abstract_meta->count_pieces();
    meta->commission(count); // (a failed allocation crashes here, as in the original)
    meta->name_key_ = abstract_meta->name_key_;
    if (abstract_meta->flag_bits_ & PieceGroup::kBuiltShell) {
        piece_flags |= 0x20;
        meta->flag_bits_ |= PieceGroup::kBuiltShell;
    }
    if (abstract_meta->flag_bits_ & PieceGroup::kNoRails) {
        piece_flags |= 0x100;
        meta->flag_bits_ |= PieceGroup::kNoRails;
    }
    if (abstract_meta->flag_bits_ & PieceGroup::kFloorLocked)
        meta->flag_bits_ |= PieceGroup::kFloorLocked;
    for (int i = 0; i < count; ++i) {
        PieceGroupDesc entry;
        static_cast<PieceBlueprint*>(abstract_meta)->get_entry(&entry, i); // entry `i`, copied
        TileTemplate* source = get_piece_source(singleton()->builder_, reinterpret_cast<uint32_t>(entry.piece));
        ParkTile* piece = create_piece(builder_, source, piece_flags);
        piece->set_quarter_turns(entry.rot, 0); // turn it
        CellBox pos;
        pos.assign_value(entry.x, entry.y, entry.z, 0, 0, 0);
        meta->insert_tile(piece, &pos, entry.rot, entry.sets_floor_);
    }
    meta->finish_pieces();

    MapListNode* node = MapListNode::fabricate();
    node->piece_group_ = meta; // (out of memory crashes, as in the original)
    node->following_ = nullptr;
    if (!concrete_metas_) {
        concrete_metas_ = node;
    } else {
        MapListNode* last = concrete_metas_;
        while (last->following_)
            last = last->following_;
        last->following_ = node;
    }
    ++num_concrete_metas_;
    return meta;
}

bool ParkLayout::has_pieces_outside(CellBox area)
{
    for (MapListNode* node = concrete_metas_; node; node = node->following_) {
        PieceGroup* meta = node->get_piece_group()->get_cast();
        CellBox dims = meta->cell_footprint_;
        bool inside = dims.x >= area.x && dims.z >= area.z && dims.x + dims.w <= area.x + area.w &&
                      dims.z + dims.l <= area.z + area.l;
        if (inside)
            continue;
        uint32_t flags = meta->flag_bits_;
        if (!(flags & PieceGroup::kBuiltShell) && !(flags & PieceGroup::kFence) && (flags & PieceGroup::kInPark))
            return true;
    }
    return false;
}

void ParkLayout::add_meta_to_list(PieceGroup* meta, MapListNode** list)
{
    for (MapListNode* node = *list; node; node = node->following_) {
        if (node->get_piece_group() == meta)
            return;
    }
    MapListNode* node = MapListNode::fabricate();
    node->piece_group_ = meta; // (out of memory crashes, as in the original)
    node->following_ = *list;
    *list = node;
}

void ParkLayout::remove_meta_from_list(PieceGroup* meta, MapListNode** list)
{
    MapListNode* previous = nullptr;
    for (MapListNode* node = *list; node; previous = node, node = node->following_) {
        if (node->get_piece_group() != meta)
            continue;
        if (previous)
            previous->following_ = node->following_;
        else
            *list = node->following_;
        MapListNode::dealloc(node);
        return;
    }
}

void ParkLayout::add_meta_to_cells(PieceGroup* meta)
{
    for (int i = 0; i < meta->cell_footprint_.w; ++i) {
        for (int j = 0; j < meta->cell_footprint_.l; ++j) {
            MapListNode* node = MapListNode::fabricate();
            node->piece_group_ = meta; // (out of memory crashes, as in the original)
            MapListNode*& bucket = cell_metas_[meta->cell_footprint_.x + i][meta->cell_footprint_.z + j];
            node->following_ = bucket;
            bucket = node;
        }
    }
}

void ParkLayout::remove_meta_from_cells(PieceGroup* meta)
{
    for (int i = 0; i < meta->cell_footprint_.w; ++i) {
        for (int j = 0; j < meta->cell_footprint_.l; ++j) {
            MapListNode*& bucket = cell_metas_[meta->cell_footprint_.x + i][meta->cell_footprint_.z + j];
            MapListNode* previous = nullptr;
            for (MapListNode* node = bucket; node; previous = node, node = node->following_) {
                if (node->piece_group_ != meta)
                    continue;
                if (previous)
                    previous->following_ = node->following_;
                else
                    bucket = node->following_;
                MapListNode::dealloc(node);
                break;
            }
        }
    }
}

MapListNode* ParkLayout::get_intersecting_metas(CellBox area, int rot, PieceGroup* meta, uint32_t skip_inner_shell)
{
    bool precise = static_cast<uint8_t>(skip_inner_shell) != 0;
    MapListNode* list = nullptr;
    if (!meta) {
        for (int x = area.x; x < area.x + area.w; ++x) {
            for (int z = area.z; z < area.z + area.l; ++z) {
                for (MapListNode* node = cell_metas_[x][z]; node; node = node->following_) {
                    if (precise && (node->get_piece_group()->flag_bits_ & PieceGroup::kBuiltShell))
                        continue;
                    if (node->get_piece_group()->get_cast()->intersects(area, skip_inner_shell))
                        add_meta_to_list(node->get_piece_group(), &list);
                }
            }
        }
        MapListNode::audit_soundness(list);
        return list;
    }

    meta->set_quarter_turns(rot);
    area.w = meta->cell_footprint_.w;
    area.h = meta->cell_footprint_.h;
    area.l = meta->cell_footprint_.l;
    MapListHolder nearby(get_intersecting_metas(area, 0, nullptr, false));
    for (uint32_t i = 0; i < meta->filled_slots_; ++i) {
        PieceGroupDesc* entry = meta->get_entry(static_cast<int>(i));
        CellBox pos;
        pos.assign_value(entry->x, entry->y, entry->z, 0, 0, 0);
        pos.x = static_cast<uint8_t>(pos.x + area.x);
        pos.y = static_cast<int8_t>(pos.y + area.y);
        pos.z = static_cast<uint8_t>(pos.z + area.z);
        for (MapListNode* node = nearby.list; node; node = node->following_) {
            if (node->get_piece_group()->get_cast()->intersects(pos, false))
                add_meta_to_list(node->get_piece_group(), &list);
        }
    }
    meta->set_quarter_turns(0);
    MapListNode::audit_soundness(list);
    return list;
}

MapListNode* ParkLayout::collect_pieces_in_region(CellBox area)
{
    MapListNode* list = nullptr;
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            for (MapListNode* node = cell_metas_[x][z]; node; node = node->following_)
                add_meta_to_list(node->get_piece_group(), &list);
        }
    }
    return list;
}

GapDetails* ParkLayout::get_gap_info(CellBox* cell, int* half)
{
    cell->w = 1;
    cell->h = 1;
    cell->l = 1;
    MapListHolder metas(get_intersecting_metas(*cell, 0, nullptr, true));
    if (!metas.list)
        return nullptr;
    PieceGroup* meta = metas.list->get_piece_group()->get_cast();
    int gap;
    // The gap a metapiece is a half of, and its info
    if (!gap_registry_->get_gap_index(meta, &gap))
        return nullptr;
    return gap_registry_->get_gap_info(gap, half);
}

bool ParkLayout::has_floor_locked_meta(CellBox area)
{
    area.raise_ceiling_to_limit();
    MapListHolder metas(get_intersecting_metas(area, 0, nullptr, true));
    for (MapListNode* node = metas.list; node; node = node->following_) {
        if (node->get_piece_group()->get_cast()->flag_bits_ & PieceGroup::kFloorLocked)
            return true;
    }
    return false;
}

bool ParkLayout::build_riser_list(CellBox area, int y, int height, uint32_t flags, MapListNode** slid_list)
{
    area.y = static_cast<int8_t>(y);
    if (flags & 4)
        area.raise_ceiling_to_limit();
    else
        area.stretch_vertically();
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            FloorCell& cell = floor_[x][z];
            if (!cell.touched_) {
                int amount = scroll_offset(flags, cell.level_y_, height);
                cell.level_y_ += amount;
                cell.delta_ = amount;
                if (cell.level_y_ < -16 || cell.level_y_ > 15)
                    return false;
            }
            cell.touched_ = true;
        }
    }
    MapListHolder metas(get_intersecting_metas(area, 0, nullptr, true));
    for (MapListNode* node = metas.list; node; node = node->following_) {
        PieceGroup* meta = node->get_piece_group()->get_cast();
        CellBox dims = meta->cell_footprint_;
        if ((meta->flag_bits_ & PieceGroup::kBuiltShell) || (meta->flag_bits_ & PieceGroup::kFlag20))
            continue;
        meta->flag_bits_ |= PieceGroup::kFlag20;
        MapListNode* slid = MapListNode::fabricate();
        slid->following_ = *slid_list; // (out of memory crashes, as in the original)
        slid->piece_group_ = meta;
        *slid_list = slid;
        slid->dy_ = scroll_offset(flags, dims.y, height);
        if (!build_riser_list(dims, dims.y, height, flags & ~4u, slid_list))
            return false;
    }
    return true;
}

void ParkLayout::set_meta_in_cells(PieceGroup* meta, int add)
{
    for (uint32_t i = 0; i < meta->filled_slots_; ++i) {
        ParkPiece* piece = meta->get_piece(static_cast<int>(i)); // (and the cast to a concrete piece: 0x005902E0)
        PieceGroupDesc* entry = meta->get_entry(static_cast<int>(i));
        if (!entry->sets_floor_)
            continue;
        CellBox dims;
        dims.assign_value(0, 0, 0, 0, 0, 0);
        piece->get_footprint_size(&dims); // the piece's size in cells
        dims.x = static_cast<uint8_t>(entry->x + meta->cell_footprint_.x);
        dims.y = static_cast<int8_t>(entry->y + meta->cell_footprint_.y);
        dims.z = static_cast<uint8_t>(entry->z + meta->cell_footprint_.z);
        int z_end = dims.z + dims.l;
        for (int x = dims.x; x < dims.x + dims.w; ++x) {
            for (int z = dims.z; z < z_end; ++z) {
                FloorCell& cell = floor_[x][z];
                if (add) {
                    int top = dims.y + dims.h;
                    if (cell.level_y_ < top)
                        cell.level_y_ = top;
                    if (cell.level_y_ > 15)
                        cell.level_y_ = 15;
                    if (cell.level_y_ < -16)
                        cell.level_y_ = -16;
                } else {
                    // nothing left above the piece: the floor comes down to its bottom
                    CellBox above = dims;
                    above.y = static_cast<int8_t>(dims.h + dims.y);
                    above.stretch_vertically();
                    MapListHolder metas(get_intersecting_metas(above, 0, nullptr, true));
                    if (!metas.list && cell.level_y_ > dims.y)
                        cell.level_y_ = dims.y;
                }
                for (int level = dims.y; level < dims.h + dims.y; ++level) {
                    uint32_t bit = 1u << ((level + 0x11) & 31);
                    if (add)
                        cell.heights_used_ |= bit;
                    else
                        cell.heights_used_ &= ~bit;
                }
            }
        }
    }
}

} // namespace park_editing

using park_editing::ParkLayout;
