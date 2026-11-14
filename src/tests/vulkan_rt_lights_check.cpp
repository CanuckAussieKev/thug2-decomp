// NOT ORIGINAL - thug2_vulkan_rt_lights_check.exe: the night lighting ([Graphics] rt_lights - gpu_vulkan/
// vulkan_rt_lights.h, engine/gfx/rt_lights.h) tried on this machine against references worked out on the processor.
// Everything goes through the installed render::rt::RayTracing (R2's acceleration structures and geometry table, R3's
// material table), as render_pc's frame record feeds it.
//
// First the light sources on the processor alone: the emitters' clustering (a lamp of many triangles is one light, two
// lamps apart are two, a glow's flat pool lifted, a self-lit pool left out, a dark level's rule), the falloff's values,
// the frustum test and the view's selection (a lamp behind the camera left out, the most important first, the cap).
// Then the scene: a dark floor and a box standing on it (drawn darker than their albedo: "the bake at night"), a lamp
// beside the box - the frame record's point light, or the level's emitter - and the pass through the listener, as
// draw_viewport runs it. The processor answers each pixel from the camera's own position and lens: the surface it sees,
// each lamp's light there (colour x intensity x falloff x N.L), whether the shadow ray from it (started off the surface
// as the pass does, stopped the lamp's size short) is blocked by the box:
//   - "clear-cut" pixels (their 5x5 neighbourhood on the same face, every lamp's visibility the same, no two lamps
//     nearly tied for the last traced place) must be the drawn
//     colour + strength x albedo x light x (1 - drawn colour) (the 8-bit target's screen) within 2 per channel (one
//     sample) / 3 (four, resolved);
//   - the floor has a lit part and a part in the box's shadow (both a few hundred pixels);
//   - a red-textured lamp (an emitter whose material's texture is red) lights the floor red (its texture's mean);
//   - two lamps, one traced (rt_lights_shadowed=1): the other's light in the share of the traced one's that got through;
//   - rt_lights off, or no lamp in view: every pixel exactly as drawn (no pass).
//   thug2_vulkan_rt_lights_check [--device N] [--display N] [--no-validation] [--perf]
// --perf: then frames with 64 lamps in view, the pass's device time printed (its own log line, every 600 passes).
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
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "base/maths/view_transforms.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_lights.h"
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
constexpr std::size_t kPixelBytes = 4;
constexpr double kByteRange = 255.0;
constexpr double kOffsetScale = 1.0; // (the pass's shadow rays' start offset)
constexpr float kStrength = 1.5F;
constexpr int kClearCut = 2;
constexpr int kTolerance = 2;
constexpr int kToleranceSampled = 3;
constexpr int kMinLit = 300;
constexpr int kMinShadowed = 200;
constexpr uint32_t kWorldOpaque = rt::kWorld | rt::kOpaque | rt::kCastsShadow;
constexpr uint32_t kTextureSide = 2;
constexpr uint32_t kRedTexel = 0xFFFF2020U;

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
    bool perf = false; // --perf: 620 frames with 64 lamps in view, the pass's device time logged
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--perf") {
            options.perf = true;
        } else if (argument == "--no-validation") {
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
double length_of(const Vec3& v) { return std::hypot(x_of(v), y_of(v), z_of(v)); }
std::array<float, 3> floats(const Vec3& v)
{
    return {static_cast<float>(x_of(v)), static_cast<float>(y_of(v)), static_cast<float>(z_of(v))};
}

// --- the light sources on the processor ----------------------------------------------------------------------------

// A quad (two triangles) of an emissive surface: its corner, its two sides.
void add_quad(std::vector<rt::EmitterTriangle>* out, const Vec3& corner, const Vec3& side, const Vec3& other,
              rt::EmitterKind kind, const std::array<float, 3>& colour)
{
    const Vec3 a = corner;
    const Vec3 b = step(corner, side, 1.0);
    const Vec3 c = step(step(corner, side, 1.0), other, 1.0);
    const Vec3 d = step(corner, other, 1.0);
    out->push_back({.corners = {floats(a), floats(b), floats(c)}, .colour = colour, .material = 7, .kind = kind});
    out->push_back({.corners = {floats(a), floats(c), floats(d)}, .colour = colour, .material = 7, .kind = kind});
}

void check_sources(Tally* tally)
{
    text::print("the light sources (on the processor):\n");
    constexpr std::array<float, 3> kOrange{1.0F, 0.6F, 0.2F};
    // a lamp head of 4 x 4 small quads standing up (a vertical sign, 32 x 32 units), and another 600 units away
    std::vector<rt::EmitterTriangle> triangles;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            add_quad(&triangles, {(i * 8.0) - 16.0, 200.0 + (j * 8.0), 0.0}, {8.0, 0.0, 0.0}, {0.0, 8.0, 0.0},
                     rt::EmitterKind::kGlow, kOrange);
        }
    }
    add_quad(&triangles, {600.0, 200.0, 0.0}, {16.0, 0.0, 0.0}, {0.0, 16.0, 0.0}, rt::EmitterKind::kGlow, kOrange);
    std::vector<rt::Emitter> found = rt::cluster_emitters(triangles);
    tally->check(found.size() == 2, text::format("two lamps apart are two lights (%zu)", found.size()));
    if (found.size() == 2) {
        const rt::Emitter& big = found.front();
        tally->check(std::abs(big.position[0]) < 0.01F && std::abs(big.position[1] - 216.0F) < 0.01F &&
                         std::abs(big.position[2]) < 0.01F,
                     text::format("the sign of 32 triangles is one light at its centre (%.2f %.2f %.2f), the bigger "
                                  "first",
                                  static_cast<double>(big.position[0]), static_cast<double>(big.position[1]),
                                  static_cast<double>(big.position[2])));
        tally->check(std::abs(big.colour[0] - 1.0F) < 1e-5F && std::abs(big.colour[1] - 0.6F) < 1e-5F &&
                         std::abs(big.intensity - 1.0F) < 1e-4F && big.material == 7,
                     text::format("its colour the triangles' (brightest 1), intensity %.3f for 32 x 32 units, its "
                                  "material",
                                  static_cast<double>(big.intensity)));
        const float size = std::hypot(32.0F, 32.0F) * 0.5F;
        tally->check(std::abs(big.size - size) < 1e-3F &&
                         std::abs(big.reach - std::clamp(size * rt::kEmitterReachPerSize, rt::kEmitterMinReach,
                                                         rt::kEmitterMaxReach)) < 1e-3F,
                     text::format("its size %.2f (half its diagonal) and reach %.1f", static_cast<double>(big.size),
                                  static_cast<double>(big.reach)));
    }
    // a pool of light on the ground (flat, facing up): a glow is lifted by its size; a self-lit one is left out
    std::vector<rt::EmitterTriangle> pool;
    add_quad(&pool, {-40.0, 0.0, -40.0}, {80.0, 0.0, 0.0}, {0.0, 0.0, 80.0}, rt::EmitterKind::kGlow, kOrange);
    found = rt::cluster_emitters(pool);
    tally->check(found.size() == 1 && std::abs(found.front().position[1] - found.front().size) < 1e-3F,
                 "a glow's pool on the ground is a light lifted by its size");
    for (rt::EmitterTriangle& triangle : pool) {
        triangle.kind = rt::EmitterKind::kSelfLit;
    }
    tally->check(rt::cluster_emitters(pool).empty(), "a self-lit pool on the ground is no light");
    // the dark level's rule, the falloff
    tally->check(rt::dark_level(40.0F) && !rt::dark_level(90.0F) && rt::self_lit_triangle(120.0F, 40.0F) &&
                     !rt::self_lit_triangle(120.0F, 90.0F) && !rt::self_lit_triangle(90.0F, 30.0F),
                 "self-lit: only in a dark level, far brighter than it");
    const float reach = 400.0F;
    const float core = rt::kCoreShare * reach;
    tally->check(std::abs(rt::light_falloff(0.0F, 0.0F, reach) - 1.0F) < 1e-6F &&
                     rt::light_falloff(reach, 0.0F, reach) == 0.0F &&
                     std::abs(rt::light_falloff(core, 0.0F, reach) -
                              (0.5F * std::pow(1.0F - std::pow(rt::kCoreShare, 4.0F), 2.0F))) < 1e-5F,
                 "the falloff: 1 at the light, about 1/2 at its core's edge, 0 at its reach");
    // the view's selection: a camera at the origin looking down -z (D3D's right-handed look-at, as the game's)
    std::array<float, 16> view_matrix{};
    std::array<float, 16> projection{};
    constexpr std::array<float, 3> kOrigin{0.0F, 0.0F, 0.0F};
    constexpr std::array<float, 3> kAhead{0.0F, 0.0F, -1.0F};
    constexpr std::array<float, 3> kUpward{0.0F, 1.0F, 0.0F};
    maths::matrix_look_at_rh(view_matrix, kOrigin, kAhead, kUpward);
    maths::matrix_perspective_rh(projection, 2.0F, 1.5F, 1.0F, 5000.0F);
    rt::ViewLightRequest request;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += core::at(view_matrix, (row * 4) + k) * core::at(projection, (k * 4) + column);
            }
            core::at(core::at(request.view_projection, row), column) = sum;
        }
    }
    tally->check(rt::sphere_in_frustum(request.view_projection, {0.0F, 0.0F, -100.0F}, 1.0F) &&
                     !rt::sphere_in_frustum(request.view_projection, {0.0F, 0.0F, 3000.0F}, 400.0F) &&
                     rt::sphere_in_frustum(request.view_projection, {0.0F, 0.0F, 300.0F}, 400.0F),
                 "the frustum: ahead in, far behind out, a reach over the camera in");
    rt::FrameRecord record;
    record.lights.push_back({.kind = rt::Light::Kind::kPoint, .colour = {0.5F, 0.5F, 0.25F},
                             .position = {0.0F, 0.0F, -300.0F}, .radius = 400.0F});
    record.lights.push_back({.kind = rt::Light::Kind::kPoint, .colour = {1.0F, 1.0F, 1.0F},
                             .position = {0.0F, 0.0F, 3000.0F}, .radius = 400.0F}); // (behind: out)
    rt::LevelEmitters level;
    level.emitters.push_back({.position = {0.0F, 0.0F, -2000.0F}, .size = 10.0F, .colour = {1.0F, 1.0F, 1.0F},
                              .intensity = 1.0F, .reach = 500.0F, .material = 0, .kind = rt::EmitterKind::kGlow});
    request.camera = {0.0F, 0.0F, 0.0F};
    request.max = 8;
    std::vector<rt::Emitter> lights = rt::select_view_lights(record, &level, request);
    tally->check(lights.size() == 2 && lights.front().kind == rt::EmitterKind::kPoint &&
                     std::abs(lights.front().intensity - 0.5F) < 1e-6F &&
                     std::abs(lights.front().colour[2] - 0.5F) < 1e-6F,
                 text::format("the view's lights: the one behind left out, the near point light first (%zu)",
                              lights.size()));
    request.max = 1;
    lights = rt::select_view_lights(record, &level, request);
    request.emitters = false;
    const std::vector<rt::Emitter> points = rt::select_view_lights(record, &level, request);
    tally->check(lights.size() == 1 && points.size() == 1 && points.front().kind == rt::EmitterKind::kPoint,
                 "rt_lights_max caps them; the emitters can be left out");
}

// --- the scene ---------------------------------------------------------------------------------------------------

// A box (or, with no height, the floor square): its albedo (the material's colour), the colour it's drawn with.
struct Solid {
    Vec3 low{};
    Vec3 high{};
    Rgb albedo{};
    uint32_t drawn = 0;
};

constexpr double kFloorHalf = 150.0;
const std::array<Solid, 2> kScene{{
    {.low = {-kFloorHalf, 0.0, -kFloorHalf},
     .high = {kFloorHalf, 0.0, kFloorHalf},
     .albedo = {0.8, 0.8, 0.8},
     .drawn = 0xFF202020},
    {.low = {-10.0, 0.0, -20.0}, .high = {10.0, 30.0, 20.0}, .albedo = {0.5, 0.6, 0.7}, .drawn = 0xFF282C30},
}};

constexpr Vec3 kEye{0.0, 140.0, 190.0};
constexpr Vec3 kAt{0.0, 0.0, 0.0};
constexpr Vec3 kUp{0.0, 1.0, 0.0};

struct Hit {
    double distance = kNoHit;
    Vec3 normal{};
};
Hit intersect(const Solid& solid, const Vec3& origin, const Vec3& direction)
{
    constexpr double kParallel = 1e-12;
    double enter = 0.0;
    double leave = kNoHit;
    Vec3 normal{};
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
        }
        leave = std::min(leave, far_t);
        if (enter > leave) {
            return {};
        }
    }
    if (enter <= 0.0) {
        return {};
    }
    return {.distance = enter, .normal = normal};
}

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

struct Seen {
    std::size_t solid = kNoSurface;
    Vec3 normal{};
    Vec3 point{};
    double distance = 0.0;
};
Seen seen_at(const View& view, double px, double py)
{
    const double ndc_x = (2.0 * px / kTargetWidth) - 1.0;
    const double ndc_y = 1.0 - (2.0 * py / kTargetHeight);
    const Vec3 direction = normalised(
        step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height), view.back, -1.0));
    Seen seen;
    double nearest = kNoHit;
    for (std::size_t index = 0; index < kScene.size(); ++index) {
        const Hit hit = intersect(core::at(kScene, index), view.eye, direction);
        if (hit.distance < nearest) {
            nearest = hit.distance;
            seen = {.solid = index + 1, .normal = hit.normal, .point = step(view.eye, direction, hit.distance),
                    .distance = hit.distance};
        }
    }
    return seen;
}

// A lamp as the reference lights with it (its colour x intensity x its texture's tint).
struct Lamp {
    Vec3 position{};
    double size = 0.0;
    Rgb light{};
    double reach = 0.0;
};

double falloff(double d, double size, double reach)
{
    if (!(d < reach)) {
        return 0.0;
    }
    const double core = std::max(size, static_cast<double>(rt::kCoreShare) * reach);
    const double x = d / reach;
    const double window = 1.0 - (x * x * x * x);
    return window * window * core * core / ((core * core) + (d * d));
}

bool lamp_blocked(const Seen& seen, const Lamp& lamp)
{
    const double offset = kOffsetScale * (1.0 + (seen.distance * 0.0005) + (seen.distance * seen.distance * 6e-8));
    const Vec3 origin = step(seen.point, seen.normal, offset);
    const Vec3 to = minus(lamp.position, origin);
    const double dist = length_of(to);
    const double stop = dist - lamp.size;
    if (stop <= 0.0) {
        return false;
    }
    const Vec3 direction = normalised(to);
    for (const Solid& solid : kScene) {
        const Hit hit = intersect(solid, origin, direction);
        if (hit.distance <= stop) {
            return true;
        }
    }
    return false;
}

struct Answer {
    Rgb added{};                 // strength x albedo x light
    std::vector<bool> blocked{}; // each lamp's shadow ray
    bool close = false;          // the last lamp traced and the first not nearly as bright: either may be picked
};
// The pass's answer at a surface: every lamp's light, the brightest `traced` traced, the rest in the traced ones' share.
Answer answer_at(const Seen& seen, std::span<const Lamp> lamps, unsigned traced)
{
    Answer answer;
    struct Each {
        Rgb light{};
        double weight = 0.0;
        bool blocked = false;
    };
    std::vector<Each> each;
    for (const Lamp& lamp : lamps) {
        const Vec3 to = minus(lamp.position, seen.point);
        const double dist = length_of(to);
        const double facing = dot(seen.normal, normalised(to));
        Each item;
        item.blocked = lamp_blocked(seen, lamp);
        answer.blocked.push_back(item.blocked);
        if (dist < lamp.reach && facing > 0.0) {
            const double scale = falloff(dist, lamp.size, lamp.reach) * facing;
            item.light = {core::at(lamp.light, 0) * scale, core::at(lamp.light, 1) * scale, core::at(lamp.light, 2) * scale};
            item.weight = (0.299 * item.light[0]) + (0.587 * item.light[1]) + (0.114 * item.light[2]);
        }
        each.push_back(item);
    }
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < each.size(); ++i) {
        if (core::at(each, i).weight > 0.0) {
            order.push_back(i);
        }
    }
    std::ranges::stable_sort(order, [&](std::size_t one, std::size_t other) {
        return core::at(each, one).weight > core::at(each, other).weight;
    });
    constexpr double kClose = 0.02;
    if (order.size() > traced && traced > 0) {
        const double last = core::at(each, core::at(order, traced - 1)).weight;
        const double next = core::at(each, core::at(order, traced)).weight;
        answer.close = last - next <= kClose * last;
    }
    Rgb lit{};
    Rgb rest{};
    double through = 0.0;
    double total = 0.0;
    for (std::size_t k = 0; k < order.size(); ++k) {
        const Each& item = core::at(each, core::at(order, k));
        if (k < traced) {
            const double seen_share = item.blocked ? 0.0 : 1.0;
            for (std::size_t c = 0; c < 3; ++c) {
                core::at(lit, c) += core::at(item.light, c) * seen_share;
            }
            through += seen_share * item.weight;
            total += item.weight;
        } else {
            for (std::size_t c = 0; c < 3; ++c) {
                core::at(rest, c) += core::at(item.light, c);
            }
        }
    }
    const Rgb& albedo = core::at(kScene, seen.solid - 1).albedo;
    for (std::size_t c = 0; c < 3; ++c) {
        const double share = total > 0.0 ? through / total : 0.0;
        core::at(answer.added, c) =
            static_cast<double>(kStrength) * core::at(albedo, c) * (core::at(lit, c) + (core::at(rest, c) * share));
    }
    return answer;
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
    quad({{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}});
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

// The red glow's material: a 2 x 2 texture all one red (its mean: the light's tint).
bool make_red_material(render::Gpu& gpu, render::GpuTexture** texture, rt::MaterialId* material)
{
    if (gpu.create_texture({.width = kTextureSide,
                            .height = kTextureSide,
                            .levels = 1,
                            .usage = 0,
                            .format = value::kFormatA8R8G8B8,
                            .pool = value::kPoolManaged},
                           texture) != value::kResultOk) {
        *texture = nullptr;
        return false;
    }
    render::GpuLockedRect locked;
    if (gpu.lock_texture(*texture, 0, &locked, nullptr, 0) != value::kResultOk) {
        return false;
    }
    const auto pitch = static_cast<std::size_t>(locked.pitch);
    const std::span<uint8_t> into(static_cast<uint8_t*>(locked.bits), pitch + (kTextureSide * kPixelBytes));
    for (uint32_t row = 0; row < kTextureSide; ++row) {
        for (uint32_t column = 0; column < kTextureSide; ++column) {
            put_word(into, (row * pitch) + (column * kPixelBytes), kRedTexel);
        }
    }
    gpu.unlock_texture(*texture, 0);
    *material = rt::next_material_id();
    rt::Material surface;
    surface.texture = *texture;
    surface.flags = rt::kTransparent;
    rt::ray_tracing()->set_material(*material, surface);
    return true;
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

// A frame: the record given (the instances, an ambient light, `points`), cleared, the scene drawn, the listeners
// (draw_viewport's call). Returns the lights passes recorded.
uint64_t draw_frame(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes,
                    std::span<const rt::Light> points, uint64_t frame)
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
    record.lights.push_back({.kind = rt::Light::Kind::kAmbient, .colour = {0.1F, 0.1F, 0.15F}});
    for (const rt::Light& light : points) {
        record.lights.push_back(light);
    }
    gpu.begin_scene();
    rt::ray_tracing()->begin_frame(record);
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kClearColour, 1.0F, 0);
    draw_scene(gpu, meshes);
    const uint64_t before = render::vulkan::rt_lights_passes();
    rt::opaque_view_drawn(view_drawn());
    const uint64_t passes = render::vulkan::rt_lights_passes() - before;
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

Rgb drawn_colour(uint32_t argb)
{
    return {static_cast<double>((argb >> 16U) & 0xFFU) / kByteRange,
            static_cast<double>((argb >> 8U) & 0xFFU) / kByteRange, static_cast<double>(argb & 0xFFU) / kByteRange};
}

render::vulkan::RtLightSettings settings_for(bool on, unsigned shadowed)
{
    render::vulkan::RtLightSettings settings;
    settings.on = on;
    settings.strength = kStrength;
    settings.shadowed = shadowed;
    return settings;
}

// What a frame's colours are against: every pixel exactly as drawn (nothing added).
int pixels_changed(const std::vector<uint8_t>& texels, const View& view)
{
    int changed = 0;
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const Seen seen = seen_at(view, x, y);
            if (seen.solid == kNoSurface) {
                continue;
            }
            const Rgb drawn = drawn_colour(core::at(kScene, seen.solid - 1).drawn);
            const Rgb got = pixel_at(texels, x, y);
            for (std::size_t c = 0; c < 3; ++c) {
                if (std::lround(core::at(got, c) * kByteRange) != std::lround(core::at(drawn, c) * kByteRange)) {
                    ++changed;
                    break;
                }
            }
        }
    }
    return changed;
}

// The frame against the reference: the clear-cut pixels within the tolerance. Returns false when the frame failed.
struct Compared {
    int clear_cut = 0;
    int wrong = 0;
    int lit_floor = 0;
    int shadowed_floor = 0;
    double worst = 0.0;
    double red = 0.0;   // the lit floor's added light, summed per channel
    double green = 0.0;
};
Compared compare(const std::vector<uint8_t>& texels, const View& view, std::span<const Lamp> lamps, unsigned traced,
                 int tolerance)
{
    Compared out;
    std::vector<Seen> seen(static_cast<std::size_t>(kColumns) * kRows);
    std::vector<Answer> answers(seen.size());
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * kColumns) + static_cast<std::size_t>(x);
            core::at(seen, at) = seen_at(view, x, y);
            if (core::at(seen, at).solid != kNoSurface) {
                core::at(answers, at) = answer_at(core::at(seen, at), lamps, traced);
            }
        }
    }
    for (int y = kClearCut; y < kRows - kClearCut; ++y) {
        for (int x = kClearCut; x < kColumns - kClearCut; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * kColumns) + static_cast<std::size_t>(x);
            const Seen& here = core::at(seen, at);
            if (here.solid == kNoSurface) {
                continue;
            }
            bool clear = true;
            for (int dy = -kClearCut; dy <= kClearCut && clear; ++dy) {
                for (int dx = -kClearCut; dx <= kClearCut && clear; ++dx) {
                    const std::size_t other = (static_cast<std::size_t>(y + dy) * kColumns) + static_cast<std::size_t>(x + dx);
                    clear = core::at(seen, other).solid == here.solid && core::at(seen, other).normal == here.normal &&
                            core::at(answers, other).blocked == core::at(answers, at).blocked &&
                            !core::at(answers, other).close;
                }
            }
            if (!clear) {
                continue;
            }
            ++out.clear_cut;
            const Answer& answer = core::at(answers, at);
            const Rgb drawn = drawn_colour(core::at(kScene, here.solid - 1).drawn);
            const Rgb got = pixel_at(texels, x, y);
            bool bad = false;
            for (std::size_t c = 0; c < 3; ++c) {
                const double added = std::max(0.0, core::at(answer.added, c));
                const double expected = std::min(1.0, core::at(drawn, c) + (added * (1.0 - core::at(drawn, c))));
                const double error = std::abs(core::at(got, c) - expected) * kByteRange;
                out.worst = std::max(out.worst, error);
                bad = bad || error > tolerance;
            }
            out.wrong += bad ? 1 : 0;
            const double light = (core::at(answer.added, 0) + core::at(answer.added, 1) + core::at(answer.added, 2));
            if (here.solid == 1 && y_of(here.normal) > 0.5 && light > 0.0) {
                out.red += core::at(got, 0) - core::at(drawn, 0);
                out.green += core::at(got, 1) - core::at(drawn, 1);
            }
            if (here.solid == 1 && y_of(here.normal) > 0.5 && !answer.blocked.empty()) {
                const bool any_blocked = std::ranges::any_of(answer.blocked, [](bool b) { return b; });
                out.shadowed_floor += any_blocked ? 1 : 0;
                out.lit_floor += any_blocked ? 0 : 1;
            }
        }
    }
    return out;
}

// One case: the frame drawn with `points` and `emitters`, compared with the reference for `lamps`.
Compared run_case(render::Gpu& gpu, std::span<const SceneMesh> meshes, uint32_t samples, std::span<const rt::Light> points,
                  std::span<const Lamp> lamps, unsigned traced, uint64_t* passes, std::vector<uint8_t>* texels)
{
    Targets targets;
    Compared compared;
    *passes = 0;
    if (make_targets(gpu, samples, &targets)) {
        *passes = draw_frame(gpu, targets, meshes, points, 1);
        if (read_colour(gpu, targets, texels)) {
            compared = compare(*texels, view_from(kEye), lamps, traced,
                               samples == 0 ? kTolerance : kToleranceSampled);
        }
    }
    release_targets(gpu, targets);
    return compared;
}

constexpr Vec3 kLampAt{-45.0, 40.0, 10.0};
constexpr double kLampReach = 400.0;

void run_point(render::Gpu& gpu, std::span<const SceneMesh> meshes, uint32_t samples, Tally* tally)
{
    text::print("a point light beside the box (%s):\n", samples == 0 ? "one sample" : "four samples, resolved");
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
    rt::set_level_emitters({});
    const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 0.9F, 0.7F},
                                            .position = floats(kLampAt),
                                            .radius = static_cast<float>(kLampReach)}}};
    const std::array<Lamp, 1> lamps{{{.position = kLampAt, .size = 0.0, .light = {1.0, 0.9, 0.7}, .reach = kLampReach}}};
    uint64_t passes = 0;
    std::vector<uint8_t> texels;
    const Compared got = run_case(gpu, meshes, samples, points, lamps, 4, &passes, &texels);
    tally->check(passes == 1, text::format("the pass recorded (%llu)", static_cast<unsigned long long>(passes)));
    tally->check(got.clear_cut > 5000 && got.wrong == 0,
                 text::format("%d clear-cut pixels as the processor's lamp light, %d wrong (worst %.2f of 255)",
                              got.clear_cut, got.wrong, got.worst));
    tally->check(got.lit_floor >= kMinLit && got.shadowed_floor >= kMinShadowed,
                 text::format("the floor lit (%d) and in the box's shadow (%d)", got.lit_floor, got.shadowed_floor));
}

void run_emitter(render::Gpu& gpu, std::span<const SceneMesh> meshes, rt::MaterialId red, Tally* tally)
{
    text::print("a red glow (the level's emitter, its material's texture red):\n");
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
    rt::LevelEmitters level;
    level.serial = 1;
    constexpr float kSize = 6.0F;
    level.emitters.push_back({.position = floats(kLampAt), .size = kSize, .colour = {1.0F, 1.0F, 1.0F},
                              .intensity = 1.2F, .reach = static_cast<float>(kLampReach), .material = red,
                              .kind = rt::EmitterKind::kGlow});
    rt::set_level_emitters(level);
    // the texture's mean: (1, 0x20 / 0xFF, 0x20 / 0xFF), its brightest channel 1
    constexpr double kTintGreen = 32.0 / 255.0;
    const std::array<Lamp, 1> lamps{
        {{.position = kLampAt, .size = kSize, .light = {1.2, 1.2 * kTintGreen, 1.2 * kTintGreen}, .reach = kLampReach}}};
    uint64_t passes = 0;
    std::vector<uint8_t> texels;
    const Compared got = run_case(gpu, meshes, 0, {}, lamps, 4, &passes, &texels);
    tally->check(passes == 1 && got.clear_cut > 5000 && got.wrong == 0,
                 text::format("%d clear-cut pixels as the processor's red glow, %d wrong (worst %.2f of 255)",
                              got.clear_cut, got.wrong, got.worst));
    tally->check(got.red > 4.0 * got.green && got.red > 1.0,
                 text::format("the floor lit red (red %.1f, green %.1f added)", got.red, got.green));
    rt::set_level_emitters({});
}

void run_two(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("two lamps, one traced (rt_lights_shadowed=1):\n");
    render::vulkan::set_rt_light_settings(settings_for(true, 1));
    rt::set_level_emitters({});
    constexpr Vec3 kOther{50.0, 25.0, -40.0};
    const std::array<rt::Light, 2> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 0.9F, 0.7F},
                                            .position = floats(kLampAt),
                                            .radius = static_cast<float>(kLampReach)},
                                           {.kind = rt::Light::Kind::kPoint,
                                            .colour = {0.3F, 0.4F, 0.8F},
                                            .position = floats(kOther),
                                            .radius = 300.0F}}};
    const std::array<Lamp, 2> lamps{{{.position = kLampAt, .size = 0.0, .light = {1.0, 0.9, 0.7}, .reach = kLampReach},
                                     {.position = kOther, .size = 0.0, .light = {0.3, 0.4, 0.8}, .reach = 300.0}}};
    uint64_t passes = 0;
    std::vector<uint8_t> texels;
    const Compared got = run_case(gpu, meshes, 0, points, lamps, 1, &passes, &texels);
    tally->check(passes == 1 && got.clear_cut > 3000 && got.wrong == 0,
                 text::format("%d clear-cut pixels as the processor's, %d wrong (worst %.2f of 255)", got.clear_cut,
                              got.wrong, got.worst));
}

void run_off(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("off, and no lamp in view:\n");
    const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 1.0F, 1.0F},
                                            .position = floats(kLampAt),
                                            .radius = static_cast<float>(kLampReach)}}};
    render::vulkan::set_rt_light_settings(settings_for(false, 4));
    uint64_t passes = 0;
    std::vector<uint8_t> texels;
    run_case(gpu, meshes, 0, points, {}, 4, &passes, &texels);
    const int changed_off = texels.empty() ? -1 : pixels_changed(texels, view_from(kEye));
    tally->check(passes == 0 && changed_off == 0,
                 text::format("rt_lights=0: no pass, every pixel as drawn (%d changed)", changed_off));
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
    const std::array<rt::Light, 1> behind{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 1.0F, 1.0F},
                                            .position = {0.0F, 140.0F, 900.0F},
                                            .radius = 300.0F}}};
    run_case(gpu, meshes, 0, behind, {}, 4, &passes, &texels);
    const int changed_behind = texels.empty() ? -1 : pixels_changed(texels, view_from(kEye));
    tally->check(passes == 0 && changed_behind == 0,
                 text::format("a lamp behind the camera out of reach: no pass, every pixel as drawn (%d changed)",
                              changed_behind));
}

// --perf: frames with kPerfLamps lamps over the floor (all in view), the pass's own timing line (vulkan_rt_lights.cpp:
// every 600 passes) printed through the log.
void run_perf(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    constexpr int kPerfFrames = 700; // (some timings are lost when their frame hasn't finished: 600 measured)
    constexpr int kPerfLamps = 64;
    constexpr int kPerfSide = 8;
    constexpr double kPerfSpacing = 36.0;
    constexpr double kPerfMiddle = 3.5;
    text::print("timing: %d frames, %d lamps in view, 4 traced a pixel (%ux%u):\n", kPerfFrames, kPerfLamps,
                kTargetWidth, kTargetHeight);
    render::vulkan::RtLightSettings settings = settings_for(true, 4);
    settings.max_lights = kPerfLamps;
    render::vulkan::set_rt_light_settings(settings);
    std::vector<rt::Light> points;
    for (int i = 0; i < kPerfLamps; ++i) {
        const double x = ((i % kPerfSide) - kPerfMiddle) * kPerfSpacing;
        const double z = ((i / kPerfSide) - kPerfMiddle) * kPerfSpacing;
        points.push_back({.kind = rt::Light::Kind::kPoint,
                          .colour = {0.5F, 0.45F, 0.35F},
                          .position = {static_cast<float>(x), 35.0F, static_cast<float>(z)},
                          .radius = 250.0F});
    }
    Targets targets;
    uint64_t passes = 0;
    if (make_targets(gpu, 0, &targets)) {
        for (int frame = 0; frame < kPerfFrames; ++frame) {
            passes += draw_frame(gpu, targets, meshes, points, static_cast<uint64_t>(frame) + 1);
        }
    }
    release_targets(gpu, targets);
    tally->check(passes == kPerfFrames, text::format("%llu passes timed", static_cast<unsigned long long>(passes)));
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
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
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

int run(const Options& options, SDL_Window* window, Tally* tally)
{
    const Opened opened = open_device(options, window);
    if (opened == Opened::kNoRayQueries) {
        return 2;
    }
    if (opened == Opened::kNoDevice) {
        return render::vulkan::loader_version() == 0 ? 2 : 1;
    }
    render::Gpu& gpu = render::vulkan::gpu();
    std::array<SceneMesh, kScene.size()> meshes{};
    bool made = true;
    for (std::size_t i = 0; i < kScene.size(); ++i) {
        made = made && make_solid(gpu, core::at(kScene, i), &core::at(meshes, i));
    }
    render::GpuTexture* red_texture = nullptr;
    rt::MaterialId red = 0;
    made = made && make_red_material(gpu, &red_texture, &red);
    tally->check(made, "the scene made (a floor, a box, their materials, the red glow's)");
    if (made) {
        constexpr uint32_t kFourSamples = 4;
        run_point(gpu, meshes, 0, tally);
        run_point(gpu, meshes, kFourSamples, tally);
        run_emitter(gpu, meshes, red, tally);
        run_two(gpu, meshes, tally);
        run_off(gpu, meshes, tally);
        if (options.perf) {
            run_perf(gpu, meshes, tally);
        }
    }
    for (SceneMesh& mesh : meshes) {
        release_solid(gpu, &mesh);
    }
    if (red != 0) {
        rt::ray_tracing()->remove_material(red);
    }
    if (red_texture != nullptr) {
        gpu.release_texture(red_texture);
    }
    render::vulkan::set_rt_light_settings({});
    rt::set_level_emitters({});
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::finish_work(state);
    }
    render::vulkan::gpu_context().shutdown();
    return -1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    set_log_listener(print_log_line);
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rt_lights_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    Tally tally;
    check_sources(&tally);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan ray-traced lights check", kWindowWidth, kWindowHeight,
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
        result = run(options, window, &tally);
    } catch (const std::exception& error) {
        text::print("the check stopped: %s\n", error.what());
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (result >= 0) {
        return result;
    }
    tally.check(validation_messages() == 0, text::format("validation messages: %d", validation_messages()));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}
