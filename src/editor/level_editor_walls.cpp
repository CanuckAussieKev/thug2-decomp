// NOT ORIGINAL - the level editor's collision switches and invisible walls (ours). See level_editor_walls.h.

#include "editor/level_editor_walls.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_scenery.h"
#include "engine/render/coll_static.h"
#include "engine/gfx/sector.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "platform/run_log.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/geom.h"
#include "engine/core/format.h"

namespace frontend::level_editor::walls {

namespace {

constexpr float kSixteenth = 16.0F;          // wall positions snap to sixteenths
constexpr std::size_t kStatusTextSize = 192; // the collision status line

} // namespace

namespace {

constexpr float kHalf = 0.5F; // half

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

float to_sixteenth(float value) { return std::round(value * kSixteenth) / kSixteenth; }

V3 on_grid(V3 v) { return {.x = to_sixteenth(v.x), .y = to_sixteenth(v.y), .z = to_sixteenth(v.z)}; }

Step step_between(const Placement& from, const Placement& to)
{
    // Apply(to, x) = Q(to.q)(x) + to.o = Q(d)(Q(from.q)(x)) + to.o = Q(d)(Apply(from, x) - from.o) + from.o + (to.o -
    // from.o): turned about from.o, then moved.
    Step step{};
    step.turns = (((to.quarters - from.quarters) % 4) + 4) % 4;
    step.centre = from.offset;
    step.delta = to.offset - from.offset;
    return step;
}

void step_collision(render::TriMeshSource* tri, const Placement& from, const Placement& to)
{
    const Step step = step_between(from, to);
    if (step.turns) {
        const maths::Vec4 centre = {.x = step.centre.x, .y = step.centre.y, .z = step.centre.z, .w = 0.0F};
        tri->rotate_y(&centre, step.turns);
    }
    if (step.delta.x != 0.0F || step.delta.y != 0.0F || step.delta.z != 0.0F) {
        const maths::Vec4 delta = {.x = step.delta.x, .y = step.delta.y, .z = step.delta.z, .w = 0.0F};
        tri->get_slide(&delta);
    }
}

Placement moved_to(const Placement& placement, V3 pivot, V3 to)
{
    Placement moved = placement;
    moved.offset = on_grid(placement.offset + (to - pivot));
    moved.scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F};
    return moved;
}

Placement turned_about(const Placement& placement, V3 pivot, int quarters)
{
    Placement turned = placement;
    turn_about(&turned, on_grid(pivot), quarters);
    turned.offset = on_grid(turned.offset);
    turned.scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F};
    return turned;
}

bool is_home(const Placement& placement)
{
    return placement.quarters == 0 && placement.offset.x == 0.0F && placement.offset.y == 0.0F &&
           placement.offset.z == 0.0F;
}

int get_combined(const std::vector<bool>& offs)
{
    if (offs.empty())
        return -1;
    bool any_off = false;
    bool any_on = false;
    for (const bool off : offs)
        (off ? any_off : any_on) = true;
    return any_off && any_on ? 2 : any_off ? 0 : 1;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

render::PcGeom* geom_of(render::WorldCell* sector) { return static_cast<render::PcGeom*>(sector->geom_handle()); }

// The sector's level collision (StaticCollider) and its geometry, whether on or off.
render::HitMesh* coll_of(render::WorldCell* sector)
{
    render::HitMesh* coll = sector->hit_body();
    return render::is_static_collider(coll) ? coll : nullptr;
}
render::TriMeshSource* tri_of(render::HitMesh* coll) { return coll->geometry(); }
bool flag_off(render::HitMesh* coll) { return (coll->flag_bits() & 4) != 0; }

// WorldCell::change_live (0x0049E9B0: the collision's flag 4, the geometry's active flag when it has one).
void change_live(render::WorldCell* sector, bool active) { sector->change_live(active); }
// The geometry's is_operative (0x0048DE70, as WorldCell::is_operative 0x0049E9E0 asks it).
bool geom_active(render::PcGeom* geom) { return geom->is_operative(); }

bool same_v(const maths::Vec4& a, const maths::Vec4& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// What the editor has done to a sector's collision: where it put it (walls), whether it switched it off, how it was
// when first touched; the box after its last step (a wall's: anything else moving it - another level at the same
// addresses - starts afresh).
struct Track {
    render::WorldCell* sector;
    render::HitMesh* coll;
    render::TriMeshSource* tri;
    bool wall;
    Placement placement;
    bool off0, off;
    maths::Vec4 min, max;
};
std::map<uint32_t, Track>& tracks_state()
{
    static std::map<uint32_t, Track> value;
    return value;
}

// Whether a track is still of the sector that has that checksum now, as the editor left it.
bool at_hand(uint32_t checksum, const Track& t)
{
    render::WorldCell* sector = scenery::sector_of(checksum);
    render::HitMesh* coll = sector ? coll_of(sector) : nullptr;
    const render::TriMeshSource* tri = coll ? tri_of(coll) : nullptr;
    if (!tri)
        return false;
    const bool wall = geom_of(sector) == nullptr;
    // (a drawn piece's collision box moves with the piece - Scenery's moves: only its pointers checked)
    return t.sector == sector && t.coll == coll && t.tri == tri && t.wall == wall &&
           (!wall || (same_v(t.min, tri->bounds().min) && same_v(t.max, tri->bounds().max)));
}

Track* track_of(uint32_t checksum)
{
    render::WorldCell* sector = scenery::sector_of(checksum);
    render::HitMesh* coll = sector ? coll_of(sector) : nullptr;
    render::TriMeshSource* tri = coll ? tri_of(coll) : nullptr;
    if (!tri)
        return nullptr;
    const bool wall = geom_of(sector) == nullptr;
    auto it = tracks_state().find(checksum);
    if (it != tracks_state().end()) {
        if (at_hand(checksum, it->second))
            return &it->second;
        tracks_state().erase(it); // (another level, or moved by something else: started again from where it is)
    }
    Track t = {};
    t.sector = sector;
    t.coll = coll;
    t.tri = tri;
    t.wall = wall;
    t.placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
    // (a drawn piece hidden - not active - has its collision off with it: that isn't this switch)
    t.off0 = t.off = flag_off(coll) && (wall || geom_active(geom_of(sector)));
    t.min = tri->bounds().min;
    t.max = tri->bounds().max;
    return &tracks_state().emplace(checksum, t).first->second;
}

// The collision's flag as the track wants it.
void put_switch(Track* t)
{
    if (t->wall) {
        if (flag_off(t->coll) != t->off)
            change_live(t->sector, !t->off);
        return;
    }
    render::PcGeom* geom = geom_of(t->sector);
    if (!geom || !geom_active(geom))
        return; // (hidden: off with the piece - shown again, Tick puts the switch back)
    if (t->off)
        t->coll->flag_bits() |= 4;
    else if (t->off != t->off0 || flag_off(t->coll))
        t->coll->flag_bits() &= static_cast<uint16_t>(~4);
}

void get_place(uint32_t checksum, Track* t, const Placement& to)
{
    if (!t->wall)
        return;
    Placement target = to;
    target.offset = on_grid(target.offset);
    target.scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F};
    step_collision(t->tri, t->placement, target);
    t->placement = target;
    t->min = t->tri->bounds().min;
    t->max = t->tri->bounds().max;
    scenery::note_collision_moved(checksum);
}

} // namespace

bool is_wall(uint32_t checksum)
{
    render::WorldCell* sector = scenery::sector_of(checksum);
    render::HitMesh* coll = sector ? coll_of(sector) : nullptr;
    return coll && tri_of(coll) && geom_of(sector) == nullptr;
}

bool cuboid(uint32_t checksum, V3* min, V3* max)
{
    render::WorldCell* sector = scenery::sector_of(checksum);
    render::HitMesh* coll = sector ? coll_of(sector) : nullptr;
    const render::TriMeshSource* tri = coll ? tri_of(coll) : nullptr;
    if (!tri)
        return false;
    *min = {.x = tri->bounds().min.x, .y = tri->bounds().min.y, .z = tri->bounds().min.z};
    *max = {.x = tri->bounds().max.x, .y = tri->bounds().max.y, .z = tri->bounds().max.z};
    return true;
}

V3 get_pivot(uint32_t checksum)
{
    V3 min = {};
    V3 max = {};
    cuboid(checksum, &min, &max);
    return (min + max) * kHalf;
}

float get_bottom(uint32_t checksum)
{
    V3 min = {};
    V3 max = {};
    cuboid(checksum, &min, &max);
    return min.y;
}

bool move_to(uint32_t checksum, V3 pivot)
{
    Track* t = track_of(checksum);
    if (!t || !t->wall)
        return false;
    get_place(checksum, t, moved_to(t->placement, get_pivot(checksum), pivot));
    return true;
}

bool get_turn(uint32_t checksum, int quarters)
{
    Track* t = track_of(checksum);
    if (!t || !t->wall)
        return false;
    get_place(checksum, t, turned_about(t->placement, get_pivot(checksum), quarters));
    return true;
}

bool set_off(uint32_t checksum, bool off)
{
    Track* t = track_of(checksum);
    if (!t)
        return false;
    t->off = off;
    put_switch(t);
    if (debug_enabled())
        get_log("level editor: collision of %08X switched %s (%s)", checksum, off ? "off" : "on",
                t->wall ? "an invisible wall" : "a drawn piece");
    return true;
}

bool disengaged(uint32_t checksum)
{
    auto it = tracks_state().find(checksum);
    if (it != tracks_state().end()) {
        const Track* t = track_of(checksum);
        if (t)
            return t->off;
    }
    render::WorldCell* sector = scenery::sector_of(checksum);
    render::HitMesh* coll = sector ? coll_of(sector) : nullptr;
    return coll && geom_of(sector) == nullptr && flag_off(coll);
}

bool get_moved(uint32_t checksum, Placement* placement)
{
    auto it = tracks_state().find(checksum);
    if (it == tracks_state().end() || !track_of(checksum))
        return false;
    *placement = it->second.placement;
    return !is_home(*placement);
}

void finalise() { scenery::finalise(); }

std::vector<WallState> store_away(const std::vector<uint32_t>& checksums)
{
    std::vector<WallState> states;
    for (const uint32_t checksum : checksums) {
        if (const Track* t = track_of(checksum))
            states.push_back({.checksum = checksum, .placement = t->placement, .off = t->off});
    }
    return states;
}

void apply_states(const std::vector<WallState>& states)
{
    for (const WallState& state : states) {
        Track* t = track_of(state.checksum);
        if (!t)
            continue;
        get_place(state.checksum, t, state.placement);
        t->off = state.off;
        put_switch(t);
    }
    scenery::finalise();
}

std::vector<WallState> get_changed()
{
    std::vector<WallState> changed;
    for (const auto& entry : tracks_state()) {
        const Track& t = entry.second;
        if (scenery::is_clone(entry.first) || !at_hand(entry.first, t))
            continue;
        if (!is_home(t.placement) || t.off != t.off0)
            changed.push_back({.checksum = entry.first,
                               .placement = t.wall
                                                ? t.placement
                                                : Placement{.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}},
                               .off = t.off});
    }
    return changed;
}

void reset_all()
{
    // (tracks of another level - the sector gone or not the one tracked - are forgotten, not applied)
    std::vector<WallState> home;
    for (auto it = tracks_state().begin(); it != tracks_state().end();) {
        if (!at_hand(it->first, it->second)) {
            it = tracks_state().erase(it);
            continue;
        }
        home.push_back({.checksum = it->first,
                        .placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}},
                        .off = it->second.off0});
        ++it;
    }
    apply_states(home);
}

void tick()
{
    for (auto& entry : tracks_state()) {
        const Track& t = entry.second;
        if (t.wall || !t.off || !at_hand(entry.first, t))
            continue;
        render::PcGeom* geom = geom_of(t.sector);
        if (geom && geom_active(geom) && !flag_off(t.coll))
            t.coll->flag_bits() |= 4;
    }
}

std::string describe_state(uint32_t checksum)
{
    const bool off = disengaged(checksum);
    Placement placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
    const bool moved = get_moved(checksum, &placement);
    std::array<char, kStatusTextSize> text{};
    if (moved)
        text::format_to(text, "Collision %s - moved by (%.1f, %.1f, %.1f), %d quarter turn%s", off ? "OFF" : "on",
                        placement.offset.x, placement.offset.y, placement.offset.z, placement.quarters,
                        placement.quarters == 1 ? "" : "s");
    else
        text::format_to(text, "Collision %s - where the level has it", off ? "OFF" : "on");
    return text.data();
}

} // namespace frontend::level_editor::walls
