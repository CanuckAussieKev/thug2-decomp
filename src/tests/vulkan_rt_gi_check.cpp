// NOT ORIGINAL - thug2_vulkan_rt_gi_check.exe: the Vulkan Gpu's one-bounce ray-traced indirect light
// (gpu_vulkan/vulkan_rt_effects.h, [Graphics] rt_gi) tried on this machine against references worked out on the processor.
// Everything goes through the installed render::rt::RayTracing (R2's acceleration structures and geometry table, R3's
// material table), as render_pc's frame record feeds it.
//
// The scene: a white floor, a red wall standing on it and a grey pillar between the wall and the sun (so the floor
// beside the wall is partly in the pillar's shadow), untextured materials of those colours (their albedo), a sun from
// the pillar's side (set as the console's `rt sun` sets it - in the game, the level's baked sun) and a bluish ambient
// light; drawn through render::Gpu with darker flat colours (the "baked" frame).
// The processor answers each pixel from the camera's own position and lens: the surface it sees, the pass's own ray
// directions from there (the same sequence, the same per-pixel turn, the same frame index), what each hits, whether
// that point sees the sun.
//   - The rays as they are (rt_gi_denoise=0): pixels whose rays all hit the same faces, with the same sun visibility,
//     when the start moves a little or the normal tilts a little ("clear-cut") must have the processor's light (E)
//     within float precision, and their surface's albedo; every pixel the colour before + strength x albedo x max(E -
//     sky, 0) x (1 - colour) from the images read back (one sample and four, resolved); no value NaN or infinite.
//   - The bounce is reddish on the floor beside the red wall.
//   - An open floor (nothing else in the scene): every ray misses, E is the sky's light, the frame exactly as before.
//   - Denoised, a still camera: after 48 frames the light the colour was given is near a 256-ray reference (mean and
//     99th-percentile error) with every history full (32 frames).
//   - rt_gi off: no pass, nothing changed.
//   thug2_vulkan_rt_gi_check [--device N] [--display N] [--no-validation]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan ray queries for this process.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "base/maths/view_transforms.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_effects.h"
#include "platform/run_log.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
namespace rt = render::rt;
using Vec3 = std::array<double, 3>;
using Rgb = std::array<double, 3>;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr uint32_t kTargetWidth = 320;
constexpr uint32_t kTargetHeight = 240;
constexpr int kColumns = static_cast<int>(kTargetWidth);
constexpr int kRows = static_cast<int>(kTargetHeight);
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvfColour = 0x042;   // XYZ | DIFFUSE (the raster draws)
constexpr uint32_t kFvfTextured = 0x142; // XYZ | DIFFUSE | TEX1 (the meshes the rays see)
constexpr uint32_t kTexturedStride = 24; // the position, the colour at 12, the uv at 16
constexpr uint32_t kTexturedColour = 12;
constexpr uint32_t kTexturedUv = 16;
constexpr uint32_t kGrey = 0x80808080U;
constexpr uint32_t kClearColour = 0xFF102030;
constexpr float kNearZ = 2.0F;
constexpr float kFarZ = 2000.0F;
constexpr double kHalfFovTan = 0.57735026918962576; // tan 30 degrees: a 60-degree horizontal field of view
constexpr double kNoHit = std::numeric_limits<double>::infinity();
constexpr std::size_t kNoSurface = 0;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint32_t kRedShift = 16;
constexpr uint32_t kGreenShift = 8;
constexpr std::size_t kPixelBytes = 4;
constexpr double kByteRange = 255.0;
constexpr double kOffsetScale = 1.0;   // (the pass's start offset)
constexpr double kSunReach = 20000.0;  // (the pass's sun rays' length)
constexpr float kDistance = 400.0F;    // rt_gi_distance here
constexpr float kStrength = 1.0F;
constexpr unsigned kRawRays = 4;
constexpr unsigned kDenoisedRays = 2;
constexpr int kStillFrames = 48;
constexpr uint32_t kWorldOpaque = rt::kWorld | rt::kOpaque | rt::kCastsShadow;

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--no-validation") {
            options.validation = false;
        } else if (argument == "--device" && has_value) {
            options.device = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--display" && has_value) {
            options.display = text::to_int(core::at(arguments, ++i));
        }
    }
    return options;
}

int& validation_messages()
{
    static int count = 0;
    return count;
}

void print_line(const std::string& line)
{
    if (line.starts_with("validation ")) {
        ++validation_messages();
    }
    text::print("  %s\n", line.c_str());
}

void print_log_line(const char* line) { text::print("  log: %s\n", line); }

struct Tally {
    int passed = 0;
    int failed = 0;
    void check(bool ok, const std::string& what)
    {
        if (ok) {
            ++passed;
        } else {
            ++failed;
        }
        text::print("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    }
};

double x_of(const Vec3& v) { return core::at(v, 0); }
double y_of(const Vec3& v) { return core::at(v, 1); }
double z_of(const Vec3& v) { return core::at(v, 2); }
Vec3 normalised(const Vec3& v)
{
    const double length = std::hypot(x_of(v), y_of(v), z_of(v));
    return {x_of(v) / length, y_of(v) / length, z_of(v) / length};
}
double dot(const Vec3& one, const Vec3& other)
{
    return (x_of(one) * x_of(other)) + (y_of(one) * y_of(other)) + (z_of(one) * z_of(other));
}
Vec3 cross(const Vec3& one, const Vec3& other)
{
    return {(y_of(one) * z_of(other)) - (z_of(one) * y_of(other)), (z_of(one) * x_of(other)) - (x_of(one) * z_of(other)),
            (x_of(one) * y_of(other)) - (y_of(one) * x_of(other))};
}
Vec3 step(const Vec3& from, const Vec3& along, double scale)
{
    return {x_of(from) + (x_of(along) * scale), y_of(from) + (y_of(along) * scale), z_of(from) + (z_of(along) * scale)};
}
Vec3 minus(const Vec3& one, const Vec3& other) { return step(one, other, -1.0); }
std::array<float, 3> floats(const Vec3& v)
{
    return {static_cast<float>(x_of(v)), static_cast<float>(y_of(v)), static_cast<float>(z_of(v))};
}

// --- the scene ---------------------------------------------------------------------------------------------------

// A box (or, with no height, the floor square): its albedo (the material's colour), the colour it's drawn with.
struct Solid {
    Vec3 low{};
    Vec3 high{};
    Rgb albedo{};
    uint32_t drawn = 0;
};

constexpr double kFloorHalf = 100.0;
const std::array<Solid, 3> kRoom{{
    {.low = {-kFloorHalf, 0.0, -kFloorHalf},
     .high = {kFloorHalf, 0.0, kFloorHalf},
     .albedo = {0.8, 0.8, 0.8},
     .drawn = 0xFF606060},
    {.low = {-34.0, 0.0, -60.0}, .high = {-28.0, 50.0, 60.0}, .albedo = {0.9, 0.15, 0.1}, .drawn = 0xFFA03020},
    {.low = {6.0, 0.0, -14.0}, .high = {18.0, 40.0, 14.0}, .albedo = {0.5, 0.5, 0.5}, .drawn = 0xFF707070},
}};

// The lights: toward the sun (from the pillar's side, high), its colour, the ambient light (also the sky's).
constexpr Vec3 kToSunUnnormalised{1.0, 0.75, 0.2};
Vec3 to_sun() { return normalised(kToSunUnnormalised); }
constexpr float kSkaterLight = 0.5F; // (the frame record's directional light: the skater's, unused by the pass)
constexpr Rgb kSun{1.0, 1.0, 1.0}; // (the pass's sun: white, the bake has no colour)
constexpr Rgb kAmbient{0.25, 0.28, 0.34}; // (the frame record's: the pass doesn't use it)
// What the pass gives a hit besides the sun, and a ray that hits nothing: none (the bake holds the sky and the ambient)
constexpr Rgb kHitAmbient{0.0, 0.0, 0.0};
constexpr Rgb kSky{0.0, 0.0, 0.0};

constexpr Vec3 kEye{70.0, 75.0, 95.0};
constexpr Vec3 kAt{-12.0, 4.0, 0.0};
constexpr Vec3 kUp{0.0, 1.0, 0.0};

// A ray against a box (slabs; a flat box is the floor square): the distance to its first hit and that face's normal
// (axis and sign).
struct Hit {
    double distance = kNoHit;
    Vec3 normal{};
    std::size_t axis = 0;
};
Hit intersect(const Solid& solid, const Vec3& origin, const Vec3& direction)
{
    constexpr double kParallel = 1e-12;
    double enter = 0.0;
    double leave = kNoHit;
    Vec3 normal{};
    std::size_t entered = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double from = core::at(origin, axis);
        const double along = core::at(direction, axis);
        const double low = core::at(solid.low, axis);
        const double high = core::at(solid.high, axis);
        if (std::abs(along) < kParallel) {
            if (from < low || from > high) {
                return {};
            }
            continue;
        }
        const bool rising = along > 0.0;
        const double near_t = ((rising ? low : high) - from) / along;
        const double far_t = ((rising ? high : low) - from) / along;
        if (near_t > enter) {
            enter = near_t;
            normal = {};
            core::at(normal, axis) = rising ? -1.0 : 1.0;
            entered = axis;
        }
        leave = std::min(leave, far_t);
        if (enter > leave) {
            return {};
        }
    }
    if (enter <= 0.0) {
        return {};
    }
    return {.distance = enter, .normal = normal, .axis = entered};
}

// The first solid along a ray (index + 1; 0: none) within `reach`.
struct Found {
    std::size_t solid = kNoSurface;
    Hit hit;
};
Found first_hit(std::span<const Solid> solids, const Vec3& origin, const Vec3& direction, double reach)
{
    Found found;
    for (std::size_t index = 0; index < solids.size(); ++index) {
        const Hit hit = intersect(core::at(solids, index), origin, direction);
        if (hit.distance < found.hit.distance && hit.distance <= reach) {
            found = {.solid = index + 1, .hit = hit};
        }
    }
    return found;
}

// The camera: its position, its axes (Direct3D's right-handed look-at: z backward), its lens at distance 1.
struct View {
    Vec3 eye{};
    Vec3 right{};
    Vec3 up{};
    Vec3 back{};
    double half_width = 0.0;
    double half_height = 0.0;
};
View view_from(const Vec3& eye)
{
    View view;
    view.eye = eye;
    view.back = normalised(minus(eye, kAt));
    view.right = normalised(cross(kUp, view.back));
    view.up = cross(view.back, view.right);
    view.half_width = kHalfFovTan;
    view.half_height = kHalfFovTan * static_cast<double>(kTargetHeight) / static_cast<double>(kTargetWidth);
    return view;
}

// The surface a pixel sees (offset: where in the pixel the depth was read, from its centre).
struct Seen {
    std::size_t solid = kNoSurface;
    Vec3 normal{};
    Vec3 point{};
    double distance = 0.0;
};
using Offset = std::array<double, 2>;
Seen seen_at(std::span<const Solid> solids, const View& view, double px, double py)
{
    // (Direct3D 9's pixel centres: whole coordinates)
    const double ndc_x = (2.0 * px / kTargetWidth) - 1.0;
    const double ndc_y = 1.0 - (2.0 * py / kTargetHeight);
    const Vec3 direction = normalised(
        step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height), view.back, -1.0));
    const Found found = first_hit(solids, view.eye, direction, kNoHit);
    Seen seen;
    if (found.solid != kNoSurface) {
        seen.solid = found.solid;
        seen.normal = found.hit.normal;
        seen.distance = found.hit.distance;
        seen.point = step(view.eye, direction, found.hit.distance);
    }
    return seen;
}
bool same_face(const Seen& one, const Seen& other) { return one.solid == other.solid && one.normal == other.normal; }

// --- the light, as the pass traces it ----------------------------------------------------------------------------

uint32_t hash(uint32_t x)
{
    constexpr uint32_t kFirst = 0x7feb352dU;
    constexpr uint32_t kSecond = 0x846ca68bU;
    constexpr uint32_t kShortShift = 15;
    constexpr uint32_t kLongShift = 16;
    x ^= x >> kLongShift;
    x *= kFirst;
    x ^= x >> kShortShift;
    x *= kSecond;
    x ^= x >> kLongShift;
    return x;
}

float fraction(float value) { return value - std::floor(value); }

// A direction over the hemisphere around `normal`, cosine-weighted, from two numbers in 0 - 1 (the pass's frame).
Vec3 hemisphere(const Vec3& normal, const std::array<double, 2>& numbers)
{
    const double u = numbers.front();
    const double v = numbers.back();
    const double side = z_of(normal) >= 0.0 ? 1.0 : -1.0;
    const double scale = -1.0 / (side + z_of(normal));
    const double mixed = x_of(normal) * y_of(normal) * scale;
    const Vec3 across{1.0 + (side * x_of(normal) * x_of(normal) * scale), side * mixed, -side * x_of(normal)};
    const Vec3 up{mixed, side + (y_of(normal) * y_of(normal) * scale), -y_of(normal)};
    constexpr double kTurn = 6.2831853;
    const double radius = std::sqrt(u);
    const double phi = kTurn * v;
    return step(step(step(Vec3{}, across, radius * std::cos(phi)), up, radius * std::sin(phi)), normal,
                std::sqrt(std::max(0.0, 1.0 - u)));
}

double start_offset(double distance)
{
    constexpr double kLinear = 0.0005;
    constexpr double kSquare = 6e-8;
    return kOffsetScale * (1.0 + (distance * kLinear) + (distance * distance * kSquare));
}

// What one bounce ray brings back, and what decided it (the solid, the face, whether the sun was seen).
struct RayAnswer {
    Rgb light{};
    std::size_t solid = kNoSurface;
    std::size_t face = 0;
    bool sunlit = false;
    bool near_reach = false; // hit close to the rays' length (a float's rounding decides it)
};
RayAnswer bounce(std::span<const Solid> solids, const Vec3& origin, const Vec3& direction, double travelled)
{
    RayAnswer answer;
    const Found found = first_hit(solids, origin, direction, kDistance);
    if (found.solid == kNoSurface) {
        answer.light = kSky;
        return answer;
    }
    const Solid& solid = core::at(solids, found.solid - 1);
    answer.solid = found.solid;
    constexpr double kReachMargin = 0.98;
    answer.near_reach = found.hit.distance > kReachMargin * kDistance;
    answer.face = (found.hit.axis * 2) + (core::at(found.hit.normal, found.hit.axis) > 0.0 ? 1 : 0);
    Rgb light = kHitAmbient;
    const double facing = dot(found.hit.normal, to_sun());
    if (facing > 0.0) {
        const Vec3 point = step(origin, direction, found.hit.distance);
        const Vec3 lifted = step(point, found.hit.normal, start_offset(travelled + found.hit.distance));
        if (first_hit(solids, lifted, to_sun(), kSunReach).solid == kNoSurface) {
            answer.sunlit = true;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                core::at(light, channel) += core::at(kSun, channel) * facing;
            }
        }
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
        core::at(answer.light, channel) = core::at(solid.albedo, channel) * core::at(light, channel);
    }
    return answer;
}

struct Pixel {
    int x = 0;
    int y = 0;
};

// The pass's rays for a pixel in a frame: the mean light and each ray's answer. `shift` moves the start, `tilt` the
// normal.
struct Traced {
    Rgb light{};
    std::vector<RayAnswer> rays;
};
Traced traced(std::span<const Solid> solids, const Seen& seen, Pixel local, uint32_t frame, uint32_t rays,
              const Vec3& shift, const Vec3& tilt)
{
    constexpr float kUnit = 1.0F / 16777216.0F;
    constexpr uint32_t kDropBits = 8;
    constexpr uint32_t kWrap = 4095;
    constexpr float kSequenceX = 0.7548776662F;
    constexpr float kSequenceY = 0.5698402910F;
    const Vec3 normal = normalised(step(seen.normal, tilt, 1.0));
    const Vec3 origin = step(step(seen.point, normal, start_offset(seen.distance)), shift, 1.0);
    const uint32_t seed = hash(static_cast<uint32_t>(local.x) + hash(static_cast<uint32_t>(local.y)));
    const float turn_x = static_cast<float>(hash(seed) >> kDropBits) * kUnit;
    const float turn_y = static_cast<float>(hash(seed + 1U) >> kDropBits) * kUnit;
    Traced out;
    for (uint32_t i = 0; i < rays; ++i) {
        const auto index = static_cast<float>(((frame * rays) + i) & kWrap);
        const float u = fraction(turn_x + (index * kSequenceX));
        const float v = fraction(turn_y + (index * kSequenceY));
        const RayAnswer answer = bounce(solids, origin, hemisphere(normal, {u, v}), seen.distance);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            core::at(out.light, channel) += core::at(answer.light, channel) / rays;
        }
        out.rays.push_back(answer);
    }
    return out;
}

bool same_answers(const Traced& one, const Traced& other)
{
    for (std::size_t i = 0; i < one.rays.size(); ++i) {
        const RayAnswer& mine = core::at(one.rays, i);
        const RayAnswer& theirs = core::at(other.rays, i);
        if (mine.solid != theirs.solid || mine.face != theirs.face || mine.sunlit != theirs.sunlit || mine.near_reach ||
            theirs.near_reach) {
            return false;
        }
    }
    return true;
}

// The light with many rays (a 16 x 16 stratified grid over the hemisphere): what the denoiser converges to.
Rgb reference_light(std::span<const Solid> solids, const Seen& seen)
{
    constexpr int kSide = 16;
    constexpr double kMiddle = 0.5;
    const Vec3 origin = step(seen.point, seen.normal, start_offset(seen.distance));
    Rgb sum{};
    for (int i = 0; i < kSide; ++i) {
        for (int j = 0; j < kSide; ++j) {
            const RayAnswer answer =
                bounce(solids, origin, hemisphere(seen.normal, {(i + kMiddle) / kSide, (j + kMiddle) / kSide}),
                       seen.distance);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                core::at(sum, channel) += core::at(answer.light, channel) / (kSide * kSide);
            }
        }
    }
    return sum;
}

// --- the meshes and materials through the backend -------------------------------------------------------------------

std::vector<std::array<float, 3>> triangles_of(const Solid& solid)
{
    using Corner = std::array<int, 3>;
    const auto corner = [&solid](const Corner& at) {
        std::array<float, 3> out{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            core::at(out, axis) =
                static_cast<float>(core::at(at, axis) != 0 ? core::at(solid.high, axis) : core::at(solid.low, axis));
        }
        return out;
    };
    std::vector<std::array<float, 3>> out;
    const auto quad = [&](const std::array<Corner, 4>& corners) {
        for (const std::size_t index : {0U, 1U, 2U, 0U, 2U, 3U}) {
            out.push_back(corner(core::at(corners, index)));
        }
    };
    quad({{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}); // -y (the floor square's one face)
    if (y_of(solid.high) == y_of(solid.low)) {
        return out;
    }
    quad({{{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}}});
    quad({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    quad({{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}});
    quad({{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}});
    quad({{{1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}}});
    return out;
}

struct SceneMesh {
    rt::MeshId id = 0;
    rt::MaterialId material = 0;
    render::GpuVertexBuffer* vertices = nullptr;
    std::vector<std::array<float, 3>> triangles;
    uint32_t drawn = 0;
};

void put_float(std::span<uint8_t> bytes, std::size_t at, float value)
{
    std::memcpy(bytes.subspan(at, sizeof(value)).data(), &value, sizeof(value));
}
void put_word(std::span<uint8_t> bytes, std::size_t at, uint32_t value)
{
    std::memcpy(bytes.subspan(at, sizeof(value)).data(), &value, sizeof(value));
}

// A solid's mesh (an unindexed list with texture coordinates and grey vertex colours) and its material (untextured: its
// colour is the albedo), registered with the backend.
bool make_solid(render::Gpu& gpu, const Solid& solid, SceneMesh* mesh)
{
    mesh->triangles = triangles_of(solid);
    mesh->drawn = solid.drawn;
    std::vector<uint8_t> bytes(mesh->triangles.size() * kTexturedStride);
    for (std::size_t i = 0; i < mesh->triangles.size(); ++i) {
        const std::size_t base = i * kTexturedStride;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            put_float(bytes, base + (axis * sizeof(float)), core::at(core::at(mesh->triangles, i), axis));
        }
        put_word(bytes, base + kTexturedColour, kGrey);
        put_float(bytes, base + kTexturedUv, 0.0F);
        put_float(bytes, base + kTexturedUv + sizeof(float), 0.0F);
    }
    void* data = nullptr;
    if (gpu.create_vertex_buffer(static_cast<uint32_t>(bytes.size()), 0, kFvfTextured, value::kPoolDefault,
                                 &mesh->vertices) != value::kResultOk ||
        gpu.lock_vertex_buffer(mesh->vertices, 0, 0, &data, 0) != value::kResultOk) {
        return false;
    }
    std::memcpy(data, bytes.data(), bytes.size());
    if (gpu.unlock_vertex_buffer(mesh->vertices) != value::kResultOk) {
        return false;
    }
    const rt::Geometry geometry{.vertices = mesh->vertices,
                                .vertex_offset = 0,
                                .vertex_stride = kTexturedStride,
                                .vertex_count = static_cast<uint32_t>(mesh->triangles.size()),
                                .position_offset = 0,
                                .indices = nullptr,
                                .index_offset = 0,
                                .index_count = 0,
                                .indices_32bit = false,
                                .strip = false,
                                .dynamic = false,
                                .uv_offset = kTexturedUv,
                                .uv_format = rt::kUvFloat2,
                                .colour_offset = kTexturedColour,
                                .colour_format = rt::kColourA8R8G8B8};
    mesh->id = rt::next_mesh_id();
    rt::ray_tracing()->add_mesh(mesh->id, geometry);
    mesh->material = rt::next_material_id();
    rt::Material surface;
    surface.colour = {static_cast<float>(core::at(solid.albedo, 0)), static_cast<float>(core::at(solid.albedo, 1)),
                      static_cast<float>(core::at(solid.albedo, 2)), 1.0F};
    surface.flags = rt::kOpaque;
    rt::ray_tracing()->set_material(mesh->material, surface);
    return true;
}

void release_solid(render::Gpu& gpu, SceneMesh* mesh)
{
    if (rt::ray_tracing() != nullptr) {
        if (mesh->id != 0) {
            rt::ray_tracing()->remove_mesh(mesh->id);
        }
        if (mesh->material != 0) {
            rt::ray_tracing()->remove_material(mesh->material);
        }
    }
    if (mesh->vertices != nullptr) {
        gpu.release_vertex_buffer(mesh->vertices);
    }
    *mesh = SceneMesh{};
}

// --- drawing it through the Gpu ----------------------------------------------------------------------------------

struct ColourVertex {
    std::array<float, 3> position{};
    uint32_t colour = 0;
};

constexpr std::size_t kMatrixFloats = 16;
using Matrix = std::array<float, kMatrixFloats>;
struct Matrices {
    Matrix view{};
    Matrix projection{};
};
Matrices matrices_for(const Vec3& eye)
{
    Matrices out;
    maths::matrix_look_at_rh(out.view, floats(eye), floats(kAt), floats(kUp));
    const double width = 2.0 * kNearZ * kHalfFovTan;
    const double height = width * static_cast<double>(kTargetHeight) / static_cast<double>(kTargetWidth);
    maths::matrix_perspective_rh(out.projection, static_cast<float>(width), static_cast<float>(height), kNearZ, kFarZ);
    return out;
}

void draw_scene(render::Gpu& gpu, std::span<const SceneMesh> meshes)
{
    using render::GpuRenderState;
    using render::GpuStageState;
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    const Matrices matrices = matrices_for(kEye);
    constexpr Matrix kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    gpu.set_transform(render::GpuTransform::kWorld, kIdentity.data());
    gpu.set_transform(render::GpuTransform::kView, matrices.view.data());
    gpu.set_transform(render::GpuTransform::kProjection, matrices.projection.data());
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_vertex_format(kFvfColour);
    gpu.set_texture(0, nullptr);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgDiffuse);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgDiffuse);
    for (const SceneMesh& mesh : meshes) {
        std::vector<ColourVertex> vertices;
        vertices.reserve(mesh.triangles.size());
        for (const std::array<float, 3>& position : mesh.triangles) {
            vertices.push_back({.position = position, .colour = mesh.drawn});
        }
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(vertices.size() / 3),
                             vertices.data(), sizeof(ColourVertex));
    }
}

rt::ViewDrawn view_drawn()
{
    const Matrices matrices = matrices_for(kEye);
    rt::ViewDrawn view;
    constexpr std::size_t kSide = 4;
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(view.camera.view, row), column) = core::at(matrices.view, (row * kSide) + column);
            core::at(core::at(view.camera.projection, row), column) =
                core::at(matrices.projection, (row * kSide) + column);
        }
    }
    view.camera.position = floats(kEye);
    view.rect = {0, 0, kTargetWidth, kTargetHeight};
    view.depth_range = {0.0F, 1.0F};
    return view;
}

struct Targets {
    GpuSurface* colour = nullptr;
    GpuSurface* depth = nullptr;
    GpuSurface* resolved = nullptr;
};
bool make_targets(render::Gpu& gpu, uint32_t samples, Targets* out)
{
    const render::GpuSurfaceDesc colour{.width = kTargetWidth,
                                        .height = kTargetHeight,
                                        .format = value::kFormatX8R8G8B8,
                                        .samples = samples,
                                        .quality = 0,
                                        .lockable_or_discard = samples == 0};
    render::GpuSurfaceDesc depth = colour;
    depth.format = value::kFormatD24S8;
    depth.lockable_or_discard = false;
    render::GpuSurfaceDesc resolved = colour;
    resolved.samples = 0;
    resolved.lockable_or_discard = true;
    return gpu.create_render_target(colour, &out->colour) == value::kResultOk &&
           gpu.create_depth_surface(depth, &out->depth) == value::kResultOk &&
           (samples == 0 || gpu.create_render_target(resolved, &out->resolved) == value::kResultOk);
}

void release_targets(render::Gpu& gpu, const Targets& targets)
{
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    for (GpuSurface* surface : {targets.colour, targets.depth, targets.resolved}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
}

bool read_colour(render::Gpu& gpu, const Targets& targets, std::vector<uint8_t>* texels)
{
    GpuSurface* read_from = targets.colour;
    if (targets.resolved != nullptr) {
        if (gpu.copy_surface(targets.colour, nullptr, targets.resolved, nullptr, value::kFilterNone) !=
            value::kResultOk) {
            return false;
        }
        read_from = targets.resolved;
    }
    return render::vulkan::read_surface(read_from, texels) &&
           texels->size() == static_cast<std::size_t>(kTargetWidth) * kTargetHeight * kPixelBytes;
}

// A frame: the record given, cleared, the scene drawn, the passes through the listener (draw_viewport's call). Returns
// the indirect light passes recorded.
uint64_t draw_frame(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, uint64_t frame)
{
    rt::FrameRecord& record = rt::frame_record();
    record = rt::FrameRecord{};
    record.frame = frame;
    constexpr std::array<std::array<float, 4>, 3> kIdentityRows{
        {{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
    for (const SceneMesh& mesh : meshes) {
        record.instances.push_back(
            {.mesh = mesh.id, .material = mesh.material, .flags = kWorldOpaque, .object_to_world = kIdentityRows});
    }
    record.lights.push_back({.kind = rt::Light::Kind::kAmbient,
                             .colour = {static_cast<float>(core::at(kAmbient, 0)),
                                        static_cast<float>(core::at(kAmbient, 1)),
                                        static_cast<float>(core::at(kAmbient, 2))}});
    // (the skater's light, which the bounce doesn't use: its sun is the level's - here the console's `rt sun`)
    record.lights.push_back({.kind = rt::Light::Kind::kDirectional,
                             .colour = {kSkaterLight, kSkaterLight, kSkaterLight},
                             .direction = {0.0F, -1.0F, 0.0F}});
    const std::array<float, 3> toward_sun = floats(to_sun());
    rt::set_sun_override(&toward_sun);
    gpu.begin_scene();
    rt::ray_tracing()->begin_frame(record);
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kClearColour, 1.0F, 0);
    draw_scene(gpu, meshes);
    const uint64_t before = render::vulkan::rt_gi_passes();
    rt::opaque_view_drawn(view_drawn());
    const uint64_t passes = render::vulkan::rt_gi_passes() - before;
    gpu.end_scene();
    gpu.present();
    return passes;
}

Rgb pixel_at(const std::vector<uint8_t>& texels, int x, int y)
{
    const std::size_t at = ((static_cast<std::size_t>(y) * kTargetWidth) + static_cast<std::size_t>(x)) * kPixelBytes;
    return {core::at(texels, at + 2) / kByteRange, core::at(texels, at + 1) / kByteRange,
            core::at(texels, at) / kByteRange};
}

bool read_gi(render::vulkan::RtGiImage which, render::vulkan::RtAoTexels* out)
{
    render::vulkan::Backend& state = render::vulkan::backend();
    const std::scoped_lock lock(state.mutex);
    return render::vulkan::read_rt_gi_image(state, {0, 0, kTargetWidth, kTargetHeight}, which, out) &&
           out->width == kTargetWidth && out->height == kTargetHeight;
}

const std::array<float, 4>& texel_at(const render::vulkan::RtAoTexels& image, int x, int y)
{
    return core::at(image.texels, (static_cast<std::size_t>(y) * kTargetWidth) + static_cast<std::size_t>(x));
}

int not_finite(const render::vulkan::RtAoTexels& image)
{
    int count = 0;
    for (const std::array<float, 4>& texel : image.texels) {
        for (const float channel : texel) {
            count += std::isfinite(channel) ? 0 : 1;
        }
    }
    return count;
}

std::string rgb_text(const Rgb& colour)
{
    return text::format("%.4f %.4f %.4f", core::at(colour, 0), core::at(colour, 1), core::at(colour, 2));
}

// The colour the pass should leave: before + strength x albedo x max(E - sky, 0) x (1 - before), per channel; a pixel
// without an albedo unchanged.
void compare_colour(const std::vector<uint8_t>& with, const std::vector<uint8_t>& without,
                    const render::vulkan::RtAoTexels& light, const render::vulkan::RtAoTexels& albedo, Tally* tally)
{
    constexpr double kColourTolerance = 2.5 / kByteRange;
    int wrong = 0;
    int added = 0;
    int untouched_wrong = 0;
    std::string first;
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const Rgb before = pixel_at(without, column, row);
            const Rgb got = pixel_at(with, column, row);
            const std::array<float, 4>& surface = texel_at(albedo, column, row);
            if (!(surface.back() > 0.0F)) {
                untouched_wrong += got == before ? 0 : 1;
                continue;
            }
            const std::array<float, 4>& arriving = texel_at(light, column, row);
            Rgb want{};
            bool off = false;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const double beyond = std::max(0.0, static_cast<double>(core::at(arriving, channel)) - core::at(kSky, channel));
                const double add = std::clamp(kStrength * static_cast<double>(core::at(surface, channel)) * beyond, 0.0, 1.0);
                core::at(want, channel) =
                    std::min(1.0, core::at(before, channel) + (add * (1.0 - core::at(before, channel))));
                off = off || std::abs(core::at(want, channel) - core::at(got, channel)) > kColourTolerance;
                added += add > 0.0 ? 1 : 0;
            }
            if (off) {
                ++wrong;
                if (first.empty()) {
                    first = text::format(" (first at %d,%d: %s, %s wanted)", column, row, rgb_text(got).c_str(),
                                         rgb_text(want).c_str());
                }
            }
        }
    }
    tally->check(wrong == 0 && untouched_wrong == 0 && added > 0,
                 text::format("the colour = before + albedo x max(E - sky, 0) x (1 - before): %d wrong, %d pixels "
                              "without an albedo changed, %d channels added to%s",
                              wrong, untouched_wrong, added, first.c_str()));
}

// The rays as they are against the processor's.
void compare_raw(std::span<const Solid> solids, const render::vulkan::RtAoTexels& light,
                 const render::vulkan::RtAoTexels& albedo, const Offset& sample, uint32_t rays, Tally* tally)
{
    const View view = view_from(kEye);
    int clear_cut = 0;
    int wrong = 0;
    int albedo_wrong = 0;
    int borderline = 0;
    int sky_wrong = 0;
    double near_red = 0.0;
    double near_green = 0.0;
    int near_count = 0;
    std::string first;
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const double px = column + sample.front();
            const double py = row + sample.back();
            const Seen seen = seen_at(solids, view, px, py);
            const std::array<float, 4>& arriving = texel_at(light, column, row);
            if (seen.solid == kNoSurface) {
                sky_wrong += texel_at(albedo, column, row).back() == 0.0F ? 0 : 1;
                continue;
            }
            constexpr double kAround = 1.0;
            bool clear = true;
            for (const auto& [dx, dy] : std::array<std::pair<double, double>, 4>{
                     {{kAround, 0.0}, {-kAround, 0.0}, {0.0, kAround}, {0.0, -kAround}}}) {
                clear = clear && same_face(seen, seen_at(solids, view, px + dx, py + dy));
            }
            const Pixel local{.x = column, .y = row};
            const Traced here = traced(solids, seen, local, 0, rays, {}, {});
            constexpr double kShift = 0.05;
            // (the pass's normal comes from the depth: less precise far away)
            constexpr double kTiltNear = 0.002;
            constexpr double kTiltDistance = 100.0;
            const double tilt_size = kTiltNear * std::max(1.0, seen.distance / kTiltDistance);
            for (std::size_t axis = 0; axis < 3 && clear; ++axis) {
                for (const double sign : {-1.0, 1.0}) {
                    Vec3 move{};
                    core::at(move, axis) = sign * kShift;
                    Vec3 tilt{};
                    core::at(tilt, axis) = sign * tilt_size;
                    clear = clear && same_answers(here, traced(solids, seen, local, 0, rays, move, {})) &&
                            same_answers(here, traced(solids, seen, local, 0, rays, {}, tilt));
                }
            }
            if (!clear) {
                ++borderline;
                continue;
            }
            ++clear_cut;
            const Rgb& want_albedo = core::at(solids, seen.solid - 1).albedo;
            const std::array<float, 4>& got_albedo = texel_at(albedo, column, row);
            bool albedo_off = got_albedo.back() != 1.0F;
            bool off = false;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                constexpr double kRelative = 0.004;
                constexpr double kAbsolute = 0.002;
                const double want = core::at(here.light, channel);
                off = off || std::abs(static_cast<double>(core::at(arriving, channel)) - want) > kAbsolute + (kRelative * want);
                albedo_off = albedo_off || std::abs(static_cast<double>(core::at(got_albedo, channel)) -
                                                    core::at(want_albedo, channel)) > kAbsolute;
            }
            albedo_wrong += albedo_off ? 1 : 0;
            if (off) {
                ++wrong;
                if (first.empty()) {
                    first = text::format(" (first at %d,%d: %s, %s wanted)", column, row,
                                         rgb_text({core::at(arriving, 0), core::at(arriving, 1), core::at(arriving, 2)}).c_str(),
                                         rgb_text(here.light).c_str());
                }
            }
            // the floor beside the red wall: what the bounce adds there
            constexpr double kNearWall = -8.0;
            constexpr double kNearWallSpan = 40.0;
            if (seen.solid == 1 && x_of(seen.point) < kNearWall && std::abs(z_of(seen.point)) < kNearWallSpan) {
                near_red += std::max(0.0, static_cast<double>(core::at(arriving, 0)) - core::at(kSky, 0));
                near_green += std::max(0.0, static_cast<double>(core::at(arriving, 1)) - core::at(kSky, 1));
                ++near_count;
            }
        }
    }
    constexpr int kEnough = 20000;
    tally->check(clear_cut >= kEnough && wrong == 0,
                 text::format("clear-cut pixels' light as the processor traced it: %d of %d wrong (%d borderline "
                              "left out)%s",
                              wrong, clear_cut, borderline, first.c_str()));
    tally->check(albedo_wrong == 0 && sky_wrong == 0,
                 text::format("the surfaces' albedo found: %d of %d wrong, %d sky pixels with one", albedo_wrong,
                              clear_cut, sky_wrong));
    if (near_count > 0) {
        near_red /= near_count;
        near_green /= near_count;
    }
    constexpr int kEnoughNear = 500;
    constexpr double kReddish = 2.0;
    constexpr double kSomeRed = 0.02;
    tally->check(near_count >= kEnoughNear && near_red > kReddish * near_green && near_red > kSomeRed,
                 text::format("the floor beside the red wall gets a reddish bounce: red %.4f, green %.4f beyond the sky "
                              "(%d pixels)",
                              near_red, near_green, near_count));
}

// --- the cases -----------------------------------------------------------------------------------------------------

render::vulkan::RtGiSettings settings_for(bool on, unsigned rays, bool denoise)
{
    return {.on = on, .strength = kStrength, .distance = kDistance, .rays = rays, .denoise = denoise};
}

void run_raw(render::Gpu& gpu, std::span<const SceneMesh> meshes, uint32_t samples, Tally* tally)
{
    text::print("the rays as they are (%u rays, %s):\n", kRawRays, samples == 0 ? "one sample" : "four samples");
    Targets targets;
    const bool made = make_targets(gpu, samples, &targets);
    tally->check(made, "the targets made");
    if (made) {
        render::vulkan::set_rt_gi_settings(settings_for(false, kRawRays, false));
        tally->check(draw_frame(gpu, targets, meshes, 1) == 0, "off: no pass");
        std::vector<uint8_t> without;
        const bool read_without = read_colour(gpu, targets, &without);
        render::vulkan::set_rt_gi_settings(settings_for(true, kRawRays, false));
        tally->check(draw_frame(gpu, targets, meshes, 2) == 1, "on: the listener recorded the passes");
        std::vector<uint8_t> with;
        const bool read_with = read_colour(gpu, targets, &with);
        render::vulkan::RtAoTexels light;
        render::vulkan::RtAoTexels albedo;
        const bool read_images = read_gi(render::vulkan::RtGiImage::kRaw, &light) &&
                                 read_gi(render::vulkan::RtGiImage::kAlbedo, &albedo);
        tally->check(read_without && read_with && read_images, "the targets and the pass's images read back");
        if (read_without && read_with && read_images) {
            tally->check(not_finite(light) == 0 && not_finite(albedo) == 0,
                         text::format("no NaN or infinity: %d in the light, %d in the albedo", not_finite(light),
                                      not_finite(albedo)));
            // (four samples: the depth read at sample 0 - Vulkan's standard place, 0.375, 0.125 in the pixel)
            constexpr Offset kSampleZero{-0.125, -0.375};
            compare_raw(kRoom, light, albedo, samples == 0 ? Offset{} : kSampleZero, kRawRays, tally);
            compare_colour(with, without, light, albedo, tally);
        }
    }
    release_targets(gpu, targets);
}

void run_open(render::Gpu& gpu, std::span<const SceneMesh> floor, Tally* tally)
{
    text::print("an open floor (nothing to bounce from):\n");
    Targets targets;
    const bool made = make_targets(gpu, 0, &targets);
    tally->check(made, "the targets made");
    if (made) {
        render::vulkan::set_rt_gi_settings(settings_for(false, kRawRays, false));
        draw_frame(gpu, targets, floor, 1);
        std::vector<uint8_t> without;
        const bool read_without = read_colour(gpu, targets, &without);
        render::vulkan::set_rt_gi_settings(settings_for(true, kRawRays, false));
        tally->check(draw_frame(gpu, targets, floor, 2) == 1, "the pass recorded");
        std::vector<uint8_t> with;
        const bool read_with = read_colour(gpu, targets, &with);
        render::vulkan::RtAoTexels light;
        render::vulkan::RtAoTexels albedo;
        const bool read_images = read_gi(render::vulkan::RtGiImage::kRaw, &light) &&
                                 read_gi(render::vulkan::RtGiImage::kAlbedo, &albedo);
        int floor_pixels = 0;
        int not_sky = 0;
        if (read_images) {
            for (std::size_t i = 0; i < light.texels.size(); ++i) {
                if (!(core::at(albedo.texels, i).back() > 0.0F)) {
                    continue;
                }
                ++floor_pixels;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    constexpr double kHalfPrecision = 0.001;
                    not_sky += std::abs(static_cast<double>(core::at(core::at(light.texels, i), channel)) -
                                        core::at(kSky, channel)) > kHalfPrecision
                                   ? 1
                                   : 0;
                }
            }
        }
        constexpr int kEnoughFloor = 20000;
        tally->check(read_images && floor_pixels >= kEnoughFloor && not_sky == 0,
                     text::format("every ray missed: the light is the sky's at %d floor pixels (%d channels not)",
                                  floor_pixels, not_sky));
        tally->check(read_without && read_with && with == without, "the frame exactly as without the pass");
    }
    release_targets(gpu, targets);
}

void run_denoised(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("denoised (%u rays a frame), a still camera:\n", kDenoisedRays);
    Targets targets;
    const bool made = make_targets(gpu, 0, &targets);
    tally->check(made, "the targets made");
    if (!made) {
        release_targets(gpu, targets);
        return;
    }
    render::vulkan::set_rt_gi_settings(settings_for(true, kDenoisedRays, true));
    int passes = 0;
    for (int frame = 0; frame < kStillFrames; ++frame) {
        passes += static_cast<int>(draw_frame(gpu, targets, meshes, static_cast<uint64_t>(frame) + 1));
    }
    tally->check(passes == kStillFrames, text::format("%d frames, %d passes", kStillFrames, passes));
    render::vulkan::RtAoTexels final_light;
    const bool read = read_gi(render::vulkan::RtGiImage::kFinal, &final_light);
    tally->check(read && not_finite(final_light) == 0,
                 text::format("the denoised light read back, %d values not finite", read ? not_finite(final_light) : -1));
    if (read) {
        const View view = view_from(kEye);
        std::vector<double> errors;
        int short_history = 0;
        constexpr int kStride = 3;
        for (int row = 2; row < kRows - 2; row += kStride) {
            for (int column = 2; column < kColumns - 2; column += kStride) {
                const Seen seen = seen_at(kRoom, view, column, row);
                if (seen.solid == kNoSurface) {
                    continue;
                }
                // (a few pixels from any edge: the filter's footprint stays on the face)
                constexpr double kMargin = 4.0;
                bool inside_face = true;
                for (const auto& [dx, dy] : std::array<std::pair<double, double>, 4>{
                         {{kMargin, 0.0}, {-kMargin, 0.0}, {0.0, kMargin}, {0.0, -kMargin}}}) {
                    inside_face = inside_face && same_face(seen, seen_at(kRoom, view, column + dx, row + dy));
                }
                if (!inside_face) {
                    continue;
                }
                const std::array<float, 4>& got = texel_at(final_light, column, row);
                short_history += got.back() < static_cast<float>(render::vulkan::kAoMaxHistory) ? 1 : 0;
                const Rgb want = reference_light(kRoom, seen);
                double error = 0.0;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    error = std::max(error, std::abs(static_cast<double>(core::at(got, channel)) - core::at(want, channel)));
                }
                errors.push_back(error);
            }
        }
        std::ranges::sort(errors);
        double mean = 0.0;
        for (const double error : errors) {
            mean += error;
        }
        mean /= static_cast<double>(std::max<std::size_t>(1, errors.size()));
        constexpr double kPercentile = 0.99;
        const double p99 =
            errors.empty() ? 0.0 : core::at(errors, static_cast<std::size_t>(kPercentile * static_cast<double>(errors.size() - 1)));
        constexpr double kMeanBound = 0.02;
        constexpr double kP99Bound = 0.08;
        constexpr std::size_t kEnoughPixels = 3000;
        tally->check(errors.size() >= kEnoughPixels && mean <= kMeanBound && p99 <= kP99Bound,
                     text::format("against a 256-ray reference at %zu pixels: mean error %.4f (at most %.2f), 99th "
                                  "percentile %.4f (at most %.2f)",
                                  errors.size(), mean, kMeanBound, p99, kP99Bound));
        tally->check(short_history == 0, text::format("every history full (%u frames): %d not",
                                                      render::vulkan::kAoMaxHistory, short_history));
    }
    release_targets(gpu, targets);
}

int window_position(int display)
{
    int displays = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&displays);
    int chosen = display;
    if (chosen <= 0) {
        chosen = displays > 1 ? 2 : 1;
    }
    int position = static_cast<int>(SDL_WINDOWPOS_CENTERED);
    if (ids != nullptr && chosen <= displays) {
        const std::span<const SDL_DisplayID> listed(ids, static_cast<size_t>(displays));
        const SDL_DisplayID id = core::at(listed, chosen - 1);
        position = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(id));
    }
    SDL_free(ids);
    return position;
}

enum class Opened : uint8_t { kYes, kNoDevice, kNoRayQueries };
Opened open_device(const Options& options, SDL_Window* window)
{
    render::vulkan::set_context_options({.validation = options.validation,
                                         .device_index = options.device,
                                         .on_message = print_line,
                                         .ray_tracing = true},
                                        window);
    // (on before the device: its depth surfaces are made samplable)
    render::vulkan::set_rt_gi_settings(settings_for(true, kRawRays, false));
    render::GpuContext& context = render::vulkan::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return Opened::kNoDevice;
    }
    const render::vulkan::Backend& state = render::vulkan::backend();
    text::print("validation layer: %s\n",
                state.context.validation_on() ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWindowWidth,
                       .height = kWindowHeight,
                       .windowed = true,
                       .vsync = true,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24S8});
    const long made = context.create_device(
        {.width = kWindowWidth, .height = kWindowHeight, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
    if (made != value::kResultOk) {
        text::print("create_device failed (%ld)\n", made);
        context.shutdown();
        return Opened::kNoDevice;
    }
    context.mark_ready();
    text::print("device: %s\n", context.adapter().description.data());
    if (!state.context.ray_tracing_enabled() || rt::ray_tracing() == nullptr) {
        text::print("this device has no ray queries (VK_KHR_acceleration_structure + VK_KHR_ray_query) - nothing to "
                    "check\n");
        context.shutdown();
        return Opened::kNoRayQueries;
    }
    return Opened::kYes;
}

int run(const Options& options, SDL_Window* window)
{
    const Opened opened = open_device(options, window);
    if (opened == Opened::kNoRayQueries) {
        return 2;
    }
    if (opened == Opened::kNoDevice) {
        return render::vulkan::loader_version() == 0 ? 2 : 1;
    }
    Tally tally;
    render::Gpu& gpu = render::vulkan::gpu();
    std::array<SceneMesh, kRoom.size()> meshes{};
    bool made = true;
    for (std::size_t i = 0; i < kRoom.size(); ++i) {
        made = made && make_solid(gpu, core::at(kRoom, i), &core::at(meshes, i));
    }
    tally.check(made, "the scene made (a floor, a red wall, a pillar, their materials)");
    if (made) {
        constexpr uint32_t kFourSamples = 4;
        run_raw(gpu, meshes, 0, &tally);
        run_raw(gpu, meshes, kFourSamples, &tally);
        run_open(gpu, std::span(meshes).first(1), &tally);
        run_denoised(gpu, meshes, &tally);
    }
    for (SceneMesh& mesh : meshes) {
        release_solid(gpu, &mesh);
    }
    render::vulkan::set_rt_gi_settings({});
    rt::set_sun_override(nullptr);
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::finish_work(state);
    }
    render::vulkan::gpu_context().shutdown();
    tally.check(validation_messages() == 0, text::format("validation messages: %d", validation_messages()));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    set_log_listener(print_log_line);
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rt_gi_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    if (!render::vulkan::open_loader()) {
        text::print("no Vulkan loader (vulkan-1.dll) for this process\n");
        return 2;
    }
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        text::print("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int position = window_position(options.display);
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan ray-traced indirect light check", kWindowWidth, kWindowHeight,
                                          SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        text::print("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SDL_SetWindowPosition(window, position, position);
    SDL_ShowWindow(window);
    int result = 1;
    try {
        result = run(options, window);
    } catch (const std::exception& error) {
        text::print("the check stopped: %s\n", error.what());
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
