// NOT ORIGINAL - the level editor's view options (ours). See level_editor_view.h.

#include "editor/level_editor_view.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <array>

#include "engine/core/enum_ops.h"
#include "editor/level_editor.h"
#include "editor/level_editor_ui.h"
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
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/render/coll_obj.h"

namespace frontend::level_editor::view {

namespace {

// The surface kinds overlay fill colours (ARGB).
constexpr std::uint32_t kInvisibleWallColour = 0xB0FF20E0;
constexpr std::uint32_t kTriggerColour = 0x782080FF;
constexpr std::uint32_t kNoCollideColour = 0x40A0A0A0;
constexpr std::uint32_t kWallrideColour = 0x78FFD020;
constexpr std::uint32_t kVertColour = 0x78FF8020;
constexpr std::uint32_t kNotSkatableColour = 0x78FF3030;
constexpr std::uint32_t kSkatableColour = 0x7840E040;
constexpr std::uint32_t kPlainColour = 0x58D0E8FF;
constexpr std::uint32_t kRgbMaskU = 0x00FFFFFFU;
constexpr std::uint32_t kFadedAlphaMax = 0x30; // the most alpha a faded face keeps
constexpr std::uint32_t kTriggerFillAlpha = 0x78000000U;
constexpr std::uint32_t kSelectedWallFill = 0xC8FFA0FFU; // the selected wall, lighter
constexpr std::uint32_t kSelectedEdge = 0xFFFFFFFFU;
constexpr std::uint32_t kHoveredEdge = 0xFFFFF060U;
constexpr std::uint32_t kWallEdge = 0xE0FF60F0U;
constexpr float kHighlightedEdgeThickness = 1.5F;
constexpr float kWallPriorityBias = 1e7F; // walls are ordered before faces by this much
constexpr std::uint32_t kAlphaMask = 0xFF000000;
constexpr float kDashLength = 6.0F;
constexpr float kDashGap = 5.0F;
constexpr float kFarthest = 1e30F; // a pick search starts from this distance
constexpr std::size_t kWallTextSize = 256;
constexpr std::size_t kMaxCountsShown = 8; // face counts listed in the wall description

} // namespace

namespace {

constexpr float kHalf = 0.5F; // half

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

Surface get_classify(render::FaceFlag face_flags, bool collision_only)
{
    if (collision_only) {
        return Surface::kInvisibleWall;
    }
    if (core::any(face_flags & render::FaceFlag::kTrigger)) {
        return Surface::kTrigger;
    }
    if (core::any(face_flags & render::FaceFlag::kNotCollidable)) {
        return Surface::kNoCollide;
    }
    if (core::any(face_flags & render::FaceFlag::kWall)) {
        return Surface::kWallride;
    }
    if (core::any(face_flags & render::FaceFlag::kVert)) {
        return Surface::kVert;
    }
    if (core::any(face_flags & render::FaceFlag::kNotSkatable) &&
        !core::any(face_flags & render::FaceFlag::kSkatable)) {
        return Surface::kNotSkatable;
    }
    if (core::any(face_flags & render::FaceFlag::kSkatable)) {
        return Surface::kSkatable;
    }
    return Surface::kPlain;
}

uint32_t surface_colour(Surface surface)
{
    switch (surface) {
    case Surface::kInvisibleWall:
        return kInvisibleWallColour;
    case Surface::kTrigger:
        return kTriggerColour;
    case Surface::kNoCollide:
        return kNoCollideColour;
    case Surface::kWallride:
        return kWallrideColour;
    case Surface::kVert:
        return kVertColour;
    case Surface::kNotSkatable:
        return kNotSkatableColour;
    case Surface::kSkatable:
        return kSkatableColour;
    default:
        return kPlainColour;
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

int clip_triangle_to_near(V3 camera_pos, const TickCycle& frame, float near_z, std::span<const V3, 3> in, std::span<V3, 4> out)
{
    // (Sutherland-Hodgman against one plane: depth = (p - camera) . -back)
    std::array<float, 3> depth{};
    for (int i = 0; i < 3; ++i) {
        core::at(depth, i) = -get_dot(core::at(in, i) - camera_pos, frame.back);
    }
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        const int next = (i + 1) % 3;
        const bool in_i = core::at(depth, i) >= near_z;
        const bool in_j = core::at(depth, next) >= near_z;
        if (in_i) {
            core::at(out, count++) = core::at(in, i);
        }
        if (in_i != in_j) {
            const float scale = (near_z - core::at(depth, i)) / (core::at(depth, next) - core::at(depth, i));
            core::at(out, count++) = core::at(in, i) + (core::at(in, next) - core::at(in, i)) * scale;
        }
    }
    return count >= 3 ? count : 0;
}

float distance_to_box(V3 point, V3 min, V3 max)
{
    auto axis = [](float value, float lo, float hi) {
        return value < lo ? lo - value : value > hi ? value - hi : 0.0F;
    };
    const float dx = axis(point.x, min.x, max.x);
    const float dy = axis(point.y, min.y, max.y);
    const float dz = axis(point.z, min.z, max.z);
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

bool HiddenSet::get_has(uint32_t id) const { return std::ranges::find(ids, id) != ids.end(); }

bool HiddenSet::append_one(uint32_t id)
{
    if (get_has(id)) {
        return false;
    }
    ids.push_back(id);
    return true;
}

void add_dashed(std::vector<ui::Seg>* lines, const ui::Seg& line, float dash, float gap)
{
    const float dx = line.bx - line.ax;
    const float dy = line.by - line.ay;
    const float length = std::sqrt((dx * dx) + (dy * dy));
    if (!(length > 0.0F) || !(dash > 0.0F) || gap < 0.0F) {
        return;
    }
    const float ux = dx / length;
    const float uy = dy / length;
    // (at most 256 dashes an edge: a face seen from very near)
    const float period = std::max(dash + gap, length / 256.0F);
    for (int dash_index = 0;; ++dash_index) {
        const float at = static_cast<float>(dash_index) * period;
        if (!(at < length)) {
            break;
        }
        const float end = std::min(length, at + dash);
        ui::Seg piece = line;
        piece.ax = line.ax + (ux * at);
        piece.ay = line.ay + (uy * at);
        piece.bx = line.ax + (ux * end);
        piece.by = line.ay + (uy * end);
        lines->push_back(piece);
    }
}

uint32_t get_faded(uint32_t argb) { return (argb & kRgbMask) | (std::min<uint32_t>(argb >> kAlphaShift, kFadedAlphaMax) << kAlphaShift); }

FaceStyle style_of(const FaceFacts& facts)
{
    FaceStyle style;
    const bool wall = facts.collision_only_ && !facts.companion;
    if (core::any(facts.flags & render::FaceFlag::kTrigger) && (facts.trigger_colour_ != 0U)) {
        style.fill = (facts.trigger_colour_ & kRgbMaskU) | kTriggerFillAlpha;
    } else {
        style.fill = surface_colour(get_classify(facts.taken ? facts.flags & ~render::FaceFlag::kNotCollidable : facts.flags, wall));
    }
    if (facts.selected) {
        style.fill = kSelectedWallFill; // (the selected wall: lighter)
    } else if (facts.hovered) {
        style.fill = kHoverWall;
    }
    if (facts.off || facts.taken) {
        style.fill = get_faded(style.fill); // (switched off / taken along: faded)
    }
    if (facts.companion) {
        style.edge = kCompanionEdge;
    } else if (facts.taken) {
        style.edge = kTakenEdge;
        style.dashed = true;
    } else if (wall || facts.selected || facts.hovered) {
        style.edge = facts.selected ? kSelectedEdge : facts.hovered ? kHoveredEdge : kWallEdge;
        style.dashed = facts.off;
        style.thickness = facts.selected || facts.hovered ? kHighlightedEdgeThickness : 1.0F;
    }
    return style;
}

std::vector<uint32_t> isolate_others(const std::vector<uint32_t>& all, std::span<const uint32_t> keep)
{
    std::vector<uint32_t> others;
    for (const uint32_t id : all) {
        if (std::ranges::find(keep, id) == keep.end() && std::ranges::find(others, id) == others.end()) {
            others.push_back(id);
        }
    }
    return others;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

render::PcGeom* geom_of(render::WorldCell* sector) { return static_cast<render::PcGeom*>(sector->geom_handle()); }
gsl::owner<render::HitMesh*> coll_of(render::WorldCell* sector) { return sector->hit_body(); }

// The sector's collision geometry, whether its collision is on or off (StaticCollider; `off`: its flag 4 - the game's
// switch, level_editor_walls.h).
render::TriMeshSource* any_tri_data_of(render::WorldCell* sector, bool* off)
{
    const render::HitMesh* coll = coll_of(sector);
    if (!render::is_static_collider(coll)) {
        return nullptr;
    }
    if (off) {
        *off = (coll->flag_bits() & 4) != 0;
    }
    return coll->geometry();
}

// The collision drawn for a sector (null: none): on, or switched off by the editor's switch (an invisible wall, or a
// drawn piece's collision by itself - shown faded so it can be switched on again). A piece hidden (its geometry off,
// its collision with it) isn't drawn.
render::TriMeshSource* drawn_tri_data_of(uint32_t checksum, render::WorldCell* sector, bool* off)
{
    render::TriMeshSource* tri = any_tri_data_of(sector, off);
    if (tri && *off && geom_of(sector) && !walls::disengaged(checksum)) {
        return nullptr;
    }
    return tri;
}

V3 to_v3(const maths::Vec4& v) { return {.x = v.x, .y = v.y, .z = v.z}; }

// NOT ORIGINAL grouping - level_editor_view.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_view_state()).
struct LevelEditorViewState {
    bool show_collision_{};
    bool setting_read_{};
};

LevelEditorViewState& level_editor_view_state()
{
    static core::NoDestroy<LevelEditorViewState> state;
    return state.get();
}

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
    std::vector<render::FaceFlag> flags;
    std::vector<uint8_t> taken; // (each face: turned off for a moved piece's shared collision)
    std::vector<int> index;     // (each face: its index in the collision - faces with bad corners are left out)
};
std::unordered_map<uint32_t, Cached>& cache_state()
{
    static std::unordered_map<uint32_t, Cached> value;
    return value;
}

const Cached* cache_of(uint32_t checksum, render::WorldCell* sector, const render::TriMeshSource* tri)
{
    const V3 min = to_v3(tri->bounds().min);
    const V3 max = to_v3(tri->bounds().max);
    V3 first = {};
    V3 last = {};
    if (tri->vertices() && (tri->num_vertices() != 0U)) {
        first = to_v3(tri->read_stored_vertex(0));
        last = to_v3(tri->read_stored_vertex(tri->num_vertices() - 1));
    }
    auto same = [](V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    const bool collision_only = geom_of(sector) == nullptr;
    Cached& cached = cache_state()[checksum];
    const uint32_t generation = shared_collision::get_generation();
    if (cached.sector == sector && cached.tri == tri && std::cmp_equal(cached.faces, tri->tri_count()) &&
        cached.collision_only_ == collision_only && cached.generation == generation && same(cached.min, min) &&
        same(cached.max, max) && same(cached.first, first) && same(cached.last, last)) {
        return &cached;
    }
    cached = Cached{};
    cached.generation = generation;
    std::vector<int> taken = shared_collision::taken_faces(tri);
    cached.sector = sector;
    cached.tri = tri;
    cached.min = min;
    cached.max = max;
    cached.first = first;
    cached.last = last;
    cached.faces = tri->tri_count();
    cached.collision_only_ = collision_only;
    if (!tri->faces() || !tri->vertices()) {
        return &cached;
    }
    cached.points.reserve(static_cast<size_t>(cached.faces) * 3);
    cached.flags.reserve(cached.faces);
    for (int face_index = 0; face_index < cached.faces; ++face_index) {
        bool ok = true;
        std::array<V3, 3> corners{};
        for (int k = 0; k < 3; ++k) {
            const uint32_t index = tri->face_vertex(face_index, k);
            if (index >= tri->num_vertices()) {
                ok = false;
                break;
            }
            core::at(corners, k) = to_v3(tri->read_stored_vertex(static_cast<int>(index)));
        }
        if (!ok) {
            continue;
        }
        cached.points.insert(cached.points.end(), corners.begin(), corners.end());
        cached.flags.push_back(tri->face_flags(face_index));
        cached.taken.push_back(std::ranges::find(taken, face_index) != taken.end() ? 1 : 0);
        cached.index.push_back(face_index);
    }
    return &cached;
}

uint32_t& stored_hover_wall()
{
    static uint32_t hover_wall{}; // (Shift held with the collision shown: the wall a Shift+click would take)
    return hover_wall;
}

// What was drawn last frame (for Shift+click on a wall): each face's sector and points.
struct Drawn {
    uint32_t checksum;
    bool wall;
    V3 a, b, c;
    int face = -1;          // (its index in its collision - level_editor_faces.h)
    bool companion = false; // (a moved piece's shared collision: not a wall's, never Shift+clicked)
};
// NOT ORIGINAL grouping - what was drawn, the stats and the wall (were globals). Made on first use, never destroyed
// (view_stats()).
struct ViewStats {
    std::vector<Drawn> drawn_;
    Attributes stats_{};
    uint32_t wall_{};
};

ViewStats& view_stats()
{
    static core::NoDestroy<ViewStats> state;
    return state.get();
}

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
// NOT ORIGINAL grouping - the hidden sectors and the records concealed (were globals). Made on first use, never
// destroyed (hidden_sectors()).
struct HiddenSectors {
    std::vector<HiddenSectorRec> hidden_sectors_;
    std::vector<HiddenObjectRec> concealed_records_;
};

HiddenSectors& hidden_sectors()
{
    static core::NoDestroy<HiddenSectors> state;
    return state.get();
}

// WorldCell::toggle_shown (0x0049E680, thiscall, RET 4) / fetch_shown (0x0049E690): the viewports it shows in.
void toggle_shown(render::WorldCell* sector, uint32_t mask) { sector->toggle_shown(mask); }
uint32_t fetch_shown(render::WorldCell* sector) { return sector->fetch_shown(); }

// A geometry's change_live (vfunc +0x34) / is_operative (vfunc +0x38).
void set_geom_active(void* geom, bool on) { static_cast<render::Geom*>(geom)->change_live(on); }
bool geom_active(void* geom) { return static_cast<render::Geom*>(geom)->is_operative(); }

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && ((object->object_flags() & objects::kObjectFlagDead) == 0U) ? object : nullptr;
}

bool hide_sector(uint32_t checksum)
{
    if (hidden_sector(checksum)) {
        return false;
    }
    render::WorldCell* sector = scenery::sector_of(checksum);
    if (!sector || !geom_of(sector)) {
        return false; // (collision only: nothing drawn to hide)
    }
    hidden_sectors().hidden_sectors_.push_back({.checksum = checksum, .sector = sector, .mask = fetch_shown(sector)});
    toggle_shown(sector, 0);
    return true;
}

bool hide_object(uint32_t id)
{
    if (hidden_object(id)) {
        return false;
    }
    objects::GameEntity* object = live_object(id);
    if (!object) {
        return false;
    }
    HiddenObjectRec rec = {.id = id, .geoms = {}};
    for (int i = 0; i < kMaxGeometries; ++i) {
        void* geom = object_edit::get_geometry(object, i);
        if (!geom) {
            break;
        }
        rec.geoms.emplace_back(geom, geom_active(geom));
        set_geom_active(geom, false);
    }
    if (rec.geoms.empty()) {
        return false;
    }
    hidden_sectors().concealed_records_.push_back(rec);
    return true;
}

struct ObjectWalk {
    std::vector<uint32_t> ids;
};
void collect_object(objects::Entity* object, void* data)
{
    auto* composite = core::down_cast<objects::GameEntity>(object);
    OrientedBox box{};
    if (object_edit::model_box(composite, &box)) {
        static_cast<ObjectWalk*>(data)->ids.push_back(composite->ident());
    }
}

} // namespace

void kickoff()
{
    if (!level_editor_view_state().setting_read_) {
        level_editor_view_state().show_collision_ = config_int("editor", "collision", 0) != 0;
        level_editor_view_state().setting_read_ = true;
    }
    cache_state().clear();
    view_stats().drawn_.clear();
    view_stats().wall_ = 0;
    stored_hover_wall() = 0;
    hidden_sectors().hidden_sectors_.clear();
    hidden_sectors().concealed_records_.clear();
}

void halt()
{
    show_all();
    cache_state().clear();
    view_stats().drawn_.clear();
    view_stats().wall_ = 0;
}

void tick()
{
    // (the model's suspend pass turns its geometries on when its decision changes: off again)
    for (size_t i = 0; i < hidden_sectors().concealed_records_.size();) {
        const HiddenObjectRec& rec = core::at(hidden_sectors().concealed_records_, i);
        objects::GameEntity* object = live_object(rec.id);
        if (!object) {
            hidden_sectors().concealed_records_.erase(hidden_sectors().concealed_records_.begin() + static_cast<long>(i));
            continue;
        }
        for (int geom_index = 0; std::cmp_less(geom_index, rec.geoms.size()); ++geom_index) {
            void* geom = object_edit::get_geometry(object, geom_index);
            if (geom && geom_active(geom)) {
                set_geom_active(geom, false);
            }
        }
        ++i;
    }
}

bool show_collision() { return level_editor_view_state().show_collision_; }

void set_show_collision(bool on)
{
    level_editor_view_state().show_collision_ = on;
    level_editor_view_state().setting_read_ = true;
    config_set_int("editor", "collision", on ? 1 : 0);
    if (!on) {
        view_stats().drawn_.clear();
    }
}

void add_collision(V3 camera_pos, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                   std::vector<ui::Overlay::Triangle>* triangles, std::vector<ui::Seg>* lines)
{
    view_stats().drawn_.clear();
    view_stats().stats_ = {};
    if (!level_editor_view_state().show_collision_) {
        return;
    }
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
    } walk = {.camera = camera_pos, .frame = &frame, .near_z_ = near_z, .found = {}};
    scenery::for_each_level_sector(
        [](void* context, uint32_t checksum, render::WorldCell* sector) {
            auto* w = static_cast<Walk*>(context);
            bool off = false;
            render::TriMeshSource* tri = drawn_tri_data_of(checksum, sector, &off);
            if (!tri || tri->tri_count() == 0 || hidden_sector(checksum)) {
                return;
            }
            const bool wall = geom_of(sector) == nullptr;
            const V3 min = to_v3(tri->bounds().min);
            const V3 max = to_v3(tri->bounds().max);
            const float distance = distance_to_box(w->camera, min, max);
            if (distance > (wall ? kWallRadius : kRadius) && checksum != view_stats().wall_) {
                return;
            }
            // (wholly behind the camera: left out)
            bool front = false;
            for (int i = 0; i < kBoxCorners && !front; ++i) {
                const V3 corner = {.x = (i & 1) ? max.x : min.x, .y = (i & 2) ? max.y : min.y, .z = (i & 4) ? max.z : min.z};
                front = -get_dot(corner - w->camera, w->frame->back) >= w->near_z_;
            }
            if (!front) {
                return;
            }
            w->found.push_back({.checksum = checksum, .sector = sector, .tri = tri, .order = wall || checksum == view_stats().wall_ ? distance - kWallPriorityBias : distance, .off = off});
        },
        &walk);
    std::ranges::sort(walk.found, [](const Candidate& a, const Candidate& b) { return a.order < b.order; });

    struct Polygon {
        float depth;
        uint32_t argb;
        int count;
        std::array<float, 4> x;
        std::array<float, 4> y;
    };
    std::vector<Polygon> polygons;
    const V3 light = make_unit({.x = 0.35F, .y = 0.85F, .z = 0.4F});
    // A face projected and styled (style_of): its polygon added, its edges drawn; false when it isn't on the screen.
    auto add_face = [&](std::span<const V3, 3> corners, const FaceFacts& facts) {
        std::array<V3, 4> clipped{};
        const int count = clip_triangle_to_near(camera_pos, frame, near_z, corners, clipped);
        if (!count) {
            return false;
        }
        Polygon poly = {};
        poly.count = count;
        bool left = true;
        bool right = true;
        bool below = true;
        bool above = true;
        float depth = 0.0F;
        for (int k = 0; k < count; ++k) {
            float nx = 0.0F;
            float ny = 0.0F;
            if (!cast_onto(camera_pos, frame, lens, core::at(clipped, k), near_z * kHalf, &nx, &ny)) {
                nx = ny = 0.0F; // (on the near plane - can't happen after clipping)
            }
            left = left && nx < -1.0F;
            right = right && nx > 1.0F;
            below = below && ny < -1.0F;
            above = above && ny > 1.0F;
            ndc_to_pixel(viewport, nx, ny, &core::at(poly.x, k), &core::at(poly.y, k));
            depth += -get_dot(core::at(clipped, k) - camera_pos, frame.back);
        }
        if (left || right || below || above) {
            return false;
        }
        poly.depth = depth / static_cast<float>(count);
        const FaceStyle style = style_of(facts);
        const uint32_t argb = style.fill;
        // (shaded by the face's facing: the shape reads through the colour)
        const V3 normal =
            perpendicular(core::at(corners, 1) - core::at(corners, 0), core::at(corners, 2) - core::at(corners, 0));
        const float length = std::sqrt(get_dot(normal, normal));
        const float shade = length > 0.0F ? 0.55F + (0.45F * std::fabs(get_dot(normal, light)) / length) : 1.0F;
        auto red_value = static_cast<uint32_t>(static_cast<float>((argb >> kRedShift) & kChannelMask) * shade);
        auto green_value = static_cast<uint32_t>(static_cast<float>((argb >> kGreenShift) & kChannelMask) * shade);
        auto blue_value = static_cast<uint32_t>(static_cast<float>(argb & kChannelMask) * shade);
        poly.argb = (argb & kAlphaMask) | (red_value << kRedShift) | (green_value << kGreenShift) | blue_value;
        polygons.push_back(poly);
        if (style.edge && lines) {
            for (int k = 0; k < count; ++k) {
                const int current_count = (k + 1) % count;
                const ui::Seg line = {.ax = core::at(poly.x, k),
                                      .ay = core::at(poly.y, k),
                                      .bx = core::at(poly.x, current_count),
                                      .by = core::at(poly.y, current_count),
                                      .argb = style.edge,
                                      .thickness = style.thickness};
                if (style.dashed) {
                    add_dashed(lines, line, kDashLength, kDashGap);
                } else {
                    lines->push_back(line);
                }
            }
        }
        return true;
    };
    // (the level's triggers: their kinds by sector)
    std::unordered_map<uint32_t, uint32_t> trigger_colours;
    for (const triggers::LevelTrigger& level_trigger : triggers::level_triggers()) {
        trigger_colours[level_trigger.sector] = triggers::kind_colour(level_trigger.kind);
    }
    for (const Candidate& candidate : walk.found) {
        if (static_cast<int>(polygons.size()) >= kMaxTriangles) {
            view_stats().stats_.capped = true;
            break;
        }
        const Cached* cached = cache_of(candidate.checksum, candidate.sector, candidate.tri);
        FaceFacts facts;
        facts.collision_only_ = cached->collision_only_;
        facts.off = candidate.off;
        facts.selected = candidate.checksum == view_stats().wall_;
        facts.hovered = candidate.checksum == stored_hover_wall();
        auto trigger = trigger_colours.find(candidate.checksum);
        facts.trigger_colour_ = trigger == trigger_colours.end() ? 0 : trigger->second;
        bool any = false;
        for (size_t face_index = 0;
             face_index < cached->flags.size() && static_cast<int>(polygons.size()) < kMaxTriangles; ++face_index) {
            const auto corners = std::span<const V3>(cached->points).subspan(face_index * 3).first<3>();
            facts.flags = core::at(cached->flags, face_index);
            facts.taken = core::at(cached->taken, face_index) != 0;
            if (!add_face(corners, facts)) {
                continue;
            }
            // (a drawn piece's collision switched off isn't in the way of a click: the piece's own click selects it;
            // nor is a face taken along - its piece's companion is where it is now)
            if ((cached->collision_only_ || !candidate.off) && !facts.taken) {
                view_stats().drawn_.push_back({.checksum = candidate.checksum,
                                               .wall = cached->collision_only_,
                                               .a = core::at(corners, 0),
                                               .b = core::at(corners, 1),
                                               .c = core::at(corners, 2),
                                               .face = core::at(cached->index, face_index)});
            }
            any = true;
            view_stats().stats_.taken += facts.taken ? 1 : 0;
            view_stats().stats_.triggers +=
                core::any(facts.flags & render::FaceFlag::kTrigger) && (facts.trigger_colour_ != 0U) ? 1 : 0;
        }
        if (any) {
            ++view_stats().stats_.sectors;
            view_stats().stats_.walls += cached->collision_only_ ? 1 : 0;
            view_stats().stats_.off += candidate.off ? 1 : 0;
        }
    }
    // The moved pieces' shared collision where it is now (level_editor_shared_coll.h - not a sector's: drawn apart).
    struct CompanionWalk {
        V3 camera;
        std::vector<std::pair<uint32_t, const shared_collision::Companion*>> near;
    } companions = {.camera = camera_pos, .near = {}};
    shared_collision::for_each_companion(
        [](void* context, uint32_t checksum, const shared_collision::Companion& companion) {
            auto* w = static_cast<CompanionWalk*>(context);
            const render::TriMeshSource& tri = companion.get_geometry();
            if (distance_to_box(w->camera, to_v3(tri.bounds().min), to_v3(tri.bounds().max)) <= kRadius &&
                !hidden_sector(checksum)) {
                w->near.emplace_back(checksum, &companion);
            }
        },
        &companions);
    for (const auto& entry : companions.near) {
        const render::TriMeshSource& tri = entry.second->get_geometry();
        bool any = false;
        for (int face_index = 0; std::cmp_less(face_index, tri.tri_count()) && tri.faces() &&
                                 static_cast<int>(polygons.size()) < kMaxTriangles;
             ++face_index) {
            std::array<V3, 3> corners{};
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                const uint32_t index = tri.face_vertex(face_index, k);
                ok = index < tri.num_vertices();
                if (ok) {
                    core::at(corners, k) = entry.second->get_vertex(static_cast<int>(index));
                }
            }
            FaceFacts facts;
            facts.flags = tri.face_flags(face_index);
            facts.companion = true;
            const bool drawn = ok && add_face(corners, facts);
            if (drawn) {
                view_stats().drawn_.push_back({.checksum = entry.first,
                                               .wall = false,
                                               .a = core::at(corners, 0),
                                               .b = core::at(corners, 1),
                                               .c = core::at(corners, 2),
                                               .face = face_index,
                                               .companion = true});
            }
            any = drawn || any;
        }
        view_stats().stats_.companions += any ? 1 : 0;
    }
    // Far to near: the nearer faces painted over the farther.
    std::ranges::sort(polygons, [](const Polygon& a, const Polygon& b) { return a.depth > b.depth; });
    triangles->reserve(triangles->size() + (polygons.size() * 2));
    for (const Polygon& polygon : polygons) {
        triangles->push_back({.ax = core::at(polygon.x, 0),
                              .ay = core::at(polygon.y, 0),
                              .bx = core::at(polygon.x, 1),
                              .by = core::at(polygon.y, 1),
                              .cx = core::at(polygon.x, 2),
                              .cy = core::at(polygon.y, 2),
                              .argb = polygon.argb});
        if (polygon.count == 4) {
            triangles->push_back({.ax = core::at(polygon.x, 0),
                                  .ay = core::at(polygon.y, 0),
                                  .bx = core::at(polygon.x, 2),
                                  .by = core::at(polygon.y, 2),
                                  .cx = core::at(polygon.x, 3),
                                  .cy = core::at(polygon.y, 3),
                                  .argb = polygon.argb});
        }
    }
    view_stats().stats_.triangles = static_cast<int>(polygons.size());
}

Attributes last_stats() { return view_stats().stats_; }

uint32_t pick_wall(V3 origin, V3 unit_dir)
{
    float best = kFarthest;
    const Drawn* nearest = nullptr;
    for (const Drawn& drawn : view_stats().drawn_) {
        float fraction = 0.0F;
        if (drawn.companion) {
            continue; // (as before the face flags: a wall's faces only compete with sectors' faces)
        }
        if (picking::ray_triangle({.origin = origin, .dir = unit_dir}, drawn.a, drawn.b, drawn.c, &fraction) &&
            fraction < best) {
            best = fraction;
            nearest = &drawn;
        }
    }
    return nearest && nearest->wall ? nearest->checksum : 0;
}

bool pick_face(V3 origin, V3 unit_dir, FaceHit* hit)
{
    float best = kFarthest;
    const Drawn* nearest = nullptr;
    for (const Drawn& drawn : view_stats().drawn_) {
        float fraction = 0.0F;
        if (drawn.face >= 0 &&
            picking::ray_triangle({.origin = origin, .dir = unit_dir}, drawn.a, drawn.b, drawn.c, &fraction) &&
            fraction < best) {
            best = fraction;
            nearest = &drawn;
        }
    }
    if (!nearest) {
        return false;
    }
    hit->checksum = nearest->checksum;
    hit->face = nearest->face;
    hit->companion = nearest->companion;
    hit->t = best;
    return true;
}

void stale_cache() { cache_state().clear(); }

void set_hover_wall(uint32_t checksum) { stored_hover_wall() = level_editor_view_state().show_collision_ ? checksum : 0; }
uint32_t hover_wall() { return stored_hover_wall(); }
void select_wall(uint32_t checksum) { view_stats().wall_ = checksum; }
uint32_t selected_wall() { return view_stats().wall_; }
void clear_wall() { view_stats().wall_ = 0; }

std::string describe_wall(uint32_t checksum)
{
    render::WorldCell* sector = scenery::sector_of(checksum);
    const render::TriMeshSource* tri = sector ? any_tri_data_of(sector, nullptr) : nullptr;
    if (!tri) {
        return "(gone)";
    }
    // The faces' flags: each distinct value and how many faces have it.
    std::vector<std::pair<render::FaceFlag, int>> counts;
    for (int face_index = 0; std::cmp_less(face_index, tri->tri_count()) && tri->faces(); ++face_index) {
        render::FaceFlag flags = tri->face_flags(face_index);
        auto it = std::ranges::find_if(counts, [&](const auto& entry) { return entry.first == flags; });
        if (it == counts.end()) {
            counts.emplace_back(flags, 1);
        } else {
            ++it->second;
        }
    }
    std::array<char, kWallTextSize> text{};
    text::format_to(
        text,
        "Collision only: nothing is drawn for it.\n%d faces, %d vertices\nbox (%.0f, %.0f, %.0f) to (%.0f, %.0f, "
        "%.0f)\nsize %.0f x %.0f x %.0f\nface flags:",
        tri->tri_count(), tri->num_vertices(), tri->bounds().min.x, tri->bounds().min.y, tri->bounds().min.z,
        tri->bounds().max.x, tri->bounds().max.y, tri->bounds().max.z, tri->bounds().max.x - tri->bounds().min.x,
        tri->bounds().max.y - tri->bounds().min.y, tri->bounds().max.z - tri->bounds().min.z);
    std::string out = text.data();
    for (size_t i = 0; i < counts.size() && i < kMaxCountsShown; ++i) {
        text::format_to(text, " 0x%04X x%d", core::to_underlying(core::at(counts, i).first), core::at(counts, i).second);
        out += text.data();
    }
    if (counts.size() > kMaxCountsShown) {
        out += " ...";
    }
    return out;
}

bool hidden_sector(uint32_t checksum)
{
    return std::ranges::any_of(hidden_sectors().hidden_sectors_, [&](const HiddenSectorRec& hidden_sector_rec) {
        return hidden_sector_rec.checksum == checksum;
    });
}

bool hidden_object(uint32_t id)
{
    return std::ranges::any_of(hidden_sectors().concealed_records_,
                               [&](const HiddenObjectRec& hidden_object_rec) { return hidden_object_rec.id == id; });
}

int conceal(const std::vector<uint32_t>& sectors, std::span<const uint32_t> objects)
{
    int hidden = 0;
    for (const uint32_t checksum : sectors) {
        hidden += hide_sector(checksum) ? 1 : 0;
    }
    for (const uint32_t id : objects) {
        hidden += hide_object(id) ? 1 : 0;
    }
    if (debug_enabled()) {
        get_log("level editor: view - hid %d (%d sectors, %d objects hidden in all)", hidden,
                static_cast<int>(hidden_sectors().hidden_sectors_.size()), static_cast<int>(hidden_sectors().concealed_records_.size()));
    }
    return hidden;
}

int get_isolate(const std::vector<uint32_t>& sectors, const std::vector<uint32_t>& objects)
{
    struct SectorWalk {
        std::vector<uint32_t> ids;
    } sector_walk;
    scenery::for_each_level_sector(
        [](void* context, uint32_t checksum, render::WorldCell* sector) {
            if (geom_of(sector)) {
                static_cast<SectorWalk*>(context)->ids.push_back(checksum);
            }
        },
        &sector_walk);
    ObjectWalk object_walk;
    if (objects::EntityHub* manager = objects::object_hub_slot().get()) {
        manager->for_each(collect_object, &object_walk);
    }
    return conceal(isolate_others(sector_walk.ids, sectors), isolate_others(object_walk.ids, objects));
}

void show_all()
{
    for (const HiddenSectorRec& rec : hidden_sectors().hidden_sectors_) {
        if (scenery::sector_of(rec.checksum) == rec.sector && geom_of(rec.sector)) {
            toggle_shown(rec.sector, rec.mask);
        }
    }
    hidden_sectors().hidden_sectors_.clear();
    // (a copy made of a hidden piece - picked in the Outliner - copied its visibility too: shown as its source is)
    for (const scenery::Replicate& clone : scenery::get_clones()) {
        render::WorldCell* copy = scenery::sector_of(clone.checksum);
        render::WorldCell* source = scenery::sector_of(clone.source);
        if (copy && source && geom_of(copy) && geom_of(source) && fetch_shown(copy) == 0 && fetch_shown(source) != 0) {
            toggle_shown(copy, fetch_shown(source));
        }
    }
    for (const HiddenObjectRec& rec : hidden_sectors().concealed_records_) {
        objects::GameEntity* object = live_object(rec.id);
        for (int geom_index = 0; object && std::cmp_less(geom_index, rec.geoms.size()); ++geom_index) {
            void* geom = object_edit::get_geometry(object, geom_index);
            if (geom && geom == core::at(rec.geoms, geom_index).first) {
                set_geom_active(geom, core::at(rec.geoms, geom_index).second);
            }
        }
    }
    hidden_sectors().concealed_records_.clear();
}

int get_show(const std::vector<uint32_t>& sectors, const std::vector<uint32_t>& objects)
{
    int shown = 0;
    for (auto it = hidden_sectors().hidden_sectors_.begin(); it != hidden_sectors().hidden_sectors_.end();) {
        if (std::ranges::find(sectors, it->checksum) == sectors.end()) {
            ++it;
            continue;
        }
        if (scenery::sector_of(it->checksum) == it->sector && geom_of(it->sector)) {
            toggle_shown(it->sector, it->mask);
        }
        it = hidden_sectors().hidden_sectors_.erase(it);
        ++shown;
    }
    for (auto it = hidden_sectors().concealed_records_.begin(); it != hidden_sectors().concealed_records_.end();) {
        if (std::ranges::find(objects, it->id) == objects.end()) {
            ++it;
            continue;
        }
        objects::GameEntity* object = live_object(it->id);
        for (int geom_index = 0; object && std::cmp_less(geom_index, it->geoms.size()); ++geom_index) {
            void* geom = object_edit::get_geometry(object, geom_index);
            if (geom && geom == core::at(it->geoms, geom_index).first) {
                set_geom_active(geom, core::at(it->geoms, geom_index).second);
            }
        }
        it = hidden_sectors().concealed_records_.erase(it);
        ++shown;
    }
    return shown;
}

int hidden_count() { return static_cast<int>(hidden_sectors().hidden_sectors_.size() + hidden_sectors().concealed_records_.size()); }

std::vector<uint32_t> hidden_ids()
{
    std::vector<uint32_t> ids;
    ids.reserve(hidden_sectors().hidden_sectors_.size());
    for (const HiddenSectorRec& rec : hidden_sectors().hidden_sectors_) {
        ids.push_back(rec.checksum);
    }
    for (const HiddenObjectRec& rec : hidden_sectors().concealed_records_) {
        ids.push_back(rec.id);
    }
    return ids;
}

} // namespace frontend::level_editor::view
