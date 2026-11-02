// NOT ORIGINAL - [features] debug_collision (thug2-decomp.ini, default 0): logs what collision feelers hit, to find
// invisible walls in converted levels. Our own debugging aid, not in the original game.
//
//   debug_collision=1  hits of the skaters' own feelers (their core physics' wall and floor feelers, the walk's
//                      ledge feeler) that aren't floors (face normal's y <= 0.7) - walls, the things that stop them
//   debug_collision=2  every hit of every feeler (cameras, peds, floors too)
//
// Each line: "collision: <what> face <i> flags <f> terrain <t> at (x y z) normal (x y z) line (a) -> (b) [skater (p)]
// caller <address>" (the return address into the code that asked: symbolize it with the build's PDB). <what> is "sector <name/checksum> [obj flags <o>]" for level geometry, "object <name/id>" for a
// composite object (a movable collision: a level object, a car...). Each distinct hit (collision object / object
// and face) is logged at most once every 2 seconds, and at most 8 lines a second in all.
//
// How the engine's collision works (from the code, the reference build):
// - The feeler (Nx's line query object, 0xB0 bytes; constructor 0x00417760, also 0x00417250 with its line) holds
//   its line (+0x00 start, +0x10 end), a callback (+0x20), the collision data (CollData, +0x24 below) and, after a
//   query, a copy of the result (+0x74 distance, +0x78 point, +0x88 normal), the composite object hit (smart
//   pointer +0x98, its id +0xA4) and the collision cache to use (+0xAC; null: the current one, 0x00691974).
// - CollData (feeler +0x24): +0x00 normal, +0x10 point, +0x20 face index, +0x24 distance (0..1 along the line),
//   +0x28 ignore_1 (u16: faces with any of these flags are skipped), +0x2A ignore_0 (faces lacking any of these
//   are skipped), +0x2C the face's flags, +0x2E its terrain (byte), +0x30 the collision object (HitMesh) hit,
//   +0x34 that object's checksum (HitMesh +0x04: the sector's name), +0x3C a callback (called with the CollData on
//   each accepted face), +0x48 hit, +0x49 the face is a trigger (flags & 0x40).
// - 0x004172F0 get_hit_shape(movables, reverse): the static query 0x004106C0 (0x00410930 for reverse: farthest
//   first), then with `movables` the composite objects' (0x004118C0 / 0x00411AE0); true if either hit.
// - Every accepted face goes through 0x004131F0: the face's flags come from the collision object's vtable +0x08
//   (StaticCollider, vtables 0x00645C18 / 0x00645C78: 0x00410BF0, straight from the face array of the .col data - the
//   u16 at face +0, faces of 8 bytes when the geometry's "small faces" byte (+0x0A) is set, else 10); the face
//   is skipped if (flags & ignore_1) || (~flags & ignore_0). A movable's collision object (vtable 0x00645BC0) has
//   one set of flags for all its faces (+0xC4, terrain +0xC8).
// - Ignore flags: both constructors set ignore_1 = 0x0010, ignore_0 = 0 - faces flagged 0x10 (non-collidable) are
//   skipped by every feeler that doesn't change them. set_excluded (0x004170F0) is called by 19 places: the skater's
//   feeler check 0x005003A0 takes them from its callers, and all of those that can be read pass (0x10, 0); others:
//   (0x810, 0) 0x004F8698, (0x90, 0) 0x004E4425, (0x3000, 0) 0x0051DF71 / 0x0051E74B (0x10 NOT ignored there, but
//   0x1000 is), (0, 0x20) - only faces with 0x20 - 0x00422F51 / 0x004F8341 / 0x0052716D, (0, 0x50) 0x0043B4C4 /
//   0x00434E83 / 0x004E5405. Trigger faces (0x40) are not treated apart in the query: a feeler hits them unless
//   it ignores 0x10 and they have 0x10 (THUG2's pass-through triggers are 0x10|0x40...); the hit only notes
//   flags & 0x40 (+0x49).
// - Collision objects have flags of their own (HitMesh +0x08): 2 and 4 (4 = killed: Kill on a LevelGeometry
//   node, 0x005A32C0 -> the sector's 0x0049E9B0(false) sets it on the sector's collision object, sector +0x18).
//   The world query (no cache, 0x004188C0's grid lists) skips objects with 2 or 4. The collision cache
//   (0x0040ABA0 fills it from the same lists, so also without them) is NOT re-checked when queried
//   (0x004106C0's cache branch): a sector killed after the cache was filled stays collidable through that cache
//   until it is filled again - but its faces still go through the face-flag test above.
// - Shatter (0x005A5090) on a LevelGeometry node: the sector made invisible (0x005A3B80) and shattered (sector
//   vtable +0x04, 0x004A1090); it doesn't touch the collision. Kill on it: the killed flag above, plus 0x004D8C90.
//
// render::ProbeRay::get_hit_shape (0x004172F0, src/engine/render/probe_ray.cpp) calls note_feeler_query after each query; nothing
// else changes.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <windows.h>
#include <array>

#include "game/rider/debug_collision.h"
#include "engine/render/coll_obj.h"
#include "engine/render/probe_ray.h"
#include "game/rider/core_physics.h"
#include "game/rider/rider.h"
#include "game/rider/walk_physics.h"
#include "game/skate/skate_module.h"
#include "engine/script/names.h"
#include "engine/script/qb.h"
#include "platform/run_log.h"
#include "engine/core/checked_index.h"

namespace collision_overlay {

namespace {

int get_mode()
{
    static const int kMode = config_int("features", "debug_collision", 0);
    return kMode;
}

const char* get_name_of(uint32_t checksum, char* buffer, size_t size)
{
    const char* name = script::kept_qb_name(checksum);
    if (!name)
        name = script::look_up_hash_name(checksum);
    if (name)
        std::snprintf(buffer, size, "%s (%08X)", name, checksum);
    else
        std::snprintf(buffer, size, "%08X", checksum);
    return buffer;
}

// The skater whose own feeler `feeler` is (its core physics' wall or floor feeler, or its walk's ledge feeler), or
// null: the skate module's skaters looked through.
const objects::Rider* skater_of_feeler(const void* feeler)
{
    modules::Skate* skate = modules::Skate::sole();
    if (!skate)
        return nullptr;
    for (uint32_t i = 0; const objects::Rider* rider = skate->get_skater_at(i); ++i) {
        const skater::SkaterPhysicsModule* core = rider->core_physics_;
        if (core && (feeler == &core->feeler_ || feeler == &core->floor_feeler_))
            return rider;
        if (rider->walk_ && feeler == &rider->walk_->feeler_)
            return rider;
    }
    return nullptr;
}

bool get_skater_position(const objects::Rider* skater, float* position)
{
    if (!skater)
        return false;
    const objects::GameEntity* object = skater;
    position[0] = object->at_point_.x;
    position[1] = object->at_point_.y;
    position[2] = object->at_point_.z;
    return true;
}

struct Recent {
    const void* what;
    int face;
    DWORD time;
};
std::array<Recent, 32> g_recent;
int g_next_recent = 0;
DWORD g_second_start = 0;
int g_lines_this_second = 0;

bool should_log(const void* what, int face)
{
    const DWORD now = GetTickCount();
    if (now - g_second_start >= 1000) {
        g_second_start = now;
        g_lines_this_second = 0;
    }
    if (g_lines_this_second >= 8)
        return false;
    for (Recent& recent : g_recent) {
        if (recent.what == what && recent.face == face) {
            if (now - recent.time < 2000)
                return false;
            recent.time = now;
            ++g_lines_this_second;
            return true;
        }
    }
    core::at(g_recent, g_next_recent) = {what, face, now};
    g_next_recent = static_cast<int>((g_next_recent + 1) % (sizeof(g_recent) / sizeof(core::at(g_recent, 0))));
    ++g_lines_this_second;
    return true;
}

void log_hit(const void* feeler, uint32_t caller)
{
    const auto* ray = static_cast<const render::ProbeRay*>(feeler);
    const float* start = &ray->line_.start.x;
    const float* end = &ray->line_.end.x;
    const float* normal = &ray->hit_.normal.x;
    const float* point = &ray->hit_.point.x;
    const int face = static_cast<int>(ray->hit_.face);
    const uint16_t flags = ray->hit_.face_flags_;
    const uint8_t terrain = ray->hit_.terrain;
    const bool static_hit = ray->hit_.hit;
    const render::HitMesh* coll_object = ray->hit_.object;
    const uint32_t sector = ray->hit_.object_checksum_;
    const void* object = ray->entity_.get_pull();
    const uint32_t object_id = ray->entity_key_;

    const objects::Rider* skater_feeler = skater_of_feeler(feeler);
    if (get_mode() == 1 && (!skater_feeler || normal[1] > 0.7f))
        return;
    if (!should_log(object ? object : static_cast<const void*>(coll_object), face))
        return;

    std::array<char, 160> what{};
    std::array<char, 96> name{};
    if (object)
        std::snprintf(what.data(), sizeof(what), "object %s", get_name_of(object_id, name.data(), sizeof(name)));
    else if (static_hit && coll_object)
        std::snprintf(what.data(), sizeof(what), "sector %s obj flags %04X", get_name_of(sector, name.data(), sizeof(name)),
                      coll_object->flag_bits_);
    else
        std::snprintf(what.data(), sizeof(what), "(nothing recorded)");

    std::array<char, 64> skater = {""};
    std::array<float, 3> position{};
    if (get_skater_position(skater_feeler, position.data()))
        std::snprintf(skater.data(), sizeof(skater), " skater (%.1f %.1f %.1f)", core::at(position, 0), core::at(position, 1), core::at(position, 2));

    get_log("collision: %s face %d flags %04X terrain %u at (%.1f %.1f %.1f) normal (%.2f %.2f %.2f) line (%.1f %.1f "
            "%.1f) -> (%.1f %.1f %.1f)%s caller %08X",
            what.data(), face, flags, terrain, point[0], point[1], point[2], normal[0], normal[1], normal[2], start[0],
            start[1], start[2], end[0], end[1], end[2], skater.data(), caller);
}

} // namespace

// Called by render::ProbeRay::get_hit_shape (0x004172F0) after each query.
void note_feeler_query(const void* feeler, int hit, uint32_t caller)
{
    if (get_mode() == 0)
        return;
    if (hit)
        log_hit(feeler, caller);
}

} // namespace collision_overlay
