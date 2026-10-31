// The park editor's pieces (Ed::CPiece, CSourcePiece, CClonedPiece) and cell boxes (GridDims): a source piece is
// one of the editor's own sectors (its name and size); the pieces placed in the park are cloned from them and moved,
// turned, stretched, shown or tinted through their sectors.

#include "game/ed/park.h"
#include "game/gfx/d3dx.h"
#include "hook/game_code.h"

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/ed/park_nx.h"
#include "game/mth/matrix.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

GAME_VTABLE(kPieceVtable, 0x0064F3B4, GAME_LIBRARY_SLOT(0x00623771), &CPiece::Destroy);
GAME_VTABLE(kSourcePieceVtable, 0x0064F3C8, &CSourcePiece::GetDims, &CSourcePiece::Destroy);
GAME_VTABLE(kClonedPieceVtable, 0x0064F3D0, &CClonedPiece::GetDims, &CSourcePiece::Destroy);

namespace {

// A little slack before a size is cut down to whole cells (0x00645448).
constexpr double kCellSlack = 2.0;
// Degrees to radians, as the exe has it (0x00645464).
constexpr float kDegreesToRadians = 0.0174532924f;

// A piece's size in whole cells along one axis (truncated).
uint8_t Cells(float size, float cell) { return static_cast<uint8_t>(static_cast<int>((size + kCellSlack) / cell)); }

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library's): out = a * b.
void MultiplyMatrices(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

} // namespace

// --- GridDims ----------------------------------------------------------------------------------------------------

GridDims* GridDims::Set(uint8_t x_, int8_t y_, uint8_t z_, uint8_t w_, uint8_t h_, uint8_t l_)
{
    x = x_;
    y = y_;
    z = z_;
    w = w_;
    h = h_;
    l = l_;
    return this;
}

uint8_t* GridDims::Byte(int index) { return reinterpret_cast<uint8_t*>(this) + index; }

void GridDims::MakeInfiniteOnY() { h = static_cast<uint8_t>(0x7F - y); }

void GridDims::MakeInfinitelyHigh()
{
    y = -100;
    h = 200;
}

void GridDims::PrintContents() const
{
    Dbg::Printf(GAME_TEXT(0x0064F394, "pos=(%d,%d,%d) dims=(%d,%d,%d)\n"), x, static_cast<int>(y), z, w, h, l); // "pos=(%d,%d,%d) dims=..."
}

// --- CPiece -------------------------------------------------------------------------------------------------------

void CPiece::DestroyBody() { m_vtable = kVtable; }

CPiece* CPiece::Destroy(unsigned int flags)
{
    m_vtable = kVtable;
    if (flags & 1)
        Mem::Free(this);
    return this;
}

uint32_t CPiece::GetId() { return m_id; }

void CPiece::SetRot(int rot, int update)
{
    m_rot = rot;
    if (update == 1)
        Scene::SetYRotation(Scene::GetSector(m_sector), rot);
}

void CPiece::SetActive(bool active) { Scene::SetActive(Scene::GetSector(m_sector), active); }

void CPiece::SetVisible(bool visible) { Scene::SetVisibility(Scene::GetSector(m_sector), visible ? 0xFF : 0); }

void CPiece::GetCellDims(GridDims* dims)
{
    Mth::Vector size;
    GetDims(&size);
    dims->l = Cells(size.z, kCellLength);
    dims->h = Cells(size.y, kCellHeight);
    dims->w = Cells(size.x, kCellWidth);
}

Mth::Vector* CPiece::GetDimsTurned(Mth::Vector* out, int rot)
{
    CPiece* source = (m_flags & kSource) ? this : mp_source->AsPiece();
    // (The original asks the source three times, once per component; it's a plain getter. Its w: the source's when
    // the turn is odd, a stack leftover when it's even - the source's here.)
    Mth::Vector size;
    source->GetDims(&size);
    const bool across = (rot & 1) != 0;
    out->x = across ? size.z : size.x;
    out->y = size.y;
    out->z = across ? size.x : size.z;
    out->w = size.w;
    return out;
}

void CPiece::SetPosition(Mth::Vector* position, int update)
{
    m_pos.x = position->x;
    m_pos.y = position->y;
    m_pos.z = position->z;
    m_pos.w = position->w;
    if (update == 1)
        Scene::SetWorldPosition(Scene::GetSector(m_sector), position);
}

void CPiece::SetLength(float length)
{
    uint8_t* sector = Scene::GetSector(m_sector);
    const bool across = (m_rot & 1) != 0;
    // (w is a stack leftover in the original)
    Mth::Vector scale = {across ? 1.0f : length, 1.0f, across ? length : 1.0f, 1.0f};
    Scene::SetScale(sector, &scale);
}

void CPiece::SetAngle(float degrees)
{
    uint8_t* geometry = Scene::GetGeometry(Scene::GetSector(m_sector));
    if (!geometry)
        return;
    Mth::Matrix orientation;
    orientation.Ident();
    Mth::Matrix turn;
    Mth::CreateRotateYMatrix(turn, static_cast<float>(static_cast<double>(degrees) * kDegreesToRadians));
    MultiplyMatrices(&orientation, &orientation, &turn);
    Scene::SetOrientation(geometry, &orientation);
}

void CPiece::SetHighlight(bool on, bool gap)
{
    uint8_t* sector = Scene::GetSector(m_sector);
    if (!sector)
        return;
    uint8_t* geometry = Scene::GetGeometry(sector);
    if (!geometry)
        return;
    if (!on) {
        Scene::ClearColor(geometry);
        return;
    }
    // r g b (a 0): purple over a gap's pieces, red otherwise
    Scene::SetColor(geometry, gap ? 0x00A064A0u : 0x006464A0u);
}

// --- CSourcePiece / CClonedPiece ---------------------------------------------------------------------------------

CSourcePiece* CSourcePiece::Construct()
{
    m_name = 0;
    mp_next = nullptr;
    m_vtable = kVtable;
    m_flags = CPiece::kSource;
    m_size = {0.0f, 0.0f, 0.0f, 1.0f};
    m_20 = 1;
    m_triggerScript = 0;
    m_numRailPoints = 0;
    m_numRailSegments = 0;
    return this;
}

Mth::Vector* CSourcePiece::GetDims(Mth::Vector* out)
{
    out->x = m_size.x;
    out->y = m_size.y;
    out->z = m_size.z;
    out->w = m_size.w;
    return out;
}

CSourcePiece* CSourcePiece::Destroy(unsigned int flags)
{
    AsPiece()->DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

CClonedPiece* CClonedPiece::Construct()
{
    m_sector = 0;
    mp_next = nullptr;
    m_vtable = kVtable;
    m_id = 0;
    m_flags = kCloned;
    mp_source = nullptr;
    return this;
}

Mth::Vector* CClonedPiece::GetDims(Mth::Vector* out)
{
    const Mth::Vector& size = mp_source->m_size;
    const bool across = (m_rot & 1) != 0;
    out->x = across ? size.z : size.x;
    out->y = size.y;
    out->z = across ? size.x : size.z;
    out->w = size.w; // (a stack leftover in the original)
    return out;
}

} // namespace Ed

using Ed::CClonedPiece;
using Ed::CPiece;
using Ed::CSourcePiece;
using Ed::GridDims;

REPLACE(0x005901B0, &GridDims::Set);
REPLACE(0x005901E0, &GridDims::Byte);
REPLACE(0x005901F0, &GridDims::MakeInfiniteOnY);
REPLACE(0x00590200, &GridDims::MakeInfinitelyHigh);
REPLACE(0x00590210, &GridDims::PrintContents);
REPLACE(0x00590240, &CPiece::DestroyBody);
REPLACE_SOURCE_ONLY(0x00590250, &CPiece::GetId); // 4 bytes (folded: other classes' +0x10 getters too)
REPLACE(0x00590260, &CPiece::SetRot);
REPLACE(0x00590290, &CPiece::SetActive);
REPLACE(0x005902B0, &CPiece::SetVisible);
REPLACE(0x00590490, &CPiece::Destroy);
REPLACE(0x005904C0, &CPiece::GetCellDims);
REPLACE(0x00590530, &CPiece::GetDimsTurned);
REPLACE(0x005905C0, &CSourcePiece::Construct);
REPLACE(0x00590600, &CSourcePiece::GetDims);
REPLACE(0x00590620, &CClonedPiece::Construct);
REPLACE(0x00590640, &CSourcePiece::Destroy);
REPLACE(0x00590670, &CPiece::SetPosition);
REPLACE(0x005906B0, &CPiece::SetLength);
REPLACE(0x00590710, &CClonedPiece::GetDims);
REPLACE(0x00591510, &CPiece::SetAngle);
REPLACE(0x00591600, &CPiece::SetHighlight);
