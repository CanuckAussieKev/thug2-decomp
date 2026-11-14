#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

// NOT ORIGINAL - the demo files (ours, added at the user's request: a run recorded and played back, rendered under
// different settings for before / after pictures - devtools/demo.h, docs/demos.md). The game never had them.
//
// A file (little-endian): the magic "THUG2DEM", u32 version (kVersion), then chunks {4-character tag, u32 size, size
// bytes} up to the end. A reader skips a chunk it doesn't know (a later version's additions); a file of a newer
// version than kVersion is refused (its chunks may mean something else), as is one whose chunks run past the end or
// that lacks INFO or INPT.
//
//   INFO  strings (u32 length + bytes): level (the load script without Load_), skater (display name), created (local
//         time), build (the exe's commit); u32: the level's load script checksum, the skater profile's name checksum,
//         the seed both random streams got at the load, the C runtime rand's state at the load, the frame the level
//         was up (the check track and a capture start there), frames (the input frames), ports (1-2), the check
//         track's stride; the two random streams' state (6 x u32: add, mul, value each) and the C runtime rand's at
//         the frame the level was up.
//   SETS  u32 count, then key / value strings: the settings at record time ("Graphics.rt_shadows" = "1").
//   PROF  bytes: the skater profile's network copy (appearance, trick style, specials, info names); DETL: its whole
//         info structure (names, stats...) in the script structure buffer format. Both opaque here.
//   INPT  u32 ports, u32 frames, then for each port runs {u32 count, kPortBytes bytes}: the port's control data (its
//         PS2 format, buttons down as set bits, before the poll inverts them) for `count` frames in a row.
//   CHEK  u32 count, then check points (kCheckPointBytes each): every `stride` frames from the frame the level was up.
//   CLCK  the clocks when the level was up: u32 x 2 the vblanks since the stopwatch's reset (low, high), u32 the game clock's
//         last change of speed, f32 its speed, i32 its offset. (Optional: a demo without it plays from the clocks as
//         they are.)
//   RATE  (version 2) u32: the tick rate it was recorded at, ticks a second (engine/sys/tick_rate.h). A demo without it -
//         every version 1 file - was recorded at 60. A 60-tick demo is still written as version 1 without it (the same
//         bytes as before; older builds read it); one at another rate is version 2, which older builds refuse.
namespace demo_format {

constexpr std::array<char, 8> kMagic = {'T', 'H', 'U', 'G', '2', 'D', 'E', 'M'};
constexpr uint32_t kVersion = 2;          // the newest this game reads (and writes for a demo not at 60 ticks)
constexpr uint32_t kVersionOriginalRate = 1; // what a 60-tick demo is written as
constexpr uint32_t kOriginalTickRate = 60;
constexpr uint32_t kMaxTickRate = 1000;
constexpr std::size_t kPortBytes = 32; // a port's control data (sio's kPortBufferBytes)
constexpr uint32_t kMaxPorts = 2;

// Where the run is, every `stride` frames (a check point): the skater (position, velocity, the matrix's at and up
// rows, its state - automation's trace: 0 ground, 1 air, 2 wall, 3 lip, 4 rail, 5 wallplant; -1 none) and the camera
// (viewport 0's position and view direction as last drawn).
struct CheckPoint {
    uint32_t frame = 0;
    uint32_t flags = 0; // kHasSkater, kHasCamera, kPaused
    std::array<float, 3> position{};
    std::array<float, 3> velocity{};
    std::array<float, 3> at{};
    std::array<float, 3> up{};
    int32_t state = -1;
    std::array<float, 3> camera{};
    std::array<float, 3> camera_forward{};
};
constexpr uint32_t kHasSkater = 1U << 0;
constexpr uint32_t kHasCamera = 1U << 1;
constexpr uint32_t kPaused = 1U << 2;
constexpr std::size_t kCheckPointBytes = 4 + 4 + (4 * 3 * 4) + 4 + (4 * 3 * 2);

// The random streams (maths::seed_random's two: add, mul, value each) and the C runtime rand's state.
constexpr std::size_t kRandomStreamWords = 6;
struct RandomState {
    std::array<uint32_t, kRandomStreamWords> streams{};
    uint32_t crt = 0;
    bool operator==(const RandomState&) const = default;
};

using PortData = std::array<uint8_t, kPortBytes>;

// The clocks when the level was up (frame_clock: the stopwatch's vblanks, the game clock's state).
struct Clocks {
    bool present = false;
    uint64_t vblanks = 0;
    uint32_t last_change = 0;
    float pace = 1.0F;
    int32_t offset = 0;
};

// How the run's random numbers were kept apart from what a session leaves behind (the RAND chunk; none: 0, a demo made
// before 2026-11-13).
// - kMusicApart: the music's shuffles drew from streams of their own while it recorded (audio::set_music_random_apart),
//   so its playback does too. Without it the music's draws were the game's, as when a session's first level loaded.
constexpr uint32_t kMusicApart = 1U << 0U;

// A script global with a simple value (an integer, a float or a name) as it was when the recording began (the GLOB
// chunk): the counters and switches a session's scripts change (Car_CheckForSkater_Pause...) that a level's scripts
// read.
struct Global {
    uint32_t name = 0;
    uint8_t kind = 0; // script::QbValueKind (1 integer, 2 float, 13 name)
    uint32_t word = 0;
    bool operator==(const Global&) const = default;
};

struct Demo {
    uint32_t version = kVersion;
    std::string level;
    std::string skater;
    std::string created;
    std::string build;
    uint32_t level_load = 0;
    uint32_t skater_name = 0;
    uint32_t seed = 0;
    uint32_t crt_seed = 0;
    uint32_t up_frame = 0;
    uint32_t ports = 1;
    uint32_t check_stride = 1;
    RandomState random_at_up;
    Clocks clocks;
    uint32_t random_flags = 0; // kMusicApart...
    uint32_t tick_rate = kOriginalTickRate; // the ticks a second it was recorded at (the RATE chunk)
    std::vector<Global> globals; // (none: a demo made before the GLOB chunk)
    std::vector<std::pair<std::string, std::string>> settings;
    std::vector<uint8_t> profile;
    std::vector<uint8_t> details;
    // the input frames: frame f's port p at inputs[f * ports + p]
    std::vector<PortData> inputs;
    std::vector<CheckPoint> checks;

    [[nodiscard]] uint32_t frames() const { return ports == 0 ? 0 : static_cast<uint32_t>(inputs.size() / ports); }
    // Frame f's port p (null past the end or for a port not recorded).
    [[nodiscard]] const PortData* input(uint32_t frame, uint32_t port) const;
    // The check point at `frame` (null if none).
    [[nodiscard]] const CheckPoint* check_at(uint32_t frame) const;
    // A setting's value ("" if none).
    [[nodiscard]] std::string setting(const std::string& key) const;
};

// The file's bytes.
std::vector<uint8_t> write_demo(const Demo& demo);
// Read from a file's bytes: false (and why in `error`) for a file that isn't one, a newer version, or a broken one.
bool read_demo(std::span<const uint8_t> bytes, Demo* out, std::string* error);

// A check point as a line of the check track's text (the playback's track file; tools/game/demo_compare.ts reads it):
// frame flags x y z vx vy vz at_x at_y at_z up_x up_y up_z state cam_x cam_y cam_z cam_dx cam_dy cam_dz
std::string check_line(const CheckPoint& point);
constexpr const char* kCheckHeader =
    "# frame flags x y z vx vy vz at_x at_y at_z up_x up_y up_z state cam_x cam_y cam_z cam_dx cam_dy cam_dz";

// Two check points compared: the skater's distance apart, the camera's, and whether anything else differs (state,
// flags, the matrix rows or velocity beyond `tolerance`).
struct Difference {
    float skater = 0.0F;
    float camera = 0.0F;
    bool other = false;
};
Difference compare_points(const CheckPoint& a, const CheckPoint& b, float tolerance);

// A playback's check points against the recorded ones (matched by frame): how many compared, the first frame that
// differs beyond `tolerance` (or -1), its distances, and the largest skater / camera distance over the run.
struct Drift {
    uint32_t compared = 0;
    int64_t first_frame = -1;
    Difference first;
    float max_skater = 0.0F;
    float max_camera = 0.0F;
};
// Adds one more pair (the track's next point) to `drift`.
void add_to_drift(Drift* drift, const CheckPoint& recorded, const CheckPoint& played, float tolerance);

} // namespace demo_format
