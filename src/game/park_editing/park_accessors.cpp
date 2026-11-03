// The park editor's park: accessors of the grid, the compressed map's header, the piece sets and the special
// pieces, and the floor-slide bookkeeping.

#include "game/park_editing/park.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/vec4.h"
#include "formats/park_map_format.h"
#include "game/park/park_memory.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"

namespace park_editing {

GapRegistry*& GapRegistry::sole()
{
    static GapRegistry* gap_registry{};
    return gap_registry;
}
ParkLayout*& ParkLayout::sole()
{
    static ParkLayout* park_layout{};
    return park_layout;
}

namespace {

constexpr std::uint32_t kDefault = 0x1CA1FF20u; // Default
constexpr std::size_t kShellNameSize = 40;
constexpr std::size_t kShellNameBytes = 12;   // "sk5ed_shell" with its terminator
constexpr int kFarOutHeight = 10000;          // beyond any floor height

// The size of a cell in world units (0x0064F41C, 0x0064F420, 0x0064F424: 120, 48, 120).
float cell_size_x() { return kParkCellSizeX; }
float cell_size_y() { return kParkCellSizeY; }
float cell_size_z() { return kParkCellSizeZ; }

// The grid's centre cell (world x and z are 0 at the corner of cell 29).
constexpr int kGridCentre = 29;

// strcpy (the original copies with no length limit).
void copy_text(std::span<char> to, const char* from)
{
    size_t at = 0;
    for (const char c : std::string_view(from))
        core::at(to, at++) = c;
    core::at(to, at) = '\0';
}

} // namespace

ParkLayout* ParkLayout::singleton() { return park_editing::ParkLayout::sole(); }

ParkMap* ParkLayout::get_packed_park_data(bool flag_clean)
{
    if (flag_clean)
        flag_bits_ &= ~kMapNotSavedLocal;
    return &map_;
}

void ParkLayout::set_packed_park_data(const ParkMap* map, bool flag_dirty)
{
    map_ = *map;
    flag_bits_ |= kMapInSync | kMapNewerThanPark;
    if (flag_dirty)
        flag_bits_ |= kMapNotSavedLocal;
}

void ParkLayout::set_packed_park_bytes(const uint8_t* bytes, bool flag_dirty)
{
    const std::span<const uint8_t, kParkMapBytes> received(bytes, kParkMapBytes);
    std::ranges::copy(received, map_.bytes().begin());
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
        return map_.shell_size();
    return 0;
}

const char* ParkLayout::get_park_name()
{
    if (map_.version() == kParkMapVersion)
        return map_.name().data();
    return nullptr;
}

void ParkLayout::set_park_name(const char* name)
{
    if (map_.version() != kParkMapVersion)
        return;
    copy_text(map_.name(), name);
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
    std::array<char, kShellNameSize> name{};
    if ((flag_bits_ & kMapInSync) && map_.shell_size() != 0)
        text::format_to(name, "sk5ed%d_shell", map_.shell_size() + 1); // 0x0064F650
    else
        std::memcpy(name.data(), "sk5ed_shell", kShellNameBytes); // 0x0064F644
    return checksums::hash_from_text(name.data());
}

MapListNode* ParkLayout::get_cell_chain(int x, int z) { return core::at(core::at(cell_metas_, x), z); }

PieceFamily* ParkLayout::get_piece_family(int index, int* used_before)
{
    int used = 0;
    for (int i = 0; i < index; ++i) {
        if (core::at(sets_, i).is_displayed_)
            ++used;
    }
    if (used_before)
        *used_before = used;
    return &core::at(sets_, index);
}

void ParkLayout::get_map_info(const ParkMap* map, MapInfoOut info)
{
    const auto num_gaps = info.num_gaps_;
    const auto num_metas = info.num_metas_;
    const auto theme = info.theme;
    const auto unknown18 = info.unknown18;
    const auto width = info.width;
    const auto length = info.length;
    *width = map->area_width();
    *length = map->area_length();
    *num_gaps = map->gap_count();
    *num_metas = map->piece_count();
    *theme = map->shell_size();
    *unknown18 = map->theme() ? map->theme() : kDefault; // Default
}

void ParkLayout::restore_floor_heights()
{
    for (auto& column : floor_) {
        for (auto& cell : column) {
            if (cell.touched_) {
                cell.touched_ = false;
                cell.level_y_ -= cell.delta_;
            }
            if (cell.level_y_ > kFloorLevelMax)
                cell.level_y_ = kFloorLevelMax;
            else if (cell.level_y_ < -kFloorLevelDepth)
                cell.level_y_ = -kFloorLevelDepth;
        }
    }
}

int ParkLayout::get_restart_index(uint32_t name)
{
    for (int i = 1; i <= kNumRestartPieces; ++i) {
        if (core::at(restart_pieces_, i - 1) == name)
            return i;
    }
    return 0;
}

PieceGroup* ParkLayout::fetch_meta_template(uint32_t name, int* index)
{
    int position = num_abstract_metas_ - 1;
    for (const MapListNode* node = abstract_metas_; node; node = node->following(), --position) {
        PieceGroup* meta = node->get_piece_group()->get_cast();
        if (meta->name_key() == name) {
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
    const int first = core::at(core::at(floor_, area.x), area.z).level_y_;
    int highest = -kFarOutHeight;
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            const int height = core::at(core::at(floor_, x), z).level_y_;
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
    return {.x = x, .y = y, .z = z, .w = 1.0f};
}

bool ParkLayout::is_area_blocked(CellBox area)
{
    for (uint32_t i = 0; i < num_blocked_areas_; ++i) {
        const CellBox& mask = core::at(blocked_areas_, i);
        if (area.x + area.w > mask.x && area.x < mask.x + mask.w && area.z + area.l > mask.z &&
            area.z < mask.z + mask.l)
            return true;
    }
    return false;
}

namespace {

// GapRegistry (0xA8 bytes): the instance in use (sSingleton, 0x0058CBB0), constructor, copying and destructor.
GapRegistry*& gap_manager_instance() { return park_editing::GapRegistry::sole(); }

} // namespace

void ParkLayout::create_gap_manager_copy()
{
    GapRegistry* current = gap_manager_instance();
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    void* memory = memory::alloc(sizeof(GapRegistry)); // (0xA8 bytes in the original)
    GapRegistry* copy = memory ? memory::construct_in<GapRegistry>(memory, nullptr) : nullptr;
    gap_manager_copy_ = copy;
    copy->copy_from(current); // (null if out of memory)
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
    for (const MapListNode* node = concrete_metas_; node; node = node->following()) {
        if (node->get_piece_group()->get_cast()->flag_bits() & PieceGroup::kHighlightable)
            node->get_piece_group()->get_cast()->spotlight(on, 1);
    }
    builder_->set_pieces_highlighted(0);
    if (on) {
        if (gap && gap->slot_index() >= 0)
            gap_registry_->change_gap_in_progress(gap->slot_index());
    } else {
        builder_->set_pieces_highlighted(0);
    }
}

void ParkLayout::clear_floor_change_marks()
{
    for (int x = shell_bounds_.x; x < shell_bounds_.x + shell_bounds_.w; ++x) {
        for (int z = shell_bounds_.z; z < shell_bounds_.z + shell_bounds_.l; ++z) {
            core::at(core::at(floor_, x), z).touched_ = false;
            core::at(core::at(floor_, x), z).delta_ = 0;
        }
    }
    for (const MapListNode* node = concrete_metas_; node; node = node->following())
        node->get_piece_group()->get_cast()->flag_bits() &= ~PieceGroup::kFlag20;
}

} // namespace park_editing

