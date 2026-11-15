// NOT ORIGINAL - thug2_format_check's checks of the demo files (format_check_demo.h).

#include "tests/format_check_demo.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "formats/demo_format.h"
#include "formats/sha256.h"

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
constexpr uint32_t kGlobalName = 0x08B6D03A; // (Car_CheckForSkater_Pause)
constexpr uint32_t kGlobalWord = 5;
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
// the seal
constexpr std::size_t kChunkHeader = 8;
constexpr std::size_t kMillion = 1000000;
constexpr float kStatOverride = 13.0F;
constexpr std::size_t kInputRunData = 8 + 4 + 2; // (ports, frames, the first run's count, then its data's byte 2)
constexpr std::size_t kFirstPointPosition = 4 + 4 + 4; // (count, frame, flags)
constexpr std::size_t kSkaterNameLastLetter = 4 + 2 + 4 + 8; // ("BO", then "Tony Hawk"'s last letter)
constexpr uint32_t kLongFrames = 1500;
constexpr uint32_t kLaterFrame = 1000;
constexpr uint32_t kLaterStretch = 600; // (the link holding frame 1000: 600 - 1199)
constexpr uint8_t kOtherByte = 0xEE;
constexpr uint32_t kTinyFrames = 3;
constexpr std::string_view kTinyHex =
    "544855473244454d01000000494e464f7100000002000000424f09000000546f6e79204861776b13000000323032362d3131"
    "2d31352031323a30303a3030070000006162633132333478563412f0debc9aeeffc000000000000000000003000000010000"
    "00010000000000000000000000000000000000000000000000000000000000000053455453040000000000000050524f4605"
    "00000001020300ff4445544c03000000090807494e5054740000000100000003000000010000008080008080808080808080"
    "8080808080808080808080808080808080808080800100000080800180808080808080808080808080808080808080808080"
    "808080808080800100000080800280808080808080808080808080808080808080808080808080808080804348454b580000"
    "000100000001000000030000000000803f00000040000080bf0000803f0000003f000080be00000000000000000000803f00"
    "0000000000803f00000000010000000000803f0000ca42008095c300000000cdcc4cbe48e17a3f4f50545306000000010000"
    "0000005345414c8c000000010000009f7cf6ce970dc05eb4a937646d61110f4b2f9e1c9173a7c95c7611c4ca8795cda891aa"
    "9b06be9cf6e26651bc6ed9d2e9ac4f608570a62adeba3c54e2c0a8679e5802000001000000d7143dbbb96c17239f2dfc20fa"
    "4dd7f8f1b837328307b2492e2e0945b75823b55fe40407233c8928610d6f047b75f757d447a0fa9b9172b90b28dfc96cc8e2"
    "03";

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
    demo.random_flags = demo_format::kMusicApart;
    demo.globals = {{.name = kGlobalName, .kind = 1, .word = kGlobalWord}, {.name = kSkaterName, .kind = 2, .word = kCrtSeed}};
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
           a.clocks.offset == b.clocks.offset && a.random_flags == b.random_flags &&
           a.globals == b.globals;
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
    demo_format::Demo no_random = demo;
    no_random.random_flags = 0;
    demo_format::Demo back_no_random;
    check(demo_format::read_demo(demo_format::write_demo(no_random), &back_no_random, &error) &&
              back_no_random.random_flags == 0,
          "demo: the random chunk is optional (a demo made before it: no flags)");
    demo_format::Demo no_globals = demo;
    no_globals.globals.clear();
    demo_format::Demo back_no_globals;
    check(demo_format::read_demo(demo_format::write_demo(no_globals), &back_no_globals, &error) &&
              back_no_globals.globals.empty(),
          "demo: the globals are optional");
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

    // A version 2 demo (written 2026-11-14 - 15 by builds with a faster tick): its RATE chunk at 60 plays as any demo,
    // at another rate it is refused.
    for (const uint32_t rate : {demo_format::kTickRate, uint32_t{120}}) {
        std::vector<uint8_t> rated = bytes;
        put_u32(&rated, kVersionOffset, demo_format::kNewestVersion);
        const std::vector<uint8_t> rate_chunk = {'R', 'A', 'T', 'E', 4, 0, 0, 0, static_cast<uint8_t>(rate), 0, 0, 0};
        rated.insert(rated.end(), rate_chunk.begin(), rate_chunk.end());
        const bool read = demo_format::read_demo(rated, &back, &error);
        if (rate == demo_format::kTickRate) {
            check(read && same_demos(back, sample()), "demo: version 2 with a RATE of 60 read (" + error + ")");
        } else {
            check(!read && error.find("ticks a second") != std::string::npos,
                  "demo: version 2 with a RATE of 120 refused (" + error + ")");
        }
    }

    // A newer version: refused.
    std::vector<uint8_t> newer = bytes;
    put_u32(&newer, kVersionOffset, demo_format::kNewestVersion + 1);
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
    demo_format::CheckPoint undrawn = point_at(1);
    undrawn.flags &= ~demo_format::kHasCamera;
    undrawn.camera = {};
    undrawn.camera_forward = {};
    const demo_format::Difference no_camera = demo_format::compare_points(point_at(1), undrawn, kTolerance);
    check(!no_camera.other && no_camera.camera == 0.0F, "drift: a frame one run didn't draw (no camera) isn't a difference");
    check(demo_format::check_line(point_at(kLineFrame)).starts_with("2 3 2.000 4.000 -2.000"), "drift: a track line");
}

// The offset of the first chunk tagged `tag` in a demo's bytes (its tag), or the size if none.
std::size_t chunk_at(const std::vector<uint8_t>& bytes, std::string_view tag)
{
    std::size_t at = kInfoTagOffset;
    while (at + kChunkHeader <= bytes.size()) {
        if (std::equal(tag.begin(), tag.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at))) {
            return at;
        }
        uint32_t size = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            size |= static_cast<uint32_t>(core::at(bytes, at + 4 + i)) << (kBitsPerByte * i);
        }
        at += kChunkHeader + size;
    }
    return bytes.size();
}

std::string hex_of(std::span<const uint8_t> bytes)
{
    std::string out;
    for (const uint8_t byte : bytes) {
        out += text::format("%02x", static_cast<unsigned>(byte));
    }
    return out;
}

// The tiny demo whose bytes tools/game/verify_replay.test.ts holds (kTinyHex): the same seal in C++ and TypeScript.
demo_format::Demo tiny()
{
    demo_format::Demo demo;
    demo.level = "BO";
    demo.skater = "Tony Hawk";
    demo.created = "2026-11-15 12:00:00";
    demo.build = "abc1234";
    demo.level_load = kLevelLoad;
    demo.skater_name = kSkaterName;
    demo.seed = kSeed;
    demo.ports = 1;
    demo.profile.assign(kProfile.begin(), kProfile.end());
    demo.details.assign(kDetails.begin(), kDetails.end());
    demo.options = {.present = true, .auto_kick = true, .spin_taps = false, .stat_override = 0.0F};
    for (uint32_t frame = 0; frame < kTinyFrames; ++frame) {
        demo_format::PortData data{};
        data.fill(kStickCentred);
        core::at(data, kChangingByte) = static_cast<uint8_t>(frame);
        demo.inputs.push_back(data);
    }
    demo.checks.push_back(point_at(1));
    return demo;
}

// The seal: a written demo reads back intact; a byte changed in the input, the header or the skater is found (and
// where); a demo without a seal says so; the options round-trip; SHA-256's own test vectors.
void check_seal()
{
    constexpr std::array<uint8_t, 3> kAbc = {'a', 'b', 'c'};
    check(sha256::hex(sha256::of(kAbc)) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "seal: SHA-256 of \"abc\"");
    check(sha256::hex(sha256::of({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "seal: SHA-256 of nothing");
    std::vector<uint8_t> million(kMillion, 'a');
    check(sha256::hex(sha256::of(million)) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "seal: SHA-256 of a million a's");

    demo_format::Demo demo = sample();
    demo.options = {.present = true, .auto_kick = false, .spin_taps = true, .stat_override = kStatOverride};
    const std::vector<uint8_t> bytes = demo_format::write_demo(demo);
    demo_format::Demo back;
    std::string error;
    check(demo_format::read_demo(bytes, &back, &error) && back.seal.state == demo_format::SealState::kIntact,
          "seal: a written demo is intact (" + demo_format::seal_text(back.seal) + ")");
    check(back.options == demo.options, "seal: the options read back");
    check(back.seal.profile_hash == demo_format::profile_hash(demo), "seal: the skater's profile hash");

    // the input of frame 250 changed (its run's data byte): found in frames 0 - 299 (one link: 300 frames)
    std::vector<uint8_t> input = bytes;
    const std::size_t input_at = chunk_at(input, "INPT");
    core::at(input, input_at + kChunkHeader + kInputRunData) ^= 1U;
    check(demo_format::read_demo(input, &back, &error) && back.seal.state == demo_format::SealState::kChanged &&
              back.seal.changed_from == 0 && !back.seal.header_changed && !back.seal.profile_changed,
          "seal: a changed input found (" + demo_format::seal_text(back.seal) + ")");
    // a check point's position changed
    std::vector<uint8_t> point = bytes;
    const std::size_t checks_at = chunk_at(point, "CHEK");
    core::at(point, checks_at + kChunkHeader + kFirstPointPosition) ^= 1U;
    check(demo_format::read_demo(point, &back, &error) && back.seal.state == demo_format::SealState::kChanged &&
              back.seal.changed_from >= 0,
          "seal: a changed check point found (" + demo_format::seal_text(back.seal) + ")");
    // the skater's name in INFO ("Tony Hawk" -> "Tony Hawl")
    std::vector<uint8_t> header = bytes;
    const std::size_t info_at = chunk_at(header, "INFO");
    core::at(header, info_at + kChunkHeader + kSkaterNameLastLetter) = 'l';
    check(demo_format::read_demo(header, &back, &error) && back.skater == "Tony Hawl" &&
              back.seal.state == demo_format::SealState::kChanged && back.seal.header_changed,
          "seal: a changed header found (" + demo_format::seal_text(back.seal) + ")");
    // the skater's profile
    std::vector<uint8_t> profile = bytes;
    core::at(profile, chunk_at(profile, "PROF") + kChunkHeader) ^= 1U;
    check(demo_format::read_demo(profile, &back, &error) && back.seal.state == demo_format::SealState::kChanged &&
              back.seal.profile_changed,
          "seal: a changed skater found (" + demo_format::seal_text(back.seal) + ")");
    // a longer demo: the stretch named
    demo_format::Demo longer = sample();
    for (uint32_t frame = kFrames; frame < kLongFrames; ++frame) {
        longer.inputs.push_back(longer.inputs.back());
    }
    core::at(core::at(longer.inputs, kLaterFrame), kChangingByte) = kOtherByte;
    std::vector<uint8_t> long_changed = demo_format::write_demo(longer);
    // (the input chunk of the same demo with that frame's byte another value put into the sealed file: the same
    // length - one run's byte differs)
    core::at(core::at(longer.inputs, kLaterFrame), kChangingByte) = kOtherByte + 1;
    const std::vector<uint8_t> resealed = demo_format::write_demo(longer);
    const std::size_t from = chunk_at(resealed, "INPT");
    const std::size_t to = chunk_at(resealed, "CHEK");
    std::copy(resealed.begin() + static_cast<std::ptrdiff_t>(from), resealed.begin() + static_cast<std::ptrdiff_t>(to),
              long_changed.begin() + static_cast<std::ptrdiff_t>(from));
    check(demo_format::read_demo(long_changed, &back, &error) && back.seal.changed_from == kLaterStretch &&
              back.seal.changed_to == kLaterStretch + demo_format::kSealStride,
          "seal: the changed stretch named (" + demo_format::seal_text(back.seal) + ")");
    // the seal cut off (the chunk removed): no seal
    std::vector<uint8_t> unsealed = bytes;
    unsealed.resize(chunk_at(unsealed, "SEAL"));
    check(demo_format::read_demo(unsealed, &back, &error) && back.seal.state == demo_format::SealState::kNone,
          "seal: a demo without one says so");
    // the tiny demo, byte for byte as verify_replay.test.ts has it
    const std::string tiny_hex = hex_of(demo_format::write_demo(tiny()));
    check(tiny_hex == kTinyHex, "seal: the tiny demo's bytes as tools/game/verify_replay.test.ts has them:\n" + tiny_hex);
}

} // namespace

int run()
{
    text::print("demo files\n");
    check_round_trip();
    check_versions();
    check_drift();
    check_seal();
    return failures();
}

} // namespace format_check_demo
