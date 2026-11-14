// NOT ORIGINAL - thug2_vulkan_upscale_check.exe: the Vulkan Gpu's upscaling (gpu_vulkan/vulkan_upscale.h, its motion
// vectors gpu_vulkan/vulkan_motion.h) tried on this machine.
//
// The frames are made as render_pc's draw_viewport makes them: rt::view_scale() read, the world drawn into the
// viewport's scaled part with the projection jittered (rt::jitter_projection), rt::view_begins / opaque_view_drawn /
// world_view_drawn told, then presented.
//   - The jitter: the Halton (2, 3) offsets, in -0.5 - 0.5, their cycle's length, their mean near the pixel's centre.
//   - Motion vectors: a ground square that doesn't move (key 0), a quad that moves and turns between two frames (its
//     key's world transform), a "skinned" quad whose vertices the processor rewrote (the frame before's in another
//     buffer: Gpu::MotionSource::previous_vertices), the sky (no draw: from the depth) - the camera moved too. Every
//     clear-cut pixel's motion (the preparation pass's output, what the provider is given) against the processor's: the
//     pixel's ray through the jittered camera, the point it hits, where that point was the frame before (its object's
//     old transform or old vertices) seen by the old camera without jitter. Within 0.02 pixels.
//   - The upscaler on a still scene of thin bars: after 48 frames its output (the whole target) against a 4x4
//     supersampled render at the output's size (PSNR, noted); the jitter's sign handed over the other way does worse;
//     the output has no NaN and covers the output; two runs from a reset give the same bytes (FSR).
//   - DLSS (--dlss, an NVIDIA RTX device): the same round trip - it runs, the output is the right size, no NaN.
//   thug2_vulkan_upscale_check [--device N] [--display N] [--no-validation] [--dlss] [--quality performance|...]
//                              [--save <folder>] (the round trip's pictures as .ppm)
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan or no upscaler for it.
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
#include <fstream>
#include <ios>
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
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "platform/run_log.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
using Vec3 = std::array<double, 3>;
constexpr std::size_t kMatrixFloats = 16; // (4 x 4)
using Matrix = std::array<double, kMatrixFloats>; // D3D's row vectors: v' = v M

constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 360;
constexpr uint32_t kSuper = 4; // (the reference's samples a side)
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x042; // XYZ | DIFFUSE
constexpr uint32_t kSkyColour = 0xFF6080A0;
constexpr std::size_t kPixelBytes = 4;
constexpr double kNearZ = 2.0;
constexpr double kFarZ = 4000.0;
constexpr double kHalfFovTan = 0.6;
constexpr uint64_t kMoverKey = 77;
constexpr uint64_t kSkinKey = 99;
constexpr int kStillFrames = 48;
constexpr double kMotionTolerancePixels = 0.02;
constexpr double kAxisSlack = 0.2;

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
    bool dlss = false;
    bool grid = false;
    std::string quality = "quality";
    std::string save;
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--no-validation") {
            options.validation = false;
        } else if (argument == "--dlss") {
            options.dlss = true;
        } else if (argument == "--grid") {
            options.grid = true;
        } else if (argument == "--device" && has_value) {
            options.device = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--display" && has_value) {
            options.display = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--quality" && has_value) {
            options.quality = core::at(arguments, ++i);
        } else if (argument == "--save" && has_value) {
            options.save = core::at(arguments, ++i);
        }
    }
    return options;
}

int& validation_messages()
{
    static int count = 0;
    return count;
}

// The validation layer's messages about the vendor libraries' own work - AMD FSR's shaders declare 32-bit formats for
// internal images it makes 16-bit (its variables are named rw_...); NVIDIA NGX's copies into and layouts of its own
// resources (nv.ngx... / nv.sl...) - inside the signed DLLs, nothing this code hands them. Counted apart.
int& library_messages()
{
    static int count = 0;
    return count;
}

void print_line(const std::string& line)
{
    if (line.starts_with("validation ")) {
        // (FSR's shaders' format warnings; NVIDIA's NGX / Streamline's own objects - named nv.ngx... / nv.sl... - in
        // their own passes; the layer's notes that it stops repeating a message)
        const bool fsr = line.find("variable \"rw_") != std::string::npos && line.find("Format operand") != std::string::npos;
        const bool nvidia = line.find("[nv.ngx.") != std::string::npos || line.find("[nv.sl.") != std::string::npos;
        if (line.find("This VUID has now been reported") != std::string::npos) {
            return;
        }
        if (fsr || nvidia) {
            ++library_messages();
        } else {
            ++validation_messages();
        }
    }
    text::print("  %s\n", line.c_str());
}

// The renderer's log lines (get_log): printed as they come (the upscaler says which provider runs and why not).
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

// --- matrices -------------------------------------------------------------------------------------------------------

constexpr std::size_t kSide = 4;          // (a matrix's rows and columns)
constexpr std::size_t kAugmented = 2 * kSide;

double& cell(Matrix& matrix, std::size_t row, std::size_t column) { return core::at(matrix, (row * kSide) + column); }
double cell(const Matrix& matrix, std::size_t row, std::size_t column)
{
    return core::at(matrix, (row * kSide) + column);
}

Matrix identity()
{
    Matrix matrix{};
    for (std::size_t index = 0; index < kSide; ++index) {
        cell(matrix, index, index) = 1.0;
    }
    return matrix;
}

Matrix multiply(const Matrix& left, const Matrix& right)
{
    Matrix product{};
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            double sum = 0.0;
            for (std::size_t term = 0; term < kSide; ++term) {
                sum += cell(left, row, term) * cell(right, term, column);
            }
            cell(product, row, column) = sum;
        }
    }
    return product;
}

Matrix inverse(const Matrix& matrix)
{
    std::array<std::array<double, kAugmented>, kSide> rows{};
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(rows, row), column) = cell(matrix, row, column);
        }
        core::at(core::at(rows, row), kSide + row) = 1.0;
    }
    for (std::size_t column = 0; column < kSide; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < kSide; ++row) {
            if (std::fabs(core::at(core::at(rows, row), column)) > std::fabs(core::at(core::at(rows, pivot), column))) {
                pivot = row;
            }
        }
        std::swap(core::at(rows, pivot), core::at(rows, column));
        const double lead = core::at(core::at(rows, column), column);
        for (double& value : core::at(rows, column)) {
            value /= lead;
        }
        for (std::size_t row = 0; row < kSide; ++row) {
            if (row != column) {
                const double factor = core::at(core::at(rows, row), column);
                for (std::size_t term = 0; term < kAugmented; ++term) {
                    core::at(core::at(rows, row), term) -= factor * core::at(core::at(rows, column), term);
                }
            }
        }
    }
    Matrix result{};
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            cell(result, row, column) = core::at(core::at(rows, row), kSide + column);
        }
    }
    return result;
}

std::array<double, kSide> transform(const std::array<double, kSide>& vector, const Matrix& matrix)
{
    std::array<double, kSide> out{};
    for (std::size_t column = 0; column < kSide; ++column) {
        for (std::size_t term = 0; term < kSide; ++term) {
            core::at(out, column) += core::at(vector, term) * cell(matrix, term, column);
        }
    }
    return out;
}

Vec3 point(const Vec3& place, const Matrix& matrix)
{
    const std::array<double, kSide> out =
        transform({core::at(place, 0), core::at(place, 1), core::at(place, 2), 1.0}, matrix);
    return {core::at(out, 0) / core::at(out, 3), core::at(out, 1) / core::at(out, 3), core::at(out, 2) / core::at(out, 3)};
}

Vec3 minus(const Vec3& from, const Vec3& taken)
{
    return {core::at(from, 0) - core::at(taken, 0), core::at(from, 1) - core::at(taken, 1),
            core::at(from, 2) - core::at(taken, 2)};
}
Vec3 plus(const Vec3& one, const Vec3& other)
{
    return {core::at(one, 0) + core::at(other, 0), core::at(one, 1) + core::at(other, 1),
            core::at(one, 2) + core::at(other, 2)};
}
Vec3 scaled(const Vec3& vector, double factor)
{
    return {core::at(vector, 0) * factor, core::at(vector, 1) * factor, core::at(vector, 2) * factor};
}
double dot(const Vec3& one, const Vec3& other)
{
    return (core::at(one, 0) * core::at(other, 0)) + (core::at(one, 1) * core::at(other, 1)) +
           (core::at(one, 2) * core::at(other, 2));
}
Vec3 cross(const Vec3& one, const Vec3& other)
{
    return {(core::at(one, 1) * core::at(other, 2)) - (core::at(one, 2) * core::at(other, 1)),
            (core::at(one, 2) * core::at(other, 0)) - (core::at(one, 0) * core::at(other, 2)),
            (core::at(one, 0) * core::at(other, 1)) - (core::at(one, 1) * core::at(other, 0))};
}
Vec3 normalised(const Vec3& vector) { return scaled(vector, 1.0 / std::sqrt(dot(vector, vector))); }

// Direct3D's left-handed look-at and perspective (the game's: render_pc's choose_camera).
Matrix look_at(const Vec3& eye, const Vec3& target)
{
    const Vec3 forward = normalised(minus(target, eye));
    const Vec3 right = normalised(cross({0.0, 1.0, 0.0}, forward));
    const Vec3 up = cross(forward, right);
    Matrix view = identity();
    for (std::size_t row = 0; row < 3; ++row) {
        cell(view, row, 0) = core::at(right, row);
        cell(view, row, 1) = core::at(up, row);
        cell(view, row, 2) = core::at(forward, row);
    }
    cell(view, 3, 0) = -dot(right, eye);
    cell(view, 3, 1) = -dot(up, eye);
    cell(view, 3, 2) = -dot(forward, eye);
    return view;
}

Matrix perspective()
{
    const double aspect = static_cast<double>(kWidth) / kHeight;
    Matrix projection{};
    cell(projection, 0, 0) = 1.0 / kHalfFovTan;
    cell(projection, 1, 1) = aspect / kHalfFovTan;
    cell(projection, 2, 2) = kFarZ / (kFarZ - kNearZ);
    cell(projection, 2, 3) = 1.0;
    cell(projection, 3, 2) = -kNearZ * kFarZ / (kFarZ - kNearZ);
    return projection;
}

// A turn about y (radians), then a move.
Matrix placement(const Vec3& move, double turn)
{
    Matrix matrix = identity();
    cell(matrix, 0, 0) = std::cos(turn);
    cell(matrix, 0, 2) = -std::sin(turn);
    cell(matrix, 2, 0) = std::sin(turn);
    cell(matrix, 2, 2) = std::cos(turn);
    cell(matrix, 3, 0) = core::at(move, 0);
    cell(matrix, 3, 1) = core::at(move, 1);
    cell(matrix, 3, 2) = core::at(move, 2);
    return matrix;
}

using Floats16 = std::array<float, kSide * kSide>;
Floats16 floats(const Matrix& matrix)
{
    Floats16 out{};
    for (std::size_t index = 0; index < out.size(); ++index) {
        core::at(out, index) = static_cast<float>(core::at(matrix, index));
    }
    return out;
}

using Rows = std::array<std::array<float, kSide>, kSide>;
Rows rows_of(const Matrix& matrix)
{
    Rows rows{};
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(rows, row), column) = static_cast<float>(cell(matrix, row, column));
        }
    }
    return rows;
}

Matrix from_rows(const Rows& rows)
{
    Matrix matrix{};
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            cell(matrix, row, column) = core::at(core::at(rows, row), column);
        }
    }
    return matrix;
}

// --- the scene ------------------------------------------------------------------------------------------------------

struct ColourVertex {
    std::array<float, 3> position{};
    uint32_t colour = 0;
};

// A quad: four corners (in its own space), a colour.
struct Quad {
    std::array<Vec3, 4> corners{};
    uint32_t colour = 0;
};

constexpr Quad kGround{.corners = {{{-200.0, 0.0, -200.0}, {200.0, 0.0, -200.0}, {200.0, 0.0, 200.0}, {-200.0, 0.0, 200.0}}},
                       .colour = 0xFF909090};
constexpr Quad kMover{.corners = {{{-20.0, 0.0, 0.0}, {20.0, 0.0, 0.0}, {20.0, 30.0, 0.0}, {-20.0, 30.0, 0.0}}},
                      .colour = 0xFFC04040};
constexpr Quad kSkinBefore{.corners = {{{30.0, 0.0, -20.0}, {60.0, 0.0, -20.0}, {60.0, 25.0, -20.0}, {30.0, 25.0, -20.0}}},
                           .colour = 0xFF40C040};
constexpr Vec3 kSkinCentre{45.0, 12.5, -20.0};
constexpr double kSkinGrowth = 1.15;
constexpr Vec3 kSkinMove{3.0, 2.0, 0.0};

// The skinned quad this frame: the frame before's grown by 15% about its centre and moved.
Quad skin_now()
{
    Quad quad = kSkinBefore;
    for (Vec3& corner : quad.corners) {
        corner = plus(plus(kSkinCentre, scaled(minus(corner, kSkinCentre), kSkinGrowth)), kSkinMove);
    }
    return quad;
}

// The frame's state: the camera, the mover's place and turn.
struct Pose {
    Vec3 eye{};
    Vec3 mover{};
    double turn = 0.0;
};
constexpr Pose kBefore{.eye = {0.0, 60.0, -160.0}, .mover = {-10.0, 0.0, 10.0}, .turn = 0.0};
constexpr Pose kNow{.eye = {8.0, 62.0, -150.0}, .mover = {5.0, 0.0, 12.0}, .turn = 0.2};
constexpr Vec3 kLookAt{0.0, 10.0, 0.0};

std::vector<ColourVertex> triangles(const Quad& quad, const Matrix& world)
{
    std::vector<ColourVertex> out;
    for (const std::size_t index : {0U, 1U, 2U, 0U, 2U, 3U}) {
        const Vec3 corner = point(core::at(quad.corners, index), world);
        out.push_back({.position = {static_cast<float>(core::at(corner, 0)), static_cast<float>(core::at(corner, 1)),
                                    static_cast<float>(core::at(corner, 2))},
                       .colour = quad.colour});
    }
    return out;
}

// The round trip's scene: a lattice of thin bars over the ground (the detail an upscaler must rebuild).
constexpr std::array<uint32_t, 3> kBarColours{0xFFFFFFFFU, 0xFF202020U, 0xFFE0C040U};
constexpr std::array<uint32_t, 2> kRailColours{0xFF3050D0U, 0xFFF0F0F0U};
std::vector<ColourVertex> bars()
{
    constexpr int kBars = 90;
    constexpr double kLeft = -90.0;
    constexpr double kSpacing = 2.0;
    constexpr double kBarWidth = 0.7;
    constexpr double kBarDepth = -30.0;
    constexpr double kBarHeight = 40.0;
    constexpr int kHeightSteps = 7;
    constexpr double kRailFirst = 1.0;
    constexpr double kRailSpacing = 0.6;
    constexpr double kRailHeight = 0.25;
    constexpr double kRailDepth = -31.0;
    std::vector<ColourVertex> out;
    for (int index = 0; index < kBars; ++index) {
        const double left = kLeft + (kSpacing * index);
        const double top = kBarHeight + (index % kHeightSteps);
        const Quad bar{.corners = {{{left, 0.0, kBarDepth},
                                    {left + kBarWidth, 0.0, kBarDepth},
                                    {left + kBarWidth, top, kBarDepth},
                                    {left, top, kBarDepth}}},
                       .colour = core::at(kBarColours, static_cast<std::size_t>(index) % kBarColours.size())};
        const std::vector<ColourVertex> made = triangles(bar, identity());
        out.insert(out.end(), made.begin(), made.end());
        // (and as many lying bars across them, a little in front: detail along y)
        const double bottom = kRailFirst + (kRailSpacing * index);
        const Quad rail{.corners = {{{kLeft, bottom, kRailDepth},
                                     {-kLeft, bottom, kRailDepth},
                                     {-kLeft, bottom + kRailHeight, kRailDepth},
                                     {kLeft, bottom + kRailHeight, kRailDepth}}},
                        .colour = core::at(kRailColours, static_cast<std::size_t>(index) % kRailColours.size())};
        const std::vector<ColourVertex> across = triangles(rail, identity());
        out.insert(out.end(), across.begin(), across.end());
    }
    return out;
}

struct Buffers {
    render::GpuVertexBuffer* skin_before = nullptr;
    render::GpuVertexBuffer* skin_now = nullptr;
};

render::GpuVertexBuffer* make_buffer(render::Gpu& gpu, const std::vector<ColourVertex>& vertices)
{
    render::GpuVertexBuffer* buffer = nullptr;
    const auto bytes = static_cast<uint32_t>(vertices.size() * sizeof(ColourVertex));
    if (gpu.create_vertex_buffer(bytes, value::kUsageWriteOnly, kFvf, value::kPoolManaged, &buffer) != value::kResultOk) {
        return nullptr;
    }
    void* data = nullptr;
    if (gpu.lock_vertex_buffer(buffer, 0, 0, &data, 0) != value::kResultOk) {
        return nullptr;
    }
    std::memcpy(data, vertices.data(), bytes);
    gpu.unlock_vertex_buffer(buffer);
    return buffer;
}

void common_states(render::Gpu& gpu)
{
    using render::GpuRenderState;
    using render::GpuStageState;
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
}

// --- a frame as draw_viewport makes it ------------------------------------------------------------------------------

struct Targets {
    GpuSurface* colour = nullptr;
    GpuSurface* depth = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

bool make_targets(render::Gpu& gpu, uint32_t width, uint32_t height, Targets* out)
{
    out->width = width;
    out->height = height;
    const render::GpuSurfaceDesc colour{.width = width,
                                        .height = height,
                                        .format = value::kFormatX8R8G8B8,
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

enum class SceneKind : uint8_t { kMotion, kBars };

// What a frame drew with: the rectangle, the jitter, the (jittered) projection.
struct Drawn {
    std::array<uint32_t, 4> rect{};
    std::array<float, 2> jitter{};
    Matrix view{};
    Matrix projection{}; // jittered
};

Drawn draw_frame(render::Gpu& gpu, const Targets& targets, SceneKind kind, const Pose& pose, const Buffers& buffers,
                 bool skin_moved)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = targets.width, .height = targets.height, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kSkyColour, 1.0F, 0);
    const std::array<uint32_t, 4> output{0, 0, targets.width, targets.height};
    const render::rt::ViewScale scale = render::rt::view_scale();
    Drawn drawn;
    drawn.rect = scale.on ? render::rt::scaled_rect(output, scale.scale) : output;
    drawn.jitter = scale.on ? scale.jitter : std::array<float, 2>{};
    drawn.view = look_at(pose.eye, kLookAt);
    Rows projection = rows_of(perspective());
    if (scale.on) {
        render::rt::jitter_projection(&projection, drawn.jitter, core::at(drawn.rect, 2), core::at(drawn.rect, 3));
    }
    drawn.projection = from_rows(projection);
    gpu.set_viewport({.x = 0,
                      .y = 0,
                      .width = core::at(drawn.rect, 2),
                      .height = core::at(drawn.rect, 3),
                      .min_z = 0.0F,
                      .max_z = 1.0F});
    const Floats16 view_floats = floats(drawn.view);
    const Floats16 projection_floats = floats(drawn.projection);
    const Floats16 identity_floats = floats(identity());
    gpu.set_transform(render::GpuTransform::kWorld, identity_floats.data());
    gpu.set_transform(render::GpuTransform::kView, view_floats.data());
    gpu.set_transform(render::GpuTransform::kProjection, projection_floats.data());
    render::rt::ViewDrawn view;
    view.camera.view = rows_of(drawn.view);
    view.camera.projection = projection;
    view.rect = drawn.rect;
    view.output_rect = output;
    view.jitter = drawn.jitter;
    view.depth_range = {0.0F, 1.0F};
    render::rt::view_begins(view);
    common_states(gpu);
    // the ground (the level's: key 0)
    gpu.set_motion_source({});
    std::vector<ColourVertex> ground = triangles(kGround, identity());
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, ground.data(), sizeof(ColourVertex));
    if (kind == SceneKind::kMotion) {
        // the mover (its key's world transform)
        const Floats16 mover = floats(placement(pose.mover, pose.turn));
        gpu.set_transform(render::GpuTransform::kWorld, mover.data());
        gpu.set_motion_source({.key = kMoverKey});
        std::vector<ColourVertex> quad = triangles(kMover, identity());
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, quad.data(), sizeof(ColourVertex));
        gpu.set_transform(render::GpuTransform::kWorld, identity_floats.data());
        // the skinned quad: this frame's vertices, the frame before's beside them
        gpu.set_motion_source({.key = kSkinKey, .previous_vertices = skin_moved ? buffers.skin_before : nullptr});
        gpu.set_stream_source(0, skin_moved ? buffers.skin_now : buffers.skin_before, 0, sizeof(ColourVertex));
        gpu.draw(render::GpuPrimitive::kTriangleList, 0, 2);
        gpu.set_motion_source({});
    } else {
        std::vector<ColourVertex> detail = bars();
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(detail.size() / 3), detail.data(),
                             sizeof(ColourVertex));
    }
    render::rt::opaque_view_drawn(view);
    render::rt::world_view_drawn(view);
    gpu.end_scene();
    gpu.present();
    return drawn;
}

// --- read-backs ----------------------------------------------------------------------------------------------------

bool read_floats(render::vulkan::Image* image, uint32_t channels, std::vector<float>* out)
{
    render::vulkan::Backend& state = render::vulkan::backend();
    const std::scoped_lock lock(state.mutex);
    if (!render::vulkan::finish_work(state)) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(image->extent.width) * image->extent.height;
    const bool half = image->format == VK_FORMAT_R16G16B16A16_SFLOAT;
    const std::size_t bytes = half ? sizeof(uint16_t) : sizeof(float);
    std::vector<uint8_t> raw(pixels * channels * bytes);
    if (!state.memory.read_image_level(image, 0, &raw)) {
        return false;
    }
    out->resize(pixels * channels);
    for (std::size_t index = 0; index < out->size(); ++index) {
        if (half) {
            const auto bits = static_cast<uint16_t>(core::at(raw, index * bytes) |
                                                    (core::at(raw, (index * bytes) + 1) << kBitsPerByte));
            core::at(*out, index) = render::vulkan::half_to_float(bits);
        } else {
            float number = 0.0F;
            std::memcpy(&number, std::span(raw).subspan(index * bytes, bytes).data(), bytes);
            core::at(*out, index) = number;
        }
    }
    return true;
}

bool read_colour(const Targets& targets, std::vector<uint8_t>* texels)
{
    return render::vulkan::read_surface(targets.colour, texels) &&
           texels->size() == static_cast<std::size_t>(targets.width) * targets.height * kPixelBytes;
}

// --- the processor's motion ----------------------------------------------------------------------------------------

struct Hit {
    int object = -1; // -1 the sky, 0 ground, 1 mover, 2 skin
    Vec3 previous{}; // where the point was the frame before (world)
};

// A ray (from its start along its direction) against a quad (world corners): the distance, and the point's place in
// the quad (two triangles' barycentrics) - Moller and Trumbore's test.
struct Ray {
    Vec3 start{};
    Vec3 along{};
};
bool intersect(const Ray& ray, const std::array<Vec3, 4>& corners, double* distance, std::array<double, 4>* weights)
{
    constexpr double kParallel = 1e-12;
    bool found = false;
    for (const std::array<std::size_t, 3> triangle :
         {std::array<std::size_t, 3>{0, 1, 2}, std::array<std::size_t, 3>{0, 2, 3}}) {
        const Vec3& first = core::at(corners, core::at(triangle, 0));
        const Vec3 edge1 = minus(core::at(corners, core::at(triangle, 1)), first);
        const Vec3 edge2 = minus(core::at(corners, core::at(triangle, 2)), first);
        const Vec3 normal_part = cross(ray.along, edge2);
        const double determinant = dot(edge1, normal_part);
        if (std::fabs(determinant) < kParallel) {
            continue;
        }
        const Vec3 offset = minus(ray.start, first);
        const double along1 = dot(offset, normal_part) / determinant;
        const Vec3 turned = cross(offset, edge1);
        const double along2 = dot(ray.along, turned) / determinant;
        const double travelled = dot(edge2, turned) / determinant;
        if (along1 < 0.0 || along2 < 0.0 || along1 + along2 > 1.0 || travelled <= 0.0 ||
            (found && travelled >= *distance)) {
            continue;
        }
        found = true;
        *distance = travelled;
        *weights = {};
        core::at(*weights, core::at(triangle, 0)) = 1.0 - along1 - along2;
        core::at(*weights, core::at(triangle, 1)) = along1;
        core::at(*weights, core::at(triangle, 2)) = along2;
    }
    return found;
}

Vec3 blend(const std::array<Vec3, 4>& corners, const std::array<double, 4>& weights)
{
    Vec3 out{};
    for (std::size_t corner = 0; corner < corners.size(); ++corner) {
        out = plus(out, scaled(core::at(corners, corner), core::at(weights, corner)));
    }
    return out;
}

std::array<Vec3, 4> world_corners(const Quad& quad, const Matrix& world)
{
    std::array<Vec3, 4> out{};
    for (std::size_t corner = 0; corner < out.size(); ++corner) {
        core::at(out, corner) = point(core::at(quad.corners, corner), world);
    }
    return out;
}

// What the ray through a point of the drawn rectangle (pixels: Direct3D 9's centres are whole numbers) of `drawn`
// hits, and where that was the frame before.
struct Spot {
    double x = 0.0;
    double y = 0.0;
};
Hit trace(const Drawn& drawn, Spot spot)
{
    const auto width = static_cast<double>(core::at(drawn.rect, 2));
    const auto height = static_cast<double>(core::at(drawn.rect, 3));
    const Matrix to_world = inverse(multiply(drawn.view, drawn.projection));
    const double across = (spot.x / width * 2.0) - 1.0;
    const double up = 1.0 - (spot.y / height * 2.0);
    const Vec3 nearest = point({across, up, 0.0}, to_world);
    const Vec3 farthest = point({across, up, 1.0}, to_world);
    const Ray ray{.start = nearest, .along = minus(farthest, nearest)};
    Hit hit;
    double best = std::numeric_limits<double>::infinity();
    std::array<double, 4> weights{};
    double distance = best;
    if (intersect(ray, world_corners(kGround, identity()), &distance, &weights) && distance < best) {
        best = distance;
        hit = {.object = 0, .previous = blend(world_corners(kGround, identity()), weights)};
    }
    distance = best;
    if (intersect(ray, world_corners(kMover, placement(kNow.mover, kNow.turn)), &distance, &weights) && distance < best) {
        best = distance;
        hit = {.object = 1, .previous = blend(world_corners(kMover, placement(kBefore.mover, kBefore.turn)), weights)};
    }
    distance = best;
    if (intersect(ray, skin_now().corners, &distance, &weights) && distance < best) {
        hit = {.object = 2, .previous = blend(kSkinBefore.corners, weights)};
    }
    if (hit.object < 0) {
        hit.previous = farthest; // (the sky: the depth's far plane, as the preparation pass reads it)
    }
    return hit;
}

// --- the checks ----------------------------------------------------------------------------------------------------

void check_jitter(Tally* tally)
{
    using render::vulkan::upscale_jitter;
    using render::vulkan::upscale_jitter_phases;
    // (Halton 2: 1/2, 1/4, 3/4...; Halton 3: 1/3, 2/3, 1/9...; each less a half)
    constexpr uint32_t kCycle = 8;
    constexpr float kHalf = 0.5F;
    constexpr float kExact = 1e-6F;
    const std::array<std::array<float, 2>, 2> expected{{{0.5F - kHalf, (1.0F / 3.0F) - kHalf},
                                                       {0.25F - kHalf, (2.0F / 3.0F) - kHalf}}};
    bool halton = true;
    for (uint32_t index = 0; index < expected.size(); ++index) {
        const std::array<float, 2> offset = upscale_jitter(index, kCycle);
        for (std::size_t axis = 0; axis < offset.size(); ++axis) {
            halton = halton && std::fabs(core::at(offset, axis) - core::at(core::at(expected, index), axis)) < kExact;
        }
    }
    tally->check(halton, "the jitter is Halton (2, 3) from its first point, centred on the pixel");
    // (ratio: phases)
    constexpr std::array<std::pair<float, uint32_t>, 4> kPhases{
        {{1.0F, kCycle}, {1.5F, 18}, {2.0F, 32}, {3.0F, 72}}};
    bool cycles = true;
    for (const auto& [ratio, count] : kPhases) {
        cycles = cycles && upscale_jitter_phases(ratio) == count;
    }
    tally->check(cycles, "the cycle: ceil(8 x ratio^2), at least 8 (8, 18, 32, 72)");
    constexpr float kPerformance = 2.0F;
    constexpr double kCentred = 0.02;
    const uint32_t phases = upscale_jitter_phases(kPerformance);
    double sum_x = 0.0;
    double sum_y = 0.0;
    bool inside = true;
    for (uint32_t index = 0; index < phases; ++index) {
        const std::array<float, 2> offset = upscale_jitter(index, phases);
        inside = inside && std::fabs(core::at(offset, 0)) <= kHalf && std::fabs(core::at(offset, 1)) <= kHalf;
        sum_x += core::at(offset, 0);
        sum_y += core::at(offset, 1);
    }
    tally->check(inside && std::fabs(sum_x / phases) < kCentred && std::fabs(sum_y / phases) < kCentred,
                 text::format("a cycle's offsets inside the pixel, their mean at its centre (%.4f, %.4f)", sum_x / phases,
                              sum_y / phases));
    tally->check(upscale_jitter(phases, phases) == upscale_jitter(0, phases), "the cycle repeats");
}

void check_motion(render::Gpu& gpu, const Targets& targets, const Buffers& buffers, Tally* tally)
{
    // two frames: the one before (its own frame before: itself), then the moved one
    draw_frame(gpu, targets, SceneKind::kMotion, kBefore, buffers, false);
    draw_frame(gpu, targets, SceneKind::kMotion, kBefore, buffers, false);
    const Drawn drawn = draw_frame(gpu, targets, SceneKind::kMotion, kNow, buffers, true);
    const render::vulkan::UpscaleImages images = render::vulkan::upscale_images();
    std::vector<float> motion;
    tally->check(images.motion != nullptr && read_floats(images.motion, 4, &motion), "the motion read back");
    if (motion.empty()) {
        return;
    }
    const uint32_t stride = images.motion->extent.width;
    const auto width = static_cast<double>(core::at(drawn.rect, 2));
    const auto height = static_cast<double>(core::at(drawn.rect, 3));
    const Matrix before_clip = multiply(look_at(kBefore.eye, kLookAt), perspective());
    constexpr double kClearCut = 1.5;     // (pixels: the same object this far off each way)
    constexpr double kShareOfMove = 0.01; // (the error allowed: 1% of a longer move)
    constexpr std::size_t kChannels = 4;
    std::array<int, 4> counted{};
    std::array<double, 4> worst{};
    std::array<int, 4> moved{};
    for (uint32_t row = 0; row < core::at(drawn.rect, 3); ++row) {
        for (uint32_t column = 0; column < core::at(drawn.rect, 2); ++column) {
            // (Direct3D 9's pixel centres: the Vulkan Gpu moves the draws half a pixel onto Vulkan's)
            const Spot spot{.x = static_cast<double>(column), .y = static_cast<double>(row)};
            const Hit hit = trace(drawn, spot);
            bool clear = true;
            for (const std::array<double, 2> step :
                 {std::array<double, 2>{1.0, 0.0}, std::array<double, 2>{-1.0, 0.0}, std::array<double, 2>{0.0, 1.0},
                  std::array<double, 2>{0.0, -1.0}}) {
                const Spot near_by{.x = spot.x + (kClearCut * core::at(step, 0)), .y = spot.y + (kClearCut * core::at(step, 1))};
                clear = clear && trace(drawn, near_by).object == hit.object;
            }
            if (!clear) {
                continue;
            }
            const std::array<double, kSide> clip = transform(
                {core::at(hit.previous, 0), core::at(hit.previous, 1), core::at(hit.previous, 2), 1.0}, before_clip);
            const double before_u = ((core::at(clip, 0) / core::at(clip, 3)) * 0.5) + 0.5;
            const double before_v = 0.5 - ((core::at(clip, 1) / core::at(clip, 3)) * 0.5);
            const double now_u = (spot.x - core::at(drawn.jitter, 0)) / width;
            const double now_v = (spot.y - core::at(drawn.jitter, 1)) / height;
            const double expect_x = (before_u - now_u) * width;
            const double expect_y = (before_v - now_v) * height;
            const std::size_t at = ((static_cast<std::size_t>(row) * stride) + column) * kChannels;
            const double got_x = static_cast<double>(core::at(motion, at)) * width;
            const double got_y = static_cast<double>(core::at(motion, at + 1)) * height;
            const auto slot = static_cast<std::size_t>(hit.object) + 1; // (the sky's -1 first)
            const double error = std::hypot(got_x - expect_x, got_y - expect_y);
            const double length = std::hypot(expect_x, expect_y);
            ++core::at(counted, slot);
            core::at(worst, slot) =
                std::max(core::at(worst, slot), error / std::max(1.0, length * kShareOfMove / kMotionTolerancePixels));
            if (length > 1.0) {
                ++core::at(moved, slot);
            }
        }
    }
    const std::array<const char*, 4> names{"the sky (from the depth)", "the ground (key 0: the camera's motion)",
                                           "the moving quad (its key's world transform)",
                                           "the skinned quad (the frame before's vertices)"};
    constexpr int kLeastPixels = 200;
    for (std::size_t object = 0; object < names.size(); ++object) {
        tally->check(core::at(counted, object) > kLeastPixels && core::at(moved, object) > core::at(counted, object) / 2 &&
                         core::at(worst, object) <= kMotionTolerancePixels,
                     text::format("motion of %s: %d clear-cut pixels (%d moving more than a pixel), worst error %.4f px "
                                  "(1%% of the move allowed above 2 px)",
                                  core::at(names, object), core::at(counted, object), core::at(moved, object),
                                  core::at(worst, object)));
    }
}

// The peak signal-to-noise ratio of a picture against a reference (dB; 99 when they are the same).
double psnr(const std::vector<uint8_t>& picture, const std::vector<uint8_t>& reference)
{
    constexpr std::size_t kAlphaByte = 3;
    constexpr double kSame = 99.0;
    constexpr double kDecibels = 10.0;
    constexpr double kPeak = 255.0;
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t index = 0; index < picture.size() && index < reference.size(); ++index) {
        if ((index % kPixelBytes) == kAlphaByte) {
            continue;
        }
        const double difference = static_cast<double>(core::at(picture, index)) - core::at(reference, index);
        sum += difference * difference;
        ++count;
    }
    const double mean_square = sum / static_cast<double>(std::max<std::size_t>(count, 1));
    return mean_square <= 0.0 ? kSame : kDecibels * std::log10(kPeak * kPeak / mean_square);
}

void save_ppm(const Options& options, const char* name, const std::vector<uint8_t>& bgrx, uint32_t width, uint32_t height)
{
    if (options.save.empty()) {
        return;
    }
    std::ofstream file(options.save + "/" + name, std::ios::binary);
    if (!file) {
        return;
    }
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::size_t at = 0; at + kPixelBytes <= bgrx.size(); at += kPixelBytes) {
        const std::array<char, 3> rgb{static_cast<char>(core::at(bgrx, at + 2)), static_cast<char>(core::at(bgrx, at + 1)),
                                      static_cast<char>(core::at(bgrx, at))};
        file.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    }
}

// The still scene upscaled for kStillFrames frames: the last frame's whole target.
bool upscaled_still(render::Gpu& gpu, const Targets& targets, const Buffers& buffers, std::vector<uint8_t>* out)
{
    for (int frame = 0; frame < kStillFrames; ++frame) {
        draw_frame(gpu, targets, SceneKind::kBars, kBefore, buffers, false);
    }
    return read_colour(targets, out);
}

void check_round_trip(render::Gpu& gpu, const Options& options, const Targets& targets, const Buffers& buffers,
                      render::vulkan::UpscaleSettings settings, Tally* tally)
{
    // the reference: the scene at 4 x 4 the size, averaged down (no upscaler)
    render::vulkan::set_upscale_settings({});
    std::vector<uint8_t> reference(static_cast<std::size_t>(kWidth) * kHeight * kPixelBytes);
    std::vector<uint8_t> native;
    {
        Targets large;
        const bool made = make_targets(gpu, kWidth * kSuper, kHeight * kSuper, &large);
        std::vector<uint8_t> texels;
        const bool drawn =
            made && (draw_frame(gpu, large, SceneKind::kBars, kBefore, buffers, false), read_colour(large, &texels));
        tally->check(drawn, "the 4 x 4 supersampled reference drawn");
        if (drawn) {
            constexpr std::size_t kColourBytes = 3;
            constexpr std::size_t kBigWidth = static_cast<std::size_t>(kWidth) * kSuper;
            for (std::size_t row = 0; row < kHeight; ++row) {
                for (std::size_t column = 0; column < kWidth; ++column) {
                    for (std::size_t channel = 0; channel < kColourBytes; ++channel) {
                        uint32_t sum = 0;
                        for (std::size_t sub_row = 0; sub_row < kSuper; ++sub_row) {
                            for (std::size_t sub_column = 0; sub_column < kSuper; ++sub_column) {
                                const std::size_t big =
                                    ((((row * kSuper) + sub_row) * kBigWidth) + (column * kSuper) + sub_column);
                                sum += core::at(texels, (big * kPixelBytes) + channel);
                            }
                        }
                        core::at(reference, (((row * kWidth) + column) * kPixelBytes) + channel) =
                            static_cast<uint8_t>((sum + (kSuper * kSuper / 2)) / (kSuper * kSuper));
                    }
                }
            }
        }
        release_targets(gpu, large);
        draw_frame(gpu, targets, SceneKind::kBars, kBefore, buffers, false);
        read_colour(targets, &native);
    }
    save_ppm(options, "reference.ppm", reference, kWidth, kHeight);
    save_ppm(options, "native.ppm", native, kWidth, kHeight);
    const double native_psnr = psnr(native, reference);
    // upscaled
    render::vulkan::set_upscale_settings(settings);
    std::vector<uint8_t> upscaled;
    const bool ran = upscaled_still(gpu, targets, buffers, &upscaled);
    const char* name = render::vulkan::upscale_provider_name();
    const std::string provider = name != nullptr ? name : "none";
    tally->check(ran && render::vulkan::upscale_passes() > 0, text::format("%s ran %d frames", provider.c_str(), kStillFrames));
    save_ppm(options, (provider + "_upscaled.ppm").c_str(), upscaled, kWidth, kHeight);
    const double upscaled_psnr = psnr(upscaled, reference);
    const render::rt::ViewScale scale = render::rt::view_scale();
    tally->check(scale.on, text::format("the view drawn at %.0f%% of the output", static_cast<double>(scale.scale) * 100.0));
    // the output: no NaN, the whole output covered (the reactive and output images read too)
    std::vector<float> output;
    const render::vulkan::UpscaleImages images = render::vulkan::upscale_images();
    bool finite = images.output != nullptr && read_floats(images.output, 4, &output);
    for (const float number : output) {
        finite = finite && std::isfinite(number);
    }
    tally->check(finite && images.output->extent.width == kWidth && images.output->extent.height == kHeight,
                 text::format("%s's output %ux%u, no NaN or infinity", provider.c_str(), kWidth, kHeight));
    // (a lattice of thin bars: aliased at any size - the upscaler, from fewer pixels, at least near the full size's own)
    tally->check(upscaled_psnr > native_psnr - 1.0,
                 text::format("%s at %.0f%%: PSNR %.2f dB against the 4x4 reference (the full size without AA %.2f dB)",
                              provider.c_str(), static_cast<double>(scale.scale) * 100.0, upscaled_psnr, native_psnr));
    if (provider == "FSR") {
        // the same again from a reset: the same bytes
        render::vulkan::set_upscale_settings({});
        render::vulkan::set_upscale_settings(settings);
        std::vector<uint8_t> again;
        upscaled_still(gpu, targets, buffers, &again);
        // (at the output's own size - native AA - FSR's passes on the AMD GPU here aren't bit-exact run to run: noted)
        const bool same = again == upscaled;
        if (scale.scale < 1.0F || same) {
            tally->check(same, "FSR: two runs of the same frames give the same bytes");
        } else {
            text::print("  note FSR at 100%%: two runs differ (PSNR between them %.2f dB)\n", psnr(again, upscaled));
        }
    }
    // the offset made the other ways (the jitter's sign the other way, no pixel-centre alignment, the alignment the
    // other way): each worse
    const render::vulkan::UpscaleJitterTerms chosen = render::vulkan::upscale_jitter_terms();
    const auto negated = [](std::array<float, 2> values, std::size_t axis) {
        core::at(values, axis) = -core::at(values, axis);
        return values;
    };
    struct Alternative {
        const char* name = nullptr;
        render::vulkan::UpscaleJitterTerms terms;
        bool alignment = false;
        double slack = 0.0; // (dB the alternative may come within: one axis' sign matters little where the picture's
                            // detail lies mostly along the other)
    };
    const std::array<Alternative, 5> alternatives{{
        {.name = "the jitter reversed",
         .terms = {.sign = negated(negated(chosen.sign, 0), 1), .align = chosen.align}},
        {.name = "the jitter's x reversed",
         .terms = {.sign = negated(chosen.sign, 0), .align = chosen.align},
         .slack = kAxisSlack},
        {.name = "the jitter's y reversed",
         .terms = {.sign = negated(chosen.sign, 1), .align = chosen.align},
         .slack = kAxisSlack},
        {.name = "no pixel-centre alignment", .terms = {.sign = chosen.sign, .align = {0.0F, 0.0F}}, .alignment = true},
        {.name = "the alignment reversed",
         .terms = {.sign = chosen.sign, .align = {-core::at(chosen.align, 0), -core::at(chosen.align, 1)}},
         .alignment = true},
    }};
    if (options.grid) {
        // every combination's PSNR (finding a library's convention)
        for (const float sign_x : {1.0F, -1.0F}) {
            for (const float sign_y : {1.0F, -1.0F}) {
                for (const float align : {-1.0F, 0.0F, 1.0F}) {
                    const render::vulkan::UpscaleJitterTerms terms{.sign = {sign_x, sign_y}, .align = {align, align}};
                    render::vulkan::set_upscale_jitter_terms(&terms);
                    render::vulkan::set_upscale_settings({});
                    render::vulkan::set_upscale_settings(settings);
                    std::vector<uint8_t> tried;
                    upscaled_still(gpu, targets, buffers, &tried);
                    text::print("  grid: sign %+.0f %+.0f align %+.0f: %.2f dB\n", static_cast<double>(sign_x),
                                static_cast<double>(sign_y), static_cast<double>(align), psnr(tried, reference));
                }
            }
        }
    }
    for (const Alternative& alternative : alternatives) {
        if (alternative.alignment && scale.scale >= 1.0F) {
            continue; // (drawn at the output's size: no alignment to make)
        }
        render::vulkan::set_upscale_jitter_terms(&alternative.terms);
        render::vulkan::set_upscale_settings({});
        render::vulkan::set_upscale_settings(settings);
        std::vector<uint8_t> tried;
        upscaled_still(gpu, targets, buffers, &tried);
        const double other_psnr = psnr(tried, reference);
        tally->check(upscaled_psnr > other_psnr - alternative.slack,
                     text::format("%s: the offset as given beats %s (%.2f against %.2f dB)", provider.c_str(),
                                  alternative.name, upscaled_psnr, other_psnr));
    }
    render::vulkan::set_upscale_jitter_terms(nullptr);
    render::vulkan::set_upscale_settings({});
}

enum class Opened : uint8_t { kYes, kNoDevice };

Opened open_device(const Options& options, SDL_Window* window, const render::vulkan::UpscaleSettings& settings)
{
    render::vulkan::ContextOptions context_options{.validation = options.validation,
                                                   .device_index = options.device,
                                                   .on_message = print_line};
    render::vulkan::set_upscale_settings(settings); // (before the device: its depth made samplable)
    context_options.additions = render::vulkan::upscale_context_additions();
    render::vulkan::set_context_options(context_options, window);
    render::GpuContext& context = render::vulkan::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return Opened::kNoDevice;
    }
    const render::vulkan::Backend& state = render::vulkan::backend();
    text::print("validation layer: %s\n", state.context.validation_on() ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWidth,
                       .height = kHeight,
                       .windowed = true,
                       .vsync = false,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24S8});
    const long made = context.create_device(
        {.width = kWidth, .height = kHeight, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
    if (made != value::kResultOk) {
        text::print("create_device failed (%ld)\n", made);
        context.shutdown();
        return Opened::kNoDevice;
    }
    context.mark_ready();
    text::print("device: %s\n", context.adapter().description.data());
    return Opened::kYes;
}

int run(const Options& options, SDL_Window* window)
{
    render::vulkan::UpscaleSettings settings{.upscaler = options.dlss ? render::vulkan::Upscaler::kDlss
                                                                      : render::vulkan::Upscaler::kFsr,
                                             .quality = render::vulkan::UpscaleQuality::kQuality};
    if (!render::vulkan::parse_upscale_quality(options.quality, &settings.quality)) {
        text::print("--quality %s isn't a quality\n", options.quality.c_str());
        return 1;
    }
    if (open_device(options, window, settings) == Opened::kNoDevice) {
        return render::vulkan::loader_version() == 0 ? 2 : 1;
    }
    Tally tally;
    check_jitter(&tally);
    render::Gpu& gpu = render::vulkan::gpu();
    Targets targets;
    Buffers buffers;
    const bool made = make_targets(gpu, kWidth, kHeight, &targets);
    buffers.skin_before = make_buffer(gpu, triangles(kSkinBefore, identity()));
    buffers.skin_now = make_buffer(gpu, triangles(skin_now(), identity()));
    tally.check(made && buffers.skin_before != nullptr && buffers.skin_now != nullptr, "the targets and buffers made");
    int result = 1;
    if (made) {
        // is there an upscaler for this device?
        draw_frame(gpu, targets, SceneKind::kMotion, kBefore, buffers, false);
        const char* provider = render::vulkan::upscale_provider_name();
        text::print("upscaler: %s\n", provider != nullptr ? provider : "none");
        if (provider == nullptr) {
            text::print("no upscaler runs on this device in this build - nothing more to check\n");
            result = 2;
        } else {
            check_motion(gpu, targets, buffers, &tally);
            check_round_trip(gpu, options, targets, buffers, settings, &tally);
        }
    }
    for (render::GpuVertexBuffer* buffer : {buffers.skin_before, buffers.skin_now}) {
        if (buffer != nullptr) {
            gpu.release_vertex_buffer(buffer);
        }
    }
    release_targets(gpu, targets);
    render::vulkan::set_upscale_settings({});
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::finish_work(state);
    }
    render::vulkan::gpu_context().shutdown();
    tally.check(validation_messages() == 0,
                text::format("validation messages: %d (and %d inside the vendor libraries' own passes)",
                             validation_messages(), library_messages()));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    if (result == 2 && tally.failed == 0) {
        return 2;
    }
    return tally.failed == 0 ? 0 : 1;
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

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_upscale_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    if (!render::vulkan::open_loader()) {
        text::print("no Vulkan loader (vulkan-1.dll) for this process\n");
        return 2;
    }
    set_log_listener(print_log_line);
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        text::print("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int position = window_position(options.display);
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan upscaling check", static_cast<int>(kWidth), static_cast<int>(kHeight),
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
