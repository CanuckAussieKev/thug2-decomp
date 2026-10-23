// The park editor's clipboards (0x0058B7E0-0x0058C984): a copied area - the metapieces in it (CClipboardEntry: the
// palette piece, its cell relative to the park, its turn and size) and its rails (the rail editor's) - shown at the
// cursor, turned with it, and pasted. See park.h.

#include <cstdint>

#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/ed/park.h"
#include "game/ed/park_editor.h"
#include "game/mth/x87_math.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

constexpr uint32_t kRiserBelow = 0x4A0D22DB; // riser_below
constexpr uint32_t kRiserFloor = 0xB0A98147; // riser_floor
constexpr int kNoHeight = -1000;
constexpr float kQuarterTurnDegrees = 90.0f; // 0x00645EB0
constexpr float kDegreesToRadians = 0.017453292f; // 0x00645464

// The clipboard entries' pool pair (CPoolable, 0x18-byte items).
Mem::PoolPair& EntryPools() { return GameVariable<Mem::PoolPair>(0x007CE5C4); }

// The rail editor (0x004F4970) and its rails (Obj::CEditedRail-like objects linked by their first word; not
// rewritten yet: called at their addresses).
void* RailEditor() { return GameFunction<void*(__cdecl*)()>(0x004F4970)(); }
// 0x004F7230: copies of the rails in the world box (x0, z0)-(x1, z1); 0x004F6BF0: how many rail points are free;
// 0x004F72C0: a new rail.
void* CopyRailsInBox(void* editor, float x0, float z0, float x1, float z1)
{
    return GameFunction<void*(__thiscall*)(void*, float, float, float, float)>(0x004F7230)(editor, x0, z0, x1, z1);
}
int FreeRailPoints(void* editor) { return GameFunction<int(__thiscall*)(void*)>(0x004F6BF0)(editor); }
void* NewRail(void* editor) { return GameFunction<void*(__thiscall*)(void*)>(0x004F72C0)(editor); }
void* NextRail(void* rail) { return *static_cast<void**>(rail); }
// A rail's points (0x004F3B90); a copy of `from` (0x004F6F40); placed at `corner` and `cursor` turned by `angle`
// (0x004F4370); two transforms with an origin, a position and an angle (0x004F61C0, 0x004F6210); hidden (0x004F4220,
// 0x004F41C0); its destructor body (0x004F70F0); its pool pair (0x007CCD40).
int RailPointCount(void* rail) { return GameFunction<int(__thiscall*)(void*)>(0x004F3B90)(rail); }
void CopyRail(void* rail, void* from) { GameFunction<void(__thiscall*)(void*, void*)>(0x004F6F40)(rail, from); }
void PlaceRail(void* rail, Mth::Vector* corner, Mth::Vector* cursor, float angle)
{
    GameFunction<void(__thiscall*)(void*, Mth::Vector*, Mth::Vector*, float)>(0x004F4370)(rail, corner, cursor, angle);
}
void TransformRail1(void* rail, const Mth::Vector* origin, const Mth::Vector* position, float angle)
{
    GameFunction<void(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, float)>(0x004F61C0)(rail, origin,
                                                                                                      position, angle);
}
void TransformRail2(void* rail, const Mth::Vector* origin, const Mth::Vector* position, float angle)
{
    GameFunction<void(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, float)>(0x004F6210)(rail, origin,
                                                                                                      position, angle);
}
void HideRail(void* rail)
{
    GameFunction<void(__thiscall*)(void*)>(0x004F4220)(rail);
    GameFunction<void(__thiscall*)(void*)>(0x004F41C0)(rail);
}
void DestroyRail(void* rail) { GameFunction<void(__thiscall*)(void*)>(0x004F70F0)(rail); }
Mem::PoolPair& RailPools() { return GameVariable<Mem::PoolPair>(0x007CCD40); }
const Mth::Vector* RailOrigin() { return &GameVariable<Mth::Vector>(0x007CCD30); } // (zero)

// A preview metapiece taken out of the park.
void DestroyPreview(CClipboardEntry* entry)
{
    entry->mp_preview->m_flags &= ~CMetaPiece::kCursor;
    CParkManager::sInstance()->DestroyConcreteMeta(entry->mp_preview, 0);
    CParkManager::sInstance()->RebuildParkGeometry(0);
    entry->mp_preview = nullptr;
}

} // namespace

// --- CClipboardEntry ---

// 0x0058B800
void CClipboardEntry::GetArea(GridDims* area, uint8_t centreX, uint8_t centreZ, CCursor* cursor)
{
    GridDims size = GridDims::Make(0, 0, 0, 0, 0, 0);
    uint8_t width = 1, length = 1;
    if (mp_meta) {
        size = mp_meta->m_cell_area;
        width = size.w;
        length = size.l;
    }
    if (((cursor->m_rot + m_rot) & 3) & 1) {
        area->w = length;
        area->l = width;
    } else {
        area->w = width;
        area->l = length;
    }
    area->h = 1;
    // Its offset from the copy's centre, turned with the cursor (a turned piece's corner moves to its other side).
    int dx = m_x - centreX;
    int dz = m_z - centreZ;
    switch (cursor->m_rot) {
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
    switch (cursor->m_rot) {
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
    area->x = static_cast<uint8_t>(dx + cursor->m_cell_area.x);
    area->z = static_cast<uint8_t>(dz + cursor->m_cell_area.z);
    area->y = m_y;
}

// 0x0058B940
bool CClipboardEntry::CanPlace(uint8_t centreX, uint8_t centreZ, CCursor* cursor)
{
    GridDims area = GridDims::Make(0, 0, 0, 0, 0, 0);
    GetArea(&area, centreX, centreZ, cursor);
    area.y = 0;
    return CParkManager::sInstance()->CanChangeFloorHere(area, true);
}

// 0x0058B9C0
void CClipboardEntry::Paste(uint8_t centreX, int height, uint8_t centreZ, CCursor* cursor)
{
    GridDims area = GridDims::Make(0, 0, 0, 0, 0, 0);
    GetArea(&area, centreX, centreZ, cursor);
    int top = m_y + height;
    if (mp_meta && (mp_meta->m_flags & CMetaPiece::kInnerShell))
        top += mp_meta->m_cell_area.h;
    if (top >= 16)
        top = 15;
    area.y = static_cast<int8_t>(top);
    CParkManager* manager = CParkManager::sInstance();
    // The floor raised to it (at most 41 steps; a flat floor already there stops it).
    bool flat = false;
    int floor = manager->GetFloorHeight(area, &flat);
    for (int steps = 0; floor <= top && steps <= 40; ++steps) {
        if (flat && floor == top)
            break;
        manager->ChangeFloorHeight(area, 1);
        flat = false;
        floor = manager->GetFloorHeight(area, &flat);
    }
    if (mp_meta && !(mp_meta->m_flags & CMetaPiece::kInnerShell)) {
        CMetaPiece* meta = manager->CreateConcreteMeta(mp_meta, 0);
        meta->SetRot((cursor->m_rot + m_rot) & 3);
        manager->AddMetaPieceToPark(area, meta);
    }
}

// 0x0058BB20
int CClipboardEntry::GetHeight(uint8_t centreX, uint8_t centreZ, CCursor* cursor)
{
    GridDims area = GridDims::Make(0, 0, 0, 0, 0, 0);
    GetArea(&area, centreX, centreZ, cursor);
    bool flat;
    return CParkManager::sInstance()->GetFloorHeight(area, &flat);
}

// 0x0058BBA0
void CClipboardEntry::HighlightIntersectingMetas(uint8_t centreX, uint8_t centreZ, CCursor* cursor, int on)
{
    GridDims area = GridDims::Make(0, 0, 0, 0, 0, 0);
    GetArea(&area, centreX, centreZ, cursor);
    area.y = 0;
    MapListHolder metas(CParkManager::sInstance()->GetAreaMetaPieces(area));
    for (CMapListNode* node = metas.list; node; node = node->mp_next) {
        CMetaPiece* meta = node->GetMeta()->Cast();
        if (!(meta->m_flags & CMetaPiece::kInnerShell))
            meta->Highlight(on, 0);
    }
}

// 0x0058BCA0
void CClipboardEntry::ShowPreview(uint8_t centreX, int /*height*/, uint8_t centreZ, Mth::Vector position, float angle)
{
    if (!mp_meta)
        return;
    // The middle of its cells from the copy's centre (in the world's units), turned by `angle`.
    const int dx = m_x - centreX;
    const int dz = m_z - centreZ;
    const float x = static_cast<float>(((m_w - 1) * 0.5 + dx) * kCellWidth);
    const double z = ((m_l - 1) * 0.5 + dz) * kCellLength;
    const double radians = static_cast<double>(angle) * kDegreesToRadians;
    const float sine = static_cast<float>(X87::Sin(radians));
    const double cosine = X87::Cos(radians);
    const float turnedX = static_cast<float>(z * sine + x * cosine);
    const double turnedZ = z * cosine - static_cast<double>(x) * sine;
    // (The original adds an offset whose w it never sets - stack garbage - to the position's: the position's w kept.)
    Mth::Vector at;
    at.w = position.w;
    at.z = static_cast<float>(position.z + turnedZ);
    at.x = position.x + turnedX;
    at.y = static_cast<float>(m_y * static_cast<double>(kCellHeight) + position.y);
    if (!mp_preview) {
        mp_preview = CParkManager::sInstance()->CreateConcreteMeta(mp_meta, 0x204);
        mp_preview->m_flags |= CMetaPiece::kCursor;
    }
    mp_preview->SetPosition(at, static_cast<float>(m_rot * static_cast<double>(kQuarterTurnDegrees) + angle));
}

// --- CClipboard ---

// 0x0058B7E0
bool CClipboard::IsEmpty() { return mp_metas ? false : mp_rails == nullptr; }

// 0x0058BDF0
CClipboard* CClipboard::Construct()
{
    m_area.Set(0, 0, 0, 0, 0, 0);
    m_min_z = 0xFF;
    m_min_x = 0xFF;
    mp_metas = nullptr;
    m_max_z = 0;
    m_max_x = 0;
    mp_next = nullptr;
    m_area.Set(0, 0, 0, 0, 0, 0);
    mp_rails = nullptr;
    return this;
}

// 0x0058BE30
int CClipboard::GetHeight(CCursor* cursor)
{
    const uint8_t centreX = static_cast<uint8_t>((m_min_x + m_max_x) / 2);
    const uint8_t centreZ = static_cast<uint8_t>((m_min_z + m_max_z) / 2);
    int highest = kNoHeight;
    for (CClipboardEntry* entry = mp_metas; entry; entry = entry->mp_next) {
        const int height = entry->GetHeight(centreX, centreZ, cursor);
        if (height > highest)
            highest = height;
    }
    return highest == kNoHeight ? 0 : highest;
}

// 0x0058BEB0
void CClipboard::HighlightIntersectingMetas(CCursor* cursor, int on)
{
    const uint8_t centreX = static_cast<uint8_t>((m_min_x + m_max_x) / 2);
    const uint8_t centreZ = static_cast<uint8_t>((m_min_z + m_max_z) / 2);
    for (CClipboardEntry* entry = mp_metas; entry; entry = entry->mp_next)
        entry->HighlightIntersectingMetas(centreX, centreZ, cursor, on);
}

// 0x0058BF10
void CClipboard::GetArea(CCursor* cursor, GridDims* area)
{
    const int left = m_min_x - static_cast<uint8_t>((m_min_x + m_max_x) / 2);
    const int near = m_min_z - static_cast<uint8_t>((m_min_z + m_max_z) / 2);
    int dx = 0, dz = 0;
    switch (cursor->m_rot) {
    case 0:
        area->l = m_area.l;
        area->w = m_area.w;
        area->h = 1;
        dx = left;
        dz = near;
        break;
    case 1:
        area->w = m_area.l;
        area->l = m_area.w;
        area->h = 1;
        dx = near;
        dz = 1 - m_area.w - left;
        break;
    case 2:
        area->w = m_area.w;
        area->l = m_area.l;
        area->h = 1;
        dx = 1 - m_area.w - left;
        dz = 1 - m_area.l - near;
        break;
    case 3:
        area->w = m_area.l;
        area->l = m_area.w;
        area->h = 1;
        dx = 1 - m_area.l - near;
        dz = left;
        break;
    default:
        break;
    }
    area->z = static_cast<uint8_t>(dz + cursor->m_cell_area.z);
    area->x = static_cast<uint8_t>(dx + cursor->m_cell_area.x);
    area->y = 0;
}

// 0x0058C050
Mth::Vector* CClipboard::GetCentre(Mth::Vector* centre)
{
    // The cell in the middle of the copy (one high, at the copy's floor), and half its size on.
    const uint8_t centreX = static_cast<uint8_t>((m_min_x + m_max_x) / 2);
    const uint8_t centreZ = static_cast<uint8_t>((m_min_z + m_max_z) / 2);
    GridDims cell = GridDims::Make(0, 0, 0, 0, 0, 0);
    cell.y = m_area.y;
    cell.w = 1;
    cell.h = 1;
    cell.l = 1;
    cell.z = centreZ;
    cell.x = centreX;
    Mth::Vector size;
    Mth::Vector corner;
    const Mth::Vector* world = CParkManager::sInstance()->GridDimsToWorld(&corner, &cell, &size);
    const Mth::Vector at = *world;
    centre->x = static_cast<float>(size.x * 0.5 + at.x);
    centre->y = at.y;
    centre->z = static_cast<float>(size.z * 0.5 + at.z);
    centre->w = at.w;
    return centre;
}

// 0x0058C130
void CClipboard::ShowMetas(Mth::Vector position, float angle, int height)
{
    const uint8_t centreX = static_cast<uint8_t>((m_min_x + m_max_x) / 2);
    const uint8_t centreZ = static_cast<uint8_t>((m_min_z + m_max_z) / 2);
    for (CClipboardEntry* entry = mp_metas; entry; entry = entry->mp_next)
        entry->ShowPreview(centreX, height, centreZ, position, angle);
    Mth::Vector centre;
    GetCentre(&centre);
    for (void* rail = mp_rails; rail; rail = NextRail(rail)) {
        TransformRail1(rail, &centre, &position, angle);
        TransformRail2(rail, &centre, &position, angle);
    }
}

// 0x0058C200
void CClipboard::DestroyMetas()
{
    for (CClipboardEntry* entry = mp_metas; entry; entry = entry->mp_next) {
        if (entry->mp_preview)
            DestroyPreview(entry);
    }
    for (void* rail = mp_rails; rail; rail = NextRail(rail))
        HideRail(rail);
}

// 0x0058C270
bool CClipboard::AddEntry(CMetaPiece* meta, int dy)
{
    const GridDims area = meta->m_cell_area;
    const int8_t y = static_cast<int8_t>(area.y + dy);
    CMetaPiece* source = CParkManager::sInstance()->GetAbstractMetaPiece(meta->m_name_checksum, nullptr);
    // (Already in it: done.)
    for (CClipboardEntry* entry = mp_metas; entry; entry = entry->mp_next) {
        if (entry->mp_meta == source && entry->m_x == area.x && entry->m_y == y && entry->m_z == area.z &&
            entry->m_w == area.w && entry->m_l == area.l && entry->m_rot == meta->m_rot)
            return true;
    }
    Mem::PoolPair& pools = EntryPools();
    Mem::Pool* pool = pools.pools[pools.current]; // (not null-checked: as the original)
    if (pool->m_used == pool->m_count)
        return false;
    auto* entry = static_cast<CClipboardEntry*>(pools.Allocate(sizeof(CClipboardEntry)));
    if (entry) {
        entry->mp_meta = nullptr;
        entry->mp_preview = nullptr;
        entry->m_z = 0;
        entry->m_y = 0;
        entry->m_x = 0;
        entry->m_w = 0;
        entry->m_l = 0;
        entry->m_rot = 0;
        entry->mp_next = nullptr;
    }
    entry->m_y = y; // (not null-checked: as the original)
    entry->m_z = area.z;
    entry->mp_meta = source;
    entry->m_x = area.x;
    entry->m_w = area.w;
    entry->m_l = area.l;
    // The copy's bounds take in both corners.
    auto takeIn = [this](uint8_t x, uint8_t z) {
        if (x < m_min_x)
            m_min_x = x;
        if (x > m_max_x)
            m_max_x = x;
        if (z < m_min_z)
            m_min_z = z;
        if (z > m_max_z)
            m_max_z = z;
    };
    takeIn(area.x, area.z);
    takeIn(static_cast<uint8_t>(area.w + area.x - 1), static_cast<uint8_t>(area.l + area.z - 1));
    entry->m_rot = meta->m_rot;
    entry->mp_next = mp_metas;
    mp_metas = entry;
    return true;
}

// 0x0058C430 (also reached through 0x0058C980)
void CClipboard::Destroy()
{
    Mem::PoolPair& pools = EntryPools();
    for (CClipboardEntry* entry = mp_metas; entry;) {
        CClipboardEntry* next = entry->mp_next;
        if (entry->mp_preview)
            DestroyPreview(entry);
        pools.Free(entry);
        entry = next;
    }
    mp_metas = nullptr;
    m_min_z = 0xFF;
    m_min_x = 0xFF;
    m_max_z = 0;
    m_max_x = 0;
    m_area.Set(0, 0, 0, 0, 0, 0);
    for (void* rail = mp_rails; rail;) {
        void* next = NextRail(rail);
        DestroyRail(rail);
        RailPools().Free(rail);
        rail = next;
    }
    mp_rails = nullptr;
}

// 0x0058C590
bool CClipboard::CopySelectionToClipboard(GridDims area)
{
    m_area = area;
    CParkManager* manager = CParkManager::sInstance();
    // The lowest piece in the area (not above the floor: 0 at most) - risers below count 15 higher, floor risers 1.
    int lowest = 0;
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            for (CMapListNode* node = manager->GetMapListNode(x, z); node; node = node->mp_next) {
                CMetaPiece* meta = node->GetMeta()->Cast();
                int y = meta->m_cell_area.y;
                if (meta->m_name_checksum == kRiserBelow)
                    y += 15;
                else if (meta->m_name_checksum == kRiserFloor)
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
            for (CMapListNode* node = manager->GetMapListNode(x, z); node; node = node->mp_next) {
                CMetaPiece* meta = node->GetMeta()->Cast();
                if (meta->IsRestartPiece() || !meta->IsCompletelyWithinArea(&area) ||
                    meta->m_name_checksum == kRiserBelow)
                    continue;
                if (!AddEntry(meta, dy)) {
                    Destroy();
                    return false;
                }
            }
        }
    }
    // The rails over it.
    Mth::Vector size;
    Mth::Vector corner;
    const Mth::Vector* world = manager->GridDimsToWorld(&corner, &area, &size);
    const float x0 = world->x;
    const float z0 = world->z;
    mp_rails = CopyRailsInBox(RailEditor(), x0, z0, size.x + x0, size.z + z0);
    return true;
}

// 0x0058C780
bool CClipboard::ShowAt(CCursor* cursor)
{
    CParkEditor* editor = CParkEditor::Create(false);
    if (!editor->MainHeapHasRoom(m_max_x - m_min_x + 1, m_max_z - m_min_z + 1)) {
        CParkEditor::Release();
        return false;
    }
    const uint8_t centreX = static_cast<uint8_t>((m_min_x + m_max_x) / 2);
    const uint8_t centreZ = static_cast<uint8_t>((m_min_z + m_max_z) / 2);
    for (CClipboardEntry* entry = mp_metas; entry; entry = entry->mp_next) {
        if (!entry->CanPlace(centreX, centreZ, cursor)) {
            CParkEditor::Release();
            return false;
        }
    }
    int railPoints = 0;
    for (void* rail = mp_rails; rail; rail = NextRail(rail))
        railPoints += RailPointCount(rail);
    if (railPoints > FreeRailPoints(RailEditor())) {
        CParkEditor::Release();
        return false;
    }
    for (CClipboardEntry* entry = mp_metas; entry; entry = entry->mp_next)
        entry->Paste(centreX, cursor->m_clipboardHeight, centreZ, cursor);
    Mth::Vector centre;
    GetCentre(&centre);
    Mth::Vector at = *editor->GetCursorPos();
    for (void* rail = mp_rails; rail; rail = NextRail(rail)) {
        void* copy = NewRail(RailEditor());
        CopyRail(copy, rail);
        PlaceRail(copy, &centre, &at, static_cast<float>(cursor->m_rot * static_cast<double>(kQuarterTurnDegrees)));
        TransformRail1(copy, RailOrigin(), RailOrigin(), 0.0f);
        TransformRail2(copy, RailOrigin(), RailOrigin(), 0.0f);
    }
    CParkEditor::Release();
    return true;
}

} // namespace Ed

REPLACE(0x0058B800, &Ed::CClipboardEntry::GetArea);
REPLACE(0x0058B940, &Ed::CClipboardEntry::CanPlace);
REPLACE(0x0058B9C0, &Ed::CClipboardEntry::Paste);
REPLACE(0x0058BB20, &Ed::CClipboardEntry::GetHeight);
REPLACE(0x0058BBA0, &Ed::CClipboardEntry::HighlightIntersectingMetas);
REPLACE(0x0058BCA0, &Ed::CClipboardEntry::ShowPreview);
REPLACE(0x0058B7E0, &Ed::CClipboard::IsEmpty);
REPLACE(0x0058BDF0, &Ed::CClipboard::Construct);
REPLACE(0x0058BE30, &Ed::CClipboard::GetHeight);
REPLACE(0x0058BEB0, &Ed::CClipboard::HighlightIntersectingMetas);
REPLACE(0x0058BF10, &Ed::CClipboard::GetArea);
REPLACE(0x0058C050, &Ed::CClipboard::GetCentre);
REPLACE(0x0058C130, &Ed::CClipboard::ShowMetas);
REPLACE(0x0058C200, &Ed::CClipboard::DestroyMetas);
REPLACE(0x0058C270, &Ed::CClipboard::AddEntry);
REPLACE(0x0058C430, &Ed::CClipboard::Destroy);
REPLACE_SOURCE_ONLY(0x0058C980, &Ed::CClipboard::Destroy); // a jump to 0x0058C430
REPLACE(0x0058C590, &Ed::CClipboard::CopySelectionToClipboard);
REPLACE(0x0058C780, &Ed::CClipboard::ShowAt);
