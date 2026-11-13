#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// NOT ORIGINAL - the demo viewer's camera path (devtools/demo_viewer.h): keyframes on a demo's timeline - a position,
// a rotation and a field of view at a frame - and the poses between them, the fixed cameras placed in the level, the
// loop region; kept beside the demo as <demo>.camera.txt (versioned text). Pure: no game - thug2_viewer_check tests it.
namespace devtools::camera_path {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    bool operator==(const Vec3&) const = default;
};

// A camera's pose. Angles in degrees: yaw 0 looks along +z, 90 along +x (the screenshot sidecar's heading); pitch up
// positive (kept within +-89); roll turns the picture clockwise as seen. fov: the view's angle as the game's camera
// gives it (the horizontal one, render_pc's) - 0: the game camera's own.
struct Pose {
    Vec3 position;
    float yaw = 0.0F;
    float pitch = 0.0F;
    float roll = 0.0F;
    float fov = 0.0F;
};

// How the camera moves from a key to the next one: smooth (a curve through the keys - Catmull-Rom slopes, so the
// speed carries on through each key), linear, ease (slowing into and out of both keys) or hold (it stays, then cuts).
enum class Ease : uint8_t { kSmooth, kLinear, kEase, kHold };

struct Key {
    int32_t frame = 0; // the demo's frame (60 a second from the level's load)
    Pose pose;
    Ease ease = Ease::kSmooth;
};

// A camera placed in the level (looked through as it is).
struct FixedCamera {
    std::string name;
    Pose pose;
};

struct Path {
    std::vector<Key> keys; // by frame, one key a frame at most
    std::vector<FixedCamera> fixed;
    int32_t loop_in = -1; // the loop region's frames (-1: none)
    int32_t loop_out = -1;
};

inline constexpr int kVersion = 1;
inline constexpr float kMostPitch = 89.0F;

// A key put on the path (one already at its frame replaced): its index.
std::size_t add_key(Path* path, const Key& key);
// A key moved to another frame (false, nothing moved: a key is there, or no such index); `index` follows it.
bool move_key(Path* path, std::size_t* index, int32_t frame);
void delete_key(Path* path, std::size_t index);
// The key at a frame (-1: none).
int key_at(const Path& path, int32_t frame);

// The pose at a frame (fractions between frames too): before the first key the first's, after the last the last's; false
// with no keys.
bool pose_at(const Path& path, double frame, Pose* out);

// A pose's camera rows (the game's convention: right, up, back - the view looks along -back).
struct Rows {
    Vec3 right;
    Vec3 up;
    Vec3 back;
};
Rows rows_of(const Pose& pose);
// Where a pose looks (a unit vector: -back).
Vec3 forward_of(const Pose& pose);
// The yaw and pitch that look along `forward` (any length; `found` false for a zero one - then both 0).
struct Angles {
    float yaw = 0.0F;
    float pitch = 0.0F;
    bool found = false;
};
Angles angles_toward(const Vec3& forward);
// `pose` turned to look at `target`.
void look_at(Pose* pose, const Vec3& target);

// The sidecar's text and back (`why`: what is wrong - a newer version, a bad line; lines of kinds it doesn't know are
// skipped).
std::string write_text(const Path& path);
bool read_text(const std::string& text, Path* out, std::string* why);

const char* ease_name(Ease ease);

} // namespace devtools::camera_path
