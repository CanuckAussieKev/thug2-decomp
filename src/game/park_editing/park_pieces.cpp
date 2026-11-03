// The park editor's pieces (park_editing::ParkPiece: ParkTile / CopiedPiece, TileTemplate) and cell boxes (CellBox): a source piece is
// one of the editor's own sectors (its name and size); the pieces placed in the park are cloned from them and moved,
// turned, stretched, shown or tinted through their sectors.

#include <cstdint>

#include "game/park_editing/park.h"
#include "platform/d3dx.h"

#include "engine/core/dbg.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/sector.h"
#include "engine/maths/vec4.h"
#include "game/park_editing/park_nx.h"
#include "engine/maths/mat4.h"
#include "engine/core/down_cast.h"

namespace park_editing {

namespace {

constexpr int kCellHeightLimit = 0x7F;       // the most a cell's y + h can reach
constexpr int kCeilingFloor = 100;           // raise_ceiling_to_limit: from -100 for 200 cells
constexpr int kCeilingSpan = 200;
constexpr int kShownMask = 0xFF;
constexpr std::uint32_t kGapTint = 0x00A064A0u;   // r g b (a 0): purple over a gap's pieces
constexpr std::uint32_t kPieceTint = 0x006464A0u; // red otherwise

// A little slack before a size is cut down to whole cells (0x00645448).
constexpr double kCellSlack = 2.0;
// Degrees to radians, as the exe has it (0x00645464).
constexpr float kRadiansPerDegree = 0.0174532924f;

// A piece's size in whole cells along one axis (truncated).
uint8_t get_cells(float size, float cell) { return static_cast<uint8_t>(static_cast<int>((size + kCellSlack) / cell)); }

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library's): out = a * b.
void multiply_matrices(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b)
{
    d3dx::matrix_multiply(out, a, b);
}

} // namespace

// --- CellBox ----------------------------------------------------------------------------------------------------

CellBox* CellBox::assign_value(CellAt cell_at, CellExtent cell_extent)
{
    const auto width = cell_extent.width;
    const auto height = cell_extent.height;
    const auto length = cell_extent.length;
    const auto cell_x = cell_at.cell_x_;
    const auto cell_y = cell_at.cell_y_;
    const auto cell_z = cell_at.cell_z_;
    x = cell_x;
    y = cell_y;
    z = cell_z;
    w = width;
    h = height;
    l = length;
    return this;
}


void CellBox::stretch_vertically() { h = static_cast<uint8_t>(kCellHeightLimit - y); }

void CellBox::raise_ceiling_to_limit()
{
    y = -kCeilingFloor;
    h = kCeilingSpan;
}

void CellBox::dump_to_log() const
{
    debug::printf("pos=(%d,%d,%d) dims=(%d,%d,%d)\n", x, static_cast<int>(y), z, w, h, l); // "pos=(%d,%d,%d) dims=..."
}

// --- ParkPiece --------------------------------------------------------------------------------------------------

void ParkPiece::get_footprint_size(CellBox* dims)
{
    maths::Vec4 size;
    measure_span(&size);
    dims->l = get_cells(size.z, kGridStepAlong);
    dims->h = get_cells(size.y, kGridStepUp);
    dims->w = get_cells(size.x, kGridPitch);
}

maths::Vec4* ParkPiece::get_dims_turned(maths::Vec4* out, int rot)
{
    ParkPiece* source = (flag_bits_ & kSource) ? this : core::down_cast<ParkTile>(this)->blend_source();
    // (The original asks the source three times, once per component; it's a plain getter. Its w: the source's when
    // the turn is odd, a stack leftover when it's even - the source's here.)
    maths::Vec4 size;
    source->measure_span(&size);
    const bool across = (rot & 1) != 0;
    out->x = across ? size.z : size.x;
    out->y = size.y;
    out->z = across ? size.x : size.z;
    out->w = size.w;
    return out;
}

// --- ParkTile -------------------------------------------------------------------------------------------------------

uint32_t ParkTile::fetch_serial() const { return ident_; }

void ParkTile::set_quarter_turns(QuarterTurns quarter_turns)
{
    const auto rot = quarter_turns.rot;
    const auto update = quarter_turns.update;
    quarter_turns_ = rot;
    if (update == 1)
        scene::spin_around_up(scene::look_up_sector(sector()), rot);
}

void ParkTile::change_live(bool active) { scene::change_live(scene::look_up_sector(sector()), active); }

void ParkTile::set_visible(bool visible) { scene::toggle_shown(scene::look_up_sector(sector()), visible ? kShownMask : 0); }

void ParkTile::change_place(maths::Vec4* position, int update)
{
    at_point_.x = position->x;
    at_point_.y = position->y;
    at_point_.z = position->z;
    at_point_.w = position->w;
    if (update == 1)
        scene::place_at_absolute_point(scene::look_up_sector(sector()), position);
}

void ParkTile::set_length(float length)
{
    render::WorldCell* sector = scene::look_up_sector(this->sector());
    const bool across = (quarter_turns_ & 1) != 0;
    // (w is a stack leftover in the original)
    const maths::Vec4 scale = {.x = across ? 1.0f : length, .y = 1.0f, .z = across ? length : 1.0f, .w = 1.0f};
    scene::change_scale(sector, &scale);
}

void ParkTile::set_angle(float degrees)
{
    render::Geom* geometry = scene::fetch_shape(scene::look_up_sector(sector()));
    if (!geometry)
        return;
    maths::Mat4 orientation{};
    orientation.reset_to_identity();
    maths::Mat4 turn{};
    maths::make_yaw_matrix(turn, static_cast<float>(static_cast<double>(degrees) * kRadiansPerDegree));
    multiply_matrices(&orientation, &orientation, &turn);
    scene::change_facing(geometry, &orientation);
}

void ParkTile::set_highlight(bool on, bool gap)
{
    render::WorldCell* sector = scene::look_up_sector(this->sector());
    if (!sector)
        return;
    render::Geom* geometry = scene::fetch_shape(sector);
    if (!geometry)
        return;
    if (!on) {
        scene::wipe_tint(geometry);
        return;
    }
    // r g b (a 0): purple over a gap's pieces, red otherwise
    scene::change_tint(geometry, gap ? kGapTint : kPieceTint);
}

// --- TileTemplate / CopiedPiece ---------------------------------------------------------------------------------

maths::Vec4* TileTemplate::measure_span(maths::Vec4* out)
{
    out->x = extent_.x;
    out->y = extent_.y;
    out->z = extent_.z;
    out->w = extent_.w;
    return out;
}

maths::Vec4* CopiedPiece::measure_span(maths::Vec4* out)
{
    const maths::Vec4& size = blend_source()->extent();
    const bool across = (quarter_turns() & 1) != 0;
    out->x = across ? size.z : size.x;
    out->y = size.y;
    out->z = across ? size.x : size.z;
    out->w = size.w; // (a stack leftover in the original)
    return out;
}

} // namespace park_editing

