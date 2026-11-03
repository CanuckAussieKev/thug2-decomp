// NOT ORIGINAL - the level editor's navigation pieces (ours). See level_editor_nav.h.

#include "editor/level_editor_nav.h"
#include "editor/level_editor.h"
#include "editor/level_editor_organize.h"
#include "engine/core/checked_index.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace frontend::level_editor::nav {

namespace {

constexpr float kPi = 3.14159265358979f;

// Whether the feet can go from `from` by `move` (horizontal): no wall at knee or head height, up to the radius past it.
bool get_clear(V3 from, V3 move, const Cast& cast)
{
    const float length = std::sqrt(move.x * move.x + move.z * move.z);
    if (length <= 0.0f)
        return true;
    const V3 dir = {.x = move.x / length, .y = 0.0f, .z = move.z / length};
    V3 hit{};
    for (const float height : {kStepHeight + 1.0f, kEyeHeight - 4.0f}) {
        const V3 a = {.x = from.x, .y = from.y + height, .z = from.z};
        if (cast(a, a + dir * (length + kRadius), &hit))
            return false;
        // (and a radius clear of walls along each axis it moves on, from where it gets to: a slanting move stops as far
        // from a wall as a straight one)
        const V3 to = a + move;
        if (move.x != 0.0f && cast(to, to + V3{.x = move.x > 0.0f ? kRadius : -kRadius, .y = 0.0f, .z = 0.0f}, &hit))
            return false;
        if (move.z != 0.0f && cast(to, to + V3{.x = 0.0f, .y = 0.0f, .z = move.z > 0.0f ? kRadius : -kRadius}, &hit))
            return false;
    }
    return true;
}

} // namespace

Walker walker_at(V3 camera)
{
    Walker w;
    w.feet = {.x = camera.x, .y = camera.y - kEyeHeight, .z = camera.z};
    return w;
}

void get_step(Walker* w, float yaw, const WalkInput& input, float seconds, const Cast& cast)
{
    if (seconds <= 0.0f)
        return;
    // Along the ground: the heading's forward and right (as frame_from_yaw_pitch's at pitch 0).
    const V3 forward = {.x = std::sin(yaw), .y = 0.0f, .z = std::cos(yaw)};
    const V3 right = {.x = -std::cos(yaw), .y = 0.0f, .z = std::sin(yaw)}; // (the frame's right: up x back)
    const float f = std::clamp(input.forward, -1.0f, 1.0f);
    const float r = std::clamp(input.right, -1.0f, 1.0f);
    V3 wish = forward * f + right * r;
    const float length = std::sqrt(wish.x * wish.x + wish.z * wish.z);
    if (length > 1.0f)
        wish = wish * (1.0f / length);
    const V3 move = wish * ((input.run ? kRunSpeed : kWalkSpeed) * seconds);
    if (move.x != 0.0f || move.z != 0.0f) {
        if (get_clear(w->feet, move, cast)) {
            w->feet = w->feet + move;
        } else {
            // (sliding: each axis by itself)
            const V3 along_x = {.x = move.x, .y = 0.0f, .z = 0.0f};
            const V3 along_z = {.x = 0.0f, .y = 0.0f, .z = move.z};
            if (move.x != 0.0f && get_clear(w->feet, along_x, cast))
                w->feet = w->feet + along_x;
            if (move.z != 0.0f && get_clear(w->feet, along_z, cast))
                w->feet = w->feet + along_z;
        }
    }
    // The ground below (from a step's height up: a kerb walked onto is stood on).
    const V3 top = {.x = w->feet.x, .y = w->feet.y + kStepHeight, .z = w->feet.z};
    V3 ground{};
    const bool have_ground = cast(top, {.x = top.x, .y = top.y - kGroundSearch, .z = top.z}, &ground);
    if (input.jump && w->grounded) {
        w->vy = kJumpSpeed;
        w->grounded = false;
    }
    if (!have_ground) {
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
    const float vy0 = w->vy;
    w->vy -= kGravity * seconds;
    float dy = (vy0 + w->vy) * 0.5f * seconds;
    if (dy > 0.0f) {
        const V3 head = {.x = w->feet.x, .y = w->feet.y + kEyeHeight, .z = w->feet.z};
        V3 hit{};
        if (cast(head, {.x = head.x, .y = head.y + dy + 4.0f, .z = head.z}, &hit)) {
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

bool is_double_click(uint32_t first_ms, float first_x, float first_y, uint32_t second_ms, float second_x,
                     float second_y, uint32_t max_ms, float max_pixels)
{
    if (second_ms < first_ms || second_ms - first_ms > max_ms)
        return false;
    return std::fabs(second_x - first_x) <= max_pixels && std::fabs(second_y - first_y) <= max_pixels;
}

V3 teleport_target(V3 camera, V3 hit)
{
    const V3 along = hit - camera;
    const float distance = std::sqrt(get_dot(along, along));
    if (distance < 1.0f)
        return {.x = hit.x, .y = std::max(camera.y, hit.y + kLift), .z = hit.z};
    const V3 dir = along * (1.0f / distance);
    const float back = std::min(kStandOff, distance * 0.8f);
    V3 to = hit - dir * back;
    to.y = std::max(to.y, hit.y + kLift);
    return to;
}

V3 follow_step(Follower* f, bool active, V3 target)
{
    if (!active) {
        f->have = false;
        return {.x = 0.0f, .y = 0.0f, .z = 0.0f};
    }
    V3 delta = f->have ? target - f->last : V3{.x = 0.0f, .y = 0.0f, .z = 0.0f};
    f->have = true;
    f->last = target;
    return delta;
}

int drill_index(int current, int steps, int count)
{
    if (count <= 0)
        return -1;
    const int base = current >= 0 && current < count ? current : (steps > 0 ? -1 : 0);
    return ((base + steps) % count + count) % count;
}

int slice_at(float dx, float dy, int count, float deadzone)
{
    if (count <= 0 || dx * dx + dy * dy < deadzone * deadzone)
        return -1;
    // (clockwise from straight up: screen y points down)
    float angle = std::atan2(dx, -dy);
    if (angle < 0.0f)
        angle += 2.0f * kPi;
    const float slice = 2.0f * kPi / static_cast<float>(count);
    const int index = static_cast<int>(std::floor((angle + slice * 0.5f) / slice));
    return index % count;
}

void slice_direction(int index, int count, float* dx, float* dy)
{
    const float angle = count > 0 ? 2.0f * kPi * static_cast<float>(index) / static_cast<float>(count) : 0.0f;
    *dx = std::sin(angle);
    *dy = -std::cos(angle);
}

float get_stick(StickRead stick)
{
    const auto raw = stick.raw;
    const auto deadzone = stick.deadzone;
    const float v = static_cast<float>(raw) / (raw < 0 ? 32768.0f : 32767.0f);
    const float a = std::fabs(v);
    if (a <= deadzone || deadzone >= 1.0f)
        return 0.0f;
    const float t = std::min(1.0f, (a - deadzone) / (1.0f - deadzone));
    return (v < 0.0f ? -1.0f : 1.0f) * t * t;
}

std::string quick_set_name(int number) { return "Set " + std::to_string(number); }

int find_group(const organize::State& state, const std::string& name)
{
    for (int i = 0; std::cmp_less(i, state.groups.size()); ++i) {
        if (core::at(state.groups, i).name == name)
            return i;
    }
    return -1;
}

bool store_group(organize::State* state, const std::string& name, const std::vector<organize::Member>& members)
{
    if (members.empty())
        return false;
    const int at = find_group(*state, name);
    if (at >= 0) {
        core::at(state->groups, at).members = members;
    } else {
        organize::GroupSet g;
        g.name = name;
        g.members = members;
        state->groups.push_back(g);
    }
    return true;
}

bool remove_from_group(organize::State* state, int index, const std::vector<organize::Member>& members)
{
    if (index < 0 || std::cmp_greater_equal(index, state->groups.size()))
        return false;
    std::vector<organize::Member>& in = core::at(state->groups, index).members;
    const size_t before = in.size();
    std::erase_if(in, [&](const organize::Member& m) { return std::ranges::find(members, m) != members.end(); });
    return in.size() != before;
}

} // namespace frontend::level_editor::nav
