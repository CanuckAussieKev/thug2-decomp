// NOT ORIGINAL - the level editor's view options (ours). See level_editor_view.h.

#include "editor/level_editor_view.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "engine/render/coll_static.h"
#include "engine/gfx/engine_geom.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_shared_coll.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_walls.h"
#include "engine/gfx/sector.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"

namespace frontend::level_editor::view {

// --- the pieces -------------------------------------------------------------------------------------------------

Surface get_classify(uint32_t face_flags, bool collision_only)
{
    if (collision_only)
        return Surface::kInvisibleWall;
    if (face_flags & 0x40)
        return Surface::kTrigger;
    if (face_flags & 0x10)
        return Surface::kNoCollide;
    if (face_flags & 0x04)
        return Surface::kWallride;
    if (face_flags & 0x08)
        return Surface::kVert;
    if ((face_flags & 0x02) && !(face_flags & 0x01))
        return Surface::kNotSkatable;
    if (face_flags & 0x01)
        return Surface::kSkatable;
    return Surface::kPlain;
}

uint32_t surface_colour(Surface surface)
{
    switch (surface) {
    case Surface::kInvisibleWall:
        return 0xB0FF20E0;
    case Surface::kTrigger:
        return 0x782080FF;
    case Surface::kNoCollide:
        return 0x40A0A0A0;
    case Surface::kWallride:
        return 0x78FFD020;
    case Surface::kVert:
        return 0x78FF8020;
    case Surface::kNotSkatable:
        return 0x78FF3030;
    case Surface::kSkatable:
        return 0x7840E040;
    default:
        return 0x58D0E8FF;
    }
}

const char* surface_name(Surface surface)
{
    switch (surface) {
    case Surface::kInvisibleWall:
        return "Invisible wall";
    case Surface::kTrigger:
        return "Trigger";
    case Surface::kNoCollide:
        return "Not collidable (0x10)";
    case Surface::kWallride:
        return "Wallride (0x04)";
    case Surface::kVert:
        return "Vert (0x08)";
    case Surface::kNotSkatable:
        return "Not skatable (0x02)";
    case Surface::kSkatable:
        return "Skatable (0x01, any slope)";
    default:
        return "Plain";
    }
}

int clip_triangle_to_near(V3 camera_pos, const TickCycle& frame, float near_z, const V3 in[3], V3 out[4])
{
    // (Sutherland-Hodgman against one plane: depth = (p - camera) . -back)
    float depth[3];
    for (int i = 0; i < 3; ++i)
        depth[i] = -get_dot(in[i] - camera_pos, frame.back);
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        int j = (i + 1) % 3;
        bool in_i = depth[i] >= near_z, in_j = depth[j] >= near_z;
        if (in_i)
            out[count++] = in[i];
        if (in_i != in_j) {
            float s = (near_z - depth[i]) / (depth[j] - depth[i]);
            out[count++] = in[i] + (in[j] - in[i]) * s;
        }
    }
    return count >= 3 ? count : 0;
}

float distance_to_box(V3 point, V3 min, V3 max)
{
    auto axis = [](float p, float lo, float hi) { return p < lo ? lo - p : p > hi ? p - hi : 0.0f; };
    float dx = axis(point.x, min.x, max.x), dy = axis(point.y, min.y, max.y), dz = axis(point.z, min.z, max.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool HiddenSet::get_has(uint32_t id) const { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

bool HiddenSet::append_one(uint32_t id)
{
    if (get_has(id))
        return false;
    ids.push_back(id);
    return true;
}

void add_dashed(std::vector<ui::Seg>* lines, const ui::Seg& line, float dash, float gap)
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
        ui::Seg piece = line;
        piece.ax = line.ax + ux * at;
        piece.ay = line.ay + uy * at;
        piece.bx = line.ax + ux * end;
        piece.by = line.ay + uy * end;
        lines->push_back(piece);
    }
}

uint32_t get_faded(uint32_t argb) { return (argb & 0x00FFFFFF) | (std::min<uint32_t>(argb >> 24, 0x30) << 24); }

FaceStyle style_of(const FaceFacts& facts)
{
    FaceStyle style;
    bool wall = facts.collision_only_ && !facts.companion;
    if ((facts.flags & 0x40) && facts.trigger_colour_)
        style.fill = (facts.trigger_colour_ & 0x00FFFFFFu) | 0x78000000u;
    else
        style.fill = surface_colour(get_classify(facts.taken ? facts.flags & ~0x10u : facts.flags, wall));
    if (facts.selected)
        style.fill = 0xC8FFA0FFu; // (the selected wall: lighter)
    else if (facts.hovered)
        style.fill = kHoverWall;
    if (facts.off || facts.taken)
        style.fill = get_faded(style.fill); // (switched off / taken along: faded)
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

std::vector<uint32_t> isolate_others(const std::vector<uint32_t>& all, const std::vector<uint32_t>& keep)
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

render::PcGeom* geom_of(render::WorldCell* sector) { return static_cast<render::PcGeom*>(sector->geom_handle_); }
render::HitMesh* coll_of(render::WorldCell* sector) { return sector->hit_body_; }

// The sector's collision geometry, whether its collision is on or off (StaticCollider; `off`: its flag 4 - the game's
// switch, level_editor_walls.h).
render::TriMeshSource* any_tri_data_of(render::WorldCell* sector, bool* off)
{
    render::HitMesh* coll = coll_of(sector);
    if (!render::is_static_collider(coll))
        return nullptr;
    if (off)
        *off = (coll->flag_bits_ & 4) != 0;
    return coll->geometry_;
}

// The collision drawn for a sector (null: none): on, or switched off by the editor's switch (an invisible wall, or a
// drawn piece's collision by itself - shown faded so it can be switched on again). A piece hidden (its geometry off,
// its collision with it) isn't drawn.
render::TriMeshSource* drawn_tri_data_of(uint32_t checksum, render::WorldCell* sector, bool* off)
{
    render::TriMeshSource* tri = any_tri_data_of(sector, off);
    if (tri && *off && geom_of(sector) && !walls::disengaged(checksum))
        return nullptr;
    return tri;
}

V3 to_v3(const maths::Vec4& v) { return {v.x, v.y, v.z}; }

bool g_show_collision;
bool g_setting_read;

// The collision cache: a sector's faces in world space (3 points each) and their flags, made again when its collision
// geometry, its box or its first and last vertices change (moved, or turned - a quarter turn can keep the box).
struct Cached {
    render::WorldCell* sector = nullptr;
    const render::TriMeshSource* tri = nullptr;
    V3 min = {}, max = {}, first = {}, last = {};
    int faces = 0;
    bool collision_only_ = false;
    uint32_t generation = 0; // (SharedCollision::get_generation when made: faces taken since - made again)
    std::vector<V3> points;
    std::vector<uint16_t> flags;
    std::vector<uint8_t> taken; // (each face: turned off for a moved piece's shared collision)
    std::vector<int> index;     // (each face: its index in the collision - faces with bad corners are left out)
};
std::unordered_map<uint32_t, Cached> g_cache;

const Cached* cache_of(uint32_t checksum, render::WorldCell* sector, const render::TriMeshSource* tri)
{
    V3 min = to_v3(tri->lower_limit_), max = to_v3(tri->upper_limit_);
    V3 first = {}, last = {};
    if (tri->vertices_ && tri->num_vertices_) {
        first = to_v3(tri->read_stored_vertex(0));
        last = to_v3(tri->read_stored_vertex(tri->num_vertices_ - 1));
    }
    auto same = [](V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    bool collision_only = geom_of(sector) == nullptr;
    Cached& c = g_cache[checksum];
    uint32_t generation = shared_collision::get_generation();
    if (c.sector == sector && c.tri == tri && c.faces == tri->tri_count_ && c.collision_only_ == collision_only &&
        c.generation == generation && same(c.min, min) && same(c.max, max) && same(c.first, first) &&
        same(c.last, last))
        return &c;
    c = Cached{};
    c.generation = generation;
    std::vector<int> taken = shared_collision::taken_faces(tri);
    c.sector = sector;
    c.tri = tri;
    c.min = min;
    c.max = max;
    c.first = first;
    c.last = last;
    c.faces = tri->tri_count_;
    c.collision_only_ = collision_only;
    if (!tri->faces_ || !tri->vertices_)
        return &c;
    c.points.reserve(static_cast<size_t>(c.faces) * 3);
    c.flags.reserve(c.faces);
    for (int f = 0; f < c.faces; ++f) {
        bool ok = true;
        V3 p[3];
        for (int k = 0; k < 3; ++k) {
            uint32_t index = tri->face_vertex(f, k);
            if (index >= tri->num_vertices_) {
                ok = false;
                break;
            }
            p[k] = to_v3(tri->read_stored_vertex(static_cast<int>(index)));
        }
        if (!ok)
            continue;
        c.points.insert(c.points.end(), p, p + 3);
        c.flags.push_back(*reinterpret_cast<const uint16_t*>(tri->faces_ + f * (tri->small_faces_ ? 8 : 10)));
        c.taken.push_back(std::find(taken.begin(), taken.end(), f) != taken.end() ? 1 : 0);
        c.index.push_back(f);
    }
    return &c;
}

uint32_t g_hover_wall; // (Shift held with the collision shown: the wall a Shift+click would take)

// What was drawn last frame (for Shift+click on a wall): each face's sector and points.
struct Drawn {
    uint32_t checksum;
    bool wall;
    V3 a, b, c;
    int face = -1;          // (its index in its collision - level_editor_faces.h)
    bool companion = false; // (a moved piece's shared collision: not a wall's, never Shift+clicked)
};
std::vector<Drawn> g_drawn;
Attributes g_stats;
uint32_t g_wall;

// --- hiding ---

struct HiddenSectorRec {
    uint32_t checksum;
    render::WorldCell* sector;
    uint32_t mask;
};
struct HiddenObjectRec {
    uint32_t id;
    std::vector<std::pair<void*, bool>> geoms; // (each geometry and whether it was on)
};
std::vector<HiddenSectorRec> g_hidden_sectors;
std::vector<HiddenObjectRec> g_concealed_records;

// WorldCell::toggle_shown (0x0049E680, thiscall, RET 4) / fetch_shown (0x0049E690): the viewports it shows in.
void toggle_shown(render::WorldCell* sector, uint32_t mask) { sector->toggle_shown(mask); }
uint32_t fetch_shown(render::WorldCell* sector) { return sector->fetch_shown(); }

// A geometry's change_live (vfunc +0x34) / is_operative (vfunc +0x38).
void set_geom_active(void* geom, bool on) { static_cast<render::Geom*>(geom)->change_live(on); }
bool geom_active(void* geom) { return static_cast<render::Geom*>(geom)->is_operative(); }

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = static_cast<objects::GameEntity*>(objects::find_object(id));
    return object && !(object->object_flags_ & objects::kObjectFlagDead) ? object : nullptr;
}

bool hide_sector(uint32_t checksum)
{
    if (hidden_sector(checksum))
        return false;
    render::WorldCell* sector = scenery::sector_of(checksum);
    if (!sector || !geom_of(sector))
        return false; // (collision only: nothing drawn to hide)
    g_hidden_sectors.push_back({checksum, sector, fetch_shown(sector)});
    toggle_shown(sector, 0);
    return true;
}

bool hide_object(uint32_t id)
{
    if (hidden_object(id))
        return false;
    objects::GameEntity* object = live_object(id);
    if (!object)
        return false;
    HiddenObjectRec rec = {id, {}};
    for (int i = 0; i < 16; ++i) {
        void* geom = object_edit::get_geometry(object, i);
        if (!geom)
            break;
        rec.geoms.emplace_back(geom, geom_active(geom));
        set_geom_active(geom, false);
    }
    if (rec.geoms.empty())
        return false;
    g_concealed_records.push_back(rec);
    return true;
}

struct ObjectWalk {
    std::vector<uint32_t> ids;
};
void collect_object(objects::Entity* object, void* data)
{
    auto* composite = static_cast<objects::GameEntity*>(object);
    OrientedBox box;
    if (object_edit::model_box(composite, &box))
        static_cast<ObjectWalk*>(data)->ids.push_back(composite->ident_);
}

} // namespace

void kickoff()
{
    if (!g_setting_read) {
        g_show_collision = config_int("editor", "collision", 0) != 0;
        g_setting_read = true;
    }
    g_cache.clear();
    g_drawn.clear();
    g_wall = 0;
    g_hover_wall = 0;
    g_hidden_sectors.clear();
    g_concealed_records.clear();
}

void halt()
{
    show_all();
    g_cache.clear();
    g_drawn.clear();
    g_wall = 0;
}

void tick()
{
    // (the model's suspend pass turns its geometries on when its decision changes: off again)
    for (size_t i = 0; i < g_concealed_records.size();) {
        HiddenObjectRec& rec = g_concealed_records[i];
        objects::GameEntity* object = live_object(rec.id);
        if (!object) {
            g_concealed_records.erase(g_concealed_records.begin() + static_cast<long>(i));
            continue;
        }
        for (int g = 0; g < static_cast<int>(rec.geoms.size()); ++g) {
            void* geom = object_edit::get_geometry(object, g);
            if (geom && geom_active(geom))
                set_geom_active(geom, false);
        }
        ++i;
    }
}

bool show_collision() { return g_show_collision; }

void set_show_collision(bool on)
{
    g_show_collision = on;
    g_setting_read = true;
    config_set_int("editor", "collision", on ? 1 : 0);
    if (!on)
        g_drawn.clear();
}

void add_collision(V3 camera_pos, const TickCycle& frame, const Lens& lens, const uint32_t viewport[4], float near_z,
                   std::vector<ui::Overlay::Triangle>* triangles, std::vector<ui::Seg>* lines)
{
    g_drawn.clear();
    g_stats = {};
    if (!g_show_collision)
        return;
    // The sectors near enough, nearest first (invisible walls before the rest: never left out by the cap).
    struct Candidate {
        uint32_t checksum;
        render::WorldCell* sector;
        render::TriMeshSource* tri;
        float order;
        bool off;
    };
    struct Walk {
        V3 camera;
        const TickCycle* frame;
        float near_z_;
        std::vector<Candidate> found;
    } walk = {camera_pos, &frame, near_z, {}};
    scenery::for_each_level_sector(
        [](void* context, uint32_t checksum, render::WorldCell* sector) {
            auto* w = static_cast<Walk*>(context);
            bool off = false;
            render::TriMeshSource* tri = drawn_tri_data_of(checksum, sector, &off);
            if (!tri || tri->tri_count_ == 0 || hidden_sector(checksum))
                return;
            bool wall = geom_of(sector) == nullptr;
            V3 min = to_v3(tri->lower_limit_), max = to_v3(tri->upper_limit_);
            float distance = distance_to_box(w->camera, min, max);
            if (distance > (wall ? kWallRadius : kRadius) && checksum != g_wall)
                return;
            // (wholly behind the camera: left out)
            bool front = false;
            for (int i = 0; i < 8 && !front; ++i) {
                V3 corner = {(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z};
                front = -get_dot(corner - w->camera, w->frame->back) >= w->near_z_;
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
    const V3 light = make_unit({0.35f, 0.85f, 0.4f});
    // A face projected and styled (style_of): its polygon added, its edges drawn; false when it isn't on the screen.
    auto add_face = [&](const V3* p, const FaceFacts& facts) {
        V3 clipped[4];
        int count = clip_triangle_to_near(camera_pos, frame, near_z, p, clipped);
        if (!count)
            return false;
        Polygon poly = {};
        poly.count = count;
        bool left = true, right = true, below = true, above = true;
        float depth = 0.0f;
        for (int k = 0; k < count; ++k) {
            float nx, ny;
            if (!cast_onto(camera_pos, frame, lens, clipped[k], near_z * 0.5f, &nx, &ny)) {
                nx = ny = 0.0f; // (on the near plane - can't happen after clipping)
            }
            left = left && nx < -1.0f;
            right = right && nx > 1.0f;
            below = below && ny < -1.0f;
            above = above && ny > 1.0f;
            ndc_to_pixel(viewport, nx, ny, &poly.x[k], &poly.y[k]);
            depth += -get_dot(clipped[k] - camera_pos, frame.back);
        }
        if (left || right || below || above)
            return false;
        poly.depth = depth / static_cast<float>(count);
        FaceStyle style = style_of(facts);
        uint32_t argb = style.fill;
        // (shaded by the face's facing: the shape reads through the colour)
        V3 normal = perpendicular(p[1] - p[0], p[2] - p[0]);
        float length = std::sqrt(get_dot(normal, normal));
        float shade = length > 0.0f ? 0.55f + 0.45f * std::fabs(get_dot(normal, light)) / length : 1.0f;
        auto r = static_cast<uint32_t>(((argb >> 16) & 0xFF) * shade);
        auto g = static_cast<uint32_t>(((argb >> 8) & 0xFF) * shade);
        auto b = static_cast<uint32_t>((argb & 0xFF) * shade);
        poly.argb = (argb & 0xFF000000) | (r << 16) | (g << 8) | b;
        polygons.push_back(poly);
        if (style.edge && lines) {
            for (int k = 0; k < count; ++k) {
                int n = (k + 1) % count;
                ui::Seg line = {poly.x[k], poly.y[k], poly.x[n], poly.y[n], style.edge, style.thickness};
                if (style.dashed)
                    add_dashed(lines, line, 6.0f, 5.0f);
                else
                    lines->push_back(line);
            }
        }
        return true;
    };
    // (the level's triggers: their kinds by sector)
    std::unordered_map<uint32_t, uint32_t> trigger_colours;
    for (const triggers::LevelTrigger& t : triggers::level_triggers())
        trigger_colours[t.sector] = triggers::kind_colour(t.kind);
    for (const Candidate& candidate : walk.found) {
        if (static_cast<int>(polygons.size()) >= kMaxTriangles) {
            g_stats.capped = true;
            break;
        }
        const Cached* cached = cache_of(candidate.checksum, candidate.sector, candidate.tri);
        FaceFacts facts;
        facts.collision_only_ = cached->collision_only_;
        facts.off = candidate.off;
        facts.selected = candidate.checksum == g_wall;
        facts.hovered = candidate.checksum == g_hover_wall;
        auto trigger = trigger_colours.find(candidate.checksum);
        facts.trigger_colour_ = trigger == trigger_colours.end() ? 0 : trigger->second;
        bool any = false;
        for (size_t f = 0; f < cached->flags.size() && static_cast<int>(polygons.size()) < kMaxTriangles; ++f) {
            const V3* p = &cached->points[f * 3];
            facts.flags = cached->flags[f];
            facts.taken = cached->taken[f] != 0;
            if (!add_face(p, facts))
                continue;
            // (a drawn piece's collision switched off isn't in the way of a click: the piece's own click selects it;
            // nor is a face taken along - its piece's companion is where it is now)
            if ((cached->collision_only_ || !candidate.off) && !facts.taken)
                g_drawn.push_back({candidate.checksum, cached->collision_only_, p[0], p[1], p[2], cached->index[f]});
            any = true;
            g_stats.taken += facts.taken ? 1 : 0;
            g_stats.triggers += (facts.flags & 0x40) && facts.trigger_colour_ ? 1 : 0;
        }
        if (any) {
            ++g_stats.sectors;
            g_stats.walls += cached->collision_only_ ? 1 : 0;
            g_stats.off += candidate.off ? 1 : 0;
        }
    }
    // The moved pieces' shared collision where it is now (level_editor_shared_coll.h - not a sector's: drawn apart).
    struct CompanionWalk {
        V3 camera;
        std::vector<std::pair<uint32_t, const shared_collision::Companion*>> near;
    } companions = {camera_pos, {}};
    shared_collision::for_each_companion(
        [](void* context, uint32_t checksum, const shared_collision::Companion& companion) {
            auto* w = static_cast<CompanionWalk*>(context);
            const render::TriMeshSource& tri = companion.get_geometry();
            if (distance_to_box(w->camera, to_v3(tri.lower_limit_), to_v3(tri.upper_limit_)) <= kRadius &&
                !hidden_sector(checksum))
                w->near.emplace_back(checksum, &companion);
        },
        &companions);
    for (const auto& entry : companions.near) {
        const render::TriMeshSource& tri = entry.second->get_geometry();
        bool any = false;
        for (int f = 0; f < tri.tri_count_ && tri.faces_ && static_cast<int>(polygons.size()) < kMaxTriangles; ++f) {
            V3 p[3];
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                uint32_t index = tri.face_vertex(f, k);
                ok = index < tri.num_vertices_;
                if (ok)
                    p[k] = entry.second->get_vertex(static_cast<int>(index));
            }
            FaceFacts facts;
            facts.flags = *reinterpret_cast<const uint16_t*>(tri.faces_ + f * (tri.small_faces_ ? 8 : 10));
            facts.companion = true;
            bool drawn = ok && add_face(p, facts);
            if (drawn)
                g_drawn.push_back({entry.first, false, p[0], p[1], p[2], f, true});
            any = drawn || any;
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

Attributes last_stats() { return g_stats; }

uint32_t pick_wall(V3 origin, V3 unit_dir)
{
    float best = 1e30f;
    const Drawn* nearest = nullptr;
    for (const Drawn& d : g_drawn) {
        float t;
        if (d.companion)
            continue; // (as before the face flags: a wall's faces only compete with sectors' faces)
        if (picking::ray_triangle(origin, unit_dir, d.a, d.b, d.c, &t) && t < best) {
            best = t;
            nearest = &d;
        }
    }
    return nearest && nearest->wall ? nearest->checksum : 0;
}

bool pick_face(V3 origin, V3 unit_dir, FaceHit* hit)
{
    float best = 1e30f;
    const Drawn* nearest = nullptr;
    for (const Drawn& d : g_drawn) {
        float t;
        if (d.face >= 0 && picking::ray_triangle(origin, unit_dir, d.a, d.b, d.c, &t) && t < best) {
            best = t;
            nearest = &d;
        }
    }
    if (!nearest)
        return false;
    hit->checksum = nearest->checksum;
    hit->face = nearest->face;
    hit->companion = nearest->companion;
    hit->t = best;
    return true;
}

void stale_cache() { g_cache.clear(); }

void set_hover_wall(uint32_t checksum) { g_hover_wall = g_show_collision ? checksum : 0; }
uint32_t hover_wall() { return g_hover_wall; }
void select_wall(uint32_t checksum) { g_wall = checksum; }
uint32_t selected_wall() { return g_wall; }
void clear_wall() { g_wall = 0; }

std::string describe_wall(uint32_t checksum)
{
    render::WorldCell* sector = scenery::sector_of(checksum);
    render::TriMeshSource* tri = sector ? any_tri_data_of(sector, nullptr) : nullptr;
    if (!tri)
        return "(gone)";
    // The faces' flags: each distinct value and how many faces have it.
    std::vector<std::pair<uint16_t, int>> counts;
    for (int f = 0; f < tri->tri_count_ && tri->faces_; ++f) {
        uint16_t flags = *reinterpret_cast<const uint16_t*>(tri->faces_ + f * (tri->small_faces_ ? 8 : 10));
        auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == flags; });
        if (it == counts.end())
            counts.emplace_back(flags, 1);
        else
            ++it->second;
    }
    char text[256];
    std::snprintf(
        text, sizeof(text),
        "Collision only: nothing is drawn for it.\n%d faces, %d vertices\nbox (%.0f, %.0f, %.0f) to (%.0f, %.0f, "
        "%.0f)\nsize %.0f x %.0f x %.0f\nface flags:",
        tri->tri_count_, tri->num_vertices_, tri->lower_limit_.x, tri->lower_limit_.y, tri->lower_limit_.z,
        tri->upper_limit_.x, tri->upper_limit_.y, tri->upper_limit_.z, tri->upper_limit_.x - tri->lower_limit_.x,
        tri->upper_limit_.y - tri->lower_limit_.y, tri->upper_limit_.z - tri->lower_limit_.z);
    std::string out = text;
    for (size_t i = 0; i < counts.size() && i < 8; ++i) {
        std::snprintf(text, sizeof(text), " 0x%04X x%d", counts[i].first, counts[i].second);
        out += text;
    }
    if (counts.size() > 8)
        out += " ...";
    return out;
}

bool hidden_sector(uint32_t checksum)
{
    return std::any_of(g_hidden_sectors.begin(), g_hidden_sectors.end(),
                       [&](const HiddenSectorRec& r) { return r.checksum == checksum; });
}

bool hidden_object(uint32_t id)
{
    return std::any_of(g_concealed_records.begin(), g_concealed_records.end(),
                       [&](const HiddenObjectRec& r) { return r.id == id; });
}

int conceal(const std::vector<uint32_t>& sectors, const std::vector<uint32_t>& objects)
{
    int hidden = 0;
    for (uint32_t checksum : sectors)
        hidden += hide_sector(checksum) ? 1 : 0;
    for (uint32_t id : objects)
        hidden += hide_object(id) ? 1 : 0;
    if (debug_enabled())
        get_log("level editor: view - hid %d (%d sectors, %d objects hidden in all)", hidden,
                static_cast<int>(g_hidden_sectors.size()), static_cast<int>(g_concealed_records.size()));
    return hidden;
}

int get_isolate(const std::vector<uint32_t>& sectors, const std::vector<uint32_t>& objects)
{
    struct SectorWalk {
        std::vector<uint32_t> ids;
    } sector_walk;
    scenery::for_each_level_sector(
        [](void* context, uint32_t checksum, render::WorldCell* sector) {
            if (geom_of(sector))
                static_cast<SectorWalk*>(context)->ids.push_back(checksum);
        },
        &sector_walk);
    ObjectWalk object_walk;
    if (objects::EntityHub* manager = objects::object_hub_slot())
        manager->for_each(collect_object, &object_walk);
    return conceal(isolate_others(sector_walk.ids, sectors), isolate_others(object_walk.ids, objects));
}

void show_all()
{
    for (const HiddenSectorRec& rec : g_hidden_sectors) {
        if (scenery::sector_of(rec.checksum) == rec.sector && geom_of(rec.sector))
            toggle_shown(rec.sector, rec.mask);
    }
    g_hidden_sectors.clear();
    // (a copy made of a hidden piece - picked in the Outliner - copied its visibility too: shown as its source is)
    for (const scenery::Replicate& clone : scenery::get_clones()) {
        render::WorldCell* copy = scenery::sector_of(clone.checksum);
        render::WorldCell* source = scenery::sector_of(clone.source);
        if (copy && source && geom_of(copy) && geom_of(source) && fetch_shown(copy) == 0 && fetch_shown(source) != 0)
            toggle_shown(copy, fetch_shown(source));
    }
    for (const HiddenObjectRec& rec : g_concealed_records) {
        objects::GameEntity* object = live_object(rec.id);
        for (int g = 0; object && g < static_cast<int>(rec.geoms.size()); ++g) {
            void* geom = object_edit::get_geometry(object, g);
            if (geom && geom == rec.geoms[g].first)
                set_geom_active(geom, rec.geoms[g].second);
        }
    }
    g_concealed_records.clear();
}

int get_show(const std::vector<uint32_t>& sectors, const std::vector<uint32_t>& objects)
{
    int shown = 0;
    for (auto it = g_hidden_sectors.begin(); it != g_hidden_sectors.end();) {
        if (std::find(sectors.begin(), sectors.end(), it->checksum) == sectors.end()) {
            ++it;
            continue;
        }
        if (scenery::sector_of(it->checksum) == it->sector && geom_of(it->sector))
            toggle_shown(it->sector, it->mask);
        it = g_hidden_sectors.erase(it);
        ++shown;
    }
    for (auto it = g_concealed_records.begin(); it != g_concealed_records.end();) {
        if (std::find(objects.begin(), objects.end(), it->id) == objects.end()) {
            ++it;
            continue;
        }
        objects::GameEntity* object = live_object(it->id);
        for (int g = 0; object && g < static_cast<int>(it->geoms.size()); ++g) {
            void* geom = object_edit::get_geometry(object, g);
            if (geom && geom == it->geoms[g].first)
                set_geom_active(geom, it->geoms[g].second);
        }
        it = g_concealed_records.erase(it);
        ++shown;
    }
    return shown;
}

int hidden_count() { return static_cast<int>(g_hidden_sectors.size() + g_concealed_records.size()); }

std::vector<uint32_t> hidden_ids()
{
    std::vector<uint32_t> ids;
    for (const HiddenSectorRec& rec : g_hidden_sectors)
        ids.push_back(rec.checksum);
    for (const HiddenObjectRec& rec : g_concealed_records)
        ids.push_back(rec.id);
    return ids;
}

} // namespace frontend::level_editor::view
