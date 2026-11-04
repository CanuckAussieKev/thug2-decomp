// The park editor's metapieces (park_editing::PieceGroup and its two kinds): a list of descriptors, each a piece and its cell
// offset and turn within the metapiece. An abstract metapiece (a palette entry) names its pieces (source pieces or
// other abstract metapieces); a concrete one (placed in the park or held by the cursor) holds cloned pieces, which it
// moves and turns with itself.

#include <algorithm>
#include <cstdint>
#include <utility>
#include <array>
#include <span>

#include "game/park_editing/park.h"
#include "platform/d3dx.h"

#include "engine/core/mem.h"
#include "engine/maths/vec4.h"
#include "engine/script/cell.h"
#include "game/park_editing/park_nx.h"
#include "engine/maths/mat4.h"
#include "engine/script/param_struct.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"
#include <gsl/pointers>

namespace park_editing {

namespace {

constexpr std::uint32_t kSk3EdRS1p = 0xDC09A6A4U;       // Sk3Ed_RS_1p
constexpr std::uint32_t kSk3EdRSMp = 0xCA07B4FCU;       // Sk3Ed_RS_Mp
constexpr std::uint32_t kSk3EdRsHo = 0x3A784D4CU;       // Sk3Ed_Rs_Ho
constexpr std::uint32_t kSk3EdRsKOTH = 0x4E2EFD2AU;     // Sk3Ed_Rs_KOTH
constexpr std::uint32_t kSk4EdTeamBlue = 0x8A1576B6U;   // Sk4Ed_Team_Blue
constexpr std::uint32_t kSk4EdTeamGreen = 0xEC82433CU;  // Sk4Ed_Team_Green
constexpr std::uint32_t kSk4EdTeamRed = 0xD7EB62B5U;    // Sk4Ed_Team_Red
constexpr std::uint32_t kSk4EdTeamYellow = 0x613C7766U; // Sk4Ed_Team_Yellow
constexpr double kHalf = 0.5;
constexpr double kQuarterTurnDegrees = 90.0;

// Degrees to radians, as the exe has it (0x00645464).
constexpr float kRadiansPerDegree = 0.0174532924F;
// A little slack before a size is cut down to whole cells (0x00645448).
constexpr double kCellSlack = 2.0;
// The bounds' starting values (0x47C35000): far outside the park.
constexpr float kFarAway = 100000.0F;

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library's): out = a * b.
void multiply_matrices(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b)
{
    d3dx::matrix_multiply(out, a, b);
}

uint8_t get_cells(double size, float cell)
{
    return static_cast<uint8_t>(static_cast<int>((size + kCellSlack) / cell));
}

} // namespace

// --- PieceSlots -------------------------------------------------------------------------------------------------

template <typename Piece>
memory::HeapPtr<typename PieceSlots<Piece>::Slot> PieceSlots<Piece>::allocate(uint32_t count)
{
    return memory::heap_ptr<Slot>(memory::carve(count * sizeof(Slot), 1, nullptr));
}

template <typename Piece>
void PieceSlots<Piece>::commission(int count)
{
    memory::HeapPtr<Slot> extra;
    if (count > 2) {
        extra = allocate(static_cast<uint32_t>(count - 2));
        // Growing: the extra entries kept.
        if (extra && extra_ && limit_ <= ints::as_unsigned(count) && limit_ > 2) {
            const std::span<Slot> to(extra.get(), static_cast<uint32_t>(count - 2));
            const std::span<Slot> from = this->extra();
            for (uint32_t i = 2; i < limit_; ++i) {
                core::at(to, i - 2) = core::at(from, i - 2);
            }
        }
    }
    extra_ = std::move(extra); // (the old array freed)
    limit_ = static_cast<uint32_t>(count);
}

template <typename Piece>
template <typename SizeOf>
void PieceSlots<Piece>::turn(SlotTurn slot_turn, const CellBox& area, SizeOf size_of)
{
    const auto filled = slot_turn.filled;
    const auto turn = slot_turn.turn;
    // Every descriptor turned about the metapiece's corner, into new storage.
    memory::HeapPtr<Slot> extra = extra_ ? allocate(limit_ - 2) : nullptr;
    const std::span<Slot> to_extra(extra.get(), extra ? limit_ - 2 : 0);
    // (the original leaves the unused ones of these two as stack leftovers; here they keep what they were)
    std::array<Slot, 2> first = first_;
    for (uint32_t i = 0; i < filled; ++i) {
        Slot* to = i < 2 ? &core::at(first, i) : &core::at(to_extra, i - 2);
        const Slot* from = &at(static_cast<int>(i));
        *to = *from;
        // The entry's own size in cells, as it lies (x, z).
        const auto [across, along] = size_of(*from);
        switch (turn) {
        case 0:
            to->x = from->x;
            to->z = from->z;
            break;
        case 1:
            to->x = from->z;
            to->z = static_cast<uint8_t>(area.l - from->x - across);
            break;
        case 2:
            to->x = static_cast<uint8_t>(area.w - from->x - across);
            to->z = static_cast<uint8_t>(area.l - from->z - along);
            break;
        case 3:
            to->x = static_cast<uint8_t>(area.w - from->z - along);
            to->z = from->x;
            break;
        default:
            break;
        }
        to->y = from->y;
        to->rot = static_cast<uint8_t>((from->rot + turn) & 3);
        to->piece = from->piece;
    }
    extra_ = std::move(extra); // (the old array freed)
    first_ = first;
}

template class PieceSlots<ParkTile*>;
template class PieceSlots<uint32_t>;

// --- PieceGroup ---------------------------------------------------------------------------------------------------

bool PieceGroup::is_spawn_marker() const
{
    switch (name_key_) {
    case kSk3EdRSMp:
    case kSk4EdTeamYellow:
    case kSk3EdRsHo:
    case kSk3EdRsKOTH:
    case kSk4EdTeamBlue:
    case kSk4EdTeamRed:
    case kSk3EdRS1p:
    case kSk4EdTeamGreen:
        return true;
    default:
        return false;
    }
}

ParkTile* PieceGroup::placed_piece(uint32_t index)
{
    return core::down_cast<PlacedPieceGroup>(this)->slot(static_cast<int>(index)).piece;
}

uint32_t PieceGroup::piece_name(uint32_t index)
{
    return core::down_cast<PieceBlueprint>(this)->slot(static_cast<int>(index)).piece;
}

void PieceGroup::toggle_shown(bool visible)
{
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        auto* piece = placed_piece(i);
        scene::change_live(scene::look_up_sector(piece->sector()), visible);
    }
}

int PieceGroup::count_pieces()
{
    int count = 0;
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        const uint32_t name = piece_name(i);
        const PieceGroup* entry = ParkLayout::singleton()->fetch_meta_template(name, nullptr);
        if ((entry->flag_bits_ & kSector) != 0U) { // a single piece
            ++count;
        } else {
            count += static_cast<int>(entry->filled_slots_);
        }
    }
    return count;
}

bool PieceGroup::fits_inside_region(CellBox* area)
{
    const CellBox& cells = cell_footprint_;
    if (cells.x < area->x || cells.x + cells.w > area->x + area->w) {
        return false;
    }
    if (cells.z < area->z || cells.z + cells.l > area->z + area->l) {
        return false;
    }
    return true;
}

void PieceGroup::set_quarter_turns(int rot)
{
    const int turn = (rot - quarter_turns_) & 3;
    quarter_turns_ = rot;
    if ((turn & 1) != 0) { // a quarter turn: width and length swap
        const uint8_t w = cell_footprint_.w;
        cell_footprint_.w = cell_footprint_.l;
        cell_footprint_.l = w;
    }
    turn_descriptors(turn);
}

maths::Vec4* PieceGroup::get_relative_position(maths::Vec4* out, uint32_t index)
{
    // The metapiece's half size (x and z).
    const auto half_w = static_cast<float>(static_cast<double>(cell_footprint_.w) * kGridPitch * 0.5);
    const auto half_l = static_cast<float>(static_cast<double>(cell_footprint_.l) * kGridStepAlong * 0.5);
    const PiecePlace* entry = get_descriptor(static_cast<int>(index));
    ParkPiece* piece = get_piece(index);
    maths::Vec4 size1;
    maths::Vec4 size2;
    const float size_z = piece->get_dims_turned(&size1, entry->rot)->z;
    const float size_x = piece->get_dims_turned(&size2, entry->rot)->x;
    // The piece's centre in the metapiece, from the metapiece's centre.
    const double x = (static_cast<double>(entry->x) * kGridPitch) + (static_cast<double>(size_x) * 0.5);
    const double y = static_cast<double>(static_cast<uint8_t>(entry->y)) * kGridStepUp; // (read unsigned)
    const auto z =
        static_cast<float>((static_cast<double>(entry->z) * kGridStepAlong) + (static_cast<double>(size_z) * 0.5));
    out->x = static_cast<float>(x - half_w);
    out->y = static_cast<float>(y);
    out->z = static_cast<float>(static_cast<double>(z) - half_l);
    out->w = 1.0F; // (1 less a stack leftover in the original)
    return out;
}

void PieceGroup::get_bounds(maths::Vec4* max, maths::Vec4* min)
{
    *min = {.x = kFarAway, .y = kFarAway, .z = kFarAway, .w = 0.0F};
    *max = {.x = -kFarAway, .y = -kFarAway, .z = -kFarAway, .w = 0.0F};
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        auto* piece = placed_piece(i);
        maths::Vec4 size;
        piece->measure_span(&size);
        const double x = piece->at_point().x;
        const float y = piece->at_point().y;
        const double z = piece->at_point().z;
        const double half_x = static_cast<double>(size.x) * 0.5;
        if (x - half_x < min->x) {
            min->x = static_cast<float>(x - half_x);
        }
        if (half_x + x > max->x) {
            max->x = static_cast<float>(half_x + x);
        }
        const double half_z = static_cast<double>(size.z) * 0.5;
        if (z - half_z < min->z) {
            min->z = static_cast<float>(z - half_z);
        }
        if (half_z + z > max->z) {
            max->z = static_cast<float>(half_z + z);
        }
        min->y = std::min(min->y, y);
        if (static_cast<double>(size.y) + y > max->y) {
            max->y = static_cast<float>(static_cast<double>(size.y) + y);
        }
    }
}

bool PieceGroup::intersects(CellBox area, uint32_t precise)
{
    const CellBox& cells = cell_footprint_;
    const int x_start = area.x;
    const int x_end = area.x + area.w;
    const int y_start = int{area.y};
    const int y_end = area.y + area.h;
    const int z_start = area.z;
    const int z_end = area.z + area.l;
    if (cells.x + cells.w <= x_start || std::cmp_greater_equal(cells.x, x_end)) {
        return false;
    }
    if (cells.y + cells.h <= y_start || cells.y >= y_end) {
        return false;
    }
    if (cells.z + cells.l <= z_start || std::cmp_greater_equal(cells.z, z_end)) {
        return false;
    }
    if (static_cast<uint8_t>(precise) == 0U) {
        return true;
    }
    // Precisely (when any piece is flagged setsFloor): by the cells of the pieces that are NOT flagged.
    bool any_sets_floor = false;
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        if (get_descriptor(static_cast<int>(i))->sets_floor_) {
            any_sets_floor = true;
        }
    }
    if (!any_sets_floor) {
        return true;
    }
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        const PiecePlace* entry = get_descriptor(static_cast<int>(i));
        CellBox size{};
        size.w = 0;
        size.h = 0;
        size.l = 0;
        get_piece(i)->get_footprint_size(&size);
        const auto x = static_cast<uint8_t>(entry->x + cells.x);
        const auto y = static_cast<int8_t>(entry->y + cells.y);
        const auto z = static_cast<uint8_t>(entry->z + cells.z);
        if (entry->sets_floor_) {
            continue;
        }
        if (x + size.w > x_start && y + size.h > y_start && z + size.l > z_start && std::cmp_less(x, x_end) &&
            y < y_end && std::cmp_less(z, z_end)) {
            return true;
        }
    }
    return false;
}

void PieceGroup::get_piece_list(script::Array* out)
{
    out->configure_size_kind(filled_slots_, script::QbValueKind::kStruct);
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        gsl::owner<script::Struct*> memory = script::spawn_param_bag();
        script::Struct* piece = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
        piece->put_name_key(checksums::Name{0U}, piece_name(i)); // (out of memory crashes, as in the original)
        maths::Vec4 position;
        get_relative_position(&position, i);
        piece->put_vec3(checksums::Name{0U}, {.x = position.x, .y = position.y, .z = position.z});
        out->set_cell(i, script::Cell::of(piece));
    }
}

void PieceGroup::change_place(maths::Vec4 position, float angle)
{
    const auto half_w = static_cast<float>(static_cast<double>(cell_footprint_.w) * kGridPitch * 0.5);
    const auto half_l = static_cast<float>(static_cast<double>(cell_footprint_.l) * kGridStepAlong * 0.5);
    if (filled_slots_ == 0U) {
        return;
    }
    const auto radians = static_cast<float>(static_cast<double>(angle) * kRadiansPerDegree);
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        const PiecePlace* entry = get_descriptor(static_cast<int>(i));
        auto* piece = placed_piece(i);
        maths::Vec4 size;
        piece->measure_span(&size);
        // The piece's centre from the metapiece's centre (y read unsigned here), turned with the metapiece.
        maths::Mat4 place{};
        place.reset_to_identity();
        core::at(place.row, 3).x = static_cast<float>((static_cast<double>(entry->x) * kGridPitch) +
                                                      (static_cast<double>(size.x) * kHalf) - half_w);
        core::at(place.row, 3).y = static_cast<float>(static_cast<double>(static_cast<uint8_t>(entry->y)) * kGridStepUp);
        core::at(place.row, 3).z = static_cast<float>((static_cast<double>(entry->z) * kGridStepAlong) +
                                                      (static_cast<double>(size.z) * kHalf) - half_l);
        core::at(place.row, 3).w = 1.0F;
        maths::Mat4 turn{};
        maths::make_yaw_matrix(turn, radians);
        multiply_matrices(&place, &place, &turn);
        maths::Vec4& at = core::at(place.row, 3);
        at.x = static_cast<float>(static_cast<double>(position.x) + at.x);
        at.y = static_cast<float>(static_cast<double>(position.y) + at.y);
        at.z = static_cast<float>(static_cast<double>(position.z) + at.z);
        at.w = static_cast<float>(static_cast<double>(position.w) + at.w);
        piece->at_point().x = at.x;
        piece->at_point().y = at.y;
        piece->at_point().z = at.z;
        piece->at_point().w = at.w;
        scene::place_at_absolute_point(scene::look_up_sector(piece->sector()), &at);
        piece->set_angle(static_cast<float>((static_cast<double>(entry->rot) * kQuarterTurnDegrees) + angle));
    }
}

void PieceGroup::spotlight(int on, int gap)
{
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        auto* piece = placed_piece(i);
        if ((piece->flag_bits() & ParkTile::kCloned) != 0U) {
            piece->set_highlight(on != 0, gap != 0);
        }
    }
}

void PieceGroup::finish_pieces()
{
    maths::Vec4 max;
    maths::Vec4 min;
    get_bounds(&max, &min);
    const double size_x = static_cast<double>(max.x) - min.x;
    const double size_y = static_cast<double>(max.y) - min.y;
    const auto size_z = static_cast<float>(static_cast<double>(max.z) - min.z);
    const double exact_z = static_cast<double>(max.z) - min.z;
    cell_footprint_.l = get_cells(exact_z, kGridStepAlong);
    cell_footprint_.h = get_cells(size_y, kGridStepUp);
    cell_footprint_.w = get_cells(size_x, kGridPitch);
    cell_footprint_.h = std::max<uint8_t>(cell_footprint_.h, 1);
    // The pieces moved so that the metapiece's centre is the origin (x and z).
    const double half_x = size_x * 0.5;
    const double half_z = static_cast<double>(size_z) * 0.5;
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        auto* piece = placed_piece(i);
        piece->at_point().x = static_cast<float>((static_cast<double>(piece->at_point().x) - min.x) - half_x);
        piece->at_point().z = static_cast<float>((static_cast<double>(piece->at_point().z) - min.z) - half_z);
    }
}

// --- PlacedPieceGroup -------------------------------------------------------------------------------------------

PlacedPieceGroup::PlacedPieceGroup()
{
    flag_bits() = kConcrete;
    assign_quarter_turns(0);
}

ParkTile* PlacedPieceGroup::get_piece(uint32_t index)
{
    if (index >= filled_slots() || static_cast<int>(index) < 0) {
        return nullptr;
    }
    return slot(static_cast<int>(index)).piece;
}

void PlacedPieceGroup::commission(int count) { slots_.commission(count); }

void PlacedPieceGroup::turn_descriptors(int turn)
{
    slots_.turn({.filled = filled_slots(), .turn = turn}, cell_footprint(), [](const PlacedSlot& from) {
        CellBox size{};
        size.w = 0;
        size.l = 0;
        from.piece->get_footprint_size(&size);
        return SlotSize{.across = size.w, .along = size.l};
    });
}

void PlacedPieceGroup::insert_tile(ParkPiece* placed, CellBox* pos, int rot, bool sets_floor)
{
    auto* piece = core::down_cast<ParkTile>(placed);
    PlacedSlot* entry = &slot(static_cast<int>(filled_slots()++));
    maths::Vec4 size;
    piece->measure_span(&size);
    // The piece's centre in the metapiece's cells.
    const double x = (static_cast<double>(pos->x) * kGridPitch) + (static_cast<double>(size.x) * 0.5);
    const double y = static_cast<double>(pos->y) * kGridStepUp;
    const double z = (static_cast<double>(pos->z) * kGridStepAlong) + (static_cast<double>(size.z) * 0.5);
    piece->at_point().w = 1.0F;
    piece->at_point().z = static_cast<float>(z);
    piece->at_point().x = static_cast<float>(x);
    piece->at_point().y = static_cast<float>(y);
    entry->piece = piece;
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    entry->sets_floor_ = sets_floor;
}

// --- PieceBlueprint -------------------------------------------------------------------------------------------

PieceBlueprint::PieceBlueprint() { flag_bits() = kAbstract; }

TileTemplate* PieceBlueprint::get_piece(uint32_t index)
{
    if (index >= filled_slots() || static_cast<int>(index) < 0) {
        return nullptr;
    }
    return ParkLayout::singleton()->builder()->get_piece_source(slot(static_cast<int>(index)).piece);
}

void PieceBlueprint::commission(int count) { slots_.commission(count); }

void PieceBlueprint::turn_descriptors(int turn)
{
    slots_.turn({.filled = filled_slots(), .turn = turn}, cell_footprint(), [](const NamedSlot& from) {
        const PieceGroup* entry = ParkLayout::singleton()->fetch_meta_template(from.piece, nullptr);
        const CellBox size = entry->cell_footprint();
        if (from.rot & 1) {
            return SlotSize{.across = size.l, .along = size.w};
        }
        return SlotSize{.across = size.w, .along = size.l};
    });
}

namespace {

// The abstract metapiece's area grown to take in `pos` (with its size).
void grow_to_take(CellBox& cells, const CellBox* pos)
{
    const int w = pos->x + pos->w;
    const int h = pos->y + pos->h;
    const int l = pos->z + pos->l;
    if (std::cmp_greater(w, cells.w)) {
        cells.w = static_cast<uint8_t>(w);
    }
    if (std::cmp_greater(h, cells.h)) {
        cells.h = static_cast<uint8_t>(h);
    }
    if (std::cmp_greater(l, cells.l)) {
        cells.l = static_cast<uint8_t>(l);
    }
}

} // namespace

void PieceBlueprint::insert_tile(ParkPiece* piece, CellBox* pos, int rot, bool sets_floor)
{
    auto* source = ((piece->flag_bits() & ParkPiece::kSource) != 0U) ? core::down_cast<TileTemplate>(piece) : nullptr;
    NamedSlot* entry = &slot(static_cast<int>(filled_slots()++));
    if (!source) {
        return; // (counted all the same)
    }
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    entry->sets_floor_ = sets_floor;
    entry->piece = source->sector();
    grow_to_take(cell_footprint(), pos);
    cell_footprint().h = std::max<uint8_t>(cell_footprint().h, 1);
}

void PieceBlueprint::add_meta_piece(PieceGroup* meta, CellBox* pos, MetaPlacement meta_placement)
{
    const auto rot = meta_placement.rot;
    const auto sets_floor = meta_placement.sets_floor_;
    NamedSlot* entry = &slot(static_cast<int>(filled_slots()++));
    if (!meta) {
        return;
    }
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    entry->sets_floor_ = sets_floor != 0;
    entry->piece = meta->name_key();
    grow_to_take(cell_footprint(), pos);
}

NamedSlot* PieceBlueprint::get_entry(NamedSlot* out, int index)
{
    int n = 0; // the entries counted so far, a nested metapiece's pieces each one
    // (when `index` is past the end the original returns a stack leftover; here an empty entry)
    NamedSlot result = {};
    for (uint32_t i = 0; i < filled_slots(); ++i) {
        const NamedSlot* outer = &slot(static_cast<int>(i));
        PieceGroup* entry = ParkLayout::singleton()->fetch_meta_template(outer->piece, nullptr);
        if ((entry->flag_bits() & kSector) != 0U) { // a single piece
            if (n == index) {
                *out = *outer;
                return out;
            }
            ++n;
            continue;
        }
        for (uint32_t j = 0; j < entry->filled_slots(); ++j) {
            const NamedSlot* inner = &core::down_cast<PieceBlueprint>(entry)->slot(static_cast<int>(j));
            if (n != index) {
                ++n;
                continue;
            }
            // The nested piece placed in this metapiece: turned with its metapiece (the turn not wrapped).
            result = *inner;
            result.rot = static_cast<uint8_t>(inner->rot + outer->rot);
            result.y = static_cast<int8_t>(inner->y + outer->y);
            const CellBox& here = cell_footprint();
            const CellBox& nested = entry->cell_footprint();
            uint8_t x = outer->x;
            uint8_t z = outer->z;
            switch (outer->rot) {
            case 0:
                x = static_cast<uint8_t>(x + inner->x);
                z = static_cast<uint8_t>(z + inner->z);
                break;
            case 1:
                x = static_cast<uint8_t>(x + inner->z);
                z = static_cast<uint8_t>(z + (here.w - inner->x - nested.w));
                break;
            case 2:
                x = static_cast<uint8_t>(x + (here.w - inner->x - nested.w));
                z = static_cast<uint8_t>(z + (here.l - inner->z - nested.l));
                break;
            case 3:
                x = static_cast<uint8_t>(x + (here.l - inner->z - nested.l));
                z = static_cast<uint8_t>(z + inner->x);
                break;
            default:
                break;
            }
            result.x = x;
            result.z = z;
            *out = result;
            return out;
        }
    }
    *out = result;
    return out;
}

} // namespace park_editing

