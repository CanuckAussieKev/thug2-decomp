// The park editor's pieces (ParkEditing::CParkTile, CTileTemplate, CCopiedPiece) and cell boxes (SCellBox): a source piece is
// one of the editor's own sectors (its name and size); the pieces placed in the park are cloned from them and moved,
// turned, stretched, shown or tinted through their sectors.

#include <cstdint>

#include "game/ed/park.h"
#include "game/gfx/d3dx.h"
#include "game/core/code_address.h"

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/ed/park_nx.h"
#include "game/mth/matrix.h"

namespace ParkEditing {

static VtableSlot kPieceVtable_slots[] = {&_purecall, &CParkTile::Destroy};
void** const kPieceVtable = reinterpret_cast<void**>(kPieceVtable_slots);
static VtableSlot kSourcePieceVtable_slots[] = {&CTileTemplate::MeasureSpan, &CTileTemplate::Destroy};
void** const kSourcePieceVtable = reinterpret_cast<void**>(kSourcePieceVtable_slots);
static VtableSlot kClonedPieceVtable_slots[] = {&CCopiedPiece::MeasureSpan, &CTileTemplate::Destroy};
void** const kClonedPieceVtable = reinterpret_cast<void**>(kClonedPieceVtable_slots);

namespace {

// A little slack before a size is cut down to whole cells (0x00645448).
constexpr double kCellSlack = 2.0;
// Degrees to radians, as the exe has it (0x00645464).
constexpr float kDegreesToRadians = 0.0174532924f;

// A piece's size in whole cells along one axis (truncated).
uint8_t Cells(float size, float cell) { return static_cast<uint8_t>(static_cast<int>((size + kCellSlack) / cell)); }

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library's): out = a * b.
void MultiplyMatrices(Maths::Matrix* out, const Maths::Matrix* a, const Maths::Matrix* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

} // namespace

// --- SCellBox ----------------------------------------------------------------------------------------------------

SCellBox* SCellBox::Set(uint8_t x_, int8_t y_, uint8_t z_, uint8_t w_, uint8_t h_, uint8_t l_)
{
    x = x_;
    y = y_;
    z = z_;
    w = w_;
    h = h_;
    l = l_;
    return this;
}

uint8_t* SCellBox::Byte(int index) { return reinterpret_cast<uint8_t*>(this) + index; }

void SCellBox::StretchVertically() { h = static_cast<uint8_t>(0x7F - y); }

void SCellBox::RaiseCeilingToLimit()
{
    y = -100;
    h = 200;
}

void SCellBox::DumpToLog() const
{
    DebugOut::Printf("pos=(%d,%d,%d) dims=(%d,%d,%d)\n", x, static_cast<int>(y), z, w, h, l); // "pos=(%d,%d,%d) dims=..."
}

// --- CParkTile -------------------------------------------------------------------------------------------------------

void CParkTile::DestroyBody() { m_vtable = kVtable; }

CParkTile* CParkTile::Destroy(unsigned int flags)
{
    m_vtable = kVtable;
    if (flags & 1)
        MemSys::Free(this);
    return this;
}

uint32_t CParkTile::GetId() { return m_id; }

void CParkTile::SetQuarterTurns(int rot, int update)
{
    m_quarterTurns = rot;
    if (update == 1)
        Scene::SpinAroundUp(Scene::LookUpSector(m_sector), rot);
}

void CParkTile::SetActive(bool active) { Scene::SetActive(Scene::LookUpSector(m_sector), active); }

void CParkTile::SetVisible(bool visible) { Scene::ToggleShown(Scene::LookUpSector(m_sector), visible ? 0xFF : 0); }

void CParkTile::GetFootprintSize(SCellBox* dims)
{
    Maths::Vector size;
    MeasureSpan(&size);
    dims->l = Cells(size.z, kCellLength);
    dims->h = Cells(size.y, kCellHeight);
    dims->w = Cells(size.x, kCellWidth);
}

Maths::Vector* CParkTile::GetDimsTurned(Maths::Vector* out, int rot)
{
    CParkTile* source = (m_flags & kSource) ? this : mp_source->AsPiece();
    // (The original asks the source three times, once per component; it's a plain getter. Its w: the source's when
    // the turn is odd, a stack leftover when it's even - the source's here.)
    Maths::Vector size;
    source->MeasureSpan(&size);
    const bool across = (rot & 1) != 0;
    out->x = across ? size.z : size.x;
    out->y = size.y;
    out->z = across ? size.x : size.z;
    out->w = size.w;
    return out;
}

void CParkTile::SetPosition(Maths::Vector* position, int update)
{
    m_pos.x = position->x;
    m_pos.y = position->y;
    m_pos.z = position->z;
    m_pos.w = position->w;
    if (update == 1)
        Scene::PlaceAtAbsolutePoint(Scene::LookUpSector(m_sector), position);
}

void CParkTile::SetLength(float length)
{
    uint8_t* sector = Scene::LookUpSector(m_sector);
    const bool across = (m_quarterTurns & 1) != 0;
    // (w is a stack leftover in the original)
    Maths::Vector scale = {across ? 1.0f : length, 1.0f, across ? length : 1.0f, 1.0f};
    Scene::ChangeScale(sector, &scale);
}

void CParkTile::SetAngle(float degrees)
{
    uint8_t* geometry = Scene::GetGeometry(Scene::LookUpSector(m_sector));
    if (!geometry)
        return;
    Maths::Matrix orientation;
    orientation.ResetToIdentity();
    Maths::Matrix turn;
    Maths::MakeYawMatrix(turn, static_cast<float>(static_cast<double>(degrees) * kDegreesToRadians));
    MultiplyMatrices(&orientation, &orientation, &turn);
    Scene::SetOrientation(geometry, &orientation);
}

void CParkTile::SetHighlight(bool on, bool gap)
{
    uint8_t* sector = Scene::LookUpSector(m_sector);
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

// --- CTileTemplate / CCopiedPiece ---------------------------------------------------------------------------------

CTileTemplate* CTileTemplate::Construct()
{
    m_name = 0;
    m_following = nullptr;
    m_vtable = kVtable;
    m_flags = CParkTile::kSource;
    m_size = {0.0f, 0.0f, 0.0f, 1.0f};
    m_20 = 1;
    m_triggerScript = 0;
    m_numRailPoints = 0;
    m_numRailSegments = 0;
    return this;
}

Maths::Vector* CTileTemplate::MeasureSpan(Maths::Vector* out)
{
    out->x = m_size.x;
    out->y = m_size.y;
    out->z = m_size.z;
    out->w = m_size.w;
    return out;
}

CTileTemplate* CTileTemplate::Destroy(unsigned int flags)
{
    AsPiece()->DestroyBody();
    if (flags & 1)
        MemSys::Free(this);
    return this;
}

CCopiedPiece* CCopiedPiece::Construct()
{
    m_sector = 0;
    m_following = nullptr;
    m_vtable = kVtable;
    m_id = 0;
    m_flags = kCloned;
    mp_source = nullptr;
    return this;
}

Maths::Vector* CCopiedPiece::MeasureSpan(Maths::Vector* out)
{
    const Maths::Vector& size = mp_source->m_size;
    const bool across = (m_quarterTurns & 1) != 0;
    out->x = across ? size.z : size.x;
    out->y = size.y;
    out->z = across ? size.x : size.z;
    out->w = size.w; // (a stack leftover in the original)
    return out;
}

} // namespace Ed

using ParkEditing::CCopiedPiece;
using ParkEditing::CParkTile;
using ParkEditing::CTileTemplate;
using ParkEditing::SCellBox;
