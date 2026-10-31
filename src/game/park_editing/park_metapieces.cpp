// The park editor's metapieces (park_editing::PieceGroup and its two kinds): a list of descriptors, each a piece and its cell
// offset and turn within the metapiece. An abstract metapiece (a palette entry) names its pieces (source pieces or
// other abstract metapieces); a concrete one (placed in the park or held by the cursor) holds cloned pieces, which it
// moves and turns with itself.

#include <cstdint>

#include "game/park_editing/park.h"
#include "engine/gfx/d3dx.h"

#include "engine/core/mem.h"
#include "game/park_editing/park_nx.h"
#include "engine/maths/mat4.h"
#include "engine/script/param_struct.h"

namespace park_editing {

namespace {

// Degrees to radians, as the exe has it (0x00645464).
constexpr float kRadiansPerDegree = 0.0174532924f;
// A little slack before a size is cut down to whole cells (0x00645448).
constexpr double kCellSlack = 2.0;
// The bounds' starting values (0x47C35000): far outside the park.
constexpr float kFarAway = 100000.0f;

// The extra descriptors' memory (the memory manager's, on the current heap).
PieceGroupDesc* allocate_descriptors(uint32_t count)
{
    return static_cast<PieceGroupDesc*>(memory::carve(count * sizeof(PieceGroupDesc), 1, nullptr));
}

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

// --- PieceGroup ---------------------------------------------------------------------------------------------------

bool PieceGroup::is_spawn_marker()
{
    switch (name_key_) {
    case 0xCA07B4FC:
    case 0x613C7766:
    case 0x3A784D4C:
    case 0x4E2EFD2A:
    case 0x8A1576B6:
    case 0xD7EB62B5:
    case 0xDC09A6A4:
    case 0xEC82433C:
        return true;
    default:
        return false;
    }
}

PieceGroupDesc* PieceGroup::get_descriptor(int index) { return index < 2 ? &desc_[index] : &extra_desc_[index - 2]; }

void PieceGroup::toggle_shown(bool visible)
{
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        auto* piece = static_cast<ParkTile*>(get_descriptor(static_cast<int>(i))->piece);
        scene::change_live(scene::look_up_sector(piece->sector_), visible);
    }
}

int PieceGroup::count_pieces()
{
    int count = 0;
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        uint32_t name = reinterpret_cast<uint32_t>(get_descriptor(static_cast<int>(i))->piece);
        PieceGroup* entry = ParkLayout::singleton()->fetch_meta_template(name, nullptr);
        if (entry->flag_bits_ & kSector) // a single piece
            ++count;
        else
            count += static_cast<int>(entry->filled_slots_);
    }
    return count;
}

bool PieceGroup::fits_inside_region(CellBox* area)
{
    const CellBox& cells = cell_footprint_;
    if (cells.x < area->x || cells.x + cells.w > area->x + area->w)
        return false;
    if (cells.z < area->z || cells.z + cells.l > area->z + area->l)
        return false;
    return true;
}

void PieceGroup::set_quarter_turns(int rot)
{
    const int turn = (rot - quarter_turns_) & 3;
    quarter_turns_ = rot;
    if (turn & 1) { // a quarter turn: width and length swap
        uint8_t w = cell_footprint_.w;
        cell_footprint_.w = cell_footprint_.l;
        cell_footprint_.l = w;
    }
    // Every descriptor turned about the metapiece's corner, into new storage.
    PieceGroupDesc* extra = extra_desc_ ? allocate_descriptors(slot_limit_ - 2) : nullptr;
    // (the original leaves the unused ones of these two as stack leftovers; here they keep what they were)
    PieceGroupDesc first[2] = {desc_[0], desc_[1]};
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        PieceGroupDesc* to = i < 2 ? &first[i] : &extra[i - 2];
        const PieceGroupDesc* from = i < 2 ? &desc_[i] : &extra_desc_[i - 2];
        *to = *from;
        // The entry's own size in cells, as it lies (x, z).
        uint8_t across;
        uint8_t along;
        if (flag_bits_ & kAbstract) {
            PieceGroup* entry =
                ParkLayout::singleton()->fetch_meta_template(reinterpret_cast<uint32_t>(from->piece), nullptr);
            CellBox size = entry->cell_footprint_;
            if (from->rot & 1) {
                along = size.w;
                across = size.l;
            } else {
                along = size.l;
                across = size.w;
            }
        } else {
            CellBox size;
            size.w = 0;
            size.l = 0;
            static_cast<ParkTile*>(from->piece)->get_footprint_size(&size);
            along = size.l;
            across = size.w;
        }
        switch (turn) {
        case 0:
            to->x = from->x;
            to->z = from->z;
            break;
        case 1:
            to->x = from->z;
            to->z = static_cast<uint8_t>(cell_footprint_.l - from->x - across);
            break;
        case 2:
            to->x = static_cast<uint8_t>(cell_footprint_.w - from->x - across);
            to->z = static_cast<uint8_t>(cell_footprint_.l - from->z - along);
            break;
        case 3:
            to->x = static_cast<uint8_t>(cell_footprint_.w - from->z - along);
            to->z = from->x;
            break;
        }
        to->y = from->y;
        to->rot = static_cast<uint8_t>((from->rot + turn) & 3);
        to->piece = from->piece;
    }
    if (extra_desc_)
        memory::dealloc(extra_desc_);
    extra_desc_ = extra;
    desc_[0] = first[0];
    desc_[1] = first[1];
}

maths::Vec4* PieceGroup::get_relative_position(maths::Vec4* out, uint32_t index)
{
    // The metapiece's half size (x and z).
    const float half_w = static_cast<float>(static_cast<double>(cell_footprint_.w) * kGridPitch * 0.5);
    const float half_l = static_cast<float>(static_cast<double>(cell_footprint_.l) * kGridStepAlong * 0.5);
    const PieceGroupDesc* entry = get_descriptor(static_cast<int>(index));
    ParkPiece* piece = get_piece(index);
    maths::Vec4 size1;
    maths::Vec4 size2;
    const float size_z = piece->get_dims_turned(&size1, entry->rot)->z;
    const float size_x = piece->get_dims_turned(&size2, entry->rot)->x;
    // The piece's centre in the metapiece, from the metapiece's centre.
    const double x = static_cast<double>(entry->x) * kGridPitch + static_cast<double>(size_x) * 0.5;
    const double y = static_cast<double>(static_cast<uint8_t>(entry->y)) * kGridStepUp; // (read unsigned)
    const float z =
        static_cast<float>(static_cast<double>(entry->z) * kGridStepAlong + static_cast<double>(size_z) * 0.5);
    out->x = static_cast<float>(x - half_w);
    out->y = static_cast<float>(y);
    out->z = static_cast<float>(static_cast<double>(z) - half_l);
    out->w = 1.0f; // (1 less a stack leftover in the original)
    return out;
}

void PieceGroup::commission(int count)
{
    PieceGroupDesc* extra = nullptr;
    if (count > 2) {
        extra = allocate_descriptors(static_cast<uint32_t>(count - 2));
        // Growing: the extra entries kept.
        if (extra && extra_desc_ && slot_limit_ <= static_cast<uint32_t>(count) && slot_limit_ > 2) {
            for (uint32_t i = 2; i < slot_limit_; ++i)
                extra[i - 2] = extra_desc_[i - 2];
        }
    }
    if (extra_desc_)
        memory::dealloc(extra_desc_);
    extra_desc_ = extra;
    slot_limit_ = static_cast<uint32_t>(count);
}

void PieceGroup::get_bounds(maths::Vec4* max, maths::Vec4* min)
{
    *min = {kFarAway, kFarAway, kFarAway, 0.0f};
    *max = {-kFarAway, -kFarAway, -kFarAway, 0.0f};
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        auto* piece = static_cast<ParkTile*>(get_descriptor(static_cast<int>(i))->piece);
        maths::Vec4 size;
        piece->measure_span(&size);
        const double x = piece->at_point_.x;
        const float y = piece->at_point_.y;
        const double z = piece->at_point_.z;
        const double half_x = static_cast<double>(size.x) * 0.5;
        if (x - half_x < min->x)
            min->x = static_cast<float>(x - half_x);
        if (half_x + x > max->x)
            max->x = static_cast<float>(half_x + x);
        const double half_z = static_cast<double>(size.z) * 0.5;
        if (z - half_z < min->z)
            min->z = static_cast<float>(z - half_z);
        if (half_z + z > max->z)
            max->z = static_cast<float>(half_z + z);
        if (y < min->y)
            min->y = y;
        if (static_cast<double>(size.y) + y > max->y)
            max->y = static_cast<float>(static_cast<double>(size.y) + y);
    }
}

bool PieceGroup::intersects(CellBox area, uint32_t precise)
{
    const CellBox& cells = cell_footprint_;
    const int x_start = area.x;
    const int x_end = area.x + area.w;
    const int y_start = area.y;
    const int y_end = area.y + area.h;
    const int z_start = area.z;
    const int z_end = area.z + area.l;
    if (cells.x + cells.w <= x_start || cells.x >= x_end)
        return false;
    if (cells.y + cells.h <= y_start || cells.y >= y_end)
        return false;
    if (cells.z + cells.l <= z_start || cells.z >= z_end)
        return false;
    if (!static_cast<uint8_t>(precise))
        return true;
    // Precisely (when any piece is flagged setsFloor): by the cells of the pieces that are NOT flagged.
    bool any_sets_floor = false;
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        if (get_descriptor(static_cast<int>(i))->sets_floor_)
            any_sets_floor = true;
    }
    if (!any_sets_floor)
        return true;
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        const PieceGroupDesc* entry = get_descriptor(static_cast<int>(i));
        CellBox size;
        size.w = 0;
        size.h = 0;
        size.l = 0;
        get_piece(i)->get_footprint_size(&size);
        const uint8_t x = static_cast<uint8_t>(entry->x + cells.x);
        const int8_t y = static_cast<int8_t>(entry->y + cells.y);
        const uint8_t z = static_cast<uint8_t>(entry->z + cells.z);
        if (entry->sets_floor_)
            continue;
        if (x + size.w > x_start && y + size.h > y_start && z + size.l > z_start && x < x_end && y < y_end && z < z_end)
            return true;
    }
    return false;
}

PieceGroup::~PieceGroup()
{
    if (extra_desc_)
        memory::dealloc(extra_desc_);
}

void PieceGroup::get_piece_list(script::Array* out)
{
    out->configure_size_kind(filled_slots_, script::kQbvalStruct);
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        const PieceGroupDesc* entry = get_descriptor(static_cast<int>(i));
        script::Struct* memory = script::spawn_param_bag();
        script::Struct* piece = memory ? memory->prepare_all() : nullptr;
        piece->put_name_key(0u,
                            reinterpret_cast<uint32_t>(entry->piece)); // (out of memory crashes, as in the original)
        maths::Vec4 position;
        get_relative_position(&position, i);
        piece->put_vec3(0u, position.x, position.y, position.z);
        out->set_element(i, reinterpret_cast<uint32_t>(piece));
    }
}

void PieceGroup::change_place(maths::Vec4 position, float angle)
{
    const float half_w = static_cast<float>(static_cast<double>(cell_footprint_.w) * kGridPitch * 0.5);
    const float half_l = static_cast<float>(static_cast<double>(cell_footprint_.l) * kGridStepAlong * 0.5);
    if (!filled_slots_)
        return;
    const float radians = static_cast<float>(static_cast<double>(angle) * kRadiansPerDegree);
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        const PieceGroupDesc* entry = get_descriptor(static_cast<int>(i));
        auto* piece = static_cast<ParkTile*>(entry->piece);
        maths::Vec4 size;
        piece->measure_span(&size);
        // The piece's centre from the metapiece's centre (y read unsigned here), turned with the metapiece.
        maths::Mat4 place;
        place.reset_to_identity();
        place.row[3].x =
            static_cast<float>(static_cast<double>(entry->x) * kGridPitch + static_cast<double>(size.x) * 0.5 - half_w);
        place.row[3].y = static_cast<float>(static_cast<double>(static_cast<uint8_t>(entry->y)) * kGridStepUp);
        place.row[3].z = static_cast<float>(static_cast<double>(entry->z) * kGridStepAlong +
                                            static_cast<double>(size.z) * 0.5 - half_l);
        place.row[3].w = 1.0f;
        maths::Mat4 turn;
        maths::make_yaw_matrix(turn, radians);
        multiply_matrices(&place, &place, &turn);
        maths::Vec4& at = place.row[3];
        at.x = static_cast<float>(static_cast<double>(position.x) + at.x);
        at.y = static_cast<float>(static_cast<double>(position.y) + at.y);
        at.z = static_cast<float>(static_cast<double>(position.z) + at.z);
        at.w = static_cast<float>(static_cast<double>(position.w) + at.w);
        piece->at_point_.x = at.x;
        piece->at_point_.y = at.y;
        piece->at_point_.z = at.z;
        piece->at_point_.w = at.w;
        scene::place_at_absolute_point(scene::look_up_sector(piece->sector_), &at);
        piece->set_angle(static_cast<float>(static_cast<double>(entry->rot) * 90.0 + angle));
    }
}

void PieceGroup::spotlight(int on, int gap)
{
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        auto* piece = static_cast<ParkTile*>(get_descriptor(static_cast<int>(i))->piece);
        if (piece->flag_bits_ & ParkTile::kCloned)
            piece->set_highlight(on != 0, gap != 0);
    }
}

void PieceGroup::finish_pieces()
{
    maths::Vec4 max;
    maths::Vec4 min;
    get_bounds(&max, &min);
    const double size_x = static_cast<double>(max.x) - min.x;
    const double size_y = static_cast<double>(max.y) - min.y;
    const float size_z = static_cast<float>(static_cast<double>(max.z) - min.z);
    const double exact_z = static_cast<double>(max.z) - min.z;
    cell_footprint_.l = get_cells(exact_z, kGridStepAlong);
    cell_footprint_.h = get_cells(size_y, kGridStepUp);
    cell_footprint_.w = get_cells(size_x, kGridPitch);
    if (cell_footprint_.h < 1)
        cell_footprint_.h = 1;
    // The pieces moved so that the metapiece's centre is the origin (x and z).
    const double half_x = size_x * 0.5;
    const double half_z = static_cast<double>(size_z) * 0.5;
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        auto* piece = static_cast<ParkTile*>(get_descriptor(static_cast<int>(i))->piece);
        piece->at_point_.x = static_cast<float>((static_cast<double>(piece->at_point_.x) - min.x) - half_x);
        piece->at_point_.z = static_cast<float>((static_cast<double>(piece->at_point_.z) - min.z) - half_z);
    }
}

// --- PlacedPieceGroup -------------------------------------------------------------------------------------------

PlacedPieceGroup::PlacedPieceGroup()
{
    flag_bits_ = kConcrete;
    quarter_turns_ = 0;
}

ParkTile* PlacedPieceGroup::get_piece(uint32_t index)
{
    if (index >= filled_slots_ || static_cast<int>(index) < 0)
        return nullptr;
    return static_cast<ParkTile*>(get_descriptor(static_cast<int>(index))->piece);
}

void PlacedPieceGroup::insert_tile(ParkPiece* placed, CellBox* pos, int rot, bool sets_floor)
{
    auto* piece = static_cast<ParkTile*>(placed);
    PieceGroupDesc* entry = get_descriptor(static_cast<int>(filled_slots_++));
    maths::Vec4 size;
    piece->measure_span(&size);
    // The piece's centre in the metapiece's cells.
    const double x = static_cast<double>(pos->x) * kGridPitch + static_cast<double>(size.x) * 0.5;
    const double y = static_cast<double>(pos->y) * kGridStepUp;
    const double z = static_cast<double>(pos->z) * kGridStepAlong + static_cast<double>(size.z) * 0.5;
    piece->at_point_.w = 1.0f;
    piece->at_point_.z = static_cast<float>(z);
    piece->at_point_.x = static_cast<float>(x);
    piece->at_point_.y = static_cast<float>(y);
    entry->piece = piece;
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    entry->sets_floor_ = sets_floor;
}

// --- PieceBlueprint -------------------------------------------------------------------------------------------

PieceBlueprint::PieceBlueprint() { flag_bits_ = kAbstract; }

TileTemplate* PieceBlueprint::get_piece(uint32_t index)
{
    if (index >= filled_slots_ || static_cast<int>(index) < 0)
        return nullptr;
    const PieceGroupDesc* entry = get_descriptor(static_cast<int>(index));
    return ParkLayout::singleton()->builder_->get_piece_source(reinterpret_cast<uint32_t>(entry->piece));
}

namespace {

// The abstract metapiece's area grown to take in `pos` (with its size).
void grow_to_take(CellBox& cells, const CellBox* pos)
{
    const int w = pos->x + pos->w;
    const int h = pos->y + pos->h;
    const int l = pos->z + pos->l;
    if (w > cells.w)
        cells.w = static_cast<uint8_t>(w);
    if (h > cells.h)
        cells.h = static_cast<uint8_t>(h);
    if (l > cells.l)
        cells.l = static_cast<uint8_t>(l);
}

} // namespace

void PieceBlueprint::insert_tile(ParkPiece* piece, CellBox* pos, int rot, bool sets_floor)
{
    auto* source = (piece->flag_bits_ & ParkPiece::kSource) ? static_cast<TileTemplate*>(piece) : nullptr;
    PieceGroupDesc* entry = get_descriptor(static_cast<int>(filled_slots_++));
    if (!source)
        return; // (counted all the same)
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    entry->sets_floor_ = sets_floor;
    entry->piece = reinterpret_cast<void*>(source->sector_);
    grow_to_take(cell_footprint_, pos);
    if (cell_footprint_.h < 1)
        cell_footprint_.h = 1;
}

void PieceBlueprint::add_meta_piece(PieceGroup* meta, CellBox* pos, int rot, uint8_t sets_floor)
{
    PieceGroupDesc* entry = get_descriptor(static_cast<int>(filled_slots_++));
    if (!meta)
        return;
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    reinterpret_cast<uint8_t&>(entry->sets_floor_) = sets_floor;
    entry->piece = reinterpret_cast<void*>(meta->name_key_);
    grow_to_take(cell_footprint_, pos);
}

PieceGroupDesc* PieceBlueprint::get_entry(PieceGroupDesc* out, int index)
{
    int n = 0; // the entries counted so far, a nested metapiece's pieces each one
    // (when `index` is past the end the original returns a stack leftover; here an empty entry)
    PieceGroupDesc result = {};
    for (uint32_t i = 0; i < filled_slots_; ++i) {
        const PieceGroupDesc* outer = get_descriptor(static_cast<int>(i));
        PieceGroup* entry =
            ParkLayout::singleton()->fetch_meta_template(reinterpret_cast<uint32_t>(outer->piece), nullptr);
        if (entry->flag_bits_ & kSector) { // a single piece
            if (n == index) {
                *out = *outer;
                return out;
            }
            ++n;
            continue;
        }
        for (uint32_t j = 0; j < entry->filled_slots_; ++j) {
            const PieceGroupDesc* inner = entry->get_descriptor(static_cast<int>(j));
            if (n != index) {
                ++n;
                continue;
            }
            // The nested piece placed in this metapiece: turned with its metapiece (the turn not wrapped).
            result = *inner;
            result.rot = static_cast<uint8_t>(inner->rot + outer->rot);
            result.y = static_cast<int8_t>(inner->y + outer->y);
            const CellBox& here = cell_footprint_;
            const CellBox& nested = entry->cell_footprint_;
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

using park_editing::PieceBlueprint;
using park_editing::PieceGroup;
using park_editing::PlacedPieceGroup;
