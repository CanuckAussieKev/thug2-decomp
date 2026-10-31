// The park editor's pieces (park_editing::ParkTile, TileTemplate, CopiedPiece) and cell boxes (CellBox): a source piece is
// one of the editor's own sectors (its name and size); the pieces placed in the park are cloned from them and moved,
// turned, stretched, shown or tinted through their sectors.

#include <cstdint>

#include "game/park_editing/park.h"
#include "engine/gfx/d3dx.h"
#include "engine/core/code_address.h"

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "game/park_editing/park_nx.h"
#include "engine/maths/mat4.h"

namespace park_editing {

static VtableSlot g_piece_vtable_slots[] = {&_purecall, &ParkTile::teardown};
void** const kPieceVtable = reinterpret_cast<void**>(g_piece_vtable_slots);
static VtableSlot g_source_piece_vtable_slots[] = {&TileTemplate::measure_span, &TileTemplate::teardown};
void** const kSourcePieceVtable = reinterpret_cast<void**>(g_source_piece_vtable_slots);
static VtableSlot g_cloned_piece_vtable_slots[] = {&CopiedPiece::measure_span, &TileTemplate::teardown};
void** const kClonedPieceVtable = reinterpret_cast<void**>(g_cloned_piece_vtable_slots);

namespace {

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

CellBox* CellBox::assign_value(uint8_t x, int8_t y, uint8_t z, uint8_t w, uint8_t h, uint8_t l)
{
    x = x;
    y = y;
    z = z;
    w = w;
    h = h;
    l = l;
    return this;
}

uint8_t* CellBox::get_byte(int index) { return reinterpret_cast<uint8_t*>(this) + index; }

void CellBox::stretch_vertically() { h = static_cast<uint8_t>(0x7F - y); }

void CellBox::raise_ceiling_to_limit()
{
    y = -100;
    h = 200;
}

void CellBox::dump_to_log() const
{
    debug::printf("pos=(%d,%d,%d) dims=(%d,%d,%d)\n", x, static_cast<int>(y), z, w, h, l); // "pos=(%d,%d,%d) dims=..."
}

// --- ParkTile -------------------------------------------------------------------------------------------------------

void ParkTile::destroy_body() { vtable_ = kVtable; }

ParkTile* ParkTile::teardown(unsigned int flags)
{
    vtable_ = kVtable;
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

uint32_t ParkTile::fetch_serial() { return ident_; }

void ParkTile::set_quarter_turns(int rot, int update)
{
    quarter_turns_ = rot;
    if (update == 1)
        scene::spin_around_up(scene::look_up_sector(sector_), rot);
}

void ParkTile::change_live(bool active) { scene::change_live(scene::look_up_sector(sector_), active); }

void ParkTile::set_visible(bool visible) { scene::toggle_shown(scene::look_up_sector(sector_), visible ? 0xFF : 0); }

void ParkTile::get_footprint_size(CellBox* dims)
{
    maths::Vec4 size;
    measure_span(&size);
    dims->l = get_cells(size.z, kGridStepAlong);
    dims->h = get_cells(size.y, kGridStepUp);
    dims->w = get_cells(size.x, kGridPitch);
}

maths::Vec4* ParkTile::get_dims_turned(maths::Vec4* out, int rot)
{
    ParkTile* source = (flag_bits_ & kSource) ? this : blend_source_->as_piece();
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

void ParkTile::change_place(maths::Vec4* position, int update)
{
    at_point_.x = position->x;
    at_point_.y = position->y;
    at_point_.z = position->z;
    at_point_.w = position->w;
    if (update == 1)
        scene::place_at_absolute_point(scene::look_up_sector(sector_), position);
}

void ParkTile::set_length(float length)
{
    uint8_t* sector = scene::look_up_sector(sector_);
    const bool across = (quarter_turns_ & 1) != 0;
    // (w is a stack leftover in the original)
    maths::Vec4 scale = {across ? 1.0f : length, 1.0f, across ? length : 1.0f, 1.0f};
    scene::change_scale(sector, &scale);
}

void ParkTile::set_angle(float degrees)
{
    uint8_t* geometry = scene::fetch_shape(scene::look_up_sector(sector_));
    if (!geometry)
        return;
    maths::Mat4 orientation;
    orientation.reset_to_identity();
    maths::Mat4 turn;
    maths::make_yaw_matrix(turn, static_cast<float>(static_cast<double>(degrees) * kRadiansPerDegree));
    multiply_matrices(&orientation, &orientation, &turn);
    scene::change_facing(geometry, &orientation);
}

void ParkTile::set_highlight(bool on, bool gap)
{
    uint8_t* sector = scene::look_up_sector(sector_);
    if (!sector)
        return;
    uint8_t* geometry = scene::fetch_shape(sector);
    if (!geometry)
        return;
    if (!on) {
        scene::wipe_tint(geometry);
        return;
    }
    // r g b (a 0): purple over a gap's pieces, red otherwise
    scene::change_tint(geometry, gap ? 0x00A064A0u : 0x006464A0u);
}

// --- TileTemplate / CopiedPiece ---------------------------------------------------------------------------------

TileTemplate* TileTemplate::make_new()
{
    label_ = 0;
    following_ = nullptr;
    vtable_ = kVtable;
    flag_bits_ = ParkTile::kSource;
    extent_ = {0.0f, 0.0f, 0.0f, 1.0f};
    field_20_ = 1;
    trigger_script_ = 0;
    rail_point_total_ = 0;
    num_rail_segments_ = 0;
    return this;
}

maths::Vec4* TileTemplate::measure_span(maths::Vec4* out)
{
    out->x = extent_.x;
    out->y = extent_.y;
    out->z = extent_.z;
    out->w = extent_.w;
    return out;
}

TileTemplate* TileTemplate::teardown(unsigned int flags)
{
    as_piece()->destroy_body();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

CopiedPiece* CopiedPiece::make_new()
{
    sector_ = 0;
    following_ = nullptr;
    vtable_ = kVtable;
    ident_ = 0;
    flag_bits_ = kCloned;
    blend_source_ = nullptr;
    return this;
}

maths::Vec4* CopiedPiece::measure_span(maths::Vec4* out)
{
    const maths::Vec4& size = blend_source_->extent_;
    const bool across = (quarter_turns_ & 1) != 0;
    out->x = across ? size.z : size.x;
    out->y = size.y;
    out->z = across ? size.x : size.z;
    out->w = size.w; // (a stack leftover in the original)
    return out;
}

} // namespace Ed

using park_editing::CopiedPiece;
using park_editing::ParkTile;
using park_editing::TileTemplate;
using park_editing::CellBox;
