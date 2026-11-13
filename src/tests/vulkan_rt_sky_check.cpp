// NOT ORIGINAL - thug2_vulkan_rt_sky_check.exe: the Vulkan Gpu's sky (gpu_vulkan/vulkan_rt_sky_pass.h, vulkan_rt_sky.h,
// vulkan_rt_clouds.h; engine/gfx/rt_sky.h, rt_sky_match.h, rt_clouds.h) tried on this machine against references worked
// out on the processor.
//
// A ground square and a wall drawn through render::Gpu as render_pc draws a level; rt::sky_view_drawn runs the sky's
// passes through its listener, as draw_viewport does after the sky's backdrop. The sun set by hand (the console's
// `rt sun` override).
//   - The atmosphere's tables: the transmittance table's texels against rt_sky.h's integral (2048 segments), Psi against
//     rt_sky.h's multiple_scattering (the same directions and segments; the sun's transmittance integrated) and the
//     sky-view table (unmatched) against sky_radiance_ms with the processor's own Psi table, at two sun elevations;
//     the multiple scattering brightening the sky; the zenith bluer than the horizon.
//   - The match: a capture made here (a blue sky darkening upward, white cloud blobs, a dark sea below) analysed by the
//     device as rt_sky_match.h does on the processor (the same numbers), and the matched sky's bands brought to the
//     capture's colours (rt_sky_match 1) or left as the model's (0).
//   - The composite (8-bit and 16-bit float targets): every clear-cut sky pixel the sky-view table toward it, as the
//     game's display-referred colour; every surface pixel unchanged; values above 1 kept only in the float target.
//   - The clouds: the device's noise against rt_clouds.h's texels (sampled), every marched texel against the
//     processor's march of the same ray on the device's noise; after still frames the history steady; a turned camera's
//     history held inside this frame's neighbourhood (no ghost); no NaN anywhere.
//   - rt_sky=0: no pass, no replacement asked of render_pc, the picture unchanged.
//   - --perf: the passes' cost at 1920 x 1080 (their timestamps).
//   thug2_vulkan_rt_sky_check [--device N] [--display N] [--no-validation] [--perf]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan.
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
#include <numbers>
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
#include "engine/gfx/rt_clouds.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"
#include "engine/gfx/rt_sky_match.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_clouds.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "gpu_vulkan/vulkan_rt_sky_pass.h"
#include "platform/run_log.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
namespace vk = render::vulkan;
namespace rt = render::rt;
using Vec3 = std::array<double, 3>;
using Rgb = std::array<double, 3>;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr uint32_t kTargetWidth = 320;
constexpr uint32_t kTargetHeight = 240;
constexpr uint32_t kPerfWidth = 1920;
constexpr uint32_t kPerfHeight = 1080;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x042; // XYZ | DIFFUSE
constexpr uint32_t kClearColour = 0xFF6080B0;
constexpr float kNearZ = 2.0F;
constexpr float kFarZ = 4000.0F;
constexpr double kHalfFovTan = 0.57735026918962576; // a 60-degree horizontal field of view
constexpr double kNoHit = std::numeric_limits<double>::infinity();
constexpr std::size_t kPixelBytes = 4;
constexpr std::size_t kHalfPixelBytes = 8;
constexpr double kPerByte = 1.0 / 255.0;
constexpr double kPi = std::numbers::pi;
constexpr double kDegrees = kPi / 180.0;
constexpr double kHalf = 0.5;

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
    bool perf = false;
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--no-validation") {
            options.validation = false;
        } else if (argument == "--perf") {
            options.perf = true;
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

// --- maths ------------------------------------------------------------------------------------------------------------

double x_of(const Vec3& v) { return core::at(v, 0); }
double y_of(const Vec3& v) { return core::at(v, 1); }
double z_of(const Vec3& v) { return core::at(v, 2); }
Vec3 normalised(const Vec3& v)
{
    const double length = std::hypot(x_of(v), y_of(v), z_of(v));
    return {x_of(v) / length, y_of(v) / length, z_of(v) / length};
}
double dot(const Vec3& a, const Vec3& b) { return (x_of(a) * x_of(b)) + (y_of(a) * y_of(b)) + (z_of(a) * z_of(b)); }
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {(y_of(a) * z_of(b)) - (z_of(a) * y_of(b)), (z_of(a) * x_of(b)) - (x_of(a) * z_of(b)),
            (x_of(a) * y_of(b)) - (y_of(a) * x_of(b))};
}
Vec3 step(const Vec3& from, const Vec3& along, double scale)
{
    return {x_of(from) + (x_of(along) * scale), y_of(from) + (y_of(along) * scale), z_of(from) + (z_of(along) * scale)};
}
Vec3 minus(const Vec3& a, const Vec3& b) { return step(a, b, -1.0); }
std::array<float, 3> floats(const Vec3& v)
{
    return {static_cast<float>(x_of(v)), static_cast<float>(y_of(v)), static_cast<float>(z_of(v))};
}
double luma(const Rgb& c) { return (0.2126 * c[0]) + (0.7152 * c[1]) + (0.0722 * c[2]); }

double percentile(std::vector<double> values, double share)
{
    if (values.empty()) {
        return 0.0;
    }
    const auto at = static_cast<std::size_t>(share * static_cast<double>(values.size() - 1));
    std::ranges::nth_element(values, values.begin() + static_cast<std::ptrdiff_t>(at));
    return core::at(values, at);
}
double mean_of(const std::vector<double>& values)
{
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}
// relative where it is bright enough, else absolute (against `floor`)
double error_of(double got, double want, double floor) { return std::abs(got - want) / std::max(std::abs(want), floor); }

// --- the scene ---------------------------------------------------------------------------------------------------------

struct Solid {
    Vec3 low{};
    Vec3 high{};
    uint32_t colour = 0;
};
constexpr double kGroundHalf = 300.0;
const std::array<Solid, 2> kSolids{{
    {.low = {-kGroundHalf, 0.0, -kGroundHalf}, .high = {kGroundHalf, 0.0, kGroundHalf}, .colour = 0xFF909090},
    {.low = {-150.0, 0.0, -2.0}, .high = {150.0, 120.0, 2.0}, .colour = 0xFFA08060},
}};

const Vec3& to_sun()
{
    static const Vec3 kToward = normalised({-0.3, 0.45, -1.0}); // ahead, 23 degrees up
    return kToward;
}
constexpr Vec3 kUp{0.0, 1.0, 0.0};

struct Pose {
    Vec3 eye{};
    Vec3 at{};
};
// in front of the wall, looking past its top into the sky (the sun ahead)
Pose sky_pose() { return {.eye = {0.0, 60.0, 260.0}, .at = {0.0, 160.0, 0.0}}; }
// the same eye turned 8 degrees to the side
Pose turned_pose()
{
    const double turn = 8.0 * kDegrees;
    const Pose pose = sky_pose();
    const Vec3 d = minus(pose.at, pose.eye);
    return {.eye = pose.eye,
            .at = step(pose.eye, Vec3{(x_of(d) * std::cos(turn)) + (z_of(d) * std::sin(turn)), y_of(d),
                                      (-x_of(d) * std::sin(turn)) + (z_of(d) * std::cos(turn))},
                       1.0)};
}

using Corner = std::array<int, 3>;
std::vector<std::array<float, 3>> triangles_of(const Solid& solid)
{
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

double intersect(const Solid& solid, const Vec3& origin, const Vec3& direction)
{
    double enter = 0.0;
    double leave = kNoHit;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double o = core::at(origin, axis);
        const double d = core::at(direction, axis);
        const double low = core::at(solid.low, axis);
        const double high = core::at(solid.high, axis);
        if (std::abs(d) < 1e-12) {
            if (o < low || o > high) {
                return kNoHit;
            }
            continue;
        }
        enter = std::max(enter, ((d > 0.0 ? low : high) - o) / d);
        leave = std::min(leave, ((d > 0.0 ? high : low) - o) / d);
        if (enter > leave) {
            return kNoHit;
        }
    }
    return enter <= 0.0 ? kNoHit : enter;
}

struct View {
    Vec3 eye{};
    Vec3 right{};
    Vec3 up{};
    Vec3 back{};
    double half_width = 0.0;
    double half_height = 0.0;
    uint32_t width = 0;
    uint32_t height = 0;
};
View view_from(const Pose& pose, uint32_t width, uint32_t height)
{
    View view;
    view.eye = pose.eye;
    view.back = normalised(minus(pose.eye, pose.at));
    view.right = normalised(cross(kUp, view.back));
    view.up = cross(view.back, view.right);
    view.half_width = kHalfFovTan;
    view.half_height = kHalfFovTan * static_cast<double>(height) / static_cast<double>(width);
    view.width = width;
    view.height = height;
    return view;
}
// the direction through a point of the target (whole coordinates: pixel centres)
Vec3 direction_at(const View& view, double x, double y)
{
    const double ndc_x = (x / (kHalf * view.width)) - 1.0;
    const double ndc_y = 1.0 - (y / (kHalf * view.height));
    return normalised(step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height),
                           view.back, -1.0));
}
bool sky_at(const View& view, double x, double y)
{
    const Vec3 d = direction_at(view, x, y);
    return std::ranges::all_of(kSolids, [&](const Solid& solid) { return intersect(solid, view.eye, d) == kNoHit; });
}
// sky or surface in a whole 3 x 3 neighbourhood
bool clear_cut(const View& view, int x, int y, bool* sky)
{
    *sky = sky_at(view, x, y);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (sky_at(view, x + dx, y + dy) != *sky) {
                return false;
            }
        }
    }
    return true;
}

// --- drawing it through the Gpu --------------------------------------------------------------------------------------

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
Matrices matrices_for(const Pose& pose, uint32_t width, uint32_t height)
{
    Matrices out;
    maths::matrix_look_at_rh(out.view, floats(pose.eye), floats(pose.at), floats(kUp));
    const double lens_width = 2.0 * kNearZ * kHalfFovTan;
    const double lens_height = lens_width * static_cast<double>(height) / static_cast<double>(width);
    maths::matrix_perspective_rh(out.projection, static_cast<float>(lens_width), static_cast<float>(lens_height), kNearZ,
                                 kFarZ);
    return out;
}

void draw_scene(render::Gpu& gpu, const Pose& pose, uint32_t width, uint32_t height)
{
    using render::GpuRenderState;
    using render::GpuStageState;
    gpu.set_viewport({.x = 0, .y = 0, .width = width, .height = height, .min_z = 0.0F, .max_z = 1.0F});
    const Matrices matrices = matrices_for(pose, width, height);
    constexpr Matrix kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    gpu.set_transform(render::GpuTransform::kWorld, kIdentity.data());
    gpu.set_transform(render::GpuTransform::kView, matrices.view.data());
    gpu.set_transform(render::GpuTransform::kProjection, matrices.projection.data());
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_vertex_format(kFvf);
    gpu.set_texture(0, nullptr);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgDiffuse);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgDiffuse);
    std::vector<ColourVertex> vertices;
    for (const Solid& solid : kSolids) {
        for (const std::array<float, 3>& corner : triangles_of(solid)) {
            vertices.push_back({.position = corner, .colour = solid.colour});
        }
    }
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(vertices.size() / 3), vertices.data(),
                         sizeof(ColourVertex));
}

rt::ViewDrawn view_drawn(const Pose& pose, uint32_t width, uint32_t height)
{
    const Matrices matrices = matrices_for(pose, width, height);
    rt::ViewDrawn view;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(core::at(view.camera.view, row), column) = core::at(matrices.view, (row * 4) + column);
            core::at(core::at(view.camera.projection, row), column) = core::at(matrices.projection, (row * 4) + column);
        }
    }
    view.camera.position = floats(pose.eye);
    view.rect = {0, 0, width, height};
    view.depth_range = {0.0F, 1.0F};
    view.game_seconds = 0.0;
    return view;
}

struct Targets {
    GpuSurface* colour = nullptr;
    GpuSurface* depth = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    bool floats = false;
};
bool make_targets(render::Gpu& gpu, uint32_t width, uint32_t height, bool floats, Targets* out)
{
    out->width = width;
    out->height = height;
    out->floats = floats;
    const render::GpuSurfaceDesc colour{.width = width,
                                        .height = height,
                                        .format = floats ? value::kFormatA16B16G16R16F : value::kFormatX8R8G8B8,
                                        .samples = 0,
                                        .quality = 0,
                                        .lockable_or_discard = true};
    render::GpuSurfaceDesc depth = colour;
    depth.format = value::kFormatD24S8;
    depth.lockable_or_discard = false;
    return gpu.create_render_target(colour, &out->colour) == value::kResultOk &&
           gpu.create_depth_surface(depth, &out->depth) == value::kResultOk;
}
void release_targets(render::Gpu& gpu, const Targets& targets)
{
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    for (GpuSurface* surface : {targets.colour, targets.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
}

// One frame: the scene, then the sky's listeners (what draw_viewport does after the sky's backdrop). Returns the
// passes recorded.
uint64_t draw_frame(render::Gpu& gpu, const Targets& targets, const Pose& pose)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = targets.width, .height = targets.height, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kClearColour, 1.0F, 0);
    draw_scene(gpu, pose, targets.width, targets.height);
    const std::array<float, 3> toward = floats(to_sun());
    rt::set_sun_override(&toward);
    const uint64_t before = vk::rt_sky_passes();
    if (rt::sky_replacement().on) {
        rt::sky_view_drawn(view_drawn(pose, targets.width, targets.height));
    }
    const uint64_t passes = vk::rt_sky_passes() - before;
    gpu.end_scene();
    return passes;
}

bool read_colour(const Targets& targets, std::vector<Rgb>* out)
{
    std::vector<uint8_t> bytes;
    const std::size_t pixels = static_cast<std::size_t>(targets.width) * targets.height;
    if (!vk::read_surface(targets.colour, &bytes) || bytes.size() != pixels * (targets.floats ? kHalfPixelBytes : kPixelBytes)) {
        return false;
    }
    out->resize(pixels);
    for (std::size_t i = 0; i < pixels; ++i) {
        Rgb& colour = core::at(*out, i);
        if (targets.floats) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                uint16_t half = 0;
                std::memcpy(&half, &core::at(bytes, (i * kHalfPixelBytes) + (channel * sizeof(uint16_t))), sizeof(half));
                core::at(colour, channel) = vk::half_to_float(half);
            }
        } else {
            core::at(colour, 0) = core::at(bytes, (i * kPixelBytes) + 2) * kPerByte;
            core::at(colour, 1) = core::at(bytes, (i * kPixelBytes) + 1) * kPerByte;
            core::at(colour, 2) = core::at(bytes, (i * kPixelBytes) + 0) * kPerByte;
        }
    }
    return true;
}

vk::RtSkySettings sky_settings(bool clouds, bool hdr, float match = 1.0F)
{
    vk::RtSkySettings settings;
    settings.on = true;
    settings.match = match;
    settings.clouds = clouds;
    settings.coverage = 0.45F;
    settings.speed = 0.0F;
    settings.quality = 1;
    settings.hdr = hdr;
    return settings;
}

bool read_table(vk::AtmosphereTable which, vk::AtmosphereTexels* out)
{
    vk::Backend& state = vk::backend();
    const std::scoped_lock lock(state.mutex);
    return vk::read_atmosphere_table(state, which, out);
}

int count_not_finite(const std::vector<std::array<float, 4>>& texels)
{
    int count = 0;
    for (const std::array<float, 4>& texel : texels) {
        count += std::ranges::all_of(texel, [](float v) { return std::isfinite(v); }) ? 0 : 1;
    }
    return count;
}

// The sky-view table read bilinearly as the shaders read it (clamped, texel centres at (i + 0.5) / size).
Rgb sky_view_at(const vk::AtmosphereTexels& table, const Vec3& dir, const Vec3& sun)
{
    const double hs = std::hypot(x_of(sun), z_of(sun));
    const double hv = std::hypot(x_of(dir), z_of(dir));
    const double c = (hs > 1e-5 && hv > 1e-5) ? ((x_of(sun) * x_of(dir)) + (z_of(sun) * z_of(dir))) / (hs * hv) : 1.0;
    const double azimuth = std::acos(std::clamp(c, -1.0, 1.0));
    const double e = std::asin(std::clamp(y_of(dir), -1.0, 1.0));
    const double u = std::sqrt(azimuth / kPi);
    const double v = kHalf + (kHalf * (e < 0.0 ? -1.0 : (e > 0.0 ? 1.0 : 0.0)) * std::sqrt(std::abs(e) / (kPi / 2.0)));
    const double x = std::clamp((u * table.width) - kHalf, 0.0, table.width - 1.0);
    const double y = std::clamp((v * table.height) - kHalf, 0.0, table.height - 1.0);
    const auto x0 = static_cast<uint32_t>(x);
    const auto y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = std::min(x0 + 1, table.width - 1);
    const uint32_t y1 = std::min(y0 + 1, table.height - 1);
    const double fx = x - x0;
    const double fy = y - y0;
    const auto at = [&table](uint32_t column, uint32_t row) -> const std::array<float, 4>& {
        return core::at(table.texels, (static_cast<std::size_t>(row) * table.width) + column);
    };
    Rgb out{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const double top = (core::at(at(x0, y0), ch) * (1.0 - fx)) + (core::at(at(x1, y0), ch) * fx);
        const double bottom = (core::at(at(x0, y1), ch) * (1.0 - fx)) + (core::at(at(x1, y1), ch) * fx);
        core::at(out, ch) = std::max(0.0, (top * (1.0 - fy)) + (bottom * fy));
    }
    return out;
}

// --- 1. the atmosphere's tables --------------------------------------------------------------------------------------

void run_atmosphere(render::Gpu& gpu, Tally* tally)
{
    text::print("the atmosphere's tables:\n");
    Targets targets;
    tally->check(make_targets(gpu, kTargetWidth, kTargetHeight, false, &targets), "the targets made");
    rt::set_sky_capture({});
    vk::set_rt_sky_settings(sky_settings(false, false, 0.0F));
    tally->check(draw_frame(gpu, targets, sky_pose()) == 1, "a pass with rt_sky=1 (no capture: the model alone)");
    // the transmittance
    vk::AtmosphereTexels transmittance;
    tally->check(read_table(vk::AtmosphereTable::kTransmittance, &transmittance) &&
                     transmittance.width == rt::kTransmittanceWidth && transmittance.height == rt::kTransmittanceHeight,
                 "the transmittance table read back");
    std::vector<double> errors;
    for (uint32_t row = 0; row < transmittance.height; row += 3) {
        for (uint32_t column = 0; column < transmittance.width; column += 7) {
            const rt::RadiusCosine point = rt::transmittance_lut_point((column + kHalf) / transmittance.width,
                                                                       (row + kHalf) / transmittance.height);
            const rt::Rgb want = rt::transmittance_to_top(point, 2048);
            const std::array<float, 4>& got =
                core::at(transmittance.texels, (static_cast<std::size_t>(row) * transmittance.width) + column);
            for (std::size_t c = 0; c < 3; ++c) {
                errors.push_back(std::abs(core::at(got, c) - core::at(want, c)));
            }
        }
    }
    tally->check(percentile(errors, 0.99) < 0.004 && mean_of(errors) < 0.001,
                 text::format("the transmittance against 2048 segments (%zu values): mean %.5f, 99th percentile %.5f",
                              errors.size(), mean_of(errors), percentile(errors, 0.99)));
    tally->check(count_not_finite(transmittance.texels) == 0, "no NaN in the transmittance");
    // Psi
    vk::AtmosphereTexels multiple;
    tally->check(read_table(vk::AtmosphereTable::kMultiple, &multiple) && multiple.width == rt::kMultipleWidth,
                 "the multiple-scattering table read back");
    errors.clear();
    for (uint32_t row = 0; row < multiple.height; row += 5) {
        for (uint32_t column = 0; column < multiple.width; column += 5) {
            const double altitude = (row + kHalf) / multiple.height * (rt::kTopRadius - rt::kGroundRadius);
            const double cos_sun = (((column + kHalf) / multiple.width) * 2.0) - 1.0;
            const rt::Rgb want = rt::multiple_scattering(altitude, cos_sun, rt::kMultipleSteps, 256);
            const std::array<float, 4>& got = core::at(multiple.texels, (static_cast<std::size_t>(row) * multiple.width) + column);
            const double most = std::max({want[0], want[1], want[2]});
            for (std::size_t c = 0; c < 3; ++c) {
                errors.push_back(error_of(core::at(got, c), core::at(want, c), std::max(most * 0.05, 1e-9)));
            }
        }
    }
    tally->check(mean_of(errors) < 0.02 && percentile(errors, 0.95) < 0.05,
                 text::format("Psi against rt_sky.h's integral (%zu values, relative): mean %.4f, 95th percentile %.4f",
                              errors.size(), mean_of(errors), percentile(errors, 0.95)));
    // the sky-view table at two sun elevations
    text::print("  (the processor's Psi table: a moment)\n");
    const rt::MultipleTable cpu_table = rt::multiple_table(rt::kMultipleSteps, 64);
    for (const double elevation_degrees : {10.0, 45.0}) {
        const double elevation = elevation_degrees * kDegrees;
        const Vec3 sun{std::cos(elevation) * 0.6, std::sin(elevation), std::cos(elevation) * -0.8};
        vk::AtmosphereInput input;
        input.sun = floats(sun);
        {
            vk::Backend& state = vk::backend();
            const std::scoped_lock lock(state.mutex);
            vk::update_atmosphere(state, vk::outside_pass(state), input);
        }
        vk::AtmosphereTexels view;
        tally->check(read_table(vk::AtmosphereTable::kSkyView, &view) && view.width == rt::kSkyViewWidth,
                     text::format("the sky-view table (sun %.0f degrees up) read back", elevation_degrees));
        errors.clear();
        double single_sum = 0.0;
        double multiple_sum = 0.0;
        const rt::MultipleTable no_table;
        for (uint32_t row = 0; row < view.height; row += 4) {
            for (uint32_t column = 0; column < view.width; column += 6) {
                const rt::Dir dir = rt::sky_view_direction((column + kHalf) / view.width, (row + kHalf) / view.height);
                const rt::Rgb want = rt::sky_radiance_ms(dir, rt::sun_direction_at(elevation),
                                                         {.view = rt::kSkyViewSteps, .light = 256}, cpu_table);
                const std::array<float, 4>& got = core::at(view.texels, (static_cast<std::size_t>(row) * view.width) + column);
                for (std::size_t c = 0; c < 3; ++c) {
                    const double exposed = core::at(want, c) * rt::sky_exposure();
                    errors.push_back(error_of(core::at(got, c), exposed, 0.01));
                }
                if (core::at(dir, 1) > 0.05) {
                    single_sum += luma(rt::sky_radiance(dir, rt::sun_direction_at(elevation), {.view = 32, .light = 16}));
                    multiple_sum += luma(want);
                }
            }
        }
        tally->check(mean_of(errors) < 0.02 && percentile(errors, 0.99) < 0.08,
                     text::format("  the sky-view table against sky_radiance_ms (%zu values): mean %.4f, 99th percentile %.4f",
                                  errors.size(), mean_of(errors), percentile(errors, 0.99)));
        tally->check(multiple_sum > single_sum * 1.03,
                     text::format("  the light scattered more than once brightens the sky (x %.3f)", multiple_sum / single_sum));
        const Rgb zenith = sky_view_at(view, {0.0, 1.0, 0.0}, sun);
        const Rgb horizon = sky_view_at(view, normalised({-x_of(sun), 0.03, -z_of(sun)}), sun);
        tally->check(zenith[2] / std::max(luma(zenith), 1e-9) > horizon[2] / std::max(luma(horizon), 1e-9),
                     "  the zenith bluer than the horizon (away from the sun)");
        tally->check(count_not_finite(view.texels) == 0, "  no NaN in the sky-view table");
    }
    release_targets(gpu, targets);
}

// --- 2. the match --------------------------------------------------------------------------------------------------------

constexpr uint32_t kFace = 64;
// A painted sky: blue darkening upward, white cloud blobs 10 - 40 degrees up, a dark sea below the horizon.
std::vector<uint8_t> painted_sky()
{
    std::vector<uint8_t> bytes(static_cast<std::size_t>(kFace) * 3 * kFace * 2 * 4);
    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t y = 0; y < kFace; ++y) {
            for (uint32_t x = 0; x < kFace; ++x) {
                const rt::CaptureTexel texel = rt::capture_texel(face, x, y, kFace);
                const double e = std::asin(texel.direction[1]) / kDegrees;
                const double a = std::atan2(texel.direction[2], texel.direction[0]);
                std::array<double, 3> rgb{};
                if (e < -1.0) {
                    rgb = {0.10, 0.20, 0.30};
                } else {
                    const double t = std::clamp(e / 90.0, 0.0, 1.0);
                    rgb = {0.75 - (0.45 * t), 0.82 - (0.32 * t), 0.95 - (0.05 * t)};
                    const bool cloud = e > 10.0 && e < 40.0 && std::sin(a * 5.0) > 0.4;
                    if (cloud) {
                        rgb = {0.93, 0.93, 0.95};
                    }
                }
                const std::size_t at = ((static_cast<std::size_t>((face / 3) * kFace + y) * kFace * 3) + ((face % 3) * kFace) + x) * 4;
                core::at(bytes, at + 0) = static_cast<uint8_t>(std::lround(rgb[2] * 255.0));
                core::at(bytes, at + 1) = static_cast<uint8_t>(std::lround(rgb[1] * 255.0));
                core::at(bytes, at + 2) = static_cast<uint8_t>(std::lround(rgb[0] * 255.0));
                core::at(bytes, at + 3) = 255;
            }
        }
    }
    return bytes;
}

void run_match(render::Gpu& gpu, Tally* tally)
{
    text::print("the level's sky matched:\n");
    const std::vector<uint8_t> bytes = painted_sky();
    vk::set_rt_sky_settings(sky_settings(true, false, 1.0F));
    render::GpuTexture* texture = nullptr;
    const bool made = gpu.create_texture({.width = kFace * 3,
                                          .height = kFace * 2,
                                          .levels = 1,
                                          .usage = 0,
                                          .format = value::kFormatA8R8G8B8,
                                          .pool = value::kPoolManaged},
                                         &texture) == value::kResultOk;
    render::GpuLockedRect locked{};
    bool filled = made && gpu.lock_texture(texture, 0, &locked, nullptr, 0) == value::kResultOk;
    if (filled) {
        for (uint32_t row = 0; row < kFace * 2; ++row) {
            std::memcpy(static_cast<uint8_t*>(locked.bits) + (static_cast<std::ptrdiff_t>(row) * locked.pitch),
                        &core::at(bytes, static_cast<std::size_t>(row) * kFace * 3 * 4), static_cast<std::size_t>(kFace) * 3 * 4);
        }
        filled = gpu.unlock_texture(texture, 0) == value::kResultOk;
    }
    tally->check(filled, "a capture texture painted (a blue sky, white clouds, a dark sea)");
    const rt::SkyLook want = rt::analyse_sky({.bytes = bytes, .face_size = kFace, .bgra = true, .clear = {1, 2, 3}});
    Targets targets;
    make_targets(gpu, kTargetWidth, kTargetHeight, false, &targets);
    // the model's and the matched sky's colour at each band's centre (the mean over the azimuths): there the gain is
    // the band's own - the matched over the model is the capture's clear colour over the model's band mean
    const auto centre_means = [](const vk::AtmosphereTexels& view) {
        std::array<Rgb, rt::kSkyBands> out{};
        for (std::size_t band = 1; band < rt::kSkyBands; ++band) {
            const double e = rt::kSkyBandRanges[band].centre * kDegrees;
            constexpr int kAzimuths = 64;
            for (int i = 0; i < kAzimuths; ++i) {
                const double a = 2.0 * kPi * (i + kHalf) / kAzimuths;
                const Rgb c = sky_view_at(view, {std::cos(e) * std::cos(a), std::sin(e), std::cos(e) * std::sin(a)}, to_sun());
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    out[band][ch] += c[ch] / kAzimuths;
                }
            }
        }
        return out;
    };
    std::array<Rgb, rt::kSkyBands> model{};
    for (const float strength : {0.0F, 1.0F}) {
        rt::set_sky_capture({.texture = texture, .face_size = kFace, .serial = strength > 0.5F ? 101U : 102U, .scene = 1, .clear = {}});
        vk::set_rt_sky_settings(sky_settings(true, false, strength));
        tally->check(draw_frame(gpu, targets, sky_pose()) == 1, text::format("a pass with rt_sky_match=%.0f", static_cast<double>(strength)));
        const vk::RtSkyMatch match = vk::rt_sky_match();
        const rt::SkyLook& got = match.look;
        bool same = got.valid == want.valid && std::abs(got.coverage - want.coverage) < 1e-5F;
        for (std::size_t band = 0; band < rt::kSkyBands; ++band) {
            for (std::size_t c = 0; c < 3; ++c) {
                same = same && std::abs(got.clear[band][c] - want.clear[band][c]) < 1e-5F &&
                       std::abs(got.all[band][c] - want.all[band][c]) < 1e-5F;
            }
        }
        vk::AtmosphereTexels view;
        read_table(vk::AtmosphereTable::kSkyView, &view);
        const std::array<Rgb, rt::kSkyBands> means = centre_means(view);
        if (strength < 0.5F) {
            tally->check(same, text::format("the device's analysis of the capture is the processor's (clouds %.0f%%, zenith %.2f "
                                            "%.2f %.2f, ground %.0f%% covered)",
                                            100.0 * got.coverage, static_cast<double>(got.clear[3][0]),
                                            static_cast<double>(got.clear[3][1]), static_cast<double>(got.clear[3][2]),
                                            100.0 * got.covered[0]));
            tally->check(got.coverage > 0.1F && got.coverage < 0.6F, "  the clouds' share found (some, not all)");
            bool unit = true;
            for (const std::array<float, 3>& gain : match.atmosphere.gains) {
                unit = unit && std::ranges::all_of(gain, [](float g) { return g == 1.0F; });
            }
            tally->check(unit && match.atmosphere.ground_share == 0.0F, "  rt_sky_match=0: the model's own sky (gains 1)");
            model = means;
            continue;
        }
        double worst = 0.0;
        for (std::size_t band = 1; band < rt::kSkyBands; ++band) {
            for (std::size_t ch = 0; ch < 3; ++ch) {
                const double target = std::pow(static_cast<double>(want.clear[band][ch]), rt::kDisplayGamma);
                const double expected = model[band][ch] * target / static_cast<double>(match.model[band][ch]);
                worst = std::max(worst, std::abs(means[band][ch] - expected) / expected);
            }
        }
        tally->check(worst < 0.03, text::format("  rt_sky_match=1: each band's centre the model x the capture's clear colour "
                                                "over the model's band (worst %.4f)", worst));
        tally->check(match.atmosphere.ground_share > 0.99F, "  the capture's sea used below the horizon");
    }
    rt::set_sky_capture({});
    vk::set_rt_sky_settings({});
    release_targets(gpu, targets);
    gpu.release_texture(texture);
}

// --- 3. the composite --------------------------------------------------------------------------------------------------

void run_composite(render::Gpu& gpu, bool floats, Tally* tally)
{
    text::print("the composite (%s target):\n", floats ? "16-bit float, rt_hdr=1" : "8-bit, rt_hdr=0");
    vk::set_rt_sky_settings(sky_settings(false, floats, 0.0F)); // (before the targets: their depth made samplable)
    Targets targets;
    tally->check(make_targets(gpu, kTargetWidth, kTargetHeight, floats, &targets), "the targets made");
    rt::set_sky_capture({});
    vk::set_rt_sky_settings({});
    draw_frame(gpu, targets, sky_pose());
    std::vector<Rgb> before;
    read_colour(targets, &before);
    vk::set_rt_sky_settings(sky_settings(false, floats, 0.0F));
    tally->check(draw_frame(gpu, targets, sky_pose()) == 1, "the pass ran");
    std::vector<Rgb> after;
    vk::AtmosphereTexels table;
    tally->check(read_colour(targets, &after) && read_table(vk::AtmosphereTable::kSkyView, &table), "read back");
    const View view = view_from(sky_pose(), kTargetWidth, kTargetHeight);
    std::vector<double> sky_errors;
    int surfaces = 0;
    int surfaces_changed = 0;
    double brightest = 0.0;
    for (int y = 2; y < static_cast<int>(kTargetHeight) - 2; ++y) {
        for (int x = 2; x < static_cast<int>(kTargetWidth) - 2; ++x) {
            bool sky = false;
            if (!clear_cut(view, x, y, &sky)) {
                continue;
            }
            const std::size_t i = (static_cast<std::size_t>(y) * kTargetWidth) + x;
            if (!sky) {
                ++surfaces;
                surfaces_changed += before[i] != after[i] ? 1 : 0;
                continue;
            }
            const Rgb linear = sky_view_at(table, direction_at(view, x, y), to_sun());
            for (std::size_t c = 0; c < 3; ++c) {
                double want = std::pow(linear[c], 1.0 / 2.2);
                if (!floats) {
                    want = std::min(want, 1.0);
                }
                brightest = std::max(brightest, after[i][c]);
                sky_errors.push_back(floats ? error_of(after[i][c], want, 0.05) : std::abs(after[i][c] - want));
            }
        }
    }
    tally->check(!sky_errors.empty() && percentile(sky_errors, 0.99) < (floats ? 0.01 : 2.5 / 255.0),
                 text::format("every clear-cut sky pixel the sky-view table's colour (%zu values; 99th percentile %.4f)",
                              sky_errors.size(), percentile(sky_errors, 0.99)));
    tally->check(surfaces > 1000 && surfaces_changed == 0,
                 text::format("every surface pixel unchanged (%d of %d changed)", surfaces_changed, surfaces));
    tally->check(floats || brightest <= 1.0, text::format("the brightest sky value %.3f (8-bit: at most 1)", brightest));
    vk::set_rt_sky_settings({});
    release_targets(gpu, targets);
}

// --- 4. the clouds -----------------------------------------------------------------------------------------------------

bool read_sky(vk::RtSkyImage which, vk::RtSkyTexels* out)
{
    vk::Backend& state = vk::backend();
    const std::scoped_lock lock(state.mutex);
    return vk::read_rt_sky_image(state, {0, 0, kTargetWidth, kTargetHeight}, which, out);
}

float jitter_of(uint32_t x, uint32_t y, float offset)
{
    const float inner = (0.06711056F * static_cast<float>(x)) + (0.00583715F * static_cast<float>(y));
    const float a = inner - std::floor(inner);
    const float b = 52.9829189F * a;
    const float j = b - std::floor(b);
    const float s = j + offset;
    return s - std::floor(s);
}

// the direction the device marches for a low-resolution texel (vulkan_rt_sky_pass.cpp low_centre / direction_of)
Vec3 march_direction(const vk::RtSkyFrame& frame, uint32_t x, uint32_t y)
{
    const double px = (static_cast<double>(x) * frame.scale) + (0.5 * (frame.scale - 1.0));
    const double py = (static_cast<double>(y) * frame.scale) + (0.5 * (frame.scale - 1.0));
    const std::array<float, 16>& m = frame.to_world; // column-major
    std::array<double, 4> w{};
    for (std::size_t row = 0; row < 4; ++row) {
        w[row] = (m[row] * px) + (m[4 + row] * py) + (m[8 + row] * 0.5) + m[12 + row];
    }
    return normalised({(w[0] / w[3]) - frame.camera[0], (w[1] / w[3]) - frame.camera[1], (w[2] / w[3]) - frame.camera[2]});
}

void run_clouds(render::Gpu& gpu, Tally* tally)
{
    text::print("the clouds:\n");
    vk::set_rt_sky_settings(sky_settings(true, false, 0.0F));
    Targets targets;
    tally->check(make_targets(gpu, kTargetWidth, kTargetHeight, false, &targets), "the targets made");
    rt::set_sky_capture({});
    vk::set_rt_sky_settings(sky_settings(true, false, 0.0F));
    tally->check(draw_frame(gpu, targets, sky_pose()) == 1, "a pass with rt_clouds=1");
    // the noise
    rt::CloudNoise noise;
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        tally->check(vk::read_cloud_noise(state, &noise), "the noise read back");
    }
    int noise_checked = 0;
    int noise_off = 0;
    for (uint32_t i = 0; i < 400; ++i) {
        const uint32_t x = (i * 37U) % rt::kCloudShapeSize;
        const uint32_t y = (i * 59U) % rt::kCloudShapeSize;
        const uint32_t z = (i * 83U) % rt::kCloudShapeSize;
        const std::array<double, 4> shape = rt::cloud_shape_texel(x, y, z);
        const std::size_t at = (((static_cast<std::size_t>(z) * rt::kCloudShapeSize) + y) * rt::kCloudShapeSize) + x;
        const std::array<double, 4> detail = rt::cloud_detail_texel(x % rt::kCloudDetailSize, y % rt::kCloudDetailSize,
                                                                    z % rt::kCloudDetailSize);
        const std::size_t at_detail = (((static_cast<std::size_t>(z % rt::kCloudDetailSize) * rt::kCloudDetailSize) +
                                        (y % rt::kCloudDetailSize)) * rt::kCloudDetailSize) + (x % rt::kCloudDetailSize);
        const std::array<double, 4> weather = rt::cloud_weather_texel(x * 2, y * 2);
        const std::size_t at_weather = (static_cast<std::size_t>(y * 2) * rt::kCloudWeatherSize) + (x * 2);
        for (std::size_t c = 0; c < 4; ++c) {
            noise_off += std::abs(noise.shape[(at * 4) + c] - std::lround(shape[c] * 255.0)) > 1 ? 1 : 0;
            noise_off += std::abs(noise.detail[(at_detail * 4) + c] - std::lround(detail[c] * 255.0)) > 1 ? 1 : 0;
            noise_off += std::abs(noise.weather[(at_weather * 4) + c] - std::lround(weather[c] * 255.0)) > 1 ? 1 : 0;
            noise_checked += 3;
        }
    }
    tally->check(noise_off <= noise_checked / 200,
                 text::format("the device's noise is rt_clouds.h's (%d of %d values more than a step off)", noise_off, noise_checked));
    // the march, texel by texel
    vk::RtSkyTexels march;
    vk::RtSkyTexels info;
    tally->check(read_sky(vk::RtSkyImage::kMarch, &march) && read_sky(vk::RtSkyImage::kMarchInfo, &info),
                 "the march read back");
    const vk::RtSkyFrame frame = vk::rt_sky_frame();
    std::vector<double> t_errors;
    std::vector<double> l_errors;
    int marched = 0;
    int clouded = 0;
    for (uint32_t y = 0; y < march.height; ++y) {
        for (uint32_t x = 0; x < march.width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * march.width) + x;
            if (info.texels[i][1] < 0.5F) {
                continue;
            }
            ++marched;
            rt::CloudRay ray;
            ray.camera_altitude = frame.camera_altitude;
            ray.offset = frame.offset;
            ray.direction = march_direction(frame, x, y);
            ray.to_sun = {frame.sun[0], frame.sun[1], frame.sun[2]};
            ray.coverage = frame.coverage;
            ray.density_scale = 1.0;
            ray.steps = frame.steps;
            ray.jitter = jitter_of(x, y, frame.jitter_offset);
            for (std::size_t c = 0; c < 3; ++c) {
                ray.sun_light[c] = frame.sun_light[c];
                ray.ambient_bottom[c] = frame.ambient_bottom[c];
                ray.ambient_top[c] = frame.ambient_top[c];
            }
            const rt::CloudResult want = rt::march_clouds(noise, ray);
            const std::array<float, 4>& got = march.texels[i];
            t_errors.push_back(std::abs(got[3] - want.transmittance));
            const double light = luma({want.light[0], want.light[1], want.light[2]});
            l_errors.push_back(std::abs(luma({got[0], got[1], got[2]}) - light) / std::max(light, 0.05));
            clouded += want.transmittance < 0.9 ? 1 : 0;
        }
    }
    tally->check(marched > 2000 && clouded > marched / 20,
                 text::format("%d texels marched (sky in their block), %d with cloud", marched, clouded));
    tally->check(percentile(t_errors, 0.95) < 0.02 && mean_of(t_errors) < 0.01,
                 text::format("  the transmittance against the processor's march: mean %.4f, 95th percentile %.4f",
                              mean_of(t_errors), percentile(t_errors, 0.95)));
    tally->check(percentile(l_errors, 0.95) < 0.05 && mean_of(l_errors) < 0.02,
                 text::format("  the light against it (relative): mean %.4f, 95th percentile %.4f", mean_of(l_errors),
                              percentile(l_errors, 0.95)));
    tally->check(count_not_finite(march.texels) == 0, "  no NaN in the march");
    // temporal: still frames
    vk::RtSkyTexels last;
    vk::RtSkyTexels history;
    vk::RtSkyTexels previous_march = march;
    double history_change = 0.0;
    double march_change = 0.0;
    constexpr int kStillFrames = 30;
    for (int i = 0; i < kStillFrames; ++i) {
        draw_frame(gpu, targets, sky_pose());
        if (i >= kStillFrames - 2) {
            last = history;
            read_sky(vk::RtSkyImage::kHistory, &history);
        }
        if (i == kStillFrames - 1) {
            vk::RtSkyTexels now;
            read_sky(vk::RtSkyImage::kMarch, &now);
            for (std::size_t t = 0; t < now.texels.size(); ++t) {
                if (info.texels[t][1] > 0.5F) {
                    history_change += std::abs(history.texels[t][3] - last.texels[t][3]);
                    march_change += std::abs(now.texels[t][3] - previous_march.texels[t][3]);
                }
            }
        }
        if (i == kStillFrames - 2) {
            read_sky(vk::RtSkyImage::kMarch, &previous_march);
        }
    }
    tally->check(vk::rt_sky_frame().history && history_change < 0.3 * std::max(march_change, 1e-6),
                 text::format("after %d still frames the history steadier than the march (changes %.3f against %.3f)",
                              kStillFrames, history_change, march_change));
    tally->check(count_not_finite(history.texels) == 0, "  no NaN in the history");
    // a turned camera: the history inside this frame's neighbourhood
    draw_frame(gpu, targets, turned_pose());
    vk::RtSkyTexels turned_march;
    vk::RtSkyTexels turned_info;
    vk::RtSkyTexels turned_history;
    read_sky(vk::RtSkyImage::kMarch, &turned_march);
    read_sky(vk::RtSkyImage::kMarchInfo, &turned_info);
    read_sky(vk::RtSkyImage::kHistory, &turned_history);
    int outside = 0;
    int checked = 0;
    const auto w = static_cast<int>(turned_march.width);
    const auto h = static_cast<int>(turned_march.height);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w) + x;
            if (turned_info.texels[i][1] < 0.5F) {
                continue;
            }
            std::array<float, 4> least = turned_march.texels[i];
            std::array<float, 4> most = least;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const std::size_t n = (static_cast<std::size_t>(std::clamp(y + dy, 0, h - 1)) * w) + std::clamp(x + dx, 0, w - 1);
                    if (turned_info.texels[n][1] > 0.5F) {
                        for (std::size_t c = 0; c < 4; ++c) {
                            least[c] = std::min(least[c], turned_march.texels[n][c]);
                            most[c] = std::max(most[c], turned_march.texels[n][c]);
                        }
                    }
                }
            }
            ++checked;
            for (std::size_t c = 0; c < 4; ++c) {
                const float v = turned_history.texels[i][c];
                const float slack = 0.002F + (0.002F * std::abs(most[c]));
                outside += v < least[c] - slack || v > most[c] + slack ? 1 : 0;
            }
        }
    }
    tally->check(checked > 1000 && outside == 0,
                 text::format("a camera turned 8 degrees: every history texel within this frame's 3 x 3 neighbourhood (%d "
                              "outside of %d texels) - no ghost",
                              outside, checked));
    // the composite with clouds: no NaN, nothing black where the sky is
    std::vector<Rgb> colour;
    read_colour(targets, &colour);
    int bad = 0;
    for (const Rgb& c : colour) {
        bad += std::ranges::all_of(c, [](double v) { return std::isfinite(v); }) ? 0 : 1;
    }
    tally->check(bad == 0, "the composite with clouds: no NaN");
    vk::set_rt_sky_settings({});
    release_targets(gpu, targets);
}

// --- 5. off ------------------------------------------------------------------------------------------------------------

void run_off(render::Gpu& gpu, Tally* tally)
{
    text::print("rt_sky=0:\n");
    vk::set_rt_sky_settings({});
    tally->check(!rt::sky_replacement().on && !rt::sky_capture_wanted(), "render_pc not asked to replace or capture the sky");
    Targets targets;
    make_targets(gpu, kTargetWidth, kTargetHeight, false, &targets);
    const uint64_t before = vk::rt_sky_passes();
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, kClearColour, 1.0F, 0);
    draw_scene(gpu, sky_pose(), kTargetWidth, kTargetHeight);
    rt::sky_view_drawn(view_drawn(sky_pose(), kTargetWidth, kTargetHeight)); // (nobody listens)
    gpu.end_scene();
    tally->check(vk::rt_sky_passes() == before, "no pass");
    release_targets(gpu, targets);
}

void run_perf(render::Gpu& gpu)
{
    text::print("the cost at %u x %u:\n", kPerfWidth, kPerfHeight);
    vk::set_rt_sky_settings(sky_settings(true, true, 0.8F));
    Targets targets;
    make_targets(gpu, kPerfWidth, kPerfHeight, true, &targets);
    for (const unsigned quality : {1U, 0U, 2U}) {
        for (const bool clouds : {false, true}) {
            vk::RtSkySettings settings = sky_settings(clouds, true, 0.8F);
            settings.quality = quality;
            vk::set_rt_sky_settings(settings);
            // (each frame's work finished: its timestamps read 8 passes later - the warm-up's left out)
            vk::RtSkyTiming before = vk::rt_sky_timing();
            for (int i = 0; i < 140; ++i) {
                draw_frame(gpu, targets, i % 2 == 0 ? sky_pose() : turned_pose());
                vk::Backend& state = vk::backend();
                const std::scoped_lock lock(state.mutex);
                vk::finish_work(state);
                if (i == 20) {
                    before = vk::rt_sky_timing();
                }
            }
            {
                vk::Backend& state = vk::backend();
                const std::scoped_lock lock(state.mutex);
                vk::finish_work(state);
            }
            draw_frame(gpu, targets, sky_pose());
            const vk::RtSkyTiming after = vk::rt_sky_timing();
            const double samples = static_cast<double>(after.samples - before.samples);
            if (samples < 1.0) {
                text::print("  no timing\n");
                continue;
            }
            std::array<double, 3> parts{};
            for (std::size_t i = 0; i < 3; ++i) {
                parts[i] = (after.milliseconds[i] - before.milliseconds[i]) / samples;
            }
            text::print("  %s, quality %u: %.3f ms - clouds %.3f, temporal %.3f, composite %.3f (%.0f passes timed)\n",
                        clouds ? "clouds" : "sky alone", quality, parts[0] + parts[1] + parts[2], parts[0], parts[1], parts[2],
                        samples);
            if (!clouds && quality != 1) {
                break;
            }
        }
    }
    vk::set_rt_sky_settings({});
    release_targets(gpu, targets);
}

// --- the device --------------------------------------------------------------------------------------------------------

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
        position = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(core::at(listed, chosen - 1)));
    }
    SDL_free(ids);
    return position;
}

bool open_device(const Options& options, SDL_Window* window)
{
    vk::set_context_options(
        {.validation = options.validation, .device_index = options.device, .on_message = print_line, .ray_tracing = false},
        window);
    // (on before the device: its depth surfaces are made samplable)
    vk::set_rt_sky_settings(sky_settings(true, false));
    render::GpuContext& context = vk::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return false;
    }
    const vk::Backend& state = vk::backend();
    text::print("validation layer: %s\n", state.context.validation_on() ? "on" : "off (not installed, or not asked for)");
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
        return false;
    }
    context.mark_ready();
    text::print("device: %s\n", context.adapter().description.data());
    return true;
}

int run(const Options& options, SDL_Window* window)
{
    if (!open_device(options, window)) {
        return vk::loader_version() == 0 ? 2 : 1;
    }
    Tally tally;
    render::Gpu& gpu = vk::gpu();
    run_atmosphere(gpu, &tally);
    run_match(gpu, &tally);
    run_composite(gpu, false, &tally);
    run_composite(gpu, true, &tally);
    run_clouds(gpu, &tally);
    run_off(gpu, &tally);
    if (options.perf) {
        run_perf(gpu);
    }
    vk::set_rt_sky_settings({});
    rt::set_sun_override(nullptr);
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        vk::finish_work(state);
    }
    vk::gpu_context().shutdown();
    tally.check(validation_messages() == 0, text::format("validation messages: %d", validation_messages()));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    set_log_listener([](const char* line) { text::print("  log: %s\n", line); });
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rt_sky_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    if (!vk::open_loader()) {
        text::print("no Vulkan loader (vulkan-1.dll) for this process\n");
        return 2;
    }
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        text::print("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int position = window_position(options.display);
    SDL_Window* window =
        SDL_CreateWindow("thug2 Vulkan sky check", kWindowWidth, kWindowHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
