// The park editor's metapieces (Ed::CMetaPiece and its two kinds): a list of descriptors, each a piece and its cell
// offset and turn within the metapiece. An abstract metapiece (a palette entry) names its pieces (source pieces or
// other abstract metapieces); a concrete one (placed in the park or held by the cursor) holds cloned pieces, which it
// moves and turns with itself.

#include "game/ed/park.h"

#include "game/core/mem.h"
#include "game/ed/park_nx.h"
#include "game/mth/matrix.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

// Degrees to radians, as the exe has it (0x00645464).
constexpr float kDegreesToRadians = 0.0174532924f;
// A little slack before a size is cut down to whole cells (0x00645448).
constexpr double kCellSlack = 2.0;
// The bounds' starting values (0x47C35000): far outside the park.
constexpr float kFarAway = 100000.0f;

// The extra descriptors' memory (the memory manager's, on the current heap).
SMetaDescriptor* AllocateDescriptors(uint32_t count)
{
    return static_cast<SMetaDescriptor*>(Mem::Allocate(count * sizeof(SMetaDescriptor), 1, nullptr));
}

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library's): out = a * b.
void MultiplyMatrices(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}

uint8_t Cells(double size, float cell) { return static_cast<uint8_t>(static_cast<int>((size + kCellSlack) / cell)); }

} // namespace

// --- CMetaPiece ---------------------------------------------------------------------------------------------------

bool CMetaPiece::IsRestartPiece()
{
    switch (m_name_checksum) {
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

SMetaDescriptor* CMetaPiece::GetDescriptor(int index) { return index < 2 ? &m_desc[index] : &mp_extra_desc[index - 2]; }

void CMetaPiece::SetVisibility(bool visible)
{
    for (uint32_t i = 0; i < m_num_used_entries; ++i) {
        auto* piece = static_cast<CPiece*>(GetDescriptor(static_cast<int>(i))->piece);
        Scene::SetActive(Scene::GetSector(piece->m_sector), visible);
    }
}

int CMetaPiece::CountPieces()
{
    int count = 0;
    for (uint32_t i = 0; i < m_num_used_entries; ++i) {
        uint32_t name = reinterpret_cast<uint32_t>(GetDescriptor(static_cast<int>(i))->piece);
        CMetaPiece* entry = CParkManager::sInstance()->GetAbstractMetaPiece(name, nullptr);
        if (entry->m_flags & kSector) // a single piece
            ++count;
        else
            count += static_cast<int>(entry->m_num_used_entries);
    }
    return count;
}

bool CMetaPiece::IsCompletelyWithinArea(GridDims* area)
{
    const GridDims& cells = m_cell_area;
    if (cells.x < area->x || cells.x + cells.w > area->x + area->w)
        return false;
    if (cells.z < area->z || cells.z + cells.l > area->z + area->l)
        return false;
    return true;
}

void CMetaPiece::SetRot(int rot)
{
    const int turn = (rot - m_rot) & 3;
    m_rot = rot;
    if (turn & 1) { // a quarter turn: width and length swap
        uint8_t w = m_cell_area.w;
        m_cell_area.w = m_cell_area.l;
        m_cell_area.l = w;
    }
    // Every descriptor turned about the metapiece's corner, into new storage.
    SMetaDescriptor* extra = mp_extra_desc ? AllocateDescriptors(m_max_entries - 2) : nullptr;
    // (the original leaves the unused ones of these two as stack leftovers; here they keep what they were)
    SMetaDescriptor first[2] = {m_desc[0], m_desc[1]};
    for (uint32_t i = 0; i < m_num_used_entries; ++i) {
        SMetaDescriptor* to = i < 2 ? &first[i] : &extra[i - 2];
        const SMetaDescriptor* from = i < 2 ? &m_desc[i] : &mp_extra_desc[i - 2];
        *to = *from;
        // The entry's own size in cells, as it lies (x, z).
        uint8_t across;
        uint8_t along;
        if (m_flags & kAbstract) {
            CMetaPiece* entry =
                CParkManager::sInstance()->GetAbstractMetaPiece(reinterpret_cast<uint32_t>(from->piece), nullptr);
            GridDims size = entry->m_cell_area;
            if (from->rot & 1) {
                along = size.w;
                across = size.l;
            } else {
                along = size.l;
                across = size.w;
            }
        } else {
            GridDims size;
            size.w = 0;
            size.l = 0;
            static_cast<CPiece*>(from->piece)->GetCellDims(&size);
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
            to->z = static_cast<uint8_t>(m_cell_area.l - from->x - across);
            break;
        case 2:
            to->x = static_cast<uint8_t>(m_cell_area.w - from->x - across);
            to->z = static_cast<uint8_t>(m_cell_area.l - from->z - along);
            break;
        case 3:
            to->x = static_cast<uint8_t>(m_cell_area.w - from->z - along);
            to->z = from->x;
            break;
        }
        to->y = from->y;
        to->rot = static_cast<uint8_t>((from->rot + turn) & 3);
        to->piece = from->piece;
    }
    if (mp_extra_desc)
        Mem::Free(mp_extra_desc);
    mp_extra_desc = extra;
    m_desc[0] = first[0];
    m_desc[1] = first[1];
}

Mth::Vector* CMetaPiece::GetRelativePosition(Mth::Vector* out, uint32_t index)
{
    // The metapiece's half size (x and z).
    const float halfW = static_cast<float>(static_cast<double>(m_cell_area.w) * kCellWidth * 0.5);
    const float halfL = static_cast<float>(static_cast<double>(m_cell_area.l) * kCellLength * 0.5);
    const SMetaDescriptor* entry = GetDescriptor(static_cast<int>(index));
    CPiece* piece = GetPiece(index);
    Mth::Vector size1;
    Mth::Vector size2;
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

void CMetaPiece::Initialize(int count)
{
    SMetaDescriptor* extra = nullptr;
    if (count > 2) {
        extra = AllocateDescriptors(static_cast<uint32_t>(count - 2));
        // Growing: the extra entries kept.
        if (extra && mp_extra_desc && m_max_entries <= static_cast<uint32_t>(count) && m_max_entries > 2) {
            for (uint32_t i = 2; i < m_max_entries; ++i)
                extra[i - 2] = mp_extra_desc[i - 2];
        }
    }
    if (mp_extra_desc)
        Mem::Free(mp_extra_desc);
    mp_extra_desc = extra;
    m_max_entries = static_cast<uint32_t>(count);
}

void CMetaPiece::GetBounds(Mth::Vector* max, Mth::Vector* min)
{
    *min = {kFarAway, kFarAway, kFarAway, 0.0f};
    *max = {-kFarAway, -kFarAway, -kFarAway, 0.0f};
    for (uint32_t i = 0; i < m_num_used_entries; ++i) {
        auto* piece = static_cast<CPiece*>(GetDescriptor(static_cast<int>(i))->piece);
        Mth::Vector size;
        piece->GetDims(&size);
        const double x = piece->m_pos.x;
        const float y = piece->m_pos.y;
        const double z = piece->m_pos.z;
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

bool CMetaPiece::Intersects(GridDims area, uint32_t precise)
{
    const GridDims& cells = m_cell_area;
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
    for (uint32_t i = 0; i < m_num_used_entries; ++i) {
        if (GetDescriptor(static_cast<int>(i))->setsFloor)
            anySetsFloor = true;
    }
    if (!anySetsFloor)
        return true;
    for (uint32_t i = 0; i < m_num_used_entries; ++i) {
        const SMetaDescriptor* entry = GetDescriptor(static_cast<int>(i));
        GridDims size;
        size.w = 0;
        size.h = 0;
        size.l = 0;
        GetPiece(i)->GetCellDims(&size);
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

CMetaPiece* CMetaPiece::Destroy(unsigned int flags)
{
    m_vtable = reinterpret_cast<void**>(kVtable);
    if (mp_extra_desc)
        Mem::Free(mp_extra_desc);
    if (flags & 1)
        Mem::Free(this);
    return this;
}

void CMetaPiece::GetPieceList(Script::CArray* out)
{
    out->SetSizeAndType(m_num_used_entries, Script::ESYMBOLTYPE_STRUCTURE);
    for (uint32_t i = 0; i < m_num_used_entries; ++i) {
        const SMetaDescriptor* entry = GetDescriptor(static_cast<int>(i));
        Script::CStruct* memory = Script::NewStruct();
        Script::CStruct* piece = memory ? memory->Init() : nullptr;
        piece->AddChecksum(0u, reinterpret_cast<uint32_t>(entry->piece)); // (out of memory crashes, as in the original)
        Mth::Vector position;
        GetRelativePosition(&position, i);
        piece->AddVector(0u, position.x, position.y, position.z);
        out->SetElement(i, reinterpret_cast<uint32_t>(piece));
    }
}

void CMetaPiece::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kVtable);
    if (mp_extra_desc)
        Mem::Free(mp_extra_desc);
}

void CMetaPiece::SetPosition(Mth::Vector position, float angle)
{
    const float halfW = static_cast<float>(static_cast<double>(m_cell_area.w) * kCellWidth * 0.5);
    const float halfL = static_cast<float>(static_cast<double>(m_cell_area.l) * kCellLength * 0.5);
    if (!m_num_used_entries)
        return;
    const float radians = static_cast<float>(static_cast<double>(angle) * kDegreesToRadians);
    for (uint32_t i = 0; i < m_num_used_entries; ++i) {
        const SMetaDescriptor* entry = GetDescriptor(static_cast<int>(i));
        auto* piece = static_cast<CPiece*>(entry->piece);
        Mth::Vector size;
        piece->GetDims(&size);
        // The piece's centre from the metapiece's centre (y read unsigned here), turned with the metapiece.
        Mth::Matrix place;
        place.Ident();
        place.row[3].x = static_cast<float>(static_cast<double>(entry->x) * kCellWidth + static_cast<double>(size.x) * 0.5 -
                                            halfW);
        place.row[3].y = static_cast<float>(static_cast<double>(static_cast<uint8_t>(entry->y)) * kCellHeight);
        place.row[3].z = static_cast<float>(static_cast<double>(entry->z) * kCellLength + static_cast<double>(size.z) * 0.5 -
                                            halfL);
        place.row[3].w = 1.0f;
        Mth::Matrix turn;
        Mth::CreateRotateYMatrix(turn, radians);
        MultiplyMatrices(&place, &place, &turn);
        Mth::Vector& at = place.row[3];
        at.x = static_cast<float>(static_cast<double>(position.x) + at.x);
        at.y = static_cast<float>(static_cast<double>(position.y) + at.y);
        at.z = static_cast<float>(static_cast<double>(position.z) + at.z);
        at.w = static_cast<float>(static_cast<double>(position.w) + at.w);
        piece->m_pos.x = at.x;
        piece->m_pos.y = at.y;
        piece->m_pos.z = at.z;
        piece->m_pos.w = at.w;
        Scene::SetWorldPosition(Scene::GetSector(piece->m_sector), &at);
        piece->SetAngle(static_cast<float>(static_cast<double>(entry->rot) * 90.0 + angle));
    }
}

void CMetaPiece::Highlight(int on, int gap)
{
    for (uint32_t i = 0; i < m_num_used_entries; ++i) {
        auto* piece = static_cast<CPiece*>(GetDescriptor(static_cast<int>(i))->piece);
        if (piece->m_flags & CPiece::kCloned)
            piece->SetHighlight(on != 0, gap != 0);
    }
}

void CMetaPiece::FinishPieces()
{
    Mth::Vector max;
    Mth::Vector min;
    GetBounds(&max, &min);
    const double sizeX = static_cast<double>(max.x) - min.x;
    const double sizeY = static_cast<double>(max.y) - min.y;
    const float sizeZ = static_cast<float>(static_cast<double>(max.z) - min.z);
    const double exactZ = static_cast<double>(max.z) - min.z;
    m_cell_area.l = Cells(exactZ, kCellLength);
    m_cell_area.h = Cells(sizeY, kCellHeight);
    m_cell_area.w = Cells(sizeX, kCellWidth);
    if (m_cell_area.h < 1)
        m_cell_area.h = 1;
    // The pieces moved so that the metapiece's centre is the origin (x and z).
    const double halfX = sizeX * 0.5;
    const double halfZ = static_cast<double>(sizeZ) * 0.5;
    for (uint32_t i = 0; i < m_num_used_entries; ++i) {
        auto* piece = static_cast<CPiece*>(GetDescriptor(static_cast<int>(i))->piece);
        piece->m_pos.x = static_cast<float>((static_cast<double>(piece->m_pos.x) - min.x) - halfX);
        piece->m_pos.z = static_cast<float>((static_cast<double>(piece->m_pos.z) - min.z) - halfZ);
    }
}

// --- CConcreteMetaPiece -------------------------------------------------------------------------------------------

CConcreteMetaPiece* CConcreteMetaPiece::Construct()
{
    m_cell_area = {0, 0, 0, 0, 0, 0};
    mp_extra_desc = nullptr;
    m_max_entries = 0;
    m_num_used_entries = 0;
    m_vtable = reinterpret_cast<void**>(kVtable);
    m_flags = kConcrete;
    m_rot = 0;
    return this;
}

CPiece* CConcreteMetaPiece::GetPiece(uint32_t index)
{
    if (index >= m_num_used_entries || static_cast<int>(index) < 0)
        return nullptr;
    return static_cast<CPiece*>(GetDescriptor(static_cast<int>(index))->piece);
}

void CConcreteMetaPiece::AddPiece(CPiece* piece, GridDims* pos, int rot, bool setsFloor)
{
    SMetaDescriptor* entry = GetDescriptor(static_cast<int>(m_num_used_entries++));
    Mth::Vector size;
    piece->GetDims(&size);
    // The piece's centre in the metapiece's cells.
    const double x = static_cast<double>(pos->x) * kCellWidth + static_cast<double>(size.x) * 0.5;
    const double y = static_cast<double>(pos->y) * kCellHeight;
    const double z = static_cast<double>(pos->z) * kCellLength + static_cast<double>(size.z) * 0.5;
    piece->m_pos.w = 1.0f;
    piece->m_pos.z = static_cast<float>(z);
    piece->m_pos.x = static_cast<float>(x);
    piece->m_pos.y = static_cast<float>(y);
    entry->piece = piece;
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    entry->setsFloor = setsFloor;
}

CConcreteMetaPiece* CConcreteMetaPiece::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// --- CAbstractMetaPiece -------------------------------------------------------------------------------------------

CAbstractMetaPiece* CAbstractMetaPiece::Construct()
{
    m_cell_area = {0, 0, 0, 0, 0, 0};
    mp_extra_desc = nullptr;
    m_max_entries = 0;
    m_num_used_entries = 0;
    m_vtable = reinterpret_cast<void**>(kVtable);
    m_flags = kAbstract;
    return this;
}

CPiece* CAbstractMetaPiece::GetPiece(uint32_t index)
{
    if (index >= m_num_used_entries || static_cast<int>(index) < 0)
        return nullptr;
    const SMetaDescriptor* entry = GetDescriptor(static_cast<int>(index));
    CSourcePiece* source = CParkManager::sInstance()->mp_generator->GetPieceSource(reinterpret_cast<uint32_t>(entry->piece));
    return source ? source->AsPiece() : nullptr;
}

namespace {

// The abstract metapiece's area grown to take in `pos` (with its size).
void GrowToTake(GridDims& cells, const GridDims* pos)
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

void CAbstractMetaPiece::AddPiece(CPiece* piece, GridDims* pos, int rot, uint8_t setsFloor)
{
    auto* source = (piece->m_flags & CPiece::kSource) ? reinterpret_cast<CSourcePiece*>(piece) : nullptr;
    SMetaDescriptor* entry = GetDescriptor(static_cast<int>(m_num_used_entries++));
    if (!source)
        return; // (counted all the same)
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    reinterpret_cast<uint8_t&>(entry->setsFloor) = setsFloor; // (the byte as given)
    entry->piece = reinterpret_cast<void*>(source->m_name);
    GrowToTake(m_cell_area, pos);
    if (m_cell_area.h < 1)
        m_cell_area.h = 1;
}

void CAbstractMetaPiece::AddMetaPiece(CMetaPiece* meta, GridDims* pos, int rot, uint8_t setsFloor)
{
    SMetaDescriptor* entry = GetDescriptor(static_cast<int>(m_num_used_entries++));
    if (!meta)
        return;
    entry->x = pos->x;
    entry->y = pos->y;
    entry->z = pos->z;
    entry->rot = static_cast<uint8_t>(rot);
    reinterpret_cast<uint8_t&>(entry->setsFloor) = setsFloor;
    entry->piece = reinterpret_cast<void*>(meta->m_name_checksum);
    GrowToTake(m_cell_area, pos);
}

SMetaDescriptor* CAbstractMetaPiece::GetEntry(SMetaDescriptor* out, int index)
{
    int n = 0; // the entries counted so far, a nested metapiece's pieces each one
    // (when `index` is past the end the original returns a stack leftover; here an empty entry)
    SMetaDescriptor result = {};
    for (uint32_t i = 0; i < m_num_used_entries; ++i) {
        const SMetaDescriptor* outer = GetDescriptor(static_cast<int>(i));
        CMetaPiece* entry =
            CParkManager::sInstance()->GetAbstractMetaPiece(reinterpret_cast<uint32_t>(outer->piece), nullptr);
        if (entry->m_flags & kSector) { // a single piece
            if (n == index) {
                *out = *outer;
                return out;
            }
            ++n;
            continue;
        }
        for (uint32_t j = 0; j < entry->m_num_used_entries; ++j) {
            const SMetaDescriptor* inner = entry->GetDescriptor(static_cast<int>(j));
            if (n != index) {
                ++n;
                continue;
            }
            // The nested piece placed in this metapiece: turned with its metapiece (the turn not wrapped).
            result = *inner;
            result.rot = static_cast<uint8_t>(inner->rot + outer->rot);
            result.y = static_cast<int8_t>(inner->y + outer->y);
            const GridDims& here = m_cell_area;
            const GridDims& nested = entry->m_cell_area;
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

using Ed::CAbstractMetaPiece;
using Ed::CConcreteMetaPiece;
using Ed::CMetaPiece;

REPLACE_SOURCE_ONLY(0x005902E0, &CMetaPiece::Cast); // 3 bytes (folded with many empty constructors)
REPLACE(0x005902F0, &CMetaPiece::IsRestartPiece);
REPLACE(0x00590340, &CMetaPiece::GetDescriptor);
REPLACE(0x00590360, &CConcreteMetaPiece::GetPiece);
REPLACE(0x005903A0, &CMetaPiece::SetVisibility);
REPLACE(0x00590400, &CMetaPiece::CountPieces);
REPLACE(0x00590750, &CMetaPiece::IsCompletelyWithinArea);
REPLACE(0x005907B0, &CMetaPiece::SetRot);
REPLACE(0x005909C0, &CMetaPiece::GetRelativePosition);
REPLACE(0x00590AD0, &CMetaPiece::Initialize);
REPLACE(0x00590B60, &CConcreteMetaPiece::Construct);
REPLACE(0x00590B90, &CMetaPiece::GetBounds);
REPLACE(0x00590CE0, &CMetaPiece::Intersects);
REPLACE(0x00590ED0, &CConcreteMetaPiece::AddPiece);
REPLACE(0x00590FA0, &CAbstractMetaPiece::Construct);
REPLACE(0x00590FD0, &CAbstractMetaPiece::GetPiece);
REPLACE(0x00591020, &CAbstractMetaPiece::AddPiece);
REPLACE(0x00591100, &CAbstractMetaPiece::AddMetaPiece);
REPLACE(0x005911D0, &CAbstractMetaPiece::GetEntry);
REPLACE(0x00591670, &CMetaPiece::Destroy);
REPLACE(0x005916B0, &CMetaPiece::GetPieceList);
REPLACE(0x005917E0, &CMetaPiece::DestroyBody);
REPLACE(0x00591800, &CMetaPiece::SetPosition);
REPLACE(0x00591A50, &CMetaPiece::Highlight);
REPLACE(0x00591AB0, &CMetaPiece::FinishPieces);
REPLACE(0x00591BB0, &CConcreteMetaPiece::Destroy);
