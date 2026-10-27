// Self-tests for THAW's parked bikes (src/game/skater/thaw_bmx_parked.cpp - our own addition, no original to compare
// with): the walk's step and turn, headings, and where the level's bike may go near the start. Pure maths; no game.

#include <cmath>

#include "game/skater/thaw_bmx_parked.h"
#include "hook/selftest.h"

namespace {

using namespace Skater::ThawBmx;

bool Near(float a, float b, float within = 1e-3f) { return std::fabs(a - b) < within; }
bool Near(Point a, Point b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }

void TestThawBmxParkedWalk()
{
    Point out{};
    // 10 along x, 4 up: a step of 2 goes a fifth of the way, the height in proportion.
    Check(!StepToward({0, 0, 0}, {10, 4, 0}, 2.0f, &out) && Near(out, {2, 0.8f, 0}), "bmx parked: a step along the ground");
    // The height doesn't count towards the distance: 1.5 away along z, 50 up - there.
    Check(StepToward({0, 0, 0}, {0, 50, 1.5f}, 2.0f, &out) && Near(out, {0, 50, 1.5f}), "bmx parked: within a step: there");
    Check(StepToward({3, 1, 3}, {3, 1, 3}, 2.0f, &out) && Near(out, {3, 1, 3}), "bmx parked: already there");
    Check(!StepToward({0, 0, 0}, {-6, 0, 8}, 2.0f, &out) && Near(out, {-1.2f, 0, 1.6f}), "bmx parked: a diagonal step");

    float h = 0.0f;
    Check(!TurnToward(0.0f, 90.0f, 6.0f, &h) && Near(h, 6.0f), "bmx parked: turning right by the step");
    Check(!TurnToward(0.0f, -90.0f, 6.0f, &h) && Near(h, -6.0f), "bmx parked: turning left by the step");
    // The short way round: from 170 to -170 is 20 degrees to the right, past 180.
    Check(!TurnToward(170.0f, -170.0f, 6.0f, &h) && Near(h, 176.0f), "bmx parked: the short way round");
    Check(!TurnToward(178.0f, -170.0f, 6.0f, &h) && Near(h, -176.0f), "bmx parked: across 180, wrapped");
    Check(TurnToward(10.0f, 14.0f, 6.0f, &h) && Near(h, 14.0f), "bmx parked: within the step: there");

    Check(Near(HeadingOf(0, 1), 0.0f) && Near(HeadingOf(1, 0), 90.0f) && Near(HeadingOf(-1, 0), -90.0f) &&
              Near(std::fabs(HeadingOf(0, -1)), 180.0f) && HeadingOf(0, 0) == 0.0f,
          "bmx parked: headings (0 = +z, 90 = +x)");
}

void TestThawBmxParkedStartSpot()
{
    Point c[4];
    // Facing +z (heading 0): right is +x (up x at - the standing skater's right row), then left, behind, ahead.
    StartSpotCandidates({100, 20, 50}, 0.0f, 96.0f, c);
    Check(Near(c[0], {196, 20, 50}) && Near(c[1], {4, 20, 50}), "bmx parked: right, then left");
    Check(Near(c[2], {100, 20, -46}) && Near(c[3], {100, 20, 146}), "bmx parked: behind, then ahead");
    // Facing +x (heading 90): the sides along z.
    StartSpotCandidates({0, 0, 0}, 90.0f, 96.0f, c);
    Check(Near(c[0], {0, 0, -96}) && Near(c[1], {0, 0, 96}) && Near(c[2], {-96, 0, 0}) && Near(c[3], {96, 0, 0}),
          "bmx parked: turned with the heading");
}

} // namespace

SELFTEST(Early, TestThawBmxParkedWalk);
SELFTEST(Early, TestThawBmxParkedStartSpot);
