// NOT ORIGINAL - thug2_vulkan_grass_check.exe: the grass (engine/gfx/grass.h's field of blades, gpu_vulkan/vulkan_grass.h's
// pass) tried on this machine.
//
// A synthetic lawn: a 400 x 400 square of grass faces on a grey ground (a face of its own under all of it, at the same
// height: coplanar ground doesn't cover grass), a box standing on it (its top 40 units up, its sides), a steep grass bank
// and a dried patch; the ground drawn as pre-transformed quads with the depth of the view's projection - the lawn's left
// half green, its right half straw. Cases:
//   - the field: the same faces give the same blades byte for byte; the count the area x the density (within 3%); every
//     root on a grass face, none under the box (the roots it covers counted), none on the steep bank, the dried ones
//     flagged; each tile's blades in rank order;
//   - the pass (three frames: a blade is drawn once its colour has been seen): only pixels over the lawn change (a ray
//     through each changed pixel meets the lawn, outside the box's footprint, below the tallest blade); most of the
//     lawn's inside is covered; the blades are the ground's colour - green over the green half, straw over the straw
//     half; the same picture twice from a new field; every pixel finite with not-a-number and infinities in the ground;
//   - the shapes: every blade's tip and height on the device against the processor's twin (grass_blade_shape);
//   - the level of detail: the share kept falls with the distance, nothing is drawn or tall beyond the distance, full
//     height near; fewer blades drawn from further away;
//   - the push: a skater on the lawn bends every blade within its reach straight away from it, more than without;
//   - four samples: the pass runs; the validation layer (with synchronization validation) says nothing all along;
//   - the cost at 2560x1440 with the lawn filling the view (printed).
//   thug2_vulkan_grass_check [--device N] [--display N] [--no-validation]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan for this process.
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
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/grass.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_grass.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuRenderState;
using render::GpuStageState;
using render::GpuSurface;
namespace value = render::gpu_value;
namespace vk = render::vulkan;
namespace grass = render::grass;
using Vec3 = std::array<float, 3>;
using Matrix = std::array<std::array<float, 4>, 4>;

constexpr int kWindowWidth = 256;
constexpr int kWindowHeight = 128;
constexpr uint32_t kWidth = 320;
constexpr uint32_t kHeight = 200;
constexpr int kBitsPerByte = 8;
constexpr std::size_t kHalfPixelBytes = 8;
constexpr std::size_t kChannels = 3;
constexpr uint32_t kHighByte = 8;
constexpr uint16_t kHalfSign = 0x8000;
constexpr uint32_t kFvf = 0x044; // XYZRHW | DIFFUSE
constexpr uint32_t kFourSamples = 4;
constexpr float kHalf = 0.5F;
constexpr int kLearningPasses = 3; // (a blade's colour seen three times: fully known)

// The view (Direct3D's left-handed projection, row vectors): near 1, far 10000, a vertical scale of 1.5.
constexpr float kNear = 1.0F;
constexpr float kFar = 10000.0F;
constexpr float kVerticalScale = 1.5F;
constexpr Vec3 kEye{0.0F, 120.0F, -250.0F};
constexpr Vec3 kLookAt{0.0F, 0.0F, 250.0F};

// The lawn, the box on it, the ground around.
constexpr float kLawnLeft = -200.0F;
constexpr float kLawnRight = 200.0F;
constexpr float kLawnNear = 100.0F;
constexpr float kLawnFar = 500.0F;
constexpr float kBoxLeft = -40.0F;
constexpr float kBoxRight = 40.0F;
constexpr float kBoxNear = 260.0F;
constexpr float kBoxFar = 340.0F;
constexpr float kBoxTop = 40.0F;
constexpr float kGroundSide = 900.0F;
constexpr float kGroundNear = -60.0F;
constexpr float kGroundFar = 1800.0F;
constexpr float kBankLeft = 300.0F; // (a steep grass bank, off to the side)
constexpr float kBankRight = 340.0F;
constexpr float kBankRise = 200.0F;
constexpr float kDriedLeft = -400.0F; // (a dried patch, behind the camera)
constexpr float kDriedRight = -300.0F;
constexpr float kDriedNear = -700.0F;
constexpr float kDriedFar = -600.0F;
constexpr uint32_t kGreenColour = 0xFF40A030;
constexpr uint32_t kStrawColour = 0xFFA08C22;
constexpr uint32_t kGreyColour = 0xFF808080;

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

float from_half(uint16_t bits)
{
    constexpr uint32_t kMantissaBits = 10;
    constexpr uint32_t kExponentMask = 0x1F;
    constexpr uint32_t kMantissaMask = 0x3FF;
    constexpr uint32_t kImplicitOne = 0x400;
    constexpr int kSmallestPower = -24;
    constexpr int kPowerBias = 25;
    const uint32_t exponent = (static_cast<uint32_t>(bits) >> kMantissaBits) & kExponentMask;
    const uint32_t mantissa = bits & kMantissaMask;
    float magnitude = 0.0F;
    if (exponent == 0) {
        magnitude = std::ldexp(static_cast<float>(mantissa), kSmallestPower);
    } else if (exponent == kExponentMask) {
        magnitude = mantissa == 0 ? INFINITY : NAN;
    } else {
        magnitude = std::ldexp(static_cast<float>(mantissa + kImplicitOne), static_cast<int>(exponent) - kPowerBias);
    }
    return (bits & kHalfSign) != 0 ? -magnitude : magnitude;
}

// --- the view's matrices ---------------------------------------------------------------------------------------

Vec3 minus(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}
float dot(const Vec3& a, const Vec3& b) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); }
Vec3 unit(const Vec3& a)
{
    const float length = std::sqrt(dot(a, a));
    return {a[0] / length, a[1] / length, a[2] / length};
}

// D3DXMatrixLookAtLH's layout.
Matrix look_at(const Vec3& eye, const Vec3& at)
{
    const Vec3 z = unit(minus(at, eye));
    const Vec3 x = unit(cross({0.0F, 1.0F, 0.0F}, z));
    const Vec3 y = cross(z, x);
    return {{{x[0], y[0], z[0], 0.0F},
             {x[1], y[1], z[1], 0.0F},
             {x[2], y[2], z[2], 0.0F},
             {-dot(x, eye), -dot(y, eye), -dot(z, eye), 1.0F}}};
}

render::rt::ViewDrawn view_of(const Vec3& eye, const Vec3& at, uint32_t width, uint32_t height, double seconds = 0.0)
{
    render::rt::ViewDrawn view;
    view.rect = {0, 0, width, height};
    view.output_rect = view.rect;
    view.depth_range = {0.0F, 1.0F};
    auto& projection = view.camera.projection;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    core::at(core::at(projection, 0), 0) = kVerticalScale / aspect;
    core::at(core::at(projection, 1), 1) = kVerticalScale;
    core::at(core::at(projection, 2), 2) = kFar / (kFar - kNear);
    core::at(core::at(projection, 2), 3) = 1.0F;
    core::at(core::at(projection, 3), 2) = -kNear * kFar / (kFar - kNear);
    view.camera.view = look_at(eye, at);
    view.camera.position = eye;
    view.game_seconds = seconds;
    return view;
}

// clip = (x, y, z, 1) x the view x the projection
std::array<float, 4> to_clip(const render::rt::ViewDrawn& view, const Vec3& point)
{
    const std::array<float, 4> in{point[0], point[1], point[2], 1.0F};
    std::array<float, 4> eye{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t k = 0; k < 4; ++k) {
            core::at(eye, column) += core::at(in, k) * core::at(core::at(view.camera.view, k), column);
        }
    }
    std::array<float, 4> clip{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t k = 0; k < 4; ++k) {
            core::at(clip, column) += core::at(eye, k) * core::at(core::at(view.camera.projection, k), column);
        }
    }
    return clip;
}

// --- the ground drawn ------------------------------------------------------------------------------------------

struct ScreenVertex {
    float x, y, z, rhw;
    uint32_t colour;
};

// A ground quad (y = `height`, x and z ranges) as two pre-transformed triangles in the view's depth.
void add_ground(std::vector<ScreenVertex>* out, const render::rt::ViewDrawn& view, std::array<float, 4> area, float height,
                uint32_t colour)
{
    const auto [left, right, front, back] = area;
    const std::array<Vec3, 4> corners{{{left, height, front}, {right, height, front}, {right, height, back}, {left, height, back}}};
    const auto width = static_cast<float>(view.rect[2]);
    const auto tall = static_cast<float>(view.rect[3]);
    for (const std::size_t which : {0U, 1U, 2U, 0U, 2U, 3U}) {
        const std::array<float, 4> clip = to_clip(view, core::at(corners, which));
        const float w = clip[3];
        out->push_back({.x = ((clip[0] / w * kHalf) + kHalf) * width,
                        .y = (kHalf - (clip[1] / w * kHalf)) * tall,
                        .z = clip[2] / w,
                        .rhw = 1.0F / w,
                        .colour = colour});
    }
}

void draw_ground(render::Gpu& gpu, const render::rt::ViewDrawn& view, bool lawn)
{
    gpu.set_viewport({.x = 0, .y = 0, .width = view.rect[2], .height = view.rect[3], .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, 0xFF8090C0, 1.0F, 0);
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kZFunc, value::kCompareAlways); // (the quads in order: the lawn over the ground)
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaTestEnable, 0);
    gpu.set_render_state(GpuRenderState::kFogEnable, 0);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_vertex_format(kFvf);
    gpu.set_texture(0, nullptr);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgDiffuse);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgDiffuse);
    std::vector<ScreenVertex> quads;
    add_ground(&quads, view, {-kGroundSide, kGroundSide, kGroundNear, kGroundFar}, 0.0F, kGreyColour);
    if (lawn) {
        add_ground(&quads, view, {kLawnLeft, 0.0F, kLawnNear, kLawnFar}, 0.0F, kGreenColour);
        add_ground(&quads, view, {0.0F, kLawnRight, kLawnNear, kLawnFar}, 0.0F, kStrawColour);
    }
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(quads.size() / 3), quads.data(),
                         sizeof(ScreenVertex));
}

// --- the faces -------------------------------------------------------------------------------------------------

void add_quad_faces(std::vector<grass::Face>* faces, const std::array<Vec3, 4>& corners, grass::Kind kind)
{
    faces->push_back({.corners = {corners[0], corners[1], corners[2]}, .kind = kind});
    faces->push_back({.corners = {corners[0], corners[2], corners[3]}, .kind = kind});
}

std::vector<grass::Face> lawn_faces()
{
    std::vector<grass::Face> faces;
    // the ground under everything (coplanar with the lawn: it covers nothing)
    add_quad_faces(&faces, {{{-kGroundSide, 0.0F, kGroundNear}, {kGroundSide, 0.0F, kGroundNear}, {kGroundSide, 0.0F, kGroundFar}, {-kGroundSide, 0.0F, kGroundFar}}},
                   grass::Kind::kBlocker);
    add_quad_faces(&faces, {{{kLawnLeft, 0.0F, kLawnNear}, {kLawnRight, 0.0F, kLawnNear}, {kLawnRight, 0.0F, kLawnFar}, {kLawnLeft, 0.0F, kLawnFar}}},
                   grass::Kind::kGrass);
    // the box: its top and four sides
    add_quad_faces(&faces, {{{kBoxLeft, kBoxTop, kBoxNear}, {kBoxRight, kBoxTop, kBoxNear}, {kBoxRight, kBoxTop, kBoxFar}, {kBoxLeft, kBoxTop, kBoxFar}}},
                   grass::Kind::kBlocker);
    add_quad_faces(&faces, {{{kBoxLeft, 0.0F, kBoxNear}, {kBoxRight, 0.0F, kBoxNear}, {kBoxRight, kBoxTop, kBoxNear}, {kBoxLeft, kBoxTop, kBoxNear}}},
                   grass::Kind::kBlocker);
    add_quad_faces(&faces, {{{kBoxLeft, 0.0F, kBoxFar}, {kBoxRight, 0.0F, kBoxFar}, {kBoxRight, kBoxTop, kBoxFar}, {kBoxLeft, kBoxTop, kBoxFar}}},
                   grass::Kind::kBlocker);
    add_quad_faces(&faces, {{{kBoxLeft, 0.0F, kBoxNear}, {kBoxLeft, 0.0F, kBoxFar}, {kBoxLeft, kBoxTop, kBoxFar}, {kBoxLeft, kBoxTop, kBoxNear}}},
                   grass::Kind::kBlocker);
    add_quad_faces(&faces, {{{kBoxRight, 0.0F, kBoxNear}, {kBoxRight, 0.0F, kBoxFar}, {kBoxRight, kBoxTop, kBoxFar}, {kBoxRight, kBoxTop, kBoxNear}}},
                   grass::Kind::kBlocker);
    // a steep grass bank (no blades) and a dried patch (behind the camera's view: the field's checks only)
    add_quad_faces(&faces, {{{kBankLeft, 0.0F, kLawnNear}, {kBankRight, kBankRise, kLawnNear}, {kBankRight, kBankRise, kLawnFar}, {kBankLeft, 0.0F, kLawnFar}}},
                   grass::Kind::kGrass);
    add_quad_faces(&faces, {{{kDriedLeft, 0.0F, kDriedNear}, {kDriedRight, 0.0F, kDriedNear}, {kDriedRight, 0.0F, kDriedFar}, {kDriedLeft, 0.0F, kDriedFar}}},
                   grass::Kind::kDried);
    return faces;
}

bool inside(float x, float z, std::array<float, 4> area, float margin)
{
    return x > area[0] + margin && x < area[1] - margin && z > area[2] + margin && z < area[3] - margin;
}

constexpr std::array<float, 4> kLawn{kLawnLeft, kLawnRight, kLawnNear, kLawnFar};
constexpr std::array<float, 4> kBox{kBoxLeft, kBoxRight, kBoxNear, kBoxFar};
constexpr std::array<float, 4> kDried{kDriedLeft, kDriedRight, kDriedNear, kDriedFar};

void check_field(Tally* tally)
{
    text::print("the field:\n");
    const std::vector<grass::Face> faces = lawn_faces();
    const grass::Field one = grass::make_field(faces, 1.0F);
    const grass::Field two = grass::make_field(faces, 1.0F);
    tally->check(one.blades.size() == two.blades.size() && !one.blades.empty() &&
                     std::memcmp(one.blades.data(), two.blades.data(), one.blades.size() * sizeof(grass::Blade)) == 0,
                 text::format("the same faces, the same %zu blades byte for byte", one.blades.size()));
    const double lawn_area = (kLawnRight - kLawnLeft) * (kLawnFar - kLawnNear);
    const double box_area = (kBoxRight - kBoxLeft) * (kBoxFar - kBoxNear);
    const double dried_area = (kDriedRight - kDriedLeft) * (kDriedFar - kDriedNear);
    const double expected = (lawn_area - box_area + dried_area) * grass::kBladesPerSquareUnit;
    const auto count = static_cast<double>(one.blades.size());
    tally->check(std::abs(count - expected) < expected * 0.03,
                 text::format("%zu blades for %.0f expected (the area x the density)", one.blades.size(), expected));
    const double covered_expected = box_area * grass::kBladesPerSquareUnit;
    tally->check(std::abs(static_cast<double>(one.covered) - covered_expected) < covered_expected * 0.2,
                 text::format("%u roots under the box dropped (%.0f expected)", one.covered, covered_expected));
    int off_grass = 0;
    int under_box = 0;
    int dried_wrong = 0;
    for (const grass::Blade& blade : one.blades) {
        const float x = blade.root[0];
        const float z = blade.root[2];
        const bool on_lawn = inside(x, z, kLawn, -0.01F);
        const bool on_dried = inside(x, z, kDried, -0.01F);
        off_grass += (!on_lawn && !on_dried) || std::abs(blade.root[1]) > 0.01F ? 1 : 0;
        under_box += inside(x, z, kBox, 0.0F) ? 1 : 0;
        dried_wrong += on_dried != ((blade.packed & grass::kDriedFlag) != 0) ? 1 : 0;
    }
    tally->check(off_grass == 0, text::format("every root on a lawn face, none on the steep bank (%d off)", off_grass));
    tally->check(under_box == 0, text::format("no root under the box (%d)", under_box));
    tally->check(dried_wrong == 0 && one.dried_faces == 2 && one.steep_faces == 2,
                 text::format("the dried patch flagged (%d wrong), %u steep faces left bare", dried_wrong, one.steep_faces));
    bool ordered = true;
    for (const grass::Tile& tile : one.tiles) {
        for (uint32_t k = 1; k < tile.count; ++k) {
            ordered = ordered && (core::at(one.blades, tile.first + k - 1).packed & 0xFFU) <=
                                     (core::at(one.blades, tile.first + k).packed & 0xFFU);
        }
    }
    tally->check(ordered, text::format("each of the %zu tiles' blades in rank order", one.tiles.size()));
}

// --- the targets ---------------------------------------------------------------------------------------------------

struct Targets {
    GpuSurface* colour = nullptr;
    GpuSurface* depth = nullptr;
};

Targets make_targets(render::Gpu& gpu, uint32_t samples, uint32_t width = kWidth, uint32_t height = kHeight)
{
    Targets targets;
    const render::GpuSurfaceDesc colour{.width = width,
                                        .height = height,
                                        .format = value::kFormatA16B16G16R16F,
                                        .samples = samples,
                                        .quality = 0,
                                        .lockable_or_discard = samples == 0};
    render::GpuSurfaceDesc depth = colour;
    depth.format = value::kFormatD24S8;
    depth.lockable_or_discard = false;
    if (gpu.create_render_target(colour, &targets.colour) != value::kResultOk) {
        targets.colour = nullptr;
    }
    if (gpu.create_depth_surface(depth, &targets.depth) != value::kResultOk) {
        targets.depth = nullptr;
    }
    return targets;
}

void release(render::Gpu& gpu, const Targets& targets)
{
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    for (GpuSurface* surface : {targets.colour, targets.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
}

using Picture = std::vector<std::array<float, 3>>;

bool read_picture(GpuSurface* target, Picture* picture, uint32_t width = kWidth, uint32_t height = kHeight)
{
    std::vector<uint8_t> bytes;
    if (!vk::read_surface(target, &bytes)) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (bytes.size() < pixels * kHalfPixelBytes) {
        return false;
    }
    picture->resize(pixels);
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            const std::size_t first = (pixel * kHalfPixelBytes) + (channel * 2);
            const auto bits = static_cast<uint16_t>(core::at(bytes, first) | (core::at(bytes, first + 1) << kHighByte));
            core::at(core::at(*picture, pixel), channel) = from_half(bits);
        }
    }
    return true;
}

// The ground drawn, then `passes` grass passes (each a frame: the scene drawn again first).
void run_frames(render::Gpu& gpu, const Targets& targets, const render::rt::ViewDrawn& view, int passes, bool lawn = true)
{
    for (int pass = 0; pass < std::max(passes, 1); ++pass) {
        gpu.begin_scene();
        gpu.set_render_target(0, targets.colour);
        gpu.set_depth_surface(targets.depth);
        draw_ground(gpu, view, lawn);
        if (passes > 0) {
            render::rt::opaque_view_drawn(view);
        }
        gpu.set_render_target(0, nullptr);
        gpu.set_depth_surface(nullptr);
        gpu.end_scene();
    }
}

// A ray through a pixel's centre: its points at heights 0 - `top` above the ground.
struct Ray {
    Vec3 origin{};
    Vec3 direction{};
};

Ray ray_through(const render::rt::ViewDrawn& view, uint32_t column, uint32_t row)
{
    vk::Float4x4 camera{};
    vk::Float4x4 projection{};
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            core::at(camera, (r * 4) + c) = core::at(core::at(view.camera.view, r), c);
            core::at(projection, (r * 4) + c) = core::at(core::at(view.camera.projection, r), c);
        }
    }
    const vk::Float4x4 view_projection = vk::motion_multiply(camera, projection);
    vk::Float4x4 inverse{};
    vk::motion_inverse(view_projection, &inverse);
    const float x = ((static_cast<float>(column) + kHalf) / static_cast<float>(view.rect[2]) * 2.0F) - 1.0F;
    const float y = 1.0F - ((static_cast<float>(row) + kHalf) / static_cast<float>(view.rect[3]) * 2.0F);
    std::array<Vec3, 2> ends{};
    for (std::size_t end = 0; end < 2; ++end) {
        const std::array<float, 4> clip{x, y, end == 0 ? 0.0F : 1.0F, 1.0F};
        std::array<float, 4> world{};
        for (std::size_t c = 0; c < 4; ++c) {
            for (std::size_t k = 0; k < 4; ++k) {
                core::at(world, c) += core::at(clip, k) * core::at(inverse, (k * 4) + c);
            }
        }
        core::at(ends, end) = {world[0] / world[3], world[1] / world[3], world[2] / world[3]};
    }
    return {.origin = ends[0], .direction = minus(ends[1], ends[0])};
}

// Where the ray is at a height.
bool at_height(const Ray& ray, float height, float* x, float* z)
{
    if (std::abs(ray.direction[1]) < 1.0e-9F) {
        return false;
    }
    const float t = (height - ray.origin[1]) / ray.direction[1];
    if (!(t > 0.0F)) {
        return false;
    }
    *x = ray.origin[0] + (t * ray.direction[0]);
    *z = ray.origin[2] + (t * ray.direction[2]);
    return true;
}

constexpr float kTallest = vk::kGrassBaseHeight * vk::kGrassTallest; // (grass_height 1)
constexpr float kReachOut = kTallest + 2.0F; // a blade's lean and width at most

// Where blades may show: the ray meets, somewhere up to the tallest blade, the lawn (grown by a blade's reach) outside
// the box's footprint (shrunk by it). Inside: everywhere up to it, the lawn (shrunk) away from the box (grown).
struct Region {
    bool may = false;
    bool inside = false;
    float ground_x = 0.0F;
};

Region region_of(const render::rt::ViewDrawn& view, uint32_t column, uint32_t row)
{
    const Ray ray = ray_through(view, column, row);
    Region region;
    region.inside = true;
    constexpr int kSteps = 12;
    for (int step = 0; step <= kSteps; ++step) {
        const float height = kTallest * static_cast<float>(step) / static_cast<float>(kSteps);
        float x = 0.0F;
        float z = 0.0F;
        if (!at_height(ray, height, &x, &z)) {
            region.inside = false;
            continue;
        }
        if (step == 0) {
            region.ground_x = x;
        }
        region.may = region.may || (inside(x, z, kLawn, -kReachOut) && !inside(x, z, kBox, kReachOut));
        region.inside = region.inside && inside(x, z, kLawn, kReachOut) && !inside(x, z, kBox, -kReachOut * 4.0F);
    }
    return region;
}

bool changed(const std::array<float, 3>& before, const std::array<float, 3>& after)
{
    constexpr float kSame = 1.0e-3F;
    return std::abs(before[0] - after[0]) > kSame || std::abs(before[1] - after[1]) > kSame ||
           std::abs(before[2] - after[2]) > kSame;
}

const vk::GrassSettings kDefaults{.on = true};

void check_pass(render::Gpu& gpu, Tally* tally)
{
    text::print("the pass:\n");
    vk::set_grass_settings(kDefaults);
    const render::rt::ViewDrawn view = view_of(kEye, kLookAt, kWidth, kHeight);
    const Targets targets = make_targets(gpu, 0);
    if (targets.colour == nullptr || targets.depth == nullptr) {
        tally->check(false, "the targets");
        release(gpu, targets);
        return;
    }
    const std::vector<grass::Face> faces = lawn_faces();
    vk::grass_sink().set_field(std::make_shared<grass::Field>(grass::make_field(faces, 1.0F)));
    vk::grass_sink().set_pushers({});
    Picture before;
    Picture after;
    run_frames(gpu, targets, view, 0);
    const bool read_before = read_picture(targets.colour, &before);
    const uint64_t passes = vk::grass_passes();
    run_frames(gpu, targets, view, kLearningPasses);
    const bool read_after = read_picture(targets.colour, &after);
    tally->check(read_before && read_after && vk::grass_passes() == passes + kLearningPasses,
                 text::format("%d passes recorded, %u blades in %u draws the last", kLearningPasses, vk::grass_last_blades(),
                              vk::grass_last_draws()));
    if (!read_before || !read_after) {
        release(gpu, targets);
        return;
    }
    int stray = 0;
    int inside_pixels = 0;
    int inside_changed = 0;
    int finite = 0;
    std::array<double, 3> green_sum{};
    std::array<double, 3> straw_sum{};
    int green_count = 0;
    int straw_count = 0;
    constexpr float kSideMargin = 30.0F;
    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t column = 0; column < kWidth; ++column) {
            const std::size_t pixel = (static_cast<std::size_t>(row) * kWidth) + column;
            const auto& was = core::at(before, pixel);
            const auto& now = core::at(after, pixel);
            finite += std::isfinite(now[0]) && std::isfinite(now[1]) && std::isfinite(now[2]) ? 1 : 0;
            const bool moved = changed(was, now);
            const Region region = region_of(view, column, row);
            stray += moved && !region.may ? 1 : 0;
            if (region.inside) {
                ++inside_pixels;
                inside_changed += moved ? 1 : 0;
            }
            if (moved && region.inside) {
                std::array<double, 3>& sum = region.ground_x < -kSideMargin ? green_sum : straw_sum;
                if (std::abs(region.ground_x) > kSideMargin) {
                    for (std::size_t channel = 0; channel < kChannels; ++channel) {
                        core::at(sum, channel) += core::at(now, channel);
                    }
                    (region.ground_x < 0.0F ? green_count : straw_count) += 1;
                }
            }
        }
    }
    tally->check(stray == 0, text::format("blades only over the lawn, none over the box's footprint (%d stray pixels)", stray));
    const double coverage = inside_pixels > 0 ? static_cast<double>(inside_changed) / inside_pixels : 0.0;
    tally->check(inside_pixels > 1000 && coverage > 0.35,
                 text::format("%.0f%% of the lawn's %d inside pixels covered by blades", coverage * 100.0, inside_pixels));
    tally->check(finite == static_cast<int>(kWidth * kHeight), text::format("every pixel finite (%d)", finite));
    const bool green_ok = green_count > 0 && green_sum[1] > green_sum[0] * 1.8 && green_sum[1] > green_sum[2] * 1.8;
    const bool straw_ok = straw_count > 0 && straw_sum[0] > straw_sum[1] * 1.05 && straw_sum[1] > straw_sum[2] * 2.0;
    tally->check(green_ok && straw_ok,
                 text::format("the blades the ground's colour: green half %.2f %.2f %.2f, straw half %.2f %.2f %.2f",
                              green_count > 0 ? green_sum[0] / green_count : 0.0, green_count > 0 ? green_sum[1] / green_count : 0.0,
                              green_count > 0 ? green_sum[2] / green_count : 0.0, straw_count > 0 ? straw_sum[0] / straw_count : 0.0,
                              straw_count > 0 ? straw_sum[1] / straw_count : 0.0, straw_count > 0 ? straw_sum[2] / straw_count : 0.0));
    // the same picture from a new field of the same faces
    vk::grass_sink().set_field(std::make_shared<grass::Field>(grass::make_field(faces, 1.0F)));
    run_frames(gpu, targets, view, kLearningPasses);
    Picture again;
    tally->check(read_picture(targets.colour, &again) && again == after, "the same picture again from a new field");
    // the colours known, all finite
    std::vector<std::array<float, 4>> colours;
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        vk::read_grass_colours(state, &colours);
    }
    int known = 0;
    int bad = 0;
    for (const auto& colour : colours) {
        known += colour[3] >= 1.0F ? 1 : 0;
        bad += std::isfinite(colour[0]) && std::isfinite(colour[1]) && std::isfinite(colour[2]) && std::isfinite(colour[3]) ? 0 : 1;
    }
    tally->check(!colours.empty() && bad == 0 && known > 0,
                 text::format("%d of %zu blades' colours known, %d not finite", known, colours.size(), bad));
    // not-a-number and infinities in the ground under the blades: nothing drawn turns bad
    {
        gpu.begin_scene();
        gpu.set_render_target(0, targets.colour);
        gpu.set_depth_surface(targets.depth);
        draw_ground(gpu, view, true);
        gpu.set_render_target(0, nullptr);
        gpu.set_depth_surface(nullptr);
        gpu.end_scene();
        render::GpuLockedRect locked{};
        bool poisoned = gpu.lock_surface(targets.colour, &locked, nullptr, 0) == value::kResultOk;
        if (poisoned) {
            const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits),
                                           static_cast<std::size_t>(kWidth) * kHeight * kHalfPixelBytes);
            constexpr std::array<uint16_t, 3> kBad{0x7E00, 0x7C00, 0xFC00};
            for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(kWidth) * kHeight; pixel += 3) {
                const uint16_t bad_bits = core::at(kBad, (pixel / 3) % kBad.size());
                for (std::size_t channel = 0; channel < kChannels; ++channel) {
                    core::at(bytes, (pixel * kHalfPixelBytes) + (channel * 2)) = static_cast<uint8_t>(bad_bits & 0xFFU);
                    core::at(bytes, (pixel * kHalfPixelBytes) + (channel * 2) + 1) = static_cast<uint8_t>(bad_bits >> kHighByte);
                }
            }
            poisoned = gpu.unlock_surface(targets.colour) == value::kResultOk;
        }
        gpu.begin_scene();
        gpu.set_render_target(0, targets.colour);
        gpu.set_depth_surface(targets.depth);
        render::rt::opaque_view_drawn(view);
        gpu.set_render_target(0, nullptr);
        gpu.set_depth_surface(nullptr);
        gpu.end_scene();
        {
            vk::Backend& state = vk::backend();
            const std::scoped_lock lock(state.mutex);
            vk::read_grass_colours(state, &colours);
        }
        bad = 0;
        for (const auto& colour : colours) {
            bad += std::isfinite(colour[0]) && std::isfinite(colour[1]) && std::isfinite(colour[2]) ? 0 : 1;
        }
        tally->check(poisoned && bad == 0, text::format("not-a-number and infinities under the roots: %d colours bad", bad));
    }
    release(gpu, targets);
}

// The device's shapes against the twin, the level of detail, the push.
void check_shapes(render::Gpu& gpu, Tally* tally)
{
    text::print("the shapes:\n");
    vk::set_grass_settings(kDefaults);
    const std::vector<grass::Face> faces = lawn_faces();
    const grass::Field field = grass::make_field(faces, 1.0F);
    vk::grass_sink().set_field(std::make_shared<grass::Field>(field));
    const Vec3 skater{0.0F, 0.0F, 200.0F};
    constexpr float kSeconds = 7.25F;
    const auto probe = [&](const render::rt::ViewDrawn& view, std::span<const grass::Pusher> pushers,
                           std::vector<std::array<float, 4>>* tips) {
        vk::grass_sink().set_pushers(pushers);
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        return vk::probe_grass(state, view, 0, static_cast<uint32_t>(field.blades.size()), tips);
    };
    const std::array<grass::Pusher, 1> pusher{{{.at = skater, .radius = grass::kCharacterReach}}};
    const render::rt::ViewDrawn near_view = view_of(kEye, kLookAt, kWidth, kHeight, kSeconds);
    std::vector<std::array<float, 4>> pushed;
    std::vector<std::array<float, 4>> free;
    const bool probed = probe(near_view, pusher, &pushed) && probe(near_view, {}, &free);
    tally->check(probed && pushed.size() == field.blades.size(), "the device's shapes read back");
    if (!probed) {
        return;
    }
    vk::GrassView shape_view = vk::grass_view(kDefaults, near_view, kHeight);
    float worst = 0.0F;
    int not_finite = 0;
    int pushed_blades = 0;
    int pushed_wrong = 0;
    for (std::size_t index = 0; index < field.blades.size(); ++index) {
        const grass::Blade& blade = core::at(field.blades, index);
        const vk::GrassShape twin = vk::grass_blade_shape(blade, shape_view, pusher, 1.0F);
        const std::array<float, 4>& got = core::at(pushed, index);
        not_finite += std::isfinite(got[0]) && std::isfinite(got[1]) && std::isfinite(got[2]) && std::isfinite(got[3]) ? 0 : 1;
        worst = std::max({worst, std::abs(got[0] - twin.tip[0]), std::abs(got[1] - twin.tip[1]),
                          std::abs(got[2] - twin.tip[2]), std::abs(got[3] - twin.height)});
        const float away_x = blade.root[0] - skater[0];
        const float away_z = blade.root[2] - skater[2];
        const float apart = std::sqrt((away_x * away_x) + (away_z * away_z));
        if (apart > 2.0F && apart < grass::kCharacterReach * kHalf && got[3] > kHalf) { // (the push stronger than lean and wind)
            ++pushed_blades;
            const float out_pushed = (((got[0] - blade.root[0]) * away_x) + ((got[2] - blade.root[2]) * away_z)) / apart;
            const std::array<float, 4>& loose = core::at(free, index);
            const float out_free = (((loose[0] - blade.root[0]) * away_x) + ((loose[2] - blade.root[2]) * away_z)) / apart;
            pushed_wrong += out_pushed > 0.0F && out_pushed > out_free ? 0 : 1;
        }
    }
    tally->check(worst < 0.02F && not_finite == 0,
                 text::format("every tip and height within %.4f units of the twin (%zu blades), %d not finite",
                              static_cast<double>(worst), field.blades.size(), not_finite));
    tally->check(pushed_blades > 20 && pushed_wrong == 0,
                 text::format("the %d blades within the skater's reach bent away from him (%d not)", pushed_blades, pushed_wrong));
    // the level of detail
    const float distance = kDefaults.distance * vk::kGrassUnitsPerMetre;
    bool falling = true;
    float last = 2.0F;
    for (float d = 0.0F; d < distance * 1.5F; d += distance / 64.0F) {
        const float keep = vk::grass_keep(shape_view, d) * vk::grass_fade(shape_view, d);
        falling = falling && keep <= last;
        last = keep;
    }
    tally->check(falling && vk::grass_fade(shape_view, distance) == 0.0F && vk::grass_keep(shape_view, 0.0F) == 1.0F,
                 "the share kept and the height fall with the distance, to nothing at it");
    const Vec3 far_eye{0.0F, 120.0F, kLawnFar + distance + 400.0F};
    const render::rt::ViewDrawn far_view = view_of(far_eye, kLookAt, kWidth, kHeight, kSeconds);
    std::vector<std::array<float, 4>> far_tips;
    int far_tall = 0;
    if (probe(far_view, {}, &far_tips)) {
        for (const auto& tip : far_tips) {
            far_tall += tip[3] > 0.0F ? 1 : 0;
        }
    }
    vk::GrassView far_shape = vk::grass_view(kDefaults, far_view, kHeight);
    uint32_t far_blades = 0;
    const std::vector<vk::GrassDraw> far_draws = vk::plan_grass_draws(field, &far_shape, &far_blades);
    tally->check(far_tips.size() == field.blades.size() && far_tall == 0 && far_draws.empty(),
                 text::format("beyond the distance: no blade tall (%d), nothing drawn (%zu draws)", far_tall, far_draws.size()));
    // (a camera standing on the lawn: the blades near it whose rank is clear of the growth band - every one kept)
    const Vec3 close_eye{0.0F, 60.0F, 150.0F};
    std::vector<std::array<float, 4>> close_tips;
    const bool close_probed = probe(view_of(close_eye, kLookAt, kWidth, kHeight, kSeconds), {}, &close_tips);
    int near_full = 0;
    int near_count = 0;
    constexpr float kClearRank = 0.9F * 256.0F;
    for (std::size_t index = 0; close_probed && index < field.blades.size(); ++index) {
        const grass::Blade& blade = core::at(field.blades, index);
        const Vec3 to = minus(close_eye, blade.root);
        if (std::sqrt(dot(to, to)) < distance * vk::kGrassNearShare * 0.9F &&
            static_cast<float>(blade.packed & 0xFFU) < kClearRank) {
            ++near_count;
            const float tall = vk::kGrassShortest + ((vk::kGrassTallest - vk::kGrassShortest) *
                                                     static_cast<float>((blade.packed >> 8U) & 0xFFU) / 255.0F);
            near_full += std::abs(core::at(close_tips, index)[3] - (vk::kGrassBaseHeight * tall)) < 1.0e-3F ? 1 : 0;
        }
    }
    tally->check(near_count > 0 && near_full == near_count,
                 text::format("near the camera every blade full height (%d of %d)", near_full, near_count));
    uint32_t near_blades = 0;
    const std::vector<vk::GrassDraw> near_draws = vk::plan_grass_draws(field, &shape_view, &near_blades);
    const Vec3 middle_eye{0.0F, 120.0F, kLawnNear - (distance * 0.6F)};
    vk::GrassView middle_shape = vk::grass_view(kDefaults, view_of(middle_eye, kLookAt, kWidth, kHeight), kHeight);
    uint32_t middle_blades = 0;
    const std::vector<vk::GrassDraw> middle_draws = vk::plan_grass_draws(field, &middle_shape, &middle_blades);
    tally->check(!near_draws.empty() && !middle_draws.empty() && middle_blades < near_blades,
                 text::format("fewer blades drawn from further away: %u near, %u at 60%% of the distance", near_blades,
                              middle_blades));
    (void)gpu;
}

void check_multisampled(render::Gpu& gpu, Tally* tally)
{
    text::print("four samples:\n");
    vk::set_grass_settings(kDefaults);
    vk::grass_sink().set_field(std::make_shared<grass::Field>(grass::make_field(lawn_faces(), 1.0F)));
    const Targets targets = make_targets(gpu, kFourSamples);
    if (targets.colour == nullptr || targets.depth == nullptr) {
        text::print("  (no four-sample targets here)\n");
        release(gpu, targets);
        return;
    }
    const uint64_t passes = vk::grass_passes();
    run_frames(gpu, targets, view_of(kEye, kLookAt, kWidth, kHeight), kLearningPasses);
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        vk::finish_work(state);
    }
    tally->check(vk::grass_passes() == passes + kLearningPasses && vk::grass_last_blades() > 0,
                 text::format("the pass ran on four samples (%u blades)", vk::grass_last_blades()));
    release(gpu, targets);
}

void check_off(render::Gpu& gpu, Tally* tally)
{
    text::print("off:\n");
    vk::set_grass_settings({});
    const Targets targets = make_targets(gpu, 0);
    const uint64_t passes = vk::grass_passes();
    run_frames(gpu, targets, view_of(kEye, kLookAt, kWidth, kHeight), 1);
    tally->check(vk::grass_passes() == passes && render::grass::sink() == nullptr, "grass=0: no pass, no sink");
    release(gpu, targets);
}

// The pass's time on the device at 2560x1440 with the lawn filling the view (the defaults): printed.
void measure_cost(render::Gpu& gpu, Tally* tally)
{
    constexpr int kTimedPasses = 20;
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    constexpr uint32_t kBigWidth = 2560;
    constexpr uint32_t kBigHeight = 1440;
    constexpr float kLawnHalf = 3000.0F;
    vk::set_grass_settings(kDefaults);
    std::vector<grass::Face> faces;
    add_quad_faces(&faces, {{{-kLawnHalf, 0.0F, -kLawnHalf}, {kLawnHalf, 0.0F, -kLawnHalf}, {kLawnHalf, 0.0F, kLawnHalf}, {-kLawnHalf, 0.0F, kLawnHalf}}},
                   grass::Kind::kGrass);
    vk::grass_sink().set_field(std::make_shared<grass::Field>(grass::make_field(faces, 1.0F)));
    vk::Backend& state = vk::backend();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    if (properties.limits.timestampComputeAndGraphics == VK_FALSE) {
        text::print("the cost: no timestamps on this device\n");
        return;
    }
    VkQueryPool pool = VK_NULL_HANDLE;
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = 2,
                                     .pipelineStatistics = 0};
    if (vkCreateQueryPool(state.context.device(), &info, nullptr, &pool) != VK_SUCCESS) {
        return;
    }
    const Targets targets = make_targets(gpu, 0, kBigWidth, kBigHeight);
    if (targets.colour != nullptr && targets.depth != nullptr) {
        // a skater's camera: a few feet up, looking along the lawn
        const render::rt::ViewDrawn view = view_of({0.0F, 70.0F, -kLawnHalf * 0.5F}, {0.0F, 0.0F, 0.0F}, kBigWidth, kBigHeight);
        // (the ground is the lawn: drawn so that every root sees its colour)
        const auto draw = [&](bool grass_pass) {
            gpu.begin_scene();
            gpu.set_render_target(0, targets.colour);
            gpu.set_depth_surface(targets.depth);
            gpu.set_viewport({.x = 0, .y = 0, .width = kBigWidth, .height = kBigHeight, .min_z = 0.0F, .max_z = 1.0F});
            gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, 0xFF8090C0, 1.0F, 0);
            std::vector<ScreenVertex> quads;
            add_ground(&quads, view, {-kLawnHalf, kLawnHalf, -kLawnHalf * 0.5F + 20.0F, kLawnHalf}, 0.0F, kGreenColour);
            gpu.set_vertex_format(kFvf);
            gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(quads.size() / 3), quads.data(),
                                 sizeof(ScreenVertex));
            if (grass_pass) {
                render::rt::opaque_view_drawn(view);
            }
        };
        for (int warm = 0; warm < kLearningPasses; ++warm) {
            draw(true);
            gpu.set_render_target(0, nullptr);
            gpu.set_depth_surface(nullptr);
            gpu.end_scene();
        }
        draw(false);
        {
            const std::scoped_lock lock(state.mutex);
            VkCommandBuffer commands = vk::outside_pass(state);
            vkCmdResetQueryPool(commands, pool, 0, 2);
            vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
        }
        for (int i = 0; i < kTimedPasses; ++i) {
            render::rt::opaque_view_drawn(view);
        }
        std::array<uint64_t, 2> stamps{};
        {
            const std::scoped_lock lock(state.mutex);
            vkCmdWriteTimestamp(vk::outside_pass(state), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, 1);
            vk::finish_work(state);
            vkGetQueryPoolResults(state.context.device(), pool, 0, 2, sizeof(stamps), stamps.data(), sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        }
        gpu.set_render_target(0, nullptr);
        gpu.set_depth_surface(nullptr);
        gpu.end_scene();
        const double each = static_cast<double>(stamps[1] - stamps[0]) * static_cast<double>(properties.limits.timestampPeriod) /
                            kNanosecondsPerMillisecond / kTimedPasses;
        text::print("the cost: %.3f ms a pass at %ux%u (%u blades in %u draws; the defaults, the lawn filling the view)\n", each,
                    kBigWidth, kBigHeight, vk::grass_last_blades(), vk::grass_last_draws());
        tally->check(std::isfinite(each) && each > 0.0, text::format("timed at %ux%u", kBigWidth, kBigHeight));
    }
    release(gpu, targets);
    vkDestroyQueryPool(state.context.device(), pool, nullptr);
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

bool open_device(const Options& options, SDL_Window* window)
{
    vk::set_context_options({.validation = options.validation, .device_index = options.device, .on_message = print_line},
                            window);
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
    Tally tally;
    check_field(&tally);
    if (!open_device(options, window)) {
        return vk::loader_version() == 0 ? 2 : 1;
    }
    render::Gpu& gpu = vk::gpu();
    check_pass(gpu, &tally);
    check_shapes(gpu, &tally);
    check_multisampled(gpu, &tally);
    check_off(gpu, &tally);
    measure_cost(gpu, &tally);
    vk::set_grass_settings({});
    vk::gpu_context().shutdown();
    tally.check(validation_messages() == 0, text::format("validation messages: %d", validation_messages()));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_grass_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
        SDL_CreateWindow("thug2 Vulkan grass check", kWindowWidth, kWindowHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
