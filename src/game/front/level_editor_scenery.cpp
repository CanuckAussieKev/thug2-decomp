// NOT ORIGINAL - the level editor's scenery (ours, added at the user's request). See level_editor_scenery.h.

#include "game/front/level_editor_scenery.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include "game/core/manager.h"
#include "game/mth/vector.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor {

// --- the pieces (pure) ------------------------------------------------------------------------------------------

V3 QuarterTurn(V3 v, int quarters)
{
    switch (quarters & 3) {
    case 1: return {v.z, v.y, -v.x};
    case 2: return {-v.x, v.y, -v.z};
    case 3: return {-v.z, v.y, v.x};
    default: return v;
    }
}

V3 Apply(const Placement& placement, V3 x) { return QuarterTurn(x, placement.quarters) + placement.offset; }

void MoveBy(Placement* placement, V3 delta) { placement->offset = placement->offset + delta; }

void TurnAbout(Placement* placement, V3 pivot, int quarters)
{
    // x'' = R (x' - pivot) + pivot, x' = Q x + offset: x'' = R Q x + R (offset - pivot) + pivot.
    placement->quarters = (placement->quarters + quarters) & 3;
    placement->offset = QuarterTurn(placement->offset - pivot, quarters) + pivot;
}

void EngineState(int quarters0, V3 position0, const Placement& placement, int* quarters, V3* position)
{
    // Loaded vertex v was at Q0 v + p0; now at Q (Q0 v + p0) + offset = Q Q0 v + (Q p0 + offset).
    *quarters = (quarters0 + placement.quarters) & 3;
    *position = Apply(placement, position0);
}

OrientedBox PlacedBox(V3 min, V3 max, const Placement& placement)
{
    OrientedBox box;
    box.origin = placement.offset;
    box.axes[0] = QuarterTurn({1.0f, 0.0f, 0.0f}, placement.quarters);
    box.axes[1] = {0.0f, 1.0f, 0.0f};
    box.axes[2] = QuarterTurn({0.0f, 0.0f, 1.0f}, placement.quarters);
    box.min = min;
    box.max = max;
    return box;
}

bool SectorSizeMovable(V3 min, V3 max, float limit)
{
    if (!(min.x <= max.x && min.y <= max.y && min.z <= max.z)) // (also NaN)
        return false;
    return max.x - min.x <= limit && max.y - min.y <= limit && max.z - min.z <= limit;
}

void GrowBox(V3* min, V3* max, bool have, V3 boxMin, V3 boxMax)
{
    if (!have) {
        *min = boxMin;
        *max = boxMax;
        return;
    }
    *min = {std::min(min->x, boxMin.x), std::min(min->y, boxMin.y), std::min(min->z, boxMin.z)};
    *max = {std::max(max->x, boxMax.x), std::max(max->y, boxMax.y), std::max(max->z, boxMax.z)};
}

void BoundsOf(const OrientedBox& box, V3* min, V3* max)
{
    V3 corners[8];
    BoxCorners(box.origin, box.axes, box.min, box.max, corners);
    for (int i = 0; i < 8; ++i)
        GrowBox(min, max, i != 0, corners[i], corners[i]);
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace Scenery {
namespace {

constexpr uint32_t kPcGeomVtable = 0x0064A368;
constexpr float kMaxSectorExtent = 4000.0f; // (larger: the ground, a street, a building block - not moved)
constexpr uint32_t kCluster = 0x1A3A966Bu;  // "Cluster" (the node's TrickObject Cluster=)

template <typename F>
F Method(void* object, int offset)
{
    return reinterpret_cast<F>((*reinterpret_cast<void***>(object))[offset / 4]);
}

// The scenes (0x006FA7EC's list: nodes {?, scene +4, ?, next +0xC}, ended by a node whose scene is -1), skies left
// out (+0x8D: drawn in the sky pass).
struct SceneNode {
    uint32_t unknown0;
    uint8_t* scene;
    uint32_t unknown8;
    SceneNode* next;
};
bool IsEnd(const SceneNode* node) { return reinterpret_cast<intptr_t>(node->scene) == -1; }

template <typename F>
void ForEachScene(F fn)
{
    SceneNode* node = GameVariable<SceneNode*>(0x006FA7EC);
    for (int guard = 0; node && !IsEnd(node) && guard < 256; ++guard) {
        SceneNode* next = node->next;
        if (node->scene && !node->scene[0x8D])
            fn(node->scene);
        node = next;
    }
}

// A scene's sector table (+0xD0): {bits, entries}; 1 << bits entries {checksum, sector, next}.
struct TableEntry {
    uint32_t checksum;
    uint8_t* sector;
    TableEntry* next;
};
template <typename F>
void ForEachSector(uint8_t* scene, F fn)
{
    auto* table = *reinterpret_cast<uint32_t**>(scene + 0xD0);
    if (!table || table[0] > 20)
        return;
    auto* entries = reinterpret_cast<TableEntry*>(table[1]);
    if (!entries)
        return;
    uint32_t count = 1u << table[0];
    for (uint32_t i = 0; i < count; ++i) {
        for (TableEntry* entry = &entries[i]; entry; entry = entry->next) {
            if (entry->sector)
                fn(entry->checksum, entry->sector);
        }
    }
}

uint8_t* LookupSector(uint8_t* scene, uint32_t checksum)
{
    return GameFunction<uint8_t*(__thiscall*)(void*, uint32_t)>(0x0049CB00)(scene, checksum);
}

// The sector of that checksum in the first (non-sky) scene having one.
uint8_t* FindSector(uint32_t checksum, uint8_t** sceneOut)
{
    uint8_t* found = nullptr;
    ForEachScene([&](uint8_t* scene) {
        if (found)
            return;
        if (uint8_t* sector = LookupSector(scene, checksum)) {
            found = sector;
            if (sceneOut)
                *sceneOut = scene;
        }
    });
    return found;
}

uint8_t* GeomOf(uint8_t* sector) { return *reinterpret_cast<uint8_t**>(sector + 0x14); }
uint8_t* CollOf(uint8_t* sector) { return *reinterpret_cast<uint8_t**>(sector + 0x18); }
uint32_t& FlagsOf(uint8_t* sector) { return *reinterpret_cast<uint32_t*>(sector + 0x08); }
int QuartersOf(uint8_t* sector) { return *reinterpret_cast<int*>(sector + 0x10); }

// The PC geometry of level scenery (not an instance), active.
bool PlainGeom(uint8_t* geom)
{
    return geom && *reinterpret_cast<uint32_t*>(geom) == kPcGeomVtable && !*reinterpret_cast<void**>(geom + 0x4C);
}

// Its box as loaded (+0x14 min, +0x24 max).
bool GeomBox(uint8_t* geom, V3* min, V3* max)
{
    const float* box = reinterpret_cast<const float*>(geom + 0x14);
    *min = {box[0], box[1], box[2]};
    *max = {box[4], box[5], box[6]};
    return min->x <= max->x && min->y <= max->y && min->z <= max->z;
}

// The geometry's world position (vfunc +0x4C, 0x004B46A0: the first mesh's; 0 when never moved).
V3 GeomPosition(uint8_t* geom)
{
    const Mth::Vector* v = GameFunction<const Mth::Vector*(__thiscall*)(void*)>(0x0048DED0)(geom);
    return {v->x, v->y, v->z};
}

// The meshes' vertices can be moved: each vertex buffer has its own CPU copy (+0x0C) holding exactly the mesh's
// vertices (+0x22 count, +0x18 stride) - what sMesh::SetPosition walks and uploads.
const char* MeshesMovable(uint8_t* geom)
{
    auto** meshes = *reinterpret_cast<uint8_t***>(geom + 0x3C);
    uint32_t count = *reinterpret_cast<uint32_t*>(geom + 0x40);
    if (!meshes || count == 0)
        return "no meshes";
    if (count > 4096)
        return "odd mesh count";
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* mesh = meshes[i];
        if (!mesh)
            return "no mesh";
        uint32_t buffers = mesh[0x1A];
        uint32_t vertices = *reinterpret_cast<uint16_t*>(mesh + 0x22);
        uint32_t stride = mesh[0x18];
        if (buffers > 8)
            return "odd vertex buffers";
        for (uint32_t b = 0; b < buffers; ++b) {
            auto* buffer = *reinterpret_cast<uint32_t**>(mesh + 0xCC + 4 * b);
            if (!buffer || !buffer[3])
                return "no vertex copy";
            if (buffer[0] != vertices * stride)
                return "shared vertex buffer";
        }
    }
    return nullptr;
}

// What we've done to a sector: the box, turns and position when first touched, the placement since, and the engine's
// state as we set it (checked again before each use: a level reloaded at the same addresses starts afresh).
struct Track {
    uint8_t* scene;
    uint8_t* sector;
    uint8_t* geom;
    V3 min0, max0;
    int quarters0;
    V3 position0;
    Placement placement;
    int quarters;
    V3 position;
    bool moved;
};
std::map<uint32_t, Track> g_tracks;
std::vector<uint32_t> g_selection;
V3 g_pivot;
std::vector<uint8_t*> g_dirtyScenes;

bool SameV3(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// The track of a sector (made when first touched); null when the sector can't be found or has no plain geometry.
Track* TrackOf(uint32_t checksum)
{
    uint8_t* scene = nullptr;
    uint8_t* sector = FindSector(checksum, &scene);
    if (!sector)
        return nullptr;
    uint8_t* geom = GeomOf(sector);
    if (!PlainGeom(geom))
        return nullptr;
    auto it = g_tracks.find(checksum);
    if (it != g_tracks.end()) {
        Track& track = it->second;
        if (track.sector == sector && track.geom == geom && track.scene == scene && track.quarters == QuartersOf(sector) &&
            SameV3(track.position, GeomPosition(geom)))
            return &track;
        g_tracks.erase(it); // (another level, or moved by something else: start again from where it is)
    }
    Track track = {};
    track.scene = scene;
    track.sector = sector;
    track.geom = geom;
    if (!GeomBox(geom, &track.min0, &track.max0))
        return nullptr;
    track.quarters0 = track.quarters = QuartersOf(sector);
    track.position0 = track.position = GeomPosition(geom);
    track.placement = {0, {0.0f, 0.0f, 0.0f}};
    return &g_tracks.emplace(checksum, track).first->second;
}

// A sector's box now, without making a track for it.
bool CurrentBox(uint32_t checksum, uint8_t* geom, OrientedBox* box)
{
    auto it = g_tracks.find(checksum);
    if (it != g_tracks.end() && it->second.geom == geom) {
        *box = PlacedBox(it->second.min0, it->second.max0, it->second.placement);
        return true;
    }
    V3 min, max;
    if (!GeomBox(geom, &min, &max))
        return false;
    *box = PlacedBox(min, max, {0, {0.0f, 0.0f, 0.0f}});
    return true;
}

void MarkDirty(uint8_t* scene)
{
    if (std::find(g_dirtyScenes.begin(), g_dirtyScenes.end(), scene) == g_dirtyScenes.end())
        g_dirtyScenes.push_back(scene);
}

// The engine brought to the track's placement: the turn (CSector::SetYRotation 0x0049E760, about the geometry's
// world position as it is), then the position (the geometry's SetWorldPosition, vfunc +0x48 through 0x0048DEC0; the
// collision's, vfunc +0x14 - handed its own origin (vfunc +0x1C) plus the change: it translates by the difference),
// the sector flagged for the super sectors' update as CSector::SetWorldPosition (0x0049E6C0) does.
void ApplyTrack(Track* track)
{
    int quarters;
    V3 position;
    EngineState(track->quarters0, track->position0, track->placement, &quarters, &position);
    if (quarters != track->quarters) {
        GameFunction<void(__thiscall*)(void*, int)>(0x0049E760)(track->sector, quarters);
        track->quarters = quarters;
    }
    if (!SameV3(position, track->position)) {
        Mth::Vector to = {position.x, position.y, position.z, 1.0f};
        GameFunction<void(__thiscall*)(void*, const Mth::Vector*)>(0x0048DEC0)(track->geom, &to);
        if (uint8_t* coll = CollOf(track->sector)) {
            const Mth::Vector* origin = Method<const Mth::Vector*(__thiscall*)(void*)>(coll, 0x1C)(coll);
            V3 change = position - track->position;
            Mth::Vector moved = {origin->x + change.x, origin->y + change.y, origin->z + change.z, origin->w};
            Method<void(__thiscall*)(void*, const Mth::Vector*)>(coll, 0x14)(coll, &moved);
        }
        track->position = position;
    }
    if (FlagsOf(track->sector) & 0x40)
        FlagsOf(track->sector) |= 0x20;
    track->moved = true;
    MarkDirty(track->scene);
}

const char* NameOf(uint32_t checksum)
{
    const char* name = Script::FindChecksumName(checksum);
    return name ? name : nullptr;
}

std::string Label(uint32_t checksum)
{
    char text[96];
    const char* name = NameOf(checksum);
    std::snprintf(text, sizeof(text), "%s%s%08X", name ? name : "", name ? " " : "", checksum);
    return text;
}

uint32_t ClusterOf(uint32_t checksum)
{
    auto* node = GameFunction<Script::CStruct*(__cdecl*)(uint32_t, int)>(0x0045C1B0)(checksum, 0);
    uint32_t cluster = 0;
    if (node)
        node->GetChecksum(kCluster, &cluster, 0);
    return cluster;
}

void ComputePivot()
{
    V3 min = {}, max = {};
    bool have = false;
    for (uint32_t checksum : g_selection) {
        OrientedBox box;
        if (!Box(checksum, &box))
            continue;
        V3 a, b;
        BoundsOf(box, &a, &b);
        GrowBox(&min, &max, have, a, b);
        have = true;
    }
    g_pivot = have ? (min + max) * 0.5f : V3{0.0f, 0.0f, 0.0f};
}

// The feeler (as GroundBelow in level_editor.cpp): constructor 0x00417760, cast 0x00417710 (start, end, movables).
// Its collision data from +0x24: the normal, the point (+0x34), the collision object hit (+0x54) and its checksum
// (+0x58: the object's +4, written by 0x004131F0).
bool CastStatic(V3 from, V3 to, V3* point, uint32_t* checksum)
{
    alignas(16) uint8_t feeler[0x100] = {};
    GameFunction<void*(__thiscall*)(void*)>(0x00417760)(feeler);
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!GameFunction<bool(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, int)>(0x00417710)(
            feeler, &start, &end, 0))
        return false;
    const float* hit = reinterpret_cast<const float*>(feeler + 0x34);
    *point = {hit[0], hit[1], hit[2]};
    *checksum = *reinterpret_cast<const uint32_t*>(feeler + 0x58);
    return *reinterpret_cast<void* const*>(feeler + 0x54) != nullptr;
}

} // namespace

bool Pick(V3 origin, V3 unitDir, float maxT, Hit* hit)
{
    float best = maxT;
    bool found = false;
    // The collision ray, past collision that has no geometry to show (invisible walls), at most a few times.
    V3 from = origin;
    for (int i = 0; i < 4; ++i) {
        V3 point;
        uint32_t checksum;
        if (!CastStatic(from, origin + unitDir * maxT, &point, &checksum))
            break;
        float t = Dot(point - origin, unitDir);
        if (t >= best)
            break;
        uint8_t* sector = FindSector(checksum, nullptr);
        if (sector && GeomOf(sector)) {
            best = t;
            hit->checksum = checksum;
            hit->t = t;
            found = true;
            break;
        }
        from = point + unitDir * 1.0f;
    }
    // Sectors without collision: by their boxes (not huge ones - a box around the camera would always win).
    ForEachScene([&](uint8_t* scene) {
        ForEachSector(scene, [&](uint32_t checksum, uint8_t* sector) {
            uint8_t* geom = GeomOf(sector);
            if (CollOf(sector) || !PlainGeom(geom) || !geom[0x48])
                return;
            OrientedBox box;
            if (!CurrentBox(checksum, geom, &box) || !SectorSizeMovable(box.min, box.max, kMaxSectorExtent))
                return;
            float t;
            if (RayBox(origin, unitDir, box.origin, box.axes, box.min, box.max, &t) && t < best) {
                best = t;
                hit->checksum = checksum;
                hit->t = t;
                found = true;
            }
        });
    });
    return found;
}

bool Box(uint32_t checksum, OrientedBox* box)
{
    uint8_t* sector = FindSector(checksum, nullptr);
    if (!sector || !PlainGeom(GeomOf(sector)))
        return false;
    return CurrentBox(checksum, GeomOf(sector), box);
}

const char* WhyNotMovable(uint32_t checksum)
{
    uint8_t* sector = FindSector(checksum, nullptr);
    if (!sector)
        return "not found";
    uint8_t* geom = GeomOf(sector);
    if (!geom)
        return "collision only";
    if (*reinterpret_cast<uint32_t*>(geom) != kPcGeomVtable)
        return "odd geometry";
    if (*reinterpret_cast<void**>(geom + 0x4C))
        return "an instance";
    OrientedBox box;
    if (!CurrentBox(checksum, geom, &box))
        return "no box";
    if (!SectorSizeMovable(box.min, box.max, kMaxSectorExtent))
        return "too big";
    return MeshesMovable(geom);
}

std::string DescribeHover(uint32_t checksum)
{
    std::string text = "scenery " + Label(checksum);
    OrientedBox box;
    if (Box(checksum, &box)) {
        char size[64];
        std::snprintf(size, sizeof(size), "  %.0f x %.0f x %.0f", box.max.x - box.min.x, box.max.y - box.min.y,
                      box.max.z - box.min.z);
        text += size;
    }
    if (uint32_t cluster = ClusterOf(checksum)) {
        const char* name = NameOf(cluster);
        char line[64];
        std::snprintf(line, sizeof(line), "  cluster %s", name ? name : "");
        text += name ? line : "  (in a cluster)";
    }
    if (const char* why = WhyNotMovable(checksum))
        text += std::string("  (can't move: ") + why + ")";
    return text;
}

std::string DescribeSelection()
{
    if (g_selection.empty())
        return "-";
    std::string text = "scenery " + Label(g_selection[0]);
    char more[160];
    int quarters = 0;
    auto it = g_tracks.find(g_selection[0]);
    if (it != g_tracks.end())
        quarters = it->second.placement.quarters;
    std::snprintf(more, sizeof(more), "  at (%.0f, %.0f, %.0f)  turned %d", g_pivot.x, g_pivot.y, g_pivot.z,
                  quarters * 90);
    if (g_selection.size() > 1)
        text += " + " + std::to_string(g_selection.size() - 1) + " more";
    return text + more;
}

bool HasSelection() { return !g_selection.empty(); }
const std::vector<uint32_t>& Selection() { return g_selection; }

bool Selected(uint32_t checksum)
{
    return std::find(g_selection.begin(), g_selection.end(), checksum) != g_selection.end();
}

void ClearSelection() { g_selection.clear(); }

void SelectGroup(uint32_t checksum)
{
    g_selection.clear();
    if (WhyNotMovable(checksum))
        return;
    g_selection.push_back(checksum);
    uint32_t cluster = ClusterOf(checksum);
    uint8_t* scene = nullptr;
    if (cluster && FindSector(checksum, &scene)) {
        ForEachSector(scene, [&](uint32_t other, uint8_t*) {
            if (other != checksum && !Selected(other) && ClusterOf(other) == cluster && !WhyNotMovable(other))
                g_selection.push_back(other);
        });
    }
    ComputePivot();
    if (DebugEnabled()) {
        Log("level editor: selected scenery %s, %u sector%s (cluster %08X), pivot (%.1f, %.1f, %.1f)",
            Label(checksum).c_str(), static_cast<unsigned>(g_selection.size()), g_selection.size() == 1 ? "" : "s",
            cluster, g_pivot.x, g_pivot.y, g_pivot.z);
        for (uint32_t member : g_selection)
            Log("level editor:   %s", Label(member).c_str());
    }
}

void Toggle(uint32_t checksum)
{
    auto it = std::find(g_selection.begin(), g_selection.end(), checksum);
    if (it != g_selection.end())
        g_selection.erase(it);
    else if (!WhyNotMovable(checksum))
        g_selection.push_back(checksum);
    ComputePivot();
}

V3 Pivot() { return g_pivot; }

void MoveTo(V3 pivot)
{
    V3 delta = pivot - g_pivot;
    if (delta.x == 0.0f && delta.y == 0.0f && delta.z == 0.0f)
        return;
    for (uint32_t checksum : g_selection) {
        if (Track* track = TrackOf(checksum)) {
            MoveBy(&track->placement, delta);
            ApplyTrack(track);
        }
    }
    g_pivot = pivot;
}

void Turn(int quarters)
{
    if (!(quarters & 3))
        return;
    for (uint32_t checksum : g_selection) {
        if (Track* track = TrackOf(checksum)) {
            TurnAbout(&track->placement, g_pivot, quarters);
            ApplyTrack(track);
        }
    }
}

void Commit()
{
    if (g_dirtyScenes.empty())
        return;
    // As the park editor does (0x00592090): the scene's UpdateSuperSectors (thiscall, a list of sectors to add - none)
    // with the bottom-up heap current.
    Mem::Manager* manager = Mem::Manager::Get();
    for (uint8_t* scene : g_dirtyScenes) {
        bool live = false;
        ForEachScene([&](uint8_t* s) { live |= s == scene; });
        if (!live || !*reinterpret_cast<void**>(scene + 0xD4))
            continue;
        if (manager)
            manager->PushContext(manager->m_bottomUpHeap);
        GameFunction<void(__thiscall*)(void*, void*)>(0x0049D780)(scene, nullptr);
        if (manager)
            manager->PopContext();
    }
    if (DebugEnabled())
        Log("level editor: super sectors updated in %u scene%s", static_cast<unsigned>(g_dirtyScenes.size()),
            g_dirtyScenes.size() == 1 ? "" : "s");
    g_dirtyScenes.clear();
}

int MovedCount()
{
    int count = 0;
    for (const auto& entry : g_tracks)
        count += entry.second.moved ? 1 : 0;
    return count;
}

void LogCensus()
{
    // (every node of the scene list, skies too: what the walk sees)
    SceneNode* node = GameVariable<SceneNode*>(0x006FA7EC);
    for (int i = 0; node && i < 16; ++i, node = node->next) {
        bool end = IsEnd(node);
        uint8_t* scene = end ? nullptr : node->scene;
        auto* table = scene ? *reinterpret_cast<uint32_t**>(scene + 0xD0) : nullptr;
        Log("level editor: scene list node %d %p: scene %p%s, sky byte %d, name %08X, sector table %p (bits %u)", i,
            static_cast<void*>(node), static_cast<void*>(node->scene), end ? " (end)" : "", scene ? scene[0x8D] : -1,
            scene ? *reinterpret_cast<uint32_t*>(scene + 0x84) : 0, static_cast<void*>(table), table ? table[0] : 0);
        if (end)
            break;
    }
    ForEachScene([](uint8_t* scene) {
        int sectors = 0, withGeometry = 0, withCollision = 0, movable = 0;
        ForEachSector(scene, [&](uint32_t checksum, uint8_t* sector) {
            ++sectors;
            withGeometry += GeomOf(sector) ? 1 : 0;
            withCollision += CollOf(sector) ? 1 : 0;
            movable += WhyNotMovable(checksum) ? 0 : 1;
        });
        uint32_t name = *reinterpret_cast<uint32_t*>(scene + 0x84);
        Log("level editor: scene %08X (%s): %d sectors, %d with geometry, %d with collision, %d movable", name,
            NameOf(name) ? NameOf(name) : "?", sectors, withGeometry, withCollision, movable);
    });
}

} // namespace Scenery
} // namespace Front::LevelEditor
