// The park manager's floor (Ed::CParkManager): the risers under raised and lowered cells, the fence round the
// park, changing the floor's height, and the restart points put on a new park.

#include "game/ed/park.h"
#include "game/ed/park_editor.h"

#include <cstdio>

#include "game/core/crc.h"
#include "game/obj/smtptr.h"
#include "hook/hook.h"

namespace Ed {

namespace {

constexpr uint32_t kRiserFloor = 0xB0A98147;      // "riser_floor"
constexpr uint32_t kRiserFloorBelow = 0x14FD33E4; // "riser_floor_below"
constexpr uint32_t kRiserBelow = 0x4A0D22DB;      // "riser_below"
constexpr uint32_t kFence = 0x771994FF;           // "Sk4Ed_Fence_20x20"

// A collision probe along a line (Nx's feeler, 0xB0 bytes): constructed by 0x00417760, its ends set by
// 0x00417190 / 0x004171B0, tested by 0x004172F0; what it hit is held by a smart pointer.
struct Feeler {
    uint8_t unknown00[0x78];
    Mth::Vector m_point;              // +0x78: where it hit
    uint8_t unknown88[0x10];
    Obj::CSmtPtr<Obj::CRefCounted> m_object; // +0x98
    uint8_t unknownA4[0xC];

    Feeler* Construct() { return GameFunction<Feeler*(__thiscall*)(Feeler*)>(0x00417760)(this); }
    void SetStart(Mth::Vector* start) { GameFunction<void(__thiscall*)(Feeler*, Mth::Vector*)>(0x00417190)(this, start); }
    void SetEnd(Mth::Vector* end) { GameFunction<void(__thiscall*)(Feeler*, Mth::Vector*)>(0x004171B0)(this, end); }
    bool GetCollision(int a, int b) { return GameFunction<bool(__thiscall*)(Feeler*, int, int)>(0x004172F0)(this, a, b); }
};
static_assert(offsetof(Feeler, m_object) == 0x98);
static_assert(sizeof(Feeler) == 0xB0);

// The park editor (a reference taken here, let go by ReleaseParkEditor): whether the park is full.
bool ParkIsFull() { return CParkEditor::Create(false)->IsParkFull() != 0; }
void ReleaseParkEditor() { CParkEditor::Release(); }

} // namespace

// 0x005993E0
void CParkManager::GenerateNodeArray()
{
    mp_generator->ClearAutomaticRestarts();
    for (int i = 0; i < 18; ++i) {
        // Round the middle of the park: player 1 and 2's restarts, four more, six of type 3, six of type 4.
        GridDims pos = GridDims::Make(0, 0, 0, 0, 0, 0);
        pos = m_parkBounds;
        pos.x = static_cast<uint8_t>(pos.x + (m_parkBounds.w >> 1));
        pos.y = 0;
        pos.z = static_cast<uint8_t>(m_parkBounds.z + (m_parkBounds.l >> 1));
        pos.w = 1;
        pos.h = 1;
        pos.l = 1;
        int type = 1;
        int count = 1;
        if (i == 1) {
            pos.x = static_cast<uint8_t>(pos.x + 1);
            type = 2;
        } else if (i == 2) {
            pos.x = static_cast<uint8_t>(pos.x - 2);
            pos.z = static_cast<uint8_t>(pos.z + 2);
            type = 5;
        } else if (i == 3) {
            pos.z = static_cast<uint8_t>(pos.z + 2);
            type = 6;
        } else if (i == 4) {
            pos.x = static_cast<uint8_t>(pos.x + 2);
            pos.z = static_cast<uint8_t>(pos.z + 2);
            type = 7;
        } else if (i == 5) {
            pos.x = static_cast<uint8_t>(pos.x + 4);
            pos.z = static_cast<uint8_t>(pos.z + 2);
            type = 8;
        } else if (i >= 6 && i <= 11) {
            pos.x = static_cast<uint8_t>(pos.x + i - 9);
            pos.z = static_cast<uint8_t>(pos.z - 2);
            count = 6;
            type = 3;
        }
        bool flag = false;
        if (i >= 12 && i <= 17) {
            pos.x = static_cast<uint8_t>(pos.x + i - 15);
            pos.y = 0;
            pos.z = static_cast<uint8_t>(pos.z - 3);
            flag = true;
            count = 6;
            type = 4;
        }
        if (!mp_generator->NeedsRestarts(type, count))
            continue;

        // On the floor (or the metapiece standing there), then down onto whatever is below a feeler's line.
        pos.y = static_cast<int8_t>(GetFloorHeight(pos, nullptr));
        {
            MapListHolder metas(GetIntersectingMetas(pos, 0, nullptr, 0));
            if (metas.list)
                pos.y = static_cast<int8_t>(metas.list->GetConcreteMeta()->m_cell_area.h + pos.y);
        }
        Mth::Vector point;
        point.x = static_cast<float>((pos.x - 29) * static_cast<double>(kCellWidth) + kCellWidth * 0.5);
        point.y = static_cast<float>(pos.y * static_cast<double>(kCellHeight));
        point.z = static_cast<float>((pos.z - 29) * static_cast<double>(kCellLength) + kCellLength * 0.5);
        point.w = 1.0f;
        Feeler feeler;
        feeler.Construct();
        Mth::Vector start = {point.x, static_cast<float>(static_cast<double>(point.y) + 10000.0), point.z,
                             static_cast<float>(static_cast<double>(point.w) + 1.0)};
        feeler.SetStart(&start);
        Mth::Vector end = {point.x, static_cast<float>(static_cast<double>(point.y) - 1000.0), point.z,
                           static_cast<float>(static_cast<double>(point.w) + 1.0)};
        feeler.SetEnd(&end);
        if (feeler.GetCollision(1, 0)) {
            if (i >= 2 && i <= 5)
                point.y = feeler.m_point.y;
            else
                point.y = static_cast<float>(static_cast<double>(feeler.m_point.y) + kCellHeight);
        }
        mp_generator->AddRestart(&point, 0, &pos, type, 1, flag);
        // (the feeler's destructor lets go of what it hit)
    }
    mp_generator->BuildNodeArray(mp_concrete_metas);
}

// 0x00599CD0
bool CParkManager::CanChangeFloorHere(GridDims area, bool uneven)
{
    bool ok = false;
    if (!ParkIsFull()) {
        MapListHolder metas(GetAreaMetaPieces(area));
        bool clear = true;
        for (CMapListNode* node = metas.list; node; node = node->mp_next) {
            if (!(node->GetConcreteMeta()->m_flags & CMetaPiece::kInnerShell)) {
                clear = false;
                break;
            }
        }
        if (clear) {
            // Without `uneven`, the floor must be flat at the box's height.
            bool level = true;
            if (!uneven) {
                bool flat;
                level = GetFloorHeight(area, &flat) == area.y && flat;
            }
            if (level && !IsAreaBlocked(area))
                ok = true;
        }
    }
    ReleaseParkEditor();
    return ok;
}

// 0x00599E20
void CParkManager::DestroyMetasBelow(GridDims area)
{
    GridDims below = area;
    below.y = static_cast<int8_t>(area.y - 1);
    below.h = 1;
    uint32_t destroyed = DestroyMetasInArea(below, 4);
    m_riserMetas += destroyed >> 16;
    m_riserPieces += destroyed & 0xFFFF;
}

// 0x0059B280
void CParkManager::UndoFloorChange()
{
    RestoreFloorHeights();
    for (CMapListNode* node = mp_riserList; node; node = node->mp_next)
        node->mp_meta = RelocateMetaPiece(node->GetConcreteMeta(), 0, -node->m_dy, 0);
}

// 0x0059B2C0
void CParkManager::MoveRiserList(CMapListNode* list)
{
    for (CMapListNode* node = list; node; node = node->mp_next) {
        CMetaPiece* meta = node->GetConcreteMeta();
        if (node->m_dy < 0)
            DestroyMetasBelow(meta->m_cell_area);
        node->mp_meta = RelocateMetaPiece(meta, 0, node->m_dy, 0);
    }
}

// 0x0059B320
void CParkManager::RebuildFloorPiece(FloorPieceRequest* request)
{
    int x = request->x;
    int z = request->z;
    FloorCell* cells = &m_floor[0][0];
    auto heightAt = [&](int cx, int cz) { return cells[cx * kFloorSize + cz].m_height; };

    // The lowest neighbour (outside the shell counts as 0).
    int lowest = 10000;
    if (x > m_shellBounds.x) {
        if (heightAt(x - 1, z) < lowest)
            lowest = heightAt(x - 1, z);
    } else {
        lowest = 0;
    }
    auto atMostZero = [&] {
        if (lowest > 0)
            lowest = 0;
    };
    if (x < m_shellBounds.w + m_shellBounds.x - 1) {
        if (heightAt(x + 1, z) < lowest)
            lowest = heightAt(x + 1, z);
    } else {
        atMostZero();
    }
    if (z > m_shellBounds.z) {
        if (heightAt(x, z - 1) < lowest)
            lowest = heightAt(x, z - 1);
    } else {
        atMostZero();
    }
    if (z < m_shellBounds.l + m_shellBounds.z - 1) {
        if (heightAt(x, z + 1) < lowest)
            lowest = heightAt(x, z + 1);
    } else {
        atMostZero();
    }

    // The piece on top: a riser of the cell's height (the floor itself at 0, a dug-in floor below 0), and the
    // riser_below wall where a neighbour is lower than this cell's floor.
    uint32_t top;
    uint32_t below = 0;
    int belowY = -15;
    int height = request->height;
    bool inPark;
    if (height > 0) {
        char name[128];
        std::snprintf(name, sizeof(name), "riser_above%d", request->index);
        top = Crc::GenerateCRCFromString(name);
        request->y = 0;
        inPark = lowest >= 0;
        if (!inPark)
            below = kRiserBelow;
    } else if (height == 0) {
        top = kRiserFloor;
        request->y = -1;
        inPark = lowest >= 0;
        if (!inPark) {
            top = 0;
            below = kRiserBelow;
        }
    } else {
        top = kRiserFloorBelow;
        request->y = height;
        if (lowest < height) {
            belowY = height - 15;
            top = 0;
            below = kRiserBelow;
        }
        inPark = lowest >= 0;
    }
    // (outside the park's bounds, no top piece unless the floor is dug in)
    if (inPark) {
        if (x < m_parkBounds.x || x >= m_parkBounds.x + m_parkBounds.w)
            top = 0;
        if (z < m_parkBounds.z || z >= m_parkBounds.z + m_parkBounds.l)
            top = 0;
    }

    // Keep what's there already, delete the rest of the inner shell in the cell.
    GridDims cell = GridDims::Make(static_cast<uint8_t>(request->x), static_cast<int8_t>(request->y),
                                   static_cast<uint8_t>(request->z), 1, 1, 1);
    MapListHolder metas(GetAreaMetaPieces(cell));
    for (CMapListNode* node = metas.list; node;) {
        CMetaPiece* meta = node->GetConcreteMeta();
        node = node->mp_next;
        if (!(meta->m_flags & CMetaPiece::kInnerShell))
            continue;
        if (top && meta->m_name_checksum == top && meta->m_cell_area.y == request->y) {
            top = 0;
        } else if (below && meta->m_name_checksum == below &&
                   (static_cast<uint8_t>(meta->m_cell_area.y) == static_cast<uint8_t>(request->y) ||
                    meta->m_cell_area.y == static_cast<int8_t>(belowY))) {
            below = 0;
        } else {
            DestroyConcreteMeta(meta, 0);
            mp_generator->CleanUpOutRefs();
        }
    }
    if (top) {
        CMetaPiece* meta = CreateConcreteMeta(GetAbstractMeta(top), 0);
        AddMetaPieceToPark(GridDims::Make(static_cast<uint8_t>(request->x), static_cast<int8_t>(request->y),
                                          static_cast<uint8_t>(request->z), 0, 0, 0),
                           meta);
    }
    if (below) {
        CMetaPiece* meta = CreateConcreteMeta(GetAbstractMeta(below), 0);
        if (request->x < m_parkBounds.x || request->x >= m_parkBounds.x + m_parkBounds.w ||
            request->z < m_parkBounds.z || request->z >= m_parkBounds.z + m_parkBounds.l)
            meta->m_flags |= CMetaPiece::kRiserBelow | CMetaPiece::kFlag400;
        AddMetaPieceToPark(GridDims::Make(static_cast<uint8_t>(request->x), static_cast<int8_t>(belowY),
                                          static_cast<uint8_t>(request->z), 0, 0, 0),
                           meta);
    }
}

// 0x0059BED0
void CParkManager::RebuildInnerShell(GridDims bounds)
{
    m_parkBounds = bounds;
    GridDims& park = m_parkBounds;
    // The four strips round the park (where the walls go).
    m_blockedAreas[0].x = static_cast<uint8_t>(park.x - 1);
    m_blockedAreas[0].z = static_cast<uint8_t>(park.z - 1);
    m_blockedAreas[0].w = 1;
    m_blockedAreas[0].l = static_cast<uint8_t>(park.l + 2);
    m_blockedAreas[1].x = static_cast<uint8_t>(park.w + park.x);
    m_blockedAreas[1].z = static_cast<uint8_t>(park.z - 1);
    m_blockedAreas[1].w = 1;
    m_blockedAreas[1].l = static_cast<uint8_t>(park.l + 2);
    m_blockedAreas[2].x = static_cast<uint8_t>(park.x - 1);
    m_blockedAreas[2].z = static_cast<uint8_t>(park.z - 1);
    m_blockedAreas[2].w = static_cast<uint8_t>(park.w + 2);
    m_blockedAreas[2].l = 1;
    m_blockedAreas[3].x = static_cast<uint8_t>(park.x - 1);
    m_blockedAreas[3].z = static_cast<uint8_t>(park.l + park.z);
    m_blockedAreas[3].w = static_cast<uint8_t>(park.w + 2);
    m_blockedAreas[3].l = 1;

    // Out go the fences and the player's metapieces no longer (wholly) in the park.
    for (CMapListNode* node = mp_concrete_metas; node;) {
        CMapListNode* next = node->mp_next;
        GridDims area = node->GetConcreteMeta()->m_cell_area;
        uint32_t flags = node->GetConcreteMeta()->m_flags;
        bool destroy;
        if (flags & CMetaPiece::kFence)
            destroy = true;
        else if ((flags & CMetaPiece::kInnerShell) || !(flags & CMetaPiece::kInPark))
            destroy = false;
        else
            destroy = area.x < park.x || area.z < park.z || area.w + area.x > park.w + park.x ||
                      area.l + area.z > park.l + park.z;
        if (destroy)
            DestroyConcreteMeta(node->GetConcreteMeta(), 0);
        node = next;
    }
    mp_generator->CleanUpOutRefs();
    MarkProgress("RebuildInnerShell-After any inner shell clipping");

    // The fence: two-cell pieces along each side, facing in, wherever the strip isn't blocked.
    CMetaPiece* fence = GetAbstractMeta(kFence);
    auto addFence = [&](GridDims test, GridDims at, int rot) {
        if (IsAreaBlocked(test))
            return;
        CMetaPiece* meta = CreateConcreteMeta(fence, 0);
        meta->SetRot(rot);
        meta->m_flags |= CMetaPiece::kFence;
        AddMetaPieceToPark(at, meta);
    };
    for (int x = park.x; x < park.w + park.x; x += 2) {
        uint8_t px = static_cast<uint8_t>(x);
        addFence(GridDims::Make(px, 0, park.z, 2, 1, 1), GridDims::Make(px, 0, static_cast<uint8_t>(park.z - 1), 0, 0, 0), 0);
        uint8_t far = static_cast<uint8_t>(park.z + park.l);
        addFence(GridDims::Make(px, 0, static_cast<uint8_t>(far - 1), 2, 1, 1), GridDims::Make(px, 0, far, 0, 0, 0), 2);
    }
    for (int z = park.z; z < park.l + park.z; z += 2) {
        uint8_t pz = static_cast<uint8_t>(z);
        addFence(GridDims::Make(park.x, 0, pz, 1, 1, 2), GridDims::Make(static_cast<uint8_t>(park.x - 1), 0, pz, 0, 0, 0), 1);
        uint8_t far = static_cast<uint8_t>(park.w + park.x);
        addFence(GridDims::Make(static_cast<uint8_t>(far - 1), 0, pz, 1, 1, 2), GridDims::Make(far, 0, pz, 0, 0, 0), 3);
    }
    MarkProgress("RebuildInnerShell-After adding fence pieces");

    // No floor height outside the park (the first 56 x 56 cells).
    for (int x = 0; x < 56; ++x) {
        for (int z = 0; z < 56; ++z) {
            if (x < park.x || z < park.z || x >= park.w + park.x || z >= park.l + park.z)
                m_floor[x][z].m_height = 0;
        }
    }
    mp_generator->UpdateBounds((m_flags & kMapLoaded) ? m_map.m_shellSize : 0);
    MarkProgress("RebuildInnerShell-End");
}

// 0x0059C520
bool CParkManager::ChangeFloorHeight(GridDims area, int y, int target, bool raise, bool level)
{
    uint32_t mode = raise ? 6 : 4;
    if (level)
        mode |= 1;
    ClearFloorChangeMarks();
    if (mp_riserList)
        CMapListNode::DestroyList(mp_riserList);
    mp_riserList = nullptr;
    m_riserMetas = 0;
    m_riserPieces = 0;
    if (level && HasFloorLockedMeta(area))
        return false;
    if (!BuildRiserList(area, y, target, mode, &mp_riserList)) {
        RestoreFloorHeights();
        return false;
    }
    MoveRiserList(mp_riserList);
    if (!raise)
        DestroyMetasBelow(area);
    return true;
}

// 0x0059C600
bool CParkManager::RebuildFloor(GridDims* area, int /*unused*/)
{
    if (!area) {
        // The inner shell's pieces outside the park go first.
        for (CMapListNode* node = mp_concrete_metas; node; node = node->mp_next) {
            CMetaPiece* meta = node->GetMeta();
            if (!(meta->m_flags & CMetaPiece::kInnerShell))
                continue;
            GridDims d = meta->m_cell_area;
            if (d.x >= m_parkBounds.x && d.z >= m_parkBounds.z && d.w + d.x <= m_parkBounds.w + m_parkBounds.x &&
                d.l + d.z <= m_parkBounds.l + m_parkBounds.z)
                continue;
            DestroyConcreteMeta(node->GetConcreteMeta(), 0);
        }
        mp_generator->CleanUpOutRefs();
        area = &m_parkBounds;
    }
    // Every cell of the area and the ring round it.
    FloorCell* cells = &m_floor[0][0];
    for (int x = area->x - 1; x < area->w + 1 + area->x; ++x) {
        for (int z = area->z - 1; z < area->l + 1 + area->z; ++z) {
            FloorPieceRequest request;
            request.height = cells[x * kFloorSize + z].m_height;
            request.index = request.height - 1;
            request.x = x;
            request.z = z;
            RebuildFloorPiece(&request);
        }
    }
    MarkProgress("generate_floor_pieces");
    return true;
}

} // namespace Ed

REPLACE(0x005993E0, &Ed::CParkManager::GenerateNodeArray);
REPLACE(0x00599CD0, &Ed::CParkManager::CanChangeFloorHere);
// 0x00599DD4 is CanChangeFloorHere's last block (jumped to from inside it; no callers) that Ghidra made a function of:
// its source is CanChangeFloorHere's.
REPLACE_SOURCE_ONLY(0x00599DD4, &Ed::CParkManager::CanChangeFloorHere);
REPLACE(0x00599E20, &Ed::CParkManager::DestroyMetasBelow);
REPLACE(0x0059B280, &Ed::CParkManager::UndoFloorChange);
REPLACE(0x0059B2C0, &Ed::CParkManager::MoveRiserList);
REPLACE(0x0059B320, &Ed::CParkManager::RebuildFloorPiece);
REPLACE(0x0059BED0, &Ed::CParkManager::RebuildInnerShell);
REPLACE(0x0059C520, static_cast<bool (Ed::CParkManager::*)(Ed::GridDims, int, int, bool, bool)>(
                         &Ed::CParkManager::ChangeFloorHeight));
REPLACE(0x0059C600, &Ed::CParkManager::RebuildFloor);
