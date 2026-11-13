// NOT ORIGINAL - the demo viewer's camera path (demo_camera_path.h).

#include "devtools/demo_camera_path.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <iterator>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"

namespace devtools::camera_path {

namespace {

constexpr double kRadians = std::numbers::pi / 180.0;
constexpr double kDegrees = 180.0 / std::numbers::pi;
constexpr double kHalfTurn = 180.0;
constexpr double kTurn = 360.0;
constexpr std::string_view kHeader = "thug2-demo-camera";
constexpr std::size_t kNumberChars = 32; // (a float written the shortest way that reads back exactly)

// A pose's values as the curve moves them: x, y, z, yaw, pitch, roll, fov.
constexpr std::size_t kX = 0;
constexpr std::size_t kY = 1;
constexpr std::size_t kZ = 2;
constexpr std::size_t kYaw = 3;
constexpr std::size_t kPitch = 4;
constexpr std::size_t kRoll = 5;
constexpr std::size_t kFov = 6;
constexpr std::size_t kChannels = 7;
using Channels = std::array<double, kChannels>;

Channels channels_of(const Pose& pose)
{
    return {pose.position.x, pose.position.y, pose.position.z, pose.yaw, pose.pitch, pose.roll, pose.fov};
}

double value(const Channels& values, std::size_t channel) { return core::at(values, channel); }

Pose pose_of(const Channels& values)
{
    Pose pose;
    pose.position = {.x = static_cast<float>(value(values, kX)),
                     .y = static_cast<float>(value(values, kY)),
                     .z = static_cast<float>(value(values, kZ))};
    // (the yaw back within -180..180)
    double yaw = std::fmod(value(values, kYaw), kTurn);
    if (yaw > kHalfTurn) {
        yaw -= kTurn;
    } else if (yaw <= -kHalfTurn) {
        yaw += kTurn;
    }
    pose.yaw = static_cast<float>(yaw);
    pose.pitch = std::clamp(static_cast<float>(value(values, kPitch)), -kMostPitch, kMostPitch);
    pose.roll = static_cast<float>(value(values, kRoll));
    pose.fov = static_cast<float>(value(values, kFov));
    return pose;
}

// The keys' values, the yaw unwrapped (each key's the nearest turn to the one before: 350 then 10 is 350 then 370).
std::vector<Channels> unwrapped(const std::vector<Key>& keys)
{
    std::vector<Channels> values;
    values.reserve(keys.size());
    for (const Key& key : keys) {
        Channels each = channels_of(key.pose);
        if (!values.empty()) {
            const double before = value(values.back(), kYaw);
            double step = std::fmod(value(each, kYaw) - before, kTurn);
            if (step > kHalfTurn) {
                step -= kTurn;
            } else if (step < -kHalfTurn) {
                step += kTurn;
            }
            core::at(each, kYaw) = before + step;
        }
        values.push_back(each);
    }
    return values;
}

// The curve's slope at key `index` (per frame): Catmull-Rom's over the keys either side, spaced as they are in time;
// one-sided at the ends.
Channels slope_at(const std::vector<Key>& keys, const std::vector<Channels>& values, std::size_t index)
{
    Channels slope{};
    const std::size_t before = index > 0 ? index - 1 : index;
    const std::size_t after = index + 1 < keys.size() ? index + 1 : index;
    const double span =
        static_cast<double>(core::at(keys, after).frame) - static_cast<double>(core::at(keys, before).frame);
    if (span <= 0.0) {
        return slope;
    }
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        core::at(slope, channel) =
            (core::at(core::at(values, after), channel) - core::at(core::at(values, before), channel)) / span;
    }
    return slope;
}

std::string number_text(float number)
{
    std::array<char, kNumberChars> buffer{};
    char* const end = std::next(buffer.data(), static_cast<std::ptrdiff_t>(buffer.size()));
    const auto result = std::to_chars(buffer.data(), end, number);
    return {buffer.data(), result.ptr};
}

bool read_number(const std::string& word, float* out)
{
    const char* const end = std::next(word.data(), static_cast<std::ptrdiff_t>(word.size()));
    const auto result = std::from_chars(word.data(), end, *out);
    return result.ec == std::errc() && result.ptr == end && std::isfinite(*out);
}

bool read_integer(const std::string& word, int32_t* out)
{
    const char* const end = std::next(word.data(), static_cast<std::ptrdiff_t>(word.size()));
    const auto result = std::from_chars(word.data(), end, *out);
    return result.ec == std::errc() && result.ptr == end;
}

std::string pose_text(const Pose& pose)
{
    std::string text;
    for (const float number : {pose.position.x, pose.position.y, pose.position.z, pose.yaw, pose.pitch, pose.roll,
                               pose.fov}) {
        if (!text.empty()) {
            text += ' ';
        }
        text += number_text(number);
    }
    return text;
}

bool read_pose(std::istringstream& words, Pose* pose)
{
    for (float* number : {&pose->position.x, &pose->position.y, &pose->position.z, &pose->yaw, &pose->pitch,
                          &pose->roll, &pose->fov}) {
        std::string word;
        if (!(words >> word) || !read_number(word, number)) {
            return false;
        }
    }
    return true;
}

bool ease_of(const std::string& name, Ease* out)
{
    constexpr std::array kEases = {Ease::kSmooth, Ease::kLinear, Ease::kEase, Ease::kHold};
    const auto found = std::ranges::find_if(kEases, [&name](Ease ease) { return name == ease_name(ease); });
    if (found == kEases.end()) {
        return false;
    }
    *out = *found;
    return true;
}

Vec3 normalised(Vec3 vector)
{
    const double length = std::sqrt((static_cast<double>(vector.x) * vector.x) +
                                    (static_cast<double>(vector.y) * vector.y) +
                                    (static_cast<double>(vector.z) * vector.z));
    if (length <= 0.0) {
        return vector;
    }
    return {.x = static_cast<float>(vector.x / length),
            .y = static_cast<float>(vector.y / length),
            .z = static_cast<float>(vector.z / length)};
}

Vec3 cross(Vec3 left, Vec3 right)
{
    return {.x = (left.y * right.z) - (left.z * right.y),
            .y = (left.z * right.x) - (left.x * right.z),
            .z = (left.x * right.y) - (left.y * right.x)};
}

// The cubic Hermite blend of two values with their slopes (per unit of `share`, 0..1 between them).
struct HermiteEnds {
    double from = 0.0;
    double from_slope = 0.0;
    double to = 0.0;
    double to_slope = 0.0;
};
double hermite(const HermiteEnds& ends, double share)
{
    const double square = share * share;
    const double cube = square * share;
    return (((2 * cube) - (3 * square) + 1) * ends.from) + ((cube - (2 * square) + share) * ends.from_slope) +
           (((3 * square) - (2 * cube)) * ends.to) + ((cube - square) * ends.to_slope);
}

} // namespace

const char* ease_name(Ease ease)
{
    switch (ease) {
    case Ease::kLinear:
        return "linear";
    case Ease::kEase:
        return "ease";
    case Ease::kHold:
        return "hold";
    case Ease::kSmooth:
    default:
        return "smooth";
    }
}

std::size_t add_key(Path* path, const Key& key)
{
    std::vector<Key>& keys = path->keys;
    const auto place = std::ranges::lower_bound(keys, key.frame, {}, &Key::frame);
    if (place != keys.end() && place->frame == key.frame) {
        *place = key;
        return static_cast<std::size_t>(place - keys.begin());
    }
    return static_cast<std::size_t>(keys.insert(place, key) - keys.begin());
}

bool move_key(Path* path, std::size_t* index, int32_t frame)
{
    std::vector<Key>& keys = path->keys;
    if (*index >= keys.size()) {
        return false;
    }
    if (core::at(keys, *index).frame == frame) {
        return true;
    }
    if (key_at(*path, frame) >= 0) {
        return false;
    }
    Key moved = core::at(keys, *index);
    moved.frame = frame;
    keys.erase(std::next(keys.begin(), static_cast<std::ptrdiff_t>(*index)));
    *index = add_key(path, moved);
    return true;
}

void delete_key(Path* path, std::size_t index)
{
    if (index < path->keys.size()) {
        path->keys.erase(std::next(path->keys.begin(), static_cast<std::ptrdiff_t>(index)));
    }
}

int key_at(const Path& path, int32_t frame)
{
    const auto place = std::ranges::lower_bound(path.keys, frame, {}, &Key::frame);
    return place != path.keys.end() && place->frame == frame ? static_cast<int>(place - path.keys.begin()) : -1;
}

bool pose_at(const Path& path, double frame, Pose* out)
{
    const std::vector<Key>& keys = path.keys;
    if (keys.empty()) {
        return false;
    }
    if (keys.size() == 1 || frame <= keys.front().frame) {
        *out = keys.front().pose;
        return true;
    }
    if (frame >= keys.back().frame) {
        *out = keys.back().pose;
        return true;
    }
    // (the segment: the last key at or before the frame)
    const auto next = std::ranges::upper_bound(keys, frame, {}, [](const Key& key) { return static_cast<double>(key.frame); });
    const auto segment = static_cast<std::size_t>(next - keys.begin()) - 1;
    const Key& from = core::at(keys, segment);
    const Key& to = core::at(keys, segment + 1);
    const double span = static_cast<double>(to.frame) - static_cast<double>(from.frame);
    const double share = (frame - from.frame) / span;
    if (from.ease == Ease::kHold) {
        *out = from.pose;
        return true;
    }
    const std::vector<Channels> values = unwrapped(keys);
    const Channels& from_values = core::at(values, segment);
    const Channels& to_values = core::at(values, segment + 1);
    Channels result{};
    // (cubic Hermite: smooth - Catmull-Rom's slopes at both keys; ease - none, so it slows into and out of them)
    const bool smooth = from.ease == Ease::kSmooth;
    const Channels from_slope = smooth ? slope_at(keys, values, segment) : Channels{};
    const Channels to_slope = smooth ? slope_at(keys, values, segment + 1) : Channels{};
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        const double start = core::at(from_values, channel);
        const double end = core::at(to_values, channel);
        if (from.ease == Ease::kLinear) {
            core::at(result, channel) = start + ((end - start) * share);
        } else {
            core::at(result, channel) = hermite({.from = start,
                                                 .from_slope = span * core::at(from_slope, channel),
                                                 .to = end,
                                                 .to_slope = span * core::at(to_slope, channel)},
                                                share);
        }
    }
    *out = pose_of(result);
    return true;
}

Vec3 forward_of(const Pose& pose)
{
    const double yaw = static_cast<double>(pose.yaw) * kRadians;
    const double pitch = static_cast<double>(std::clamp(pose.pitch, -kMostPitch, kMostPitch)) * kRadians;
    return {.x = static_cast<float>(std::sin(yaw) * std::cos(pitch)),
            .y = static_cast<float>(std::sin(pitch)),
            .z = static_cast<float>(std::cos(yaw) * std::cos(pitch))};
}

Rows rows_of(const Pose& pose)
{
    const Vec3 forward = forward_of(pose);
    const Vec3 back = {.x = -forward.x, .y = -forward.y, .z = -forward.z};
    // right = (0, 1, 0) x back, up = back x right (test_view's camera_rows)
    Vec3 right = normalised({.x = back.z, .y = 0.0F, .z = -back.x});
    Vec3 up = cross(back, right);
    const double roll = static_cast<double>(pose.roll) * kRadians;
    if (roll != 0.0) {
        const auto cosine = static_cast<float>(std::cos(roll));
        const auto sine = static_cast<float>(std::sin(roll));
        const Vec3 turned_right = {.x = (right.x * cosine) + (up.x * sine),
                                   .y = (right.y * cosine) + (up.y * sine),
                                   .z = (right.z * cosine) + (up.z * sine)};
        const Vec3 turned_up = {.x = (up.x * cosine) - (right.x * sine),
                                .y = (up.y * cosine) - (right.y * sine),
                                .z = (up.z * cosine) - (right.z * sine)};
        right = turned_right;
        up = turned_up;
    }
    return {.right = right, .up = up, .back = back};
}

Angles angles_toward(const Vec3& forward)
{
    const double length = std::sqrt((static_cast<double>(forward.x) * forward.x) +
                                    (static_cast<double>(forward.y) * forward.y) +
                                    (static_cast<double>(forward.z) * forward.z));
    if (length <= 0.0 || !std::isfinite(length)) {
        return {};
    }
    Angles angles;
    angles.found = true;
    if ((static_cast<double>(forward.x) * forward.x) + (static_cast<double>(forward.z) * forward.z) > 0.0) {
        angles.yaw = static_cast<float>(std::atan2(forward.x, forward.z) * kDegrees);
    }
    angles.pitch = std::clamp(static_cast<float>(std::asin(std::clamp(forward.y / length, -1.0, 1.0)) * kDegrees),
                              -kMostPitch, kMostPitch);
    return angles;
}

void look_at(Pose* pose, const Vec3& target)
{
    const Angles angles = angles_toward(
        {.x = target.x - pose->position.x, .y = target.y - pose->position.y, .z = target.z - pose->position.z});
    if (angles.found) {
        pose->yaw = angles.yaw;
        pose->pitch = angles.pitch;
    }
}

std::string write_text(const Path& path)
{
    std::string text(kHeader);
    text += " " + std::to_string(kVersion) + "\n";
    text += "# key <frame> <x> <y> <z> <yaw> <pitch> <roll> <fov> <smooth | linear | ease | hold>\n";
    text += "# fixed <x> <y> <z> <yaw> <pitch> <roll> <fov> <name>\n";
    text += "# loop <in frame> <out frame>\n";
    for (const Key& key : path.keys) {
        text += "key ";
        text += std::to_string(key.frame);
        text += ' ';
        text += pose_text(key.pose);
        text += ' ';
        text += ease_name(key.ease);
        text += '\n';
    }
    for (const FixedCamera& camera : path.fixed) {
        text += "fixed ";
        text += pose_text(camera.pose);
        text += ' ';
        text += camera.name;
        text += '\n';
    }
    if (path.loop_in >= 0 && path.loop_out > path.loop_in) {
        text += "loop ";
        text += std::to_string(path.loop_in);
        text += ' ';
        text += std::to_string(path.loop_out);
        text += '\n';
    }
    return text;
}

namespace {

// A line after the header: true when read (or of a kind skipped), else `why`.
bool read_line(std::istringstream& words, const std::string& kind, Path* path, std::string* why)
{
    if (kind == "key") {
        Key key;
        std::string frame;
        std::string ease;
        if (!(words >> frame) || !read_integer(frame, &key.frame) || !read_pose(words, &key.pose) || !(words >> ease) ||
            !ease_of(ease, &key.ease)) {
            *why = "a key is <frame> <x> <y> <z> <yaw> <pitch> <roll> <fov> <ease>";
            return false;
        }
        add_key(path, key);
    } else if (kind == "fixed") {
        FixedCamera camera;
        if (!read_pose(words, &camera.pose)) {
            *why = "a fixed camera is <x> <y> <z> <yaw> <pitch> <roll> <fov> <name>";
            return false;
        }
        std::getline(words >> std::ws, camera.name);
        path->fixed.push_back(camera);
    } else if (kind == "loop") {
        std::string from;
        std::string to;
        if (!(words >> from >> to) || !read_integer(from, &path->loop_in) || !read_integer(to, &path->loop_out)) {
            *why = "a loop is <in frame> <out frame>";
            return false;
        }
    }
    // (anything else: a later version's - skipped)
    return true;
}

bool read_header(std::istringstream& words, const std::string& kind, std::string* why)
{
    int32_t version = 0;
    std::string word;
    if (kind != kHeader || !(words >> word) || !read_integer(word, &version)) {
        *why = "not a demo camera file";
        return false;
    }
    if (version > kVersion || version < 1) {
        *why = "version " + std::to_string(version) + " (this game reads " + std::to_string(kVersion) + ")";
        return false;
    }
    return true;
}

} // namespace

bool read_text(const std::string& text, Path* out, std::string* why)
{
    Path path;
    std::istringstream lines(text);
    std::string line;
    int number = 0;
    bool header = false;
    while (std::getline(lines, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream words(line);
        std::string kind;
        if (!(words >> kind) || kind.starts_with('#')) {
            continue;
        }
        if (!header) {
            if (!read_header(words, kind, why)) {
                return false;
            }
            header = true;
        } else if (!read_line(words, kind, &path, why)) {
            *why = "line " + std::to_string(number) + ": " + *why;
            return false;
        }
    }
    if (!header) {
        *why = "empty";
        return false;
    }
    *out = std::move(path);
    return true;
}

} // namespace devtools::camera_path
