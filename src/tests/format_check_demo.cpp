// NOT ORIGINAL - thug2_format_check's checks of the demo files (format_check_demo.h).

#include "tests/format_check_demo.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "formats/demo_format.h"

namespace format_check_demo {

namespace {

constexpr uint32_t kFrames = 300;
constexpr uint32_t kChangeEvery = 7;     // the sample's input changes this often (runs to compress)
constexpr std::size_t kVersionOffset = 8; // the u32 after the magic
constexpr std::size_t kInfoTagOffset = 12;

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
    const auto f = static_cast<float>(frame);
    point.position = {f, f * 2.0F, -f};
    point.velocity = {1.0F, 0.5F, -0.25F};
    point.at = {0.0F, 0.0F, 1.0F};
    point.up = {0.0F, 1.0F, 0.0F};
    point.state = static_cast<int32_t>(frame % 6);
    point.camera = {f, f + 100.0F, f - 300.0F};
    point.camera_forward = {0.0F, -0.2F, 0.98F};
    return point;
}

demo_format::Demo sample()
{
    demo_format::Demo demo;
    demo.level = "BO";
    demo.skater = "Tony Hawk";
    demo.created = "2026-11-13 12:00:00";
    demo.build = "abc1234";
    demo.level_load = 0x12345678;
    demo.skater_name = 0x9ABCDEF0;
    demo.seed = 0xC0FFEE;
    demo.crt_seed = 0xBEEF;
    demo.up_frame = 120;
    demo.ports = 1;
    demo.check_stride = 1;
    demo.random_at_up = {.streams = {{1, 2, 3, 4, 5, 6}}, .crt = 7};
    demo.settings = {{"Graphics.rt_shadows", "1"}, {"features.mute_audio", "1"}, {"Graphics.empty", ""}};
    demo.profile = {1, 2, 3, 0, 255};
    demo.details = {9, 8, 7};
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        demo_format::PortData data{};
        data.fill(0x80);
        core::at(data, 2) = static_cast<uint8_t>(frame / kChangeEvery);
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
           a.profile == b.profile && a.details == b.details && a.inputs == b.inputs;
}

void put_u32(std::vector<uint8_t>* bytes, std::size_t at, uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i) {
        core::at(*bytes, at + i) = static_cast<uint8_t>(value >> (8U * i));
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
    check(demo_format::write_demo(no_checks).size() < static_cast<std::size_t>(kFrames) * demo_format::kPortBytes / 4,
          "demo: the input compressed");
    check(back.check_at(200) != nullptr && back.check_at(200)->frame == 200, "demo: a check point by frame");
    check(back.check_at(5) == nullptr, "demo: no check point before the level was up");
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
    for (uint32_t frame = 0; frame < 10; ++frame) {
        const demo_format::CheckPoint recorded = point_at(frame);
        demo_format::add_to_drift(&same, recorded, recorded, 0.01F);
        demo_format::CheckPoint played = recorded;
        if (frame >= 4) {
            played.position[0] += 0.5F * static_cast<float>(frame - 3);
        }
        demo_format::add_to_drift(&moved, recorded, played, 0.01F);
    }
    check(same.compared == 10 && same.first_frame == -1 && same.max_skater == 0.0F, "drift: identical tracks");
    check(moved.first_frame == 4 && moved.first.skater == 0.5F && moved.max_skater == 3.0F && moved.max_camera == 0.0F,
          "drift: the first frame that differs and the largest drift");
    demo_format::CheckPoint other_state = point_at(1);
    other_state.state = 5;
    check(demo_format::compare_points(point_at(1), other_state, 0.01F).other, "drift: a different state counts");
    check(demo_format::check_line(point_at(2)).starts_with("2 3 2.000 4.000 -2.000"), "drift: a track line");
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
