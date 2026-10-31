// NOT ORIGINAL - [features] debug_collision (thug2-decomp.ini, default 0): logs what collision feelers hit, to find
// invisible walls in converted levels. Our own debugging aid, not in the original game.
//
//   debug_collision=1  hits of feelers asked by the skater's code (callers in 0x004F8000-0x00534000) that aren't
//                      floors (face normal's y <= 0.7) - walls, the things that stop the skater
//   debug_collision=2  every hit of every feeler (cameras, peds, floors too)
//
// Each line: "collision: <what> face <i> flags <f> terrain <t> at (x y z) normal (x y z) line (a) -> (b) [skater (p)]
// caller <address>". <what> is "sector <name/checksum> [obj flags <o>]" for level geometry, "object <name/id>" for a
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
//   are skipped), +0x2C the face's flags, +0x2E its terrain (byte), +0x30 the collision object (CCollObj) hit,
//   +0x34 that object's checksum (CCollObj +0x04: the sector's name), +0x3C a callback (called with the CollData on
//   each accepted face), +0x48 hit, +0x49 the face is a trigger (flags & 0x40).
// - 0x004172F0 GetCollision(movables, reverse): the static query 0x004106C0 (0x00410930 for reverse: farthest
//   first), then with `movables` the composite objects' (0x004118C0 / 0x00411AE0); true if either hit.
// - Every accepted face goes through 0x004131F0: the face's flags come from the collision object's vtable +0x08
//   (CCollStatic, vtables 0x00645C18 / 0x00645C78: 0x00410BF0, straight from the face array of the .col data - the
//   u16 at face +0, faces of 8 bytes when the geometry's "small faces" byte (+0x0A) is set, else 10); the face
//   is skipped if (flags & ignore_1) || (~flags & ignore_0). A movable's collision object (vtable 0x00645BC0) has
//   one set of flags for all its faces (+0xC4, terrain +0xC8).
// - Ignore flags: both constructors set ignore_1 = 0x0010, ignore_0 = 0 - faces flagged 0x10 (non-collidable) are
//   skipped by every feeler that doesn't change them. SetIgnore (0x004170F0) is called by 19 places: the skater's
//   feeler check 0x005003A0 takes them from its callers, and all of those that can be read pass (0x10, 0); others:
//   (0x810, 0) 0x004F8698, (0x90, 0) 0x004E4425, (0x3000, 0) 0x0051DF71 / 0x0051E74B (0x10 NOT ignored there, but
//   0x1000 is), (0, 0x20) - only faces with 0x20 - 0x00422F51 / 0x004F8341 / 0x0052716D, (0, 0x50) 0x0043B4C4 /
//   0x00434E83 / 0x004E5405. Trigger faces (0x40) are not treated apart in the query: a feeler hits them unless
//   it ignores 0x10 and they have 0x10 (THUG2's pass-through triggers are 0x10|0x40...); the hit only notes
//   flags & 0x40 (+0x49).
// - Collision objects have flags of their own (CCollObj +0x08): 2 and 4 (4 = killed: Kill on a LevelGeometry
//   node, 0x005A32C0 -> the sector's 0x0049E9B0(false) sets it on the sector's collision object, sector +0x18).
//   The world query (no cache, 0x004188C0's grid lists) skips objects with 2 or 4. The collision cache
//   (0x0040ABA0 fills it from the same lists, so also without them) is NOT re-checked when queried
//   (0x004106C0's cache branch): a sector killed after the cache was filled stays collidable through that cache
//   until it is filled again - but its faces still go through the face-flag test above.
// - Shatter (0x005A5090) on a LevelGeometry node: the sector made invisible (0x005A3B80) and shattered (sector
//   vtable +0x04, 0x004A1090); it doesn't touch the collision. Kill on it: the killed flag above, plus 0x004D8C90.
//
// Nx::CFeeler::GetCollision (0x004172F0, src/game/nx/feeler.cpp) calls NoteFeelerQuery after each query; nothing
// else changes.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <windows.h>

#include "game/skater/debug_collision.h"
#include "game/script/names.h"
#include "game/script/qb.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/sys/log.h"

namespace DebugCollision {

namespace {

template <typename T>
T& At(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

int Mode()
{
    static int mode = ConfigInt("features", "debug_collision", 0);
    return mode;
}

const char* NameOf(uint32_t checksum, char* buffer, size_t size)
{
    const char* name = Script::KeptQbName(checksum);
    if (!name)
        name = Script::FindChecksumName(checksum);
    if (name)
        std::snprintf(buffer, size, "%s (%08X)", name, checksum);
    else
        std::snprintf(buffer, size, "%08X", checksum);
    return buffer;
}

// The skater core physics component (its feelers are at +0xA8 and +0x158, its object at +0x0C, the object's
// position at +0x4C), learned from the skater's feeler check 0x005003A0 (which asks with the feeler at +0xA8).
uintptr_t g_skaterComponent = 0;

bool SkaterPosition(const void* feeler, float* position)
{
    uintptr_t self = reinterpret_cast<uintptr_t>(feeler);
    if (!g_skaterComponent || (self != g_skaterComponent + 0xA8 && self != g_skaterComponent + 0x158))
        return false; // (only read while one of the component's own feelers is in use: it's alive)
    const uint8_t* object = At<const uint8_t*>(reinterpret_cast<void*>(g_skaterComponent), 0x0C);
    if (!object)
        return false;
    for (int i = 0; i < 3; ++i)
        position[i] = At<float>(object, 0x4C + 4 * i);
    return true;
}

struct Recent {
    uintptr_t what;
    int face;
    DWORD time;
};
Recent g_recent[32];
int g_nextRecent = 0;
DWORD g_secondStart = 0;
int g_linesThisSecond = 0;

bool ShouldLog(uintptr_t what, int face)
{
    DWORD now = GetTickCount();
    if (now - g_secondStart >= 1000) {
        g_secondStart = now;
        g_linesThisSecond = 0;
    }
    if (g_linesThisSecond >= 8)
        return false;
    for (Recent& recent : g_recent) {
        if (recent.what == what && recent.face == face) {
            if (now - recent.time < 2000)
                return false;
            recent.time = now;
            ++g_linesThisSecond;
            return true;
        }
    }
    g_recent[g_nextRecent] = {what, face, now};
    g_nextRecent = (g_nextRecent + 1) % (sizeof(g_recent) / sizeof(g_recent[0]));
    ++g_linesThisSecond;
    return true;
}

void LogHit(const void* feeler, uint32_t caller)
{
    const float* start = &At<float>(feeler, 0x00);
    const float* end = &At<float>(feeler, 0x10);
    const float* normal = &At<float>(feeler, 0x24);
    const float* point = &At<float>(feeler, 0x34);
    int face = At<int>(feeler, 0x44);
    uint16_t flags = At<uint16_t>(feeler, 0x50);
    uint8_t terrain = At<uint8_t>(feeler, 0x52);
    bool staticHit = At<uint8_t>(feeler, 0x6C) != 0;
    const uint8_t* collObject = At<const uint8_t*>(feeler, 0x54);
    uint32_t sector = At<uint32_t>(feeler, 0x58);
    void* object = At<void*>(feeler, 0x98);
    uint32_t objectId = At<uint32_t>(feeler, 0xA4);

    bool skaterCaller = caller >= 0x004F8000 && caller < 0x00534000;
    if (Mode() == 1 && (!skaterCaller || normal[1] > 0.7f))
        return;
    if (!ShouldLog(object ? reinterpret_cast<uintptr_t>(object) : reinterpret_cast<uintptr_t>(collObject), face))
        return;

    char what[160], name[96];
    if (object)
        std::snprintf(what, sizeof(what), "object %s", NameOf(objectId, name, sizeof(name)));
    else if (staticHit && collObject)
        std::snprintf(what, sizeof(what), "sector %s obj flags %04X", NameOf(sector, name, sizeof(name)),
                      At<uint16_t>(collObject, 0x08));
    else
        std::snprintf(what, sizeof(what), "(nothing recorded)");

    char skater[64] = "";
    float position[3];
    if (SkaterPosition(feeler, position))
        std::snprintf(skater, sizeof(skater), " skater (%.1f %.1f %.1f)", position[0], position[1], position[2]);

    Log("collision: %s face %d flags %04X terrain %u at (%.1f %.1f %.1f) normal (%.2f %.2f %.2f) line (%.1f %.1f "
        "%.1f) -> (%.1f %.1f %.1f)%s caller %08X",
        what, face, flags, terrain, point[0], point[1], point[2], normal[0], normal[1], normal[2], start[0], start[1],
        start[2], end[0], end[1], end[2], skater, caller);
}

} // namespace

// Called by Nx::CFeeler::GetCollision (0x004172F0) after each query.
void NoteFeelerQuery(const void* feeler, int hit, uint32_t caller)
{
    if (Mode() == 0)
        return;
    if (caller == 0x005003E0) // the skater's feeler check: this feeler is its core physics component's +0xA8
        g_skaterComponent = reinterpret_cast<uintptr_t>(feeler) - 0xA8;
    if (hit)
        LogHit(feeler, caller);
}

} // namespace DebugCollision
