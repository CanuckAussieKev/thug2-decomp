// NOT ORIGINAL - thug2_frame_pacing_check.exe: [Graphics] max_fps's maths checked without the game - the transforms blended
// between two ticks (base/maths/transform_blend.h: rotations as rotations, the ends exact, jumps found) and the
// render-only frames' schedule (engine/sys/frame_pacing.h: the settings read, the frames a tick, spread evenly, each
// ending before the next tick, the blend's alpha). Exit code 0 when everything holds.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

#include "base/maths/mat4.h"
#include "base/maths/transform_blend.h"
#include "engine/sys/frame_pacing.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace blend = maths::transform_blend;
namespace pacing = frame_pacing;

int g_failures = 0;
int g_checked = 0;

void expect(bool ok, const std::string& what)
{
    ++g_checked;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

bool close_to(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

// A rotation of `angle` radians about the unit axis (x, y, z), scaled by s, at (tx, ty, tz) - D3D rows.
maths::Mat4 make(double x, double y, double z, double angle, double s, double tx, double ty, double tz)
{
    const double c = std::cos(angle);
    const double n = std::sin(angle);
    const double t = 1.0 - c;
    // (the column-vector matrix; our rows are its columns)
    const double m[3][3] = {{(t * x * x) + c, (t * x * y) - (n * z), (t * x * z) + (n * y)},
                            {(t * x * y) + (n * z), (t * y * y) + c, (t * y * z) - (n * x)},
                            {(t * x * z) - (n * y), (t * y * z) + (n * x), (t * z * z) + c}};
    maths::Mat4 out{};
    for (int row = 0; row < 3; ++row) {
        out.row[row] = {.x = static_cast<float>(m[0][row] * s),
                        .y = static_cast<float>(m[1][row] * s),
                        .z = static_cast<float>(m[2][row] * s),
                        .w = 0.0F};
    }
    out.row[3] = {.x = static_cast<float>(tx), .y = static_cast<float>(ty), .z = static_cast<float>(tz), .w = 1.0F};
    return out;
}

bool same_bits(const maths::Mat4& a, const maths::Mat4& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

double max_difference(const maths::Mat4& a, const maths::Mat4& b)
{
    double most = 0.0;
    for (int i = 0; i < 16; ++i) {
        most = std::fmax(most, std::fabs(static_cast<double>(a.data()[i]) - b.data()[i])); // NOLINT
    }
    return most;
}

double axis_length(const maths::Mat4& m, int row)
{
    const maths::Vec4& v = m.row[row];
    return std::sqrt((static_cast<double>(v.x) * v.x) + (static_cast<double>(v.y) * v.y) + (static_cast<double>(v.z) * v.z));
}

constexpr double kPi = std::numbers::pi;
constexpr double kDegree = kPi / 180.0;

void check_blend()
{
    const maths::Mat4 a = make(0, 1, 0, 10 * kDegree, 1.0, 100, 50, -20);
    const maths::Mat4 b = make(0, 1, 0, 80 * kDegree, 1.0, 140, 50, 20);
    expect(same_bits(blend::blend(a, b, 0.0F), a), "alpha 0 is the earlier transform, bit for bit");
    expect(same_bits(blend::blend(a, b, 1.0F), b), "alpha 1 is the later transform, bit for bit");
    expect(same_bits(blend::blend(a, b, 1.5F), b), "alpha past 1 is the later transform");
    expect(same_bits(blend::blend(a, a, 0.37F), a), "two equal transforms give that transform");

    // a rotation blended as a rotation: halfway between 10 and 80 degrees is 45, its axes still unit length
    const maths::Mat4 half = blend::blend(a, b, 0.5F);
    const maths::Mat4 want = make(0, 1, 0, 45 * kDegree, 1.0, 120, 50, 0);
    expect(max_difference(half, want) < 1e-4, "halfway between 10 and 80 degrees is 45, the translation halfway");
    // a fast flip (170 degrees in a tick, under the jump limit for an object... checked with the blend alone): the shape
    // kept - a matrix lerp would shrink the axes to cos(85) ~ 0.09 of their length
    const maths::Mat4 flip_a = make(1, 0, 0, 0.0, 1.0, 0, 0, 0);
    const maths::Mat4 flip_b = make(1, 0, 0, 170 * kDegree, 1.0, 0, 0, 0);
    for (const float alpha : {0.25F, 0.5F, 0.75F}) {
        const maths::Mat4 m = blend::blend(flip_a, flip_b, alpha);
        expect(close_to(axis_length(m, 0), 1.0, 1e-5) && close_to(axis_length(m, 1), 1.0, 1e-5) && close_to(axis_length(m, 2), 1.0, 1e-5),
               "a flip blended keeps unit axes (alpha " + std::to_string(alpha) + ")");
        expect(max_difference(m, make(1, 0, 0, alpha * 170 * kDegree, 1.0, 0, 0, 0)) < 1e-4,
               "a flip turns evenly: alpha " + std::to_string(alpha) + " of 170 degrees");
    }
    // the shorter way round: 170 to -170 degrees (20 degrees apart) passes 180, not 0
    const maths::Mat4 near_a = make(0, 1, 0, 170 * kDegree, 1.0, 0, 0, 0);
    const maths::Mat4 near_b = make(0, 1, 0, -170 * kDegree, 1.0, 0, 0, 0);
    expect(max_difference(blend::blend(near_a, near_b, 0.5F), make(0, 1, 0, 180 * kDegree, 1.0, 0, 0, 0)) < 1e-4,
           "170 to -170 degrees blends through 180 (the shorter way)");
    // scale blended linearly, a mirror kept
    const maths::Mat4 small = make(0, 0, 1, 0.0, 1.0, 0, 0, 0);
    const maths::Mat4 big = make(0, 0, 1, 0.0, 3.0, 0, 0, 0);
    expect(close_to(axis_length(blend::blend(small, big, 0.5F), 1), 2.0, 1e-5), "scale 1 to 3 blends to 2");
    maths::Mat4 mirror_a = make(0, 1, 0, 0.0, 1.0, 0, 0, 0);
    maths::Mat4 mirror_b = make(0, 1, 0, 40 * kDegree, 1.0, 0, 0, 0);
    for (maths::Mat4* m : {&mirror_a, &mirror_b}) {
        m->row[0] = {.x = -m->row[0].x, .y = -m->row[0].y, .z = -m->row[0].z, .w = 0.0F};
    }
    maths::Mat4 mirror_want = make(0, 1, 0, 20 * kDegree, 1.0, 0, 0, 0);
    mirror_want.row[0] = {.x = -mirror_want.row[0].x, .y = -mirror_want.row[0].y, .z = -mirror_want.row[0].z, .w = 0.0F};
    expect(max_difference(blend::blend(mirror_a, mirror_b, 0.5F), mirror_want) < 1e-4, "a mirrored transform stays mirrored");
    // sheared: not blended (the later one)
    maths::Mat4 sheared = a;
    sheared.row[1].x += 0.5F;
    expect(same_bits(blend::blend(sheared, b, 0.5F), b), "a sheared transform isn't blended: the later one");
    // the rows' w as the later matrix has them
    maths::Mat4 w_odd = b;
    w_odd.row[3].w = 2.0F;
    expect(blend::blend(a, w_odd, 0.5F).row[3].w == 2.0F, "row w copied from the later matrix");

    // decompose / compose round trip, over many rotations
    int bad = 0;
    for (int i = 0; i < 500; ++i) {
        const double ax = std::sin(i * 1.3);
        const double ay = std::cos(i * 0.7);
        const double az = std::sin(i * 2.1 + 0.3);
        const double len = std::sqrt((ax * ax) + (ay * ay) + (az * az));
        const maths::Mat4 m = make(ax / len, ay / len, az / len, std::fmod(i * 0.37, 2 * kPi) - kPi, 0.5 + (i % 7) * 0.25,
                                   i * 3.0, -i * 1.5, i * 0.25);
        blend::Pose pose;
        if (!blend::decompose(m, &pose) || max_difference(blend::compose(pose), m) > 1e-4) {
            ++bad;
        }
    }
    expect(bad == 0, "decompose / compose round trip (" + std::to_string(bad) + " of 500 off)");
    // slerp turns at a constant rate
    const blend::Quat q0{0, 0, 0, 1};
    const blend::Quat q1{0, static_cast<float>(std::sin(60 * kDegree)), 0, static_cast<float>(std::cos(60 * kDegree))}; // 120 deg
    for (const float alpha : {0.1F, 0.3F, 0.6F, 0.9F}) {
        expect(close_to(blend::angle_between(q0, blend::slerp(q0, q1, alpha)), alpha * 120 * kDegree, 1e-4),
               "slerp's angle is alpha of the whole (alpha " + std::to_string(alpha) + ")");
    }
}

void check_jumps()
{
    const maths::Mat4 at = make(0, 1, 0, 0.0, 1.0, 0, 0, 0);
    expect(!blend::is_jump(at, at, blend::kObjectJump), "no move is no jump");
    expect(!blend::is_jump(at, make(0, 1, 0, 0.0, 1.0, 50, 0, 0), blend::kObjectJump), "50 inches in a tick isn't a jump");
    expect(blend::is_jump(at, make(0, 1, 0, 0.0, 1.0, 0, 200, 0), blend::kObjectJump), "200 inches in a tick is a teleport");
    expect(!blend::is_jump(at, make(0, 1, 0, 30 * kDegree, 1.0, 0, 0, 0), blend::kObjectJump), "an object's 30-degree turn isn't a jump");
    expect(blend::is_jump(at, make(0, 1, 0, 120 * kDegree, 1.0, 0, 0, 0), blend::kObjectJump), "an object's 120-degree turn is a jump (a reset)");
    expect(!blend::is_jump(at, make(1, 0, 0, 20 * kDegree, 1.0, 0, 0, 0), blend::kCameraJump), "a camera's 20-degree turn isn't a cut");
    expect(blend::is_jump(at, make(1, 0, 0, 60 * kDegree, 1.0, 0, 0, 0), blend::kCameraJump), "a camera's 60-degree turn is a cut");
    expect(blend::is_jump(at, make(0, 1, 0, 0.0, 1.0, 130, 0, 0), blend::kCameraJump), "a camera moved 130 inches in a tick is a cut");
    maths::Mat4 mirrored = at;
    mirrored.row[0].x = -1.0F;
    expect(blend::is_jump(at, mirrored, blend::kObjectJump), "a mirror coming or going is a jump");
    maths::Mat4 broken = at;
    broken.row[0] = {};
    expect(blend::is_jump(at, broken, blend::kObjectJump), "a matrix that can't be taken apart is a jump");
}

void check_settings()
{
    expect(pacing::parse_max_fps(nullptr) == 0 && pacing::parse_max_fps("") == 0 && pacing::parse_max_fps("0") == 0,
           "max_fps empty / 0: the original");
    expect(pacing::parse_max_fps("junk") == 0 && pacing::parse_max_fps("12x") == 0, "max_fps unreadable: the original");
    expect(pacing::parse_max_fps("uncapped") == pacing::kUncapped && pacing::parse_max_fps(" Unlimited ") == pacing::kUncapped &&
               pacing::parse_max_fps("-1") == pacing::kUncapped,
           "max_fps uncapped / unlimited / -1");
    expect(pacing::parse_max_fps("144") == 144 && pacing::parse_max_fps("30") == 60 && pacing::parse_max_fps("5000") == 1000,
           "max_fps N clamped to 60-1000");
    expect(pacing::parse_present_mode("") == pacing::PresentMode::kVsync &&
               pacing::parse_present_mode("vsync") == pacing::PresentMode::kVsync &&
               pacing::parse_present_mode("MAILBOX") == pacing::PresentMode::kMailbox &&
               pacing::parse_present_mode("immediate") == pacing::PresentMode::kImmediate,
           "present_mode parsed");
}

void check_schedule()
{
    double carry = 0.0;
    expect(pacing::frames_this_tick(0, &carry) == 1, "max_fps 0: one frame a tick");
    for (const int cap : {60, 120, 144, 165, 240, 1000}) {
        carry = 0.0;
        int total = 0;
        int fewest = 1 << 30;
        int most = 0;
        for (int tick = 0; tick < 600; ++tick) {
            const int frames = pacing::frames_this_tick(cap, &carry);
            total += frames;
            fewest = std::min(fewest, frames);
            most = std::max(most, frames);
        }
        expect(total == cap * 10, "max_fps " + std::to_string(cap) + ": " + std::to_string(cap * 10) +
                                      " frames in 600 ticks (got " + std::to_string(total) + ")");
        expect(most - fewest <= 1, "max_fps " + std::to_string(cap) + ": the frames a tick differ by at most one");
    }
    carry = 0.0;
    expect(pacing::frames_this_tick(pacing::kUncapped, &carry) == pacing::kUncappedFrames, "uncapped: as many as fit");
    expect(pacing::frame_due(1000, 600, 1, 3) == 1200 && pacing::frame_due(1000, 600, 2, 3) == 1400 &&
               pacing::frame_due(1000, 600, 0, 3) == 1000,
           "frames spread evenly across the tick");
    expect(pacing::frame_fits(100, 50, 200) && !pacing::frame_fits(160, 50, 200) && !pacing::frame_fits(200, 0, 200),
           "a frame starts only when it ends by the next tick");
    expect(pacing::blend_alpha(900, 1000, 600) == 0.0F && close_to(pacing::blend_alpha(1300, 1000, 600), 0.5, 1e-6) &&
               pacing::blend_alpha(1700, 1000, 600) == 1.0F && pacing::blend_alpha(1000, 1000, 0) == 0.0F,
           "alpha: the time into the tick over its length, 0 - 1");
    expect(pacing::shown_time_ms(1000, 1.0F) == 1000 && pacing::shown_time_ms(1000, 0.0F) == 983 &&
               pacing::shown_time_ms(1000, 0.5F) == 992 && pacing::shown_time_ms(5, 0.0F) == 0,
           "the time shown is behind by the part of a tick not reached");

    // A second of the loop at 144 frames a second with each frame taking 2 ms (counter: 10 MHz): every tick's frames
    // start after it and end before the next one, their alpha rising; the ticks themselves are exactly 60, at their times.
    constexpr uint64_t kFrequency = 10000000;
    constexpr uint64_t kTick = kFrequency / 60;
    constexpr uint64_t kCost = kFrequency / 500;
    carry = 0.0;
    int drawn = 0;
    int ticks = 0;
    bool in_order = true;
    for (uint64_t tick_time = 0; tick_time < kFrequency - (kTick / 2); tick_time += kTick) {
        ++ticks;
        const int frames = pacing::frames_this_tick(144, &carry);
        uint64_t now = tick_time + kCost; // (the tick's own frame drawn)
        float last_alpha = pacing::blend_alpha(tick_time, tick_time, kTick);
        ++drawn;
        for (int i = 1; i < frames; ++i) {
            const uint64_t due = pacing::frame_due(tick_time, kTick, i, frames);
            now = std::max(now, due);
            if (!pacing::frame_fits(now, kCost, tick_time + kTick)) {
                break;
            }
            const float alpha = pacing::blend_alpha(now, tick_time, kTick);
            in_order = in_order && alpha > last_alpha && alpha < 1.0F;
            last_alpha = alpha;
            now += kCost;
            in_order = in_order && now <= tick_time + kTick;
            ++drawn;
        }
    }
    expect(ticks == 60, "a second is 60 ticks whatever the frame rate");
    expect(in_order, "render-only frames come in order within their tick and end before the next");
    expect(drawn >= 140 && drawn <= 144, "about 144 frames in the second at max_fps 144 (" + std::to_string(drawn) + ")");
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_blend();
    check_jumps();
    check_settings();
    check_schedule();
    std::printf("frame pacing check: %d checks, %d failed\n", g_checked, g_failures);
    return g_failures == 0 ? 0 : 1;
}
