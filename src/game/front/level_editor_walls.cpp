// NOT ORIGINAL - the level editor's collision switches and invisible walls (ours). See level_editor_walls.h.

#include "game/front/level_editor_walls.h"

#include <cmath>
#include <cstdio>
#include <map>

#include "game/front/level_editor_scenery.h"
#include "game/mth/vector.h"
#include "game/nx/coll_tri.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor::Walls {

// --- the pieces -------------------------------------------------------------------------------------------------

float ToSixteenth(float value) { return std::round(value * 16.0f) / 16.0f; }

V3 OnGrid(V3 v) { return {ToSixteenth(v.x), ToSixteenth(v.y), ToSixteenth(v.z)}; }

Step StepBetween(const Placement& from, const Placement& to)
{
    // Apply(to, x) = Q(to.q)(x) + to.o = Q(d)(Q(from.q)(x)) + to.o = Q(d)(Apply(from, x) - from.o) + from.o + (to.o -
    // from.o): turned about from.o, then moved.
    Step step;
    step.turns = ((to.quarters - from.quarters) % 4 + 4) % 4;
    step.centre = from.offset;
    step.delta = to.offset - from.offset;
    return step;
}

void StepCollision(Nx::CCollObjTriData* tri, const Placement& from, const Placement& to)
{
    Step step = StepBetween(from, to);
    if (step.turns) {
        Mth::Vector centre = {step.centre.x, step.centre.y, step.centre.z, 0.0f};
        tri->RotateY(&centre, step.turns);
    }
    if (step.delta.x != 0.0f || step.delta.y != 0.0f || step.delta.z != 0.0f) {
        Mth::Vector delta = {step.delta.x, step.delta.y, step.delta.z, 0.0f};
        tri->Translate(&delta);
    }
}

Placement MovedTo(const Placement& placement, V3 pivot, V3 to)
{
    Placement moved = placement;
    moved.offset = OnGrid(placement.offset + (to - pivot));
    moved.scale = {1.0f, 1.0f, 1.0f};
    return moved;
}

Placement TurnedAbout(const Placement& placement, V3 pivot, int quarters)
{
    Placement turned = placement;
    TurnAbout(&turned, OnGrid(pivot), quarters);
    turned.offset = OnGrid(turned.offset);
    turned.scale = {1.0f, 1.0f, 1.0f};
    return turned;
}

bool IsHome(const Placement& placement)
{
    return placement.quarters == 0 && placement.offset.x == 0.0f && placement.offset.y == 0.0f &&
           placement.offset.z == 0.0f;
}

int Combined(const std::vector<bool>& offs)
{
    if (offs.empty())
        return -1;
    bool anyOff = false, anyOn = false;
    for (bool off : offs)
        (off ? anyOff : anyOn) = true;
    return anyOff && anyOn ? 2 : anyOff ? 0 : 1;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kCollStaticVtable = 0x00645C18;

uint8_t* GeomOf(uint8_t* sector) { return *reinterpret_cast<uint8_t**>(sector + 0x14); }

// The sector's level collision (CCollStatic) and its geometry, whether on or off.
uint8_t* CollOf(uint8_t* sector)
{
    uint8_t* coll = *reinterpret_cast<uint8_t**>(sector + 0x18);
    return coll && *reinterpret_cast<uint32_t*>(coll) == kCollStaticVtable ? coll : nullptr;
}
Nx::CCollObjTriData* TriOf(uint8_t* coll) { return *reinterpret_cast<Nx::CCollObjTriData**>(coll + 0x0C); }
bool FlagOff(uint8_t* coll) { return (coll[0x08] & 4) != 0; }

// CSector::SetActive (0x0049E9B0: the collision's flag 4, the geometry's active flag when it has one).
void SetActive(uint8_t* sector, bool active)
{
    GameFunction<void(__thiscall*)(void*, bool)>(0x0049E9B0)(sector, active);
}
// The geometry's IsActive (0x0048DE70, as CSector::IsActive 0x0049E9E0 asks it).
bool GeomActive(uint8_t* geom) { return GameFunction<bool(__thiscall*)(void*)>(0x0048DE70)(geom); }

bool SameV(const Mth::Vector& a, const Mth::Vector& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// What the editor has done to a sector's collision: where it put it (walls), whether it switched it off, how it was
// when first touched; the box after its last step (a wall's: anything else moving it - another level at the same
// addresses - starts afresh).
struct Track {
    uint8_t* sector;
    uint8_t* coll;
    Nx::CCollObjTriData* tri;
    bool wall;
    Placement placement;
    bool off0, off;
    Mth::Vector min, max;
};
std::map<uint32_t, Track> g_tracks;

// Whether a track is still of the sector that has that checksum now, as the editor left it.
bool Current(uint32_t checksum, const Track& t)
{
    uint8_t* sector = Scenery::SectorOf(checksum);
    uint8_t* coll = sector ? CollOf(sector) : nullptr;
    Nx::CCollObjTriData* tri = coll ? TriOf(coll) : nullptr;
    if (!tri)
        return false;
    bool wall = GeomOf(sector) == nullptr;
    // (a drawn piece's collision box moves with the piece - Scenery's moves: only its pointers checked)
    return t.sector == sector && t.coll == coll && t.tri == tri && t.wall == wall &&
           (!wall || (SameV(t.min, tri->m_min) && SameV(t.max, tri->m_max)));
}

Track* TrackOf(uint32_t checksum)
{
    uint8_t* sector = Scenery::SectorOf(checksum);
    uint8_t* coll = sector ? CollOf(sector) : nullptr;
    Nx::CCollObjTriData* tri = coll ? TriOf(coll) : nullptr;
    if (!tri)
        return nullptr;
    bool wall = GeomOf(sector) == nullptr;
    auto it = g_tracks.find(checksum);
    if (it != g_tracks.end()) {
        if (Current(checksum, it->second))
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
    t.off0 = t.off = FlagOff(coll) && (wall || GeomActive(GeomOf(sector)));
    t.min = tri->m_min;
    t.max = tri->m_max;
    return &g_tracks.emplace(checksum, t).first->second;
}

// The collision's flag as the track wants it.
void PutSwitch(Track* t)
{
    if (t->wall) {
        if (FlagOff(t->coll) != t->off)
            SetActive(t->sector, !t->off);
        return;
    }
    uint8_t* geom = GeomOf(t->sector);
    if (!geom || !GeomActive(geom))
        return; // (hidden: off with the piece - shown again, Tick puts the switch back)
    if (t->off)
        t->coll[0x08] |= 4;
    else if (t->off != t->off0 || FlagOff(t->coll))
        t->coll[0x08] &= static_cast<uint8_t>(~4);
}

void Place(uint32_t checksum, Track* t, const Placement& to)
{
    if (!t->wall)
        return;
    Placement target = to;
    target.offset = OnGrid(target.offset);
    target.scale = {1.0f, 1.0f, 1.0f};
    StepCollision(t->tri, t->placement, target);
    t->placement = target;
    t->min = t->tri->m_min;
    t->max = t->tri->m_max;
    Scenery::NoteCollisionMoved(checksum);
}

} // namespace

bool IsWall(uint32_t checksum)
{
    uint8_t* sector = Scenery::SectorOf(checksum);
    uint8_t* coll = sector ? CollOf(sector) : nullptr;
    return coll && TriOf(coll) && GeomOf(sector) == nullptr;
}

bool Box(uint32_t checksum, V3* min, V3* max)
{
    uint8_t* sector = Scenery::SectorOf(checksum);
    uint8_t* coll = sector ? CollOf(sector) : nullptr;
    Nx::CCollObjTriData* tri = coll ? TriOf(coll) : nullptr;
    if (!tri)
        return false;
    *min = {tri->m_min.x, tri->m_min.y, tri->m_min.z};
    *max = {tri->m_max.x, tri->m_max.y, tri->m_max.z};
    return true;
}

V3 Pivot(uint32_t checksum)
{
    V3 min = {}, max = {};
    Box(checksum, &min, &max);
    return (min + max) * 0.5f;
}

float Bottom(uint32_t checksum)
{
    V3 min = {}, max = {};
    Box(checksum, &min, &max);
    return min.y;
}

bool MoveTo(uint32_t checksum, V3 pivot)
{
    Track* t = TrackOf(checksum);
    if (!t || !t->wall)
        return false;
    Place(checksum, t, MovedTo(t->placement, Pivot(checksum), pivot));
    return true;
}

bool Turn(uint32_t checksum, int quarters)
{
    Track* t = TrackOf(checksum);
    if (!t || !t->wall)
        return false;
    Place(checksum, t, TurnedAbout(t->placement, Pivot(checksum), quarters));
    return true;
}

bool SetOff(uint32_t checksum, bool off)
{
    Track* t = TrackOf(checksum);
    if (!t)
        return false;
    t->off = off;
    PutSwitch(t);
    if (DebugEnabled())
        Log("level editor: collision of %08X switched %s (%s)", checksum, off ? "off" : "on",
            t->wall ? "an invisible wall" : "a drawn piece");
    return true;
}

bool Off(uint32_t checksum)
{
    auto it = g_tracks.find(checksum);
    if (it != g_tracks.end()) {
        Track* t = TrackOf(checksum);
        if (t)
            return t->off;
    }
    uint8_t* sector = Scenery::SectorOf(checksum);
    uint8_t* coll = sector ? CollOf(sector) : nullptr;
    return coll && GeomOf(sector) == nullptr && FlagOff(coll);
}

bool Moved(uint32_t checksum, Placement* placement)
{
    auto it = g_tracks.find(checksum);
    if (it == g_tracks.end() || !TrackOf(checksum))
        return false;
    *placement = it->second.placement;
    return !IsHome(*placement);
}

void Commit() { Scenery::Commit(); }

std::vector<WallState> Save(const std::vector<uint32_t>& checksums)
{
    std::vector<WallState> states;
    for (uint32_t checksum : checksums) {
        if (Track* t = TrackOf(checksum))
            states.push_back({checksum, t->placement, t->off});
    }
    return states;
}

void ApplyStates(const std::vector<WallState>& states)
{
    for (const WallState& state : states) {
        Track* t = TrackOf(state.checksum);
        if (!t)
            continue;
        Place(state.checksum, t, state.placement);
        t->off = state.off;
        PutSwitch(t);
    }
    Scenery::Commit();
}

std::vector<WallState> Changed()
{
    std::vector<WallState> changed;
    for (const auto& entry : g_tracks) {
        const Track& t = entry.second;
        if (Scenery::IsClone(entry.first) || !Current(entry.first, t))
            continue;
        if (!IsHome(t.placement) || t.off != t.off0)
            changed.push_back({entry.first, t.wall ? t.placement : Placement{0, {0.0f, 0.0f, 0.0f}}, t.off});
    }
    return changed;
}

void ResetAll()
{
    // (tracks of another level - the sector gone or not the one tracked - are forgotten, not applied)
    std::vector<WallState> home;
    for (auto it = g_tracks.begin(); it != g_tracks.end();) {
        if (!Current(it->first, it->second)) {
            it = g_tracks.erase(it);
            continue;
        }
        home.push_back({it->first, {0, {0.0f, 0.0f, 0.0f}}, it->second.off0});
        ++it;
    }
    ApplyStates(home);
}

void Tick()
{
    for (auto& entry : g_tracks) {
        Track& t = entry.second;
        if (t.wall || !t.off || !Current(entry.first, t))
            continue;
        uint8_t* geom = GeomOf(t.sector);
        if (geom && GeomActive(geom) && !FlagOff(t.coll))
            t.coll[0x08] |= 4;
    }
}

std::string DescribeState(uint32_t checksum)
{
    bool off = Off(checksum);
    Placement placement = {0, {0.0f, 0.0f, 0.0f}};
    bool moved = Moved(checksum, &placement);
    char text[192];
    if (moved)
        std::snprintf(text, sizeof(text), "Collision %s - moved by (%.1f, %.1f, %.1f), %d quarter turn%s", off ? "OFF" : "on",
                      placement.offset.x, placement.offset.y, placement.offset.z, placement.quarters,
                      placement.quarters == 1 ? "" : "s");
    else
        std::snprintf(text, sizeof(text), "Collision %s - where the level has it", off ? "OFF" : "on");
    return text;
}

} // namespace Front::LevelEditor::Walls
