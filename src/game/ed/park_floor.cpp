// The park manager's floor (ParkEditing::CParkLayout): the risers under raised and lowered cells, the fence round the
// park, changing the floor's height, and the restart points put on a new park.

#include "game/ed/park.h"
#include "game/ed/park_editor.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "game/core/crc.h"
#include "game/nx/feeler.h"
#include "game/obj/smtptr.h"

namespace ParkEditing {

namespace {

constexpr uint32_t kRiserFloor = 0xB0A98147;      // "riser_floor"
constexpr uint32_t kRiserFloorBelow = 0x14FD33E4; // "riser_floor_below"
constexpr uint32_t kRiserBelow = 0x4A0D22DB;      // "riser_below"
constexpr uint32_t kFence = 0x771994FF;           // "Sk4Ed_Fence_20x20"

// A collision probe along a line (Engine3D::CProbeRay, 0xB0 bytes): constructed by 0x00417760, its ends set by
// 0x00417190 / 0x004171B0, tested by 0x004172F0; what it hit is held by a smart pointer.
struct Feeler {
    uint8_t unknown00[0x78];
    Maths::Vector m_contactPos;              // +0x78: where it hit
    uint8_t unknown88[0x10];
    GameObjects::CSharedRef<GameObjects::CSharedBase> m_object; // +0x98
    uint8_t unknownA4[0xC];

    Engine3D::CProbeRay* Ours() { return reinterpret_cast<Engine3D::CProbeRay*>(this); }
    Feeler* Construct() { return reinterpret_cast<Feeler*>(Ours()->Construct()); }
    void SetStart(Maths::Vector* start) { Ours()->SetStart(start); }
    void SetEnd(Maths::Vector* end) { Ours()->SetEnd(end); }
    // (the original's result is read from AL)
    bool GetHitShape(bool movables, bool farthest) { return Ours()->GetHitShape(movables, farthest) != 0; }
};
static_assert(offsetof(Feeler, m_object) == 0x98);
static_assert(sizeof(Feeler) == 0xB0);

// The park editor (a reference taken here, let go by ReleaseParkEditor): whether the park is full.
bool ParkIsFull() { return CParkBuilderMode::Create(false)->IsParkFull() != 0; }
void ReleaseParkEditor() { CParkBuilderMode::Release(); }

} // namespace

// 0x005993E0
void CParkLayout::GenerateNodeArray()
{
    m_builder->PurgeAutoRestartPoints();
    for (int i = 0; i < 18; ++i) {
        // Round the middle of the park: player 1 and 2's restarts, four more, six of type 3, six of type 4.
        SCellBox pos = SCellBox::Make(0, 0, 0, 0, 0, 0);
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
        if (!m_builder->NeedsRestarts(type, count))
            continue;

        // On the floor (or the metapiece standing there), then down onto whatever is below a feeler's line.
        pos.y = static_cast<int8_t>(GetGroundLevel(pos, nullptr));
        {
            MapListHolder metas(GetIntersectingMetas(pos, 0, nullptr, 0));
            if (metas.list)
                pos.y = static_cast<int8_t>(metas.list->GetPlacedPiece()->m_cellFootprint.h + pos.y);
        }
        Maths::Vector point;
        point.x = static_cast<float>((pos.x - 29) * static_cast<double>(kCellWidth) + kCellWidth * 0.5);
        point.y = static_cast<float>(pos.y * static_cast<double>(kCellHeight));
        point.z = static_cast<float>((pos.z - 29) * static_cast<double>(kCellLength) + kCellLength * 0.5);
        point.w = 1.0f;
        Feeler feeler;
        feeler.Construct();
        Maths::Vector start = {point.x, static_cast<float>(static_cast<double>(point.y) + 10000.0), point.z,
                             static_cast<float>(static_cast<double>(point.w) + 1.0)};
        feeler.SetStart(&start);
        Maths::Vector end = {point.x, static_cast<float>(static_cast<double>(point.y) - 1000.0), point.z,
                           static_cast<float>(static_cast<double>(point.w) + 1.0)};
        feeler.SetEnd(&end);
        if (feeler.GetHitShape(true, false)) {
            if (i >= 2 && i <= 5)
                point.y = feeler.m_contactPos.y;
            else
                point.y = static_cast<float>(static_cast<double>(feeler.m_contactPos.y) + kCellHeight);
        }
        m_builder->PlantRestartPoint(&point, 0, &pos, type, 1, flag);
        // (the feeler's destructor lets go of what it hit)
    }
    m_builder->BuildNodeArray(mp_concrete_metas);
}

// 0x00599CD0
bool CParkLayout::CanChangeFloorHere(SCellBox area, bool uneven)
{
    bool ok = false;
    if (!ParkIsFull()) {
        MapListHolder metas(CollectPiecesInRegion(area));
        bool clear = true;
        for (CMapListNode* node = metas.list; node; node = node->m_following) {
            if (!(node->GetPlacedPiece()->m_flags & CPieceGroup::kInnerShell)) {
                clear = false;
                break;
            }
        }
        if (clear) {
            // Without `uneven`, the floor must be flat at the box's height.
            bool level = true;
            if (!uneven) {
                bool flat;
                level = GetGroundLevel(area, &flat) == area.y && flat;
            }
            if (level && !IsAreaBlocked(area))
                ok = true;
        }
    }
    ReleaseParkEditor();
    return ok;
}

// 0x00599E20
void CParkLayout::DestroyMetasBelow(SCellBox area)
{
    SCellBox below = area;
    below.y = static_cast<int8_t>(area.y - 1);
    below.h = 1;
    uint32_t destroyed = FreePieceGroupsInRegion(below, 4);
    m_riserMetas += destroyed >> 16;
    m_riserPieces += destroyed & 0xFFFF;
}

// 0x0059B280
void CParkLayout::UndoFloorChange()
{
    RestoreFloorHeights();
    for (CMapListNode* node = mp_riserList; node; node = node->m_following)
        node->m_pieceGroup = ShiftPlacedPiece(node->GetPlacedPiece(), 0, -node->m_dy, 0);
}

// 0x0059B2C0
void CParkLayout::MoveRiserList(CMapListNode* list)
{
    for (CMapListNode* node = list; node; node = node->m_following) {
        CPieceGroup* meta = node->GetPlacedPiece();
        if (node->m_dy < 0)
            DestroyMetasBelow(meta->m_cellFootprint);
        node->m_pieceGroup = ShiftPlacedPiece(meta, 0, node->m_dy, 0);
    }
}

// 0x0059B320
void CParkLayout::RebuildFloorPiece(FloorPieceRequest* request)
{
    int x = request->x;
    int z = request->z;
    FloorCell* cells = &m_floor[0][0];
    auto heightAt = [&](int cx, int cz) { return cells[cx * kFloorSize + cz].m_levelY; };

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
        top = Checksums::HashFromText(name);
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
    SCellBox cell = SCellBox::Make(static_cast<uint8_t>(request->x), static_cast<int8_t>(request->y),
                                   static_cast<uint8_t>(request->z), 1, 1, 1);
    MapListHolder metas(CollectPiecesInRegion(cell));
    for (CMapListNode* node = metas.list; node;) {
        CPieceGroup* meta = node->GetPlacedPiece();
        node = node->m_following;
        if (!(meta->m_flags & CPieceGroup::kInnerShell))
            continue;
        if (top && meta->m_nameKey == top && meta->m_cellFootprint.y == request->y) {
            top = 0;
        } else if (below && meta->m_nameKey == below &&
                   (static_cast<uint8_t>(meta->m_cellFootprint.y) == static_cast<uint8_t>(request->y) ||
                    meta->m_cellFootprint.y == static_cast<int8_t>(belowY))) {
            below = 0;
        } else {
            FreePlacedPiece(meta, 0);
            m_builder->CleanUpOutRefs();
        }
    }
    if (top) {
        CPieceGroup* meta = MakePlacedPiece(FindPieceTemplate(top), 0);
        PlaceMetaPieceInPark(SCellBox::Make(static_cast<uint8_t>(request->x), static_cast<int8_t>(request->y),
                                          static_cast<uint8_t>(request->z), 0, 0, 0),
                           meta);
    }
    if (below) {
        CPieceGroup* meta = MakePlacedPiece(FindPieceTemplate(below), 0);
        if (request->x < m_parkBounds.x || request->x >= m_parkBounds.x + m_parkBounds.w ||
            request->z < m_parkBounds.z || request->z >= m_parkBounds.z + m_parkBounds.l)
            meta->m_flags |= CPieceGroup::kRiserBelow | CPieceGroup::kFlag400;
        PlaceMetaPieceInPark(SCellBox::Make(static_cast<uint8_t>(request->x), static_cast<int8_t>(belowY),
                                          static_cast<uint8_t>(request->z), 0, 0, 0),
                           meta);
    }
}

// 0x0059BED0
void CParkLayout::RebuildInnerShell(SCellBox bounds)
{
    m_parkBounds = bounds;
    SCellBox& park = m_parkBounds;
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
        CMapListNode* next = node->m_following;
        SCellBox area = node->GetPlacedPiece()->m_cellFootprint;
        uint32_t flags = node->GetPlacedPiece()->m_flags;
        bool destroy;
        if (flags & CPieceGroup::kFence)
            destroy = true;
        else if ((flags & CPieceGroup::kInnerShell) || !(flags & CPieceGroup::kInPark))
            destroy = false;
        else
            destroy = area.x < park.x || area.z < park.z || area.w + area.x > park.w + park.x ||
                      area.l + area.z > park.l + park.z;
        if (destroy)
            FreePlacedPiece(node->GetPlacedPiece(), 0);
        node = next;
    }
    m_builder->CleanUpOutRefs();
    MarkProgress("RebuildInnerShell-After any inner shell clipping");

    // The fence: two-cell pieces along each side, facing in, wherever the strip isn't blocked.
    CPieceGroup* fence = FindPieceTemplate(kFence);
    auto addFence = [&](SCellBox test, SCellBox at, int rot) {
        if (IsAreaBlocked(test))
            return;
        CPieceGroup* meta = MakePlacedPiece(fence, 0);
        meta->SetQuarterTurns(rot);
        meta->m_flags |= CPieceGroup::kFence;
        PlaceMetaPieceInPark(at, meta);
    };
    for (int x = park.x; x < park.w + park.x; x += 2) {
        uint8_t px = static_cast<uint8_t>(x);
        addFence(SCellBox::Make(px, 0, park.z, 2, 1, 1), SCellBox::Make(px, 0, static_cast<uint8_t>(park.z - 1), 0, 0, 0), 0);
        uint8_t far = static_cast<uint8_t>(park.z + park.l);
        addFence(SCellBox::Make(px, 0, static_cast<uint8_t>(far - 1), 2, 1, 1), SCellBox::Make(px, 0, far, 0, 0, 0), 2);
    }
    for (int z = park.z; z < park.l + park.z; z += 2) {
        uint8_t pz = static_cast<uint8_t>(z);
        addFence(SCellBox::Make(park.x, 0, pz, 1, 1, 2), SCellBox::Make(static_cast<uint8_t>(park.x - 1), 0, pz, 0, 0, 0), 1);
        uint8_t far = static_cast<uint8_t>(park.w + park.x);
        addFence(SCellBox::Make(static_cast<uint8_t>(far - 1), 0, pz, 1, 1, 2), SCellBox::Make(far, 0, pz, 0, 0, 0), 3);
    }
    MarkProgress("RebuildInnerShell-After adding fence pieces");

    // No floor height outside the park (the first 56 x 56 cells).
    for (int x = 0; x < 56; ++x) {
        for (int z = 0; z < 56; ++z) {
            if (x < park.x || z < park.z || x >= park.w + park.x || z >= park.l + park.z)
                m_floor[x][z].m_levelY = 0;
        }
    }
    m_builder->UpdateBounds((m_flags & kMapLoaded) ? m_map.m_shellSize : 0);
    MarkProgress("RebuildInnerShell-End");
}

// 0x0059C520
bool CParkLayout::RaiseOrLowerFloor(SCellBox area, int y, int target, bool raise, bool level)
{
    uint32_t mode = raise ? 6 : 4;
    if (level)
        mode |= 1;
    ClearFloorChangeMarks();
    if (mp_riserList)
        CMapListNode::FreeNodeChain(mp_riserList);
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
bool CParkLayout::RegenerateGround(SCellBox* area, int /*unused*/)
{
    if (!area) {
        // The inner shell's pieces outside the park go first.
        for (CMapListNode* node = mp_concrete_metas; node; node = node->m_following) {
            CPieceGroup* meta = node->GetPieceGroup();
            if (!(meta->m_flags & CPieceGroup::kInnerShell))
                continue;
            SCellBox d = meta->m_cellFootprint;
            if (d.x >= m_parkBounds.x && d.z >= m_parkBounds.z && d.w + d.x <= m_parkBounds.w + m_parkBounds.x &&
                d.l + d.z <= m_parkBounds.l + m_parkBounds.z)
                continue;
            FreePlacedPiece(node->GetPlacedPiece(), 0);
        }
        m_builder->CleanUpOutRefs();
        area = &m_parkBounds;
    }
    // Every cell of the area and the ring round it.
    FloorCell* cells = &m_floor[0][0];
    for (int x = area->x - 1; x < area->w + 1 + area->x; ++x) {
        for (int z = area->z - 1; z < area->l + 1 + area->z; ++z) {
            FloorPieceRequest request;
            request.height = cells[x * kFloorSize + z].m_levelY;
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
