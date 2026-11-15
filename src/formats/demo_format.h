#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "formats/sha256.h"

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
//   RATE  (version 2, written 2026-11-14 - 15 only) u32: the ticks a second the demo was recorded at, by builds that
//         could simulate faster than the original's 60 (since removed: the game simulates at exactly 60 -
//         docs/design/simulation-rate.md). Read: 60 plays as any demo; another rate is refused (its input frames are
//         that rate's ticks, not the game's). Never written: every demo is a version 1 file.
//   OPTS  (since 2026-11-15; optional) the player's options that change a run: u8 auto kick, u8 spin taps (the skate
//         module's player settings, port 0), f32 the stat override (SetStatOverride; 0: none). A playback lends them
//         with the skater and gives the player's back at its end.
//   SEAL  (since 2026-11-15; optional, written last) what shows a demo changed after it was recorded: u32 seal
//         version (1), the recorded skater's profile hash (32 bytes: profile_hash below), the start (32 bytes), u32
//         link stride (frames), u32 link count, the links (32 bytes each), the end (32 bytes). SHA-256 throughout:
//           start      = H("THUG2DEM seal 1", u32 file version, every chunk before SEAL but INPT and CHEK as written:
//                        u32 tag, u32 size, body)
//           link(f+1)  = H(link(f), u32 f, frame f's port data (each port), the check points of frame f as CHEK
//                        has them, in its order); link(0) = start
//           links      = link(stride), link(2 stride)... and link(frames) last (ceil(frames / stride) of them)
//           end        = H(link(frames), u32 count, the check points not taken by a frame, in CHEK's order)
//         Anyone with this source can seal an edited file again: the seal shows a file changed by hand, not who made
//         it (docs/demos.md "Replays as proof").
namespace demo_format {

constexpr std::array<char, 8> kMagic = {'T', 'H', 'U', 'G', '2', 'D', 'E', 'M'};
constexpr uint32_t kVersion = 1;       // what this game writes
constexpr uint32_t kNewestVersion = 2; // the newest it reads (version 2: a RATE chunk)
constexpr uint32_t kTickRate = 60;     // the simulation's ticks a second (a RATE chunk must say this)
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
// - kMusicApart: the music's shuffles drew from streams of their own while it recorded - every recording since
//   2026-11-13; since 2026-11-15 the music never draws the game's random numbers (audio/music_player.h), so a playback
//   always does so. Without it the music's draws were the game's (as when a session's first level loaded): such a demo
//   may part where the music changed track, and its playback says so.
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

// The player's options that change a run (the OPTS chunk).
struct Options {
    bool present = false; // (a demo made before 2026-11-15: none - played with the session's)
    bool auto_kick = false;
    bool spin_taps = false;
    float stat_override = 0.0F;
    bool operator==(const Options&) const = default;
};

// The seal, as read_demo found it (write_demo seals every file it writes; this field isn't written).
enum class SealState : uint8_t {
    kNone,    // no SEAL chunk (a demo made before 2026-11-15)
    kIntact,  // every link as recorded
    kChanged, // something differs: changed after it was recorded
};
constexpr uint32_t kSealVersion = 1;
constexpr uint32_t kSealStride = 600; // a link every 10 s
struct Seal {
    SealState state = SealState::kNone;
    sha256::Digest profile_hash{}; // the recorded skater's, as the seal holds it
    bool header_changed = false;   // the names, date, build, level, settings, skater or session chunks
    bool profile_changed = false;  // the skater's profile or info (PROF, DETL) against the hash
    int64_t changed_from = -1;     // the first frame of the first stretch whose input or check points differ (-1: none)
    int64_t changed_to = -1;       // (the stretch's end, exclusive)
    bool end_changed = false;      // the check points after the last frame, or the seal's own layout
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
    std::vector<Global> globals; // (none: a demo made before the GLOB chunk)
    Options options;
    Seal seal; // (read_demo's finding)
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

// The file's bytes (sealed: the SEAL chunk last).
std::vector<uint8_t> write_demo(const Demo& demo);
// The recorded skater's identity: H("PROF", u32 size, the profile's bytes, "DETL", u32 size, the info's bytes).
sha256::Digest profile_hash(const Demo& demo);
// What the seal says, in a few words: "intact", "none (recorded before seals)", "changed after recording: ...".
std::string seal_text(const Seal& seal);
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
