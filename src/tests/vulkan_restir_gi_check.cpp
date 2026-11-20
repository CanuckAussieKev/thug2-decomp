// NOT ORIGINAL - thug2_vulkan_restir_gi_check.exe: the indirect light by ReSTIR GI (gpu_vulkan/vulkan_restir_gi.h; path
// tracing plan, stage 5) tried on this machine against the reference path tracer (vulkan_path_reference.h). The
// reference's indirect light - what ReSTIR GI estimates: the sky at the surface and every bounce of every light off the
// surfaces around it (round MB) - is its image of every bounce less its one-bounce image of the disc, the emitters and the lamps alone
// (the direct light the shadows pass and ReSTIR DI own). Everything goes through the installed render::rt::RayTracing as
// the game's frame record feeds it; the material forced (albedo x a scale, F0, roughness):
//   - still scene (a floor, a disc of 16 emitting triangles with a light store, a wall, 5 lamps, a sun and an even sky):
//     the mean over many frames (reservoirs reused in time and space) within 2 % of the reference's - a glossy material
//     (its specular resampled with its diffuse light) and a smooth one (its specular by its own ray, within 3 %);
//   - colour bleeding: a red wall - the floor's red and green within 2 % of the reference's each, the floor redder than
//     it is green;
//   - a room lit only through a window in its roof (the sun's patch on its floor, the sky through it): within 3 % of the
//     reference's every bounce (round MB: the radiance cache's further bounces - without it under -10 %); the camera
//     circling in it: within 3 %, each frame's mean as steady as a still camera's (under 2 x + 1 %);
//   - energy: a white floor under an even sky (a furnace's half: nothing to bounce off) - the reference's within 2 %
//     and the sky's radiance within 3 % (the light model's albedo with its multiple-scattering compensation: 1);
//   - (round CW) a layer painted on that floor (alpha-blended, 0.8 units over it): the paint is the pixel's surface - the
//     sky's light on it within 3 % of the bare floor's (it was black: the floor under it found, shaded by the paint);
//   - (round DD) a night lit by a far floodlight and six small lamps by the wall (1/200 of its power each): the
//     secondary vertices' local lights by RIS over 8 of the pool's picks - the floor's mean the reference's (within 3 %,
//     as one pick's), its flicker frame to frame under 0.75 x one pick's;
//   - disocclusion: the camera swapped between two views every frame (history across the other view): within 2 %;
//   - no estimate NaN or infinite; the validation layer silent; the passes' device time printed.
//   thug2_vulkan_restir_gi_check [--device N] [--display N] [--no-validation]
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
#include <functional>
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
#include "engine/gfx/light_store.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_path.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_path_reference.h"
#include "gpu_vulkan/vulkan_restir_di.h"
#include "gpu_vulkan/vulkan_restir_gi.h"
#include "gpu_vulkan/vulkan_rt_fog.h"
#include "platform/run_log.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace value = render::gpu_value;
namespace rt = render::rt;
namespace path = render::rt::path;
namespace vk = render::vulkan;
using Vec3 = std::array<double, 3>;
using Rgb = std::array<double, 3>;

constexpr int kWindowWidth = 160;
constexpr int kWindowHeight = 120;
constexpr uint32_t kWidth = 160;
constexpr uint32_t kHeight = 120;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x142; // XYZ | DIFFUSE | TEX1
constexpr uint32_t kStride = 24;
constexpr uint32_t kColourAt = 12;
constexpr uint32_t kUvAt = 16;
constexpr uint32_t kGrey = 0x80808080U;
constexpr float kNearZ = 2.0F;
constexpr float kFarZ = 20000.0F;
constexpr double kHalfFovTan = 0.57735026918962576; // tan 30 degrees
constexpr float kFloorHalf = 6000.0F;
constexpr double kDiscHeight = 300.0;
constexpr double kDiscRadius = 120.0;
constexpr int kDiscSides = 16;
constexpr float kDiscRadiance = 4.0F;
constexpr double kWallZ = 260.0;
constexpr double kWallHalf = 400.0;
constexpr double kWallHeight = 160.0;
constexpr double kRoomHalf = 400.0;
constexpr double kRoomHeight = 300.0;
constexpr double kWindowHalf = 120.0;
constexpr uint32_t kWorldOpaque = rt::kWorld | rt::kOpaque | rt::kCastsShadow;
// (round CW) a layer alpha-blended over a surface (the game's blend 5, drawn alpha 2 x vertex x texel: 1 here) - seen
// through for light rays (instance mask 0x80), kept by the per-surface lighting's prepass (at least half opaque)
constexpr uint32_t kWorldBlended = rt::kWorld | rt::kTransparent;
constexpr uint32_t kBlendAlpha = 5;
constexpr double kPaintLift = 0.8; // (Boston's crosswalk paint over its asphalt)
constexpr double kPaintInset = 100.0;
constexpr double kAgreement = 0.02;      // (the brief: within 2 % of the reference's)
constexpr double kLooseAgreement = 0.03; // (the smooth surface's own rays, the room's few lit pixels)
constexpr float kRoughness = 0.6F;
constexpr float kSmooth = 0.15F;
constexpr float kF0 = 0.04F;
constexpr float kAlbedo = 0.7F;
constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kFirstRow = 40;

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
Vec3 add(const Vec3& a, const Vec3& b) { return {x_of(a) + x_of(b), y_of(a) + y_of(b), z_of(a) + z_of(b)}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {x_of(a) - x_of(b), y_of(a) - y_of(b), z_of(a) - z_of(b)}; }
Vec3 mul(const Vec3& a, double s) { return {x_of(a) * s, y_of(a) * s, z_of(a) * s}; }
double dot(const Vec3& a, const Vec3& b) { return (x_of(a) * x_of(b)) + (y_of(a) * y_of(b)) + (z_of(a) * z_of(b)); }
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {(y_of(a) * z_of(b)) - (z_of(a) * y_of(b)), (z_of(a) * x_of(b)) - (x_of(a) * z_of(b)),
            (x_of(a) * y_of(b)) - (y_of(a) * x_of(b))};
}
Vec3 unit(const Vec3& v) { return mul(v, 1.0 / std::sqrt(dot(v, v))); }
double luma(const Rgb& c) { return (0.2126 * core::at(c, 0)) + (0.7152 * core::at(c, 1)) + (0.0722 * core::at(c, 2)); }

// --- the scenes ---------------------------------------------------------------------------------------------------------

struct Triangle {
    std::array<Vec3, 3> corners{};
};

void quad(std::vector<Triangle>* out, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    out->push_back({.corners = {a, b, c}});
    out->push_back({.corners = {a, c, d}});
}

std::vector<Triangle> floor_triangles()
{
    std::vector<Triangle> out;
    const double h = kFloorHalf;
    quad(&out, {-h, 0.0, -h}, {h, 0.0, -h}, {h, 0.0, h}, {-h, 0.0, h});
    return out;
}

std::vector<Triangle> disc_triangles()
{
    std::vector<Triangle> out;
    for (int i = 0; i < kDiscSides; ++i) {
        const double a0 = 2.0 * kPi * i / kDiscSides;
        const double a1 = 2.0 * kPi * (i + 1) / kDiscSides;
        out.push_back({.corners = {{{0.0, kDiscHeight, 0.0},
                                    {kDiscRadius * std::cos(a1), kDiscHeight, kDiscRadius * std::sin(a1)},
                                    {kDiscRadius * std::cos(a0), kDiscHeight, kDiscRadius * std::sin(a0)}}}});
    }
    return out;
}

std::vector<Triangle> wall_triangles()
{
    std::vector<Triangle> out;
    const double w = kWallHalf;
    const double h = kWallHeight;
    const double z = kWallZ;
    quad(&out, {-w, 0.0, z}, {w, 0.0, z}, {w, h, z}, {-w, h, z});
    return out;
}

// A closed room: its floor, four walls and a roof with a square window in its middle.
std::vector<Triangle> room_triangles()
{
    std::vector<Triangle> out;
    const double r = kRoomHalf;
    const double h = kRoomHeight;
    const double w = kWindowHalf;
    quad(&out, {-r, 0.0, -r}, {r, 0.0, -r}, {r, 0.0, r}, {-r, 0.0, r});
    quad(&out, {-r, 0.0, -r}, {r, 0.0, -r}, {r, h, -r}, {-r, h, -r});
    quad(&out, {-r, 0.0, r}, {r, 0.0, r}, {r, h, r}, {-r, h, r});
    quad(&out, {-r, 0.0, -r}, {-r, 0.0, r}, {-r, h, r}, {-r, h, -r});
    quad(&out, {r, 0.0, -r}, {r, 0.0, r}, {r, h, r}, {r, h, -r});
    // the roof round the window
    quad(&out, {-r, h, -r}, {r, h, -r}, {r, h, -w}, {-r, h, -w});
    quad(&out, {-r, h, w}, {r, h, w}, {r, h, r}, {-r, h, r});
    quad(&out, {-r, h, -w}, {-w, h, -w}, {-w, h, w}, {-r, h, w});
    quad(&out, {w, h, -w}, {r, h, -w}, {r, h, w}, {w, h, w});
    return out;
}

// (round CW) A layer painted on the floor - a road's crosswalk: kPaintLift over it, a little inside its edge
std::vector<Triangle> paint_triangles()
{
    std::vector<Triangle> out;
    const double h = kFloorHalf - kPaintInset;
    quad(&out, {-h, kPaintLift, -h}, {h, kPaintLift, -h}, {h, kPaintLift, h}, {-h, kPaintLift, h});
    return out;
}

struct SceneMesh {
    rt::MeshId id = 0;
    rt::MaterialId material = 0;
    render::GpuVertexBuffer* vertices = nullptr;
    render::GpuVertexBuffer* stream = nullptr;
    uint32_t flags = kWorldOpaque; // (its instance's)
};

void put_float(std::span<uint8_t> bytes, std::size_t at, float value)
{
    std::memcpy(bytes.subspan(at, sizeof(value)).data(), &value, sizeof(value));
}
void put_word(std::span<uint8_t> bytes, std::size_t at, uint32_t value)
{
    std::memcpy(bytes.subspan(at, sizeof(value)).data(), &value, sizeof(value));
}

constexpr std::array<float, 4> kWhite{1.0F, 1.0F, 1.0F, 1.0F};
constexpr std::array<float, 4> kRed{1.0F, 0.15F, 0.1F, 1.0F};

// A mesh of `triangles` (untextured, its material's colour `colour`) registered; `emitting`: with a light store whose every
// vertex emits kDiscRadiance and has no tint.
bool make_mesh(render::Gpu& gpu, const std::vector<Triangle>& triangles, bool emitting, SceneMesh* mesh,
               const std::array<float, 4>& colour = kWhite, bool blended = false)
{
    const std::size_t vertices = triangles.size() * 3;
    std::vector<uint8_t> bytes(vertices * kStride);
    for (std::size_t t = 0; t < triangles.size(); ++t) {
        for (std::size_t k = 0; k < 3; ++k) {
            const std::size_t base = ((t * 3) + k) * kStride;
            const Vec3& corner = core::at(core::at(triangles, t).corners, k);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                put_float(bytes, base + (axis * sizeof(float)), static_cast<float>(core::at(corner, axis)));
            }
            put_word(bytes, base + kColourAt, kGrey);
            put_float(bytes, base + kUvAt, 0.5F);
            put_float(bytes, base + kUvAt + sizeof(float), 0.5F);
        }
    }
    void* data = nullptr;
    if (gpu.create_vertex_buffer(static_cast<uint32_t>(bytes.size()), 0, kFvf, value::kPoolDefault, &mesh->vertices) !=
            value::kResultOk ||
        gpu.lock_vertex_buffer(mesh->vertices, 0, 0, &data, 0) != value::kResultOk) {
        return false;
    }
    std::memcpy(data, bytes.data(), bytes.size());
    if (gpu.unlock_vertex_buffer(mesh->vertices) != value::kResultOk) {
        return false;
    }
    const rt::Geometry geometry{.vertices = mesh->vertices,
                                .vertex_offset = 0,
                                .vertex_stride = kStride,
                                .vertex_count = static_cast<uint32_t>(vertices),
                                .position_offset = 0,
                                .indices = nullptr,
                                .index_offset = 0,
                                .index_count = 0,
                                .indices_32bit = false,
                                .strip = false,
                                .dynamic = false,
                                .uv_offset = kUvAt,
                                .uv_format = rt::kUvFloat2,
                                .colour_offset = kColourAt,
                                .colour_format = rt::kColourA8R8G8B8};
    mesh->id = rt::next_mesh_id();
    rt::ray_tracing()->add_mesh(mesh->id, geometry);
    mesh->material = rt::next_material_id();
    rt::Material surface;
    surface.colour = colour;
    surface.flags = blended ? static_cast<uint32_t>(rt::kTransparent) : static_cast<uint32_t>(rt::kOpaque);
    surface.blend = blended ? kBlendAlpha : 0U;
    surface.filter = value::kFilterPoint;
    mesh->flags = blended ? kWorldBlended : kWorldOpaque;
    rt::ray_tracing()->set_material(mesh->material, surface);
    if (emitting) {
        rt::CelWords words = rt::kCelNoStream;
        core::at(words, rt::kCelFlagsWord) = rt::kCelHasStream;
        core::at(words, rt::kCelEmitWord) = rt::store_light({kDiscRadiance, kDiscRadiance, kDiscRadiance});
        const std::vector<rt::CelWords> stream(vertices, words);
        const auto stream_bytes = static_cast<uint32_t>(stream.size() * rt::kCelStreamBytes);
        if (gpu.create_vertex_buffer(stream_bytes, 0, 0, value::kPoolDefault, &mesh->stream) != value::kResultOk ||
            gpu.lock_vertex_buffer(mesh->stream, 0, 0, &data, 0) != value::kResultOk) {
            return false;
        }
        std::memcpy(data, stream.data(), stream_bytes);
        if (gpu.unlock_vertex_buffer(mesh->stream) != value::kResultOk) {
            return false;
        }
        rt::ray_tracing()->set_light_stream(mesh->id, mesh->stream);
    }
    return true;
}

void release_mesh(render::Gpu& gpu, SceneMesh* mesh)
{
    if (rt::ray_tracing() != nullptr) {
        if (mesh->stream != nullptr) {
            rt::ray_tracing()->set_light_stream(mesh->id, nullptr);
        }
        if (mesh->id != 0) {
            rt::ray_tracing()->remove_mesh(mesh->id);
        }
        if (mesh->material != 0) {
            rt::ray_tracing()->remove_material(mesh->material);
        }
    }
    for (render::GpuVertexBuffer* buffer : {mesh->vertices, mesh->stream}) {
        if (buffer != nullptr) {
            gpu.release_vertex_buffer(buffer);
        }
    }
    *mesh = SceneMesh{};
}

// --- the camera -----------------------------------------------------------------------------------------------------------

constexpr std::size_t kMatrixFloats = 16;
using Matrix = std::array<float, kMatrixFloats>;
const Vec3 kAt{0.0, 0.0, 0.0};
const Vec3 kUp{0.0, 1.0, 0.0};
const Vec3 kEyeA{700.0, 450.0, 0.0};
const Vec3 kEyeB{-250.0, 350.0, 700.0};
const Vec3 kEyeRoom{300.0, 200.0, 300.0};

rt::ViewDrawn view_from(const Vec3& eye)
{
    Matrix view{};
    Matrix projection{};
    const std::array<float, 3> e{static_cast<float>(x_of(eye)), static_cast<float>(y_of(eye)),
                                 static_cast<float>(z_of(eye))};
    const std::array<float, 3> at{0.0F, 0.0F, 0.0F};
    const std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    maths::matrix_look_at_rh(view, e, at, up);
    const double width = 2.0 * kNearZ * kHalfFovTan;
    const double height = width * static_cast<double>(kHeight) / static_cast<double>(kWidth);
    maths::matrix_perspective_rh(projection, static_cast<float>(width), static_cast<float>(height), kNearZ, kFarZ);
    rt::ViewDrawn drawn;
    constexpr std::size_t kSide = 4;
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(drawn.camera.view, row), column) = core::at(view, (row * kSide) + column);
            core::at(core::at(drawn.camera.projection, row), column) = core::at(projection, (row * kSide) + column);
        }
    }
    drawn.camera.position = e;
    drawn.rect = {0, 0, kWidth, kHeight};
    drawn.output_rect = drawn.rect;
    drawn.depth_range = {0.0F, 1.0F};
    return drawn;
}

Vec3 pixel_ray(const Vec3& eye, double x, double y)
{
    const Vec3 forward = unit(sub(kAt, eye));
    const Vec3 right = unit(cross(forward, kUp));
    const Vec3 up = cross(right, forward);
    const double tx = kHalfFovTan;
    const double ty = tx * static_cast<double>(kHeight) / static_cast<double>(kWidth);
    const double u = ((x / kWidth) * 2.0) - 1.0;
    const double v = 1.0 - ((y / kHeight) * 2.0);
    return unit(add(add(forward, mul(right, u * tx)), mul(up, v * ty)));
}

bool sees_disc(const Vec3& eye, uint32_t x, uint32_t y)
{
    const Vec3 d = pixel_ray(eye, x + 0.5, y + 0.5);
    if (std::abs(y_of(d)) < 1e-9) {
        return false;
    }
    const double t = (kDiscHeight - y_of(eye)) / y_of(d);
    if (t <= 0.0) {
        return false;
    }
    const Vec3 p = add(eye, mul(d, t));
    return (x_of(p) * x_of(p)) + (z_of(p) * z_of(p)) <= (kDiscRadius + 2.0) * (kDiscRadius + 2.0);
}

// Whether a pixel's ray meets the floor first (not the wall, not the disc).
bool sees_floor(const Vec3& eye, uint32_t x, uint32_t y)
{
    const Vec3 d = pixel_ray(eye, x + 0.5, y + 0.5);
    if (!(y_of(d) < -1e-9) || sees_disc(eye, x, y)) {
        return false;
    }
    const double t = -y_of(eye) / y_of(d);
    const Vec3 on_floor = add(eye, mul(d, t));
    if (std::abs(x_of(on_floor)) > kFloorHalf - 2.0 || std::abs(z_of(on_floor)) > kFloorHalf - 2.0) {
        return false; // (past its edge: the sky)
    }
    if (std::abs(z_of(d)) > 1e-9) {
        const double tw = (kWallZ - z_of(eye)) / z_of(d);
        const Vec3 w = add(eye, mul(d, tw));
        if (tw > 0.0 && tw < t && std::abs(x_of(w)) <= kWallHalf + 2.0 && y_of(w) <= kWallHeight + 2.0) {
            return false;
        }
    }
    return true;
}

// --- the device's runs --------------------------------------------------------------------------------------------------

struct Targets {
    render::GpuSurface* colour = nullptr;
    render::GpuSurface* depth = nullptr;
};

bool make_targets(render::Gpu& gpu, Targets* out)
{
    const render::GpuSurfaceDesc colour{.width = kWidth,
                                        .height = kHeight,
                                        .format = value::kFormatA16B16G16R16F,
                                        .samples = 0,
                                        .quality = 0,
                                        .lockable_or_discard = false};
    render::GpuSurfaceDesc depth = colour;
    depth.format = value::kFormatD24S8;
    return gpu.create_render_target(colour, &out->colour) == value::kResultOk &&
           gpu.create_depth_surface(depth, &out->depth) == value::kResultOk;
}

void release_targets(render::Gpu& gpu, const Targets& targets)
{
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    for (render::GpuSurface* surface : {targets.colour, targets.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
}

uint64_t& frame_number()
{
    static uint64_t frame = 0;
    return frame;
}

void draw_frame(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, const rt::ViewDrawn& view)
{
    rt::FrameRecord& record = rt::frame_record();
    record = rt::FrameRecord{};
    record.frame = ++frame_number();
    constexpr std::array<std::array<float, 4>, 3> kIdentityRows{
        {{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
    for (const SceneMesh& mesh : meshes) {
        record.instances.push_back(
            {.mesh = mesh.id, .material = mesh.material, .flags = mesh.flags, .object_to_world = kIdentityRows});
    }
    gpu.begin_scene();
    rt::ray_tracing()->begin_frame(record);
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kWidth, .height = kHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, 0, 1.0F, 0);
    rt::opaque_view_drawn(view); // (ReSTIR GI's passes)
    rt::world_view_drawn(view);  // (the reference's)
    gpu.end_scene();
    gpu.present();
}

using Region = std::vector<uint8_t>;

Region region_of(const Vec3& eye)
{
    Region in(static_cast<std::size_t>(kWidth) * kHeight, 0);
    for (uint32_t y = kFirstRow; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            core::at(in, (static_cast<std::size_t>(y) * kWidth) + x) = sees_disc(eye, x, y) ? 0 : 1;
        }
    }
    return in;
}

Region floor_region(const Vec3& eye)
{
    Region in(static_cast<std::size_t>(kWidth) * kHeight, 0);
    for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            core::at(in, (static_cast<std::size_t>(y) * kWidth) + x) = sees_floor(eye, x, y) ? 1 : 0;
        }
    }
    return in;
}

Region every_pixel() { return Region(static_cast<std::size_t>(kWidth) * kHeight, 1); }

// The mean over the region: luma, or one channel (0 - 2).
double region_mean(std::span<const std::array<float, 3>> image, std::span<const uint8_t> region, int channel = -1)
{
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < image.size() && i < region.size(); ++i) {
        if (core::at(region, i) != 0) {
            const std::array<float, 3>& c = core::at(image, i);
            sum += channel < 0 ? luma({core::at(c, 0), core::at(c, 1), core::at(c, 2)})
                               : static_cast<double>(core::at(c, static_cast<std::size_t>(channel)));
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

struct Answer {
    double mean = 0.0;
    double sigma = 0.0;
    std::vector<std::array<float, 3>> pixels;
    uint32_t not_finite = 0;
};

// The lighting and the material of a case.
struct Lighting {
    std::array<float, 3> sky{0.3F, 0.35F, 0.45F};
    std::array<float, 3> sun_toward{0.35F, 0.85F, 0.25F};
    std::array<float, 3> sun_light{1.0F, 0.95F, 0.85F};
    float sun_radius = 0.27F;
    std::vector<rt::Emitter> lamps;
    float roughness = kRoughness;
    float f0 = kF0;
    float albedo = kAlbedo;
};

Answer summary(const std::vector<double>& means, std::vector<std::array<float, 3>> pixels)
{
    Answer out;
    const auto n = static_cast<double>(std::max<std::size_t>(means.size(), 1));
    for (const double m : means) {
        out.mean += m / n;
    }
    double spread = 0.0;
    for (const double m : means) {
        spread += (m - out.mean) * (m - out.mean);
    }
    out.sigma = means.size() > 1 ? std::sqrt(spread / (n - 1.0) / n) : 0.0;
    out.pixels = std::move(pixels);
    return out;
}

// One reference image from `eye`: `bounces` bounces, the light `kinds` counted (the batches' mean).
bool reference_image(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, const Vec3& eye,
                     const Lighting& lighting, uint32_t bounces, uint32_t kinds, uint32_t seed,
                     vk::PathAccumulation* image)
{
    constexpr int kFrames = 4;
    vk::PathOverrides overrides;
    overrides.on = true;
    overrides.bounces = bounces;
    overrides.samples_per_frame = 64;
    overrides.force_material = true;
    overrides.roughness = lighting.roughness;
    overrides.f0 = lighting.f0;
    overrides.albedo_scale = lighting.albedo;
    overrides.sky = lighting.sky;
    overrides.sun_toward = lighting.sun_toward;
    overrides.sun_light = lighting.sun_light;
    overrides.sun_radius_degrees = lighting.sun_radius;
    overrides.lamps = lighting.lamps;
    overrides.lamps_surfaced.assign(lighting.lamps.size(), 0);
    overrides.light_kinds = kinds;
    overrides.seed = seed;
    vk::set_path_overrides(overrides);
    for (int frame = 0; frame <= kFrames; ++frame) { // (the first gathers the light list)
        draw_frame(gpu, targets, meshes, view_from(eye));
    }
    bool read = false;
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        read = vk::read_path_accumulation(state, image) && image->width == kWidth && image->height == kHeight;
    }
    vk::set_path_overrides({});
    return read;
}

// The reference's indirect light: its image of every bounce (round MB: was two - ReSTIR GI's secondary vertices now have
// the radiance cache's further bounces) less its one-bounce image of the disc, the emitters and the lamps.
Answer reference_gi(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, const Vec3& eye,
                    const Lighting& lighting, const Region& region, int batches, bool* ok, int channel = -1)
{
    std::vector<double> means;
    std::vector<std::array<float, 3>> pixels(static_cast<std::size_t>(kWidth) * kHeight, {0.0F, 0.0F, 0.0F});
    uint32_t not_finite = 0;
    for (int batch = 0; batch < batches; ++batch) {
        vk::PathAccumulation full;
        vk::PathAccumulation direct;
        const uint32_t seed = 4242U + (static_cast<uint32_t>(batch) * 7919U);
        if (!reference_image(gpu, targets, meshes, eye, lighting, path::kMostBounces, path::kAllLightKinds, seed, &full) ||
            !reference_image(gpu, targets, meshes, eye, lighting, 1,
                             path::kLightKindDisc | path::kLightKindEmitters | path::kLightKindLamps, seed + 1U, &direct)) {
            *ok = false;
            return {};
        }
        not_finite += full.not_finite + direct.not_finite;
        std::vector<std::array<float, 3>> difference(full.mean.size());
        for (std::size_t i = 0; i < difference.size(); ++i) {
            for (std::size_t ch = 0; ch < 3; ++ch) {
                const float d = core::at(core::at(full.mean, i), ch) - core::at(core::at(direct.mean, i), ch);
                core::at(core::at(difference, i), ch) = d;
                core::at(core::at(pixels, i), ch) += d / static_cast<float>(batches);
            }
        }
        means.push_back(region_mean(difference, region, channel));
    }
    Answer out = summary(means, pixels);
    out.not_finite = not_finite;
    return out;
}

struct RunPlan {
    int batches = 4;
    int frames = 128;
    int settle = 12;
    uint32_t spatial = 0xFFU;
    bool temporal = true;
    bool cache = true; // (round MB) the radiance cache's further bounces
    uint32_t local_candidates = 0; // (round DD) the secondary vertices' local lights' RIS candidates (0: the game's)
    bool boiling_filter = true;    // (round DD) RTXDI's boiling filter (off: the estimate unbiased)
    bool specular_reuse = true;    // (round WS) the specular reservoir's reuse (off: each frame's own GGX ray)
    bool specular_only = false;    // (round WS) the sums the specular alone
    std::function<Vec3(int)> camera;
    std::function<bool(int)> counted;
    std::function<void(const vk::RestirGiAccumulation&)> each_frame; // (round BR: read back every counted frame when set)
};

Answer restir_gi(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, const Lighting& lighting,
                 const RunPlan& plan, const Region& region, bool* ok, int channel = -1)
{
    std::vector<double> means;
    std::vector<std::array<float, 3>> pixels(static_cast<std::size_t>(kWidth) * kHeight, {0.0F, 0.0F, 0.0F});
    uint32_t not_finite = 0;
    for (int batch = 0; batch < plan.batches; ++batch) {
        vk::RestirGiOverrides overrides;
        overrides.on = true;
        overrides.roughness = lighting.roughness;
        overrides.f0 = lighting.f0;
        overrides.albedo_scale = lighting.albedo;
        overrides.sky = lighting.sky;
        overrides.sun_toward = lighting.sun_toward;
        overrides.sun_light = lighting.sun_light;
        overrides.sun_radius_degrees = lighting.sun_radius;
        overrides.lamps = lighting.lamps;
        overrides.lamps_surfaced.assign(lighting.lamps.size(), 0);
        overrides.spatial = plan.spatial;
        overrides.temporal = plan.temporal;
        overrides.cache = plan.cache;
        overrides.local_candidates = plan.local_candidates;
        overrides.boiling_filter = plan.boiling_filter;
        overrides.specular_reuse = plan.specular_reuse;
        overrides.specular_only = plan.specular_only;
        overrides.seed = 977U + (static_cast<uint32_t>(batch) * 104729U);
        vk::set_restir_gi_overrides(overrides);
        vk::forget_restir_gi_history();
        vk::set_restir_gi_accumulate(false);
        for (int frame = -plan.settle; frame < plan.frames; ++frame) {
            if (frame == 0) {
                vk::restart_restir_gi_accumulation();
            }
            const int at = std::max(frame, 0);
            const bool counted = frame >= 0 && plan.counted(at);
            vk::set_restir_gi_accumulate(counted);
            draw_frame(gpu, targets, meshes, view_from(plan.camera(at)));
            if (counted && plan.each_frame) {
                vk::RestirGiAccumulation one;
                vk::Backend& state = vk::backend();
                const std::scoped_lock lock(state.mutex);
                if (!vk::read_restir_gi_accumulation(state, &one)) {
                    *ok = false;
                    break;
                }
                plan.each_frame(one);
            }
        }
        vk::RestirGiAccumulation image;
        {
            vk::Backend& state = vk::backend();
            const std::scoped_lock lock(state.mutex);
            if (!vk::read_restir_gi_accumulation(state, &image) || image.width != kWidth || image.height != kHeight) {
                *ok = false;
                break;
            }
        }
        not_finite += image.not_finite;
        means.push_back(region_mean(image.mean, region, channel));
        for (std::size_t i = 0; i < image.mean.size(); ++i) {
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(core::at(pixels, i), ch) += core::at(core::at(image.mean, i), ch) / static_cast<float>(plan.batches);
            }
        }
    }
    vk::set_restir_gi_overrides({});
    vk::set_restir_gi_accumulate(true);
    Answer out = summary(means, pixels);
    out.not_finite = not_finite;
    return out;
}

bool agrees(double got, double expected, double share = kAgreement)
{
    return std::abs(got - expected) <= share * std::abs(expected);
}
double percent(double got, double expected) { return 100.0 * (got - expected) / std::max(std::abs(expected), 1e-12); }

rt::Emitter lamp_at(const Vec3& at, float intensity, float core)
{
    rt::Emitter one;
    one.position = {static_cast<float>(x_of(at)), static_cast<float>(y_of(at)), static_cast<float>(z_of(at))};
    one.size = 1.0F;
    one.colour = {1.0F, 1.0F, 1.0F};
    one.intensity = intensity;
    one.reach = 6000.0F;
    one.core = core;
    return one;
}

// --- the checks --------------------------------------------------------------------------------------------------------------

// (round DD) How a series of frames flickers over a region: the share of pixel-frames over 4 x their pixel's mean over
// the frames (single bright pixels - what a denoiser passes as sparkle) and each pixel's deviation over its mean.
struct Flicker {
    double over_share = 1.0;
    double variation = 0.0;
    std::size_t pixels = 0;
    double region_variation = 0.0; // (round FA) the region's mean's deviation frame to frame over its mean
};
Flicker flicker_of(const std::vector<std::vector<float>>& series, const Region& region)
{
    Flicker out;
    std::size_t over = 0;
    std::size_t samples = 0;
    double variation = 0.0;
    for (std::size_t i = 0; i < series.size(); ++i) {
        const std::vector<float>& s = core::at(series, i);
        if (core::at(region, i) == 0 || s.size() < 2) {
            continue;
        }
        double mean = 0.0;
        for (const float x : s) {
            mean += x;
        }
        mean /= static_cast<double>(s.size());
        if (!(mean > 0.0)) {
            continue;
        }
        double spread = 0.0;
        for (const float x : s) {
            spread += (x - mean) * (x - mean);
            over += x > 4.0 * mean ? 1U : 0U;
        }
        samples += s.size();
        variation += std::sqrt(spread / static_cast<double>(s.size())) / mean;
        ++out.pixels;
    }
    out.over_share = samples > 0 ? static_cast<double>(over) / static_cast<double>(samples) : 1.0;
    out.variation = out.pixels > 0 ? variation / static_cast<double>(out.pixels) : 0.0;
    // (round FA) the region's mean frame by frame: an error every pixel of it shares (a light grid cell's picks) moves it
    // as a whole - what a denoiser takes for the picture
    std::vector<double> means;
    for (std::size_t i = 0; i < series.size(); ++i) {
        const std::vector<float>& s = core::at(series, i);
        if (core::at(region, i) == 0) {
            continue;
        }
        means.resize(std::max(means.size(), s.size()), 0.0);
        for (std::size_t k = 0; k < s.size(); ++k) {
            core::at(means, k) += core::at(s, k);
        }
    }
    if (means.size() > 1) {
        double mean = 0.0;
        for (const double m : means) {
            mean += m;
        }
        mean /= static_cast<double>(means.size());
        double spread = 0.0;
        for (const double m : means) {
            spread += (m - mean) * (m - mean);
        }
        out.region_variation = mean > 0.0 ? std::sqrt(spread / static_cast<double>(means.size())) / mean : 0.0;
    }
    return out;
}

// (round DD) A night lit by lamps of very different power - one far floodlight with nearly all of it, six small lamps in
// front of the wall (each 1/200 of the flood's): the wall the floor's indirect rays meet is lit mostly by the small
// ones. The secondary vertices' next-event estimation picked one local light by power: a small lamp in about one pick in
// 200, its light then x 200 - the fireflies (BO at night: bright specks over the street, the bank's ceiling, the bus
// stop - the triangles of its lit signs and windows). With RIS over 8 of the pool's picks toward the light each sends
// to the point (RTXDI's ReSTIR GI's local light samples at its secondary surfaces) the floor flickers far less, and its
// mean is the reference's (unbiased) as one pick's is.
void check_local_candidates(render::Gpu& gpu, const Targets& targets, const std::array<SceneMesh, 3>& meshes,
                            Tally* tally, bool* read, uint32_t* not_finite)
{
    text::print("lamps of very different power at night (a floodlight far off, six small lamps by the wall):\n");
    const std::array<SceneMesh, 2> floor_wall{meshes.at(0), meshes.at(2)};
    const std::span<const SceneMesh> scene(floor_wall);
    Lighting night;
    night.sky = {0.002F, 0.002F, 0.003F};
    night.sun_light = {0.0F, 0.0F, 0.0F};
    night.lamps = {lamp_at({-2500.0, 800.0, -2500.0}, 4.0F, 100.0F)};
    for (int k = 0; k < 6; ++k) {
        night.lamps.push_back(lamp_at({-300.0 + (120.0 * k), 40.0, kWallZ - 30.0}, 2.0F, 10.0F));
    }
    const Region floor = floor_region(kEyeA);
    const std::size_t pixels = static_cast<std::size_t>(kWidth) * kHeight;
    const auto run = [&](uint32_t candidates, bool boiling, Flicker* flicker) {
        std::vector<std::vector<float>> series(pixels);
        RunPlan plan;
        plan.batches = 16;
        plan.frames = 128;
        plan.camera = [](int /*frame*/) { return kEyeA; };
        plan.counted = [](int /*frame*/) { return true; };
        plan.local_candidates = candidates;
        plan.boiling_filter = boiling;
        plan.each_frame = [&series](const vk::RestirGiAccumulation& one) {
            for (std::size_t i = 0; i < series.size() && i < one.last.size(); ++i) {
                const std::array<float, 3>& c = core::at(one.last, i);
                const double value = luma({core::at(c, 0), core::at(c, 1), core::at(c, 2)});
                core::at(series, i).push_back(static_cast<float>(value));
            }
        };
        const Answer got = restir_gi(gpu, targets, scene, night, plan, floor, read);
        *not_finite += got.not_finite;
        *flicker = flicker_of(series, floor);
        return got;
    };
    Flicker one_flicker;
    Flicker ris_flicker;
    Flicker unfiltered_flicker;
    const Answer one = run(1, true, &one_flicker);
    const Answer ris = run(0, true, &ris_flicker);
    const Answer unfiltered = run(0, false, &unfiltered_flicker);
    const Answer reference = reference_gi(gpu, targets, scene, kEyeA, night, floor, 32, read);
    *not_finite += reference.not_finite;
    // (the boiling filter drops the reservoirs far over their group's - the fireflies' light: biased low, the more the
    // more fireflies; without it the estimate is the reference's)
    tally->check(*read && agrees(unfiltered.mean, reference.mean, kLooseAgreement) &&
                     std::abs(ris.mean - reference.mean) < std::abs(one.mean - reference.mean),
                 text::format("the floor's indirect light: RIS over 8 picks %.6f +- %.6f without the boiling filter (%+.2f %%; "
                              "within 3 %%), with it %.6f (%+.2f %%), one pick with it %.6f +- %.6f (%+.2f %%: the fireflies "
                              "it drops); the reference %.6f +- %.6f",
                              unfiltered.mean, unfiltered.sigma, percent(unfiltered.mean, reference.mean), ris.mean,
                              percent(ris.mean, reference.mean), one.mean, one.sigma, percent(one.mean, reference.mean),
                              reference.mean, reference.sigma));
    tally->check(*read && ris_flicker.pixels > 1000 && ris_flicker.variation < 0.75 * one_flicker.variation &&
                     ris_flicker.over_share <= one_flicker.over_share,
                 text::format("its flicker frame to frame: each pixel's variation %.3f of its mean with RIS over 8 picks, %.3f "
                              "with one (under 0.75 x); %.3f %% / %.3f %% of the pixel-frames over 4 x their mean - %zu pixels",
                              ris_flicker.variation, one_flicker.variation, 100.0 * ris_flicker.over_share,
                              100.0 * one_flicker.over_share, ris_flicker.pixels));
}

// (round DO - RTXDI's ReGIR at secondary surfaces) The same night with ReSTIR DI on beside GI (its light grid made each
// frame from the same lamps): the secondary vertices' local lights' candidates from their cell's picks
// (vulkan_light_list.h light_grid_reader_glsl) instead of the pool by power - the floor's mean still the reference's
// (unbiased: within 3 %), its flicker frame to frame lower than the pool's.
void check_grid_candidates(render::Gpu& gpu, const Targets& targets, const std::array<SceneMesh, 3>& meshes, Tally* tally,
                           bool* read, uint32_t* not_finite)
{
    text::print("the same night, the local lights' candidates from ReSTIR DI's light grid (round DO):\n");
    const std::array<SceneMesh, 2> floor_wall{meshes.at(0), meshes.at(2)};
    const std::span<const SceneMesh> scene(floor_wall);
    Lighting night;
    night.sky = {0.002F, 0.002F, 0.003F};
    night.sun_light = {0.0F, 0.0F, 0.0F};
    night.lamps = {lamp_at({-2500.0, 800.0, -2500.0}, 4.0F, 100.0F)};
    for (int k = 0; k < 6; ++k) {
        night.lamps.push_back(lamp_at({-300.0 + (120.0 * k), 40.0, kWallZ - 30.0}, 2.0F, 10.0F));
    }
    const Region floor = floor_region(kEyeA);
    const std::size_t pixels = static_cast<std::size_t>(kWidth) * kHeight;
    vk::RestirOverrides direct; // (ReSTIR DI's check variant - its own sums, no inputs: here for its light grid)
    direct.on = true;
    direct.lamps = night.lamps;
    direct.lamps_surfaced.assign(night.lamps.size(), 0);
    direct.seed = 53U;
    vk::set_restir_overrides(direct);
    vk::forget_restir_history();
    const auto run = [&](bool grid, Flicker* flicker) {
        std::vector<std::vector<float>> series(pixels);
        vk::set_restir_gi_grid(grid);
        RunPlan plan;
        plan.batches = 16;
        plan.frames = 128;
        plan.camera = [](int /*frame*/) { return kEyeA; };
        plan.counted = [](int /*frame*/) { return true; };
        plan.boiling_filter = false; // (unbiased: the means compared with the reference's)
        plan.each_frame = [&series](const vk::RestirGiAccumulation& one) {
            for (std::size_t i = 0; i < series.size() && i < one.last.size(); ++i) {
                const std::array<float, 3>& c = core::at(one.last, i);
                core::at(series, i).push_back(static_cast<float>(luma({core::at(c, 0), core::at(c, 1), core::at(c, 2)})));
            }
        };
        const Answer got = restir_gi(gpu, targets, scene, night, plan, floor, read);
        *not_finite += got.not_finite;
        *flicker = flicker_of(series, floor);
        return got;
    };
    Flicker pool_flicker;
    Flicker grid_flicker;
    const Answer pool = run(false, &pool_flicker);
    const Answer grid = run(true, &grid_flicker);
    vk::set_restir_gi_grid(true);
    vk::set_restir_overrides({});
    const Answer reference = reference_gi(gpu, targets, scene, kEyeA, night, floor, 32, read);
    *not_finite += reference.not_finite;
    tally->check(*read && agrees(grid.mean, reference.mean, kLooseAgreement) &&
                     agrees(pool.mean, reference.mean, kLooseAgreement),
                 text::format("the floor's indirect light: the grid's candidates %.6f +- %.6f (%+.2f %%), the pool's %.6f +- "
                              "%.6f (%+.2f %%), the reference %.6f +- %.6f (both within 3 %%)",
                              grid.mean, grid.sigma, percent(grid.mean, reference.mean), pool.mean, pool.sigma,
                              percent(pool.mean, reference.mean), reference.mean, reference.sigma));
    tally->check(*read && grid_flicker.pixels > 1000 && grid_flicker.variation < pool_flicker.variation,
                 text::format("its flicker frame to frame: each pixel's variation %.3f of its mean with the grid's candidates, "
                              "%.3f with the pool's; %.3f %% / %.3f %% of the pixel-frames over 4 x their mean - %zu pixels",
                              grid_flicker.variation, pool_flicker.variation, 100.0 * grid_flicker.over_share,
                              100.0 * pool_flicker.over_share, grid_flicker.pixels));
    // (round FA) a cell's picks are every point's in it for the frame: their error moves the floor's whole mean from frame
    // to frame - BO's lamp pools brightening and darkening by 2 - 4 x through GI's specular ray, which reuses nothing. That
    // ray now takes the pool's candidates (vulkan_light_list.h ll_cell_candidates): the floor's mean with the grid on
    // moves no more than the pool's.
    tally->check(*read && grid_flicker.region_variation <= 1.25 * pool_flicker.region_variation + 1.0e-3,
                 text::format("the floor's mean frame to frame: %.4f of it with the grid's candidates, %.4f with the pool's "
                              "(at most 1.25 x)",
                              grid_flicker.region_variation, pool_flicker.region_variation));
}

// (round OM - vulkan_restir_di.h SharedPrimary) One primary surface a view: ReSTIR DI on beside ReSTIR GI (its check's
// variant - the same camera rays through the pixels' centres), GI run twice from the same start (its random numbers the
// same) - reading DI's surfaces, and tracing its own - must give the same image.
bool shared_run(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, const Lighting& lighting,
                bool shared, vk::RestirGiAccumulation* image)
{
    constexpr int kSharedFrames = 12;
    vk::set_restir_shared_primary(shared);
    vk::RestirOverrides direct;
    direct.on = true;
    direct.lamps = lighting.lamps;
    direct.lamps_surfaced.assign(lighting.lamps.size(), 0);
    direct.seed = 31U;
    vk::set_restir_overrides(direct);
    vk::forget_restir_history();
    vk::RestirGiOverrides overrides;
    overrides.on = true;
    overrides.roughness = lighting.roughness;
    overrides.f0 = lighting.f0;
    overrides.albedo_scale = lighting.albedo;
    overrides.sky = lighting.sky;
    overrides.sun_toward = lighting.sun_toward;
    overrides.sun_light = lighting.sun_light;
    overrides.sun_radius_degrees = lighting.sun_radius;
    overrides.lamps = lighting.lamps;
    overrides.lamps_surfaced.assign(lighting.lamps.size(), 0);
    overrides.cache = false; // (the cache's sums are atomics: their order isn't fixed)
    overrides.seed = 4099U;
    vk::set_restir_gi_overrides(overrides);
    vk::set_restir_gi_pool(false); // (the pool's picks are made again each submission: not the same twice)
    vk::restart_restir_gi_sequence();
    vk::set_restir_gi_accumulate(true);
    vk::restart_restir_gi_accumulation();
    for (int frame = 0; frame < kSharedFrames; ++frame) {
        draw_frame(gpu, targets, meshes, view_from(kEyeA));
    }
    bool read = false;
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        read = vk::read_restir_gi_accumulation(state, image) && image->width == kWidth && image->height == kHeight;
    }
    vk::set_restir_gi_overrides({});
    vk::set_restir_overrides({});
    vk::set_restir_shared_primary(true);
    vk::set_restir_gi_pool(true);
    return read;
}

void check_shared_primary(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes,
                          const Lighting& lighting, Tally* tally)
{
    text::print("one primary surface for ReSTIR DI and GI (round OM):\n");
    vk::RestirGiAccumulation traced;
    vk::RestirGiAccumulation shared;
    const uint64_t before = vk::restir_gi_stats().shared_views;
    const bool read_traced = shared_run(gpu, targets, meshes, lighting, false, &traced);
    const uint64_t after_traced = vk::restir_gi_stats().shared_views;
    const bool read_shared = shared_run(gpu, targets, meshes, lighting, true, &shared);
    const uint64_t after_shared = vk::restir_gi_stats().shared_views;
    double largest = 0.0;
    double total = 0.0;
    std::size_t lit = 0;
    for (std::size_t i = 0; read_traced && read_shared && i < traced.mean.size() && i < shared.mean.size(); ++i) {
        for (std::size_t ch = 0; ch < 3; ++ch) {
            const double a = core::at(core::at(traced.mean, i), ch);
            const double b = core::at(core::at(shared.mean, i), ch);
            largest = std::max(largest, std::abs(a - b));
            total += a;
            lit += a > 0.0 ? 1 : 0;
        }
    }
    const double mean = lit > 0 ? total / static_cast<double>(lit) : 0.0;
    tally->check(read_traced && read_shared && after_traced == before && after_shared > after_traced && lit > 0 &&
                     largest <= 1.0e-6 * std::max(mean, 1.0e-6),
                 text::format("ReSTIR GI with ReSTIR DI's primary surfaces (%llu views shared) and with its own: the "
                              "same image - the largest difference %.3g (mean %.4g over %zu lit channels)",
                              static_cast<unsigned long long>(after_shared - after_traced), largest, mean, lit));
}

// (round OM - round PF's gap: the fog with emitting triangles) The haze lit by the disc's 16 emitting triangles (the
// light store's - no lamp in the frame): its light list's candidates from the presampled pool, and the same candidates
// picked at each point as before the pool (vulkan_rt_fog.h set_rt_fog_light_pool) - one estimator: the two histories'
// light the same within their noise, and the haze lit at all.
double fog_light(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, bool pool, bool* ok)
{
    constexpr int kFogFrames = 96;
    vk::set_rt_fog_light_pool(pool);
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        vk::rt_fog_reset_history(state);
    }
    for (int frame = 0; frame < kFogFrames; ++frame) {
        draw_frame(gpu, targets, meshes, view_from(kEyeA));
    }
    vk::RtFogTexels history;
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        *ok = vk::read_rt_fog_image(state, {0, 0, kWidth, kHeight}, vk::RtFogImage::kHistory, &history) && *ok;
    }
    vk::set_rt_fog_light_pool(true);
    double sum = 0.0;
    for (const std::array<float, 4>& texel : history.texels) {
        sum += luma({core::at(texel, 0), core::at(texel, 1), core::at(texel, 2)});
    }
    return history.texels.empty() ? 0.0 : sum / static_cast<double>(history.texels.size());
}

void check_fog_emitters(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("the fog lit by emitting triangles (round OM - the presampled pool's triangle records):\n");
    vk::RtFogSettings fog;
    fog.on = true;
    fog.haze = true;
    fog.scale = 2;
    vk::set_rt_fog_settings(fog);
    bool ok = true;
    const double pooled = fog_light(gpu, targets, meshes, true, &ok);
    const double picked = fog_light(gpu, targets, meshes, false, &ok);
    const vk::RtFogLights lights = [] {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        return vk::rt_fog_lights_locked(state);
    }();
    vk::set_rt_fog_settings({});
    constexpr double kFogAgreement = 0.03;
    tally->check(ok && lights.triangles > 0 && lights.lamps.empty() && picked > 0.0 &&
                     std::abs(pooled - picked) <= kFogAgreement * picked,
                 text::format("the haze's light from %zu emitting triangles (no lamp): from the pool %.5g, picked at each "
                              "point %.5g (%+.2f %%; within 3 %%)",
                              lights.triangles, pooled, picked, picked > 0.0 ? 100.0 * (pooled / picked - 1.0) : 0.0));
}

// (round WS) The specular reservoir, measured on the specular alone (RestirGiOverrides::specular_only) of the scene's own
// materials (F0 0.04): the floor frame by frame with the specular's reuse off (each frame's own GGX ray, as before) and
// on, at a glossy lobe (0.35) and a less rough one (0.25) - the share of pixel-frames over 4 x their mean and each
// pixel's variation (flicker_of); and the mean over many frames with the reuse that of the own rays (both unbiased).
void check_specular_reuse(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, const Lighting& lit,
                          Tally* tally, bool* read, uint32_t* not_finite)
{
    text::print("the specular reservoir (the specular alone):\n");
    constexpr int kFrames = 64;
    const Region region = region_of(kEyeA);
    const Region floor = floor_region(kEyeA);
    for (const float roughness : {0.35F, 0.25F}) {
        Lighting spec = lit;
        spec.roughness = roughness;
        RunPlan plan;
        plan.camera = [](int /*frame*/) { return kEyeA; };
        plan.counted = [](int /*frame*/) { return true; };
        plan.specular_only = true;
        RunPlan own_rays = plan;
        own_rays.specular_reuse = false;
        const Answer before = restir_gi(gpu, targets, meshes, spec, own_rays, region, read);
        const Answer got = restir_gi(gpu, targets, meshes, spec, plan, region, read);
        *not_finite += before.not_finite + got.not_finite;
        tally->check(*read && before.mean > 0.0 && agrees(got.mean, before.mean, kLooseAgreement),
                     text::format("roughness %.2f, the specular reused %.6f +- %.6f, by its own rays %.6f +- %.6f (%+.2f %%; "
                                  "within 3 %%)",
                                  static_cast<double>(roughness), got.mean, got.sigma, before.mean, before.sigma,
                                  percent(got.mean, before.mean)));
        std::array<Flicker, 2> flicker{};
        for (const bool reuse : {false, true}) {
            std::vector<std::vector<float>> series(static_cast<std::size_t>(kWidth) * kHeight);
            RunPlan one = plan;
            one.batches = 1;
            one.frames = kFrames;
            one.specular_reuse = reuse;
            one.each_frame = [&series](const vk::RestirGiAccumulation& frame) {
                for (std::size_t i = 0; i < series.size() && i < frame.last.size(); ++i) {
                    const std::array<float, 3>& c = core::at(frame.last, i);
                    core::at(series, i).push_back(static_cast<float>(luma({core::at(c, 0), core::at(c, 1), core::at(c, 2)})));
                }
            };
            const Answer a = restir_gi(gpu, targets, meshes, spec, one, floor, read);
            *not_finite += a.not_finite;
            core::at(flicker, reuse ? 1U : 0U) = flicker_of(series, floor);
        }
        // a moving camera (an orbit): the history's lobe another view's - the reuse's mean still the own rays'
        RunPlan orbit = plan;
        orbit.frames = 96;
        orbit.camera = [](int frame) {
            constexpr double kRadius = 40.0;
            const double a = 2.0 * kPi * frame / 48.0;
            return Vec3{x_of(kEyeA) + kRadius * std::cos(a), y_of(kEyeA), z_of(kEyeA) + kRadius * std::sin(a)};
        };
        RunPlan orbit_own = orbit;
        orbit_own.specular_reuse = false;
        const Answer moving_own = restir_gi(gpu, targets, meshes, spec, orbit_own, region, read);
        const Answer moving = restir_gi(gpu, targets, meshes, spec, orbit, region, read);
        *not_finite += moving_own.not_finite + moving.not_finite;
        tally->check(*read && moving_own.mean > 0.0 && agrees(moving.mean, moving_own.mean, kLooseAgreement),
                     text::format("roughness %.2f, the camera moving: the specular reused %.6f, by its own rays %.6f "
                                  "(%+.2f %%; within 3 %%)",
                                  static_cast<double>(roughness), moving.mean, moving_own.mean,
                                  percent(moving.mean, moving_own.mean)));
        const Flicker& off = core::at(flicker, 0);
        const Flicker& on = core::at(flicker, 1);
        tally->check(*read && on.pixels > 1000 && on.variation < off.variation && on.over_share <= off.over_share,
                     text::format("roughness %.2f, %d frames: pixel-frames over 4 x their mean %.3f %% -> %.3f %%, each "
                                  "pixel's variation %.3f -> %.3f (the specular's own ray -> reused) - %zu pixels",
                                  static_cast<double>(roughness), kFrames, 100.0 * off.over_share, 100.0 * on.over_share,
                                  off.variation, on.variation, on.pixels));
    }
}

void check_device(render::Gpu& gpu, Tally* tally)
{
    Targets targets;
    std::array<SceneMesh, 3> meshes{};
    const bool made = make_targets(gpu, &targets) && make_mesh(gpu, floor_triangles(), false, &meshes.at(0)) &&
                      make_mesh(gpu, disc_triangles(), true, &meshes.at(1)) &&
                      make_mesh(gpu, wall_triangles(), false, &meshes.at(2));
    tally->check(made, "the scene made (a floor; a disc of 16 emitting triangles with a light store; a wall)");
    if (!made) {
        release_targets(gpu, targets);
        return;
    }
    const std::span<const SceneMesh> everything(meshes);
    bool read = true;
    uint32_t not_finite = 0;
    Lighting lit;
    lit.lamps = {lamp_at({300.0, 180.0, -200.0}, 6.0F, 20.0F), lamp_at({-200.0, 120.0, 400.0}, 12.0F, 25.0F),
                 lamp_at({150.0, 60.0, 360.0}, 3.0F, 10.0F), lamp_at({-500.0, 250.0, -300.0}, 0.05F, 10.0F),
                 lamp_at({900.0, 200.0, 500.0}, 0.5F, 15.0F)};
    RunPlan plan;
    plan.camera = [](int /*frame*/) { return kEyeA; };
    plan.counted = [](int /*frame*/) { return true; };
    constexpr int kReferenceBatches = 8;

    // 1. the still scene: glossy, then smooth
    text::print("still scene (a floor, the emitting disc, a wall, 5 lamps, a sun, an even sky), %ux%u:\n", kWidth, kHeight);
    const Region region_a = region_of(kEyeA);
    const Answer ref_a = reference_gi(gpu, targets, everything, kEyeA, lit, region_a, kReferenceBatches, &read);
    not_finite += ref_a.not_finite;
    const Answer full = restir_gi(gpu, targets, everything, lit, plan, region_a, &read);
    not_finite += full.not_finite;
    const vk::RestirGiStats stats = vk::restir_gi_stats();
    tally->check(read && stats.sky_ready && stats.lights == 2U + static_cast<uint32_t>(kDiscSides) + 5U,
                 text::format("the full light list: %u lights (the disc, the sky, %d emitting triangles, 5 lamps); the sky's "
                              "grid %s",
                              stats.lights, kDiscSides, stats.sky_ready ? "in use" : "not made"));
    tally->check(read && agrees(full.mean, ref_a.mean),
                 text::format("still, glossy (roughness %.2f): ReSTIR GI %.5f +- %.5f, the reference's indirect %.5f +- "
                              "%.5f (%+.2f %%)",
                              static_cast<double>(kRoughness), full.mean, full.sigma, ref_a.mean, ref_a.sigma,
                              percent(full.mean, ref_a.mean)));
    check_shared_primary(gpu, targets, everything, lit, tally); // (round OM)
    check_fog_emitters(gpu, targets, everything, tally);        // (round OM)
    RunPlan alone = plan;
    alone.spatial = 0;
    alone.temporal = false;
    const Answer no_reuse = restir_gi(gpu, targets, everything, lit, alone, region_a, &read);
    not_finite += no_reuse.not_finite;
    tally->check(read && agrees(no_reuse.mean, ref_a.mean),
                 text::format("still, the initial samples alone (no reuse) %.5f +- %.5f (%+.2f %%)", no_reuse.mean,
                              no_reuse.sigma, percent(no_reuse.mean, ref_a.mean)));
    Lighting smooth = lit;
    smooth.roughness = kSmooth;
    const Answer ref_s = reference_gi(gpu, targets, everything, kEyeA, smooth, region_a, kReferenceBatches, &read);
    not_finite += ref_s.not_finite;
    const Answer got_s = restir_gi(gpu, targets, everything, smooth, plan, region_a, &read);
    not_finite += got_s.not_finite;
    tally->check(read && agrees(got_s.mean, ref_s.mean, kLooseAgreement),
                 text::format("still, smooth (roughness %.2f - the specular by its own ray): ReSTIR GI %.5f +- %.5f, the "
                              "reference %.5f +- %.5f (%+.2f %%; within 3 %%)",
                              static_cast<double>(kSmooth), got_s.mean, got_s.sigma, ref_s.mean, ref_s.sigma,
                              percent(got_s.mean, ref_s.mean)));

    // (round BR) sparkle: a glossy floor (roughness 0.35 - its specular was the diffuse reservoir's sample through the lobe
    // from 0.3 up) frame by frame - the share of pixel-frames over 4 x their pixel's mean over the frames (single bright
    // pixels, what a temporal denoiser passes as sparkle) and each pixel's variation over the frames
    {
        constexpr float kGlossySparkle = 0.35F;
        constexpr int kSparkleFrames = 64;
        Lighting gloss = lit;
        gloss.roughness = kGlossySparkle;
        const std::size_t pixels = static_cast<std::size_t>(kWidth) * kHeight;
        std::vector<std::vector<float>> series(pixels);
        RunPlan sparkle = plan;
        sparkle.batches = 1;
        sparkle.frames = kSparkleFrames;
        sparkle.each_frame = [&series](const vk::RestirGiAccumulation& one) {
            for (std::size_t i = 0; i < series.size() && i < one.last.size(); ++i) {
                const std::array<float, 3>& c = core::at(one.last, i);
                core::at(series, i).push_back(static_cast<float>(luma({core::at(c, 0), core::at(c, 1), core::at(c, 2)})));
            }
        };
        const Region floor_a = floor_region(kEyeA);
        const Answer got = restir_gi(gpu, targets, everything, gloss, sparkle, floor_a, &read);
        not_finite += got.not_finite;
        std::size_t over = 0;
        std::size_t samples = 0;
        double variation = 0.0;
        std::size_t counted_pixels = 0;
        for (std::size_t i = 0; i < pixels; ++i) {
            const std::vector<float>& s = core::at(series, i);
            if (core::at(floor_a, i) == 0 || s.size() < 2) {
                continue;
            }
            double mean = 0.0;
            for (const float x : s) {
                mean += x;
            }
            mean /= static_cast<double>(s.size());
            if (!(mean > 0.0)) {
                continue;
            }
            double spread = 0.0;
            for (const float x : s) {
                spread += (x - mean) * (x - mean);
                over += x > 4.0 * mean ? 1U : 0U;
            }
            samples += s.size();
            variation += std::sqrt(spread / static_cast<double>(s.size())) / mean;
            ++counted_pixels;
        }
        const double over_share = samples > 0 ? static_cast<double>(over) / static_cast<double>(samples) : 1.0;
        const double mean_variation = counted_pixels > 0 ? variation / static_cast<double>(counted_pixels) : 0.0;
        tally->check(read && counted_pixels > 1000 && over_share < 0.0065 && mean_variation < 0.5,
                     text::format("glossy floor (roughness %.2f), %d frames: %.3f %% of the pixel-frames over 4 x their "
                                  "pixel's mean (under 0.65 %%; 0.83 %% with the reservoir's specular), each pixel's variation %.3f of its "
                                  "mean (under 0.5; 0.585 before) - %zu pixels",
                                  static_cast<double>(kGlossySparkle), kSparkleFrames, 100.0 * over_share, mean_variation,
                                  counted_pixels));
    }

    // (round DD) many lights of very different power: the secondary vertices' local lights by RIS over the pool's picks
    check_local_candidates(gpu, targets, meshes, tally, &read, &not_finite);
    // (round DO) ... from ReSTIR DI's light grid inside it
    check_grid_candidates(gpu, targets, meshes, tally, &read, &not_finite);
    // (round WS) the specular reservoir's reuse
    check_specular_reuse(gpu, targets, everything, lit, tally, &read, &not_finite);

    // 2. disocclusion: the camera between two views every frame
    text::print("disocclusion (the camera swapped between two views every frame):\n");
    RunPlan swap = plan;
    swap.frames = 256;
    swap.camera = [](int frame) { return frame % 2 == 0 ? kEyeA : kEyeB; };
    swap.counted = [](int frame) { return frame % 2 == 0; };
    const Answer swapped = restir_gi(gpu, targets, everything, lit, swap, region_a, &read);
    not_finite += swapped.not_finite;
    tally->check(read && agrees(swapped.mean, ref_a.mean),
                 text::format("swapped views: ReSTIR GI in the first view %.5f +- %.5f, the reference %.5f (%+.2f %%)",
                              swapped.mean, swapped.sigma, ref_a.mean, percent(swapped.mean, ref_a.mean)));

    // 3. colour bleeding: a red wall beside the white floor
    text::print("colour bleeding (the wall red):\n");
    {
        std::array<SceneMesh, 3> red{};
        const bool built = make_mesh(gpu, floor_triangles(), false, &red.at(0)) &&
                           make_mesh(gpu, disc_triangles(), true, &red.at(1)) &&
                           make_mesh(gpu, wall_triangles(), false, &red.at(2), kRed);
        tally->check(built, "the red wall's scene made");
        if (built) {
            const std::span<const SceneMesh> scene(red);
            const Region floor = floor_region(kEyeA);
            Lighting grey = lit; // (white light: the floor's colour is the wall's bleeding)
            grey.sky = {0.3F, 0.3F, 0.3F};
            grey.sun_light = {1.0F, 1.0F, 1.0F};
            std::array<double, 2> got{};
            std::array<double, 2> want{};
            for (int channel = 0; channel < 2; ++channel) {
                const Answer r = reference_gi(gpu, targets, scene, kEyeA, grey, floor, kReferenceBatches, &read, channel);
                const Answer g = restir_gi(gpu, targets, scene, grey, plan, floor, &read, channel);
                not_finite += r.not_finite + g.not_finite;
                core::at(want, static_cast<std::size_t>(channel)) = r.mean;
                core::at(got, static_cast<std::size_t>(channel)) = g.mean;
            }
            tally->check(read && agrees(got[0], want[0]) && agrees(got[1], want[1]),
                         text::format("the floor's red %.5f / green %.5f, the reference's %.5f / %.5f (%+.2f %% / %+.2f %%)",
                                      got[0], got[1], want[0], want[1], percent(got[0], want[0]), percent(got[1], want[1])));
            tally->check(read && got[0] > 1.02 * got[1] && want[0] > 1.02 * want[1],
                         text::format("the floor's indirect light redder than green: %.3f (the reference's %.3f)",
                                      got[0] / std::max(got[1], 1e-12), want[0] / std::max(want[1], 1e-12)));
        }
        for (SceneMesh& mesh : red) {
            release_mesh(gpu, &mesh);
        }
    }

    // 4. a room lit only through a window in its roof
    text::print("a room lit through a window in its roof (the sun's patch on its floor, the sky through it):\n");
    {
        std::array<SceneMesh, 1> room{};
        const bool built = make_mesh(gpu, room_triangles(), false, &room.at(0));
        tally->check(built, "the room made");
        if (built) {
            Lighting inside = lit;
            inside.lamps.clear();
            inside.sun_toward = {0.2F, 0.95F, 0.15F};
            const std::span<const SceneMesh> scene(room);
            const Region all = every_pixel();
            RunPlan room_plan = plan;
            room_plan.camera = [](int /*frame*/) { return kEyeRoom; };
            // (round MB) the radiance cache's warm-up before counting: it starts empty (every run forgets its history)
            // and its further bounces settle over a few of its 16 frames of history - as after a level's first frames
            room_plan.settle = 64;
            // (round MB) each counted frame's mean, for the stability below
            std::vector<double> still_means;
            room_plan.each_frame = [&still_means, &all](const vk::RestirGiAccumulation& one) {
                still_means.push_back(region_mean(one.last, all));
            };
            const Answer r = reference_gi(gpu, targets, scene, kEyeRoom, inside, all, kReferenceBatches, &read);
            const Answer g = restir_gi(gpu, targets, scene, inside, room_plan, all, &read);
            not_finite += r.not_finite + g.not_finite;
            tally->check(read && r.mean > 0.0 && agrees(g.mean, r.mean, kLooseAgreement),
                         text::format("the room: ReSTIR GI %.5f +- %.5f, the reference's every bounce %.5f +- %.5f (%+.2f %%; "
                                      "within 3 %%)",
                                      g.mean, g.sigma, r.mean, r.sigma, percent(g.mean, r.mean)));
            // (round MB) without the radiance cache: one bounce - the light the further bounces bring missing
            RunPlan one_bounce = room_plan;
            one_bounce.cache = false;
            one_bounce.each_frame = nullptr;
            const Answer g1 = restir_gi(gpu, targets, scene, inside, one_bounce, all, &read);
            not_finite += g1.not_finite;
            tally->check(read && g1.mean < 0.9 * r.mean,
                         text::format("the room without the radiance cache (one bounce): %.5f (%+.2f %% - the further "
                                      "bounces' share the cache brings; under -10 %%)",
                                      g1.mean, percent(g1.mean, r.mean)));
            // (round MB) a moving camera: round a circle about the view every 16 frames, counted where it comes back -
            // the cache's cells change level with the distance, its entries are made and dropped as the view moves
            constexpr int kOrbit = 16;
            constexpr double kOrbitRadius = 60.0;
            RunPlan moving = room_plan;
            moving.frames = 128 * kOrbit / 4;
            moving.camera = [](int frame) {
                const double angle = 2.0 * kPi * static_cast<double>(frame % kOrbit) / kOrbit;
                return Vec3{x_of(kEyeRoom) + (kOrbitRadius * std::sin(angle)), y_of(kEyeRoom),
                            z_of(kEyeRoom) + (kOrbitRadius * (1.0 - std::cos(angle)))};
            };
            moving.counted = [](int frame) { return frame % kOrbit == 0; };
            std::vector<double> moving_means;
            moving.each_frame = [&moving_means, &all](const vk::RestirGiAccumulation& one) {
                moving_means.push_back(region_mean(one.last, all));
            };
            still_means.clear();
            RunPlan still = room_plan;
            still.batches = 1;
            const Answer g_still = restir_gi(gpu, targets, scene, inside, still, all, &read);
            moving.batches = 1;
            const Answer g_moving = restir_gi(gpu, targets, scene, inside, moving, all, &read);
            not_finite += g_still.not_finite + g_moving.not_finite;
            const auto spread = [](const std::vector<double>& values) {
                double mean = 0.0;
                for (const double v : values) {
                    mean += v / static_cast<double>(std::max<std::size_t>(values.size(), 1));
                }
                double sum = 0.0;
                for (const double v : values) {
                    sum += (v - mean) * (v - mean);
                }
                return values.size() > 1 && mean > 0.0 ? std::sqrt(sum / static_cast<double>(values.size() - 1)) / mean : 0.0;
            };
            const double still_spread = spread(still_means);
            const double moving_spread = spread(moving_means);
            tally->check(read && agrees(g_moving.mean, r.mean, kLooseAgreement) && moving_means.size() >= 16 &&
                             moving_spread < 2.0 * still_spread + 0.01,
                         text::format("the room, the camera moving (a circle every %d frames, counted where it comes back): "
                                      "%.5f (%+.2f %%; within 3 %%); each frame's mean varies by %.2f %% (still %.2f %%; under "
                                      "2 x that + 1 %%)",
                                      kOrbit, g_moving.mean, percent(g_moving.mean, r.mean), 100.0 * moving_spread,
                                      100.0 * still_spread));
        }
        for (SceneMesh& mesh : room) {
            release_mesh(gpu, &mesh);
        }
    }

    // 5. energy: a white floor under an even sky
    text::print("energy (a white floor under an even sky of radiance 1, nothing else):\n");
    {
        Lighting furnace;
        furnace.sky = {1.0F, 1.0F, 1.0F};
        furnace.sun_light = {0.0F, 0.0F, 0.0F};
        furnace.albedo = 1.0F;
        const std::span<const SceneMesh> floor_only(meshes.data(), 1);
        const Region all_floor = floor_region(kEyeA);
        const Answer r = reference_gi(gpu, targets, floor_only, kEyeA, furnace, all_floor, kReferenceBatches, &read);
        const Answer g = restir_gi(gpu, targets, floor_only, furnace, plan, all_floor, &read);
        not_finite += r.not_finite + g.not_finite;
        tally->check(read && agrees(g.mean, r.mean),
                     text::format("furnace: ReSTIR GI %.5f, the reference %.5f (%+.2f %%)", g.mean, r.mean, percent(g.mean, r.mean)));
        const Answer g_alone = restir_gi(gpu, targets, floor_only, furnace, alone, all_floor, &read);
        not_finite += g_alone.not_finite;
        tally->check(read && agrees(g_alone.mean, r.mean),
                     text::format("furnace, no reuse: ReSTIR GI %.5f (%+.2f %%)", g_alone.mean, percent(g_alone.mean, r.mean)));
        tally->check(read && agrees(g.mean, 1.0, kLooseAgreement),
                     text::format("furnace: the white floor reflects %.4f of the sky's radiance (1: energy kept; within 3 %%)",
                                  g.mean));

        // 6. (round CW) the same floor painted over: a blended layer 0.8 units over it (a road's crosswalk) is the
        // surface the camera sees and the prepass keeps - the pixel's surface is the paint, lit as the floor was. Before,
        // the camera ray took the opaque kinds alone: it found the floor under the paint, and every light ray from there
        // went through the paint (seen through by its coverage, 1 here) - the patch black.
        text::print("a layer painted on the floor (blend 5, %.1f units over it - the prepass's surface):\n", kPaintLift);
        std::array<SceneMesh, 2> painted{};
        const bool built = make_mesh(gpu, floor_triangles(), false, &painted.at(0)) &&
                           make_mesh(gpu, paint_triangles(), false, &painted.at(1), kWhite, true);
        tally->check(built, "the painted floor made");
        if (built) {
            const Answer p = restir_gi(gpu, targets, painted, furnace, plan, all_floor, &read);
            not_finite += p.not_finite;
            tally->check(read && agrees(p.mean, g.mean, kLooseAgreement),
                         text::format("painted: the paint reflects %.4f of the sky's radiance, the bare floor %.4f (%+.2f %%; "
                                      "within 3 %%)",
                                      p.mean, g.mean, percent(p.mean, g.mean)));
        }
        for (SceneMesh& mesh : painted) {
            release_mesh(gpu, &mesh);
        }
    }

    tally->check(read && not_finite == 0, text::format("estimates not finite: %u", not_finite));
    tally->check(read, "every run read back");
    const vk::RestirGiStats after = vk::restir_gi_stats();
    text::print("  the passes' device time: %.3f ms at %ux%u (the mean of the last 64 passes)\n", after.gpu_ms,
                after.gpu_width, after.gpu_height);
    for (SceneMesh& mesh : meshes) {
        release_mesh(gpu, &mesh);
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
    vk::set_context_options({.validation = options.validation,
                             .device_index = options.device,
                             .on_message = print_line,
                             .ray_tracing = true},
                            window);
    render::GpuContext& context = vk::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return Opened::kNoDevice;
    }
    const vk::Backend& state = vk::backend();
    text::print("validation layer: %s\n",
                state.context.validation_on() ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWindowWidth,
                       .height = kWindowHeight,
                       .windowed = true,
                       .vsync = false,
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
        text::print("this device has no ray queries - nothing to check\n");
        context.shutdown();
        return Opened::kNoRayQueries;
    }
    return Opened::kYes;
}

int run(const Options& options, SDL_Window* window)
{
    Tally tally;
    const Opened opened = open_device(options, window);
    if (opened == Opened::kNoRayQueries) {
        return 2;
    }
    if (opened == Opened::kNoDevice) {
        return vk::loader_version() == 0 ? 2 : 1;
    }
    check_device(vk::gpu(), &tally);
    vk::set_path_overrides({});
    vk::set_restir_gi_overrides({});
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
    set_log_listener(print_log_line);
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_restir_gi_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan ReSTIR GI check", kWindowWidth, kWindowHeight,
                                          SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        text::print("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SDL_SetWindowPosition(window, position, position);
    tests::show_behind(window);
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
