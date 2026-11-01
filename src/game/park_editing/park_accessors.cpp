// The park editor's park: accessors of the grid, the compressed map's header, the piece sets and the special
// pieces, and the floor-slide bookkeeping.

#include "game/park_editing/park.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "game/park/park_memory.h"

namespace park_editing {

float g_park_cell_size_x{120.0f};
float g_park_cell_size_y{48.0f};
float g_park_cell_size_z{120.0f};
GapRegistry* g_gap_registry;
ParkLayout* g_park_layout;

namespace {

// The size of a cell in world units (0x0064F41C, 0x0064F420, 0x0064F424: 120, 48, 120).
float cell_size_x() { return g_park_cell_size_x; }
float cell_size_y() { return g_park_cell_size_y; }
float cell_size_z() { return g_park_cell_size_z; }

// The grid's centre cell (world x and z are 0 at the corner of cell 29).
constexpr int kGridCentre = 29;

// strcpy (the original copies with no length limit).
void copy_text(char* to, const char* from)
{
    while ((*to++ = *from++) != '\0') {
    }
}

} // namespace

ParkLayout* ParkLayout::singleton() { return g_park_layout; }

CompressedMap* ParkLayout::get_packed_park_data(bool flag_clean)
{
    if (flag_clean)
        flag_bits_ &= ~kMapNotSavedLocal;
    return &map_;
}

void ParkLayout::set_packed_park_data(const CompressedMap* map, bool flag_dirty)
{
    std::memcpy(&map_, map, sizeof(map_));
    flag_bits_ |= kMapInSync | kMapNewerThanPark;
    if (flag_dirty)
        flag_bits_ |= kMapNotSavedLocal;
}

bool ParkLayout::has_local_park_copy()
{
    return (flag_bits_ & (kMapValid | kMapNewerThanPark)) && !(flag_bits_ & kMapNotSavedLocal);
}

int ParkLayout::get_shell_size()
{
    if (flag_bits_ & kMapInSync)
        return map_.shell_size_;
    return 0;
}

const char* ParkLayout::get_park_name()
{
    if (map_.rev_ == CompressedMap::kVersion)
        return map_.label_;
    return nullptr;
}

void ParkLayout::set_park_name(const char* name)
{
    if (map_.rev_ != CompressedMap::kVersion)
        return;
    copy_text(map_.label_, name);
    copy_text(park_name_, name);
}

void ParkLayout::rebuild_park_geometry(int how)
{
    // ParkAssembler's three updates
    // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
    switch (how) {
    case 0:
        builder_->clean_up_out_refs(); // take out the removed pieces
        break;
    case 1:
        builder_->rebuild_all();
        break;
    case 2:
        builder_->rebuild_marked();
        break;
    }
}

uint32_t ParkLayout::get_shell_checksum()
{
    char name[40];
    if ((flag_bits_ & kMapInSync) && map_.shell_size_ != 0)
        std::snprintf(name, sizeof(name), "sk5ed%d_shell", map_.shell_size_ + 1); // 0x0064F650
    else
        std::memcpy(name, "sk5ed_shell", 12); // 0x0064F644
    return checksums::hash_from_text(name);
}

MapListNode* ParkLayout::get_cell_chain(int x, int z) { return cell_metas_[x][z]; }

PieceFamily* ParkLayout::get_piece_family(int index, int* used_before)
{
    int used = 0;
    for (int i = 0; i < index; ++i) {
        if (sets_[i].is_displayed_)
            ++used;
    }
    if (used_before)
        *used_before = used;
    return &sets_[index];
}

void ParkLayout::get_map_info(const CompressedMap* map, int* num_gaps, int* num_metas, uint16_t* theme,
                              uint32_t* unknown18, int* width, int* length)
{
    *width = map->w_;
    *length = map->l_;
    *num_gaps = map->num_gaps_;
    *num_metas = map->composite_count_;
    *theme = map->shell_size_;
    *unknown18 = map->style_set_ ? map->style_set_ : 0x1CA1FF20; // Default
}

void ParkLayout::restore_floor_heights()
{
    for (auto& column : floor_) {
        for (auto& cell : column) {
            if (cell.touched_) {
                cell.touched_ = false;
                cell.level_y_ -= cell.delta_;
            }
            if (cell.level_y_ > 15)
                cell.level_y_ = 15;
            else if (cell.level_y_ < -16)
                cell.level_y_ = -16;
        }
    }
}

int ParkLayout::get_restart_index(uint32_t name)
{
    for (int i = 1; i <= kNumRestartPieces; ++i) {
        if (restart_pieces_[i - 1] == name)
            return i;
    }
    return 0;
}

PieceGroup* ParkLayout::fetch_meta_template(uint32_t name, int* index)
{
    int position = num_abstract_metas_ - 1;
    for (const MapListNode* node = abstract_metas_; node; node = node->following_, --position) {
        PieceGroup* meta = node->get_piece_group()->get_cast();
        if (meta->name_key_ == name) {
            if (index)
                *index = position;
            return meta;
        }
    }
    return nullptr;
}

bool ParkLayout::can_resize_to(CellBox area)
{
    // 0x0059EB30: the park memory check for going from one floor area to another
    return pieces_fit(build_area_.w * build_area_.l, area.w * area.l);
}

int ParkLayout::get_ground_level(CellBox area, bool* uniform)
{
    if (uniform)
        *uniform = true;
    const int first = floor_[area.x][area.z].level_y_;
    int highest = -10000;
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            const int height = floor_[x][z].level_y_;
            if (uniform && height != first)
                *uniform = false;
            if (height > highest)
                highest = height;
        }
    }
    return highest;
}

maths::Vec4 ParkLayout::cell_to_world_pos(const CellBox& area, maths::Vec4* dimensions)
{
    const float x = static_cast<float>(area.x - kGridCentre) * cell_size_x();
    const float y = static_cast<float>(area.y) * cell_size_y();
    const float z = static_cast<float>(area.z - kGridCentre) * cell_size_z();
    if (dimensions) {
        dimensions->x = static_cast<float>(area.w) * cell_size_x();
        dimensions->y = static_cast<float>(area.h) * cell_size_y();
        dimensions->z = static_cast<float>(area.l) * cell_size_z();
    }
    return {x, y, z, 1.0f};
}

bool ParkLayout::is_area_blocked(CellBox area)
{
    for (uint32_t i = 0; i < num_blocked_areas_; ++i) {
        const CellBox& mask = blocked_areas_[i];
        if (area.x + area.w > mask.x && area.x < mask.x + mask.w && area.z + area.l > mask.z &&
            area.z < mask.z + mask.l)
            return true;
    }
    return false;
}

namespace {

// GapRegistry (0xA8 bytes): the instance in use (sSingleton, 0x0058CBB0), constructor, copying and destructor.
void*& gap_manager_instance() { return reinterpret_cast<void*&>(g_gap_registry); }

} // namespace

void ParkLayout::create_gap_manager_copy()
{
    void* current = gap_manager_instance();
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
    void* memory = memory::alloc(0xA8);
    GapRegistry* copy = memory ? memory::construct_in<GapRegistry>(memory, nullptr) : nullptr;
    gap_manager_copy_ = copy;
    copy->copy_from(static_cast<GapRegistry*>(current)); // (null if out of memory)
    memory::leave_heap_scope();
    gap_manager_instance() = current; // (the constructor made the copy current)
}

void ParkLayout::use_gap_manager_copy() { gap_manager_instance() = gap_manager_copy_; }

void ParkLayout::destroy_gap_manager_copy()
{
    GapRegistry* copy = gap_manager_copy_;
    if (!copy)
        return;
    copy->~GapRegistry();
    memory::Hub::get_pull()->dealloc(copy);
    gap_manager_copy_ = nullptr;
}

void ParkLayout::highlight_gap_pieces(bool on, GapDetails* gap)
{
    for (const MapListNode* node = concrete_metas_; node; node = node->following_) {
        if (node->get_piece_group()->get_cast()->flag_bits_ & PieceGroup::kHighlightable)
            node->get_piece_group()->get_cast()->spotlight(on, 1);
    }
    builder_->set_pieces_highlighted(0);
    if (on) {
        if (gap && gap->slot_index_ >= 0)
            gap_registry_->change_gap_in_progress(gap->slot_index_);
    } else {
        builder_->set_pieces_highlighted(0);
    }
}

void ParkLayout::clear_floor_change_marks()
{
    for (int x = shell_bounds_.x; x < shell_bounds_.x + shell_bounds_.w; ++x) {
        for (int z = shell_bounds_.z; z < shell_bounds_.z + shell_bounds_.l; ++z) {
            floor_[x][z].touched_ = false;
            floor_[x][z].delta_ = 0;
        }
    }
    for (const MapListNode* node = concrete_metas_; node; node = node->following_)
        node->get_piece_group()->get_cast()->flag_bits_ &= ~PieceGroup::kFlag20;
}

} // namespace park_editing

