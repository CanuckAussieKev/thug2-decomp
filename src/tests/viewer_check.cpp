// NOT ORIGINAL - thug2_viewer_check: the demo viewer's pure parts checked (tests/CMakeLists.txt): the camera path's
// interpolation (the keys hit, continuity, the yaw's wrap, ease and hold), its rows, the sidecar written and read back
// (exactly, a newer version refused, a later line skipped), and the split comparison's projection crop.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "devtools/demo_camera_path.h"
#include "engine/gfx/rt_scene.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace path_api = devtools::camera_path;
using path_api::Ease;
using path_api::Key;
using path_api::Path;
using path_api::Pose;
using path_api::Vec3;

constexpr double kExact = 1e-6;   // (values that should come out as given)
constexpr double kClose = 1e-3;   // (after a float's rounding)
constexpr double kStepTolerance = 0.05;
constexpr double kSlopeShare = 0.02; // (the slopes either side of a key: within 2%)
constexpr double kProbe = 1e-3;      // (frames either side of a point)
constexpr double kSlopeProbe = 0.01;
constexpr double kHalfFrame = 0.5;
constexpr double kHalfTurn = 180.0;
constexpr double kTurn = 360.0;

// The sample path: four keys, each the given way to the next.
struct KeyRow {
    int32_t frame = 0;
    Vec3 position;
    float yaw = 0.0F;
    float pitch = 0.0F;
    float fov = 0.0F;
};
constexpr std::array<KeyRow, 4> kKeys = {{
    {.frame = 0, .position = {.x = 0.0F, .y = 0.0F, .z = 0.0F}, .yaw = 0.0F, .pitch = 0.0F, .fov = 70.0F},
    {.frame = 60, .position = {.x = 100.0F, .y = 50.0F, .z = 0.0F}, .yaw = 90.0F, .pitch = 10.0F, .fov = 80.0F},
    {.frame = 90, .position = {.x = 150.0F, .y = 50.0F, .z = 300.0F}, .yaw = 170.0F, .pitch = -20.0F, .fov = 60.0F},
    {.frame = 200, .position = {.x = 0.0F, .y = 0.0F, .z = 0.0F}, .yaw = -170.0F, .pitch = 0.0F, .fov = 70.0F},
}};
constexpr std::array<int32_t, 2> kInnerKeys = {60, 90};
constexpr double kBefore = -50.0;
constexpr double kAfter = 500.0;
constexpr double kLastFrame = 200.0;
// linear: frame 30 is half way from key 0 to key 1
constexpr double kHalfway = 30.0;
constexpr double kHalfwayX = 50.0;
constexpr double kHalfwayFov = 75.0;
// hold: just before key 1, and at it
constexpr double kJustBefore = 59.9;
constexpr double kKeyOneX = 100.0;

int& failures()
{
    static int count = 0;
    return count;
}

void check(bool passed, const std::string& what)
{
    if (!passed) {
        ++failures();
        std::printf("FAIL %s\n", what.c_str());
    }
}

bool close_to(double value, double expected, double tolerance) { return std::abs(value - expected) <= tolerance; }
bool same_place(const Vec3& value, const Vec3& expected, double tolerance)
{
    return close_to(value.x, expected.x, tolerance) && close_to(value.y, expected.y, tolerance) &&
           close_to(value.z, expected.z, tolerance);
}
// The difference of two angles in degrees, -180..180.
double turn_between(double from, double to)
{
    double turn = std::fmod(to - from, kTurn);
    if (turn > kHalfTurn) {
        turn -= kTurn;
    } else if (turn < -kHalfTurn) {
        turn += kTurn;
    }
    return turn;
}

Pose pose_of(const KeyRow& row)
{
    Pose pose;
    pose.position = row.position;
    pose.yaw = row.yaw;
    pose.pitch = row.pitch;
    pose.fov = row.fov;
    return pose;
}

Pose at(const Path& path, double frame)
{
    Pose pose;
    check(path_api::pose_at(path, frame, &pose), "pose_at with keys");
    return pose;
}

Path sample_path(Ease ease)
{
    Path path;
    for (const KeyRow& row : kKeys) {
        path_api::add_key(&path, {.frame = row.frame, .pose = pose_of(row), .ease = ease});
    }
    return path;
}

void check_keys_hit()
{
    for (const Ease ease : {Ease::kSmooth, Ease::kLinear, Ease::kEase, Ease::kHold}) {
        const Path path = sample_path(ease);
        const std::string name = path_api::ease_name(ease);
        for (const Key& key : path.keys) {
            const Pose pose = at(path, key.frame);
            const std::string what = name + " key at frame " + std::to_string(key.frame);
            check(same_place(pose.position, key.pose.position, kClose), what + ": position");
            check(close_to(turn_between(pose.yaw, key.pose.yaw), 0.0, kClose), what + ": yaw");
            check(close_to(pose.pitch, key.pose.pitch, kClose) && close_to(pose.fov, key.pose.fov, kClose),
                  what + ": pitch, fov");
        }
        // (before the first and after the last: those keys)
        check(same_place(at(path, kBefore).position, kKeys.front().position, kExact) &&
                  same_place(at(path, kAfter).position, kKeys.back().position, kExact),
              name + ": outside the keys");
    }
    const Path empty;
    Pose unused;
    check(!path_api::pose_at(empty, kHalfway, &unused), "no keys: no pose");
}

// The curve is continuous everywhere, and smooth's speed carries through each inner key (the slopes either side agree).
void check_continuity()
{
    const Path path = sample_path(Ease::kSmooth);
    for (int step = 1; step < static_cast<int>(kLastFrame / kHalfFrame); ++step) {
        const double frame = step * kHalfFrame;
        const Pose before = at(path, frame - kProbe);
        const Pose after = at(path, frame + kProbe);
        check(same_place(before.position, after.position, kStepTolerance), "smooth: continuous at " + std::to_string(frame));
    }
    for (const int32_t key : kInnerKeys) {
        const Pose before = at(path, key - kSlopeProbe);
        const Pose here = at(path, key);
        const Pose after = at(path, key + kSlopeProbe);
        const auto agree = [](double left, double right) {
            return close_to(left, right, kSlopeShare * std::max(1.0, std::abs(left)));
        };
        check(agree((here.position.x - before.position.x) / kSlopeProbe, (after.position.x - here.position.x) / kSlopeProbe) &&
                  agree((here.position.y - before.position.y) / kSlopeProbe,
                        (after.position.y - here.position.y) / kSlopeProbe) &&
                  agree((here.position.z - before.position.z) / kSlopeProbe,
                        (after.position.z - here.position.z) / kSlopeProbe),
              "smooth: the slope carries through key " + std::to_string(key));
    }
    // ease: no speed at the keys
    const Path eased = sample_path(Ease::kEase);
    for (const KeyRow& row : kKeys) {
        const Pose here = at(eased, row.frame);
        const Pose after = at(eased, row.frame + kSlopeProbe);
        check(close_to((after.position.x - here.position.x) / kSlopeProbe, 0.0, kStepTolerance),
              "ease: still at key " + std::to_string(row.frame));
    }
    // linear: halfway is the middle
    const Path linear = sample_path(Ease::kLinear);
    check(close_to(at(linear, kHalfway).position.x, kHalfwayX, kClose) &&
              close_to(at(linear, kHalfway).fov, kHalfwayFov, kClose),
          "linear: the middle");
    // hold: the first key's pose until the next, then a cut
    const Path hold = sample_path(Ease::kHold);
    check(close_to(at(hold, kJustBefore).position.x, 0.0, kExact) &&
              close_to(at(hold, std::get<1>(kKeys).frame).position.x, kKeyOneX, kExact),
          "hold: cuts");
}

// The yaw turns the short way: 170 to -170 through 180, not through 0.
void check_yaw_wrap()
{
    constexpr float kFrom = 170.0F;
    constexpr int32_t kFrames = 10;
    constexpr double kMiddle = 5.0;
    constexpr double kQuarter = 2.5;
    constexpr double kQuarterYaw = 175.0;
    Path path;
    Pose from;
    from.yaw = kFrom;
    Pose to;
    to.yaw = -kFrom;
    path_api::add_key(&path, {.frame = 0, .pose = from, .ease = Ease::kLinear});
    path_api::add_key(&path, {.frame = kFrames, .pose = to, .ease = Ease::kLinear});
    const Pose middle = at(path, kMiddle);
    check(close_to(std::abs(middle.yaw), kHalfTurn, kClose),
          "yaw: 170 -> -170 passes 180 (got " + std::to_string(middle.yaw) + ")");
    check(close_to(at(path, kQuarter).yaw, kQuarterYaw, kClose), "yaw: a quarter of the way is 175");
}

double dot(const Vec3& left, const Vec3& right)
{
    return (static_cast<double>(left.x) * right.x) + (static_cast<double>(left.y) * right.y) +
           (static_cast<double>(left.z) * right.z);
}

void check_rows()
{
    constexpr std::array<float, 4> kYaws = {0.0F, 45.0F, 90.0F, -135.0F};
    constexpr std::array<float, 3> kPitches = {-60.0F, 0.0F, 30.0F};
    constexpr std::array<float, 2> kRolls = {0.0F, 20.0F};
    constexpr double kUnit = 1e-5;
    for (const float yaw : kYaws) {
        for (const float pitch : kPitches) {
            for (const float roll : kRolls) {
                Pose pose;
                pose.yaw = yaw;
                pose.pitch = pitch;
                pose.roll = roll;
                const path_api::Rows rows = path_api::rows_of(pose);
                const std::string what = "rows yaw " + std::to_string(yaw) + " pitch " + std::to_string(pitch) +
                                         " roll " + std::to_string(roll);
                check(close_to(dot(rows.right, rows.right), 1.0, kUnit) && close_to(dot(rows.up, rows.up), 1.0, kUnit) &&
                          close_to(dot(rows.back, rows.back), 1.0, kUnit),
                      what + ": unit");
                check(close_to(dot(rows.right, rows.up), 0.0, kUnit) && close_to(dot(rows.right, rows.back), 0.0, kUnit) &&
                          close_to(dot(rows.up, rows.back), 0.0, kUnit),
                      what + ": square");
                // (looking along -back: the yaw and pitch read back)
                const path_api::Angles angles =
                    path_api::angles_toward({.x = -rows.back.x, .y = -rows.back.y, .z = -rows.back.z});
                check(angles.found && close_to(turn_between(angles.yaw, yaw), 0.0, kClose) &&
                          close_to(angles.pitch, pitch, kClose),
                      what + ": the angles back");
            }
        }
    }
    // yaw 0 looks along +z, 90 along +x (the screenshot sidecar's heading)
    constexpr float kRight = 90.0F;
    check(close_to(path_api::forward_of(Pose{}).z, 1.0, kExact), "yaw 0 looks along +z");
    Pose turned;
    turned.yaw = kRight;
    check(close_to(path_api::forward_of(turned).x, 1.0, kExact), "yaw 90 looks along +x");
    check(!path_api::angles_toward({}).found, "no angles for a zero direction");
}

void check_sidecar()
{
    constexpr float kRoll = 12.345678F;
    constexpr Vec3 kOdd = {.x = 0.1F, .y = -1234.5677F, .z = 3.0e-7F};
    constexpr int32_t kLoopIn = 120;
    constexpr int32_t kLoopOut = 480;
    Path path = sample_path(Ease::kSmooth);
    // (keys 1 and 2: other eases; 0: odd numbers)
    path.keys.at(1).ease = Ease::kEase;
    path.keys.at(2).ease = Ease::kHold;
    path.keys.at(2).pose.roll = kRoll;
    path.keys.at(0).pose.position = kOdd;
    path.fixed.push_back({.name = "track the rail", .pose = pose_of(std::get<1>(kKeys))});
    path.fixed.push_back({.name = "Camera 2", .pose = pose_of(std::get<2>(kKeys))});
    path.loop_in = kLoopIn;
    path.loop_out = kLoopOut;
    const std::string text = path_api::write_text(path);
    Path back;
    std::string why;
    check(path_api::read_text(text, &back, &why), "sidecar read back: " + why);
    check(back.keys.size() == path.keys.size() && back.fixed.size() == path.fixed.size(), "sidecar: the counts");
    for (std::size_t i = 0; i < path.keys.size() && i < back.keys.size(); ++i) {
        const Key& written = path.keys.at(i);
        const Key& read = back.keys.at(i);
        check(written.frame == read.frame && written.ease == read.ease && written.pose.position == read.pose.position &&
                  written.pose.yaw == read.pose.yaw && written.pose.pitch == read.pose.pitch &&
                  written.pose.roll == read.pose.roll && written.pose.fov == read.pose.fov,
              "sidecar: key " + std::to_string(i) + " exactly");
    }
    for (std::size_t i = 0; i < path.fixed.size() && i < back.fixed.size(); ++i) {
        check(path.fixed.at(i).name == back.fixed.at(i).name &&
                  path.fixed.at(i).pose.position == back.fixed.at(i).pose.position &&
                  path.fixed.at(i).pose.fov == back.fixed.at(i).pose.fov,
              "sidecar: fixed camera " + std::to_string(i));
    }
    check(back.loop_in == kLoopIn && back.loop_out == kLoopOut, "sidecar: the loop");
    check(path_api::write_text(back) == text, "sidecar: written again the same");
    // CRLF, a later version's line kind skipped
    Path skipped;
    check(path_api::read_text("thug2-demo-camera 1\r\nkey 5 1 2 3 4 5 6 7 linear\r\nmarker 7 hello\r\n", &skipped, &why) &&
              skipped.keys.size() == 1,
          "sidecar: CRLF, an unknown line");
    // refused: a newer version, a broken key, not ours
    Path refused;
    check(!path_api::read_text("thug2-demo-camera 2\nkey 1 0 0 0 0 0 0 0 smooth\n", &refused, &why),
          "sidecar: a newer version refused");
    check(!path_api::read_text("thug2-demo-camera 1\nkey 1 0 0 zero 0 0 0 0 smooth\n", &refused, &why),
          "sidecar: a broken key refused");
    check(!path_api::read_text("hello\n", &refused, &why), "sidecar: not a camera file");
}

void check_key_edits()
{
    constexpr int32_t kTaken = 90;
    constexpr int32_t kFree = 150;
    constexpr float kReplaced = 9.0F;
    Path path = sample_path(Ease::kSmooth);
    std::size_t index = 1; // (frame 60)
    check(!path_api::move_key(&path, &index, kTaken), "move: refused onto another key");
    check(path_api::move_key(&path, &index, kFree) && index == 2 && path.keys.at(2).frame == kFree,
          "move: re-sorted, the index follows");
    check(path_api::key_at(path, kFree) == 2 && path_api::key_at(path, std::get<1>(kKeys).frame) == -1, "key_at");
    Pose replaced;
    replaced.position.x = kReplaced;
    path_api::add_key(&path, {.frame = kFree, .pose = replaced, .ease = Ease::kLinear});
    check(path.keys.size() == kKeys.size() && path.keys.at(2).pose.position.x == kReplaced,
          "add at a key's frame replaces it");
    path_api::delete_key(&path, 0);
    check(path.keys.size() == kKeys.size() - 1 && path.keys.front().frame == kTaken, "delete");
}

// The split comparison's crop: the left part of the viewport maps the whole's left half of normalised device coordinates
// onto -1..1, the right part its right half (y untouched for full-height parts).
struct Ndc {
    double x;
    double y;
};
Ndc cropped(const std::array<float, 4>& crop, Ndc point)
{
    return {.x = (point.x * std::get<0>(crop)) + std::get<2>(crop), .y = (point.y * std::get<1>(crop)) + std::get<3>(crop)};
}

void check_crop()
{
    constexpr float kHalf = 0.5F;
    constexpr double kAnyY = 0.3;
    const std::array<float, 4> left = render::rt::crop_clip_space({0.0F, 0.0F, kHalf, 1.0F});
    const std::array<float, 4> right = render::rt::crop_clip_space({kHalf, 0.0F, kHalf, 1.0F});
    check(close_to(cropped(left, {.x = -1.0, .y = kAnyY}).x, -1.0, kExact) &&
              close_to(cropped(left, {.x = 0.0, .y = kAnyY}).x, 1.0, kExact) &&
              close_to(cropped(left, {.x = 0.0, .y = kAnyY}).y, kAnyY, kExact),
          "crop: the left half");
    check(close_to(cropped(right, {.x = 0.0, .y = -1.0}).x, -1.0, kExact) &&
              close_to(cropped(right, {.x = 1.0, .y = -1.0}).x, 1.0, kExact) &&
              close_to(cropped(right, {.x = 1.0, .y = -1.0}).y, -1.0, kExact),
          "crop: the right half");
    // a part 30% wide from 70% across, the top quarter (y down on the screen: up in clip space) - its edges at x 0.4
    // and 1, y 1 and 0.5 of the whole
    constexpr std::array<float, 4> kCorner = {0.7F, 0.0F, 0.3F, 0.25F};
    constexpr double kLeftEdge = 0.4;
    constexpr double kBottomEdge = 0.5;
    constexpr double kCornerTolerance = 1e-5;
    const std::array<float, 4> part = render::rt::crop_clip_space(kCorner);
    check(close_to(cropped(part, {.x = kLeftEdge, .y = 1.0}).x, -1.0, kCornerTolerance) &&
              close_to(cropped(part, {.x = 1.0, .y = 1.0}).x, 1.0, kCornerTolerance) &&
              close_to(cropped(part, {.x = kLeftEdge, .y = 1.0}).y, 1.0, kCornerTolerance) &&
              close_to(cropped(part, {.x = kLeftEdge, .y = kBottomEdge}).y, -1.0, kCornerTolerance),
          "crop: a corner part");
    const std::array<float, 4> whole = render::rt::crop_clip_space({0.0F, 0.0F, 1.0F, 1.0F});
    check(whole == std::array<float, 4>{1.0F, 1.0F, 0.0F, 0.0F}, "crop: the whole is no crop");
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_keys_hit();
    check_continuity();
    check_yaw_wrap();
    check_rows();
    check_sidecar();
    check_key_edits();
    check_crop();
    std::printf("thug2_viewer_check: %s (%d failure%s)\n", failures() == 0 ? "ok" : "FAILED", failures(),
                failures() == 1 ? "" : "s");
    return failures() == 0 ? 0 : 1;
}
