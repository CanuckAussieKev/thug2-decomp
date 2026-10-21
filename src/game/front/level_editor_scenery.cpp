// NOT ORIGINAL - the level editor's scenery (ours, added at the user's request). See level_editor_scenery.h.

#include "game/front/level_editor_scenery.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/front/level_editor_clusters.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_view.h"
#include "game/front/level_editor_scale.h"
#include "game/front/level_editor_shadows.h"
#include "game/front/level_editor_shared_coll.h"
#include "game/front/level_editor_walls.h"
#include "game/gfx/mesh.h"
#include "game/mth/vector.h"
#include "game/script/names.h"
#include "game/script/qb.h"
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

V3 Apply(const Placement& placement, V3 x)
{
    return QuarterTurn(PerAxis(x, placement.scale), placement.quarters) + placement.offset;
}

void MoveBy(Placement* placement, V3 delta) { placement->offset = placement->offset + delta; }

Placement Compose(const Placement& outer, const Placement& inner)
{
    // outer(inner(x)) = Q_o S_o (Q_i S_i x + o_i) + o_o = Q_o Q_i (S_o' S_i) x + (Q_o S_o o_i + o_o), S_o' = S_o as
    // seen through Q_i (S_o Q_i = Q_i S_o': odd turns swap its x and z).
    return {(inner.quarters + outer.quarters) & 3, QuarterTurn(PerAxis(inner.offset, outer.scale), outer.quarters) + outer.offset,
            PerAxis(TurnedScale(outer.scale, inner.quarters), inner.scale)};
}

void TurnAbout(Placement* placement, V3 pivot, int quarters)
{
    // x'' = R (x' - pivot) + pivot, x' = Q x + offset: x'' = R Q x + R (offset - pivot) + pivot.
    placement->quarters = (placement->quarters + quarters) & 3;
    placement->offset = QuarterTurn(placement->offset - pivot, quarters) + pivot;
}

void EngineState(int quarters0, V3 position0, const Placement& placement, int* quarters, V3* position)
{
    // Loaded vertex v was at Q0 v + p0; now at Q s (Q0 v + p0) + offset = s Q Q0 v + (s Q p0 + offset).
    *quarters = (quarters0 + placement.quarters) & 3;
    *position = Apply(placement, position0);
}

OrientedBox PlacedBox(V3 min, V3 max, const Placement& placement)
{
    OrientedBox box;
    box.origin = placement.offset;
    box.axes[0] = QuarterTurn({placement.scale.x, 0.0f, 0.0f}, placement.quarters);
    box.axes[1] = {0.0f, placement.scale.y, 0.0f};
    box.axes[2] = QuarterTurn({0.0f, 0.0f, placement.scale.z}, placement.quarters);
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

bool PieceFits(V3 min, V3 max, V3 groupMin, V3 groupMax)
{
    V3 size = max - min, group = groupMax - groupMin;
    double volume = static_cast<double>(size.x) * size.y * size.z;
    double groupVolume = static_cast<double>(group.x) * group.y * group.z;
    float across = std::max(group.x, group.z) * 1.25f;
    return volume <= groupVolume && size.x <= across && size.z <= across;
}

bool PieceTouches(V3 min, V3 max, V3 boxMin, V3 boxMax, float margin)
{
    return min.x <= boxMax.x + margin && max.x >= boxMin.x - margin && min.z <= boxMax.z + margin &&
           max.z >= boxMin.z - margin && min.y <= boxMax.y + margin && max.y > boxMin.y + 5.0f;
}

void SegmentBox(V3 a, V3 b, V3* min, V3* max)
{
    *min = {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    *max = {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

uint32_t Pulse(uint32_t low, uint32_t high, double seconds)
{
    double phase = std::fmod(seconds, 1.25) / 1.25;
    double mix = 0.5 - 0.5 * std::cos(phase * 2.0 * 3.14159265358979);
    uint32_t colour = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        double a = (low >> shift) & 0xFF, b = (high >> shift) & 0xFF;
        colour |= static_cast<uint32_t>(a + (b - a) * mix + 0.5) << shift;
    }
    return colour;
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
    uint32_t checksum;
    uint8_t* scene;
    uint8_t* sector;
    uint8_t* geom;
    V3 min0, max0;
    int quarters0;
    V3 position0;
    Placement placement;
    int quarters;
    V3 position;
    V3 collisionMoved; // (how far its own collision has been moved since: position - position0 on sixteenths)
    bool moved;
    std::shared_ptr<Scale::Original> original; // (once scaled: its vertices and collision then - level_editor_scale.h)
};
std::map<uint32_t, Track> g_tracks;
std::vector<uint32_t> g_selection;
V3 g_pivot;
float g_halfHeight; // (the selection's bottom is this far below the pivot - quarter turns keep it)
// The shadows groups have taken (level_editor_shadows.h): each with the member whose name it carries (0: by place).
std::unordered_map<uint32_t, uint32_t> g_shadowOf;
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
        if (track.sector == sector && track.geom == geom && track.scene == scene)
            SharedCollision::Rebase(checksum); // (moved by something else: its shared collision's base is now too)
        g_tracks.erase(it); // (another level, or moved by something else: start again from where it is)
    }
    Track track = {};
    track.checksum = checksum;
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
// A scale (level_editor_scale.h): the first time, the sector's vertices and collision are kept as they are; from then
// on, after the engine's turn and move, they're written again from that copy, scaled.
void ApplyTrack(Track* track)
{
    // (first, while the piece is still where the track started: the level's shared collision that is its -
    // level_editor_shared_coll.h)
    SharedCollision::Ensure(track->checksum, track->scene, track->sector, track->geom);
    if (!track->original && !IsUnitScale(track->placement.scale)) {
        track->original = Scale::Capture(track->sector, track->geom, track->quarters, track->position);
        if (!track->original) {
            Log("level editor: sector %p can't be scaled (its meshes can't be copied)", static_cast<void*>(track->sector));
            track->placement.scale = {1.0f, 1.0f, 1.0f};
        }
    }
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
        // The collision by whole sixteenths of a unit from where the track started: its BSP tree's splits are
        // sixteenths and each Translate (0x00415D80) rounds them towards zero, so fractional steps (every frame of a
        // drag) would let the tree drift away from its faces; on sixteenths every step is exact and moving back gives
        // the level's tree again (as level_editor_walls.h keeps walls). The collision is then within 1/32 unit of the
        // meshes.
        V3 collisionMoved = Walls::OnGrid(position - track->position0);
        uint8_t* coll = CollOf(track->sector);
        if (coll && !SameV3(collisionMoved, track->collisionMoved)) {
            const Mth::Vector* origin = Method<const Mth::Vector*(__thiscall*)(void*)>(coll, 0x1C)(coll);
            V3 change = collisionMoved - track->collisionMoved;
            Mth::Vector moved = {origin->x + change.x, origin->y + change.y, origin->z + change.z, origin->w};
            Method<void(__thiscall*)(void*, const Mth::Vector*)>(coll, 0x14)(coll, &moved);
        }
        track->collisionMoved = collisionMoved;
        track->position = position;
    }
    if (track->original &&
        !Scale::Rebuild(*track->original, track->sector, track->geom, track->quarters, track->position,
                        track->placement.scale, track->quarters0))
        track->original.reset(); // (not the sector copied any more: left as the engine has it)
    SharedCollision::Place(track->checksum, track->placement);
    if (FlagsOf(track->sector) & 0x40)
        FlagsOf(track->sector) |= 0x20;
    track->moved = true;
    MarkDirty(track->scene);
}

const char* NameOf(uint32_t checksum)
{
    if (const char* name = Script::KeptQbName(checksum))
        return name;
    return Script::FindChecksumName(checksum);
}

std::string Label(uint32_t checksum)
{
    char text[96];
    const char* name = NameOf(checksum);
    if (name)
        std::snprintf(text, sizeof(text), "%s", name);
    else
        std::snprintf(text, sizeof(text), "%08X", checksum);
    return text;
}

// A node's TrickObject Cluster= (0: none), by its name (0x0045C1B0) - kept, as looked up (the level doesn't change
// while the editor is on; Reset forgets them).
std::unordered_map<uint32_t, uint32_t> g_clusters;
uint32_t ClusterOf(uint32_t checksum)
{
    // (a cluster the editor set - level_editor_clusters.h: its group follows it)
    uint32_t edited = 0;
    if (Clusters::Override(checksum, &edited))
        return edited;
    auto it = g_clusters.find(checksum);
    if (it != g_clusters.end())
        return it->second;
    auto* node = GameFunction<Script::CStruct*(__cdecl*)(uint32_t, int)>(0x0045C1B0)(checksum, 0);
    uint32_t cluster = 0;
    if (node)
        node->GetChecksum(kCluster, &cluster, 0);
    g_clusters.emplace(checksum, cluster);
    return cluster;
}

// --- grind rails ---
//
// The level's rails: the rail manager of Mdl::Skate (0x007CE478, +0x24) - filled from the node array's RailNodes and
// ClimbingNodes by 0x0057E960 (from CFunc_ParseNodeArray 0x005A34E0): +0x40 its nodes, +0x48 their count, +0x50 set
// when its rails ride an object (positions in the object's space, 0x0057CB60 - never the level's).
// A node (0x54 bytes, 0x0057D090): +0x00 / +0x10 the box of its segment (to the next node; its own point when it has
// none - 0x0057C260), +0x20 its position, +0x30 (climbing nodes), +0x40 the next node, +0x44 the previous, +0x48 flags
// (4 active, 0x10 a climbing node), +0x4C its node's name, +0x50 its terrain. Skaters find rails by testing every
// node's box (0x0057E2D0): moving one is its position and the boxes of its segment and the one before.
struct RailNode {
    float min[4], max[4], pos[4], climb[4];
    RailNode* next;
    RailNode* prev;
    uint32_t flags;
    uint32_t name;
    uint8_t terrain[4];
};
static_assert(sizeof(RailNode) == 0x54, "a rail node is 0x54 bytes");

bool RailNodes(RailNode** nodes, int* count)
{
    uint8_t* skate = GameVariable<uint8_t*>(0x007CE478);
    uint8_t* manager = skate ? *reinterpret_cast<uint8_t**>(skate + 0x24) : nullptr;
    if (!manager || manager[0x50])
        return false;
    *nodes = *reinterpret_cast<RailNode**>(manager + 0x40);
    *count = *reinterpret_cast<int*>(manager + 0x48);
    return *nodes && *count > 0 && *count < 100000;
}

V3 RailPos(const RailNode* node) { return {node->pos[0], node->pos[1], node->pos[2]}; }

// What we've done to a rail node: where it was when first touched, the placement since, where we put it.
struct RailTrack {
    RailNode* node;
    bool active0; // (flag 4 when first touched: some rails start off)
    V3 pos0;
    Placement placement;
    V3 pos;
};
std::map<uint32_t, RailTrack> g_railTracks;
std::vector<uint32_t> g_rails; // (the selection's rail nodes, by name)
std::vector<uint32_t> g_droppedRails; // (Ctrl+click / the Inspector's x: not carried until another group is selected)

RailNode* FindRail(uint32_t name)
{
    RailNode* nodes;
    int count;
    if (!RailNodes(&nodes, &count))
        return nullptr;
    // (a ladder's point too - its moves are tracked as a rail's: level_editor_chains.h)
    for (int i = 0; i < count; ++i) {
        if (nodes[i].name == name)
            return &nodes[i];
    }
    return nullptr;
}

RailTrack* RailTrackOf(uint32_t name)
{
    RailNode* node = FindRail(name);
    if (!node)
        return nullptr;
    auto it = g_railTracks.find(name);
    if (it != g_railTracks.end()) {
        if (it->second.node == node && SameV3(it->second.pos, RailPos(node)))
            return &it->second;
        g_railTracks.erase(it);
    }
    RailTrack track = {node, (node->flags & 4) != 0, RailPos(node), {0, {0.0f, 0.0f, 0.0f}}, RailPos(node)};
    return &g_railTracks.emplace(name, track).first->second;
}

uint32_t g_railGeneration = 1;

// The rail nodes `names` placed where their tracks say, then the boxes of their segments and of those ending at
// them.
void ApplyRails(const std::vector<uint32_t>& names)
{
    RailNode* nodes;
    int count;
    if (!RailNodes(&nodes, &count))
        return;
    ++g_railGeneration; // (the node layer reads the places again)
    std::vector<RailNode*> moved;
    for (uint32_t name : names) {
        auto it = g_railTracks.find(name);
        if (it == g_railTracks.end())
            continue;
        RailTrack& track = it->second;
        V3 pos = Apply(track.placement, track.pos0);
        track.node->pos[0] = pos.x;
        track.node->pos[1] = pos.y;
        track.node->pos[2] = pos.z;
        track.pos = pos;
        moved.push_back(track.node);
    }
    auto isMoved = [&](const RailNode* node) {
        return node && std::find(moved.begin(), moved.end(), node) != moved.end();
    };
    for (int i = 0; i < count; ++i) {
        RailNode* node = &nodes[i];
        if (!isMoved(node) && !isMoved(node->next))
            continue;
        V3 min, max;
        SegmentBox(RailPos(node), node->next ? RailPos(node->next) : RailPos(node), &min, &max);
        node->min[0] = min.x, node->min[1] = min.y, node->min[2] = min.z, node->min[3] = 1.0f;
        node->max[0] = max.x, node->max[1] = max.y, node->max[2] = max.z, node->max[3] = 1.0f;
    }
}

void ComputePivot()
{
    V3 min = {}, max = {};
    bool have = false;
    // (its shadows left out while it has anything else: a shadow lies wider than its caster, on the floor below)
    bool casters = std::any_of(g_selection.begin(), g_selection.end(),
                               [](uint32_t checksum) { return !g_shadowOf.count(checksum); });
    for (uint32_t checksum : g_selection) {
        if (casters && g_shadowOf.count(checksum))
            continue;
        OrientedBox box;
        if (!Box(checksum, &box))
            continue;
        V3 a, b;
        BoundsOf(box, &a, &b);
        GrowBox(&min, &max, have, a, b);
        have = true;
    }
    g_pivot = have ? (min + max) * 0.5f : V3{0.0f, 0.0f, 0.0f};
    g_halfHeight = have ? (max.y - min.y) * 0.5f : 0.0f;
}

// The selection's rails: rail nodes of the selected sectors' clusters (the fire truck's TRG_FireTruck01..25 are in
// Fire_Tender), and ones lying within a selected sector's box (both ends, within 10 units).
void SelectRails()
{
    g_rails.clear();
    RailNode* nodes;
    int count;
    if (g_selection.empty() || !RailNodes(&nodes, &count))
        return;
    std::vector<uint32_t> clusters;
    std::vector<std::pair<V3, V3>> boxes;
    for (uint32_t checksum : g_selection) {
        if (IsClone(checksum))
            continue; // (a copy has no rails of its own: the source's stay with the source)
        clusters.push_back(checksum);
        if (uint32_t cluster = ClusterOf(checksum))
            clusters.push_back(cluster);
        OrientedBox box;
        V3 min, max;
        if (!g_shadowOf.count(checksum) && Box(checksum, &box)) { // (not by a shadow's box: rails lying over it)
            BoundsOf(box, &min, &max);
            boxes.push_back({min - V3{10.0f, 10.0f, 10.0f}, max + V3{10.0f, 10.0f, 10.0f}});
        }
    }
    auto within = [&](V3 p) {
        for (const auto& box : boxes) {
            if (p.x >= box.first.x && p.x <= box.second.x && p.y >= box.first.y && p.y <= box.second.y &&
                p.z >= box.first.z && p.z <= box.second.z)
                return true;
        }
        return false;
    };
    auto listed = [&](uint32_t c) { return c && std::find(clusters.begin(), clusters.end(), c) != clusters.end(); };
    for (int i = 0; i < count; ++i) {
        const RailNode& node = nodes[i];
        if (node.flags & 0x10)
            continue;
        bool in = listed(node.name) || listed(ClusterOf(node.name)) ||
                  (within(RailPos(&node)) && (!node.next || within(RailPos(node.next))));
        if (in && std::find(g_droppedRails.begin(), g_droppedRails.end(), node.name) != g_droppedRails.end())
            in = false;
        if (in && std::find(g_rails.begin(), g_rails.end(), node.name) == g_rails.end())
            g_rails.push_back(node.name);
    }
}

// Where the editor has put a sector (none yet: where the level has it).
Placement PlacementOf(uint32_t checksum, uint8_t* geom)
{
    auto it = g_tracks.find(checksum);
    if (it != g_tracks.end() && it->second.geom == geom)
        return it->second.placement;
    return {0, {0.0f, 0.0f, 0.0f}};
}

bool GeomSeeThrough(uint8_t* geom); // (below)

bool SamePlacement(const Placement& a, const Placement& b)
{
    return a.quarters == b.quarters && std::fabs(a.scale.x - b.scale.x) < 1e-4f && std::fabs(a.scale.y - b.scale.y) < 1e-4f &&
           std::fabs(a.scale.z - b.scale.z) < 1e-4f && std::fabs(a.offset.x - b.offset.x) < 0.5f &&
           std::fabs(a.offset.y - b.offset.y) < 0.5f && std::fabs(a.offset.z - b.offset.z) < 0.5f;
}

// The group a click on a sector selects: the sector, the rest of its cluster, and the movable pieces built onto them
// (PieceFits / PieceTouches, three rounds - each with its cluster). Decided on the level's own layout (the boxes as
// loaded) and only among pieces the editor has moved together (the same placement): a group moved into something
// doesn't take it in, and a piece moved away from its group no longer brings the group along. Hidden pieces aren't
// taken, nor structure (Picking::IsStructure by size - a floor, a wall) unless the clicked piece is structure itself:
// a level's own clusters can join a small piece to the floor it lies on (TR's SkWare_Main_12, 360 x 60 x 924, and the
// 3000 x 99 x 2431 floor SkWare_Main_9), and a click on the small piece mustn't take the floor. Then the group's shadows
// (level_editor_shadows.h).
std::vector<uint32_t> GroupOf(uint32_t checksum)
{
    std::vector<uint32_t> group;
    uint8_t* scene = nullptr;
    uint8_t* clicked = FindSector(checksum, &scene);
    if (WhyNotMovable(checksum) || !clicked)
        return group;
    Placement placement = PlacementOf(checksum, GeomOf(clicked));
    struct Piece {
        uint32_t checksum;
        V3 min, max;
        bool in;
        bool seeThrough;
    };
    std::vector<Piece> pieces; // (the scene's shown sectors small enough to move, moved as the clicked one)
    ForEachSector(scene, [&](uint32_t other, uint8_t* sector) {
        uint8_t* geom = GeomOf(sector);
        V3 min, max;
        if (!PlainGeom(geom) || !geom[0x48] || !GeomBox(geom, &min, &max) ||
            !SectorSizeMovable(min, max, kMaxSectorExtent) || !SamePlacement(PlacementOf(other, geom), placement))
            return;
        pieces.push_back({other, min, max, false, GeomSeeThrough(geom)});
    });
    // (by size only: a camera nowhere near)
    const V3 nowhere = {1.0e9f, 1.0e9f, 1.0e9f};
    bool clickedStructure = false;
    for (const Piece& piece : pieces) {
        if (piece.checksum == checksum)
            clickedStructure = Picking::IsStructure(piece.min, piece.max, nowhere);
    }
    auto take = [&](Piece& piece) {
        if (piece.in || WhyNotMovable(piece.checksum))
            return false;
        if (piece.checksum != checksum && !clickedStructure && Picking::IsStructure(piece.min, piece.max, nowhere))
            return false;
        piece.in = true;
        group.push_back(piece.checksum);
        return true;
    };
    uint32_t cluster = ClusterOf(checksum);
    for (Piece& piece : pieces) {
        if (piece.checksum == checksum || (cluster && ClusterOf(piece.checksum) == cluster))
            take(piece);
    }
    if (group.empty())
        return group;
    V3 groupMin = {}, groupMax = {};
    bool have = false;
    for (const Piece& piece : pieces) {
        if (piece.in) {
            GrowBox(&groupMin, &groupMax, have, piece.min, piece.max);
            have = true;
        }
    }
    // (only a free-standing thing grows: not a floor, a wall or a whole building - what stands on or against those isn't
    // part of them)
    V3 groupSize = groupMax - groupMin;
    bool grows = std::min({groupSize.x, groupSize.y, groupSize.z}) >= 20.0f &&
                 std::max({groupSize.x, groupSize.y, groupSize.z}) <= 1500.0f;
    for (int round = 0; grows && round < 3; ++round) {
        std::vector<uint32_t> joined;
        for (Piece& piece : pieces) {
            if (piece.in || !PieceFits(piece.min, piece.max, groupMin, groupMax))
                continue;
            for (const Piece& member : pieces) {
                if (member.in && PieceTouches(piece.min, piece.max, member.min, member.max, 2.0f)) {
                    joined.push_back(piece.checksum);
                    break;
                }
            }
        }
        if (joined.empty())
            break;
        for (uint32_t joining : joined) {
            uint32_t joiningCluster = ClusterOf(joining);
            for (Piece& piece : pieces) {
                if (piece.checksum == joining ||
                    (joiningCluster && ClusterOf(piece.checksum) == joiningCluster &&
                     PieceFits(piece.min, piece.max, groupMin, groupMax))) {
                    if (take(piece) && DebugEnabled())
                        Log("level editor:   joins: %s (%.0f x %.0f x %.0f)", Label(piece.checksum).c_str(),
                            piece.max.x - piece.min.x, piece.max.y - piece.min.y, piece.max.z - piece.min.z);
                }
            }
        }
    }
    // Its shadows (level_editor_shadows.h): by name, then - for shadow-named pieces nobody owns by name - by place,
    // under one of its pieces. Not by place for a floor or wall clicked by itself.
    auto sourceOf = [](uint32_t of) {
        for (const Clone& clone : Clones()) {
            if (clone.checksum == of)
                return clone.source;
        }
        return of;
    };
    for (Piece& piece : pieces) {
        if (piece.in || WhyNotMovable(piece.checksum))
            continue;
        uint32_t source = sourceOf(piece.checksum);
        const char* name = NameOf(source);
        if (!name || !Shadows::ShadowName(name))
            continue;
        uint32_t owner = 0;
        for (const std::string& candidate : Shadows::OwnerNames(name)) {
            uint32_t crc = Crc::GenerateCRCFromString(candidate.c_str());
            if (crc != source && LookupSector(scene, crc)) {
                owner = crc;
                break;
            }
        }
        uint32_t caster = 0;
        const char* how = nullptr;
        if (owner) {
            // (a copy's shadow: a copy of its owner, and by it - copies of one piece share names)
            bool clone = source != piece.checksum;
            for (const Piece& member : pieces) {
                if (member.in && sourceOf(member.checksum) == owner && (member.checksum != owner) == clone &&
                    (!clone || Shadows::Beside(piece.min, piece.max, member.min, member.max))) {
                    caster = member.checksum;
                    how = "by name";
                    break;
                }
            }
        } else if (!clickedStructure && piece.seeThrough) {
            // (under one of the group's pieces - not a shadow taken here - and nearer to it than to anything else
            // standing there)
            std::vector<std::pair<V3, V3>> others;
            for (const Piece& other : pieces) {
                if (!other.in && !other.seeThrough && Shadows::StandsBy(other.min, piece.max) &&
                    !Picking::IsStructure(other.min, other.max, nowhere))
                    others.push_back({other.min, other.max});
            }
            for (const Piece& member : pieces) {
                if (member.in && !g_shadowOf.count(member.checksum) &&
                    Shadows::FallsUnder(piece.min, piece.max, member.min, member.max) &&
                    Shadows::NearestToGroup(piece.min, piece.max, member.min, member.max, others)) {
                    how = "by place";
                    caster = member.checksum;
                    break;
                }
            }
        }
        if (!how)
            continue;
        piece.in = true;
        group.push_back(piece.checksum);
        g_shadowOf[piece.checksum] = caster;
        if (DebugEnabled())
            Log("level editor:   shadow %s: %s (%.0f x %.0f x %.0f)%s%s", how, Label(piece.checksum).c_str(),
                piece.max.x - piece.min.x, piece.max.y - piece.min.y, piece.max.z - piece.min.z,
                caster ? " of " : "", caster ? Label(caster).c_str() : "");
    }
    return group;
}

// The hovered sector's group (what a click would select), kept while the same sector is hovered.
uint32_t g_hoverGroupOf;
std::vector<uint32_t> g_hoverGroup;

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

namespace {

// The ray against a geometry's meshes as drawn (their vertex copies are where the sector is now: moving it moves
// them): each mesh's first index buffer as a triangle strip; a billboard (turned to the camera when drawn) by its
// sphere.
bool RayMeshes(uint8_t* geom, V3 origin, V3 dir, float maxT, float* t)
{
    auto** meshes = *reinterpret_cast<Nx::sMesh***>(geom + 0x3C);
    uint32_t count = *reinterpret_cast<uint32_t*>(geom + 0x40);
    bool found = false;
    float best = maxT;
    for (uint32_t i = 0; meshes && i < count && i < 4096; ++i) {
        Nx::sMesh* mesh = meshes[i];
        if (!mesh)
            continue;
        float hit;
        if (mesh->billboard || (mesh->flags & 0x10)) {
            if (RaySphere(origin, dir, {mesh->centre[0], mesh->centre[1], mesh->centre[2]}, mesh->radius, &hit) &&
                hit < best) {
                best = hit;
                found = true;
            }
            continue;
        }
        Nx::sVertexBuffer* vertices = mesh->vertexBufferCount ? mesh->vertexBuffers[0] : nullptr;
        Nx::sIndexBuffer* indices = mesh->indexBuffers[0];
        if (!vertices || !vertices->data || !indices || !indices->indices || mesh->stride < 12)
            continue;
        if (Picking::RayStrip(origin, dir, vertices->data, mesh->stride, mesh->vertexCount, indices->indices,
                              mesh->indexCounts[0], best, &hit)) {
            best = hit;
            found = true;
        }
    }
    if (found)
        *t = best;
    return found;
}

// Whether a geometry's meshes are all alpha-blended (the scene's own test: the material's +0x20 flag 0x40).
bool GeomSeeThrough(uint8_t* geom)
{
    auto** meshes = *reinterpret_cast<Nx::sMesh***>(geom + 0x3C);
    uint32_t count = *reinterpret_cast<uint32_t*>(geom + 0x40);
    if (!meshes || count == 0 || count > 4096)
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* material = meshes[i] ? meshes[i]->material : nullptr;
        if (!material || !(material[0x20] & 0x40))
            return false;
    }
    return true;
}

} // namespace

bool SeeThrough(uint32_t checksum)
{
    uint8_t* sector = FindSector(checksum, nullptr);
    return sector && PlainGeom(GeomOf(sector)) && GeomSeeThrough(GeomOf(sector));
}

bool Passed(uint32_t checksum, V3 camera)
{
    uint8_t* sector = FindSector(checksum, nullptr);
    uint8_t* geom = sector ? GeomOf(sector) : nullptr;
    if (!PlainGeom(geom))
        return false;
    OrientedBox box;
    V3 min, max;
    if (CurrentBox(checksum, geom, &box)) {
        BoundsOf(box, &min, &max);
        if (Picking::IsStructure(min, max, camera))
            return true;
    }
    return GeomSeeThrough(geom);
}

bool Pick(V3 origin, V3 unitDir, float maxT, Hit* hit, std::vector<Layer>* layers)
{
    float best = maxT;
    bool found = false;
    *hit = Hit{};
    auto note = [&](uint32_t checksum, float t) {
        // (locked: passed by every click; structure / see-through: by a plain click - the nearest noted for Alt)
        if (Picking::Locked(Picking::kScenery, checksum) || View::HiddenSector(checksum)) // (H: level_editor_view.h)
            return false;
        bool passed = Passed(checksum, origin);
        if (layers && std::none_of(layers->begin(), layers->end(), [&](const Layer& l) { return l.checksum == checksum; }))
            layers->push_back({checksum, t, passed});
        if (passed) {
            if (!hit->passed || t < hit->passedT) {
                hit->passed = checksum;
                hit->passedT = t;
            }
            return false;
        }
        if (t < best) {
            best = t;
            hit->checksum = checksum;
            hit->t = t;
            found = true;
        }
        return true;
    };
    // The collision ray, past collision that has no geometry to show (invisible walls) and pieces passed, at most a
    // few dozen times.
    V3 from = origin;
    for (int i = 0; i < 32; ++i) {
        V3 point;
        uint32_t checksum;
        if (!CastStatic(from, origin + unitDir * maxT, &point, &checksum))
            break;
        float t = Dot(point - origin, unitDir);
        if (t >= best && !layers)
            break;
        uint8_t* sector = FindSector(checksum, nullptr);
        if (sector && GeomOf(sector) && note(checksum, t) && !layers)
            break;
        from = point + unitDir * 1.0f;
    }
    // Sectors without collision: by their triangles (the box only to skip the rest - a decal's box can fill a room).
    ForEachScene([&](uint8_t* scene) {
        ForEachSector(scene, [&](uint32_t checksum, uint8_t* sector) {
            uint8_t* geom = GeomOf(sector);
            if (CollOf(sector) || !PlainGeom(geom) || !geom[0x48])
                return;
            OrientedBox box;
            if (!CurrentBox(checksum, geom, &box))
                return;
            float t;
            bool inside = false;
            {
                V3 min, max;
                BoundsOf(box, &min, &max);
                inside = origin.x >= min.x && origin.x <= max.x && origin.y >= min.y && origin.y <= max.y &&
                         origin.z >= min.z && origin.z <= max.z;
            }
            if (!inside && !RayBox(origin, unitDir, box.origin, box.axes, box.min, box.max, &t))
                return;
            if (RayMeshes(geom, origin, unitDir, maxT, &t))
                note(checksum, t);
        });
    });
    if (layers)
        std::stable_sort(layers->begin(), layers->end(), [](const Layer& a, const Layer& b) { return a.t < b.t; });
    return found || hit->passed;
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
        V3 size3 = BoxSize(box); // (scaled: level_editor_scale.h)
        std::snprintf(size, sizeof(size), "  %.0f x %.0f x %.0f", size3.x, size3.y, size3.z);
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
    V3 scale = {1.0f, 1.0f, 1.0f};
    auto it = g_tracks.find(g_selection[0]);
    if (it != g_tracks.end()) {
        quarters = it->second.placement.quarters;
        scale = it->second.placement.scale;
    }
    std::snprintf(more, sizeof(more), "  at (%.0f, %.0f, %.0f)  turned %d", g_pivot.x, g_pivot.y, g_pivot.z,
                  quarters * 90);
    if (!IsUnitScale(scale)) {
        size_t used = std::strlen(more);
        std::snprintf(more + used, sizeof(more) - used, "  scaled %s", Scale::Describe(scale).c_str());
    }
    if (g_selection.size() > 1)
        text += " + " + std::to_string(g_selection.size() - 1) + " more";
    if (!g_rails.empty())
        text += ", " + std::to_string(g_rails.size()) + " rail points";
    return text + more;
}

bool HasSelection() { return !g_selection.empty(); }
const std::vector<uint32_t>& Selection() { return g_selection; }

bool Selected(uint32_t checksum)
{
    return std::find(g_selection.begin(), g_selection.end(), checksum) != g_selection.end();
}

void ClearSelection()
{
    g_selection.clear();
    g_rails.clear();
    g_droppedRails.clear();
}

bool IsShadow(uint32_t checksum, uint32_t* caster)
{
    auto it = g_shadowOf.find(checksum);
    if (it == g_shadowOf.end())
        return false;
    if (caster)
        *caster = it->second;
    return true;
}

const std::vector<uint32_t>& SelectedRailNames() { return g_rails; }

bool CarriesRail(uint32_t name) { return std::find(g_rails.begin(), g_rails.end(), name) != g_rails.end(); }

void DropRail(uint32_t name)
{
    if (!CarriesRail(name))
        return;
    g_droppedRails.push_back(name);
    g_rails.erase(std::find(g_rails.begin(), g_rails.end(), name));
}

void KeepOnly(uint32_t checksum)
{
    g_droppedRails.clear();
    SetSelection({checksum});
}

const std::vector<uint32_t>& HoverGroup(uint32_t checksum)
{
    if (checksum != g_hoverGroupOf) {
        g_hoverGroupOf = checksum;
        g_hoverGroup = checksum ? GroupOf(checksum) : std::vector<uint32_t>{};
    }
    return g_hoverGroup;
}

// [features] debug_editor: how the first selected sector's meshes are drawn (whether the material lets a colour
// through).
void LogMeshes()
{
    if (!DebugEnabled() || g_selection.empty())
        return;
    uint8_t* geom = SectorGeometry(g_selection[0]);
    auto** meshes = geom ? *reinterpret_cast<uint8_t***>(geom + 0x3C) : nullptr;
    uint32_t count = geom ? *reinterpret_cast<uint32_t*>(geom + 0x40) : 0;
    for (uint32_t i = 0; meshes && i < count && i < 4; ++i) {
        uint8_t* mesh = meshes[i];
        uint8_t* material = *reinterpret_cast<uint8_t**>(mesh + 0x14);
        const uint32_t* m = reinterpret_cast<const uint32_t*>(material);
        const float* colour = reinterpret_cast<const float*>(mesh + 0x80);
        Log("level editor:   mesh %u: flags %08X, colour (%.2f, %.2f, %.2f), fvf %08X, material %p: passes %u, "
            "pass flags %08X %08X, colour 0 %08X %08X %08X; renderer 786AB8 %d",
            i, *reinterpret_cast<uint32_t*>(mesh), colour[0], colour[1], colour[2],
            *reinterpret_cast<uint32_t*>(mesh + 0x38), static_cast<void*>(material), material ? m[2] & 0xFFFFFFF : 0,
            material ? m[8] : 0, material ? m[9] : 0, material ? m[0x10] : 0, material ? m[0x11] : 0,
            material ? m[0x12] : 0, GameVariable<uint8_t>(0x00786AB8));
    }
}

void SelectGroup(uint32_t checksum)
{
    g_selection = GroupOf(checksum);
    g_droppedRails.clear();
    ComputePivot();
    SelectRails();
    LogMeshes();
    if (DebugEnabled()) {
        Log("level editor: selected scenery %s, %u sector%s (cluster %08X), %u rail node%s, pivot (%.1f, %.1f, %.1f)",
            Label(checksum).c_str(), static_cast<unsigned>(g_selection.size()), g_selection.size() == 1 ? "" : "s",
            ClusterOf(checksum), static_cast<unsigned>(g_rails.size()), g_rails.size() == 1 ? "" : "s", g_pivot.x,
            g_pivot.y, g_pivot.z);
        for (uint32_t member : g_selection)
            Log("level editor:   %s%s", Label(member).c_str(), IsShadow(member) ? " (shadow)" : "");
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
    SelectRails();
}

V3 Pivot() { return g_pivot; }

float Bottom() { return g_pivot.y - g_halfHeight; }

void SetSelection(const std::vector<uint32_t>& sectors)
{
    g_selection.clear();
    for (uint32_t checksum : sectors) {
        if (!WhyNotMovable(checksum))
            g_selection.push_back(checksum);
    }
    ComputePivot();
    SelectRails();
}

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
    for (uint32_t name : g_rails) {
        if (RailTrack* track = RailTrackOf(name))
            MoveBy(&track->placement, delta);
    }
    ApplyRails(g_rails);
    g_pivot = pivot;
    g_hoverGroupOf = 0;
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
    LogMeshes();
    for (uint32_t name : g_rails) {
        if (RailTrack* track = RailTrackOf(name))
            TurnAbout(&track->placement, g_pivot, quarters);
    }
    ApplyRails(g_rails);
    g_hoverGroupOf = 0;
}

// --- scaling (level_editor_scale.h) ---

V3 SelectionScale()
{
    if (g_selection.empty())
        return {1.0f, 1.0f, 1.0f};
    uint8_t* sector = FindSector(g_selection[0], nullptr);
    return sector ? PlacementOf(g_selection[0], GeomOf(sector)).scale : V3{1.0f, 1.0f, 1.0f};
}

int SelectionQuarters()
{
    if (g_selection.empty())
        return 0;
    uint8_t* sector = FindSector(g_selection[0], nullptr);
    return sector ? PlacementOf(g_selection[0], GeomOf(sector)).quarters : 0;
}

V3 ScaleSelection(V3 factor, bool uniform)
{
    const V3 unchanged = {1.0f, 1.0f, 1.0f};
    std::vector<V3> scales, limits; // (along the world's axes)
    for (uint32_t checksum : g_selection) {
        if (Track* track = TrackOf(checksum)) {
            if (!track->original) // (kept now, as ApplyTrack would: a piece that can't be copied stops the lot)
                track->original = Scale::Capture(track->sector, track->geom, track->quarters, track->position);
            if (!track->original) {
                Log("level editor: %s can't be scaled (its meshes can't be copied)", Label(checksum).c_str());
                return unchanged;
            }
            V3 world = TurnedScale(track->placement.scale, track->placement.quarters);
            scales.push_back(world);
            limits.push_back(Scale::Limit(track->sector, world));
        }
    }
    factor = Scale::AllowedFactor(factor, scales, limits, uniform);
    if (IsUnitScale(factor) || scales.empty())
        return unchanged;
    // About the middle of the selection's bottom: it keeps standing where it stands.
    V3 anchor = {g_pivot.x, Bottom(), g_pivot.z};
    for (uint32_t checksum : g_selection) {
        if (Track* track = TrackOf(checksum)) {
            Scale::ScaleAbout(&track->placement, anchor, factor);
            ApplyTrack(track);
        }
    }
    for (uint32_t name : g_rails) {
        if (RailTrack* track = RailTrackOf(name))
            Scale::ScaleAbout(&track->placement, anchor, factor);
    }
    ApplyRails(g_rails);
    g_halfHeight *= factor.y;
    g_pivot = {anchor.x, anchor.y + g_halfHeight, anchor.z};
    g_hoverGroupOf = 0;
    return factor;
}

std::vector<std::pair<V3, V3>> SelectedRails()
{
    std::vector<std::pair<V3, V3>> segments;
    for (uint32_t name : g_rails) {
        const RailNode* node = FindRail(name);
        if (node && node->next)
            segments.push_back({RailPos(node), RailPos(node->next)});
    }
    return segments;
}

int SelectedRailCount() { return static_cast<int>(g_rails.size()); }

// --- grind rail points one by one ---

bool RailPoint(uint32_t name, V3* pos, bool climbing)
{
    const RailNode* node = FindRail(name);
    if (!node || ((node->flags & 0x10) && !climbing))
        return false;
    *pos = RailPos(node);
    return true;
}

bool RailOrigin(uint32_t name, V3* pos)
{
    RailTrack* track = RailTrackOf(name);
    if (!track)
        return false;
    *pos = track->pos0;
    return true;
}

bool SetRailPoint(uint32_t name, V3 pos)
{
    RailTrack* track = RailTrackOf(name);
    if (!track)
        return false;
    // (the quarter turns a sector move gave it don't matter for one point: a plain move from where the level has it -
    // Home's place gives an offset of exactly 0)
    track->placement = {0, pos - track->pos0};
    ApplyRails({name});
    return true;
}

std::vector<uint32_t> RailOf(uint32_t name)
{
    std::vector<uint32_t> names;
    RailNode* node = FindRail(name);
    RailNode* nodes;
    int count;
    if (!node || !RailNodes(&nodes, &count))
        return names;
    names.push_back(name);
    // (back to its first point, then along to its last - a closed rail stops where it started)
    RailNode* first = node;
    for (int steps = 0; first->prev && first->prev != node && steps < count; ++steps)
        first = first->prev;
    int steps = 0;
    for (RailNode* at = first; at && steps < count; at = at->next, ++steps) {
        // (a rail's points, or a ladder's: not the other kind)
        if (at != node && (at->flags & 0x10) == (node->flags & 0x10) &&
            std::find(names.begin(), names.end(), at->name) == names.end())
            names.push_back(at->name);
        if (at->next == first)
            break;
    }
    return names;
}

uint32_t RailGeneration() { return g_railGeneration; }

void RailsRebuilt()
{
    // (the tracks keep their nodes by address: found again by name in the new array - places and flags were copied)
    for (auto it = g_railTracks.begin(); it != g_railTracks.end();) {
        if (RailNode* node = FindRail(it->first)) {
            it->second.node = node;
            ++it;
        } else {
            it = g_railTracks.erase(it);
        }
    }
    ++g_railGeneration;
}

void ForEachRailPoint(void (*each)(void* context, uint32_t name, V3 pos), void* context)
{
    RailNode* nodes;
    int count;
    if (!RailNodes(&nodes, &count))
        return;
    for (int i = 0; i < count; ++i)
        each(context, nodes[i].name, RailPos(&nodes[i]));
}

// --- the selection's state (undo / redo, reset, hiding) ---

namespace {

// CSector::SetActive (0x0049E9B0, thiscall, RET 4: the geometry's active flag - vfunc +0x34 - and the collision's
// "off" flag 4) / IsActive (0x0049E9E0).
void SetActive(uint8_t* sector, bool active) { GameFunction<void(__thiscall*)(void*, bool)>(0x0049E9B0)(sector, active); }
bool IsActive(uint8_t* sector) { return GameFunction<bool(__thiscall*)(void*)>(0x0049E9E0)(sector); }

} // namespace

State SaveState()
{
    State state;
    state.selection = g_selection;
    for (uint32_t checksum : g_selection) {
        uint8_t* sector = FindSector(checksum, nullptr);
        if (!sector)
            continue;
        state.sectors.push_back({checksum, PlacementOf(checksum, GeomOf(sector)), !IsActive(sector)});
    }
    for (uint32_t name : g_rails) {
        if (RailTrack* track = RailTrackOf(name))
            state.rails.push_back({name, track->placement, track->active0 && !(track->node->flags & 4)});
    }
    return state;
}

void ApplyStates(const std::vector<SectorState>& sectors, const std::vector<RailState>& rails)
{
    for (const SectorState& saved : sectors) {
        Track* track = TrackOf(saved.checksum);
        if (!track)
            continue;
        if (track->placement.quarters != saved.placement.quarters ||
            !SameV3(track->placement.offset, saved.placement.offset) || !SameV3(track->placement.scale, saved.placement.scale)) {
            track->placement = saved.placement;
            ApplyTrack(track);
        }
        if (IsActive(track->sector) == saved.hidden) {
            if (saved.hidden) // (its shared collision taken first, to go off with it)
                SharedCollision::Ensure(track->checksum, track->scene, track->sector, track->geom);
            SetActive(track->sector, !saved.hidden);
            MarkDirty(track->scene);
        }
        SharedCollision::SetActive(track->checksum, !saved.hidden);
    }
    std::vector<uint32_t> names;
    for (const RailState& saved : rails) {
        if (RailTrack* track = RailTrackOf(saved.name)) {
            track->placement = saved.placement;
            bool active = track->active0 && !saved.hidden;
            track->node->flags = active ? track->node->flags | 4u : track->node->flags & ~4u;
            names.push_back(saved.name);
        }
    }
    ApplyRails(names);
    ComputePivot();
    g_hoverGroupOf = 0;
    Commit();
}

void RestoreState(const State& state)
{
    ApplyStates(state.sectors, state.rails);
    g_rails.clear();
    for (const RailState& saved : state.rails)
        g_rails.push_back(saved.name);
    g_selection = state.selection;
    ComputePivot();
}

std::vector<SectorState> ChangedSectors()
{
    std::vector<SectorState> changed;
    for (const auto& entry : g_tracks) {
        const Track& track = entry.second;
        if (IsClone(entry.first) || FindSector(entry.first, nullptr) != track.sector)
            continue;
        bool hidden = !IsActive(track.sector);
        const Placement& p = track.placement;
        if (hidden || p.quarters || !IsUnitScale(p.scale) || p.offset.x != 0.0f || p.offset.y != 0.0f || p.offset.z != 0.0f)
            changed.push_back({entry.first, p, hidden});
    }
    return changed;
}

std::vector<RailState> ChangedRails()
{
    std::vector<RailState> changed;
    for (const auto& entry : g_railTracks) {
        const RailTrack& track = entry.second;
        if (FindRail(entry.first) != track.node)
            continue;
        bool hidden = track.active0 && !(track.node->flags & 4);
        const Placement& p = track.placement;
        if (hidden || p.quarters || !IsUnitScale(p.scale) || p.offset.x != 0.0f || p.offset.y != 0.0f || p.offset.z != 0.0f)
            changed.push_back({entry.first, p, hidden});
    }
    return changed;
}

uint8_t* MainScene()
{
    uint8_t* found = nullptr;
    ForEachScene([&](uint8_t* scene) {
        if (!found)
            found = scene;
    });
    return found;
}

bool SceneLive(uint8_t* scene)
{
    bool live = false;
    ForEachScene([&](uint8_t* s) { live |= s == scene; });
    return live && scene;
}

uint32_t SceneName(uint8_t* scene) { return scene ? *reinterpret_cast<uint32_t*>(scene + 0x84) : 0; }

// --- copies of sectors ---
//
// CScene::CloneSector (0x0049DF30, thiscall, RET 0x14: the source's checksum, the scene to put it in, instance,
// add-to-collision, the new checksum - 0: a new one; returns the new checksum) as the park editor makes its pieces
// (0x00594730): not an instance (its own meshes and vertex buffers, added to the scene's render lists - so it moves
// as any level sector), flagged 4 to be added to the super sectors (Commit). The copy is made of the source as it is
// now (moved or not); its box (+0x14 / +0x24, not copied for a full clone) is set to the source's box where it is.

std::vector<Clone>& Clones()
{
    static std::vector<Clone> clones;
    return clones;
}

bool IsClone(uint32_t checksum)
{
    const auto& clones = Clones();
    return std::any_of(clones.begin(), clones.end(), [&](const Clone& c) { return c.checksum == checksum; });
}

uint32_t Duplicate(uint32_t source)
{
    uint8_t* scene = nullptr;
    uint8_t* sector = FindSector(source, &scene);
    if (!sector || WhyNotMovable(source))
        return 0;
    OrientedBox box;
    V3 min, max;
    if (!Box(source, &box))
        return 0;
    BoundsOf(box, &min, &max);
    Placement sourcePlacement = PlacementOf(source, GeomOf(sector));
    // (the source's shared collision gathered before the copy is there to tie with it)
    auto sharedFaces = SharedCollision::PrepareCopy(source);
    uint32_t made = GameFunction<uint32_t(__thiscall*)(void*, uint32_t, void*, int, int, uint32_t)>(0x0049DF30)(
        scene, source, scene, 0, 1, 0);
    uint8_t* copy = made ? LookupSector(scene, made) : nullptr;
    uint8_t* geom = copy ? GeomOf(copy) : nullptr;
    if (!geom || *reinterpret_cast<uint32_t*>(geom) != kPcGeomVtable || *reinterpret_cast<void**>(geom + 0x4C)) {
        Log("level editor: copy of %s: %s", Label(source).c_str(), made ? "not a plain sector" : "not made");
        return 0;
    }
    float* boxes = reinterpret_cast<float*>(geom + 0x14);
    boxes[0] = min.x, boxes[1] = min.y, boxes[2] = min.z;
    boxes[4] = max.x, boxes[5] = max.y, boxes[6] = max.z;
    // (the collision's flags are copied with it: a source whose collision is switched off by itself - level_editor_walls.h
    // - gives a copy with collision on, as a layout makes copies again)
    if (uint8_t* coll = CollOf(copy); coll && IsActive(copy))
        coll[0x08] &= static_cast<uint8_t>(~4);
    SharedCollision::FinishCopy(sharedFaces, made, scene, copy, geom);
    MarkDirty(scene);
    Clones().push_back({made, source, sourcePlacement});
    char name[96];
    std::snprintf(name, sizeof(name), "%s copy", Label(source).c_str());
    Script::NoteQbName(made, name);
    if (DebugEnabled())
        Log("level editor: copy %08X of %s, box (%.0f, %.0f, %.0f)-(%.0f, %.0f, %.0f), meshes %s", made,
            Label(source).c_str(), min.x, min.y, min.z, max.x, max.y, max.z,
            MeshesMovable(geom) ? MeshesMovable(geom) : "movable");
    return made;
}

bool PlacementNow(uint32_t checksum, Placement* placement)
{
    uint8_t* sector = FindSector(checksum, nullptr);
    if (!sector)
        return false;
    *placement = PlacementOf(checksum, GeomOf(sector));
    return true;
}

bool Hidden(uint32_t checksum)
{
    uint8_t* sector = FindSector(checksum, nullptr);
    return sector && !IsActive(sector);
}

void AddGroup(uint32_t checksum)
{
    for (uint32_t member : GroupOf(checksum)) {
        if (!Selected(member))
            g_selection.push_back(member);
    }
    ComputePivot();
    SelectRails();
}

std::vector<uint32_t> MovablePieces()
{
    std::vector<uint32_t> pieces;
    uint8_t* scene = MainScene();
    if (!scene)
        return pieces;
    ForEachSector(scene, [&](uint32_t checksum, uint8_t* sector) {
        uint8_t* geom = GeomOf(sector);
        if (PlainGeom(geom) && geom[0x48] && !IsClone(checksum) && !WhyNotMovable(checksum))
            pieces.push_back(checksum);
    });
    return pieces;
}

void ResetSelection()
{
    State state = SaveState();
    for (SectorState& saved : state.sectors) {
        saved.placement = {0, {0.0f, 0.0f, 0.0f}};
        saved.hidden = false;
    }
    for (RailState& saved : state.rails) {
        saved.placement = {0, {0.0f, 0.0f, 0.0f}};
        saved.hidden = false;
    }
    RestoreState(state);
}

void HideSelection()
{
    State state = SaveState();
    for (SectorState& saved : state.sectors)
        saved.hidden = true;
    for (RailState& saved : state.rails)
        saved.hidden = true;
    RestoreState(state);
    ClearSelection();
}

bool SelectionMoved(Placement* placement)
{
    if (g_selection.empty())
        return false;
    uint8_t* sector = FindSector(g_selection[0], nullptr);
    if (!sector)
        return false;
    *placement = PlacementOf(g_selection[0], GeomOf(sector));
    return placement->quarters != 0 || !IsUnitScale(placement->scale) || placement->offset.x != 0.0f || placement->offset.y != 0.0f ||
           placement->offset.z != 0.0f;
}

void Reset()
{
    ClearSelection();
    g_clusters.clear();
    g_shadowOf.clear();
    g_hoverGroupOf = 0;
    g_hoverGroup.clear();
}

uint8_t* SectorGeometry(uint32_t checksum)
{
    uint8_t* sector = FindSector(checksum, nullptr);
    return sector ? GeomOf(sector) : nullptr;
}

uint8_t* SectorOf(uint32_t checksum) { return FindSector(checksum, nullptr); }

void ForEachLevelSector(void (*each)(void* context, uint32_t checksum, uint8_t* sector), void* context)
{
    ForEachScene([&](uint8_t* scene) {
        ForEachSector(scene, [&](uint32_t checksum, uint8_t* sector) { each(context, checksum, sector); });
    });
}

// --- highlighting ---

bool Tintable(void* geom) { return geom && *static_cast<uint32_t*>(geom) == kPcGeomVtable; }

namespace {

// How a geometry's colour was before we tinted it: an instance's (+0x4C: its flags' bit 4, the colour at +0x4D), or
// each mesh's (+0x3C / +0x40: the mesh's flags' bit 4, its colour +0x80..+0x88) - what CGeom::SetColor (vfunc
// +0x18, 0x004B48B0) writes.
struct Saved {
    TintOwner owner;
    uint8_t* geom;
    uint8_t* instance;
    uint32_t instanceFlag;
    uint8_t instanceColour[4];
    std::vector<std::array<uint32_t, 4>> meshes; // {flag bit 4, r, g, b (float bits)}
    bool used;
};
std::vector<Saved> g_tinted;

Saved SaveColour(TintOwner owner, uint8_t* geom)
{
    Saved saved = {owner, geom, *reinterpret_cast<uint8_t**>(geom + 0x4C), 0, {}, {}, true};
    if (saved.instance) {
        saved.instanceFlag = *reinterpret_cast<uint32_t*>(saved.instance) & 4;
        std::memcpy(saved.instanceColour, saved.instance + 0x4D, 4);
        return saved;
    }
    auto** meshes = *reinterpret_cast<uint32_t***>(geom + 0x3C);
    uint32_t count = *reinterpret_cast<uint32_t*>(geom + 0x40);
    for (uint32_t i = 0; meshes && i < count && i < 4096; ++i) {
        uint32_t* mesh = meshes[i];
        saved.meshes.push_back({mesh[0] & 4, mesh[0x20], mesh[0x21], mesh[0x22]});
    }
    return saved;
}

void RestoreColour(const Saved& saved)
{
    if (saved.instance) {
        if (*reinterpret_cast<uint8_t**>(saved.geom + 0x4C) != saved.instance)
            return;
        auto& flags = *reinterpret_cast<uint32_t*>(saved.instance);
        flags = (flags & ~4u) | saved.instanceFlag;
        std::memcpy(saved.instance + 0x4D, saved.instanceColour, 4);
        return;
    }
    auto** meshes = *reinterpret_cast<uint32_t***>(saved.geom + 0x3C);
    uint32_t count = *reinterpret_cast<uint32_t*>(saved.geom + 0x40);
    if (!meshes || count != saved.meshes.size())
        return;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t* mesh = meshes[i];
        mesh[0] = (mesh[0] & ~4u) | saved.meshes[i][0];
        mesh[0x20] = saved.meshes[i][1];
        mesh[0x21] = saved.meshes[i][2];
        mesh[0x22] = saved.meshes[i][3];
    }
}

} // namespace

void Tint(TintOwner owner, void* geom, uint32_t rgba)
{
    if (!Tintable(geom))
        return;
    auto* g = static_cast<uint8_t*>(geom);
    auto it = std::find_if(g_tinted.begin(), g_tinted.end(), [&](const Saved& s) { return s.geom == g; });
    if (it == g_tinted.end())
        g_tinted.push_back(SaveColour(owner, g));
    else
        it->used = true;
    Method<void(__thiscall*)(void*, uint32_t)>(geom, 0x18)(geom, rgba);
}

void EndTints(void* (*resolve)(TintOwner), bool all)
{
    for (auto it = g_tinted.begin(); it != g_tinted.end();) {
        if (all || !it->used) {
            if (resolve(it->owner) == it->geom) // (still there: not a dead object's)
                RestoreColour(*it);
            it = g_tinted.erase(it);
        } else {
            it->used = false;
            ++it;
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
        SharedCollision::Commit(scene); // (the pieces' shared collision: level_editor_shared_coll.h)
    }
    if (DebugEnabled())
        Log("level editor: super sectors updated in %u scene%s", static_cast<unsigned>(g_dirtyScenes.size()),
            g_dirtyScenes.size() == 1 ? "" : "s");
    g_dirtyScenes.clear();
}

void NoteCollisionMoved(uint32_t checksum)
{
    uint8_t* scene = nullptr;
    uint8_t* sector = FindSector(checksum, &scene);
    if (!sector)
        return;
    if (FlagsOf(sector) & 0x40)
        FlagsOf(sector) |= 0x20; // (as ApplyTrack / CSector::SetWorldPosition 0x0049E6C0)
    MarkDirty(scene);
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
        int sectors = 0, withGeometry = 0, withCollision = 0, movable = 0, seeThrough = 0;
        ForEachSector(scene, [&](uint32_t checksum, uint8_t* sector) {
            ++sectors;
            withGeometry += GeomOf(sector) ? 1 : 0;
            withCollision += CollOf(sector) ? 1 : 0;
            movable += WhyNotMovable(checksum) ? 0 : 1;
            seeThrough += GeomOf(sector) && PlainGeom(GeomOf(sector)) && GeomSeeThrough(GeomOf(sector)) ? 1 : 0;
        });
        uint32_t name = *reinterpret_cast<uint32_t*>(scene + 0x84);
        Log("level editor: scene %08X (%s): %d sectors, %d with geometry, %d with collision, %d movable, %d see-through",
            name, NameOf(name) ? NameOf(name) : "?", sectors, withGeometry, withCollision, movable, seeThrough);
    });
}

} // namespace Scenery
} // namespace Front::LevelEditor
