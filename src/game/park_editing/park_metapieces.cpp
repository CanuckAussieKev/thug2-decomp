// The park editor's metapieces (ParkEditing::CPieceGroup and its two kinds): a list of descriptors, each a piece and its cell
// offset and turn within the metapiece. An abstract metapiece (a palette entry) names its pieces (source pieces or
// other abstract metapieces); a concrete one (placed in the park or held by the cursor) holds cloned pieces, which it
// moves and turns with itself.

#include <cstdint>

#include "game/park_editing/park.h"
#include "game/gfx/d3dx.h"

#include "game/core/mem.h"
#include "game/park_editing/park_nx.h"
#include "game/maths/mat4.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"

namespace ParkEditing {

static VtableSlot kMetaPieceVtable_slots[] = {&_purecall, &CPieceGroup::Teardown,
        &_purecall};
void** const kMetaPieceVtable = reinterpret_cast<void**>(kMetaPieceVtable_slots);
static VtableSlot kConcreteMetaPieceVtable_slots[] = {&CPlacedPieceGroup::GetPiece, &CPlacedPieceGroup::Teardown,
        &CPlacedPieceGroup::AddPiece};
void** const kConcreteMetaPieceVtable = reinterpret_cast<void**>(kConcreteMetaPieceVtable_slots);
static VtableSlot kAbstractMetaPieceVtable_slots[] = {&CPieceBlueprint::GetPiece, &CPlacedPieceGroup::Teardown,
        &CPieceBlueprint::AddPiece};
void** const kAbstractMetaPieceVtable = reinterpret_cast<void**>(kAbstractMetaPieceVtable_slots);

namespace {

// Degrees to radians, as the exe has it (0x00645464).
constexpr float kDegreesToRadians = 0.0174532924f;
// A little slack before a size is cut down to whole cells (0x00645448).
constexpr double kCellSlack = 2.0;
// The bounds' starting values (0x47C35000): far outside the park.
constexpr float kFarAway = 100000.0f;

// The extra descriptors' memory (the memory manager's, on the current heap).
SPieceGroupDesc* AllocateDescriptors(uint32_t count)
{
    return static_cast<SPieceGroupDesc*>(MemSys::Carve(count * sizeof(SPieceGroupDesc), 1, nullptr));
}

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library's): out = a * b.
void MultiplyMatrices(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

uint8_t Cells(double size, float cell) { return static_cast<uint8_t>(static_cast<int>((size + kCellSlack) / cell)); }

} // namespace

// --- CPieceGroup ---------------------------------------------------------------------------------------------------

bool CPieceGroup::IsSpawnMarker()
{
    switch (m_nameKey) {
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

SPieceGroupDesc* CPieceGroup::GetDescriptor(int index) { return index < 2 ? &m_desc[index] : &mp_extra_desc[index - 2]; }

void CPieceGroup::ToggleShown(bool visible)
{
    for (uint32_t i = 0; i < m_filledSlots; ++i) {
        auto* piece = static_cast<CParkTile*>(GetDescriptor(static_cast<int>(i))->piece);
        Scene::ChangeLive(Scene::LookUpSector(piece->m_sector), visible);
    }
}

int CPieceGroup::CountPieces()
{
    int count = 0;
    for (uint32_t i = 0; i < m_filledSlots; ++i) {
        uint32_t name = reinterpret_cast<uint32_t>(GetDescriptor(static_cast<int>(i))->piece);
        CPieceGroup* entry = CParkLayout::sSingleton()->GetAbstractMetaPiece(name, nullptr);
        if (entry->m_flagBits & kSector) // a single piece
            ++count;
        else
            count += static_cast<int>(entry->m_filledSlots);
    }
    return count;
}

bool CPieceGroup::FitsInsideRegion(SCellBox* area)
{
    const SCellBox& cells = m_cellFootprint;
    if (cells.x < area->x || cells.x + cells.w > area->x + area->w)
        return false;
    if (cells.z < area->z || cells.z + cells.l > area->z + area->l)
        return false;
    return true;
}

void CPieceGroup::SetQuarterTurns(int rot)
{
    const int turn = (rot - m_quarterTurns) & 3;
    m_quarterTurns = rot;
    if (turn & 1) { // a quarter turn: width and length swap
        uint8_t w = m_cellFootprint.w;
        m_cellFootprint.w = m_cellFootprint.l;
        m_cellFootprint.l = w;
    }
    // Every descriptor turned about the metapiece's corner, into new storage.
    SPieceGroupDesc* extra = mp_extra_desc ? AllocateDescriptors(m_max_entries - 2) : nullptr;
    // (the original leaves the unused ones of these two as stack leftovers; here they keep what they were)
    SPieceGroupDesc first[2] = {m_desc[0], m_desc[1]};
    for (uint32_t i = 0; i < m_filledSlots; ++i) {
        SPieceGroupDesc* to = i < 2 ? &first[i] : &extra[i - 2];
        const SPieceGroupDesc* from = i < 2 ? &m_desc[i] : &mp_extra_desc[i - 2];
        *to = *from;
        // The entry's own size in cells, as it lies (x, z).
        uint8_t across;
        uint8_t along;
        if (m_flagBits & kAbstract) {
            CPieceGroup* entry =
                CParkLayout::sSingleton()->GetAbstractMetaPiece(reinterpret_cast<uint32_t>(from->piece), nullptr);
            SCellBox size = entry->m_cellFootprint;
            if (from->rot & 1) {
                along = size.w;
                across = size.l;
            } else {
                along = size.l;
                across = size.w;
            }
        } else {
            SCellBox size;
            size.w = 0;
            size.l = 0;
            static_cast<CParkTile*>(from->piece)->GetFootprintSize(&size);
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
            to->z = static_cast<uint8_t>(m_cellFootprint.l - from->x - across);
            break;
        case 2:
            to->x = static_cast<uint8_t>(m_cellFootprint.w - from->x - across);
            to->z = static_cast<uint8_t>(m_cellFootprint.l - from->z - along);
            break;
        case 3:
            to->x = static_cast<uint8_t>(m_cellFootprint.w - from->z - along);
            to->z = from->x;
            break;
        }
        to->y = from->y;
        to->rot = static_cast<uint8_t>((from->rot + turn) & 3);
        to->piece = from->piece;
    }
    if (mp_extra_desc)
        MemSys::Dealloc(mp_extra_desc);
    mp_extra_desc = extra;
    m_desc[0] = first[0];
    m_desc[1] = first[1];
}

Maths::Vec4* CPieceGroup::GetRelativePosition(Maths::Vec4* out, uint32_t index)
{
    // The metapiece's half size (x and z).
    const float halfW = static_cast<float>(static_cast<double>(m_cellFootprint.w) * kCellWidth * 0.5);
    const float halfL = static_cast<float>(static_cast<double>(m_cellFootprint.l) * kCellLength * 0.5);
    const SPieceGroupDesc* entry = GetDescriptor(static_cast<int>(index));
    CParkTile* piece = GetPiece(index);
    Maths::Vec4 size1;
    Maths::Vec4 size2;
    const float sizeZ = piece->GetDimsTurned(&size1, entry->rot)->z;
    const float sizeX = piece->GetDimsTurned(&size2, entry->rot)->x;
    // The piece's centre in the metapiece, from the metapiece's centre.
    const double x = static_cast<double>(entry->x) * kCellWidth + static_cast<double>(sizeX) * 0.5;
    const double y = static_cast<double>(static_cast<uint8_t>(entry->y)) * kCellHeight; // (read unsigned)
    const float z = static_cast<float>(static_cast<double>(entry->z) * kCellLength + static_cast<double>(sizeZ) * 0.5);
    out->x = static_cast<float>(x - halfW);
    out->y = static_cast<float>(y);
    out->z = static_cast<float>(static_cast<double>(z) - halfL);
    out->w = 1.0f; // (1 less a stack leftover in the original)
    return out;
}

void CPieceGroup::Commission(int count)
{
    SPieceGroupDesc* extra = nullptr;
    if (count > 2) {
        extra = AllocateDescriptors(static_cast<uint32_t>(count - 2));
        // Growing: the extra entries kept.
        if (extra && mp_extra_desc && m_max_entries <= static_cast<uint32_t>(count) && m_max_entries > 2) {
            for (uint32_t i = 2; i < m_max_entries; ++i)
                extra[i - 2] = mp_extra_desc[i - 2];
        }
    }
    if (mp_extra_desc)
        MemSys::Dealloc(mp_extra_desc);
    mp_extra_desc = extra;
    m_max_entries = static_cast<uint32_t>(count);
}

void CPieceGroup::GetBounds(Maths::Vec4* max, Maths::Vec4* min)
{
    *min = {kFarAway, kFarAway, kFarAway, 0.0f};
    *max = {-kFarAway, -kFarAway, -kFarAway, 0.0f};
    for (uint32_t i = 0; i < m_filledSlots; ++i) {
        auto* piece = static_cast<CParkTile*>(GetDescriptor(static_cast<int>(i))->piece);
        Maths::Vec4 size;
        piece->MeasureSpan(&size);
        const double x = piece->m_atPoint.x;
        const float y = piece->m_atPoint.y;
        const double z = piece->m_atPoint.z;
        const double halfX = static_cast<double>(size.x) * 0.5;
        if (x - halfX < min->x)
            min->x = static_cast<float>(x - halfX);
        if (halfX + x > max->x)
            max->x = static_cast<float>(halfX + x);
        const double halfZ = static_cast<double>(size.z) * 0.5;
        if (z - halfZ < min->z)
            min->z = static_cast<float>(z - halfZ);
        if (halfZ + z > max->z)
            max->z = static_cast<float>(halfZ + z);
        if (y < min->y)
            min->y = y;
        if (static_cast<double>(size.y) + y > max->y)
            max->y = static_cast<float>(static_cast<double>(size.y) + y);
    }
}

bool CPieceGroup::Intersects(SCellBox area, uint32_t precise)
{
    const SCellBox& cells = m_cellFootprint;
    const int xStart = area.x;
    const int xEnd = area.x + area.w;
    const int yStart = area.y;
    const int yEnd = area.y + area.h;
    const int zStart = area.z;
    const int zEnd = area.z + area.l;
    if (cells.x + cells.w <= xStart || cells.x >= xEnd)
        return false;
    if (cells.y + cells.h <= yStart || cells.y >= yEnd)
        return false;
    if (cells.z + cells.l <= zStart || cells.z >= zEnd)
        return false;
    if (!static_cast<uint8_t>(precise))
        return true;
    // Precisely (when any piece is flagged setsFloor): by the cells of the pieces that are NOT flagged.
    bool anySetsFloor = false;
    for (uint32_t i = 0; i < m_filledSlots; ++i) {
        if (GetDescriptor(static_cast<int>(i))->setsFloor)
            anySetsFloor = true;
    }
    if (!anySetsFloor)
        return true;
    for (uint32_t i = 0; i < m_filledSlots; ++i) {
        const SPieceGroupDesc* entry = GetDescriptor(static_cast<int>(i));
        SCellBox size;
        size.w = 0;
        size.h = 0;
        size.l = 0;
        GetPiece(i)->GetFootprintSize(&size);
        const uint8_t x = static_cast<uint8_t>(entry->x + cells.x);
        const int8_t y = static_cast<int8_t>(entry->y + cells.y);
        const uint8_t z = static_cast<uint8_t>(entry->z + cells.z);
        if (entry->setsFloor)
            continue;
        if (x + size.w > xStart && y + size.h > yStart && z + size.l > zStart && x < xEnd && y < yEnd && z < zEnd)
            return true;
    }
    return false;
}

CPieceGroup* CPieceGroup::Teardown(unsigned int flags)
{
    m_vtable = kVtable;
    if (mp_extra_desc)
        MemSys::Dealloc(mp_extra_desc);
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

void CPieceGroup::GetPieceList(Scripts::CArray* out)
{
    out->ConfigureSizeKind(m_filledSlots, Scripts::QBVAL_STRUCT);
    for (uint32_t i = 0; i < m_filledSlots; ++i) {
        const SPieceGroupDesc* entry = GetDescriptor(static_cast<int>(i));
        Scripts::CStruct* memory = Scripts::NewStruct();
        Scripts::CStruct* piece = memory ? memory->PrepareAll() : nullptr;
        piece->PutNameKey(0u, reinterpret_cast<uint32_t>(entry->piece)); // (out of memory crashes, as in the original)
        Maths::Vec4 position;
        GetRelativePosition(&position, i);
        piece->PutVec3(0u, position.x, position.y, position.z);
        out->SetElement(i, reinterpret_cast<uint32_t>(piece));
    }
}

void CPieceGroup::DestroyBody()
{
    m_vtable = kVtable;
    if (mp_extra_desc)
        MemSys::Dealloc(mp_extra_desc);
}

void CPieceGroup::ChangePlace(Maths::Vec4 position, float angle)
{
    const float halfW = static_cast<float>(static_cast<double>(m_cellFootprint.w) * kCellWidth * 0.5);
    const float halfL = static_cast<float>(static_cast<double>(m_cellFootprint.l) * kCellLength * 0.5);
    if (!m_filledSlots)
        return;
    const float radians = static_cast<float>(static_cast<double>(angle) * kDegreesToRadians);
    for (uint32_t i = 0; i < m_filledSlots; ++i) {
        const SPieceGroupDesc* entry = GetDescriptor(static_cast<int>(i));
        auto* piece = static_cast<CParkTile*>(entry->piece);
        Maths::Vec4 size;
        piece->MeasureSpan(&size);
        // The piece's centre from the metapiece's centre (y read unsigned here), turned with the metapiece.
        Maths::Mat4 place;
        place.ResetToIdentity();
        place.row[3].x = static_cast<float>(static_cast<double>(entry->x) * kCellWidth + static_cast<double>(size.x) * 0.5 -
                                            halfW);
        place.row[3].y = static_cast<float>(static_cast<double>(static_cast<uint8_t>(entry->y)) * kCellHeight);
        place.row[3].z = static_cast<float>(static_cast<double>(entry->z) * kCellLength + static_cast<double>(size.z) * 0.5 -
                                            halfL);
        place.row[3].w = 1.0f;
        Maths::Mat4 turn;
        Maths::MakeYawMatrix(turn, radians);
        MultiplyMatrices(&place, &place, &turn);
        Maths::Vec4& at = place.row[3];
        at.x = static_cast<float>(static_cast<double>(position.x) + at.x);
        at.y = static_cast<float>(static_cast<double>(position.y) + at.y);
        at.z = static_cast<float>(static_cast<double>(position.z) + at.z);
        at.w = static_cast<float>(static_cast<double>(position.w) + at.w);
        piece->m_atPoint.x = at.x;
        piece->m_atPoint.y = at.y;
        piece->m_atPoint.z = at.z;
        piece->m_atPoint.w = at.w;
        Scene::PlaceAtAbsolutePoint(Scene::LookUpSector(piece->m_sector), &at);
        piece->SetAngle(static_cast<float>(static_cast<double>(entry->rot) * 90.0 + angle));
    }
}

void CPieceGroup::Spotlight(int on, int gap)
{
    for (uint32_t i = 0; i < m_filledSlots; ++i) {
        auto* piece = static_cast<CParkTile*>(GetDescriptor(static_cast<int>(i))->piece);
        if (piece->m_flagBits & CParkTile::kCloned)
            piece->SetHighlight(on != 0, gap != 0);
    }
}

void CPieceGroup::FinishPieces()
{
    Maths::Vec4 max;
    Maths::Vec4 min;
    GetBounds(&max, &min);
    const double sizeX = static_cast<double>(max.x) - min.x;
    const double sizeY = static_cast<double>(max.y) - min.y;
    const float sizeZ = static_cast<float>(static_cast<double>(max.z) - min.z);
    const double exactZ = static_cast<double>(max.z) - min.z;
    m_cellFootprint.l = Cells(exactZ, kCellLength);
    m_cellFootprint.h = Cells(sizeY, kCellHeight);
    m_cellFootprint.w = Cells(sizeX, kCellWidth);
    if (m_cellFootprint.h < 1)
        m_cellFootprint.h = 1;
    // The pieces moved so that the metapiece's centre is the origin (x and z).
    const double halfX = sizeX * 0.5;
    const double halfZ = static_cast<double>(sizeZ) * 0.5;
    for (uint32_t i = 0; i < m_filledSlots; ++i) {
        auto* piece = static_cast<CParkTile*>(GetDescriptor(static_cast<int>(i))->piece);
        piece->m_atPoint.x = static_cast<float>((static_cast<double>(piece->m_atPoint.x) - min.x) - halfX);
        piece->m_atPoint.z = static_cast<float>((static_cast<double>(piece->m_atPoint.z) - min.z) - halfZ);
    }
}

// --- CPlacedPieceGroup -------------------------------------------------------------------------------------------

CPlacedPieceGroup* CPlacedPieceGroup::MakeNew()
{
    m_cellFootprint = {0, 0, 0, 0, 0, 0};
    mp_extra_desc = nullptr;
    m_max_entries = 0;
    m_filledSlots = 0;
    m_vtable = kVtable;
    m_flagBits = kConcrete;
    m_quarterTurns = 0;
    return this;
}

CParkTile* CPlacedPieceGroup::GetPiece(uint32_t index)
{
    if (index >= m_filledSlots || static_cast<int>(index) < 0)
        return nullptr;
    return static_cast<CParkTile*>(GetDescriptor(static_cast<int>(index))->piece);
}

void CPlacedPieceGroup::AddPiece(CParkTile* piece, SCellBox* pos, int rot, bool setsFloor)
{
    SPieceGroupDesc* entry = GetDescriptor(static_cast<int>(m_filledSlots++));
    Maths::Vec4 size;
    piece->MeasureSpan(&size);
    // The piece's centre in the metapiece's cells.
    const double x = static_cast<double>(pos->x) * kCellWidth + static_cast<double>(size.x) * 0.5;
    const double y = static_cast<double>(pos->y) * kCellHeight;
    const double z = static_cast<double>(pos->z) * kCellLength + static_cast<double>(size.z) * 0.5;
    piece->m_atPoint.w = 1.0f;
    piece->m_atPoint.z = static_cast<float>(z);
    piece->m_atPoint.x = static_cast<float>(x);
    piece->m_atPoint.y = static_cast<float>(y);
    entry->piece = piece;
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    entry->setsFloor = setsFloor;
}

CPlacedPieceGroup* CPlacedPieceGroup::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// --- CPieceBlueprint -------------------------------------------------------------------------------------------

CPieceBlueprint* CPieceBlueprint::MakeNew()
{
    m_cellFootprint = {0, 0, 0, 0, 0, 0};
    mp_extra_desc = nullptr;
    m_max_entries = 0;
    m_filledSlots = 0;
    m_vtable = kVtable;
    m_flagBits = kAbstract;
    return this;
}

CParkTile* CPieceBlueprint::GetPiece(uint32_t index)
{
    if (index >= m_filledSlots || static_cast<int>(index) < 0)
        return nullptr;
    const SPieceGroupDesc* entry = GetDescriptor(static_cast<int>(index));
    CTileTemplate* source = CParkLayout::sSingleton()->m_builder->GetPieceSource(reinterpret_cast<uint32_t>(entry->piece));
    return source ? source->AsPiece() : nullptr;
}

namespace {

// The abstract metapiece's area grown to take in `pos` (with its size).
void GrowToTake(SCellBox& cells, const SCellBox* pos)
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

void CPieceBlueprint::AddPiece(CParkTile* piece, SCellBox* pos, int rot, uint8_t setsFloor)
{
    auto* source = (piece->m_flagBits & CParkTile::kSource) ? reinterpret_cast<CTileTemplate*>(piece) : nullptr;
    SPieceGroupDesc* entry = GetDescriptor(static_cast<int>(m_filledSlots++));
    if (!source)
        return; // (counted all the same)
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    reinterpret_cast<uint8_t&>(entry->setsFloor) = setsFloor; // (the byte as given)
    entry->piece = reinterpret_cast<void*>(source->m_label);
    GrowToTake(m_cellFootprint, pos);
    if (m_cellFootprint.h < 1)
        m_cellFootprint.h = 1;
}

void CPieceBlueprint::AddMetaPiece(CPieceGroup* meta, SCellBox* pos, int rot, uint8_t setsFloor)
{
    SPieceGroupDesc* entry = GetDescriptor(static_cast<int>(m_filledSlots++));
    if (!meta)
        return;
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    reinterpret_cast<uint8_t&>(entry->setsFloor) = setsFloor;
    entry->piece = reinterpret_cast<void*>(meta->m_nameKey);
    GrowToTake(m_cellFootprint, pos);
}

SPieceGroupDesc* CPieceBlueprint::GetEntry(SPieceGroupDesc* out, int index)
{
    int n = 0; // the entries counted so far, a nested metapiece's pieces each one
    // (when `index` is past the end the original returns a stack leftover; here an empty entry)
    SPieceGroupDesc result = {};
    for (uint32_t i = 0; i < m_filledSlots; ++i) {
        const SPieceGroupDesc* outer = GetDescriptor(static_cast<int>(i));
        CPieceGroup* entry =
            CParkLayout::sSingleton()->GetAbstractMetaPiece(reinterpret_cast<uint32_t>(outer->piece), nullptr);
        if (entry->m_flagBits & kSector) { // a single piece
            if (n == index) {
                *out = *outer;
                return out;
            }
            ++n;
            continue;
        }
        for (uint32_t j = 0; j < entry->m_filledSlots; ++j) {
            const SPieceGroupDesc* inner = entry->GetDescriptor(static_cast<int>(j));
            if (n != index) {
                ++n;
                continue;
            }
            // The nested piece placed in this metapiece: turned with its metapiece (the turn not wrapped).
            result = *inner;
            result.rot = static_cast<uint8_t>(inner->rot + outer->rot);
            result.y = static_cast<int8_t>(inner->y + outer->y);
            const SCellBox& here = m_cellFootprint;
            const SCellBox& nested = entry->m_cellFootprint;
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

} // namespace Ed

using ParkEditing::CPieceBlueprint;
using ParkEditing::CPlacedPieceGroup;
using ParkEditing::CPieceGroup;
