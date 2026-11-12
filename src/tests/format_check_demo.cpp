// NOT ORIGINAL - thug2_format_check's checks of the demo files (format_check_demo.h).

#include "tests/format_check_demo.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "formats/demo_format.h"

namespace format_check_demo {

namespace {

constexpr uint32_t kFrames = 300;
constexpr std::size_t kBitsPerByte = 8;
constexpr uint32_t kChangeEvery = 7;      // the sample's input changes this often (runs to compress)
constexpr std::size_t kVersionOffset = 8; // the u32 after the magic
constexpr std::size_t kInfoTagOffset = 12;
// the sample demo's values
constexpr uint32_t kLevelLoad = 0x12345678;
constexpr uint32_t kSkaterName = 0x9ABCDEF0;
constexpr uint32_t kSeed = 0xC0FFEE;
constexpr uint32_t kCrtSeed = 0xBEEF;
constexpr uint32_t kUpFrame = 120;
constexpr std::array<uint32_t, demo_format::kRandomStreamWords> kStreams = {{11, 22, 33, 44, 55, 66}};
constexpr uint32_t kCrtState = 77;
constexpr demo_format::Clocks kClocks = {
    .present = true, .vblanks = 0x123456789ULL, .last_change = 4000, .pace = 0.5F, .offset = -12};
constexpr std::array<uint8_t, 5> kProfile = {{1, 2, 3, 0, 255}};
constexpr std::array<uint8_t, 3> kDetails = {{9, 8, 7}};
constexpr uint8_t kStickCentred = 0x80;
constexpr std::size_t kChangingByte = 2;
constexpr uint32_t kStates = 6;
constexpr std::array<float, 3> kVelocity = {{1.0F, 0.5F, -0.25F}};
constexpr std::array<float, 3> kAt = {{0.0F, 0.0F, 1.0F}};
constexpr std::array<float, 3> kUp = {{0.0F, 1.0F, 0.0F}};
constexpr std::array<float, 3> kCameraOffset = {{0.0F, 100.0F, -300.0F}};
constexpr std::array<float, 3> kCameraForward = {{0.0F, -0.2F, 0.98F}};
constexpr float kHeightScale = 2.0F;
constexpr uint32_t kSomeCheckedFrame = 200;
constexpr uint32_t kFrameBeforeUp = 5;
constexpr std::size_t kRatio = 4; // the compressed input at most a quarter of the raw frames
// the drift check
constexpr uint32_t kDriftFrames = 10;
constexpr uint32_t kDriftFrom = 4;
constexpr float kTolerance = 0.01F;
constexpr float kDriftStep = 0.5F;
constexpr float kDriftLargest = kDriftStep * static_cast<float>(kDriftFrames - kDriftFrom);
constexpr int32_t kOtherState = 5;
constexpr uint32_t kLineFrame = 2;

int& failures()
{
    static int failures{0};
    return failures;
}

void check(bool condition, const std::string& what)
{
    if (!condition) {
        ++failures();
        text::print("  FAIL %s\n", what.c_str());
    }
}

demo_format::CheckPoint point_at(uint32_t frame)
{
    demo_format::CheckPoint point;
    point.frame = frame;
    point.flags = demo_format::kHasSkater | demo_format::kHasCamera;
    const auto along = static_cast<float>(frame);
    point.position = {along, along * kHeightScale, -along};
    point.velocity = kVelocity;
    point.at = kAt;
    point.up = kUp;
    point.state = static_cast<int32_t>(frame % kStates);
    point.camera = {along + core::at(kCameraOffset, 0), along + core::at(kCameraOffset, 1),
                    along + core::at(kCameraOffset, 2)};
    point.camera_forward = kCameraForward;
    return point;
}

demo_format::Demo sample()
{
    demo_format::Demo demo;
    demo.level = "BO";
    demo.skater = "Tony Hawk";
    demo.created = "2026-11-13 12:00:00";
    demo.build = "abc1234";
    demo.level_load = kLevelLoad;
    demo.skater_name = kSkaterName;
    demo.seed = kSeed;
    demo.crt_seed = kCrtSeed;
    demo.up_frame = kUpFrame;
    demo.ports = 1;
    demo.check_stride = 1;
    demo.random_at_up = {.streams = kStreams, .crt = kCrtState};
    demo.clocks = kClocks;
    demo.settings = {{"Graphics.rt_shadows", "1"}, {"features.mute_audio", "1"}, {"Graphics.empty", ""}};
    demo.profile.assign(kProfile.begin(), kProfile.end());
    demo.details.assign(kDetails.begin(), kDetails.end());
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        demo_format::PortData data{};
        data.fill(kStickCentred);
        core::at(data, kChangingByte) = static_cast<uint8_t>(frame / kChangeEvery);
        demo.inputs.push_back(data);
    }
    for (uint32_t frame = demo.up_frame; frame < kFrames; ++frame) {
        demo.checks.push_back(point_at(frame));
    }
    return demo;
}

bool same_points(const demo_format::CheckPoint& a, const demo_format::CheckPoint& b)
{
    return a.frame == b.frame && a.flags == b.flags && a.position == b.position && a.velocity == b.velocity &&
           a.at == b.at && a.up == b.up && a.state == b.state && a.camera == b.camera &&
           a.camera_forward == b.camera_forward;
}

bool same_demos(const demo_format::Demo& a, const demo_format::Demo& b)
{
    if (a.checks.size() != b.checks.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.checks.size(); ++i) {
        if (!same_points(core::at(a.checks, i), core::at(b.checks, i))) {
            return false;
        }
    }
    return a.level == b.level && a.skater == b.skater && a.created == b.created && a.build == b.build &&
           a.level_load == b.level_load && a.skater_name == b.skater_name && a.seed == b.seed &&
           a.crt_seed == b.crt_seed && a.up_frame == b.up_frame && a.ports == b.ports &&
           a.check_stride == b.check_stride && a.random_at_up == b.random_at_up && a.settings == b.settings &&
           a.profile == b.profile && a.details == b.details && a.inputs == b.inputs &&
           a.clocks.present == b.clocks.present && a.clocks.vblanks == b.clocks.vblanks &&
           a.clocks.last_change == b.clocks.last_change && a.clocks.pace == b.clocks.pace &&
           a.clocks.offset == b.clocks.offset;
}

void put_u32(std::vector<uint8_t>* bytes, std::size_t at, uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i) {
        core::at(*bytes, at + i) = static_cast<uint8_t>(value >> (kBitsPerByte * i));
    }
}

void check_round_trip()
{
    const demo_format::Demo demo = sample();
    const std::vector<uint8_t> bytes = demo_format::write_demo(demo);
    demo_format::Demo back;
    std::string error;
    check(demo_format::read_demo(bytes, &back, &error), "demo: read back (" + error + ")");
    check(same_demos(demo, back), "demo: every field read back as written");
    check(back.frames() == kFrames, "demo: the frame count");
    check(back.version == demo_format::kVersion, "demo: the version");
    check(demo_format::write_demo(back) == bytes, "demo: written again, byte for byte");
    // (the input runs: 300 frames changing every 7 - 43 runs of 36 bytes, far less than 300 x 32)
    demo_format::Demo no_checks = demo;
    no_checks.checks.clear();
    check(demo_format::write_demo(no_checks).size() <
              static_cast<std::size_t>(kFrames) * demo_format::kPortBytes / kRatio,
          "demo: the input compressed");
    demo_format::Demo no_clocks = demo;
    no_clocks.clocks = {};
    demo_format::Demo back_no_clocks;
    check(demo_format::read_demo(demo_format::write_demo(no_clocks), &back_no_clocks, &error) &&
              !back_no_clocks.clocks.present,
          "demo: the clocks are optional");
    check(back.check_at(kSomeCheckedFrame) != nullptr && back.check_at(kSomeCheckedFrame)->frame == kSomeCheckedFrame,
          "demo: a check point by frame");
    check(back.check_at(kFrameBeforeUp) == nullptr, "demo: no check point before the level was up");
    check(back.setting("Graphics.rt_shadows") == "1" && back.setting("nothing").empty(), "demo: a setting by name");
    check(back.input(kFrames, 0) == nullptr && back.input(0, 1) == nullptr, "demo: no input past the end / port");
}

void check_versions()
{
    const std::vector<uint8_t> bytes = demo_format::write_demo(sample());
    std::string error;

    // A later version's chunk (an unknown tag) after the known ones: skipped.
    std::vector<uint8_t> extended = bytes;
    const std::vector<uint8_t> chunk = {'X', 'T', 'R', 'A', 3, 0, 0, 0, 1, 2, 3};
    extended.insert(extended.end(), chunk.begin(), chunk.end());
    demo_format::Demo back;
    check(demo_format::read_demo(extended, &back, &error) && same_demos(back, sample()),
          "demo: an unknown chunk skipped (" + error + ")");

    // A newer version: refused.
    std::vector<uint8_t> newer = bytes;
    put_u32(&newer, kVersionOffset, demo_format::kVersion + 1);
    check(!demo_format::read_demo(newer, &back, &error) && error.find("newer") != std::string::npos,
          "demo: a newer version refused (" + error + ")");

    // Version 0, a wrong magic, a cut file, a chunk past the end, no INFO: refused.
    std::vector<uint8_t> zero = bytes;
    put_u32(&zero, kVersionOffset, 0);
    check(!demo_format::read_demo(zero, &back, &error), "demo: version 0 refused");
    std::vector<uint8_t> magic = bytes;
    core::at(magic, 0) = 'X';
    check(!demo_format::read_demo(magic, &back, &error), "demo: a wrong magic refused");
    for (const std::size_t cut : {std::size_t{4}, std::size_t{11}, bytes.size() / 2, bytes.size() - 1}) {
        check(!demo_format::read_demo(std::span(bytes).first(cut), &back, &error),
              text::format("demo: cut to %zu bytes refused", cut));
    }
    std::vector<uint8_t> no_info = bytes;
    core::at(no_info, kInfoTagOffset) = 'Z'; // (INFO becomes an unknown chunk)
    check(!demo_format::read_demo(no_info, &back, &error) && error == "no info chunk", "demo: no INFO refused");
}

void check_drift()
{
    demo_format::Drift same;
    demo_format::Drift moved;
    for (uint32_t frame = 0; frame < kDriftFrames; ++frame) {
        const demo_format::CheckPoint recorded = point_at(frame);
        demo_format::add_to_drift(&same, recorded, recorded, kTolerance);
        demo_format::CheckPoint played = recorded;
        if (frame >= kDriftFrom) {
            core::at(played.position, 0) += kDriftStep * static_cast<float>(frame - kDriftFrom + 1);
        }
        demo_format::add_to_drift(&moved, recorded, played, kTolerance);
    }
    check(same.compared == kDriftFrames && same.first_frame == -1 && same.max_skater == 0.0F,
          "drift: identical tracks");
    check(std::cmp_equal(moved.first_frame, kDriftFrom) && moved.first.skater == kDriftStep &&
              moved.max_skater == kDriftLargest && moved.max_camera == 0.0F,
          "drift: the first frame that differs and the largest drift");
    demo_format::CheckPoint other_state = point_at(1);
    other_state.state = kOtherState;
    check(demo_format::compare_points(point_at(1), other_state, kTolerance).other, "drift: a different state counts");
    check(demo_format::check_line(point_at(kLineFrame)).starts_with("2 3 2.000 4.000 -2.000"), "drift: a track line");
}

} // namespace

int run()
{
    text::print("demo files\n");
    check_round_trip();
    check_versions();
    check_drift();
    return failures();
}

} // namespace format_check_demo
