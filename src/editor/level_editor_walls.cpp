// NOT ORIGINAL - the level editor's collision switches and invisible walls (ours). See level_editor_walls.h.

#include "editor/level_editor_walls.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include "editor/level_editor_scenery.h"
#include "engine/gfx/sector.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "platform/run_log.h"
#include "engine/core/game_vtables.h"
#include "engine/gfx/engine_geom.h"

namespace frontend::level_editor::walls {

// --- the pieces -------------------------------------------------------------------------------------------------

float to_sixteenth(float value) { return std::round(value * 16.0f) / 16.0f; }

V3 on_grid(V3 v) { return {to_sixteenth(v.x), to_sixteenth(v.y), to_sixteenth(v.z)}; }

Step step_between(const Placement& from, const Placement& to)
{
    // Apply(to, x) = Q(to.q)(x) + to.o = Q(d)(Q(from.q)(x)) + to.o = Q(d)(Apply(from, x) - from.o) + from.o + (to.o -
    // from.o): turned about from.o, then moved.
    Step step;
    step.turns = ((to.quarters - from.quarters) % 4 + 4) % 4;
    step.centre = from.offset;
    step.delta = to.offset - from.offset;
    return step;
}

void step_collision(render::TriMeshSource* tri, const Placement& from, const Placement& to)
{
    Step step = step_between(from, to);
    if (step.turns) {
        maths::Vec4 centre = {step.centre.x, step.centre.y, step.centre.z, 0.0f};
        tri->rotate_y(&centre, step.turns);
    }
    if (step.delta.x != 0.0f || step.delta.y != 0.0f || step.delta.z != 0.0f) {
        maths::Vec4 delta = {step.delta.x, step.delta.y, step.delta.z, 0.0f};
        tri->get_slide(&delta);
    }
}

Placement moved_to(const Placement& placement, V3 pivot, V3 to)
{
    Placement moved = placement;
    moved.offset = on_grid(placement.offset + (to - pivot));
    moved.scale = {1.0f, 1.0f, 1.0f};
    return moved;
}

Placement turned_about(const Placement& placement, V3 pivot, int quarters)
{
    Placement turned = placement;
    turn_about(&turned, on_grid(pivot), quarters);
    turned.offset = on_grid(turned.offset);
    turned.scale = {1.0f, 1.0f, 1.0f};
    return turned;
}

bool is_home(const Placement& placement)
{
    return placement.quarters == 0 && placement.offset.x == 0.0f && placement.offset.y == 0.0f &&
           placement.offset.z == 0.0f;
}

int get_combined(const std::vector<bool>& offs)
{
    if (offs.empty())
        return -1;
    bool any_off = false, any_on = false;
    for (bool off : offs)
        (off ? any_off : any_on) = true;
    return any_off && any_on ? 2 : any_off ? 0 : 1;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

void** const& g_coll_static_vtable = vt::kVtblStaticColl;

uint8_t* geom_of(uint8_t* sector) { return *reinterpret_cast<uint8_t**>(sector + 0x14); }

// The sector's level collision (StaticCollider) and its geometry, whether on or off.
uint8_t* coll_of(uint8_t* sector)
{
    uint8_t* coll = *reinterpret_cast<uint8_t**>(sector + 0x18);
    return coll && *reinterpret_cast<void***>(coll) == g_coll_static_vtable ? coll : nullptr;
}
render::TriMeshSource* tri_of(uint8_t* coll) { return *reinterpret_cast<render::TriMeshSource**>(coll + 0x0C); }
bool flag_off(uint8_t* coll) { return (coll[0x08] & 4) != 0; }

// WorldCell::change_live (0x0049E9B0: the collision's flag 4, the geometry's active flag when it has one).
void change_live(uint8_t* sector, bool active) { reinterpret_cast<render::WorldCell*>(sector)->change_live(active); }
// The geometry's is_operative (0x0048DE70, as WorldCell::is_operative 0x0049E9E0 asks it).
bool geom_active(uint8_t* geom)
{
    return reinterpret_cast<bool(__thiscall*)(void*)>(&render::virtual_slot14_jump)(geom);
}

bool same_v(const maths::Vec4& a, const maths::Vec4& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// What the editor has done to a sector's collision: where it put it (walls), whether it switched it off, how it was
// when first touched; the box after its last step (a wall's: anything else moving it - another level at the same
// addresses - starts afresh).
struct Track {
    uint8_t* sector;
    uint8_t* coll;
    render::TriMeshSource* tri;
    bool wall;
    Placement placement;
    bool off0, off;
    maths::Vec4 min, max;
};
std::map<uint32_t, Track> g_tracks;

// Whether a track is still of the sector that has that checksum now, as the editor left it.
bool at_hand(uint32_t checksum, const Track& t)
{
    uint8_t* sector = scenery::sector_of(checksum);
    uint8_t* coll = sector ? coll_of(sector) : nullptr;
    render::TriMeshSource* tri = coll ? tri_of(coll) : nullptr;
    if (!tri)
        return false;
    bool wall = geom_of(sector) == nullptr;
    // (a drawn piece's collision box moves with the piece - Scenery's moves: only its pointers checked)
    return t.sector == sector && t.coll == coll && t.tri == tri && t.wall == wall &&
           (!wall || (same_v(t.min, tri->lower_limit_) && same_v(t.max, tri->upper_limit_)));
}

Track* track_of(uint32_t checksum)
{
    uint8_t* sector = scenery::sector_of(checksum);
    uint8_t* coll = sector ? coll_of(sector) : nullptr;
    render::TriMeshSource* tri = coll ? tri_of(coll) : nullptr;
    if (!tri)
        return nullptr;
    bool wall = geom_of(sector) == nullptr;
    auto it = g_tracks.find(checksum);
    if (it != g_tracks.end()) {
        if (at_hand(checksum, it->second))
            return &it->second;
        g_tracks.erase(it); // (another level, or moved by something else: started again from where it is)
    }
    Track t = {};
    t.sector = sector;
    t.coll = coll;
    t.tri = tri;
    t.wall = wall;
    t.placement = {0, {0.0f, 0.0f, 0.0f}};
    // (a drawn piece hidden - not active - has its collision off with it: that isn't this switch)
    t.off0 = t.off = flag_off(coll) && (wall || geom_active(geom_of(sector)));
    t.min = tri->lower_limit_;
    t.max = tri->upper_limit_;
    return &g_tracks.emplace(checksum, t).first->second;
}

// The collision's flag as the track wants it.
void put_switch(Track* t)
{
    if (t->wall) {
        if (flag_off(t->coll) != t->off)
            change_live(t->sector, !t->off);
        return;
    }
    uint8_t* geom = geom_of(t->sector);
    if (!geom || !geom_active(geom))
        return; // (hidden: off with the piece - shown again, Tick puts the switch back)
    if (t->off)
        t->coll[0x08] |= 4;
    else if (t->off != t->off0 || flag_off(t->coll))
        t->coll[0x08] &= static_cast<uint8_t>(~4);
}

void get_place(uint32_t checksum, Track* t, const Placement& to)
{
    if (!t->wall)
        return;
    Placement target = to;
    target.offset = on_grid(target.offset);
    target.scale = {1.0f, 1.0f, 1.0f};
    step_collision(t->tri, t->placement, target);
    t->placement = target;
    t->min = t->tri->lower_limit_;
    t->max = t->tri->upper_limit_;
    scenery::note_collision_moved(checksum);
}

} // namespace

bool is_wall(uint32_t checksum)
{
    uint8_t* sector = scenery::sector_of(checksum);
    uint8_t* coll = sector ? coll_of(sector) : nullptr;
    return coll && tri_of(coll) && geom_of(sector) == nullptr;
}

bool cuboid(uint32_t checksum, V3* min, V3* max)
{
    uint8_t* sector = scenery::sector_of(checksum);
    uint8_t* coll = sector ? coll_of(sector) : nullptr;
    render::TriMeshSource* tri = coll ? tri_of(coll) : nullptr;
    if (!tri)
        return false;
    *min = {tri->lower_limit_.x, tri->lower_limit_.y, tri->lower_limit_.z};
    *max = {tri->upper_limit_.x, tri->upper_limit_.y, tri->upper_limit_.z};
    return true;
}

V3 get_pivot(uint32_t checksum)
{
    V3 min = {}, max = {};
    cuboid(checksum, &min, &max);
    return (min + max) * 0.5f;
}

float get_bottom(uint32_t checksum)
{
    V3 min = {}, max = {};
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
    auto it = g_tracks.find(checksum);
    if (it != g_tracks.end()) {
        Track* t = track_of(checksum);
        if (t)
            return t->off;
    }
    uint8_t* sector = scenery::sector_of(checksum);
    uint8_t* coll = sector ? coll_of(sector) : nullptr;
    return coll && geom_of(sector) == nullptr && flag_off(coll);
}

bool get_moved(uint32_t checksum, Placement* placement)
{
    auto it = g_tracks.find(checksum);
    if (it == g_tracks.end() || !track_of(checksum))
        return false;
    *placement = it->second.placement;
    return !is_home(*placement);
}

void finalise() { scenery::finalise(); }

std::vector<WallState> store_away(const std::vector<uint32_t>& checksums)
{
    std::vector<WallState> states;
    for (uint32_t checksum : checksums) {
        if (Track* t = track_of(checksum))
            states.push_back({checksum, t->placement, t->off});
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
    for (const auto& entry : g_tracks) {
        const Track& t = entry.second;
        if (scenery::is_clone(entry.first) || !at_hand(entry.first, t))
            continue;
        if (!is_home(t.placement) || t.off != t.off0)
            changed.push_back({entry.first, t.wall ? t.placement : Placement{0, {0.0f, 0.0f, 0.0f}}, t.off});
    }
    return changed;
}

void reset_all()
{
    // (tracks of another level - the sector gone or not the one tracked - are forgotten, not applied)
    std::vector<WallState> home;
    for (auto it = g_tracks.begin(); it != g_tracks.end();) {
        if (!at_hand(it->first, it->second)) {
            it = g_tracks.erase(it);
            continue;
        }
        home.push_back({it->first, {0, {0.0f, 0.0f, 0.0f}}, it->second.off0});
        ++it;
    }
    apply_states(home);
}

void tick()
{
    for (auto& entry : g_tracks) {
        Track& t = entry.second;
        if (t.wall || !t.off || !at_hand(entry.first, t))
            continue;
        uint8_t* geom = geom_of(t.sector);
        if (geom && geom_active(geom) && !flag_off(t.coll))
            t.coll[0x08] |= 4;
    }
}

std::string describe_state(uint32_t checksum)
{
    bool off = disengaged(checksum);
    Placement placement = {0, {0.0f, 0.0f, 0.0f}};
    bool moved = get_moved(checksum, &placement);
    char text[192];
    if (moved)
        std::snprintf(text, sizeof(text), "Collision %s - moved by (%.1f, %.1f, %.1f), %d quarter turn%s",
                      off ? "OFF" : "on", placement.offset.x, placement.offset.y, placement.offset.z,
                      placement.quarters, placement.quarters == 1 ? "" : "s");
    else
        std::snprintf(text, sizeof(text), "Collision %s - where the level has it", off ? "OFF" : "on");
    return text;
}

} // namespace frontend::level_editor::walls
