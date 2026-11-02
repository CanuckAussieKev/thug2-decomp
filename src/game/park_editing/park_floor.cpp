// The park manager's floor (park_editing::ParkLayout): the risers under raised and lowered cells, the fence round the
// park, changing the floor's height, and the restart points put on a new park.

#include "game/park_editing/park.h"
#include "game/park_editing/park_editor.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/smtptr.h"
#include "engine/core/checked_index.h"

namespace park_editing {

namespace {

constexpr uint32_t kRiserFloor = 0xB0A98147;      // "riser_floor"
constexpr uint32_t kRiserFloorBelow = 0x14FD33E4; // "riser_floor_below"
constexpr uint32_t kRiserBelow = 0x4A0D22DB;      // "riser_below"
constexpr uint32_t kFence = 0x771994FF;           // "Sk4Ed_Fence_20x20"

// The park editor (a reference taken here, let go by release_park_editor): whether the park is full.
bool park_is_full() { return ParkBuilderMode::fabricate(false)->is_park_full() != 0; }
void release_park_editor() { ParkBuilderMode::get_release(); }

} // namespace

// 0x005993E0
void ParkLayout::generate_node_array()
{
    builder_->purge_auto_restart_points();
    for (int i = 0; i < 18; ++i) {
        // Round the middle of the park: player 1 and 2's restarts, four more, six of type 3, six of type 4.
        CellBox pos = CellBox::get_make(0, 0, 0, 0, 0, 0);
        pos = build_area_;
        pos.x = static_cast<uint8_t>(pos.x + (build_area_.w >> 1));
        pos.y = 0;
        pos.z = static_cast<uint8_t>(build_area_.z + (build_area_.l >> 1));
        pos.w = 1;
        pos.h = 1;
        pos.l = 1;
        int type = 1;
        int count = 1;
        if (i == 1) {
            pos.x = static_cast<uint8_t>(pos.x + 1);
            type = 2;
        } else if (i == 2) {
            pos.x = static_cast<uint8_t>(pos.x - 2);
            pos.z = static_cast<uint8_t>(pos.z + 2);
            type = 5;
        } else if (i == 3) {
            pos.z = static_cast<uint8_t>(pos.z + 2);
            type = 6;
        } else if (i == 4) {
            pos.x = static_cast<uint8_t>(pos.x + 2);
            pos.z = static_cast<uint8_t>(pos.z + 2);
            type = 7;
        } else if (i == 5) {
            pos.x = static_cast<uint8_t>(pos.x + 4);
            pos.z = static_cast<uint8_t>(pos.z + 2);
            type = 8;
        } else if (i >= 6 && i <= 11) {
            pos.x = static_cast<uint8_t>(pos.x + i - 9);
            pos.z = static_cast<uint8_t>(pos.z - 2);
            count = 6;
            type = 3;
        }
        bool flag = false;
        if (i >= 12 && i <= 17) {
            pos.x = static_cast<uint8_t>(pos.x + i - 15);
            pos.y = 0;
            pos.z = static_cast<uint8_t>(pos.z - 3);
            flag = true;
            count = 6;
            type = 4;
        }
        if (!builder_->needs_restarts(type, count))
            continue;

        // On the floor (or the metapiece standing there), then down onto whatever is below a feeler's line.
        pos.y = static_cast<int8_t>(get_ground_level(pos, nullptr));
        {
            MapListHolder metas(get_intersecting_metas(pos, 0, nullptr, 0));
            if (metas.list)
                pos.y = static_cast<int8_t>(metas.list->get_placed_piece()->cell_footprint_.h + pos.y);
        }
        maths::Vec4 point;
        point.x = static_cast<float>((pos.x - 29) * static_cast<double>(kGridPitch) + kGridPitch * 0.5);
        point.y = static_cast<float>(pos.y * static_cast<double>(kGridStepUp));
        point.z = static_cast<float>((pos.z - 29) * static_cast<double>(kGridStepAlong) + kGridStepAlong * 0.5);
        point.w = 1.0f;
        render::ProbeRay feeler;
        const maths::Vec4 start = {point.x, static_cast<float>(static_cast<double>(point.y) + 10000.0), point.z,
                                   static_cast<float>(static_cast<double>(point.w) + 1.0)};
        feeler.change_origin(&start);
        const maths::Vec4 end = {point.x, static_cast<float>(static_cast<double>(point.y) - 1000.0), point.z,
                                 static_cast<float>(static_cast<double>(point.w) + 1.0)};
        feeler.change_finish(&end);
        if (feeler.get_hit_shape(true, false)) {
            if (i >= 2 && i <= 5)
                point.y = feeler.contact_pos_.y;
            else
                point.y = static_cast<float>(static_cast<double>(feeler.contact_pos_.y) + kGridStepUp);
        }
        builder_->plant_restart_point(&point, 0, &pos, type, 1, flag);
        // (the feeler's destructor lets go of what it hit)
    }
    builder_->build_node_array(concrete_metas_);
}

// 0x00599CD0
bool ParkLayout::can_change_floor_here(CellBox area, bool uneven)
{
    bool ok = false;
    if (!park_is_full()) {
        const MapListHolder metas(collect_pieces_in_region(area));
        bool clear = true;
        for (MapListNode* node = metas.list; node; node = node->following_) {
            if (!(node->get_placed_piece()->flag_bits_ & PieceGroup::kBuiltShell)) {
                clear = false;
                break;
            }
        }
        if (clear) {
            // Without `uneven`, the floor must be flat at the box's height.
            bool level = true;
            if (!uneven) {
                bool flat = false;
                level = get_ground_level(area, &flat) == area.y && flat;
            }
            if (level && !is_area_blocked(area))
                ok = true;
        }
    }
    release_park_editor();
    return ok;
}

// 0x00599E20
void ParkLayout::destroy_metas_below(CellBox area)
{
    CellBox below = area;
    below.y = static_cast<int8_t>(area.y - 1);
    below.h = 1;
    const uint32_t destroyed = free_piece_groups_in_region(below, 4);
    riser_metas_ += static_cast<int>(destroyed >> 16);
    riser_pieces_ += static_cast<int>(destroyed & 0xFFFF);
}

// 0x0059B280
void ParkLayout::undo_floor_change()
{
    restore_floor_heights();
    for (MapListNode* node = riser_list_; node; node = node->following_)
        node->piece_group_ = shift_placed_piece(node->get_placed_piece(), 0, -node->dy_, 0);
}

// 0x0059B2C0
void ParkLayout::move_riser_list(MapListNode* list)
{
    for (MapListNode* node = list; node; node = node->following_) {
        PieceGroup* meta = node->get_placed_piece();
        if (node->dy_ < 0)
            destroy_metas_below(meta->cell_footprint_);
        node->piece_group_ = shift_placed_piece(meta, 0, node->dy_, 0);
    }
}

// 0x0059B320
void ParkLayout::rebuild_floor_piece(FloorPieceRequest* request)
{
    const int x = request->x;
    const int z = request->z;
    // (the floor's rows one after the other: the original indexed them as one run)
    const std::span<FloorCell> cells(&core::at(core::at(floor_, 0), 0), size_t{kFloorSize} * kFloorSize);
    auto height_at = [&](int cx, int cz) { return core::at(cells, cx * kFloorSize + cz).level_y_; };

    // The lowest neighbour (outside the shell counts as 0).
    int lowest = 10000;
    if (std::cmp_greater(x, shell_bounds_.x)) {
        if (height_at(x - 1, z) < lowest)
            lowest = height_at(x - 1, z);
    } else {
        lowest = 0;
    }
    auto at_most_zero = [&] {
        if (lowest > 0)
            lowest = 0;
    };
    if (x < shell_bounds_.w + shell_bounds_.x - 1) {
        if (height_at(x + 1, z) < lowest)
            lowest = height_at(x + 1, z);
    } else {
        at_most_zero();
    }
    if (std::cmp_greater(z, shell_bounds_.z)) {
        if (height_at(x, z - 1) < lowest)
            lowest = height_at(x, z - 1);
    } else {
        at_most_zero();
    }
    if (z < shell_bounds_.l + shell_bounds_.z - 1) {
        if (height_at(x, z + 1) < lowest)
            lowest = height_at(x, z + 1);
    } else {
        at_most_zero();
    }

    // The piece on top: a riser of the cell's height (the floor itself at 0, a dug-in floor below 0), and the
    // riser_below wall where a neighbour is lower than this cell's floor.
    uint32_t top = 0;
    uint32_t below = 0;
    int below_y = -15;
    const int height = request->height;
    bool in_park = false;
    if (height > 0) {
        std::array<char, 128> name{};
        std::snprintf(name.data(), sizeof(name), "riser_above%d", request->index);
        top = checksums::hash_from_text(name.data());
        request->y = 0;
        in_park = lowest >= 0;
        if (!in_park)
            below = kRiserBelow;
    } else if (height == 0) {
        top = kRiserFloor;
        request->y = -1;
        in_park = lowest >= 0;
        if (!in_park) {
            top = 0;
            below = kRiserBelow;
        }
    } else {
        top = kRiserFloorBelow;
        request->y = height;
        if (lowest < height) {
            below_y = height - 15;
            top = 0;
            below = kRiserBelow;
        }
        in_park = lowest >= 0;
    }
    // (outside the park's bounds, no top piece unless the floor is dug in)
    if (in_park) {
        if (std::cmp_less(x, build_area_.x) || x >= build_area_.x + build_area_.w)
            top = 0;
        if (std::cmp_less(z, build_area_.z) || z >= build_area_.z + build_area_.l)
            top = 0;
    }

    // Keep what's there already, delete the rest of the inner shell in the cell.
    const CellBox cell = CellBox::get_make(static_cast<uint8_t>(request->x), static_cast<int8_t>(request->y),
                                           static_cast<uint8_t>(request->z), 1, 1, 1);
    const MapListHolder metas(collect_pieces_in_region(cell));
    for (MapListNode* node = metas.list; node;) {
        PieceGroup* meta = node->get_placed_piece();
        node = node->following_;
        if (!(meta->flag_bits_ & PieceGroup::kBuiltShell))
            continue;
        if (top && meta->name_key_ == top && meta->cell_footprint_.y == request->y) {
            top = 0;
        } else if (below && meta->name_key_ == below &&
                   (static_cast<uint8_t>(meta->cell_footprint_.y) == static_cast<uint8_t>(request->y) ||
                    meta->cell_footprint_.y == static_cast<int8_t>(below_y))) {
            below = 0;
        } else {
            free_placed_piece(meta, 0);
            builder_->clean_up_out_refs();
        }
    }
    if (top) {
        PieceGroup* meta = make_placed_piece(find_piece_template(top), 0);
        place_meta_piece_in_park(CellBox::get_make(static_cast<uint8_t>(request->x), static_cast<int8_t>(request->y),
                                                   static_cast<uint8_t>(request->z), 0, 0, 0),
                                 meta);
    }
    if (below) {
        PieceGroup* meta = make_placed_piece(find_piece_template(below), 0);
        if (std::cmp_less(request->x, build_area_.x) || request->x >= build_area_.x + build_area_.w ||
            std::cmp_less(request->z, build_area_.z) || request->z >= build_area_.z + build_area_.l)
            meta->flag_bits_ |= PieceGroup::kRiserBelow | PieceGroup::kFlag400;
        place_meta_piece_in_park(CellBox::get_make(static_cast<uint8_t>(request->x), static_cast<int8_t>(below_y),
                                                   static_cast<uint8_t>(request->z), 0, 0, 0),
                                 meta);
    }
}

// 0x0059BED0
void ParkLayout::rebuild_inner_shell(CellBox bounds)
{
    build_area_ = bounds;
    const CellBox& park = build_area_;
    // The four strips round the park (where the walls go).
    core::at(blocked_areas_, 0).x = static_cast<uint8_t>(park.x - 1);
    core::at(blocked_areas_, 0).z = static_cast<uint8_t>(park.z - 1);
    core::at(blocked_areas_, 0).w = 1;
    core::at(blocked_areas_, 0).l = static_cast<uint8_t>(park.l + 2);
    core::at(blocked_areas_, 1).x = static_cast<uint8_t>(park.w + park.x);
    core::at(blocked_areas_, 1).z = static_cast<uint8_t>(park.z - 1);
    core::at(blocked_areas_, 1).w = 1;
    core::at(blocked_areas_, 1).l = static_cast<uint8_t>(park.l + 2);
    core::at(blocked_areas_, 2).x = static_cast<uint8_t>(park.x - 1);
    core::at(blocked_areas_, 2).z = static_cast<uint8_t>(park.z - 1);
    core::at(blocked_areas_, 2).w = static_cast<uint8_t>(park.w + 2);
    core::at(blocked_areas_, 2).l = 1;
    core::at(blocked_areas_, 3).x = static_cast<uint8_t>(park.x - 1);
    core::at(blocked_areas_, 3).z = static_cast<uint8_t>(park.l + park.z);
    core::at(blocked_areas_, 3).w = static_cast<uint8_t>(park.w + 2);
    core::at(blocked_areas_, 3).l = 1;

    // Out go the fences and the player's metapieces no longer (wholly) in the park.
    for (MapListNode* node = concrete_metas_; node;) {
        MapListNode* next = node->following_;
        const CellBox area = node->get_placed_piece()->cell_footprint_;
        const uint32_t flags = node->get_placed_piece()->flag_bits_;
        bool destroy = false;
        if (flags & PieceGroup::kFence)
            destroy = true;
        else if ((flags & PieceGroup::kBuiltShell) || !(flags & PieceGroup::kInPark))
            destroy = false;
        else
            destroy = area.x < park.x || area.z < park.z || area.w + area.x > park.w + park.x ||
                      area.l + area.z > park.l + park.z;
        if (destroy)
            free_placed_piece(node->get_placed_piece(), 0);
        node = next;
    }
    builder_->clean_up_out_refs();
    mark_progress("RebuildInnerShell-After any inner shell clipping");

    // The fence: two-cell pieces along each side, facing in, wherever the strip isn't blocked.
    PieceGroup* fence = find_piece_template(kFence);
    auto add_fence = [&](CellBox test, CellBox at, int rot) {
        if (is_area_blocked(test))
            return;
        PieceGroup* meta = make_placed_piece(fence, 0);
        meta->set_quarter_turns(rot);
        meta->flag_bits_ |= PieceGroup::kFence;
        place_meta_piece_in_park(at, meta);
    };
    for (int x = park.x; x < park.w + park.x; x += 2) {
        auto px = static_cast<uint8_t>(x);
        add_fence(CellBox::get_make(px, 0, park.z, 2, 1, 1),
                  CellBox::get_make(px, 0, static_cast<uint8_t>(park.z - 1), 0, 0, 0), 0);
        auto far = static_cast<uint8_t>(park.z + park.l);
        add_fence(CellBox::get_make(px, 0, static_cast<uint8_t>(far - 1), 2, 1, 1),
                  CellBox::get_make(px, 0, far, 0, 0, 0), 2);
    }
    for (int z = park.z; z < park.l + park.z; z += 2) {
        auto pz = static_cast<uint8_t>(z);
        add_fence(CellBox::get_make(park.x, 0, pz, 1, 1, 2),
                  CellBox::get_make(static_cast<uint8_t>(park.x - 1), 0, pz, 0, 0, 0), 1);
        auto far = static_cast<uint8_t>(park.w + park.x);
        add_fence(CellBox::get_make(static_cast<uint8_t>(far - 1), 0, pz, 1, 1, 2),
                  CellBox::get_make(far, 0, pz, 0, 0, 0), 3);
    }
    mark_progress("RebuildInnerShell-After adding fence pieces");

    // No floor height outside the park (the first 56 x 56 cells).
    for (int x = 0; x < 56; ++x) {
        for (int z = 0; z < 56; ++z) {
            if (std::cmp_less(x, park.x) || std::cmp_less(z, park.z) || x >= park.w + park.x || z >= park.l + park.z)
                core::at(core::at(floor_, x), z).level_y_ = 0;
        }
    }
    builder_->update_bounds((flag_bits_ & kMapLoaded) ? map_.shell_size() : 0);
    mark_progress("RebuildInnerShell-End");
}

// 0x0059C520
bool ParkLayout::raise_or_lower_floor(CellBox area, int y, int target, bool raise, bool level)
{
    uint32_t mode = raise ? 6 : 4;
    if (level)
        mode |= 1;
    clear_floor_change_marks();
    if (riser_list_)
        MapListNode::free_node_chain(riser_list_);
    riser_list_ = nullptr;
    riser_metas_ = 0;
    riser_pieces_ = 0;
    if (level && has_floor_locked_meta(area))
        return false;
    if (!build_riser_list(area, y, target, mode, &riser_list_)) {
        restore_floor_heights();
        return false;
    }
    move_riser_list(riser_list_);
    if (!raise)
        destroy_metas_below(area);
    return true;
}

// 0x0059C600
bool ParkLayout::regenerate_ground(const CellBox* area, int /*unused*/)
{
    if (!area) {
        // The inner shell's pieces outside the park go first.
        for (MapListNode* node = concrete_metas_; node; node = node->following_) {
            const PieceGroup* meta = node->get_piece_group();
            if (!(meta->flag_bits_ & PieceGroup::kBuiltShell))
                continue;
            const CellBox d = meta->cell_footprint_;
            if (d.x >= build_area_.x && d.z >= build_area_.z && d.w + d.x <= build_area_.w + build_area_.x &&
                d.l + d.z <= build_area_.l + build_area_.z)
                continue;
            free_placed_piece(node->get_placed_piece(), 0);
        }
        builder_->clean_up_out_refs();
        area = &build_area_;
    }
    // Every cell of the area and the ring round it.
    const std::span<const FloorCell> cells(&core::at(core::at(floor_, 0), 0), size_t{kFloorSize} * kFloorSize);
    for (int x = area->x - 1; x < area->w + 1 + area->x; ++x) {
        for (int z = area->z - 1; z < area->l + 1 + area->z; ++z) {
            FloorPieceRequest request{};
            request.height = core::at(cells, x * kFloorSize + z).level_y_;
            request.index = request.height - 1;
            request.x = x;
            request.z = z;
            rebuild_floor_piece(&request);
        }
    }
    mark_progress("generate_floor_pieces");
    return true;
}

} // namespace park_editing
