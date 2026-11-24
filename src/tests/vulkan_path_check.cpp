// NOT ORIGINAL - thug2_vulkan_path_check.exe: the reference path tracer (gpu_vulkan/vulkan_path_reference.h, the console's
// `pt ref`; path tracing plan, stage 1) and its sampling (engine/gfx/rt_path.h) tried on this machine against answers
// worked out on the processor. Everything goes through the installed render::rt::RayTracing (the acceleration
// structures, the geometry table with a light store, the material table) as the game's frame record feeds it.
//
// On the processor (rt_path.h's twin):
//   - the BSDF's pdf integrates to 1 over the sphere (every roughness, two view angles), and its samples' mean of
//     f cos / pdf is the lobe's integral by quadrature (the sampler draws what the pdf says);
//   - one owner per lamp: analytic_lamps keeps exactly the lamps without an emitting surface;
//   - (stage 2: rt_light_list.h) the alias table's pdfs sum to 1 and its picks fall as the weights say (a weightless
//     entry never); the light list's groups evenly, each light by power (and stage 1's kinds evenly); each lamp shape's
//     power from its photometry against its light integrated over a sphere round it; the sky's distribution - its pdf
//     integrates to 1, a sample's pdf is the pdf of its direction, the samples fall as luminance x solid angle says.
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
//   - (stage 2) the light list by power against stage 1's kinds evenly: the same mean on the floor, disc and sky; on the
//     floor under 32 lamps of very different power the same mean and less noise;
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
#include "engine/gfx/rt_light_list.h"
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
namespace lights = render::rt::lights;
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
constexpr float kDiscHeight = 300.0F;
constexpr float kDiscRadius = 120.0F;
constexpr int kDiscSides = 16;
constexpr float kDiscRadiance = 4.0F;
constexpr uint32_t kWorldOpaque = rt::kWorld | rt::kOpaque | rt::kCastsShadow;
constexpr int kBatches = 8;
constexpr int kRegion = 4; // the pixels compared: a kRegion x kRegion block round the view's centre
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
    // (round P10) its instance's flags and the height its transform moves it by (an object's mesh in its own space)
    uint32_t flags = rt::kWorld | rt::kOpaque | rt::kCastsShadow;
    float lift = 0.0F;
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
    // (round EMIT) an emitting mesh two-sided - its light from both faces, as every emitter sent before the round (the
    // scenes are wound without the game's convention: the disc's front faces up); the one-sided rule has its own cases
    surface.flags = rt::kOpaque | (emitting ? rt::kTwoSided : 0U);
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
        std::array<std::array<float, 4>, 3> rows = kIdentityRows;
        std::get<3>(std::get<1>(rows)) = mesh.lift;
        record.instances.push_back(
            {.mesh = mesh.id, .material = mesh.material, .flags = mesh.flags, .object_to_world = rows});
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
            const std::array<float, 3>& c =
                core::at(image.mean, (static_cast<std::size_t>(y) * kWidth) + static_cast<std::size_t>(x));
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
        for (int frame = 0; frame <= frames; ++frame) { // (a run's first frame gathers its light list: no paths)
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

double triangle_area(const Triangle& tri)
{
    const std::array<Vec3, 3>& c = tri.corners;
    const Vec3 across = cross(sub(core::at(c, 1), core::at(c, 0)), sub(core::at(c, 2), core::at(c, 0)));
    return 0.5 * std::sqrt(dot(across, across));
}

// The device's light list for a twin's scene (rt_light_list.h, as vulkan_path_reference.cpp builds it): the even sky's
// grid (its cells' centres above the horizon), the emitting triangles by their power (2 pi x area x Le).
struct TwinLights {
    lights::LightList list;
    lights::SkyDistribution sky;
    std::vector<std::array<float, 4>> sky_radiance;
    std::vector<uint32_t> entry_of; // per scene triangle: its entry in the list (0: none)
};
TwinLights twin_lights(const TwinSetup& setup)
{
    TwinLights out;
    const uint32_t width = lights::kSkyGridWidth;
    const uint32_t height = lights::kSkyGridHeight;
    out.sky_radiance.resize(static_cast<std::size_t>(width) * height);
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            const auto value =
                static_cast<float>(lights::sky_cell_centre(column, row, width, height).y > 0.0F ? setup.sky : 0.0);
            core::at(out.sky_radiance, (static_cast<std::size_t>(row) * width) + column) = {value, value, value, 0.0F};
        }
    }
    out.sky = lights::build_sky_distribution(out.sky_radiance, width, height);
    lights::LightSet set;
    set.sky_integral = out.sky.integral;
    out.entry_of.assign(setup.scene.size(), 0);
    for (const std::size_t index : setup.emitters) {
        core::at(out.entry_of, index) = lights::kFirstTriangleEntry + static_cast<uint32_t>(set.triangle_powers.size());
        set.triangle_powers.push_back(
            static_cast<float>(lights::triangle_power(kDiscRadiance, triangle_area(core::at(setup.scene, index)), 1.0)));
    }
    out.list = lights::build_light_list(set, lights::LightWeights::kPower);
    return out;
}

double sky_cell_value(const TwinLights& lit, const Vec3& dir)
{
    return core::at(core::at(lit.sky_radiance, lights::sky_cell_of(fv(dir), lit.sky.width, lit.sky.height)), 0);
}

// One path's luma (the twin of trace_path, MIS strategy).
double twin_path(const TwinSetup& setup, const TwinLights& lit, Vec3 origin, Vec3 dir, uint32_t* rng)
{
    const std::vector<lights::AliasEntry>& entries = lit.list.table.entries;
    double radiance = 0.0;
    Rgb weight{1.0, 1.0, 1.0};
    double last_pdf = 0.0;
    for (uint32_t depth = 0; depth <= setup.bounces; ++depth) {
        const TwinHit h = twin_closest(setup.scene, origin, dir, 1.0e6);
        if (!h.hit) {
            const double sky = sky_cell_value(lit, dir);
            if (depth == 0) {
                radiance += luma(weight) * sky;
            } else {
                const double sky_pdf = static_cast<double>(core::at(entries, lights::kSkyEntry).pdf) *
                                       static_cast<double>(lights::sky_pdf(lit.sky, fv(dir)));
                radiance += luma(weight) * sky *
                            path::power_heuristic(static_cast<float>(last_pdf), static_cast<float>(sky_pdf));
            }
            break;
        }
        const Triangle& tri = core::at(setup.scene, h.triangle);
        Vec3 n = h.normal;
        if (dot(n, dir) > 0.0) {
            n = mul(n, -1.0);
        }
        const double area = triangle_area(tri);
        const Vec3 position = add(origin, mul(dir, h.t));
        if (tri.emits) {
            if (depth == 0) {
                radiance += luma(weight) * kDiscRadiance;
            } else {
                const double cos_light = std::abs(dot(n, dir));
                const double light_pdf =
                    static_cast<double>(core::at(entries, core::at(lit.entry_of, h.triangle)).pdf) / area * h.t * h.t /
                    cos_light;
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
        // the light: one of the list, by its pick
        const float u = path::next_number(rng);
        const float u1 = path::next_number(rng);
        const float u2 = path::next_number(rng);
        const float u3 = path::next_number(rng);
        const float u4 = path::next_number(rng);
        const lights::AliasEntry& e = core::at(entries, lights::alias_pick(entries, u));
        if (e.pdf > 0.0F && lights::kind_of(e.payload) == lights::LightKind::kSky) {
            const lights::SkySample s = lights::sample_sky(lit.sky, u1, u2, u3, u4);
            const Vec3 l = dv(s.dir);
            const double pdf = static_cast<double>(e.pdf) * static_cast<double>(s.pdf);
            const double sky = core::at(core::at(lit.sky_radiance, s.cell), 0);
            if (pdf > 0.0 && dot(l, n) > 0.0 && !twin_closest(setup.scene, p, l, 1.0e6).hit) {
                const brdf::Rgb f = path::bsdf_cosine(m, fv(n), fv(v), fv(l));
                const double w = path::power_heuristic(static_cast<float>(pdf), path::bsdf_pdf(m, fv(n), fv(v), fv(l)));
                radiance += luma(weight) * luma({std::get<0>(f), std::get<1>(f), std::get<2>(f)}) * sky * w / pdf;
            }
        } else if (e.pdf > 0.0F && lights::kind_of(e.payload) == lights::LightKind::kTriangle) {
            const std::size_t index = core::at(setup.emitters, lights::index_of(e.payload));
            const Triangle& light = core::at(setup.scene, index);
            const std::array<float, 3> b = path::triangle_point(u1, u2);
            const Vec3 y = add(
                add(mul(core::at(light.corners, 0), std::get<0>(b)), mul(core::at(light.corners, 1), std::get<1>(b))),
                mul(core::at(light.corners, 2), std::get<2>(b)));
            const Vec3 to = sub(y, p);
            const double d = std::sqrt(dot(to, to));
            const Vec3 l = mul(to, 1.0 / d);
            const brdf::Rgb f = path::bsdf_cosine(m, fv(n), fv(v), fv(l));
            const TwinHit seen = twin_closest(setup.scene, p, l, (d * 1.001) + twin_offset(y));
            if (dot(l, n) > 0.0 && seen.hit && seen.triangle == index) {
                const std::array<Vec3, 3>& lc = light.corners;
                const Vec3 across = cross(sub(core::at(lc, 1), core::at(lc, 0)), sub(core::at(lc, 2), core::at(lc, 0)));
                const double cos_light = std::abs(dot(unit(across), l));
                const double pdf = static_cast<double>(e.pdf) / triangle_area(light) * seen.t * seen.t / cos_light;
                const double w = path::power_heuristic(static_cast<float>(pdf), path::bsdf_pdf(m, fv(n), fv(v), fv(l)));
                radiance +=
                    luma(weight) * luma({std::get<0>(f), std::get<1>(f), std::get<2>(f)}) * kDiscRadiance * w / pdf;
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
    const TwinLights lit = twin_lights(setup);
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
                    sum += twin_path(setup, lit, eye, pixel_ray(eye, x + jx, y + jy), &rng);
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

// --- the light list on the processor (rt_light_list.h) --------------------------------------------------------------------

// The alias table: its pdfs sum to 1, a weightless entry is never picked, and kSamples picks fall on each entry as its
// weight says (each within 4 sigma of the binomial).
void check_alias(Tally* tally)
{
    text::print("the light list's alias table (rt_light_list.h):\n");
    const std::vector<double> weights{1.0, 0.0, 3.0, 0.001, 10.0, 0.0, 5.5, 1.0e-7, 250.0, 0.25};
    const lights::AliasTable table = lights::build_alias_table(weights);
    double pdf_sum = 0.0;
    for (const lights::AliasEntry& entry : table.entries) {
        pdf_sum += static_cast<double>(entry.pdf);
    }
    tally->check(std::abs(pdf_sum - 1.0) < 1e-6, text::format("the pdfs sum to %.8f", pdf_sum));
    constexpr int kSamples = 2000000;
    std::vector<int> hits(weights.size(), 0);
    uint32_t rng = 99U;
    for (int i = 0; i < kSamples; ++i) {
        ++core::at(hits, lights::alias_pick(table.entries, path::next_number(&rng)));
    }
    bool shares_ok = true;
    double worst = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const double p = core::at(weights, i) / table.total;
        const double expected = p * kSamples;
        const double sigma = std::sqrt(std::max(expected * (1.0 - p), 1.0));
        const double off = std::abs(core::at(hits, i) - expected) / sigma;
        worst = std::max(worst, off);
        shares_ok = shares_ok && off <= 4.0 && (core::at(weights, i) > 0.0 || core::at(hits, i) == 0);
    }
    tally->check(shares_ok,
                 text::format("%d picks fall on the entries as their weights say (worst %.2f sigma; weightless "
                              "entries %d and %d picked %d and %d times)",
                              kSamples, worst, 1, 5, core::at(hits, 1), core::at(hits, 5)));
    // the light list: the two groups evenly, each by power; stage 1's kinds evenly
    lights::LightSet set;
    set.disc_light = {2.0F, 2.0F, 2.0F};
    set.disc_cos = 0.9999F;
    set.sky_integral = 1.5;
    set.triangle_powers = {1.0F, 4.0F, 0.0F, 15.0F};
    rt::Emitter lamp;
    lamp.colour = {1.0F, 1.0F, 1.0F};
    lamp.intensity = 2.0F;
    lamp.core = 10.0F;
    lamp.reach = 500.0F;
    set.lamps = {lamp, lamp};
    core::at(set.lamps, 1).intensity = 0.5F;
    const lights::LightList list = lights::build_light_list(set, lights::LightWeights::kPower);
    const auto pick = [&list](std::size_t kind) { return core::at(list.kind_pick, kind); };
    const double distant = lights::disc_power(set.disc_light) + lights::sky_power(set.sky_integral);
    const double local = 20.0 + lights::lamp_power(core::at(set.lamps, 0)) + lights::lamp_power(core::at(set.lamps, 1));
    const double triangle_share = 0.5 * 20.0 / local;
    tally->check(
        std::abs(pick(0) + pick(1) - 0.5) < 1e-6 && std::abs(pick(2) + pick(3) - 0.5) < 1e-6 &&
            std::abs(pick(0) - (0.5 * lights::disc_power(set.disc_light) / distant)) < 1e-6 &&
            std::abs(pick(2) - triangle_share) < 1e-6 && core::at(list.table.entries, 4).pdf == 0.0F &&
            std::abs(static_cast<double>(core::at(list.table.entries, 5).pdf) - (0.5 * 15.0 / local)) < 1e-6,
        text::format("by power: the distant group %.4f (the disc %.4f, the sky %.4f), the local %.4f (the triangles "
                     "%.4f, the lamps %.4f), a dark triangle never",
                     pick(0) + pick(1), pick(0), pick(1), pick(2) + pick(3), pick(2), pick(3)));
    const lights::LightList even = lights::build_light_list(set, lights::LightWeights::kKindsEvenly);
    tally->check(std::abs(core::at(even.kind_pick, 0) - 0.25) < 1e-6 &&
                     std::abs(core::at(even.kind_pick, 3) - 0.25) < 1e-6 &&
                     std::abs(static_cast<double>(core::at(even.table.entries, 2).pdf) - (0.25 / 3.0)) < 1e-6,
                 "the kinds evenly (stage 1's): each kind 1/4, each of its lit lights evenly");
}

// A lamp's power (its photometry) against its light integrated over a sphere round it: pi x lamp_geometry toward the
// sphere's inward normal (the irradiance there) over the sphere's area.
void check_lamp_power(Tally* tally)
{
    text::print("the lamps' power (rt_light_list.h lamp_power):\n");
    for (const rt::EmitterShape shape : {rt::EmitterShape::kPoint, rt::EmitterShape::kPanel, rt::EmitterShape::kLine}) {
        rt::Emitter lamp;
        lamp.colour = {1.0F, 1.0F, 1.0F};
        lamp.intensity = 3.0F;
        lamp.core = 8.0F;
        lamp.reach = 1.0e9F;
        lamp.shape = shape;
        lamp.axis = {0.0F, 1.0F, 0.0F};
        lamp.half_length = shape == rt::EmitterShape::kLine ? 40.0F : 0.0F;
        const rt::LampShape model{.position = {0.0F, 0.0F, 0.0F},
                                  .shape = shape,
                                  .axis = lamp.axis,
                                  .half_length = lamp.half_length,
                                  .core = lamp.core,
                                  .reach = lamp.reach};
        constexpr double kRadius = 20000.0;
        constexpr int kTheta = 512;
        constexpr int kPhi = 64;
        double flux = 0.0;
        for (int i = 0; i < kTheta; ++i) {
            const double theta = (i + 0.5) * kPi / kTheta;
            for (int j = 0; j < kPhi; ++j) {
                const double phi = (j + 0.5) * 2.0 * kPi / kPhi;
                const Vec3 out{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
                const Vec3 at = mul(out, kRadius);
                std::array<float, 3> toward{};
                const float light = rt::lamp_geometry(
                    model, {static_cast<float>(x_of(at)), static_cast<float>(y_of(at)), static_cast<float>(z_of(at))},
                    {static_cast<float>(-x_of(out)), static_cast<float>(-y_of(out)), static_cast<float>(-z_of(out))},
                    &toward);
                flux += kPi * static_cast<double>(lamp.intensity) * light * kRadius * kRadius * std::sin(theta);
            }
        }
        flux *= (kPi / kTheta) * (2.0 * kPi / kPhi);
        const double power = lights::lamp_power(lamp);
        const std::array<const char*, 3> names{"point", "panel", "line"};
        tally->check(std::abs(power - flux) <= 0.01 * flux,
                     text::format("a %s lamp: its photometry's flux %.6g, its light over a sphere %.6g",
                                  core::at(names, static_cast<std::size_t>(shape)), power, flux));
    }
}

// The sky's distribution over a sky with a bright patch, a gradient and a dim ground: its pdf integrates to 1, each
// sample's pdf is what sky_pdf gives for its direction, the samples fall as the luminance x solid angle says, and
// L / pdf averages the luminance's integral (exactly: the pdf is proportional to it).
void check_sky(Tally* tally)
{
    text::print("the sky's distribution (rt_light_list.h):\n");
    const uint32_t width = lights::kSkyGridWidth;
    const uint32_t height = lights::kSkyGridHeight;
    std::vector<std::array<float, 4>> radiance(static_cast<std::size_t>(width) * height);
    const Vec3 bright = unit({0.4, 0.3, -0.2});
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            const Vec3 d = dv(lights::sky_cell_centre(column, row, width, height));
            double l = y_of(d) > 0.0 ? 0.05 + std::pow(y_of(d), 3.0) : 0.01;
            if (dot(d, bright) > 0.995) {
                l += 40.0;
            }
            core::at(radiance, (static_cast<std::size_t>(row) * width) + column) = {
                static_cast<float>(l * 0.9), static_cast<float>(l), static_cast<float>(l * 1.2), 0.0F};
        }
    }
    const lights::SkyDistribution sky = lights::build_sky_distribution(radiance, width, height);
    // the pdf over the sphere, on a grid finer than the cells (each point's cell's pdf)
    constexpr int kTheta = 2048;
    constexpr int kPhi = 4096;
    double integral = 0.0;
    for (int i = 0; i < kTheta; ++i) {
        const double theta = (i + 0.5) * kPi / kTheta;
        double row = 0.0;
        for (int j = 0; j < kPhi; ++j) {
            const double phi = ((j + 0.5) * 2.0 * kPi / kPhi) - kPi;
            row += lights::sky_pdf(
                sky, fv({std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)}));
        }
        integral += row * std::sin(theta);
    }
    integral *= (kPi / kTheta) * (2.0 * kPi / kPhi);
    tally->check(std::abs(integral - 1.0) < 2e-3, text::format("its pdf integrates to %.5f over the sphere", integral));
    constexpr int kSamples = 1000000;
    constexpr uint32_t kBlocks = 8; // the histogram's blocks: 8 x 8 (rows x columns of cells)
    std::vector<double> expected(kBlocks * kBlocks, 0.0);
    for (uint32_t row = 0; row < height; ++row) {
        const double omega = lights::sky_cell_solid_angle(row, width, height);
        for (uint32_t column = 0; column < width; ++column) {
            const double l = core::at(sky.luminance, (static_cast<std::size_t>(row) * width) + column);
            core::at(expected, ((row * kBlocks / height) * kBlocks) + (column * kBlocks / width)) +=
                l * omega / sky.integral;
        }
    }
    std::vector<int> hits(kBlocks * kBlocks, 0);
    uint32_t rng = 4242U;
    int pdf_mismatch = 0;
    double sum = 0.0;
    double sum2 = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        const float u0 = path::next_number(&rng);
        const float u1 = path::next_number(&rng);
        const float u2 = path::next_number(&rng);
        const float u3 = path::next_number(&rng);
        const lights::SkySample s = lights::sample_sky(sky, u0, u1, u2, u3);
        const uint32_t row = s.cell / width;
        const uint32_t column = s.cell % width;
        ++core::at(hits, ((row * kBlocks / height) * kBlocks) + (column * kBlocks / width));
        const float again = lights::sky_pdf(sky, s.dir);
        if (std::abs(again - s.pdf) > 1e-4F * s.pdf) {
            ++pdf_mismatch;
        }
        const double value = core::at(sky.luminance, s.cell) / s.pdf;
        sum += value;
        sum2 += value * value;
    }
    double worst = 0.0;
    for (std::size_t b = 0; b < hits.size(); ++b) {
        const double p = core::at(expected, b);
        const double e = p * kSamples;
        worst = std::max(worst, std::abs(core::at(hits, b) - e) / std::sqrt(std::max(e * (1.0 - p), 1.0)));
    }
    tally->check(worst <= 4.0,
                 text::format("%d samples fall on 8 x 8 blocks of the sphere as their luminance x solid angle "
                              "says (worst %.2f sigma)",
                              kSamples, worst));
    tally->check(pdf_mismatch <= kSamples / 10000,
                 text::format("a sample's pdf is sky_pdf of its direction (%d of %d differ - a cell's edge)",
                              pdf_mismatch, kSamples));
    const double mean = sum / kSamples;
    const double spread = std::sqrt(std::max(0.0, (sum2 / kSamples) - (mean * mean)));
    tally->check(
        std::abs(mean - sky.integral) <= 1e-4 * sky.integral && spread <= 1e-3 * sky.integral,
        text::format("L / pdf averages %.6f (spread %.3g), the luminance's integral %.6f", mean, spread, sky.integral));
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
            uint32_t rng = path::sequence_start(static_cast<uint32_t>(roughness * 100.0),
                                                static_cast<uint32_t>(view_cos * 100.0), 0, 7);
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
            tally->check(
                std::abs(integral + below_share - 1.0) < 0.005,
                text::format("roughness %.2f, N.V %.2f: the pdf integrates to %.4f over the hemisphere, %.4f of the "
                             "samples fall under it",
                             roughness, view_cos, integral, below_share));
            tally->check(
                std::abs(mean - expected) <= (kSigmas * sigma) + (0.002 * expected),
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
    // (round LG) `pt ref ... nee | bsdf`: the strategy asked (the lights' samples alone, the BSDF's rays alone)
    (void)path::path_console_command("ref 4 direct nee");
    const uint8_t nee = path::path_reference_request().strategy;
    (void)path::path_console_command("ref 4 bsdf only emit");
    const uint8_t bsdf = path::path_reference_request().strategy;
    (void)path::path_console_command("ref 4");
    const uint8_t mis = path::path_reference_request().strategy;
    (void)path::path_console_command("off");
    tally->check(nee == static_cast<uint8_t>(vk::PathStrategy::kLightsOnly) &&
                     bsdf == static_cast<uint8_t>(vk::PathStrategy::kBsdfOnly) &&
                     mis == static_cast<uint8_t>(vk::PathStrategy::kMis) && path::path_reference_request().samples == 0,
                 text::format("`pt ref`'s nee / bsdf read as the strategies (%u, %u; none: %u)", nee, bsdf, mis));
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
            const Measured got = run_batches(gpu, targets, floor_only, eye, overrides, 8, seed++, &read);
            not_finite += got.not_finite;
            const Estimate expected = furnace_expected(eye, forced(1.0, 0.04, roughness));
            tally->check(
                read && within(got.mean, got.sigma, expected.mean, expected.sigma) && std::abs(got.mean - 1.0) < 0.03,
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
        const double lambert =
            0.8 / kPi * kDiscRadiance * kPi * r2 / (r2 + (static_cast<double>(kDiscHeight) * kDiscHeight));
        tally->check(
            read && got.emitters == static_cast<uint32_t>(kDiscSides),
            text::format("the emitting triangles found on the device: %u (the disc's %d)", got.emitters, kDiscSides));
        tally->check(read && within(got.mean, got.sigma, expected, 0.0),
                     text::format("the disc's direct light %.5f +- %.5f, its integral over the disc %.5f (a Lambertian "
                                  "of the albedo at the centre: %.5f)",
                                  got.mean, got.sigma, expected, lambert));
        // (round P10 - objects relit) the same disc as an object's mesh - an object drawn with its own colours, relit:
        // its light store at its vertices, its mesh in its own space 100 units up and its transform bringing it down -
        // listed, sampled and hit by the paths as the world's
        SceneMesh object_disc;
        std::vector<Triangle> raised = disc_triangles();
        constexpr double kRaised = 100.0;
        for (Triangle& tri : raised) {
            for (Vec3& corner : tri.corners) {
                std::get<1>(corner) += kRaised;
            }
        }
        if (make_mesh(gpu, raised, true, &object_disc)) {
            object_disc.flags = rt::kObject | rt::kOpaque | rt::kCastsShadow;
            object_disc.lift = static_cast<float>(-kRaised);
            const std::array<SceneMesh, 2> with_object{meshes.front(), object_disc};
            const Measured as_object = run_batches(gpu, targets, with_object, eye, overrides, 2, seed++, &read);
            not_finite += as_object.not_finite;
            tally->check(read && as_object.emitters == static_cast<uint32_t>(kDiscSides) &&
                             within(as_object.mean, as_object.sigma, got.mean, got.sigma),
                         text::format("the disc as an object's relit mesh (its own space, moved by its transform): %u "
                                      "emitting triangles listed, its direct light %.5f +- %.5f, as the world's %.5f",
                                      as_object.emitters, as_object.mean, as_object.sigma, got.mean));
        } else {
            tally->check(false, "the object's disc made");
        }
        release_mesh(gpu, &object_disc);
        // (round EMIT - an emitter's faces) the disc drawn one-sided: wound with its front up, away from the floor
        // (rt::kFrontFaceSign - the disc as made), it sends nothing down; turned over, its front facing the floor, it
        // sends the floor what the two-sided disc does (its luminance from the one face)
        constexpr uint32_t kOneSidedSeed = 0xE3171000U;
        for (const bool facing : {false, true}) {
            std::vector<Triangle> one_sided = disc_triangles();
            if (facing) {
                for (Triangle& tri : one_sided) {
                    std::swap(std::get<1>(tri.corners), std::get<2>(tri.corners));
                }
            }
            SceneMesh disc;
            if (!make_mesh(gpu, one_sided, true, &disc)) {
                tally->check(false, "the one-sided disc made");
                continue;
            }
            rt::Material surface;
            surface.colour = {1.0F, 1.0F, 1.0F, 1.0F};
            surface.flags = rt::kOpaque;
            rt::ray_tracing()->set_material(disc.material, surface);
            const std::array<SceneMesh, 2> with_disc{meshes.front(), disc};
            // (their own seeds: the later cases' sequences as before the round)
            const Measured lit = run_batches(gpu, targets, with_disc, eye, overrides, 2, kOneSidedSeed + (facing ? 1U : 0U),
                                             &read);
            not_finite += lit.not_finite;
            tally->check(read && (facing ? within(lit.mean, lit.sigma, got.mean, got.sigma) : lit.mean < 0.01 * got.mean),
                         text::format("the disc one-sided, its front %s: the floor's direct light %.5f +- %.5f (%s; "
                                      "two-sided %.5f)",
                                      facing ? "facing the floor" : "up", lit.mean, lit.sigma,
                                      facing ? "its front sends: as two-sided" : "its back sends none", got.mean));
            release_mesh(gpu, &disc);
        }
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
                 text::format("MIS %.5f +- %.5f, the lights alone %.5f +- %.5f", mis.mean, mis.sigma, lights.mean,
                              lights.sigma));
    tally->check(read && within(mis.mean, mis.sigma, bsdf.mean, bsdf.sigma),
                 text::format("MIS %.5f +- %.5f, the BSDF alone %.5f +- %.5f (the disc's light counted once)", mis.mean,
                              mis.sigma, bsdf.mean, bsdf.sigma));
    tally->check(
        mis.sigma <= bsdf.sigma * 2.0,
        text::format("MIS's noise %.5f with 64 paths a pixel, the BSDF alone's %.5f with 256", mis.sigma, bsdf.sigma));
    // (the night's light, decomposed - `pt ref ... only <kinds>`) each kind of light alone: the emitter's and the sky's
    // runs add up to the whole, and a kind the scene hasn't counts nothing
    full.light_kinds = path::kLightKindEmitters;
    const Measured emit_only = run_batches(gpu, targets, both, eye, full, 2, seed++, &read);
    full.light_kinds = path::kLightKindSky;
    const Measured sky_only = run_batches(gpu, targets, both, eye, full, 2, seed++, &read);
    full.light_kinds = path::kLightKindLamps | path::kLightKindDisc;
    const Measured none_here = run_batches(gpu, targets, both, eye, full, 1, seed++, &read);
    full.light_kinds = path::kAllLightKinds;
    not_finite += emit_only.not_finite + sky_only.not_finite + none_here.not_finite;
    tally->check(read && within(mis.mean, mis.sigma, emit_only.mean + sky_only.mean,
                                std::hypot(emit_only.sigma, sky_only.sigma)) &&
                     emit_only.mean > 0.0 && sky_only.mean > 0.0 && none_here.mean == 0.0,
                 text::format("each kind alone: the emitter's %.5f + the sky's %.5f = %.5f, the whole %.5f +- %.5f; the "
                              "disc's and the lamps' (none here) %.5f",
                              emit_only.mean, sky_only.mean, emit_only.mean + sky_only.mean, mis.mean, mis.sigma,
                              none_here.mean));
    tally->check(path::light_kinds_of("moon,sky") == (path::kLightKindDisc | path::kLightKindSky) &&
                     path::light_kinds_of("emit,lamps,disc,sky") == path::kAllLightKinds &&
                     path::light_kinds_of("sky,stars") == 0 && path::light_kinds_text(path::kAllLightKinds) == "all" &&
                     path::light_kinds_text(path::kLightKindDisc | path::kLightKindLamps) == "disc,lamps",
                 "`only`'s words read (moon,sky - every kind - a word that isn't one refused) and written");
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
                 text::format("the device %.5f +- %.5f, the processor's twin %.5f +- %.5f", mis.mean, mis.sigma,
                              cpu.mean, cpu.sigma));

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
    tally->check(
        read && twice.mean > mis.mean + (kSigmas * std::hypot(twice.sigma, mis.sigma)),
        text::format("the same lamp not marked as its surface's: %.5f - counted twice (what the rule prevents)",
                     twice.mean));

    // 5. the light list: by power against stage 1's kinds evenly
    text::print("the light list by power against the kinds evenly (stage 1's):\n");
    full.lamps.clear();
    full.lamps_surfaced.clear();
    full.light_weights = lights::LightWeights::kKindsEvenly;
    const Measured even_mixed = run_batches(gpu, targets, both, eye, full, 2, seed++, &read);
    full.light_weights = lights::LightWeights::kPower;
    not_finite += even_mixed.not_finite;
    tally->check(read && within(mis.mean, mis.sigma, even_mixed.mean, even_mixed.sigma),
                 text::format("the floor, the disc and the sky: by power %.5f +- %.5f, evenly %.5f +- %.5f", mis.mean,
                              mis.sigma, even_mixed.mean, even_mixed.sigma));
    // the floor alone under 32 lamps: one bright overhead, the rest dim and far, intensities over four decades
    vk::PathOverrides many;
    many.force_material = true;
    many.roughness = 0.6F;
    many.f0 = 0.04F;
    many.albedo_scale = 0.7F;
    many.bounces = 1;
    many.samples_per_frame = 16;
    for (int i = 0; i < 32; ++i) {
        rt::Emitter one;
        const double angle = 2.0 * kPi * i / 32.0;
        const double distance = i == 0 ? 0.0 : 900.0 + (60.0 * i);
        one.position = {static_cast<float>(distance * std::cos(angle)), i == 0 ? 260.0F : 200.0F,
                        static_cast<float>(distance * std::sin(angle))};
        one.size = 1.0F;
        one.colour = {1.0F, 1.0F, 1.0F};
        one.intensity = i == 0 ? 20.0F : static_cast<float>(std::pow(10.0, -3.0 + (3.0 * (i % 8) / 7.0)));
        one.reach = 6000.0F;
        one.core = i == 0 ? 30.0F : 12.0F;
        many.lamps.push_back(one);
    }
    const Measured by_power = run_batches(gpu, targets, floor_only, eye, many, 2, seed++, &read);
    many.light_weights = lights::LightWeights::kKindsEvenly;
    const Measured evenly = run_batches(gpu, targets, floor_only, eye, many, 2, seed++, &read);
    not_finite += by_power.not_finite + evenly.not_finite;
    tally->check(read && within(by_power.mean, by_power.sigma, evenly.mean, evenly.sigma),
                 text::format("32 lamps: by power %.5f +- %.5f, evenly %.5f +- %.5f", by_power.mean, by_power.sigma,
                              evenly.mean, evenly.sigma));
    tally->check(
        read && by_power.sigma < evenly.sigma,
        text::format("32 lamps: by power the noise %.3g, evenly %.3g (the variance x %.3g)", by_power.sigma,
                     evenly.sigma,
                     evenly.sigma > 0.0 ? (by_power.sigma * by_power.sigma) / (evenly.sigma * evenly.sigma) : 0.0));
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
    check_sampling(&tally);
    check_alias(&tally);
    check_lamp_power(&tally);
    check_sky(&tally);
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
