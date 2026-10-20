// NOT ORIGINAL - the level editor's view options (ours). See level_editor_view.h.

#include "game/front/level_editor_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_shared_coll.h"
#include "game/front/level_editor_triggers.h"
#include "game/front/level_editor_walls.h"
#include "game/mth/vector.h"
#include "game/nx/coll_tri.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor::View {

// --- the pieces -------------------------------------------------------------------------------------------------

Surface Classify(uint32_t faceFlags, bool collisionOnly)
{
    if (collisionOnly)
        return Surface::kInvisibleWall;
    if (faceFlags & 0x40)
        return Surface::kTrigger;
    if (faceFlags & 0x10)
        return Surface::kNoCollide;
    if (faceFlags & 0x04)
        return Surface::kWallride;
    if (faceFlags & 0x08)
        return Surface::kVert;
    if ((faceFlags & 0x02) && !(faceFlags & 0x01))
        return Surface::kNotSkatable;
    if (faceFlags & 0x01)
        return Surface::kSkatable;
    return Surface::kPlain;
}

uint32_t SurfaceColour(Surface surface)
{
    switch (surface) {
    case Surface::kInvisibleWall: return 0xB0FF20E0;
    case Surface::kTrigger: return 0x782080FF;
    case Surface::kNoCollide: return 0x40A0A0A0;
    case Surface::kWallride: return 0x78FFD020;
    case Surface::kVert: return 0x78FF8020;
    case Surface::kNotSkatable: return 0x78FF3030;
    case Surface::kSkatable: return 0x7840E040;
    default: return 0x58D0E8FF;
    }
}

const char* SurfaceName(Surface surface)
{
    switch (surface) {
    case Surface::kInvisibleWall: return "Invisible wall (collision, nothing drawn)";
    case Surface::kTrigger: return "Trigger (0x40; with 0x10 a volume: kill zones, water)";
    case Surface::kNoCollide: return "Not collidable (0x10)";
    case Surface::kWallride: return "Wallride (0x04)";
    case Surface::kVert: return "Vert (0x08)";
    case Surface::kNotSkatable: return "Not skatable (0x02)";
    case Surface::kSkatable: return "Skatable (0x01, any slope)";
    default: return "Plain (skatable when flat enough)";
    }
}

int ClipTriangleToNear(V3 cameraPos, const Frame& frame, float nearZ, const V3 in[3], V3 out[4])
{
    // (Sutherland-Hodgman against one plane: depth = (p - camera) . -back)
    float depth[3];
    for (int i = 0; i < 3; ++i)
        depth[i] = -Dot(in[i] - cameraPos, frame.back);
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        int j = (i + 1) % 3;
        bool inI = depth[i] >= nearZ, inJ = depth[j] >= nearZ;
        if (inI)
            out[count++] = in[i];
        if (inI != inJ) {
            float s = (nearZ - depth[i]) / (depth[j] - depth[i]);
            out[count++] = in[i] + (in[j] - in[i]) * s;
        }
    }
    return count >= 3 ? count : 0;
}

float DistanceToBox(V3 point, V3 min, V3 max)
{
    auto axis = [](float p, float lo, float hi) { return p < lo ? lo - p : p > hi ? p - hi : 0.0f; };
    float dx = axis(point.x, min.x, max.x), dy = axis(point.y, min.y, max.y), dz = axis(point.z, min.z, max.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool HiddenSet::Has(uint32_t id) const { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

bool HiddenSet::Add(uint32_t id)
{
    if (Has(id))
        return false;
    ids.push_back(id);
    return true;
}

void AddDashed(std::vector<Ui::Line>* lines, const Ui::Line& line, float dash, float gap)
{
    float dx = line.bx - line.ax, dy = line.by - line.ay;
    float length = std::sqrt(dx * dx + dy * dy);
    if (!(length > 0.0f) || !(dash > 0.0f) || gap < 0.0f)
        return;
    float ux = dx / length, uy = dy / length;
    // (at most 256 dashes an edge: a face seen from very near)
    float period = std::max(dash + gap, length / 256.0f);
    for (float at = 0.0f; at < length; at += period) {
        float end = std::min(length, at + dash);
        Ui::Line piece = line;
        piece.ax = line.ax + ux * at;
        piece.ay = line.ay + uy * at;
        piece.bx = line.ax + ux * end;
        piece.by = line.ay + uy * end;
        lines->push_back(piece);
    }
}

uint32_t Faded(uint32_t argb) { return (argb & 0x00FFFFFF) | (std::min<uint32_t>(argb >> 24, 0x30) << 24); }

FaceStyle StyleOf(const FaceFacts& facts)
{
    FaceStyle style;
    bool wall = facts.collisionOnly && !facts.companion;
    if ((facts.flags & 0x40) && facts.triggerColour)
        style.fill = (facts.triggerColour & 0x00FFFFFFu) | 0x78000000u;
    else
        style.fill = SurfaceColour(Classify(facts.taken ? facts.flags & ~0x10u : facts.flags, wall));
    if (facts.selected)
        style.fill = 0xC8FFA0FFu; // (the selected wall: lighter)
    else if (facts.hovered)
        style.fill = kHoverWall;
    if (facts.off || facts.taken)
        style.fill = Faded(style.fill); // (switched off / taken along: faded)
    if (facts.companion) {
        style.edge = kCompanionEdge;
    } else if (facts.taken) {
        style.edge = kTakenEdge;
        style.dashed = true;
    } else if (wall || facts.selected || facts.hovered) {
        style.edge = facts.selected ? 0xFFFFFFFFu : facts.hovered ? 0xFFFFF060u : 0xE0FF60F0u;
        style.dashed = facts.off;
        style.thickness = facts.selected || facts.hovered ? 1.5f : 1.0f;
    }
    return style;
}

std::vector<uint32_t> IsolateOthers(const std::vector<uint32_t>& all, const std::vector<uint32_t>& keep)
{
    std::vector<uint32_t> others;
    for (uint32_t id : all) {
        if (std::find(keep.begin(), keep.end(), id) == keep.end() &&
            std::find(others.begin(), others.end(), id) == others.end())
            others.push_back(id);
    }
    return others;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kCollStaticVtable = 0x00645C18;

template <typename F>
F Method(void* object, int offset)
{
    return reinterpret_cast<F>((*reinterpret_cast<void***>(object))[offset / 4]);
}

uint8_t* GeomOf(uint8_t* sector) { return *reinterpret_cast<uint8_t**>(sector + 0x14); }
uint8_t* CollOf(uint8_t* sector) { return *reinterpret_cast<uint8_t**>(sector + 0x18); }

// The sector's collision geometry, whether its collision is on or off (CCollStatic; `off`: its flag 4 - the game's
// switch, level_editor_walls.h).
Nx::CCollObjTriData* AnyTriDataOf(uint8_t* sector, bool* off)
{
    uint8_t* coll = CollOf(sector);
    if (!coll || *reinterpret_cast<uint32_t*>(coll) != kCollStaticVtable)
        return nullptr;
    if (off)
        *off = (coll[0x08] & 4) != 0;
    return *reinterpret_cast<Nx::CCollObjTriData**>(coll + 0x0C);
}

// The collision drawn for a sector (null: none): on, or switched off by the editor's switch (an invisible wall, or a
// drawn piece's collision by itself - shown faded so it can be switched on again). A piece hidden (its geometry off,
// its collision with it) isn't drawn.
Nx::CCollObjTriData* DrawnTriDataOf(uint32_t checksum, uint8_t* sector, bool* off)
{
    Nx::CCollObjTriData* tri = AnyTriDataOf(sector, off);
    if (tri && *off && GeomOf(sector) && !Walls::Off(checksum))
        return nullptr;
    return tri;
}

V3 ToV3(const Mth::Vector& v) { return {v.x, v.y, v.z}; }

bool g_showCollision;
bool g_settingRead;

// The collision cache: a sector's faces in world space (3 points each) and their flags, made again when its collision
// geometry, its box or its first and last vertices change (moved, or turned - a quarter turn can keep the box).
struct Cached {
    uint8_t* sector = nullptr;
    const Nx::CCollObjTriData* tri = nullptr;
    V3 min = {}, max = {}, first = {}, last = {};
    int faces = 0;
    bool collisionOnly = false;
    uint32_t generation = 0; // (SharedCollision::Generation when made: faces taken since - made again)
    std::vector<V3> points;
    std::vector<uint16_t> flags;
    std::vector<uint8_t> taken; // (each face: turned off for a moved piece's shared collision)
};
std::unordered_map<uint32_t, Cached> g_cache;

const Cached* CacheOf(uint32_t checksum, uint8_t* sector, const Nx::CCollObjTriData* tri)
{
    V3 min = ToV3(tri->m_min), max = ToV3(tri->m_max);
    V3 first = {}, last = {};
    if (tri->m_vertices && tri->m_numVertices) {
        first = ToV3(tri->GetRawVertexPos(0));
        last = ToV3(tri->GetRawVertexPos(tri->m_numVertices - 1));
    }
    auto same = [](V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    bool collisionOnly = GeomOf(sector) == nullptr;
    Cached& c = g_cache[checksum];
    uint32_t generation = SharedCollision::Generation();
    if (c.sector == sector && c.tri == tri && c.faces == tri->m_numFaces && c.collisionOnly == collisionOnly &&
        c.generation == generation && same(c.min, min) && same(c.max, max) && same(c.first, first) && same(c.last, last))
        return &c;
    c = Cached{};
    c.generation = generation;
    std::vector<int> taken = SharedCollision::TakenFaces(tri);
    c.sector = sector;
    c.tri = tri;
    c.min = min;
    c.max = max;
    c.first = first;
    c.last = last;
    c.faces = tri->m_numFaces;
    c.collisionOnly = collisionOnly;
    if (!tri->m_faces || !tri->m_vertices)
        return &c;
    c.points.reserve(static_cast<size_t>(c.faces) * 3);
    c.flags.reserve(c.faces);
    for (int f = 0; f < c.faces; ++f) {
        bool ok = true;
        V3 p[3];
        for (int k = 0; k < 3; ++k) {
            uint32_t index = tri->FaceVertex(f, k);
            if (index >= tri->m_numVertices) {
                ok = false;
                break;
            }
            p[k] = ToV3(tri->GetRawVertexPos(static_cast<int>(index)));
        }
        if (!ok)
            continue;
        c.points.insert(c.points.end(), p, p + 3);
        c.flags.push_back(*reinterpret_cast<const uint16_t*>(tri->m_faces + f * (tri->m_smallFaces ? 8 : 10)));
        c.taken.push_back(std::find(taken.begin(), taken.end(), f) != taken.end() ? 1 : 0);
    }
    return &c;
}

uint32_t g_hoverWall; // (Shift held with the collision shown: the wall a Shift+click would take)

// What was drawn last frame (for Shift+click on a wall): each face's sector and points.
struct Drawn {
    uint32_t checksum;
    bool wall;
    V3 a, b, c;
};
std::vector<Drawn> g_drawn;
Stats g_stats;
uint32_t g_wall;

// --- hiding ---

struct HiddenSectorRec {
    uint32_t checksum;
    uint8_t* sector;
    uint32_t mask;
};
struct HiddenObjectRec {
    uint32_t id;
    std::vector<std::pair<void*, bool>> geoms; // (each geometry and whether it was on)
};
std::vector<HiddenSectorRec> g_hiddenSectors;
std::vector<HiddenObjectRec> g_hiddenObjects;

// CSector::SetVisibility (0x0049E680, thiscall, RET 4) / GetVisibility (0x0049E690): the viewports it shows in.
void SetVisibility(uint8_t* sector, uint32_t mask)
{
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0049E680)(sector, mask);
}
uint32_t GetVisibility(uint8_t* sector) { return GameFunction<uint32_t(__thiscall*)(void*)>(0x0049E690)(sector); }

// A geometry's SetActive (vfunc +0x34) / IsActive (vfunc +0x38).
void SetGeomActive(void* geom, bool on) { Method<void(__thiscall*)(void*, bool)>(geom, 0x34)(geom, on); }
bool GeomActive(void* geom) { return Method<bool(__thiscall*)(void*)>(geom, 0x38)(geom); }

Obj::CCompositeObject* LiveObject(uint32_t id)
{
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
    return object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) ? object : nullptr;
}

bool HideSector(uint32_t checksum)
{
    if (HiddenSector(checksum))
        return false;
    uint8_t* sector = Scenery::SectorOf(checksum);
    if (!sector || !GeomOf(sector))
        return false; // (collision only: nothing drawn to hide)
    g_hiddenSectors.push_back({checksum, sector, GetVisibility(sector)});
    SetVisibility(sector, 0);
    return true;
}

bool HideObject(uint32_t id)
{
    if (HiddenObject(id))
        return false;
    Obj::CCompositeObject* object = LiveObject(id);
    if (!object)
        return false;
    HiddenObjectRec rec = {id, {}};
    for (int i = 0; i < 16; ++i) {
        void* geom = Objects::Geometry(object, i);
        if (!geom)
            break;
        rec.geoms.push_back({geom, GeomActive(geom)});
        SetGeomActive(geom, false);
    }
    if (rec.geoms.empty())
        return false;
    g_hiddenObjects.push_back(rec);
    return true;
}

struct ObjectWalk {
    std::vector<uint32_t> ids;
};
void __cdecl CollectObject(Obj::CObject* object, void* data)
{
    auto* composite = static_cast<Obj::CCompositeObject*>(object);
    OrientedBox box;
    if (Objects::ModelBox(composite, &box))
        static_cast<ObjectWalk*>(data)->ids.push_back(composite->m_id);
}

} // namespace

void Start()
{
    if (!g_settingRead) {
        g_showCollision = ConfigInt("editor", "collision", 0) != 0;
        g_settingRead = true;
    }
    g_cache.clear();
    g_drawn.clear();
    g_wall = 0;
    g_hoverWall = 0;
    g_hiddenSectors.clear();
    g_hiddenObjects.clear();
}

void Stop()
{
    ShowAll();
    g_cache.clear();
    g_drawn.clear();
    g_wall = 0;
}

void Tick()
{
    // (the model's suspend pass turns its geometries on when its decision changes: off again)
    for (size_t i = 0; i < g_hiddenObjects.size();) {
        HiddenObjectRec& rec = g_hiddenObjects[i];
        Obj::CCompositeObject* object = LiveObject(rec.id);
        if (!object) {
            g_hiddenObjects.erase(g_hiddenObjects.begin() + static_cast<long>(i));
            continue;
        }
        for (int g = 0; g < static_cast<int>(rec.geoms.size()); ++g) {
            void* geom = Objects::Geometry(object, g);
            if (geom && GeomActive(geom))
                SetGeomActive(geom, false);
        }
        ++i;
    }
}

bool ShowCollision() { return g_showCollision; }

void SetShowCollision(bool on)
{
    g_showCollision = on;
    g_settingRead = true;
    ConfigSetInt("editor", "collision", on ? 1 : 0);
    if (!on)
        g_drawn.clear();
}

void AddCollision(V3 cameraPos, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                  std::vector<Ui::Overlay::Triangle>* triangles, std::vector<Ui::Line>* lines)
{
    g_drawn.clear();
    g_stats = {};
    if (!g_showCollision)
        return;
    // The sectors near enough, nearest first (invisible walls before the rest: never left out by the cap).
    struct Candidate {
        uint32_t checksum;
        uint8_t* sector;
        Nx::CCollObjTriData* tri;
        float order;
        bool off;
    };
    struct Walk {
        V3 camera;
        const Frame* frame;
        float nearZ;
        std::vector<Candidate> found;
    } walk = {cameraPos, &frame, nearZ, {}};
    Scenery::ForEachLevelSector(
        [](void* context, uint32_t checksum, uint8_t* sector) {
            auto* w = static_cast<Walk*>(context);
            bool off = false;
            Nx::CCollObjTriData* tri = DrawnTriDataOf(checksum, sector, &off);
            if (!tri || tri->m_numFaces == 0 || HiddenSector(checksum))
                return;
            bool wall = GeomOf(sector) == nullptr;
            V3 min = ToV3(tri->m_min), max = ToV3(tri->m_max);
            float distance = DistanceToBox(w->camera, min, max);
            if (distance > (wall ? kWallRadius : kRadius) && checksum != g_wall)
                return;
            // (wholly behind the camera: left out)
            bool front = false;
            for (int i = 0; i < 8 && !front; ++i) {
                V3 corner = {(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z};
                front = -Dot(corner - w->camera, w->frame->back) >= w->nearZ;
            }
            if (!front)
                return;
            w->found.push_back({checksum, sector, tri, wall || checksum == g_wall ? distance - 1e7f : distance, off});
        },
        &walk);
    std::sort(walk.found.begin(), walk.found.end(),
              [](const Candidate& a, const Candidate& b) { return a.order < b.order; });

    struct Polygon {
        float depth;
        uint32_t argb;
        int count;
        float x[4], y[4];
    };
    std::vector<Polygon> polygons;
    const V3 light = Normalize({0.35f, 0.85f, 0.4f});
    // A face projected and styled (StyleOf): its polygon added, its edges drawn; false when it isn't on the screen.
    auto addFace = [&](const V3* p, const FaceFacts& facts) {
        V3 clipped[4];
        int count = ClipTriangleToNear(cameraPos, frame, nearZ, p, clipped);
        if (!count)
            return false;
        Polygon poly = {};
        poly.count = count;
        bool left = true, right = true, below = true, above = true;
        float depth = 0.0f;
        for (int k = 0; k < count; ++k) {
            float nx, ny;
            if (!Project(cameraPos, frame, lens, clipped[k], nearZ * 0.5f, &nx, &ny)) {
                nx = ny = 0.0f; // (on the near plane - can't happen after clipping)
            }
            left = left && nx < -1.0f;
            right = right && nx > 1.0f;
            below = below && ny < -1.0f;
            above = above && ny > 1.0f;
            NdcToPixel(viewport, nx, ny, &poly.x[k], &poly.y[k]);
            depth += -Dot(clipped[k] - cameraPos, frame.back);
        }
        if (left || right || below || above)
            return false;
        poly.depth = depth / static_cast<float>(count);
        FaceStyle style = StyleOf(facts);
        uint32_t argb = style.fill;
        // (shaded by the face's facing: the shape reads through the colour)
        V3 normal = Cross(p[1] - p[0], p[2] - p[0]);
        float length = std::sqrt(Dot(normal, normal));
        float shade = length > 0.0f ? 0.55f + 0.45f * std::fabs(Dot(normal, light)) / length : 1.0f;
        uint32_t r = static_cast<uint32_t>(((argb >> 16) & 0xFF) * shade);
        uint32_t g = static_cast<uint32_t>(((argb >> 8) & 0xFF) * shade);
        uint32_t b = static_cast<uint32_t>((argb & 0xFF) * shade);
        poly.argb = (argb & 0xFF000000) | (r << 16) | (g << 8) | b;
        polygons.push_back(poly);
        if (style.edge && lines) {
            for (int k = 0; k < count; ++k) {
                int n = (k + 1) % count;
                Ui::Line line = {poly.x[k], poly.y[k], poly.x[n], poly.y[n], style.edge, style.thickness};
                if (style.dashed)
                    AddDashed(lines, line, 6.0f, 5.0f);
                else
                    lines->push_back(line);
            }
        }
        return true;
    };
    // (the level's triggers: their kinds by sector)
    std::unordered_map<uint32_t, uint32_t> triggerColours;
    for (const Triggers::LevelTrigger& t : Triggers::LevelTriggers())
        triggerColours[t.sector] = Triggers::KindColour(t.kind);
    for (const Candidate& candidate : walk.found) {
        if (static_cast<int>(polygons.size()) >= kMaxTriangles) {
            g_stats.capped = true;
            break;
        }
        const Cached* cached = CacheOf(candidate.checksum, candidate.sector, candidate.tri);
        FaceFacts facts;
        facts.collisionOnly = cached->collisionOnly;
        facts.off = candidate.off;
        facts.selected = candidate.checksum == g_wall;
        facts.hovered = candidate.checksum == g_hoverWall;
        auto trigger = triggerColours.find(candidate.checksum);
        facts.triggerColour = trigger == triggerColours.end() ? 0 : trigger->second;
        bool any = false;
        for (size_t f = 0; f < cached->flags.size() && static_cast<int>(polygons.size()) < kMaxTriangles; ++f) {
            const V3* p = &cached->points[f * 3];
            facts.flags = cached->flags[f];
            facts.taken = cached->taken[f] != 0;
            if (!addFace(p, facts))
                continue;
            // (a drawn piece's collision switched off isn't in the way of a click: the piece's own click selects it;
            // nor is a face taken along - its piece's companion is where it is now)
            if ((cached->collisionOnly || !candidate.off) && !facts.taken)
                g_drawn.push_back({candidate.checksum, cached->collisionOnly, p[0], p[1], p[2]});
            any = true;
            g_stats.taken += facts.taken ? 1 : 0;
            g_stats.triggers += (facts.flags & 0x40) && facts.triggerColour ? 1 : 0;
        }
        if (any) {
            ++g_stats.sectors;
            g_stats.walls += cached->collisionOnly ? 1 : 0;
            g_stats.off += candidate.off ? 1 : 0;
        }
    }
    // The moved pieces' shared collision where it is now (level_editor_shared_coll.h - not a sector's: drawn apart).
    struct CompanionWalk {
        V3 camera;
        std::vector<std::pair<uint32_t, const SharedCollision::Companion*>> near;
    } companions = {cameraPos, {}};
    SharedCollision::ForEachCompanion(
        [](void* context, uint32_t checksum, const SharedCollision::Companion& companion) {
            auto* w = static_cast<CompanionWalk*>(context);
            const Nx::CCollObjTriData& tri = companion.Geometry();
            if (DistanceToBox(w->camera, ToV3(tri.m_min), ToV3(tri.m_max)) <= kRadius && !HiddenSector(checksum))
                w->near.push_back({checksum, &companion});
        },
        &companions);
    for (const auto& entry : companions.near) {
        const Nx::CCollObjTriData& tri = entry.second->Geometry();
        bool any = false;
        for (int f = 0; f < tri.m_numFaces && tri.m_faces && static_cast<int>(polygons.size()) < kMaxTriangles; ++f) {
            V3 p[3];
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                uint32_t index = tri.FaceVertex(f, k);
                ok = index < tri.m_numVertices;
                if (ok)
                    p[k] = entry.second->Vertex(static_cast<int>(index));
            }
            FaceFacts facts;
            facts.flags = *reinterpret_cast<const uint16_t*>(tri.m_faces + f * (tri.m_smallFaces ? 8 : 10));
            facts.companion = true;
            any = (ok && addFace(p, facts)) || any;
        }
        g_stats.companions += any ? 1 : 0;
    }
    // Far to near: the nearer faces painted over the farther.
    std::sort(polygons.begin(), polygons.end(), [](const Polygon& a, const Polygon& b) { return a.depth > b.depth; });
    triangles->reserve(triangles->size() + polygons.size() * 2);
    for (const Polygon& p : polygons) {
        triangles->push_back({p.x[0], p.y[0], p.x[1], p.y[1], p.x[2], p.y[2], p.argb});
        if (p.count == 4)
            triangles->push_back({p.x[0], p.y[0], p.x[2], p.y[2], p.x[3], p.y[3], p.argb});
    }
    g_stats.triangles = static_cast<int>(polygons.size());
}

Stats LastStats() { return g_stats; }

uint32_t PickWall(V3 origin, V3 unitDir)
{
    float best = 1e30f;
    const Drawn* nearest = nullptr;
    for (const Drawn& d : g_drawn) {
        float t;
        if (Picking::RayTriangle(origin, unitDir, d.a, d.b, d.c, &t) && t < best) {
            best = t;
            nearest = &d;
        }
    }
    return nearest && nearest->wall ? nearest->checksum : 0;
}

void SetHoverWall(uint32_t checksum) { g_hoverWall = g_showCollision ? checksum : 0; }
uint32_t HoverWall() { return g_hoverWall; }
void SelectWall(uint32_t checksum) { g_wall = checksum; }
uint32_t SelectedWall() { return g_wall; }
void ClearWall() { g_wall = 0; }

std::string DescribeWall(uint32_t checksum)
{
    uint8_t* sector = Scenery::SectorOf(checksum);
    Nx::CCollObjTriData* tri = sector ? AnyTriDataOf(sector, nullptr) : nullptr;
    if (!tri)
        return "(gone)";
    // The faces' flags: each distinct value and how many faces have it.
    std::vector<std::pair<uint16_t, int>> counts;
    for (int f = 0; f < tri->m_numFaces && tri->m_faces; ++f) {
        uint16_t flags = *reinterpret_cast<const uint16_t*>(tri->m_faces + f * (tri->m_smallFaces ? 8 : 10));
        auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == flags; });
        if (it == counts.end())
            counts.push_back({flags, 1});
        else
            ++it->second;
    }
    char text[256];
    std::snprintf(text, sizeof(text),
                  "Collision only: nothing is drawn for it.\n%d faces, %d vertices\nbox (%.0f, %.0f, %.0f) to (%.0f, %.0f, "
                  "%.0f)\nsize %.0f x %.0f x %.0f\nface flags:",
                  tri->m_numFaces, tri->m_numVertices, tri->m_min.x, tri->m_min.y, tri->m_min.z, tri->m_max.x,
                  tri->m_max.y, tri->m_max.z, tri->m_max.x - tri->m_min.x, tri->m_max.y - tri->m_min.y,
                  tri->m_max.z - tri->m_min.z);
    std::string out = text;
    for (size_t i = 0; i < counts.size() && i < 8; ++i) {
        std::snprintf(text, sizeof(text), " 0x%04X x%d", counts[i].first, counts[i].second);
        out += text;
    }
    if (counts.size() > 8)
        out += " ...";
    return out;
}

bool HiddenSector(uint32_t checksum)
{
    return std::any_of(g_hiddenSectors.begin(), g_hiddenSectors.end(),
                       [&](const HiddenSectorRec& r) { return r.checksum == checksum; });
}

bool HiddenObject(uint32_t id)
{
    return std::any_of(g_hiddenObjects.begin(), g_hiddenObjects.end(),
                       [&](const HiddenObjectRec& r) { return r.id == id; });
}

int Hide(const std::vector<uint32_t>& sectors, const std::vector<uint32_t>& objects)
{
    int hidden = 0;
    for (uint32_t checksum : sectors)
        hidden += HideSector(checksum) ? 1 : 0;
    for (uint32_t id : objects)
        hidden += HideObject(id) ? 1 : 0;
    if (DebugEnabled())
        Log("level editor: view - hid %d (%d sectors, %d objects hidden in all)", hidden,
            static_cast<int>(g_hiddenSectors.size()), static_cast<int>(g_hiddenObjects.size()));
    return hidden;
}

int Isolate(const std::vector<uint32_t>& sectors, const std::vector<uint32_t>& objects)
{
    struct SectorWalk {
        std::vector<uint32_t> ids;
    } sectorWalk;
    Scenery::ForEachLevelSector(
        [](void* context, uint32_t checksum, uint8_t* sector) {
            if (GeomOf(sector))
                static_cast<SectorWalk*>(context)->ids.push_back(checksum);
        },
        &sectorWalk);
    ObjectWalk objectWalk;
    if (Obj::CCompositeObjectManager* manager = Obj::CompositeObjectManager())
        manager->ForEach(CollectObject, &objectWalk);
    return Hide(IsolateOthers(sectorWalk.ids, sectors), IsolateOthers(objectWalk.ids, objects));
}

void ShowAll()
{
    for (const HiddenSectorRec& rec : g_hiddenSectors) {
        if (Scenery::SectorOf(rec.checksum) == rec.sector && GeomOf(rec.sector))
            SetVisibility(rec.sector, rec.mask);
    }
    g_hiddenSectors.clear();
    // (a copy made of a hidden piece - picked in the Outliner - copied its visibility too: shown as its source is)
    for (const Scenery::Clone& clone : Scenery::Clones()) {
        uint8_t* copy = Scenery::SectorOf(clone.checksum);
        uint8_t* source = Scenery::SectorOf(clone.source);
        if (copy && source && GeomOf(copy) && GeomOf(source) && GetVisibility(copy) == 0 && GetVisibility(source) != 0)
            SetVisibility(copy, GetVisibility(source));
    }
    for (const HiddenObjectRec& rec : g_hiddenObjects) {
        Obj::CCompositeObject* object = LiveObject(rec.id);
        for (int g = 0; object && g < static_cast<int>(rec.geoms.size()); ++g) {
            void* geom = Objects::Geometry(object, g);
            if (geom && geom == rec.geoms[g].first)
                SetGeomActive(geom, rec.geoms[g].second);
        }
    }
    g_hiddenObjects.clear();
}

int HiddenCount() { return static_cast<int>(g_hiddenSectors.size() + g_hiddenObjects.size()); }

std::vector<uint32_t> HiddenIds()
{
    std::vector<uint32_t> ids;
    for (const HiddenSectorRec& rec : g_hiddenSectors)
        ids.push_back(rec.checksum);
    for (const HiddenObjectRec& rec : g_hiddenObjects)
        ids.push_back(rec.id);
    return ids;
}

} // namespace Front::LevelEditor::View
