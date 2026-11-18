// NOT ORIGINAL - thug2_vulkan_path_check.exe: the reference path tracer (gpu_vulkan/vulkan_path_reference.h, the console's
// `pt ref`; path tracing plan, stage 1) and its sampling (engine/gfx/rt_path.h) tried on this machine against answers
// worked out on the processor. Everything goes through the installed render::rt::RayTracing (the acceleration
// structures, the geometry table with a light store, the material table) as the game's frame record feeds it.
//
// On the processor (rt_path.h's twin):
//   - the BSDF's pdf integrates to 1 over the sphere (every roughness, two view angles), and its samples' mean of
//     f cos / pdf is the lobe's integral by quadrature (the sampler draws what the pdf says);
//   - one owner per lamp: analytic_lamps keeps exactly the lamps without an emitting surface.
// On the device, a large white floor (a world mesh without a light store), seen from an angle, and sometimes a disc of
// 16 triangles above it facing down, emitting (its light store: radiance Le, no tint - it reflects nothing diffusely):
//   - the white furnace: the floor (albedo 1) under an even sky of 1, no disc: every pixel's mean is 1 - the light model's
//     directional albedo with its energy compensation - within the noise and the DFG table's error, at roughness
//     0.05 - 1, from two heights; and the processor's quadrature of the same lobe at those pixels within 3 sigma;
//   - the floor under the disc, direct light only (one bounce), no sky: the pixels round the view's centre against the
//     lobe integrated over the disc's area on the processor (a Lambertian's closed form reported beside it);
//   - floor, disc and a dim sky, every bounce: the light strategies' means - MIS, the lights alone, the BSDF alone -
//     within 3 sigma of each other (the disc's light counted once: by NEE or by a hit, never both), and the processor's
//     twin path tracer (the same sampling, the same sequence of numbers, its own ray casts) within 3 sigma of the device;
//   - the disc's lamp also given as an analytic lamp with its surface: the same mean (the owner rule leaves it out);
//     without its surface marked: brighter (the lamp counted twice - what the rule prevents);
//   - no sample was NaN or infinite; the validation layer silent.
//   thug2_vulkan_path_check [--device N] [--display N] [--no-validation]
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
constexpr uint32_t kFvf = 0x142;          // XYZ | DIFFUSE | TEX1
constexpr uint32_t kStride = 24;          // the position, the colour at 12, the uv at 16
constexpr uint32_t kColourAt = 12;
constexpr uint32_t kUvAt = 16;
constexpr uint32_t kGrey = 0x80808080U;
constexpr float kNearZ = 2.0F;
constexpr float kFarZ = 20000.0F;
constexpr double kHalfFovTan = 0.57735026918962576; // tan 30 degrees
constexpr float kFloorHalf = 6000.0F;
constexpr float kDiscHeight = 300.0F;
constexpr float kDiscRadius = 120.0F;
constexpr int kDiscSides = 16;
constexpr float kDiscRadiance = 4.0F;
constexpr uint32_t kWorldOpaque = rt::kWorld | rt::kOpaque | rt::kCastsShadow;
constexpr int kBatches = 8;
constexpr int kRegion = 4;                 // the pixels compared: a kRegion x kRegion block round the view's centre
constexpr double kSigmas = 3.0;
constexpr double kPi = 3.14159265358979323846;

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
brdf::Vec3 fv(const Vec3& v)
{
    return {static_cast<float>(x_of(v)), static_cast<float>(y_of(v)), static_cast<float>(z_of(v))};
}
Vec3 dv(const brdf::Vec3& v) { return {v.x, v.y, v.z}; }
double luma(const Rgb& c) { return (0.2126 * core::at(c, 0)) + (0.7152 * core::at(c, 1)) + (0.0722 * core::at(c, 2)); }

// --- the scene ----------------------------------------------------------------------------------------------------------

struct Triangle {
    std::array<Vec3, 3> corners{};
    bool emits = false;
};

std::vector<Triangle> floor_triangles()
{
    const double h = kFloorHalf;
    return {{.corners = {{{-h, 0.0, -h}, {h, 0.0, -h}, {h, 0.0, h}}}, .emits = false},
            {.corners = {{{-h, 0.0, -h}, {h, 0.0, h}, {-h, 0.0, h}}}, .emits = false}};
}

std::vector<Triangle> disc_triangles()
{
    std::vector<Triangle> out;
    for (int i = 0; i < kDiscSides; ++i) {
        const double a0 = 2.0 * kPi * i / kDiscSides;
        const double a1 = 2.0 * kPi * (i + 1) / kDiscSides;
        out.push_back({.corners = {{{0.0, kDiscHeight, 0.0},
                                    {kDiscRadius * std::cos(a1), kDiscHeight, kDiscRadius * std::sin(a1)},
                                    {kDiscRadius * std::cos(a0), kDiscHeight, kDiscRadius * std::sin(a0)}}},
                       .emits = true});
    }
    return out;
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

// A mesh of `triangles` (white, untextured) registered; `emitting`: with a light store whose every vertex emits
// kDiscRadiance and has no tint.
bool make_mesh(render::Gpu& gpu, const std::vector<Triangle>& triangles, bool emitting, SceneMesh* mesh)
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
            put_float(bytes, base + kUvAt, 0.0F);
            put_float(bytes, base + kUvAt + sizeof(float), 0.0F);
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
    rt::ray_tracing()->set_material(mesh->material, surface);
    if (emitting) {
        rt::CelWords words = rt::kCelNoStream;
        core::at(words, rt::kCelFlagsWord) = rt::kCelHasStream; // (a vertex with the store: no tint, its emission)
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

rt::ViewDrawn view_from(const Vec3& eye)
{
    Matrix view{};
    Matrix projection{};
    const std::array<float, 3> e{static_cast<float>(x_of(eye)), static_cast<float>(y_of(eye)), static_cast<float>(z_of(eye))};
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

// The ray through (x + jx, y + jy) of the view from `eye` toward kAt (the pass's: a pinhole, y down).
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
    rt::world_view_drawn(view);
    gpu.end_scene();
    gpu.present();
}

struct Measured {
    double mean = 0.0;  // the region's mean luma over the batches
    double sigma = 0.0; // the mean's standard error
    uint32_t not_finite = 0;
    uint32_t emitters = 0;
    std::vector<double> batches;
};

double region_mean(const vk::PathAccumulation& image)
{
    double sum = 0.0;
    const int x0 = static_cast<int>(kWidth / 2) - (kRegion / 2);
    const int y0 = static_cast<int>(kHeight / 2) - (kRegion / 2);
    for (int y = y0; y < y0 + kRegion; ++y) {
        for (int x = x0; x < x0 + kRegion; ++x) {
            const std::array<float, 3>& c = core::at(image.mean, (static_cast<std::size_t>(y) * kWidth) + static_cast<std::size_t>(x));
            sum += luma({core::at(c, 0), core::at(c, 1), core::at(c, 2)});
        }
    }
    return sum / (kRegion * kRegion);
}

// kBatches runs of `frames` x overrides.samples_per_frame paths a pixel, each with its own seed.
Measured run_batches(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, const Vec3& eye,
                     vk::PathOverrides overrides, int frames, uint32_t seed_base, bool* ok)
{
    Measured out;
    const rt::ViewDrawn view = view_from(eye);
    for (int batch = 0; batch < kBatches; ++batch) {
        overrides.on = true;
        overrides.seed = seed_base + static_cast<uint32_t>(batch) * 7919U;
        vk::set_path_overrides(overrides);
        for (int frame = 0; frame < frames; ++frame) {
            draw_frame(gpu, targets, meshes, view);
        }
        vk::PathAccumulation image;
        {
            vk::Backend& state = vk::backend();
            const std::scoped_lock lock(state.mutex);
            if (!vk::read_path_accumulation(state, &image) || image.width != kWidth || image.height != kHeight) {
                *ok = false;
                return out;
            }
        }
        out.not_finite += image.not_finite;
        out.emitters = image.emitters;
        out.batches.push_back(region_mean(image));
    }
    double sum = 0.0;
    for (const double value : out.batches) {
        sum += value;
    }
    out.mean = sum / kBatches;
    double spread = 0.0;
    for (const double value : out.batches) {
        spread += (value - out.mean) * (value - out.mean);
    }
    out.sigma = std::sqrt(spread / (kBatches - 1) / kBatches);
    vk::set_path_overrides({});
    return out;
}

bool within(double a, double sigma_a, double b, double sigma_b)
{
    return std::abs(a - b) <= (kSigmas * std::sqrt((sigma_a * sigma_a) + (sigma_b * sigma_b))) + (1e-6 * std::abs(b));
}

// --- the processor's answers ------------------------------------------------------------------------------------------------

brdf::Material forced(double albedo, double f0, double roughness)
{
    brdf::Material m;
    m.albedo = {static_cast<float>(albedo), static_cast<float>(albedo), static_cast<float>(albedo)};
    m.f0 = {static_cast<float>(f0), static_cast<float>(f0), static_cast<float>(f0)};
    m.roughness = static_cast<float>(roughness);
    return m;
}

// The lobe's f cos integrated over the upper hemisphere about +y, seen from `v` (luma), on a grid.
double hemisphere_integral(const brdf::Material& m, const Vec3& v)
{
    constexpr int kTheta = 256;
    constexpr int kPhi = 256;
    double sum = 0.0;
    for (int i = 0; i < kTheta; ++i) {
        const double theta = (i + 0.5) * (kPi / 2.0) / kTheta;
        for (int j = 0; j < kPhi; ++j) {
            const double phi = (j + 0.5) * 2.0 * kPi / kPhi;
            const Vec3 l{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            const brdf::Rgb f = path::bsdf_cosine(m, {0.0F, 1.0F, 0.0F}, fv(v), fv(l));
            sum += luma({std::get<0>(f), std::get<1>(f), std::get<2>(f)}) * std::sin(theta);
        }
    }
    return sum * (kPi / 2.0 / kTheta) * (2.0 * kPi / kPhi);
}

// The point of the floor a pixel's ray (through its centre) meets.
Vec3 floor_point(const Vec3& eye, double x, double y)
{
    const Vec3 d = pixel_ray(eye, x, y);
    return add(eye, mul(d, -y_of(eye) / y_of(d)));
}

// The furnace's answer at the region's pixels: the lobe's hemisphere integral seen from each, by the processor's own
// importance sampling (a narrow lobe defeats a grid) - its mean and standard error.
struct Estimate {
    double mean = 0.0;
    double sigma = 0.0;
};
Estimate furnace_expected(const Vec3& eye, const brdf::Material& m)
{
    constexpr int kSamples = 20000;
    double sum = 0.0;
    double sum2 = 0.0;
    int count = 0;
    const int x0 = static_cast<int>(kWidth / 2) - (kRegion / 2);
    const int y0 = static_cast<int>(kHeight / 2) - (kRegion / 2);
    uint32_t rng = 12345U;
    for (int y = y0; y < y0 + kRegion; ++y) {
        for (int x = x0; x < x0 + kRegion; ++x) {
            const Vec3 v = unit(sub(eye, floor_point(eye, x + 0.5, y + 0.5)));
            for (int i = 0; i < kSamples; ++i) {
                const float u0 = path::next_number(&rng);
                const float u1 = path::next_number(&rng);
                const float u2 = path::next_number(&rng);
                const path::BsdfSample s = path::sample_bsdf(m, {0.0F, 1.0F, 0.0F}, fv(v), u0, u1, u2);
                double value = 0.0;
                if (s.valid) {
                    const brdf::Rgb f = path::bsdf_cosine(m, {0.0F, 1.0F, 0.0F}, fv(v), s.l);
                    value = luma({std::get<0>(f), std::get<1>(f), std::get<2>(f)}) / s.pdf;
                }
                sum += value;
                sum2 += value * value;
                ++count;
            }
        }
    }
    const double mean = sum / count;
    return {.mean = mean, .sigma = std::sqrt(std::max(0.0, (sum2 / count) - (mean * mean)) / count)};
}

// The disc's direct light at a floor point toward the eye: the lobe over the 16-gon's area (each triangle cut into
// kCuts^2 small ones, each at its centroid), Le x f cos x cos' / d^2; a Lambertian's closed form for a round disc (albedo
// / pi x Le x the coaxial disc's form factor pi R^2 / (R^2 + h^2) at the centre) is reported beside it.
double disc_direct(const brdf::Material& m, const Vec3& p, const Vec3& v)
{
    constexpr int kCuts = 96;
    double sum = 0.0;
    for (const Triangle& tri : disc_triangles()) {
        const Vec3& a = core::at(tri.corners, 0);
        const Vec3 e1 = mul(sub(core::at(tri.corners, 1), a), 1.0 / kCuts);
        const Vec3 e2 = mul(sub(core::at(tri.corners, 2), a), 1.0 / kCuts);
        const Vec3 across = cross(e1, e2);
        const double small = 0.5 * std::sqrt(dot(across, across));
        for (int i = 0; i < kCuts; ++i) {
            for (int j = 0; i + j < kCuts; ++j) {
                // the upright small triangle (i, j) and, but on the last row, the one turned over beside it
                for (int turned = 0; turned < (i + j + 1 < kCuts ? 2 : 1); ++turned) {
                    const double ci = i + (turned == 0 ? 1.0 / 3.0 : 2.0 / 3.0);
                    const double cj = j + (turned == 0 ? 1.0 / 3.0 : 2.0 / 3.0);
                    const Vec3 y = add(a, add(mul(e1, ci), mul(e2, cj)));
                    const Vec3 to = sub(y, p);
                    const double d2 = dot(to, to);
                    const Vec3 l = mul(to, 1.0 / std::sqrt(d2));
                    const brdf::Rgb f = path::bsdf_cosine(m, {0.0F, 1.0F, 0.0F}, fv(v), fv(l));
                    sum += luma({std::get<0>(f), std::get<1>(f), std::get<2>(f)}) * y_of(l) / d2 * small;
                }
            }
        }
    }
    return sum * kDiscRadiance;
}

// --- the processor's twin path tracer (vulkan_path_reference.cpp's trace, the same sampling and numbers) ----------------

struct TwinHit {
    bool hit = false;
    double t = 0.0;
    std::size_t triangle = 0;
    Vec3 normal{};
};

TwinHit twin_closest(const std::vector<Triangle>& scene, const Vec3& origin, const Vec3& dir, double reach)
{
    TwinHit best;
    best.t = reach;
    for (std::size_t i = 0; i < scene.size(); ++i) {
        const std::array<Vec3, 3>& c = core::at(scene, i).corners;
        const Vec3 e1 = sub(core::at(c, 1), core::at(c, 0));
        const Vec3 e2 = sub(core::at(c, 2), core::at(c, 0));
        const Vec3 pv = cross(dir, e2);
        const double det = dot(e1, pv);
        if (std::abs(det) < 1e-12) {
            continue;
        }
        const Vec3 tv = sub(origin, core::at(c, 0));
        const double u = dot(tv, pv) / det;
        if (u < 0.0 || u > 1.0) {
            continue;
        }
        const Vec3 qv = cross(tv, e1);
        const double v = dot(dir, qv) / det;
        if (v < 0.0 || u + v > 1.0) {
            continue;
        }
        const double t = dot(e2, qv) / det;
        if (t > 0.0 && t < best.t) {
            best = {.hit = true, .t = t, .triangle = i, .normal = unit(cross(e1, e2))};
        }
    }
    return best;
}

double twin_offset(const Vec3& p)
{
    return 0.02 + (2.0e-5 * std::max({std::abs(x_of(p)), std::abs(y_of(p)), std::abs(z_of(p))}));
}

struct TwinSetup {
    std::vector<Triangle> scene;
    std::vector<std::size_t> emitters; // (the scene's emitting triangles, in a list)
    double sky = 0.0;
    double albedo = 1.0; // (the floor's; the disc's 0)
    double f0 = 0.04;
    double roughness = 0.5;
    uint32_t bounces = path::kMostBounces;
};

double max_of(const Rgb& c) { return std::max({core::at(c, 0), core::at(c, 1), core::at(c, 2)}); }

// One path's luma (the twin of trace_path, MIS strategy).
double twin_path(const TwinSetup& setup, Vec3 origin, Vec3 dir, uint32_t* rng)
{
    const double emitters = static_cast<double>(setup.emitters.size());
    const double there = (setup.sky > 0.0 ? 1.0 : 0.0) + (emitters > 0.0 ? 1.0 : 0.0);
    const double choice_sky = setup.sky > 0.0 ? 1.0 / there : 0.0;
    const double choice_emitter = emitters > 0.0 ? 1.0 / there : 0.0;
    double radiance = 0.0;
    Rgb weight{1.0, 1.0, 1.0};
    double last_pdf = 0.0;
    Vec3 last_normal{0.0, 1.0, 0.0};
    for (uint32_t depth = 0; depth <= setup.bounces; ++depth) {
        const TwinHit h = twin_closest(setup.scene, origin, dir, 1.0e6);
        if (!h.hit) {
            const double sky = y_of(dir) > 0.0 ? setup.sky : 0.0;
            if (depth == 0) {
                radiance += luma(weight) * sky;
            } else {
                const double sky_pdf = choice_sky * std::max(dot(last_normal, dir), 0.0) / kPi;
                radiance += luma(weight) * sky * path::power_heuristic(static_cast<float>(last_pdf), static_cast<float>(sky_pdf));
            }
            break;
        }
        const Triangle& tri = core::at(setup.scene, h.triangle);
        Vec3 n = h.normal;
        if (dot(n, dir) > 0.0) {
            n = mul(n, -1.0);
        }
        const std::array<Vec3, 3>& c = tri.corners;
        const double area = 0.5 * std::sqrt(dot(cross(sub(core::at(c, 1), core::at(c, 0)), sub(core::at(c, 2), core::at(c, 0))),
                                                cross(sub(core::at(c, 1), core::at(c, 0)), sub(core::at(c, 2), core::at(c, 0)))));
        const Vec3 position = add(origin, mul(dir, h.t));
        if (tri.emits) {
            if (depth == 0) {
                radiance += luma(weight) * kDiscRadiance;
            } else {
                const double cos_light = std::abs(dot(n, dir));
                const double light_pdf = choice_emitter / emitters / area * h.t * h.t / cos_light;
                radiance += luma(weight) * kDiscRadiance *
                            path::power_heuristic(static_cast<float>(last_pdf), static_cast<float>(light_pdf));
            }
        }
        if (depth == setup.bounces) {
            break;
        }
        const brdf::Material m = forced(tri.emits ? 0.0 : setup.albedo, setup.f0, setup.roughness);
        const Vec3 v = mul(dir, -1.0);
        const Vec3 p = add(position, mul(n, twin_offset(position)));
        // the light: the sky or an emitting triangle
        const float u = path::next_number(rng);
        const float u1 = path::next_number(rng);
        const float u2 = path::next_number(rng);
        const float u3 = path::next_number(rng);
        if (u < choice_sky) {
            const Vec3 l = dv(path::to_world(path::basis_of(fv(n)), path::cosine_direction(u1, u2)));
            if (dot(l, n) > 0.0 && !twin_closest(setup.scene, p, l, 1.0e6).hit && y_of(l) > 0.0) {
                const brdf::Rgb f = path::bsdf_cosine(m, fv(n), fv(v), fv(l));
                const double pdf = choice_sky * dot(n, l) / kPi;
                const double w = path::power_heuristic(static_cast<float>(pdf), path::bsdf_pdf(m, fv(n), fv(v), fv(l)));
                radiance += luma(weight) * luma({std::get<0>(f), std::get<1>(f), std::get<2>(f)}) * setup.sky * w / pdf;
            }
        } else if (u < choice_sky + choice_emitter) {
            const auto k = std::min(static_cast<std::size_t>(u1 * static_cast<float>(emitters)), setup.emitters.size() - 1);
            const std::size_t index = core::at(setup.emitters, k);
            const Triangle& light = core::at(setup.scene, index);
            const std::array<float, 3> b = path::triangle_point(u2, u3);
            const Vec3 y = add(add(mul(core::at(light.corners, 0), std::get<0>(b)), mul(core::at(light.corners, 1), std::get<1>(b))),
                               mul(core::at(light.corners, 2), std::get<2>(b)));
            const Vec3 to = sub(y, p);
            const double d = std::sqrt(dot(to, to));
            const Vec3 l = mul(to, 1.0 / d);
            const brdf::Rgb f = path::bsdf_cosine(m, fv(n), fv(v), fv(l));
            const TwinHit seen = twin_closest(setup.scene, p, l, (d * 1.001) + twin_offset(y));
            if (dot(l, n) > 0.0 && seen.hit && seen.triangle == index) {
                const std::array<Vec3, 3>& lc = light.corners;
                const Vec3 across = cross(sub(core::at(lc, 1), core::at(lc, 0)), sub(core::at(lc, 2), core::at(lc, 0)));
                const double light_area = 0.5 * std::sqrt(dot(across, across));
                const double cos_light = std::abs(dot(unit(across), l));
                const double pdf = choice_emitter / emitters / light_area * seen.t * seen.t / cos_light;
                const double w = path::power_heuristic(static_cast<float>(pdf), path::bsdf_pdf(m, fv(n), fv(v), fv(l)));
                radiance += luma(weight) * luma({std::get<0>(f), std::get<1>(f), std::get<2>(f)}) * kDiscRadiance * w / pdf;
            }
        }
        const float b0 = path::next_number(rng);
        const float b1 = path::next_number(rng);
        const float b2 = path::next_number(rng);
        const path::BsdfSample s = path::sample_bsdf(m, fv(n), fv(v), b0, b1, b2);
        if (!s.valid || dot(dv(s.l), n) <= 0.0) {
            break;
        }
        const brdf::Rgb f = path::bsdf_cosine(m, fv(n), fv(v), s.l);
        for (std::size_t ch = 0; ch < 3; ++ch) {
            core::at(weight, ch) *= static_cast<double>(core::at(f, ch)) / static_cast<double>(s.pdf);
        }
        last_pdf = s.pdf;
        last_normal = n;
        origin = p;
        dir = dv(s.l);
        if (depth + 1 >= path::kRouletteFrom) {
            const double keep = std::clamp(max_of(weight), static_cast<double>(path::kRouletteLeast),
                                           static_cast<double>(path::kRouletteMost));
            if (path::next_number(rng) >= keep) {
                break;
            }
            for (double& channel : weight) {
                channel /= keep;
            }
        }
    }
    return radiance;
}

Measured twin_batches(const TwinSetup& setup, const Vec3& eye, uint32_t samples, uint32_t seed_base)
{
    Measured out;
    const auto x0 = static_cast<uint32_t>((kWidth / 2) - (kRegion / 2));
    const auto y0 = static_cast<uint32_t>((kHeight / 2) - (kRegion / 2));
    for (int batch = 0; batch < kBatches; ++batch) {
        const uint32_t seed = seed_base + static_cast<uint32_t>(batch) * 7919U;
        double sum = 0.0;
        for (uint32_t y = y0; y < y0 + kRegion; ++y) {
            for (uint32_t x = x0; x < x0 + kRegion; ++x) {
                for (uint32_t s = 0; s < samples; ++s) {
                    uint32_t rng = path::sequence_start(x, y, s, seed);
                    const float jx = path::next_number(&rng);
                    const float jy = path::next_number(&rng);
                    sum += twin_path(setup, eye, pixel_ray(eye, x + jx, y + jy), &rng);
                }
            }
        }
        out.batches.push_back(sum / (static_cast<double>(samples) * kRegion * kRegion));
    }
    double sum = 0.0;
    for (const double value : out.batches) {
        sum += value;
    }
    out.mean = sum / kBatches;
    double spread = 0.0;
    for (const double value : out.batches) {
        spread += (value - out.mean) * (value - out.mean);
    }
    out.sigma = std::sqrt(spread / (kBatches - 1) / kBatches);
    return out;
}

// --- the checks --------------------------------------------------------------------------------------------------------------

// The BSDF's sampling on the processor: its pdf over the sphere integrates to 1, and its samples' f cos / pdf average to
// the lobe's integral.
void check_sampling(Tally* tally)
{
    text::print("the BSDF's sampling (rt_path.h):\n");
    for (const double roughness : {0.25, 0.5, 1.0}) {
        for (const double view_cos : {0.95, 0.3}) {
            const brdf::Material m = forced(0.7, 0.04, roughness);
            const Vec3 v{std::sqrt(1.0 - (view_cos * view_cos)), view_cos, 0.0};
            // the pdf's integral on a grid (finer toward the pole and the mirror direction: many cells)
            constexpr int kTheta = 1024;
            constexpr int kPhi = 512;
            double integral = 0.0;
            for (int i = 0; i < kTheta; ++i) {
                const double theta = (i + 0.5) * (kPi / 2.0) / kTheta;
                for (int j = 0; j < kPhi; ++j) {
                    const double phi = (j + 0.5) * 2.0 * kPi / kPhi;
                    const Vec3 l{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
                    integral += path::bsdf_pdf(m, {0.0F, 1.0F, 0.0F}, fv(v), fv(l)) * std::sin(theta);
                }
            }
            integral *= (kPi / 2.0 / kTheta) * (2.0 * kPi / kPhi);
            // the estimator's mean against the lobe's integral
            uint32_t rng = path::sequence_start(static_cast<uint32_t>(roughness * 100.0), static_cast<uint32_t>(view_cos * 100.0), 0, 7);
            constexpr int kSamples = 400000;
            double sum = 0.0;
            double sum2 = 0.0;
            int below = 0; // (a visible normal's reflection under the surface: the path ends there)
            for (int i = 0; i < kSamples; ++i) {
                const float u0 = path::next_number(&rng);
                const float u1 = path::next_number(&rng);
                const float u2 = path::next_number(&rng);
                const path::BsdfSample s = path::sample_bsdf(m, {0.0F, 1.0F, 0.0F}, fv(v), u0, u1, u2);
                double value = 0.0;
                if (s.valid) {
                    const brdf::Rgb f = path::bsdf_cosine(m, {0.0F, 1.0F, 0.0F}, fv(v), s.l);
                    value = luma({std::get<0>(f), std::get<1>(f), std::get<2>(f)}) / s.pdf;
                } else {
                    ++below;
                }
                sum += value;
                sum2 += value * value;
            }
            const double below_share = static_cast<double>(below) / kSamples;
            const double mean = sum / kSamples;
            const double sigma = std::sqrt(std::max(0.0, (sum2 / kSamples) - (mean * mean)) / kSamples);
            const double expected = hemisphere_integral(m, v);
            tally->check(std::abs(integral + below_share - 1.0) < 0.005,
                         text::format("roughness %.2f, N.V %.2f: the pdf integrates to %.4f over the hemisphere, %.4f of the "
                                      "samples fall under it",
                                      roughness, view_cos, integral, below_share));
            tally->check(std::abs(mean - expected) <= (kSigmas * sigma) + (0.002 * expected),
                         text::format("roughness %.2f, N.V %.2f: f cos / pdf averages %.5f +- %.5f, the lobe's integral %.5f",
                                      roughness, view_cos, mean, sigma, expected));
        }
    }
    // one owner per lamp
    std::vector<rt::Emitter> lamps(3);
    core::at(lamps, 0).intensity = 1.0F;
    core::at(lamps, 1).intensity = 2.0F;
    core::at(lamps, 2).intensity = 3.0F;
    const std::vector<uint8_t> surfaced{0, 1};
    const std::vector<rt::Emitter> kept = path::analytic_lamps(lamps, surfaced);
    tally->check(kept.size() == 2 && core::at(kept, 0).intensity == 1.0F && core::at(kept, 1).intensity == 3.0F,
                 text::format("analytic_lamps keeps the lamps without a surface of their own (%zu of 3)", kept.size()));
}

void check_device(render::Gpu& gpu, Tally* tally)
{
    Targets targets;
    std::array<SceneMesh, 2> meshes{};
    const bool made = make_targets(gpu, &targets) && make_mesh(gpu, floor_triangles(), false, &meshes.front()) &&
                      make_mesh(gpu, disc_triangles(), true, &meshes.back());
    tally->check(made, "the scene made (a floor; a disc of 16 emitting triangles with a light store)");
    if (!made) {
        release_targets(gpu, targets);
        return;
    }
    const std::span<const SceneMesh> floor_only(meshes.data(), 1);
    const std::span<const SceneMesh> both(meshes);
    uint32_t not_finite = 0;
    bool read = true;
    uint32_t seed = 1;

    // 1. the white furnace
    text::print("the white furnace (an albedo-1 floor under an even sky of 1):\n");
    for (const Vec3& eye : {Vec3{0.0, 900.0, 500.0}, Vec3{0.0, 300.0, 900.0}}) {
        for (const double roughness : {0.05, 0.25, 0.5, 0.75, 1.0}) {
            vk::PathOverrides overrides;
            overrides.sky = {1.0F, 1.0F, 1.0F};
            overrides.force_material = true;
            overrides.roughness = static_cast<float>(roughness);
            overrides.f0 = 0.04F;
            overrides.albedo_scale = 1.0F;
            overrides.samples_per_frame = 16;
            const Measured got = run_batches(gpu, targets, floor_only, eye, overrides, 2, seed++, &read);
            not_finite += got.not_finite;
            const Estimate expected = furnace_expected(eye, forced(1.0, 0.04, roughness));
            tally->check(read && within(got.mean, got.sigma, expected.mean, expected.sigma) && std::abs(got.mean - 1.0) < 0.03,
                         text::format("eye %.0f up: roughness %.2f returns %.5f +- %.5f (the processor's integral of the "
                                      "lobe there %.5f +- %.5f; 1 within the DFG table's error)",
                                      y_of(eye), roughness, got.mean, got.sigma, expected.mean, expected.sigma));
        }
    }

    // 2. the floor under the disc, direct light only
    text::print("the floor under the emitting disc, direct light (one bounce), no sky:\n");
    const Vec3 eye{700.0, 450.0, 0.0};
    {
        const brdf::Material m = forced(0.8, 0.0, 1.0);
        vk::PathOverrides overrides;
        overrides.force_material = true;
        overrides.roughness = 1.0F;
        overrides.f0 = 0.0F;
        overrides.albedo_scale = 0.8F;
        overrides.bounces = 1;
        overrides.samples_per_frame = 32;
        const Measured got = run_batches(gpu, targets, both, eye, overrides, 2, seed++, &read);
        not_finite += got.not_finite;
        double expected = 0.0;
        const int x0 = static_cast<int>(kWidth / 2) - (kRegion / 2);
        const int y0 = static_cast<int>(kHeight / 2) - (kRegion / 2);
        for (int y = y0; y < y0 + kRegion; ++y) {
            for (int x = x0; x < x0 + kRegion; ++x) {
                const Vec3 p = floor_point(eye, x + 0.5, y + 0.5);
                expected += disc_direct(m, p, unit(sub(eye, p)));
            }
        }
        expected /= kRegion * kRegion;
        const double r2 = static_cast<double>(kDiscRadius) * kDiscRadius;
        const double lambert = 0.8 / kPi * kDiscRadiance * kPi * r2 / (r2 + (static_cast<double>(kDiscHeight) * kDiscHeight));
        tally->check(read && got.emitters == static_cast<uint32_t>(kDiscSides),
                     text::format("the emitting triangles found on the device: %u (the disc's %d)", got.emitters, kDiscSides));
        tally->check(read && within(got.mean, got.sigma, expected, 0.0),
                     text::format("the disc's direct light %.5f +- %.5f, its integral over the disc %.5f (a Lambertian "
                                  "of the albedo at the centre: %.5f)",
                                  got.mean, got.sigma, expected, lambert));
    }

    // 3. the strategies, every bounce
    text::print("the floor, the disc and a dim sky, every bounce:\n");
    vk::PathOverrides full;
    full.sky = {0.2F, 0.2F, 0.2F};
    full.force_material = true;
    full.roughness = 0.3F;
    full.f0 = 0.04F;
    full.albedo_scale = 0.6F;
    full.samples_per_frame = 32;
    const Measured mis = run_batches(gpu, targets, both, eye, full, 2, seed++, &read);
    full.strategy = vk::PathStrategy::kLightsOnly;
    const Measured lights = run_batches(gpu, targets, both, eye, full, 2, seed++, &read);
    full.strategy = vk::PathStrategy::kBsdfOnly;
    const Measured bsdf = run_batches(gpu, targets, both, eye, full, 8, seed++, &read);
    full.strategy = vk::PathStrategy::kMis;
    not_finite += mis.not_finite + lights.not_finite + bsdf.not_finite;
    tally->check(read && within(mis.mean, mis.sigma, lights.mean, lights.sigma),
                 text::format("MIS %.5f +- %.5f, the lights alone %.5f +- %.5f", mis.mean, mis.sigma, lights.mean, lights.sigma));
    tally->check(read && within(mis.mean, mis.sigma, bsdf.mean, bsdf.sigma),
                 text::format("MIS %.5f +- %.5f, the BSDF alone %.5f +- %.5f (the disc's light counted once)", mis.mean,
                              mis.sigma, bsdf.mean, bsdf.sigma));
    tally->check(mis.sigma <= bsdf.sigma * 2.0,
                 text::format("MIS's noise %.5f with 64 paths a pixel, the BSDF alone's %.5f with 256", mis.sigma, bsdf.sigma));
    // the processor's twin
    TwinSetup twin;
    twin.scene = floor_triangles();
    for (const Triangle& tri : disc_triangles()) {
        twin.emitters.push_back(twin.scene.size());
        twin.scene.push_back(tri);
    }
    twin.sky = 0.2;
    twin.albedo = 0.6;
    twin.f0 = 0.04;
    twin.roughness = 0.3;
    const Measured cpu = twin_batches(twin, eye, 64, 9001);
    tally->check(within(mis.mean, mis.sigma, cpu.mean, cpu.sigma),
                 text::format("the device %.5f +- %.5f, the processor's twin %.5f +- %.5f", mis.mean, mis.sigma, cpu.mean, cpu.sigma));

    // 4. one owner: the disc's lamp given as an analytic lamp too
    rt::Emitter lamp;
    lamp.position = {0.0F, kDiscHeight - 1.0F, 0.0F};
    lamp.size = 1.0F;
    lamp.colour = {1.0F, 1.0F, 1.0F};
    lamp.intensity = kDiscRadiance * 0.25F; // (a sizeable light: it shows if it is shone)
    lamp.reach = 3000.0F;
    lamp.core = kDiscRadius;
    lamp.shape = rt::EmitterShape::kPanel;
    lamp.axis = {0.0F, -1.0F, 0.0F};
    full.lamps = {lamp};
    full.lamps_surfaced = {1};
    const Measured owned = run_batches(gpu, targets, both, eye, full, 2, seed++, &read);
    full.lamps_surfaced = {0};
    const Measured twice = run_batches(gpu, targets, both, eye, full, 2, seed++, &read);
    not_finite += owned.not_finite + twice.not_finite;
    tally->check(read && within(owned.mean, owned.sigma, mis.mean, mis.sigma),
                 text::format("the lamp with its own emitting surface: %.5f +- %.5f, as without it (%.5f)", owned.mean,
                              owned.sigma, mis.mean));
    tally->check(read && twice.mean > mis.mean + (kSigmas * std::hypot(twice.sigma, mis.sigma)),
                 text::format("the same lamp not marked as its surface's: %.5f - counted twice (what the rule prevents)",
                              twice.mean));
    tally->check(read && not_finite == 0, text::format("samples not finite: %u", not_finite));
    tally->check(read, "every run read back");
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
    vk::set_context_options(
        {.validation = options.validation, .device_index = options.device, .on_message = print_line, .ray_tracing = true},
        window);
    render::GpuContext& context = vk::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return Opened::kNoDevice;
    }
    const vk::Backend& state = vk::backend();
    text::print("validation layer: %s\n", state.context.validation_on() ? "on" : "off (not installed, or not asked for)");
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
    check_sampling(&tally);
    const Opened opened = open_device(options, window);
    if (opened == Opened::kNoRayQueries) {
        return 2;
    }
    if (opened == Opened::kNoDevice) {
        return vk::loader_version() == 0 ? 2 : 1;
    }
    check_device(vk::gpu(), &tally);
    vk::set_path_overrides({});
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
    text::print("thug2_vulkan_path_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan path tracer check", kWindowWidth, kWindowHeight,
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
