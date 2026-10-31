// The park editor's clipboards (0x0058B7E0-0x0058C984): a copied area - the metapieces in it (CClipboardEntry: the
// palette piece, its cell relative to the park, its turn and size) and its rails (the rail editor's) - shown at the
// cursor, turned with it, and pasted. See park.h.

#include <cstdint>

#include "game/core/mem.h"
#include "game/core/slab_bank.h"
#include "game/park_editing/park.h"
#include "game/park_editing/park_editor.h"
#include "game/park_editing/rail_editor.h"
#include "game/maths/x87_math.h"

namespace ParkEditing {

Maths::Vec4 g_railOrigin;
MemSys::PoolPair g_railPools;
MemSys::PoolPair g_clipboardEntryPools;

namespace {

constexpr uint32_t kRiserBelow = 0x4A0D22DB; // riser_below
constexpr uint32_t kRiserFloor = 0xB0A98147; // riser_floor
constexpr int kNoHeight = -1000;
constexpr float kQuarterTurnDegrees = 90.0f; // 0x00645EB0
constexpr float kRadiansPerDegree = 0.017453292f; // 0x00645464

// The clipboard entries' pool pair (CPoolable, 0x18-byte items).
MemSys::PoolPair& EntryPools() { return g_clipboardEntryPools; }

// The rail editor (0x004F4970) and its rails (CEditedRail objects linked by their first word).
void* RailEditor() { return CRailAuthoring::Sole(); }
// 0x004F7230: copies of the rails in the world box (x0, z0)-(x1, z1); 0x004F6BF0: how many rail points are free;
// 0x004F72C0: a new rail.
void* CopyRailsInBox(void* editor, float x0, float z0, float x1, float z1)
{
    return static_cast<CRailAuthoring*>(editor)->CopyRailsInside(x0, z0, x1, z1);
}
int FreeRailPoints(void* editor)
{
    (void)editor;
    return CRailAuthoring::FreePoints();
}
void* NewRail(void* editor) { return static_cast<CRailAuthoring*>(editor)->NewRail(); }
void* FollowRailLink(void* rail) { return *static_cast<void**>(rail); }
// A rail's points (0x004F3B90); a copy of `from` (0x004F6F40); placed at `corner` and `cursor` turned by `angle`
// (0x004F4370); two transforms with an origin, a position and an angle (0x004F61C0, 0x004F6210); hidden (0x004F4220,
// 0x004F41C0); its destructor body (0x004F70F0); its pool pair (0x007CCD40).
int RailPointCount(void* rail) { return static_cast<CEditedRail*>(rail)->RailPointTotal(); }
void DuplicateRailPath(void* rail, void* from)
{
    static_cast<CEditedRail*>(rail)->CopyFrom(static_cast<const CEditedRail*>(from));
}
void PlaceRail(void* rail, Maths::Vec4* corner, Maths::Vec4* cursor, float angle)
{
    static_cast<CEditedRail*>(rail)->Xform(corner, cursor, angle);
}
void TransformRail1(void* rail, const Maths::Vec4* origin, const Maths::Vec4* position, float angle)
{
    static_cast<CEditedRail*>(rail)->DrawSegments(origin, position, angle);
}
void TransformRail2(void* rail, const Maths::Vec4* origin, const Maths::Vec4* position, float angle)
{
    static_cast<CEditedRail*>(rail)->DrawPosts(origin, position, angle);
}
void HideRail(void* rail)
{
    static_cast<CEditedRail*>(rail)->DeletePosts();
    static_cast<CEditedRail*>(rail)->DeleteSegments();
}
void DestroyRail(void* rail) { static_cast<CEditedRail*>(rail)->Clear(); }
MemSys::PoolPair& RailPools() { return g_railPools; }
const Maths::Vec4* RailOrigin() { return &g_railOrigin; } // (zero)

// A preview metapiece taken out of the park.
void DestroyPreview(CClipboardEntry* entry)
{
    entry->m_preview->m_flagBits &= ~CPieceGroup::kCursor;
    CParkLayout::sSingleton()->FreePlacedPiece(entry->m_preview, 0);
    CParkLayout::sSingleton()->RebuildParkGeometry(0);
    entry->m_preview = nullptr;
}

} // namespace

// --- CClipboardEntry ---

// 0x0058B800
void CClipboardEntry::FetchExtent(SCellBox* area, uint8_t centreX, uint8_t centreZ, CBuildPointer* cursor)
{
    SCellBox size = SCellBox::Make(0, 0, 0, 0, 0, 0);
    uint8_t width = 1, length = 1;
    if (m_pieceGroup) {
        size = m_pieceGroup->m_cellFootprint;
        width = size.w;
        length = size.l;
    }
    if (((cursor->m_quarterTurns + m_quarterTurns) & 3) & 1) {
        area->w = length;
        area->l = width;
    } else {
        area->w = width;
        area->l = length;
    }
    area->h = 1;
    // Its offset from the copy's centre, turned with the cursor (a turned piece's corner moves to its other side).
    int dx = m_posX - centreX;
    int dz = m_depthCell - centreZ;
    switch (cursor->m_quarterTurns) {
    case 1: {
        const int x = dz;
        dz = -dx;
        dx = x;
        break;
    }
    case 2:
        dx = -dx;
        dz = -dz;
        break;
    case 3: {
        const int x = -dz;
        dz = dx;
        dx = x;
        break;
    }
    default:
        break;
    }
    switch (cursor->m_quarterTurns) {
    case 1:
        dz += 1 - m_w;
        break;
    case 2:
        dx += 1 - m_w;
        dz += 1 - m_l;
        break;
    case 3:
        dx += 1 - m_l;
        break;
    default:
        break;
    }
    area->x = static_cast<uint8_t>(dx + cursor->m_cellFootprint.x);
    area->z = static_cast<uint8_t>(dz + cursor->m_cellFootprint.z);
    area->y = m_y;
}

// 0x0058B940
bool CClipboardEntry::CanPlace(uint8_t centreX, uint8_t centreZ, CBuildPointer* cursor)
{
    SCellBox area = SCellBox::Make(0, 0, 0, 0, 0, 0);
    FetchExtent(&area, centreX, centreZ, cursor);
    area.y = 0;
    return CParkLayout::sSingleton()->CanChangeFloorHere(area, true);
}

// 0x0058B9C0
void CClipboardEntry::Graft(uint8_t centreX, int height, uint8_t centreZ, CBuildPointer* cursor)
{
    SCellBox area = SCellBox::Make(0, 0, 0, 0, 0, 0);
    FetchExtent(&area, centreX, centreZ, cursor);
    int top = m_y + height;
    if (m_pieceGroup && (m_pieceGroup->m_flagBits & CPieceGroup::kBuiltShell))
        top += m_pieceGroup->m_cellFootprint.h;
    if (top >= 16)
        top = 15;
    area.y = static_cast<int8_t>(top);
    CParkLayout* manager = CParkLayout::sSingleton();
    // The floor raised to it (at most 41 steps; a flat floor already there stops it).
    bool flat = false;
    int floor = manager->GetGroundLevel(area, &flat);
    for (int steps = 0; floor <= top && steps <= 40; ++steps) {
        if (flat && floor == top)
            break;
        manager->RaiseOrLowerFloor(area, 1);
        flat = false;
        floor = manager->GetGroundLevel(area, &flat);
    }
    if (m_pieceGroup && !(m_pieceGroup->m_flagBits & CPieceGroup::kBuiltShell)) {
        CPieceGroup* meta = manager->MakePlacedPiece(m_pieceGroup, 0);
        meta->SetQuarterTurns((cursor->m_quarterTurns + m_quarterTurns) & 3);
        manager->PlaceMetaPieceInPark(area, meta);
    }
}

// 0x0058BB20
int CClipboardEntry::FetchTallness(uint8_t centreX, uint8_t centreZ, CBuildPointer* cursor)
{
    SCellBox area = SCellBox::Make(0, 0, 0, 0, 0, 0);
    FetchExtent(&area, centreX, centreZ, cursor);
    bool flat;
    return CParkLayout::sSingleton()->GetGroundLevel(area, &flat);
}

// 0x0058BBA0
void CClipboardEntry::GlowOverlappingPieces(uint8_t centreX, uint8_t centreZ, CBuildPointer* cursor, int on)
{
    SCellBox area = SCellBox::Make(0, 0, 0, 0, 0, 0);
    FetchExtent(&area, centreX, centreZ, cursor);
    area.y = 0;
    MapListHolder metas(CParkLayout::sSingleton()->CollectPiecesInRegion(area));
    for (CMapListNode* node = metas.list; node; node = node->m_following) {
        CPieceGroup* meta = node->GetPieceGroup()->Cast();
        if (!(meta->m_flagBits & CPieceGroup::kBuiltShell))
            meta->Spotlight(on, 0);
    }
}

// 0x0058BCA0
void CClipboardEntry::ShowPreview(uint8_t centreX, int /*height*/, uint8_t centreZ, Maths::Vec4 position, float angle)
{
    if (!m_pieceGroup)
        return;
    // The middle of its cells from the copy's centre (in the world's units), turned by `angle`.
    const int dx = m_posX - centreX;
    const int dz = m_depthCell - centreZ;
    const float x = static_cast<float>(((m_w - 1) * 0.5 + dx) * kCellWidth);
    const double z = ((m_l - 1) * 0.5 + dz) * kGridStepAlong;
    const double radians = static_cast<double>(angle) * kRadiansPerDegree;
    const float sine = static_cast<float>(X87::Sin(radians));
    const double cosine = X87::Cos(radians);
    const float turnedX = static_cast<float>(z * sine + x * cosine);
    const double turnedZ = z * cosine - static_cast<double>(x) * sine;
    // (The original adds an offset whose w it never sets - stack garbage - to the position's: the position's w kept.)
    Maths::Vec4 at;
    at.w = position.w;
    at.z = static_cast<float>(position.z + turnedZ);
    at.x = position.x + turnedX;
    at.y = static_cast<float>(m_y * static_cast<double>(kGridStepUp) + position.y);
    if (!m_preview) {
        m_preview = CParkLayout::sSingleton()->MakePlacedPiece(m_pieceGroup, 0x204);
        m_preview->m_flagBits |= CPieceGroup::kCursor;
    }
    m_preview->ChangePlace(at, static_cast<float>(m_quarterTurns * static_cast<double>(kQuarterTurnDegrees) + angle));
}

// --- CClipboard ---

// 0x0058B7E0
bool CClipboard::IsVacant() { return m_metas ? false : m_railGroups == nullptr; }

// 0x0058BDF0
CClipboard* CClipboard::MakeNew()
{
    m_editSpan.AssignValue(0, 0, 0, 0, 0, 0);
    m_loCellZ = 0xFF;
    m_loCellX = 0xFF;
    m_metas = nullptr;
    m_hiCellZ = 0;
    m_hiCellX = 0;
    m_following = nullptr;
    m_editSpan.AssignValue(0, 0, 0, 0, 0, 0);
    m_railGroups = nullptr;
    return this;
}

// 0x0058BE30
int CClipboard::FetchTallness(CBuildPointer* cursor)
{
    const uint8_t centreX = static_cast<uint8_t>((m_loCellX + m_hiCellX) / 2);
    const uint8_t centreZ = static_cast<uint8_t>((m_loCellZ + m_hiCellZ) / 2);
    int highest = kNoHeight;
    for (CClipboardEntry* entry = m_metas; entry; entry = entry->m_following) {
        const int height = entry->FetchTallness(centreX, centreZ, cursor);
        if (height > highest)
            highest = height;
    }
    return highest == kNoHeight ? 0 : highest;
}

// 0x0058BEB0
void CClipboard::GlowOverlappingPieces(CBuildPointer* cursor, int on)
{
    const uint8_t centreX = static_cast<uint8_t>((m_loCellX + m_hiCellX) / 2);
    const uint8_t centreZ = static_cast<uint8_t>((m_loCellZ + m_hiCellZ) / 2);
    for (CClipboardEntry* entry = m_metas; entry; entry = entry->m_following)
        entry->GlowOverlappingPieces(centreX, centreZ, cursor, on);
}

// 0x0058BF10
void CClipboard::FetchExtent(CBuildPointer* cursor, SCellBox* area)
{
    const int left = m_loCellX - static_cast<uint8_t>((m_loCellX + m_hiCellX) / 2);
    const int near = m_loCellZ - static_cast<uint8_t>((m_loCellZ + m_hiCellZ) / 2);
    int dx = 0, dz = 0;
    switch (cursor->m_quarterTurns) {
    case 0:
        area->l = m_editSpan.l;
        area->w = m_editSpan.w;
        area->h = 1;
        dx = left;
        dz = near;
        break;
    case 1:
        area->w = m_editSpan.l;
        area->l = m_editSpan.w;
        area->h = 1;
        dx = near;
        dz = 1 - m_editSpan.w - left;
        break;
    case 2:
        area->w = m_editSpan.w;
        area->l = m_editSpan.l;
        area->h = 1;
        dx = 1 - m_editSpan.w - left;
        dz = 1 - m_editSpan.l - near;
        break;
    case 3:
        area->w = m_editSpan.l;
        area->l = m_editSpan.w;
        area->h = 1;
        dx = 1 - m_editSpan.l - near;
        dz = left;
        break;
    default:
        break;
    }
    area->z = static_cast<uint8_t>(dz + cursor->m_cellFootprint.z);
    area->x = static_cast<uint8_t>(dx + cursor->m_cellFootprint.x);
    area->y = 0;
}

// 0x0058C050
Maths::Vec4* CClipboard::GetCentre(Maths::Vec4* centre)
{
    // The cell in the middle of the copy (one high, at the copy's floor), and half its size on.
    const uint8_t centreX = static_cast<uint8_t>((m_loCellX + m_hiCellX) / 2);
    const uint8_t centreZ = static_cast<uint8_t>((m_loCellZ + m_hiCellZ) / 2);
    SCellBox cell = SCellBox::Make(0, 0, 0, 0, 0, 0);
    cell.y = m_editSpan.y;
    cell.w = 1;
    cell.h = 1;
    cell.l = 1;
    cell.z = centreZ;
    cell.x = centreX;
    Maths::Vec4 size;
    Maths::Vec4 corner;
    const Maths::Vec4* world = CParkLayout::sSingleton()->GridDimsToWorld(&corner, &cell, &size);
    const Maths::Vec4 at = *world;
    centre->x = static_cast<float>(size.x * 0.5 + at.x);
    centre->y = at.y;
    centre->z = static_cast<float>(size.z * 0.5 + at.z);
    centre->w = at.w;
    return centre;
}

// 0x0058C130
void CClipboard::PreviewPieces(Maths::Vec4 position, float angle, int height)
{
    const uint8_t centreX = static_cast<uint8_t>((m_loCellX + m_hiCellX) / 2);
    const uint8_t centreZ = static_cast<uint8_t>((m_loCellZ + m_hiCellZ) / 2);
    for (CClipboardEntry* entry = m_metas; entry; entry = entry->m_following)
        entry->ShowPreview(centreX, height, centreZ, position, angle);
    Maths::Vec4 centre;
    GetCentre(&centre);
    for (void* rail = m_railGroups; rail; rail = FollowRailLink(rail)) {
        TransformRail1(rail, &centre, &position, angle);
        TransformRail2(rail, &centre, &position, angle);
    }
}

// 0x0058C200
void CClipboard::FreeAllPieceGroups()
{
    for (CClipboardEntry* entry = m_metas; entry; entry = entry->m_following) {
        if (entry->m_preview)
            DestroyPreview(entry);
    }
    for (void* rail = m_railGroups; rail; rail = FollowRailLink(rail))
        HideRail(rail);
}

// 0x0058C270
bool CClipboard::AddRow(CPieceGroup* meta, int dy)
{
    const SCellBox area = meta->m_cellFootprint;
    const int8_t y = static_cast<int8_t>(area.y + dy);
    CPieceGroup* source = CParkLayout::sSingleton()->FetchMetaTemplate(meta->m_nameKey, nullptr);
    // (Already in it: done.)
    for (CClipboardEntry* entry = m_metas; entry; entry = entry->m_following) {
        if (entry->m_pieceGroup == source && entry->m_posX == area.x && entry->m_y == y && entry->m_depthCell == area.z &&
            entry->m_w == area.w && entry->m_l == area.l && entry->m_quarterTurns == meta->m_quarterTurns)
            return true;
    }
    MemSys::PoolPair& pools = EntryPools();
    MemSys::SlabBank* pool = pools.pools[pools.current]; // (not null-checked: as the original)
    if (pool->m_takenCount == pool->m_num)
        return false;
    auto* entry = static_cast<CClipboardEntry*>(pools.Carve(sizeof(CClipboardEntry)));
    if (entry) {
        entry->m_pieceGroup = nullptr;
        entry->m_preview = nullptr;
        entry->m_depthCell = 0;
        entry->m_y = 0;
        entry->m_posX = 0;
        entry->m_w = 0;
        entry->m_l = 0;
        entry->m_quarterTurns = 0;
        entry->m_following = nullptr;
    }
    entry->m_y = y; // (not null-checked: as the original)
    entry->m_depthCell = area.z;
    entry->m_pieceGroup = source;
    entry->m_posX = area.x;
    entry->m_w = area.w;
    entry->m_l = area.l;
    // The copy's bounds take in both corners.
    auto takeIn = [this](uint8_t x, uint8_t z) {
        if (x < m_loCellX)
            m_loCellX = x;
        if (x > m_hiCellX)
            m_hiCellX = x;
        if (z < m_loCellZ)
            m_loCellZ = z;
        if (z > m_hiCellZ)
            m_hiCellZ = z;
    };
    takeIn(area.x, area.z);
    takeIn(static_cast<uint8_t>(area.w + area.x - 1), static_cast<uint8_t>(area.l + area.z - 1));
    entry->m_quarterTurns = meta->m_quarterTurns;
    entry->m_following = m_metas;
    m_metas = entry;
    return true;
}

// 0x0058C430 (also reached through 0x0058C980)
void CClipboard::Teardown()
{
    MemSys::PoolPair& pools = EntryPools();
    for (CClipboardEntry* entry = m_metas; entry;) {
        CClipboardEntry* next = entry->m_following;
        if (entry->m_preview)
            DestroyPreview(entry);
        pools.Dealloc(entry);
        entry = next;
    }
    m_metas = nullptr;
    m_loCellZ = 0xFF;
    m_loCellX = 0xFF;
    m_hiCellZ = 0;
    m_hiCellX = 0;
    m_editSpan.AssignValue(0, 0, 0, 0, 0, 0);
    for (void* rail = m_railGroups; rail;) {
        void* next = FollowRailLink(rail);
        DestroyRail(rail);
        RailPools().Dealloc(rail);
        rail = next;
    }
    m_railGroups = nullptr;
}

// 0x0058C590
bool CClipboard::CopyPickedToClipboard(SCellBox area)
{
    m_editSpan = area;
    CParkLayout* manager = CParkLayout::sSingleton();
    // The lowest piece in the area (not above the floor: 0 at most) - risers below count 15 higher, floor risers 1.
    int lowest = 0;
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            for (CMapListNode* node = manager->GetCellChain(x, z); node; node = node->m_following) {
                CPieceGroup* meta = node->GetPieceGroup()->Cast();
                int y = meta->m_cellFootprint.y;
                if (meta->m_nameKey == kRiserBelow)
                    y += 15;
                else if (meta->m_nameKey == kRiserFloor)
                    y += 1;
                if (y < lowest)
                    lowest = y;
            }
        }
    }
    const int dy = -lowest;
    // Every metapiece wholly inside (not restart points or risers below), raised by that.
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            for (CMapListNode* node = manager->GetCellChain(x, z); node; node = node->m_following) {
                CPieceGroup* meta = node->GetPieceGroup()->Cast();
                if (meta->IsSpawnMarker() || !meta->FitsInsideRegion(&area) ||
                    meta->m_nameKey == kRiserBelow)
                    continue;
                if (!AddRow(meta, dy)) {
                    Teardown();
                    return false;
                }
            }
        }
    }
    // The rails over it.
    Maths::Vec4 size;
    Maths::Vec4 corner;
    const Maths::Vec4* world = manager->GridDimsToWorld(&corner, &area, &size);
    const float x0 = world->x;
    const float z0 = world->z;
    m_railGroups = CopyRailsInBox(RailEditor(), x0, z0, size.x + x0, size.z + z0);
    return true;
}

// 0x0058C780
bool CClipboard::ShowAt(CBuildPointer* cursor)
{
    CParkBuilderMode* editor = CParkBuilderMode::Fabricate(false);
    if (!editor->MainHeapHasRoom(m_hiCellX - m_loCellX + 1, m_hiCellZ - m_loCellZ + 1)) {
        CParkBuilderMode::Release();
        return false;
    }
    const uint8_t centreX = static_cast<uint8_t>((m_loCellX + m_hiCellX) / 2);
    const uint8_t centreZ = static_cast<uint8_t>((m_loCellZ + m_hiCellZ) / 2);
    for (CClipboardEntry* entry = m_metas; entry; entry = entry->m_following) {
        if (!entry->CanPlace(centreX, centreZ, cursor)) {
            CParkBuilderMode::Release();
            return false;
        }
    }
    int railPoints = 0;
    for (void* rail = m_railGroups; rail; rail = FollowRailLink(rail))
        railPoints += RailPointCount(rail);
    if (railPoints > FreeRailPoints(RailEditor())) {
        CParkBuilderMode::Release();
        return false;
    }
    for (CClipboardEntry* entry = m_metas; entry; entry = entry->m_following)
        entry->Graft(centreX, cursor->m_clipboardHeight, centreZ, cursor);
    Maths::Vec4 centre;
    GetCentre(&centre);
    Maths::Vec4 at = *editor->GetCursorPos();
    for (void* rail = m_railGroups; rail; rail = FollowRailLink(rail)) {
        void* copy = NewRail(RailEditor());
        DuplicateRailPath(copy, rail);
        PlaceRail(copy, &centre, &at, static_cast<float>(cursor->m_quarterTurns * static_cast<double>(kQuarterTurnDegrees)));
        TransformRail1(copy, RailOrigin(), RailOrigin(), 0.0f);
        TransformRail2(copy, RailOrigin(), RailOrigin(), 0.0f);
    }
    CParkBuilderMode::Release();
    return true;
}

} // namespace Ed
