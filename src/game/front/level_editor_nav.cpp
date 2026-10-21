// NOT ORIGINAL - the level editor's navigation pieces (ours). See level_editor_nav.h.

#include "game/front/level_editor_nav.h"

#include <algorithm>
#include <cmath>

namespace Front::LevelEditor::Nav {

namespace {

constexpr float kPi = 3.14159265358979f;

// Whether the feet can go from `from` by `move` (horizontal): no wall at knee or head height, up to the radius past it.
bool Clear(V3 from, V3 move, const Cast& cast)
{
    float length = std::sqrt(move.x * move.x + move.z * move.z);
    if (length <= 0.0f)
        return true;
    V3 dir = {move.x / length, 0.0f, move.z / length};
    V3 hit;
    for (float height : {kStepHeight + 1.0f, kEyeHeight - 4.0f}) {
        V3 a = {from.x, from.y + height, from.z};
        if (cast(a, a + dir * (length + kRadius), &hit))
            return false;
    }
    return true;
}

} // namespace

Walker WalkerAt(V3 camera)
{
    Walker w;
    w.feet = {camera.x, camera.y - kEyeHeight, camera.z};
    return w;
}

void Step(Walker* w, float yaw, const WalkInput& input, float seconds, const Cast& cast)
{
    if (seconds <= 0.0f)
        return;
    // Along the ground: the heading's forward and right (as FrameFromYawPitch's at pitch 0).
    V3 forward = {std::sin(yaw), 0.0f, std::cos(yaw)};
    V3 right = {-std::cos(yaw), 0.0f, std::sin(yaw)}; // (the frame's right: up x back)
    float f = std::clamp(input.forward, -1.0f, 1.0f), r = std::clamp(input.right, -1.0f, 1.0f);
    V3 wish = forward * f + right * r;
    float length = std::sqrt(wish.x * wish.x + wish.z * wish.z);
    if (length > 1.0f)
        wish = wish * (1.0f / length);
    V3 move = wish * ((input.run ? kRunSpeed : kWalkSpeed) * seconds);
    if (move.x != 0.0f || move.z != 0.0f) {
        if (Clear(w->feet, move, cast)) {
            w->feet = w->feet + move;
        } else {
            // (sliding: each axis by itself)
            V3 alongX = {move.x, 0.0f, 0.0f}, alongZ = {0.0f, 0.0f, move.z};
            if (move.x != 0.0f && Clear(w->feet, alongX, cast))
                w->feet = w->feet + alongX;
            if (move.z != 0.0f && Clear(w->feet, alongZ, cast))
                w->feet = w->feet + alongZ;
        }
    }
    // The ground below (from a step's height up: a kerb walked onto is stood on).
    V3 top = {w->feet.x, w->feet.y + kStepHeight, w->feet.z};
    V3 ground;
    bool haveGround = cast(top, {top.x, top.y - kGroundSearch, top.z}, &ground);
    if (input.jump && w->grounded) {
        w->vy = kJumpSpeed;
        w->grounded = false;
    }
    if (!haveGround) {
        // (no ground at all: the level's edge - stay at this height)
        w->vy = 0.0f;
        w->grounded = false;
        return;
    }
    if (w->grounded && w->vy <= 0.0f && w->feet.y - ground.y <= kStepHeight) {
        w->feet.y = ground.y; // (standing: up a step or down one)
        w->vy = 0.0f;
        return;
    }
    // In the air: gravity; a ceiling stops a jump; the ground stops the fall.
    float vy0 = w->vy;
    w->vy -= kGravity * seconds;
    float dy = (vy0 + w->vy) * 0.5f * seconds;
    if (dy > 0.0f) {
        V3 head = {w->feet.x, w->feet.y + kEyeHeight, w->feet.z};
        V3 hit;
        if (cast(head, {head.x, head.y + dy + 4.0f, head.z}, &hit)) {
            dy = 0.0f;
            w->vy = 0.0f;
        }
    }
    w->feet.y += dy;
    w->grounded = false;
    if (w->feet.y <= ground.y) {
        w->feet.y = ground.y;
        w->vy = 0.0f;
        w->grounded = true;
    }
}

bool IsDoubleClick(uint32_t firstMs, float firstX, float firstY, uint32_t secondMs, float secondX, float secondY,
                   uint32_t maxMs, float maxPixels)
{
    if (secondMs < firstMs || secondMs - firstMs > maxMs)
        return false;
    return std::fabs(secondX - firstX) <= maxPixels && std::fabs(secondY - firstY) <= maxPixels;
}

V3 TeleportTarget(V3 camera, V3 hit)
{
    V3 along = hit - camera;
    float distance = std::sqrt(Dot(along, along));
    if (distance < 1.0f)
        return {hit.x, std::max(camera.y, hit.y + kLift), hit.z};
    V3 dir = along * (1.0f / distance);
    float back = std::min(kStandOff, distance * 0.8f);
    V3 to = hit - dir * back;
    to.y = std::max(to.y, hit.y + kLift);
    return to;
}

V3 FollowStep(Follower* f, bool active, V3 target)
{
    if (!active) {
        f->have = false;
        return {0.0f, 0.0f, 0.0f};
    }
    V3 delta = f->have ? target - f->last : V3{0.0f, 0.0f, 0.0f};
    f->have = true;
    f->last = target;
    return delta;
}

int DrillIndex(int current, int steps, int count)
{
    if (count <= 0)
        return -1;
    int base = current >= 0 && current < count ? current : (steps > 0 ? -1 : 0);
    return ((base + steps) % count + count) % count;
}

int SliceAt(float dx, float dy, int count, float deadzone)
{
    if (count <= 0 || dx * dx + dy * dy < deadzone * deadzone)
        return -1;
    // (clockwise from straight up: screen y points down)
    float angle = std::atan2(dx, -dy);
    if (angle < 0.0f)
        angle += 2.0f * kPi;
    float slice = 2.0f * kPi / static_cast<float>(count);
    int index = static_cast<int>(std::floor((angle + slice * 0.5f) / slice));
    return index % count;
}

void SliceDirection(int index, int count, float* dx, float* dy)
{
    float angle = count > 0 ? 2.0f * kPi * static_cast<float>(index) / static_cast<float>(count) : 0.0f;
    *dx = std::sin(angle);
    *dy = -std::cos(angle);
}

float Stick(int raw, float deadzone)
{
    float v = static_cast<float>(raw) / (raw < 0 ? 32768.0f : 32767.0f);
    float a = std::fabs(v);
    if (a <= deadzone || deadzone >= 1.0f)
        return 0.0f;
    float t = std::min(1.0f, (a - deadzone) / (1.0f - deadzone));
    return (v < 0.0f ? -1.0f : 1.0f) * t * t;
}

std::string QuickSetName(int number) { return "Set " + std::to_string(number); }

int FindGroup(const Organize::State& state, const std::string& name)
{
    for (int i = 0; i < static_cast<int>(state.groups.size()); ++i) {
        if (state.groups[i].name == name)
            return i;
    }
    return -1;
}

bool StoreGroup(Organize::State* state, const std::string& name, const std::vector<Organize::Member>& members)
{
    if (members.empty())
        return false;
    int at = FindGroup(*state, name);
    if (at >= 0) {
        state->groups[at].members = members;
    } else {
        Organize::Group g;
        g.name = name;
        g.members = members;
        state->groups.push_back(g);
    }
    return true;
}

bool RemoveFromGroup(Organize::State* state, int index, const std::vector<Organize::Member>& members)
{
    if (index < 0 || index >= static_cast<int>(state->groups.size()))
        return false;
    std::vector<Organize::Member>& in = state->groups[index].members;
    size_t before = in.size();
    in.erase(std::remove_if(in.begin(), in.end(),
                            [&](const Organize::Member& m) {
                                return std::find(members.begin(), members.end(), m) != members.end();
                            }),
             in.end());
    return in.size() != before;
}

} // namespace Front::LevelEditor::Nav
