// Self-tests for [debug] collision_events (game/skater/collision_events.cpp - not original): its hooks change nothing
// in the skater's component, and the log line's format (the same line tools/pipeline/game/collision_events.test.ts parses).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "game/mth/vector.h"
#include "game/skater/collision_events.h"
#include "hook/selftest.h"

namespace {

using namespace Skater::CollisionEvents;

std::string TestName(uint32_t checksum)
{
    if (checksum == 0x12345678)
        return "Z_Houses_Road01";
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", checksum);
    return text;
}

// The hooks on a stand-in component (no game running - or, in the game, not the local skater's): nothing of it, its
// object or its flags is written.
void TestCollisionEventHooksChangeNothing()
{
    alignas(16) static uint8_t component[0x1600], object[0x200], flags[0x200];
    std::memset(component, 0x5A, sizeof(component));
    std::memset(object, 0x3C, sizeof(object));
    std::memset(flags, 0, sizeof(flags));
    *reinterpret_cast<void**>(component + 0x0C) = object;
    *reinterpret_cast<void**>(component + 0x1528) = flags;
    static uint8_t before[3][0x1600];
    std::memcpy(before[0], component, sizeof(component));
    std::memcpy(before[1], object, sizeof(object));
    std::memcpy(before[2], flags, sizeof(flags));
    Mth::Vector velocity = {100.0f, 0.0f, 0.0f, 0.0f};
    for (int path = 0; path < static_cast<int>(Path::Count); ++path)
        NotePath(component, static_cast<Path>(path), velocity);
    for (int frame = 0; frame < 3; ++frame)
        EndFrame(component);
    Check(std::memcmp(before[0], component, sizeof(component)) == 0 &&
              std::memcmp(before[1], object, sizeof(object)) == 0 && std::memcmp(before[2], flags, sizeof(flags)) == 0,
          "collision events: the hooks leave the component, its object and flags as they were");
}
SELFTEST(Early, TestCollisionEventHooksChangeNothing);

void TestCollisionEventFormat()
{
    Check(FlagNames(0) == "0" && FlagNames(0x1040) == "trigger|invisible" && FlagNames(0x0005) == "skatable|wall_ridable",
          "collision events: face flag names");

    Event e;
    e.number = 3;
    e.level = "thp8_net_beavervalley";
    e.frame = 21080;
    e.reason = "wall-ahead";
    e.paths = "wall-ahead,side-wall";
    e.frames = 4;
    const float spot[3] = {-6560.0f, 10.5f, 3452.5f}, posBefore[3] = {-6605.2f, 7.0f, 3452.5f},
                pos[3] = {-6590.0f, 7.0f, 3452.5f}, velBefore[3] = {300.0f, 0.0f, 1.2f},
                velAfter[3] = {-150.0f, 0.0f, -0.6f}, ground[3] = {0.0f, 1.0f, 0.0f};
    std::memcpy(e.spot, spot, sizeof(spot));
    std::memcpy(e.posBefore, posBefore, sizeof(spot));
    std::memcpy(e.pos, pos, sizeof(spot));
    std::memcpy(e.velBefore, velBefore, sizeof(spot));
    std::memcpy(e.velAfter, velAfter, sizeof(spot));
    std::memcpy(e.groundNormal, ground, sizeof(spot));
    e.headingBefore = 89.6f;
    e.heading = -90.4f;
    e.groundMinY = 0.98f;
    e.face.hit = true;
    e.face.checksum = 0x12345678;
    e.face.index = 17;
    e.face.flags = 0x1040;
    e.face.terrain = 6;
    const float n[3] = {-0.71f, 0.71f, 0.0f};
    std::memcpy(e.face.normal, n, sizeof(n));
    std::memcpy(e.face.point, spot, sizeof(spot));
    e.probes[1] = e.face;
    e.probes[2].hit = true;
    e.probes[2].object = true;
    e.probes[2].checksum = 0xABCDEF01;
    e.probes[2].index = 0;
    e.probes[2].flags = 0x0001;
    e.flipped = "ground-check>off-ground@21080";
    e.probeDir[0] = 1.0f;
    e.repeats = 2;
    e.screenshot = "requested";
    std::string line = FormatEvent(e, &TestName);
    const char* expected =
        "collision-event: n=3 level=thp8_net_beavervalley frame=21080 reason=wall-ahead paths=wall-ahead,side-wall "
        "frames=4 spot=-6560.0,10.5,3452.5 pos_before=-6605.2,7.0,3452.5 pos=-6590.0,7.0,3452.5 "
        "vel_before=300.0,0.0,1.2 vel_after=-150.0,0.0,-0.6 heading_before=89.6 heading=-90.4 ground_n=0.00,1.00,0.00 "
        "ground_min_y=0.98 "
        "face=sector:Z_Houses_Road01;face:17;flags:0x1040;decoded:trigger|invisible;terrain:6;n:-0.71,0.71,0.00;"
        "at:-6560.0,10.5,3452.5 "
        "probe_down_hit=none "
        "probe_down_hit_all=sector:Z_Houses_Road01;face:17;flags:0x1040;decoded:trigger|invisible;terrain:6;"
        "n:-0.71,0.71,0.00;at:-6560.0,10.5,3452.5 "
        "probe_down_beyond=object:0xABCDEF01;face:0;flags:0x0001;decoded:skatable;terrain:0;n:0.00,0.00,0.00;"
        "at:0.0,0.0,0.0 "
        "probe_down_beyond_all=none probe_ahead=none probe_ahead_all=none "
        "probe_dir=1.00,0.00,0.00 flipped=ground-check>off-ground@21080 repeats=2 screenshot=requested";
    Check(line == expected, "collision events: the log line's format");
}
SELFTEST(Early, TestCollisionEventFormat);
void TestCollisionEventDetailFormat()
{
    Detail d;
    d.number = 3;
    d.state = "ground";
    d.physics = "skating";
    d.speed = 326.4f;
    d.balance = "manual";
    d.input = ButtonNames(0x0001);
    d.inputWindow = ButtonNames(0x2003);
    d.stick[1] = -1.0f;
    d.ray = true;
    const float start[3] = {-6519.8f, 7.1f, 3453.1f}, end[3] = {-6519.8f, -1193.0f, 3453.1f};
    std::memcpy(d.rayStart, start, sizeof(start));
    std::memcpy(d.rayEnd, end, sizeof(end));
    d.geometry = true;
    const float corners[3][3] = {{-6530.0f, 7.0f, 3440.0f}, {-6513.0f, 7.0f, 3440.0f}, {-6513.0f, 7.0f, 3460.0f}};
    std::memcpy(d.corners, corners, sizeof(corners));
    const float lo[3] = {-6600.0f, 7.0f, 3300.0f}, hi[3] = {-6513.0f, 7.0f, 3640.0f};
    std::memcpy(d.boxMin, lo, sizeof(lo));
    std::memcpy(d.boxMax, hi, sizeof(hi));
    d.edge = 1;
    d.edgeDistance = 6.8f;
    d.edgeOpen = 1;
    d.colFile = "4140376@2026-10-24T12:58:51";
    d.approach = "-6599,7,3453,g;-6520,7,3453,g";
    Check(FormatDetail(d) ==
              "collision-event-detail: n=3 state=ground physics=skating speed=326.4 trick=0 balance=manual input=up "
              "input_window=up+down+x stick=0.00,-1.00 ray=-6519.8,7.1,3453.1>-6519.8,-1193.0,3453.1 "
              "corners=-6530.0,7.0,3440.0;-6513.0,7.0,3440.0;-6513.0,7.0,3460.0 "
              "col_box=-6600.0,7.0,3300.0>-6513.0,7.0,3640.0 edge=1 edge_dist=6.8 open_edge=1 "
              "col_file=4140376@2026-10-24T12:58:51 scn_file=- approach=-6599,7,3453,g;-6520,7,3453,g camera=-",
          "collision events: the detail line's format");
}
SELFTEST(Early, TestCollisionEventDetailFormat);

// The reversal and its reason: a skater braking to a stop and pushing back on flat ground is found as a reversal,
// but with no path and the ground flat it isn't logged; on a 10-degree slope it's "slope"; a smooth 180-degree turn
// (6 degrees a frame) isn't a reversal at all.
void TestCollisionEventReversal()
{
    Motion brake[40];
    for (int i = 0; i < 40; ++i) {
        brake[i].frame = 100u + static_cast<uint32_t>(i);
        brake[i].vel[0] = 300.0f - 20.0f * static_cast<float>(i); // (through zero at frame 15)
    }
    int found = FindReversal(brake, 40);
    Check(found == 0, "collision events: braking and pushing back is a reversal");
    Check(ChooseReason(false, nullptr, 1.0f) == nullptr, "collision events: on flat ground, no path - not logged");
    Check(ChooseReason(false, nullptr, 0.985f) != nullptr &&
              std::strcmp(ChooseReason(false, nullptr, 0.985f), "slope") == 0,
          "collision events: on a 10-degree slope - slope");
    Check(std::strcmp(ChooseReason(true, "no-ground", 1.0f), "uber-frig") == 0 &&
              std::strcmp(ChooseReason(false, "wall-ahead", 1.0f), "wall-ahead") == 0,
          "collision events: the uber frig first, then the path");
    Motion turn[40];
    for (int i = 0; i < 40; ++i) {
        float angle = 0.10472f * static_cast<float>(i); // (6 degrees a frame)
        turn[i].frame = 200u + static_cast<uint32_t>(i);
        turn[i].vel[0] = 300.0f * std::cos(angle);
        turn[i].vel[2] = 300.0f * std::sin(angle);
    }
    Check(FindReversal(turn, 40) < 0, "collision events: a smooth turn isn't a reversal");
    Check(FindReversal(brake, 1) < 0, "collision events: one frame isn't a reversal");
}
SELFTEST(Early, TestCollisionEventReversal);

} // namespace
