// NOT ORIGINAL - thug2_vulkan_restir_check.exe: the lamps' direct light by ReSTIR DI (gpu_vulkan/vulkan_restir_di.h; path
// tracing plan, stage 4) tried on this machine against the reference path tracer's direct light (vulkan_path_reference.h,
// one bounce, the emitting surfaces and the lamps alone - `pt ref ... only emit,lamps`) and against answers worked out on
// the processor. Everything goes through the installed render::rt::RayTracing as the game's frame record feeds it: a
// large floor, a disc of 16 emitting triangles above it facing down (a light store: radiance Le), a wall casting shadows,
// and analytic lamps of very different power (rt_lights.h's lamp model).
//   - still scene: the mean over many frames (reservoirs reused in time and space) of the floor's and the wall's pixels
//     within 2 % of the reference's; the emitting triangles found by the shared scan (vulkan_light_list.h);
//   - disocclusion: the camera swapped between two views every frame (the wall hides other floor from each - history
//     taken back through the other camera, failing its tests where the surface differs): the mean in the first view still
//     within 2 % of the reference's;
//   - a moving light: the brightest of 32 lamps circling, the reservoirs kept from frame to frame: the mean over the frames
//     within 2 % of the exact light the processor works out for each frame (the lamp model and the light model);
//   - many lights of very different power: the per-frame error against the converged answer with the reuse and without it
//     (one light from the list a pixel, no reuse - the light list alone): much lower;
//   - (round PT4b) a lamp drawn in a texture (the disc's emission on 1 in 64 texels): every emitting triangle listed,
//     though the scan's points miss the lamp texels of some (none lost); the mean within 2 % of the reference's; the
//     per-frame error low (the target the sample's own emission - not its triangle's mean);
//   - (round PT4c) a still floor lit by 4 dim lamps near among 120 bright far away: each pixel's variation over the
//     frames (the lamps' light alone, what the game's inputs carry) at least 2 x lower with the light grid than with the
//     list by power - the candidates alone and with the temporal reuse; with the spatial reuse lower than the list by
//     power's; both means within 2 % of the exact light;
//   - (round PT4d) the same as a temporal denoiser gets it (a running average over the frames): with permutation
//     sampling and the boiling filter (vulkan_light_list.h restir_denoiser_glsl) the error's frame-to-frame correlation
//     and what the running average still varies by lower than without; the mean within 2 % of the exact light;
//   - no estimate NaN or infinite; the validation layer silent; the passes' device time printed.
//   thug2_vulkan_restir_check [--device N] [--display N] [--no-validation]
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
#include "engine/gfx/rt_brdf.h"
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
#include "platform/run_log.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace value = render::gpu_value;
namespace rt = render::rt;
namespace path = render::rt::path;
namespace brdf = render::rt::brdf;
namespace vk = render::vulkan;
using Vec3 = std::array<double, 3>;
using Rgb = std::array<double, 3>;

constexpr int kWindowWidth = 160;
constexpr int kWindowHeight = 120;
constexpr uint32_t kWidth = 160;
constexpr uint32_t kHeight = 120;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x142; // XYZ | DIFFUSE | TEX1
constexpr uint32_t kStride = 24; // the position, the colour at 12, the uv at 16
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
constexpr uint32_t kWorldOpaque = rt::kWorld | rt::kOpaque | rt::kCastsShadow;
constexpr double kAgreement = 0.02; // (the brief: the realtime mean within 2 % of the reference's)
constexpr float kRoughness = 0.6F;
constexpr float kF0 = 0.04F;
constexpr float kAlbedo = 0.7F;
constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kFirstRow = 40; // the region compared: these rows down to the bottom, every column
constexpr uint32_t kCelLampTexels = 0x000000FFU; // (rt_cel.h word 6's blue: the emission on the lamp texels alone)
// (round PT4b) the lamp texture: kLampTextureSide^2 texels, white on every kLampTexelEvery-th row and column, black
// elsewhere - 1 in 64 texels lit, each triangle of the disc holding a few of them or none at the scan's points
constexpr uint32_t kLampTextureSide = 32;
constexpr uint32_t kLampTexelEvery = 8;
// (its per-frame relative RMS error at most: 0.86 measured - the reference's own pixels part of it; with the triangle's
// mean radiance as the target, before round PT4b, 5.5)
constexpr double kTexturedLampRms = 1.5;

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

// --- the scene ----------------------------------------------------------------------------------------------------------

struct Triangle {
    std::array<Vec3, 3> corners{};
};

std::vector<Triangle> floor_triangles()
{
    const double h = kFloorHalf;
    return {{.corners = {{{-h, 0.0, -h}, {h, 0.0, -h}, {h, 0.0, h}}}}, {.corners = {{{-h, 0.0, -h}, {h, 0.0, h}, {-h, 0.0, h}}}}};
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
    const double w = kWallHalf;
    const double h = kWallHeight;
    const double z = kWallZ;
    return {{.corners = {{{-w, 0.0, z}, {w, 0.0, z}, {w, h, z}}}}, {.corners = {{{-w, 0.0, z}, {w, h, z}, {-w, h, z}}}}};
}

struct SceneMesh {
    rt::MeshId id = 0;
    rt::MaterialId material = 0;
    render::GpuVertexBuffer* vertices = nullptr;
    render::GpuVertexBuffer* stream = nullptr;
};

void put_float(std::span<uint8_t> bytes, std::size_t at, float value)
{
    std::memcpy(bytes.subspan(at, sizeof(value)).data(), &value, sizeof(value));
}
void put_word(std::span<uint8_t> bytes, std::size_t at, uint32_t value)
{
    std::memcpy(bytes.subspan(at, sizeof(value)).data(), &value, sizeof(value));
}

// A mesh of `triangles` (white; untextured, or `lamp_texture`'s texels at uv = x, z over the disc's square) registered;
// `emitting`: with a light store whose every vertex emits kDiscRadiance and has no tint - with `lamp_texture`, a lamp
// drawn in a texture (the store's flag: the emission shown on its lamp texels alone - rt_cel.h word 6's blue).
bool make_mesh(render::Gpu& gpu, const std::vector<Triangle>& triangles, bool emitting, SceneMesh* mesh,
               render::GpuTexture* lamp_texture = nullptr)
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
            put_float(bytes, base + kUvAt, static_cast<float>(0.5 + (x_of(corner) / (2.0 * kDiscRadius))));
            put_float(bytes, base + kUvAt + sizeof(float), static_cast<float>(0.5 + (z_of(corner) / (2.0 * kDiscRadius))));
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
    surface.colour = {1.0F, 1.0F, 1.0F, 1.0F};
    surface.flags = rt::kOpaque;
    surface.texture = lamp_texture;
    surface.filter = value::kFilterPoint;
    rt::ray_tracing()->set_material(mesh->material, surface);
    if (emitting) {
        rt::CelWords words = rt::kCelNoStream;
        // (a vertex with the store: no tint, its emission; a lamp drawn in a texture: blue 255)
        core::at(words, rt::kCelFlagsWord) = rt::kCelHasStream | (lamp_texture != nullptr ? kCelLampTexels : 0U);
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

// (round PT4b) The lamp texture (kLampTextureSide^2 A8R8G8B8, a white texel every kLampTexelEvery rows and columns).
render::GpuTexture* make_lamp_texture(render::Gpu& gpu)
{
    render::GpuTexture* texture = nullptr;
    if (gpu.create_texture({.width = kLampTextureSide,
                            .height = kLampTextureSide,
                            .levels = 1,
                            .usage = 0,
                            .format = value::kFormatA8R8G8B8,
                            .pool = value::kPoolManaged},
                           &texture) != value::kResultOk) {
        return nullptr;
    }
    render::GpuLockedRect locked;
    if (gpu.lock_texture(texture, 0, &locked, nullptr, 0) != value::kResultOk) {
        gpu.release_texture(texture);
        return nullptr;
    }
    const std::span<uint8_t> into(static_cast<uint8_t*>(locked.bits),
                                  (static_cast<std::size_t>(kLampTextureSide - 1) * static_cast<uint32_t>(locked.pitch)) +
                                      (kLampTextureSide * 4U));
    for (uint32_t row = 0; row < kLampTextureSide; ++row) {
        for (uint32_t column = 0; column < kLampTextureSide; ++column) {
            const bool lit = row % kLampTexelEvery == kLampTexelEvery / 2 && column % kLampTexelEvery == kLampTexelEvery / 2;
            const uint32_t texel = lit ? 0xFFFFFFFFU : 0xFF000000U;
            put_word(into, (static_cast<std::size_t>(row) * static_cast<uint32_t>(locked.pitch)) + (column * 4U), texel);
        }
    }
    gpu.unlock_texture(texture, 0);
    return texture;
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

// The ray through (x, y) of the view from `eye` toward kAt (a pinhole, y down).
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

// Whether a pixel's ray meets the emitting disc first (its own emission is the reference's at the camera - not a light
// the surfaces get: left out of the comparison).
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
            {.mesh = mesh.id, .material = mesh.material, .flags = kWorldOpaque, .object_to_world = kIdentityRows});
    }
    gpu.begin_scene();
    rt::ray_tracing()->begin_frame(record);
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kWidth, .height = kHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, 0, 1.0F, 0);
    rt::opaque_view_drawn(view); // (ReSTIR's pass: with the lights, before the shading)
    rt::world_view_drawn(view);  // (the reference's: after the lens)
    gpu.end_scene();
    gpu.present();
}

// The pixels compared from `eye`: the rows from kFirstRow down, every column, not those that see the disc itself.
std::vector<uint8_t> region_of(const Vec3& eye)
{
    std::vector<uint8_t> in(static_cast<std::size_t>(kWidth) * kHeight, 0);
    for (uint32_t y = kFirstRow; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            core::at(in, (static_cast<std::size_t>(y) * kWidth) + x) = sees_disc(eye, x, y) ? 0 : 1;
        }
    }
    return in;
}

// Every pixel from kFirstRow down (a scene without the disc).
std::vector<uint8_t> rows_region()
{
    std::vector<uint8_t> in(static_cast<std::size_t>(kWidth) * kHeight, 0);
    std::fill(in.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(kFirstRow) * kWidth), in.end(), uint8_t{1});
    return in;
}

double region_mean(std::span<const std::array<float, 3>> image, std::span<const uint8_t> region)
{
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < image.size() && i < region.size(); ++i) {
        if (core::at(region, i) != 0) {
            const std::array<float, 3>& c = core::at(image, i);
            sum += luma({core::at(c, 0), core::at(c, 1), core::at(c, 2)});
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

struct Answer {
    double mean = 0.0;
    double sigma = 0.0;
    std::vector<std::array<float, 3>> pixels; // (the mean of the batches, per pixel)
    uint32_t not_finite = 0;
};

// The reference's direct light from `eye`: one bounce, the emitting surfaces and the lamps alone, the material forced.
Answer reference(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, const Vec3& eye,
                 const std::vector<rt::Emitter>& lamps, bool* ok, float roughness = kRoughness)
{
    constexpr int kBatches = 8;
    constexpr int kFrames = 4;
    Answer out;
    const rt::ViewDrawn view = view_from(eye);
    const std::vector<uint8_t> region = region_of(eye);
    std::vector<double> means;
    for (int batch = 0; batch < kBatches; ++batch) {
        vk::PathOverrides overrides;
        overrides.on = true;
        overrides.bounces = 1;
        overrides.samples_per_frame = 32;
        overrides.force_material = true;
        overrides.roughness = roughness;
        overrides.f0 = kF0;
        overrides.albedo_scale = kAlbedo;
        overrides.lamps = lamps;
        overrides.lamps_surfaced.assign(lamps.size(), 0);
        overrides.light_kinds = path::kLightKindEmitters | path::kLightKindLamps;
        overrides.seed = 4242U + (static_cast<uint32_t>(batch) * 7919U);
        vk::set_path_overrides(overrides);
        for (int frame = 0; frame <= kFrames; ++frame) { // (the first gathers the light list)
            draw_frame(gpu, targets, meshes, view);
        }
        vk::PathAccumulation image;
        {
            vk::Backend& state = vk::backend();
            const std::scoped_lock lock(state.mutex);
            if (!vk::read_path_accumulation(state, &image) || image.width != kWidth || image.height != kHeight) {
                *ok = false;
                vk::set_path_overrides({});
                return out;
            }
        }
        out.not_finite += image.not_finite;
        means.push_back(region_mean(image.mean, region));
        if (out.pixels.empty()) {
            out.pixels.assign(image.mean.size(), {0.0F, 0.0F, 0.0F});
        }
        for (std::size_t i = 0; i < image.mean.size(); ++i) {
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(core::at(out.pixels, i), ch) += core::at(core::at(image.mean, i), ch) / kBatches;
            }
        }
    }
    vk::set_path_overrides({});
    for (const double m : means) {
        out.mean += m / kBatches;
    }
    double spread = 0.0;
    for (const double m : means) {
        spread += (m - out.mean) * (m - out.mean);
    }
    out.sigma = std::sqrt(spread / (kBatches - 1) / kBatches);
    return out;
}

// ReSTIR's estimate: `batches` runs (each its own seed, its history forgotten) of `frames` frames accumulated after
// `settle` frames (the emitters' list read back, the reservoirs filled). `camera(frame)` the eye a frame is drawn from
// (accumulated only where `counted(frame)`); `lamps_at(frame)` the lamps that frame.
struct RunPlan {
    int batches = 4;
    int frames = 128;
    int settle = 8;
    uint32_t candidates = 0;
    uint32_t spatial = 0xFFU;
    bool temporal = true;
    bool grid = true; // (round PT4c) the initial candidates from the light grid (false: the list by power)
    bool denoiser = true; // (round PT4d) permutation sampling and the boiling filter (restir_denoiser_glsl)
    float roughness = kRoughness; // (round BR) every surface's
    std::function<Vec3(int)> camera;
    std::function<bool(int)> counted;
    std::function<std::vector<rt::Emitter>(int)> lamps_at;
    std::function<void(int, const vk::RestirAccumulation&)> each_frame; // (read back every counted frame when set)
};

Answer restir(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, const RunPlan& plan,
              const std::vector<uint8_t>& region, bool* ok)
{
    Answer out;
    std::vector<double> means;
    for (int batch = 0; batch < plan.batches; ++batch) {
        vk::RestirOverrides overrides;
        overrides.on = true;
        overrides.roughness = plan.roughness;
        overrides.f0 = kF0;
        overrides.albedo_scale = kAlbedo;
        overrides.candidates = plan.candidates;
        overrides.spatial = plan.spatial;
        overrides.temporal = plan.temporal;
        overrides.grid = plan.grid;
        overrides.permutation = plan.denoiser;
        overrides.boiling_filter = plan.denoiser;
        overrides.seed = 977U + (static_cast<uint32_t>(batch) * 104729U);
        overrides.lamps = plan.lamps_at(0);
        overrides.lamps_surfaced.assign(overrides.lamps.size(), 0);
        vk::set_restir_overrides(overrides);
        vk::forget_restir_history();
        vk::set_restir_accumulate(false);
        for (int frame = -plan.settle; frame < plan.frames; ++frame) {
            if (frame == 0) {
                vk::restart_restir_accumulation();
            }
            const int at = std::max(frame, 0);
            overrides.lamps = plan.lamps_at(at);
            vk::set_restir_overrides(overrides);
            const bool counted = frame >= 0 && plan.counted(at);
            vk::set_restir_accumulate(counted);
            draw_frame(gpu, targets, meshes, view_from(plan.camera(at)));
            if (counted && plan.each_frame) {
                vk::RestirAccumulation image;
                vk::Backend& state = vk::backend();
                const std::scoped_lock lock(state.mutex);
                if (!vk::read_restir_accumulation(state, &image)) {
                    *ok = false;
                    break;
                }
                plan.each_frame(at, image);
            }
        }
        vk::RestirAccumulation image;
        {
            vk::Backend& state = vk::backend();
            const std::scoped_lock lock(state.mutex);
            if (!vk::read_restir_accumulation(state, &image) || image.width != kWidth || image.height != kHeight) {
                *ok = false;
                break;
            }
        }
        out.not_finite += image.not_finite;
        means.push_back(region_mean(image.mean, region));
        if (out.pixels.empty()) {
            out.pixels.assign(image.mean.size(), {0.0F, 0.0F, 0.0F});
        }
        for (std::size_t i = 0; i < image.mean.size(); ++i) {
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(core::at(out.pixels, i), ch) += core::at(core::at(image.mean, i), ch) / static_cast<float>(plan.batches);
            }
        }
    }
    vk::set_restir_overrides({});
    vk::set_restir_accumulate(true);
    const auto batches = static_cast<double>(std::max<std::size_t>(means.size(), 1));
    for (const double m : means) {
        out.mean += m / batches;
    }
    double spread = 0.0;
    for (const double m : means) {
        spread += (m - out.mean) * (m - out.mean);
    }
    out.sigma = means.size() > 1 ? std::sqrt(spread / (batches - 1.0) / batches) : 0.0;
    return out;
}

bool agrees(double got, double expected) { return std::abs(got - expected) <= kAgreement * std::abs(expected); }

// --- the lamps ------------------------------------------------------------------------------------------------------------

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

// 32 lamps: one bright (20) at `bright`, the rest dim and far round it, intensities over four decades.
std::vector<rt::Emitter> many_lamps(const Vec3& bright)
{
    std::vector<rt::Emitter> out{lamp_at(bright, 20.0F, 30.0F)};
    for (int i = 1; i < 32; ++i) {
        const double angle = 2.0 * kPi * i / 32.0;
        const double distance = 900.0 + (60.0 * i);
        out.push_back(lamp_at({distance * std::cos(angle), 200.0, distance * std::sin(angle)},
                              static_cast<float>(std::pow(10.0, -3.0 + (3.0 * (i % 8) / 7.0))), 12.0F));
    }
    return out;
}

// The exact direct light of `lamps` (nothing in their way) on the floor at the region's pixels from `eye`: the lamp model
// (rt_lights.h lamp_geometry) on the light model (rt_brdf.h direct) - the mean luma.
double exact_floor_light(const Vec3& eye, const std::vector<rt::Emitter>& lamps, const std::vector<uint8_t>& region)
{
    brdf::Material m;
    m.albedo = {kAlbedo, kAlbedo, kAlbedo};
    m.f0 = {kF0, kF0, kF0};
    m.roughness = kRoughness;
    double sum = 0.0;
    std::size_t count = 0;
    for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            if (core::at(region, (static_cast<std::size_t>(y) * kWidth) + x) == 0) {
                continue;
            }
            const Vec3 d = pixel_ray(eye, x + 0.5, y + 0.5);
            const Vec3 p = add(eye, mul(d, -y_of(eye) / y_of(d)));
            const Vec3 v = unit(sub(eye, p));
            const std::array<float, 3> at{static_cast<float>(x_of(p)), 0.0F, static_cast<float>(z_of(p))};
            Rgb light{};
            for (const rt::Emitter& lamp : lamps) {
                const rt::LampShape shape{.position = lamp.position,
                                          .shape = lamp.shape,
                                          .axis = lamp.axis,
                                          .half_length = lamp.half_length,
                                          .core = lamp.core,
                                          .reach = lamp.reach};
                std::array<float, 3> toward{};
                const float g = rt::lamp_geometry(shape, at, {0.0F, 1.0F, 0.0F}, &toward);
                const Vec3 to{core::at(toward, 0) - x_of(p), core::at(toward, 1) - y_of(p), core::at(toward, 2) - z_of(p)};
                const Vec3 l = unit(to);
                if (!(g > 0.0F) || !(y_of(l) > 1.0e-4)) {
                    continue;
                }
                const float per = lamp.intensity * g / static_cast<float>(y_of(l));
                const brdf::Rgb f = brdf::direct(m, {0.0F, 1.0F, 0.0F},
                                                 {static_cast<float>(x_of(v)), static_cast<float>(y_of(v)), static_cast<float>(z_of(v))},
                                                 {static_cast<float>(x_of(l)), static_cast<float>(y_of(l)), static_cast<float>(z_of(l))},
                                                 {per * core::at(lamp.colour, 0), per * core::at(lamp.colour, 1), per * core::at(lamp.colour, 2)});
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    core::at(light, ch) += core::at(f, ch);
                }
            }
            sum += luma(light);
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

// (round PT4c) The lamps' light a white diffuser on the floor gets at pixel `i` from `eye` (nothing in the way): what the
// game's inputs carry - the lamp model alone (the luma; the lamps are white).
double exact_floor_lamps(const Vec3& eye, const std::vector<rt::Emitter>& lamps, std::size_t i)
{
    const Vec3 d = pixel_ray(eye, static_cast<double>(i % kWidth) + 0.5, static_cast<double>(i / kWidth) + 0.5);
    const Vec3 p = add(eye, mul(d, -y_of(eye) / y_of(d)));
    const std::array<float, 3> at{static_cast<float>(x_of(p)), 0.0F, static_cast<float>(z_of(p))};
    double light = 0.0;
    for (const rt::Emitter& lamp : lamps) {
        const rt::LampShape shape{.position = lamp.position,
                                  .shape = lamp.shape,
                                  .axis = lamp.axis,
                                  .half_length = lamp.half_length,
                                  .core = lamp.core,
                                  .reach = lamp.reach};
        std::array<float, 3> toward{};
        const float g = rt::lamp_geometry(shape, at, {0.0F, 1.0F, 0.0F}, &toward);
        if (g > 0.0F) {
            light += static_cast<double>(lamp.intensity * g) *
                     luma({core::at(lamp.colour, 0), core::at(lamp.colour, 1), core::at(lamp.colour, 2)});
        }
    }
    return light;
}

// --- the checks --------------------------------------------------------------------------------------------------------------

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
    const std::span<const SceneMesh> floor_only(meshes.data(), 1);
    bool read = true;
    uint32_t not_finite = 0;

    // the still scene's lamps: a few of very different power, round the disc, some behind the wall
    const std::vector<rt::Emitter> still_lamps{lamp_at({300.0, 180.0, -200.0}, 6.0F, 20.0F),
                                               lamp_at({-200.0, 120.0, 400.0}, 12.0F, 25.0F),
                                               lamp_at({150.0, 60.0, 360.0}, 3.0F, 10.0F),
                                               lamp_at({-500.0, 250.0, -300.0}, 0.05F, 10.0F),
                                               lamp_at({900.0, 200.0, 500.0}, 0.5F, 15.0F)};
    const auto still = [&still_lamps](int /*frame*/) { return still_lamps; };

    // 1. the still scene
    text::print("still scene (a floor, the emitting disc, a wall, 5 lamps), %ux%u:\n", kWidth, kHeight);
    const std::vector<uint8_t> region_a = region_of(kEyeA);
    const Answer ref_a = reference(gpu, targets, everything, kEyeA, still_lamps, &read);
    not_finite += ref_a.not_finite;
    RunPlan plan;
    plan.camera = [](int /*frame*/) { return kEyeA; };
    plan.counted = [](int /*frame*/) { return true; };
    plan.lamps_at = still;
    const Answer full = restir(gpu, targets, everything, plan, region_a, &read);
    not_finite += full.not_finite;
    const vk::RestirStats stats = vk::restir_stats();
    tally->check(read && stats.list_ready && stats.listed == static_cast<uint32_t>(kDiscSides),
                 text::format("the emitting triangles listed by the shared scan: %u (the disc's %d); %u lamps", stats.listed,
                              kDiscSides, stats.lamps));
    tally->check(read && agrees(full.mean, ref_a.mean),
                 text::format("still: ReSTIR (temporal + spatial) %.5f +- %.5f, the reference %.5f +- %.5f (%+.2f %%)",
                              full.mean, full.sigma, ref_a.mean, ref_a.sigma,
                              100.0 * (full.mean - ref_a.mean) / std::max(ref_a.mean, 1e-12)));
    plan.spatial = 0;
    plan.temporal = false;
    const Answer alone = restir(gpu, targets, everything, plan, region_a, &read);
    not_finite += alone.not_finite;
    tally->check(read && agrees(alone.mean, ref_a.mean),
                 text::format("still: the initial candidates alone (no reuse) %.5f +- %.5f (%+.2f %%)", alone.mean,
                              alone.sigma, 100.0 * (alone.mean - ref_a.mean) / std::max(ref_a.mean, 1e-12)));
    plan.spatial = 0xFFU;
    plan.temporal = true;
    // (round BR) a glossy floor: the lights' specular is ReSTIR DI's (sampled on the lights, written over E into the
    // specular input - here taken back as the shading does, E x it, with the diffuse light) - the reference's direct light
    {
        constexpr float kGlossy = 0.2F;
        const Answer ref_g = reference(gpu, targets, everything, kEyeA, still_lamps, &read, kGlossy);
        not_finite += ref_g.not_finite;
        plan.roughness = kGlossy;
        const Answer glossy = restir(gpu, targets, everything, plan, region_a, &read);
        not_finite += glossy.not_finite;
        plan.roughness = kRoughness;
        tally->check(read && agrees(glossy.mean, ref_g.mean),
                     text::format("glossy floor (roughness %.2f): ReSTIR diffuse + E x its specular input %.5f +- %.5f, the "
                                  "reference's direct light %.5f +- %.5f (%+.2f %%; the reference %+.1f %% against roughness %.2f)",
                                  static_cast<double>(kGlossy), glossy.mean, glossy.sigma, ref_g.mean, ref_g.sigma,
                                  100.0 * (glossy.mean - ref_g.mean) / std::max(ref_g.mean, 1e-12),
                                  100.0 * (ref_g.mean - ref_a.mean) / std::max(ref_a.mean, 1e-12),
                                  static_cast<double>(kRoughness)));
    }
    // the shadowed part on its own (the wall's shadow: the reuse across a shadow's edge stays unbiased)
    {
        std::vector<uint8_t> shade(region_a.size(), 0);
        std::size_t count = 0;
        for (std::size_t i = 0; i < region_a.size(); ++i) {
            const std::array<float, 3>& r = core::at(ref_a.pixels, i);
            // pixels darker than half the region's mean: in the wall's shadow or far from the lights
            if (core::at(region_a, i) != 0 && luma({core::at(r, 0), core::at(r, 1), core::at(r, 2)}) < 0.5 * ref_a.mean) {
                core::at(shade, i) = 1;
                ++count;
            }
        }
        const double got = region_mean(full.pixels, shade);
        const double expected = region_mean(ref_a.pixels, shade);
        tally->check(read && count > 100 && std::abs(got - expected) <= 0.05 * expected,
                     text::format("still, the %zu dim pixels (shadow edges, the far floor): ReSTIR %.5f, the reference "
                                  "%.5f (%+.2f %%; within 5 %% - fewer pixels, more noise)",
                                  count, got, expected, 100.0 * (got - expected) / std::max(expected, 1e-12)));
    }

    // 2. disocclusion: the camera between two views every frame
    text::print("disocclusion (the camera swapped between two views every frame, the wall hiding other floor from each):\n");
    RunPlan swap = plan;
    swap.frames = 256;
    swap.camera = [](int frame) { return frame % 2 == 0 ? kEyeA : kEyeB; };
    swap.counted = [](int frame) { return frame % 2 == 0; };
    const Answer swapped = restir(gpu, targets, everything, swap, region_a, &read);
    not_finite += swapped.not_finite;
    tally->check(read && agrees(swapped.mean, ref_a.mean),
                 text::format("swapped views: ReSTIR in the first view %.5f +- %.5f, the reference %.5f (%+.2f %%)",
                              swapped.mean, swapped.sigma, ref_a.mean,
                              100.0 * (swapped.mean - ref_a.mean) / std::max(ref_a.mean, 1e-12)));

    // 3. a moving light among many of very different power
    text::print("a moving light (the brightest of 32 lamps circling, the floor alone):\n");
    const auto bright_at = [](int frame) {
        const double angle = 0.05 * frame;
        return Vec3{300.0 * std::cos(angle), 260.0, 300.0 * std::sin(angle)};
    };
    const std::vector<uint8_t> floor_region = rows_region(); // (no disc in this scene: every row from kFirstRow)
    RunPlan moving = plan;
    moving.batches = 2;
    moving.frames = 256;
    moving.lamps_at = [&bright_at](int frame) { return many_lamps(bright_at(frame)); };
    double exact = 0.0;
    for (int frame = 0; frame < moving.frames; ++frame) {
        exact += exact_floor_light(kEyeA, many_lamps(bright_at(frame)), floor_region) / moving.frames;
    }
    const Answer moved = restir(gpu, targets, floor_only, moving, floor_region, &read);
    not_finite += moved.not_finite;
    tally->check(read && agrees(moved.mean, exact),
                 text::format("moving: ReSTIR over the frames %.5f +- %.5f, the exact light's mean %.5f (%+.2f %%)",
                              moved.mean, moved.sigma, exact, 100.0 * (moved.mean - exact) / std::max(exact, 1e-12)));

    // 4. many lights of very different power: the per-frame error with the reuse and with the light list alone
    text::print("32 lamps over four decades, standing still: the per-frame error against the exact light:\n");
    const std::vector<rt::Emitter> fixed = many_lamps(bright_at(0));
    // the exact light per pixel (the floor alone - nothing in the way)
    std::vector<double> truth(static_cast<std::size_t>(kWidth) * kHeight, 0.0);
    for (std::size_t i = 0; i < truth.size(); ++i) {
        if (core::at(floor_region, i) == 0) {
            continue;
        }
        std::vector<uint8_t> one(floor_region.size(), 0);
        core::at(one, i) = 1;
        core::at(truth, i) = exact_floor_light(kEyeA, fixed, one);
    }
    const auto frame_error = [&](const RunPlan& base, double* mean_out) {
        RunPlan p = base;
        p.batches = 1;
        p.frames = 48;
        p.settle = 16;
        p.lamps_at = [&fixed](int /*frame*/) { return fixed; };
        double error = 0.0;
        int frames = 0;
        p.each_frame = [&](int /*frame*/, const vk::RestirAccumulation& image) {
            double sum = 0.0;
            std::size_t count = 0;
            for (std::size_t i = 0; i < image.last.size() && i < truth.size(); ++i) {
                if (core::at(floor_region, i) == 0) {
                    continue;
                }
                const std::array<float, 3>& c = core::at(image.last, i);
                const double d = luma({core::at(c, 0), core::at(c, 1), core::at(c, 2)}) - core::at(truth, i);
                sum += d * d;
                ++count;
            }
            error += count > 0 ? sum / static_cast<double>(count) : 0.0;
            ++frames;
        };
        const Answer got = restir(gpu, targets, floor_only, p, floor_region, &read);
        not_finite += got.not_finite;
        *mean_out = got.mean;
        return frames > 0 ? error / frames : 0.0;
    };
    double truth_mean = 0.0;
    std::size_t truth_count = 0;
    for (std::size_t i = 0; i < truth.size(); ++i) {
        if (core::at(floor_region, i) != 0) {
            truth_mean += core::at(truth, i);
            ++truth_count;
        }
    }
    truth_mean /= static_cast<double>(std::max<std::size_t>(truth_count, 1));
    RunPlan list_alone = plan;
    list_alone.candidates = 1;
    list_alone.spatial = 0;
    list_alone.temporal = false;
    double mean_reuse = 0.0;
    double mean_alone = 0.0;
    const double mse_reuse = frame_error(plan, &mean_reuse);
    const double mse_alone = frame_error(list_alone, &mean_alone);
    tally->check(read && mse_reuse > 0.0 && mse_reuse * 4.0 < mse_alone,
                 text::format("per-frame mean squared error: ReSTIR %.4g, the light list alone (one light a pixel, no reuse) "
                              "%.4g - x %.3g (relative RMS %.3f / %.3f of the mean %.5f)",
                              mse_reuse, mse_alone, mse_alone > 0.0 ? mse_reuse / mse_alone : 0.0,
                              std::sqrt(mse_reuse) / std::max(truth_mean, 1e-12),
                              std::sqrt(mse_alone) / std::max(truth_mean, 1e-12), truth_mean));
    tally->check(read && agrees(mean_reuse, truth_mean) && agrees(mean_alone, truth_mean),
                 text::format("32 lamps still: ReSTIR %.5f, the light list alone %.5f, exact %.5f", mean_reuse,
                              mean_alone, truth_mean));
    // 5. (round PT4b) a lamp drawn in a texture: the disc's emission on 1 in 64 texels (the store's lamp-texel flag), no
    // lamps - its triangles' light is the texels', not their mean: the target the exact emission (a sample's weight what
    // it brings), every triangle listed though the scan's points miss its few lamp texels (none lost)
    text::print("a lamp drawn in a texture (the disc emitting on 1 in %u texels), no lamps:\n",
                kLampTexelEvery * kLampTexelEvery);
    {
        render::GpuTexture* const texture = make_lamp_texture(gpu);
        std::array<SceneMesh, 3> textured{};
        const bool built = texture != nullptr && make_mesh(gpu, floor_triangles(), false, &textured.at(0)) &&
                           make_mesh(gpu, disc_triangles(), true, &textured.at(1), texture) &&
                           make_mesh(gpu, wall_triangles(), false, &textured.at(2));
        tally->check(built, "the textured lamp's scene made");
        if (built) {
            const std::span<const SceneMesh> scene(textured);
            const std::vector<rt::Emitter> none;
            const Answer ref_t = reference(gpu, targets, scene, kEyeA, none, &read);
            not_finite += ref_t.not_finite;
            RunPlan lamp_plan = plan;
            lamp_plan.lamps_at = [&none](int /*frame*/) { return none; };
            const Answer got = restir(gpu, targets, scene, lamp_plan, region_a, &read);
            not_finite += got.not_finite;
            const vk::RestirStats listed = vk::restir_stats();
            tally->check(read && listed.listed == static_cast<uint32_t>(kDiscSides),
                         text::format("every emitting triangle listed (%u of the disc's %d; %u emitting at a corner) - "
                                      "none lost for want of a lamp texel at the scan's points",
                                      listed.listed, kDiscSides, listed.candidates));
            tally->check(read && agrees(got.mean, ref_t.mean),
                         text::format("textured lamp: ReSTIR %.5f +- %.5f, the reference %.5f +- %.5f (%+.2f %%)", got.mean,
                                      got.sigma, ref_t.mean, ref_t.sigma,
                                      100.0 * (got.mean - ref_t.mean) / std::max(ref_t.mean, 1e-12)));
            // the per-frame error against the reference's pixels: a sample's weight what it brings - no reservoir
            // keeping a point between the lamp texels (black) or one on them at the triangle's mean's weight (white)
            RunPlan p = lamp_plan;
            p.batches = 1;
            p.frames = 48;
            p.settle = 16;
            double error = 0.0;
            double lamp_sum = 0.0;
            std::size_t lamp_count = 0;
            for (std::size_t i = 0; i < region_a.size(); ++i) {
                if (core::at(region_a, i) != 0) {
                    const std::array<float, 3>& c = core::at(ref_t.pixels, i);
                    lamp_sum += luma({core::at(c, 0), core::at(c, 1), core::at(c, 2)});
                    ++lamp_count;
                }
            }
            const double lamp_mean = lamp_sum / static_cast<double>(std::max<std::size_t>(lamp_count, 1));
            int frames = 0;
            p.each_frame = [&](int /*frame*/, const vk::RestirAccumulation& image) {
                double sum = 0.0;
                std::size_t count = 0;
                for (std::size_t i = 0; i < image.last.size() && i < region_a.size(); ++i) {
                    if (core::at(region_a, i) == 0) {
                        continue;
                    }
                    const std::array<float, 3>& c = core::at(image.last, i);
                    const std::array<float, 3>& r = core::at(ref_t.pixels, i);
                    const double d = luma({core::at(c, 0), core::at(c, 1), core::at(c, 2)}) -
                                     luma({core::at(r, 0), core::at(r, 1), core::at(r, 2)});
                    sum += d * d;
                    ++count;
                }
                error += count > 0 ? sum / static_cast<double>(count) : 0.0;
                ++frames;
            };
            const Answer per_frame = restir(gpu, targets, scene, p, region_a, &read);
            not_finite += per_frame.not_finite;
            const double rms = frames > 0 ? std::sqrt(error / frames) / std::max(lamp_mean, 1e-12) : 0.0;
            tally->check(read && frames > 0 && rms < kTexturedLampRms,
                         text::format("textured lamp: per-frame relative RMS error %.3f of the mean %.5f (under %.2f)", rms,
                                      lamp_mean, kTexturedLampRms));
        }
        for (SceneMesh& mesh : textured) {
            release_mesh(gpu, &mesh);
        }
        if (texture != nullptr) {
            gpu.release_texture(texture);
        }
    }

    // 6. (round PT4c) a still scene lit by a few dim lamps near the floor among many bright ones far away (a level at
    // night: the courtyard's lamps among the whole level's): the frame-to-frame variation of each pixel (what a still
    // picture boils with) with the light grid and with the list by power, and both means against the exact light
    text::print("4 dim lamps near among 120 bright far away, standing still: each pixel's variation over the frames:\n");
    {
        std::vector<rt::Emitter> field;
        for (int i = 0; i < 4; ++i) {
            const double angle = (2.0 * kPi * i / 4.0) + 0.4;
            field.push_back(lamp_at({250.0 * std::cos(angle), 120.0, 250.0 * std::sin(angle)}, 1.0F, 20.0F));
        }
        for (int i = 0; i < 120; ++i) {
            const double angle = 2.0 * kPi * i / 120.0;
            const double distance = 2500.0 + (27.0 * i);
            field.push_back(lamp_at({distance * std::cos(angle), 300.0, distance * std::sin(angle)}, 5.0F, 30.0F));
        }
        std::vector<double> exact_px(static_cast<std::size_t>(kWidth) * kHeight, 0.0);
        double exact_mean = 0.0;
        std::size_t exact_count = 0;
        for (std::size_t i = 0; i < exact_px.size(); ++i) {
            if (core::at(floor_region, i) == 0) {
                continue;
            }
            std::vector<uint8_t> one(floor_region.size(), 0);
            core::at(one, i) = 1;
            core::at(exact_px, i) = exact_floor_lamps(kEyeA, field, i);
            exact_mean += core::at(exact_px, i);
            ++exact_count;
        }
        exact_mean /= static_cast<double>(std::max<std::size_t>(exact_count, 1));
        struct Variation {
            double temporal = 0.0; // each pixel's standard deviation over the frames (RMS over the pixels) / the mean
            double error = 0.0;    // each frame's RMS error against the exact light / the mean
            double mean = 0.0;
            // (round PT4d) what a temporal denoiser makes of it: each pixel's running average over the frames (history
            // weight 1 - kDenoiserBlend - a temporal accumulator's), its standard deviation over the frames after it has
            // settled (RMS over the pixels) / the mean; and the error's correlation from one frame to the next (each
            // pixel's deviation from its mean, summed over the pixels) - 0 for noise the accumulator averages, 1 for an
            // error it keeps as picture
            double denoised = 0.0;
            double correlation = 0.0;
        };
        constexpr int kVariationFrames = 160;
        constexpr int kDenoiserSettle = 32;
        constexpr double kDenoiserBlend = 0.1;
        const auto variation = [&](bool grid) {
            RunPlan p = plan;
            p.grid = grid;
            p.batches = 1;
            p.frames = kVariationFrames;
            p.settle = 32;
            p.lamps_at = [&field](int /*frame*/) { return field; };
            std::vector<std::size_t> floor_pixels;
            for (std::size_t i = 0; i < exact_px.size(); ++i) {
                if (core::at(floor_region, i) != 0) {
                    floor_pixels.push_back(i);
                }
            }
            std::vector<std::vector<double>> series; // per frame, per floor pixel
            p.each_frame = [&](int /*frame*/, const vk::RestirAccumulation& image) {
                std::vector<double> values(floor_pixels.size(), 0.0);
                for (std::size_t k = 0; k < floor_pixels.size(); ++k) {
                    const std::size_t i = core::at(floor_pixels, k);
                    core::at(values, k) = i < image.last_light.size() ? core::at(image.last_light, i) : 0.0;
                }
                series.push_back(std::move(values));
            };
            const Answer got = restir(gpu, targets, floor_only, p, floor_region, &read);
            not_finite += got.not_finite;
            Variation out;
            const auto frames = static_cast<double>(series.size());
            const auto pixels = static_cast<double>(std::max<std::size_t>(floor_pixels.size(), 1));
            const double scale = std::max(exact_mean, 1e-12);
            double variance = 0.0;
            double error = 0.0;
            double denoised_variance = 0.0;
            double lagged = 0.0;
            double squared = 0.0;
            for (std::size_t k = 0; k < floor_pixels.size() && series.size() > static_cast<std::size_t>(kDenoiserSettle) + 1;
                 ++k) {
                const double exact = core::at(exact_px, core::at(floor_pixels, k));
                double sum = 0.0;
                for (const std::vector<double>& frame : series) {
                    sum += core::at(frame, k);
                    error += (core::at(frame, k) - exact) * (core::at(frame, k) - exact);
                }
                const double m = sum / frames;
                out.mean += m / pixels;
                double average = core::at(series.front(), k);
                double before = 0.0;
                double ema_sum = 0.0;
                double ema_squares = 0.0;
                int ema_frames = 0;
                for (std::size_t f = 0; f < series.size(); ++f) {
                    const double v = core::at(core::at(series, f), k);
                    variance += (v - m) * (v - m) / frames;
                    squared += (v - m) * (v - m);
                    if (f > 0) {
                        lagged += (v - m) * before;
                    }
                    before = v - m;
                    average += kDenoiserBlend * (v - average);
                    if (f >= static_cast<std::size_t>(kDenoiserSettle)) {
                        ema_sum += average;
                        ema_squares += average * average;
                        ++ema_frames;
                    }
                }
                const double ema_mean = ema_sum / std::max(ema_frames, 1);
                denoised_variance += std::max(0.0, (ema_squares / std::max(ema_frames, 1)) - (ema_mean * ema_mean));
            }
            out.temporal = std::sqrt(variance / pixels) / scale;
            out.error = frames > 0.0 ? std::sqrt(error / (frames * pixels)) / scale : 0.0;
            out.denoised = std::sqrt(denoised_variance / pixels) / scale;
            out.correlation = squared > 0.0 ? lagged / squared : 0.0;
            return out;
        };
        const Variation with_grid = variation(true);
        const Variation by_power = variation(false);
        const bool kept_temporal = plan.temporal;
        const uint32_t kept_spatial = plan.spatial;
        const bool kept_denoiser = plan.denoiser;
        plan.denoiser = false;
        const Variation correlated = variation(true);
        plan.temporal = false;
        plan.spatial = 0;
        const Variation grid_alone = variation(true);
        const Variation power_alone = variation(false);
        plan.temporal = true;
        const Variation grid_temporal = variation(true);
        const Variation power_temporal = variation(false);
        plan.temporal = kept_temporal;
        plan.spatial = kept_spatial;
        plan.denoiser = kept_denoiser;
        const auto ratio = [](const Variation& a, const Variation& b) { return b.temporal > 0.0 ? a.temporal / b.temporal : 0.0; };
        tally->check(read && grid_alone.temporal > 0.0 && grid_alone.temporal * 2.0 < power_alone.temporal,
                     text::format("variation over the frames (each pixel's standard deviation / the mean), the initial "
                                  "candidates alone: light grid %.3f, the list by power %.3f (x %.2f)",
                                  grid_alone.temporal, power_alone.temporal, ratio(grid_alone, power_alone)));
        tally->check(read && grid_temporal.temporal > 0.0 && grid_temporal.temporal * 2.0 < power_temporal.temporal,
                     text::format("... with the temporal reuse: light grid %.3f, the list by power %.3f (x %.2f)",
                                  grid_temporal.temporal, power_temporal.temporal, ratio(grid_temporal, power_temporal)));
        tally->check(read && with_grid.temporal > 0.0 && with_grid.temporal < by_power.temporal,
                     text::format("... with the temporal and spatial reuse: light grid %.3f, the list by power %.3f (x %.2f); "
                                  "per-frame RMS error %.3f / %.3f",
                                  with_grid.temporal, by_power.temporal, ratio(with_grid, by_power), with_grid.error,
                                  by_power.error));
        tally->check(read && agrees(with_grid.mean, exact_mean) && agrees(by_power.mean, exact_mean),
                     text::format("near and far lamps still: light grid %.6f (%+.2f %%), the list by power %.6f (%+.2f %%), "
                                  "exact %.6f",
                                  with_grid.mean, 100.0 * (with_grid.mean - exact_mean) / std::max(exact_mean, 1e-12),
                                  by_power.mean, 100.0 * (by_power.mean - exact_mean) / std::max(exact_mean, 1e-12),
                                  exact_mean));
        // 7. (round PT4d) the same as a temporal denoiser gets it: with permutation sampling and the boiling filter
        // (RTXDI's settings for a denoised output) against without - the error's frame-to-frame correlation and what a
        // running average (a denoiser's history) still varies by; the mean within 2 % of the exact light (the filter's
        // bias)
        text::print("the same through a temporal denoiser (running average, history weight %.2f):\n", 1.0 - kDenoiserBlend);
        tally->check(read && with_grid.correlation < correlated.correlation,
                     text::format("the error's frame-to-frame correlation: %.3f with permutation sampling + the boiling "
                                  "filter, %.3f without (temporal + spatial reuse, light grid)",
                                  with_grid.correlation, correlated.correlation));
        tally->check(read && with_grid.denoised > 0.0 && with_grid.denoised < correlated.denoised,
                     text::format("variation over the frames left after the running average: %.4f, without %.4f (x %.2f); "
                                  "raw %.3f / %.3f",
                                  with_grid.denoised, correlated.denoised,
                                  correlated.denoised > 0.0 ? with_grid.denoised / correlated.denoised : 0.0,
                                  with_grid.temporal, correlated.temporal));
        tally->check(read && agrees(with_grid.mean, exact_mean),
                     text::format("... the mean %.6f (%+.2f %%; without %+.2f %%), exact %.6f", with_grid.mean,
                                  100.0 * (with_grid.mean - exact_mean) / std::max(exact_mean, 1e-12),
                                  100.0 * (correlated.mean - exact_mean) / std::max(exact_mean, 1e-12), exact_mean));
    }

    tally->check(read && not_finite == 0, text::format("estimates not finite: %u", not_finite));
    tally->check(read, "every run read back");
    const vk::RestirStats after = vk::restir_stats();
    text::print("  the passes' device time: %.3f ms at %ux%u (the mean of the last %s)\n", after.gpu_ms, after.gpu_width,
                after.gpu_height, "64 passes");
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
    vk::set_restir_overrides({});
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
    text::print("thug2_vulkan_restir_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan ReSTIR check", kWindowWidth, kWindowHeight,
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
