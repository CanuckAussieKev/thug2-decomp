// NOT ORIGINAL - thug2_vulkan_rt_effects_check.exe: the Vulkan Gpu's ray-traced ambient occlusion and its denoiser
// (gpu_vulkan/vulkan_rt_effects.h) tried on this machine against references worked out on the processor.
//
// A small scene - a ground square, a box standing on it and a slab floating low over it (world, instance mask 0x01), a
// box standing on it (an object, 0x02) and one that never occludes (0x80) - is drawn through render::Gpu as render_pc
// draws a level (D3DX-style matrices, unlit vertex colours, depth tested and written); its acceleration structures are
// built here and handed over with rt_effects_set_scene; rt::opaque_view_drawn runs the passes through the listener, as
// draw_viewport does. The processor answers each pixel from the camera's own position and lens (not the matrices): the
// surface its ray hits first, and that surface's occlusion.
//   - The rays as they are (rt_ao_denoise=0): the processor traces the pass's own directions (the same sequence, the same
//     per-pixel turn, the same frame index) from the surface; pixels whose answer doesn't change when the start moves
//     a little or the normal tilts a little ("clear-cut") must be the surface's colour x (1 - strength x occlusion)
//     within 2 per channel; the others on one face between the answers the small moves give; silhouettes no lighter
//     than the lightest surface around them; the sky untouched. One sample and four (resolved for the read-back).
//   - Denoised, a still camera: after 40 frames the result read back (the image the colour was multiplied by) must be
//     near a 256-ray reference (mean and 99th-percentile error), every history full (32 frames), no NaN anywhere, and
//     the colour that image's occlusion applied.
//   - Then the camera moved: where the processor says the surface was seen by the frame before (all four texels the
//     history is read from agree), the history must be kept (still 32 long); where none of them was the surface
//     (disoccluded: the ground the boxes hid), it must start again (1 long) - and those pixels' error stays bounded
//     (no ghost of what was there before).
//   - No scene: the passes skipped, nothing changed; and rt_ao off: no pass at all.
//   thug2_vulkan_rt_effects_check [--device N] [--display N] [--no-validation]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan ray queries for this process.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_effects.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
using Vec3 = std::array<double, 3>;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr uint32_t kTargetWidth = 320;
constexpr uint32_t kTargetHeight = 240;
constexpr int kColumns = static_cast<int>(kTargetWidth);
constexpr int kRows = static_cast<int>(kTargetHeight);
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x042; // XYZ | DIFFUSE
constexpr uint32_t kClearColour = 0xFF102030;
constexpr int kTolerance = 2;
constexpr float kNearZ = 2.0F;
constexpr float kFarZ = 2000.0F;
constexpr double kHalfFovTan = 0.57735026918962576; // tan 30 degrees: a 60-degree horizontal field of view
constexpr double kNoHit = std::numeric_limits<double>::infinity();
constexpr std::size_t kNoSurface = 0;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint32_t kRedShift = 16;
constexpr uint32_t kGreenShift = 8;
constexpr std::size_t kPixelBytes = 4;
constexpr float kRadius = 20.0F;
constexpr float kStrength = 1.0F;
constexpr unsigned kRawRays = 8;
constexpr unsigned kDenoisedRays = 4;
constexpr int kStillFrames = 40;
constexpr double kOffsetScale = 1.0; // (the pass's)

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

// --- the scene ---------------------------------------------------------------------------------------------------

// A box (or, with no height, the ground square), its colour (A8R8G8B8) and its instance mask.
struct Solid {
    Vec3 low{};
    Vec3 high{};
    uint32_t colour = 0;
    uint32_t mask = 0;
};

constexpr double kGroundHalf = 100.0;
constexpr uint32_t kMaskWorld = 0x01;
constexpr uint32_t kMaskObjects = 0x02;
constexpr uint32_t kMaskNever = 0x80; // (never an occluder: the pass's mask leaves it out)
const std::array<Solid, 5> kSolids{{
    {.low = {-kGroundHalf, 0.0, -kGroundHalf},
     .high = {kGroundHalf, 0.0, kGroundHalf},
     .colour = 0xFFC8C8C8,
     .mask = kMaskWorld},
    {.low = {20.0, 0.0, -15.0}, .high = {44.0, 26.0, 9.0}, .colour = 0xFF5078B4, .mask = kMaskWorld},
    {.low = {-55.0, 6.0, -25.0}, .high = {-22.0, 10.0, 8.0}, .colour = 0xFFB4A050, .mask = kMaskWorld},
    {.low = {-12.0, 0.0, 28.0}, .high = {4.0, 16.0, 44.0}, .colour = 0xFFB45050, .mask = kMaskObjects},
    {.low = {48.0, 0.0, 36.0}, .high = {64.0, 18.0, 52.0}, .colour = 0xFF50B450, .mask = kMaskNever},
}};

constexpr Vec3 kEye{10.0, 120.0, 150.0};
constexpr Vec3 kMovedEye{-30.0, 105.0, 140.0};
constexpr Vec3 kAt{0.0, 0.0, 0.0};
constexpr Vec3 kUp{0.0, 1.0, 0.0};

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
// `from` + `along` x `scale`.
Vec3 step(const Vec3& from, const Vec3& along, double scale)
{
    return {x_of(from) + (x_of(along) * scale), y_of(from) + (y_of(along) * scale), z_of(from) + (z_of(along) * scale)};
}
Vec3 minus(const Vec3& one, const Vec3& other) { return step(one, other, -1.0); }
std::array<float, 3> floats(const Vec3& v)
{
    return {static_cast<float>(x_of(v)), static_cast<float>(y_of(v)), static_cast<float>(z_of(v))};
}

// The 36 vertices (12 triangles) of a box, or 6 of the ground square.
using Corner = std::array<int, 3>; // (0: the low side on that axis, 1: the high one)
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
    quad({{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}); // -y (the ground square's one face)
    if (y_of(solid.high) == y_of(solid.low)) {
        return out;
    }
    quad({{{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}}}); // +y
    quad({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}}); // -z
    quad({{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}); // +z
    quad({{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}); // -x
    quad({{{1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}}}); // +x
    return out;
}

// A ray against a box (slabs; a flat box is the ground square): the distance to its first hit and that face's normal.
struct Hit {
    double distance = kNoHit;
    Vec3 normal{};
};
struct Ray {
    Vec3 origin{};
    Vec3 direction{};
};
Hit intersect(const Solid& solid, const Ray& ray)
{
    constexpr double kParallel = 1e-12;
    double enter = 0.0;
    double leave = kNoHit;
    Vec3 normal{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double origin = core::at(ray.origin, axis);
        const double direction = core::at(ray.direction, axis);
        const double low = core::at(solid.low, axis);
        const double high = core::at(solid.high, axis);
        if (std::abs(direction) < kParallel) {
            if (origin < low || origin > high) {
                return {};
            }
            continue;
        }
        const bool rising = direction > 0.0;
        const double near_t = ((rising ? low : high) - origin) / direction;
        const double far_t = ((rising ? high : low) - origin) / direction;
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

// A viewport: x, y, width, height in the target's pixels (the whole target here).
using Rect = std::array<uint32_t, 4>;
constexpr Rect kWhole{0, 0, kTargetWidth, kTargetHeight};

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

struct Pixel {
    int x = 0;
    int y = 0;
};

// The surface a pixel sees: the solid (index + 1; 0: the sky), the face's normal, the point and its distance.
struct Seen {
    std::size_t solid = kNoSurface;
    Vec3 normal{};
    Vec3 point{};
    double distance = 0.0;
};
// offset: where in the pixel (from its centre: a multisampled target's depth is read at sample 0).
using Offset = std::array<double, 2>;
Seen seen_at(const View& view, Pixel pixel, const Offset& offset = {})
{
    // (Direct3D 9's pixel centres: whole coordinates)
    const double ndc_x = (2.0 * (pixel.x + core::at(offset, 0)) / kTargetWidth) - 1.0;
    const double ndc_y = 1.0 - (2.0 * (pixel.y + core::at(offset, 1)) / kTargetHeight);
    const Vec3 direction = normalised(
        step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height), view.back, -1.0));
    Seen seen;
    Hit nearest;
    for (std::size_t index = 0; index < kSolids.size(); ++index) {
        const Hit hit = intersect(core::at(kSolids, index), {.origin = view.eye, .direction = direction});
        if (hit.distance < nearest.distance) {
            nearest = hit;
            seen.solid = index + 1;
        }
    }
    if (seen.solid != kNoSurface) {
        seen.normal = nearest.normal;
        seen.point = step(view.eye, direction, nearest.distance);
        seen.distance = nearest.distance;
    }
    return seen;
}
bool same_face(const Seen& one, const Seen& other) { return one.solid == other.solid && one.normal == other.normal; }

// Where a world point is in a view's pixels (x, y; false behind the camera).
bool pixel_of(const View& view, const Vec3& point, std::array<double, 2>* at)
{
    const Vec3 offset = minus(point, view.eye);
    const double ahead = -dot(offset, view.back);
    if (ahead <= 0.0) {
        return false;
    }
    const double ndc_x = dot(offset, view.right) / (ahead * view.half_width);
    const double ndc_y = dot(offset, view.up) / (ahead * view.half_height);
    constexpr double kHalf = 0.5;
    *at = {(ndc_x + 1.0) * kTargetWidth * kHalf, (1.0 - ndc_y) * kTargetHeight * kHalf};
    return true;
}

// --- the occlusion, as the pass traces it ------------------------------------------------------------------------

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

bool occluded(const Vec3& origin, const Vec3& direction)
{
    return std::ranges::any_of(kSolids, [&](const Solid& solid) {
        return (solid.mask & render::vulkan::kAoMask) != 0 &&
               intersect(solid, {.origin = origin, .direction = direction}).distance <= kRadius;
    });
}

// Where the rays start: the surface pushed off along its normal (the pass's offset).
Vec3 ray_start(const Seen& seen, const Vec3& normal)
{
    const double away = seen.distance;
    constexpr double kLinear = 0.0005;
    constexpr double kSquare = 6e-8;
    return step(seen.point, normal, kOffsetScale * (1.0 + (away * kLinear) + (away * away * kSquare)));
}

// The pass's rays for a pixel in a frame: the share blocked. `shift` moves the start, `tilt` the normal.
double traced(const Seen& seen, Pixel local, uint32_t frame, uint32_t rays, const Vec3& shift, const Vec3& tilt)
{
    constexpr float kUnit = 1.0F / 16777216.0F;
    constexpr uint32_t kDropBits = 8;
    constexpr uint32_t kWrap = 4095;
    constexpr float kSequenceX = 0.7548776662F;
    constexpr float kSequenceY = 0.5698402910F;
    const Vec3 normal = normalised(step(seen.normal, tilt, 1.0));
    const Vec3 origin = step(ray_start(seen, normal), shift, 1.0);
    const uint32_t seed = hash(static_cast<uint32_t>(local.x) + hash(static_cast<uint32_t>(local.y)));
    const float turn_x = static_cast<float>(hash(seed) >> kDropBits) * kUnit;
    const float turn_y = static_cast<float>(hash(seed + 1U) >> kDropBits) * kUnit;
    int hits = 0;
    for (uint32_t i = 0; i < rays; ++i) {
        const auto index = static_cast<float>(((frame * rays) + i) & kWrap);
        const float u = fraction(turn_x + (index * kSequenceX));
        const float v = fraction(turn_y + (index * kSequenceY));
        hits += occluded(origin, hemisphere(normal, {u, v})) ? 1 : 0;
    }
    return static_cast<double>(hits) / rays;
}

// The occlusion with many rays (a 16 x 16 stratified grid over the hemisphere): what the denoiser converges to.
double reference_occlusion(const Seen& seen)
{
    constexpr int kSide = 16;
    const Vec3 origin = ray_start(seen, seen.normal);
    int hits = 0;
    for (int i = 0; i < kSide; ++i) {
        for (int j = 0; j < kSide; ++j) {
            constexpr double kMiddle = 0.5;
            hits += occluded(origin, hemisphere(seen.normal, {(i + kMiddle) / kSide, (j + kMiddle) / kSide})) ? 1 : 0;
        }
    }
    return static_cast<double>(hits) / (kSide * kSide);
}

// --- the acceleration structures (the check's own: the game's come from the TLAS's builder) -----------------------

struct Structure {
    render::vulkan::DeviceBuffer storage;
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
};

struct Scene {
    std::vector<render::vulkan::DeviceBuffer> inputs;
    std::vector<Structure> bottoms;
    Structure top;
    render::vulkan::DeviceBuffer scratch;
};

VkDeviceAddress address_of(VkDevice device, VkBuffer buffer)
{
    const VkBufferDeviceAddressInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = nullptr, .buffer = buffer};
    return vkGetBufferDeviceAddress(device, &info);
}

VkDeviceAddress aligned(VkDeviceAddress address, VkDeviceSize alignment)
{
    return (address + alignment - 1) & ~(alignment - 1);
}

bool make_structure(render::vulkan::Backend& state, VkAccelerationStructureTypeKHR type, VkDeviceSize bytes,
                    Structure* out)
{
    if (!state.memory.create_buffer(bytes,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    render::vulkan::BufferPlace::kDevice, &out->storage)) {
        return false;
    }
    const VkAccelerationStructureCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
                                                    .pNext = nullptr,
                                                    .createFlags = 0,
                                                    .buffer = out->storage.buffer,
                                                    .offset = 0,
                                                    .size = bytes,
                                                    .type = type,
                                                    .deviceAddress = 0};
    const VkDevice device = state.context.device();
    if (vkCreateAccelerationStructureKHR(device, &info, nullptr, &out->handle) != VK_SUCCESS) {
        return false;
    }
    const VkAccelerationStructureDeviceAddressInfoKHR where{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .pNext = nullptr,
        .accelerationStructure = out->handle};
    out->address = vkGetAccelerationStructureDeviceAddressKHR(device, &where);
    return true;
}

template <typename T>
std::span<const uint8_t> bytes_of(std::span<const T> items)
{
    const void* const start = items.data();
    return {static_cast<const uint8_t*>(start), items.size_bytes()};
}

// A buffer the build reads, filled from `bytes` at a 16-byte aligned address (instances need it).
bool make_input(render::vulkan::Backend& state, std::span<const uint8_t> bytes, render::vulkan::DeviceBuffer* out,
                VkDeviceAddress* address)
{
    constexpr VkDeviceSize kAlignment = 16;
    if (!state.memory.create_buffer(bytes.size() + kAlignment,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    render::vulkan::BufferPlace::kUpload, out) ||
        out->mapped == nullptr) {
        return false;
    }
    const VkDeviceAddress start = address_of(state.context.device(), out->buffer);
    *address = aligned(start, kAlignment);
    const VkDeviceSize offset = *address - start;
    const std::span<uint8_t> into(static_cast<uint8_t*>(out->mapped), static_cast<std::size_t>(out->bytes));
    std::ranges::copy(bytes, into.subspan(static_cast<std::size_t>(offset)).begin());
    state.memory.flush(*out, 0, out->bytes);
    return true;
}

struct BuildInput {
    VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    const VkAccelerationStructureGeometryKHR* geometry = nullptr;
    uint32_t primitives = 0;
};
bool build(render::vulkan::Backend& state, Scene* scene, const BuildInput& input, Structure* out)
{
    const VkDevice device = state.context.device();
    VkAccelerationStructureBuildGeometryInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    info.type = input.type;
    info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.geometryCount = 1;
    info.pGeometries = input.geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info,
                                            &input.primitives, &sizes);
    if (!make_structure(state, input.type, sizes.accelerationStructureSize, out)) {
        return false;
    }
    constexpr VkDeviceSize kScratchAlignment = 256; // (at least any device's minimum)
    if (scene->scratch.buffer != VK_NULL_HANDLE) {
        state.memory.destroy_buffer(&scene->scratch);
    }
    if (!state.memory.create_buffer(sizes.buildScratchSize + kScratchAlignment,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    render::vulkan::BufferPlace::kDevice, &scene->scratch)) {
        return false;
    }
    info.dstAccelerationStructure = out->handle;
    info.scratchData.deviceAddress = aligned(address_of(device, scene->scratch.buffer), kScratchAlignment);
    const VkAccelerationStructureBuildRangeInfoKHR range{
        .primitiveCount = input.primitives, .primitiveOffset = 0, .firstVertex = 0, .transformOffset = 0};
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    return state.memory.submit_now(
        [&](VkCommandBuffer commands) { vkCmdBuildAccelerationStructuresKHR(commands, 1, &info, &ranges); });
}

// A BLAS of some solids' triangles, and its instance (identity transform, the first solid's mask).
bool make_bottom(render::vulkan::Backend& state, Scene* scene, std::span<const std::size_t> solids,
                 std::vector<VkAccelerationStructureInstanceKHR>* instances)
{
    std::vector<std::array<float, 3>> vertices;
    for (const std::size_t solid : solids) {
        const std::vector<std::array<float, 3>> more = triangles_of(core::at(kSolids, solid));
        vertices.insert(vertices.end(), more.begin(), more.end());
    }
    render::vulkan::DeviceBuffer& input = scene->inputs.emplace_back();
    VkDeviceAddress vertex_address = 0;
    if (!make_input(state, bytes_of(std::span<const std::array<float, 3>>(vertices)), &input, &vertex_address)) {
        return false;
    }
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    VkAccelerationStructureGeometryTrianglesDataKHR& triangles = geometry.geometry.triangles;
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertex_address;
    triangles.vertexStride = sizeof(std::array<float, 3>);
    triangles.maxVertex = static_cast<uint32_t>(vertices.size() - 1);
    triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
    Structure& bottom = scene->bottoms.emplace_back();
    if (!build(state, scene,
               {.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                .geometry = &geometry,
                .primitives = static_cast<uint32_t>(vertices.size() / 3)},
               &bottom)) {
        return false;
    }
    VkAccelerationStructureInstanceKHR instance{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        core::at(core::at(std::span(instance.transform.matrix), axis), axis) = 1.0F;
    }
    instance.instanceCustomIndex = static_cast<uint32_t>(instances->size());
    instance.mask = core::at(kSolids, solids.front()).mask;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = bottom.address;
    instances->push_back(instance);
    return true;
}

// The BLASes (one per mask: the ground, the standing box and the slab share 0x01) and the TLAS of their instances.
bool make_scene(render::vulkan::Backend& state, Scene* scene)
{
    constexpr std::array<std::size_t, 3> kWorld{0, 1, 2};
    constexpr std::array<std::size_t, 1> kObject{3};
    constexpr std::array<std::size_t, 1> kNever{4};
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    if (!make_bottom(state, scene, kWorld, &instances) || !make_bottom(state, scene, kObject, &instances) ||
        !make_bottom(state, scene, kNever, &instances)) {
        return false;
    }
    render::vulkan::DeviceBuffer& list = scene->inputs.emplace_back();
    VkDeviceAddress list_address = 0;
    if (!make_input(state, bytes_of(std::span<const VkAccelerationStructureInstanceKHR>(instances)), &list,
                    &list_address)) {
        return false;
    }
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = list_address;
    return build(state, scene,
                 {.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
                  .geometry = &geometry,
                  .primitives = static_cast<uint32_t>(instances.size())},
                 &scene->top);
}

void release_scene(render::vulkan::Backend& state, Scene* scene)
{
    const VkDevice device = state.context.device();
    vkDeviceWaitIdle(device);
    scene->bottoms.push_back(scene->top);
    for (Structure& structure : scene->bottoms) {
        if (structure.handle != VK_NULL_HANDLE) {
            vkDestroyAccelerationStructureKHR(device, structure.handle, nullptr);
        }
        if (structure.storage.buffer != VK_NULL_HANDLE) {
            state.memory.destroy_buffer(&structure.storage);
        }
    }
    for (render::vulkan::DeviceBuffer& buffer : scene->inputs) {
        state.memory.destroy_buffer(&buffer);
    }
    if (scene->scratch.buffer != VK_NULL_HANDLE) {
        state.memory.destroy_buffer(&scene->scratch);
    }
    *scene = Scene{};
}

// --- drawing it through the Gpu, as render_pc draws a level -----------------------------------------------------

struct ColourVertex {
    std::array<float, 3> position{};
    uint32_t colour = 0;
};
static_assert(sizeof(ColourVertex) == (3 * sizeof(float)) + sizeof(uint32_t), "XYZ | DIFFUSE");

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

void draw_scene(render::Gpu& gpu, const Vec3& eye)
{
    using render::GpuRenderState;
    using render::GpuStageState;
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    const Matrices matrices = matrices_for(eye);
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
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(vertices.size() / 3),
                         vertices.data(), sizeof(ColourVertex));
}

render::rt::ViewDrawn view_drawn(const Vec3& eye)
{
    const Matrices matrices = matrices_for(eye);
    render::rt::ViewDrawn view;
    constexpr std::size_t kSide = 4;
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(view.camera.view, row), column) = core::at(matrices.view, (row * kSide) + column);
            core::at(core::at(view.camera.projection, row), column) =
                core::at(matrices.projection, (row * kSide) + column);
        }
    }
    view.camera.position = floats(eye);
    view.rect = kWhole;
    view.depth_range = {0.0F, 1.0F};
    return view;
}

// --- the targets and a frame ---------------------------------------------------------------------------------------

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

// A frame: cleared, the scene drawn, the passes through the listener (draw_viewport's call); the passes recorded.
uint64_t draw_frame(render::Gpu& gpu, const Targets& targets, const Vec3& eye)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kClearColour, 1.0F, 0);
    draw_scene(gpu, eye);
    const uint64_t before = render::vulkan::rt_ao_passes();
    render::rt::opaque_view_drawn(view_drawn(eye));
    const uint64_t passes = render::vulkan::rt_ao_passes() - before;
    gpu.end_scene();
    return passes;
}

// The colour target read back (resolved first when multisampled): X8R8G8B8 texels.
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

bool read_ao(render::vulkan::RtAoImage which, render::vulkan::RtAoTexels* out)
{
    render::vulkan::Backend& state = render::vulkan::backend();
    const std::scoped_lock lock(state.mutex);
    return render::vulkan::read_rt_ao_image(state, kWhole, which, out) && out->width == kTargetWidth &&
           out->height == kTargetHeight;
}

// --- comparing ---------------------------------------------------------------------------------------------------

using Rgb = std::array<int, 3>;
Rgb rgb_of(uint32_t colour)
{
    return {static_cast<int>((colour >> kRedShift) & kByteMask), static_cast<int>((colour >> kGreenShift) & kByteMask),
            static_cast<int>(colour & kByteMask)};
}
// A surface's colour with its occlusion applied.
// The solid a surface index names (null: the sky).
const Solid* solid_of(std::size_t surface) { return surface == kNoSurface ? nullptr : &core::at(kSolids, surface - 1); }

Rgb shaded(const Solid* solid, double occlusion)
{
    if (solid == nullptr) {
        return rgb_of(kClearColour);
    }
    const Rgb colour = rgb_of(solid->colour);
    Rgb out{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        core::at(out, channel) = static_cast<int>(
            std::lround(core::at(colour, channel) * (1.0 - (static_cast<double>(kStrength) * occlusion))));
    }
    return out;
}
Rgb pixel_at(const std::vector<uint8_t>& texels, Pixel pixel)
{
    const std::size_t at =
        ((static_cast<std::size_t>(pixel.y) * kTargetWidth) + static_cast<std::size_t>(pixel.x)) * kPixelBytes;
    return {core::at(texels, at + 2), core::at(texels, at + 1), core::at(texels, at)};
}
// Whether `got` lies between two colours (either order), give or take the tolerance.
bool between(const Rgb& got, const Rgb& one, const Rgb& other)
{
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const int low = std::min(core::at(one, channel), core::at(other, channel));
        const int high = std::max(core::at(one, channel), core::at(other, channel));
        if (core::at(got, channel) < low - kTolerance || core::at(got, channel) > high + kTolerance) {
            return false;
        }
    }
    return true;
}

std::size_t index_of(Pixel pixel)
{
    return (static_cast<std::size_t>(pixel.y) * kTargetWidth) + static_cast<std::size_t>(pixel.x);
}

// What the processor sees through every pixel of a view.
std::vector<Seen> seen_all(const View& view, const Offset& offset = {})
{
    std::vector<Seen> out(static_cast<std::size_t>(kColumns) * kRows);
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            core::at(out, index_of({.x = column, .y = row})) = seen_at(view, {.x = column, .y = row}, offset);
        }
    }
    return out;
}

// Whether every pixel within `radius` sees the same face as this one.
bool interior(const std::vector<Seen>& seen, Pixel pixel, int radius)
{
    const Seen& here = core::at(seen, index_of(pixel));
    for (int down = -radius; down <= radius; ++down) {
        for (int across = -radius; across <= radius; ++across) {
            const Pixel there{.x = pixel.x + across, .y = pixel.y + down};
            if (there.x < 0 || there.y < 0 || there.x >= kColumns || there.y >= kRows ||
                !same_face(core::at(seen, index_of(there)), here)) {
                return false;
            }
        }
    }
    return true;
}

// The rays' answer and how far small moves of the start and tilts of the normal take it.
struct Spread {
    double answer = 0.0;
    double low = 0.0;
    double high = 0.0;
};
Spread traced_spread(const Seen& seen, Pixel pixel, uint32_t frame, uint32_t rays)
{
    constexpr double kShift = 0.05;
    constexpr double kTilt = 0.002;
    Spread out;
    out.answer = traced(seen, pixel, frame, rays, {}, {});
    out.low = out.answer;
    out.high = out.answer;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        for (const double sign : {-1.0, 1.0}) {
            Vec3 move{};
            core::at(move, axis) = sign * kShift;
            Vec3 tilt{};
            core::at(tilt, axis) = sign * kTilt;
            for (const double moved : {traced(seen, pixel, frame, rays, move, {}), traced(seen, pixel, frame, rays, {}, tilt)}) {
                out.low = std::min(out.low, moved);
                out.high = std::max(out.high, moved);
            }
        }
    }
    return out;
}

// A silhouette or crease pixel: no lighter than the lightest surface around it, no darker than the darkest fully
// occluded.
bool edge_within(const std::vector<Seen>& seen, Pixel pixel, const Rgb& got)
{
    constexpr int kWhite = 255;
    Rgb lightest{};
    Rgb darkest{kWhite, kWhite, kWhite};
    for (int down = -1; down <= 1; ++down) {
        for (int across = -1; across <= 1; ++across) {
            const Pixel there{.x = std::clamp(pixel.x + across, 0, kColumns - 1),
                              .y = std::clamp(pixel.y + down, 0, kRows - 1)};
            const std::size_t solid = core::at(seen, index_of(there)).solid;
            const Rgb open = shaded(solid_of(solid), 0.0);
            const Rgb closed = shaded(solid_of(solid), solid == kNoSurface ? 0.0 : 1.0);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                core::at(lightest, channel) = std::max(core::at(lightest, channel), core::at(open, channel));
                core::at(darkest, channel) = std::min(core::at(darkest, channel), core::at(closed, channel));
            }
        }
    }
    return between(got, darkest, lightest);
}

// The rays as they are (frame 0): the target compared pixel by pixel.
void compare_raw(const std::vector<uint8_t>& texels, const Offset& sample, Tally* tally)
{
    const std::vector<Seen> seen = seen_all(view_from(kEye), sample);
    int clear_cut = 0;
    int clear_wrong = 0;
    int occluded_count = 0;
    int open_count = 0;
    int borderline = 0;
    int borderline_wrong = 0;
    int edges = 0;
    int edge_wrong = 0;
    int sky_wrong = 0;
    std::string first_wrong;
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const Pixel pixel{.x = column, .y = row};
            const Seen& here = core::at(seen, index_of(pixel));
            const Rgb got = pixel_at(texels, pixel);
            if (!interior(seen, pixel, 1)) {
                ++edges;
                edge_wrong += edge_within(seen, pixel, got) ? 0 : 1;
                continue;
            }
            if (here.solid == kNoSurface) {
                sky_wrong += got == rgb_of(kClearColour) ? 0 : 1;
                continue;
            }
            const Spread spread = traced_spread(here, pixel, 0, kRawRays);
            if (spread.low == spread.high) {
                ++clear_cut;
                (spread.answer > 0.0 ? occluded_count : open_count) += 1;
                const Rgb want = shaded(solid_of(here.solid), spread.answer);
                if (!between(got, want, want)) {
                    ++clear_wrong;
                    if (first_wrong.empty()) {
                        first_wrong = text::format(" (first at %d,%d: %d %d %d, %d %d %d wanted)", column, row,
                                                   core::at(got, 0), core::at(got, 1), core::at(got, 2),
                                                   core::at(want, 0), core::at(want, 1), core::at(want, 2));
                    }
                }
            } else {
                ++borderline;
                borderline_wrong += between(got, shaded(solid_of(here.solid), spread.low),
                                            shaded(solid_of(here.solid), spread.high))
                                        ? 0
                                        : 1;
            }
        }
    }
    constexpr int kEnoughOccluded = 300;
    constexpr int kEnoughOpen = 1000;
    tally->check(clear_wrong == 0 && occluded_count >= kEnoughOccluded && open_count >= kEnoughOpen,
                 text::format("clear-cut pixels as the processor traced them: %d of %d wrong (%d occluded, %d open)%s",
                              clear_wrong, clear_cut, occluded_count, open_count, first_wrong.c_str()));
    tally->check(borderline_wrong == 0, text::format("borderline pixels within their small moves' answers: %d of %d "
                                                     "outside",
                                                     borderline_wrong, borderline));
    tally->check(edge_wrong == 0,
                 text::format("edge pixels between their darkest and lightest answers: %d of %d outside", edge_wrong,
                              edges));
    tally->check(sky_wrong == 0, text::format("the sky left alone: %d pixels changed", sky_wrong));
}

// Nothing changed: every interior pixel the surface's own colour.
void compare_untouched(const std::vector<uint8_t>& texels, Tally* tally)
{
    const std::vector<Seen> seen = seen_all(view_from(kEye));
    int compared = 0;
    int changed = 0;
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const Pixel pixel{.x = column, .y = row};
            if (!interior(seen, pixel, 1)) {
                continue;
            }
            ++compared;
            const Rgb want = shaded(solid_of(core::at(seen, index_of(pixel)).solid), 0.0);
            changed += between(pixel_at(texels, pixel), want, want) ? 0 : 1;
        }
    }
    tally->check(changed == 0, text::format("nothing darkened: %d of %d pixels changed", changed, compared));
}

int count_not_finite(const render::vulkan::RtAoTexels& image)
{
    int count = 0;
    for (const std::array<float, 4>& texel : image.texels) {
        for (const float channel : texel) {
            count += std::isfinite(channel) ? 0 : 1;
        }
    }
    return count;
}

double percentile(std::vector<double> values, double share)
{
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto at = static_cast<std::size_t>(share * static_cast<double>(values.size() - 1));
    return core::at(values, at);
}

// The denoised result after a still camera's frames: near the many-ray reference, the histories full, no NaN, the
// colour multiplied by it.
void compare_still(const std::vector<uint8_t>& texels, Tally* tally)
{
    render::vulkan::RtAoTexels final_image;
    render::vulkan::RtAoTexels history;
    const bool read = read_ao(render::vulkan::RtAoImage::kFinal, &final_image) &&
                      read_ao(render::vulkan::RtAoImage::kHistory, &history);
    tally->check(read, "the denoiser's images read back");
    if (!read) {
        return;
    }
    tally->check(count_not_finite(final_image) == 0 && count_not_finite(history) == 0,
                 text::format("no NaN or infinity: %d in the result, %d in the history", count_not_finite(final_image),
                              count_not_finite(history)));
    const std::vector<Seen> seen = seen_all(view_from(kEye));
    std::vector<double> errors;
    int short_history = 0;
    int colour_wrong = 0;
    double occluded_sum = 0.0;
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const Pixel pixel{.x = column, .y = row};
            const Seen& here = core::at(seen, index_of(pixel));
            if (here.solid == kNoSurface || !interior(seen, pixel, 2)) {
                continue;
            }
            const std::array<float, 4>& result = core::at(final_image.texels, index_of(pixel));
            const double reference = reference_occlusion(here);
            occluded_sum += reference;
            errors.push_back(std::abs(static_cast<double>(result.front()) - reference));
            short_history += core::at(core::at(history.texels, index_of(pixel)), 1) ==
                                     static_cast<float>(render::vulkan::kAoMaxHistory)
                                 ? 0
                                 : 1;
            const Rgb want = shaded(solid_of(here.solid), std::clamp(static_cast<double>(result.front()), 0.0, 1.0));
            colour_wrong += between(pixel_at(texels, pixel), want, want) ? 0 : 1;
        }
    }
    double mean = 0.0;
    for (const double error : errors) {
        mean += error;
    }
    mean /= static_cast<double>(std::max<std::size_t>(1, errors.size()));
    constexpr double kMeanBound = 0.03;
    constexpr double kTailShare = 0.99;
    constexpr double kTailBound = 0.12;
    constexpr double kEnoughOcclusion = 200.0; // (summed over the pixels: the comparison isn't all open ground)
    const double tail = percentile(errors, kTailShare);
    tally->check(mean <= kMeanBound && tail <= kTailBound && occluded_sum >= kEnoughOcclusion,
                 text::format("still camera, %d frames: the result near the 256-ray reference over %zu pixels - mean "
                              "error %.4f (at most %.2f), 99th percentile %.4f (at most %.2f)",
                              kStillFrames, errors.size(), mean, kMeanBound, tail, kTailBound));
    tally->check(short_history == 0,
                 text::format("every history %u frames long: %d shorter", render::vulkan::kAoMaxHistory, short_history));
    tally->check(colour_wrong == 0, text::format("the colour multiplied by the result: %d pixels not", colour_wrong));
}

// After the camera moved: where the frame before saw the surface the history is kept, where it didn't it starts again.
void compare_moved(Tally* tally)
{
    render::vulkan::RtAoTexels final_image;
    render::vulkan::RtAoTexels history;
    const bool read = read_ao(render::vulkan::RtAoImage::kFinal, &final_image) &&
                      read_ao(render::vulkan::RtAoImage::kHistory, &history);
    tally->check(read, "the denoiser's images read back");
    if (!read) {
        return;
    }
    tally->check(count_not_finite(final_image) == 0 && count_not_finite(history) == 0,
                 text::format("no NaN or infinity: %d in the result, %d in the history", count_not_finite(final_image),
                              count_not_finite(history)));
    const View before = view_from(kEye);
    const View now = view_from(kMovedEye);
    const std::vector<Seen> seen_before = seen_all(before);
    const std::vector<Seen> seen_now = seen_all(now);
    int kept = 0;
    int kept_wrong = 0;
    int fresh = 0;
    int fresh_wrong = 0;
    double kept_error = 0.0;
    double fresh_error = 0.0;
    constexpr double kAgreeMargin = 0.25;  // (well inside the pass's plane tolerance)
    constexpr double kDisagreeMargin = 3.0; // (well outside it)
    constexpr double kTolerance = 0.5;
    constexpr double kLinear = 0.005;
    constexpr double kSameNormal = 0.99;
    constexpr double kOtherNormal = 0.5;
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const Pixel pixel{.x = column, .y = row};
            const Seen& here = core::at(seen_now, index_of(pixel));
            if (here.solid == kNoSurface || !interior(seen_now, pixel, 1)) {
                continue;
            }
            std::array<double, 2> at{};
            if (!pixel_of(before, here.point, &at)) {
                continue;
            }
            const auto base_x = static_cast<int>(std::floor(core::at(at, 0)));
            const auto base_y = static_cast<int>(std::floor(core::at(at, 1)));
            const double plane = kTolerance + (kLinear * here.distance);
            int agree = 0;
            int disagree = 0;
            for (int corner = 0; corner < 4; ++corner) {
                const Pixel texel{.x = base_x + (corner & 1), .y = base_y + (corner >> 1)};
                if (texel.x < 0 || texel.y < 0 || texel.x >= kColumns || texel.y >= kRows) {
                    ++disagree;
                    continue;
                }
                const Seen& was = core::at(seen_before, index_of(texel));
                const double off_plane =
                    was.solid == kNoSurface ? kNoHit : std::abs(dot(here.normal, minus(was.point, here.point)));
                if (was.solid != kNoSurface && dot(here.normal, was.normal) > kSameNormal && off_plane < kAgreeMargin * plane) {
                    ++agree;
                } else if (was.solid == kNoSurface || dot(here.normal, was.normal) < kOtherNormal ||
                           off_plane > kDisagreeMargin * plane) {
                    ++disagree;
                }
            }
            const std::array<float, 4>& length = core::at(history.texels, index_of(pixel));
            const double error =
                std::abs(static_cast<double>(core::at(final_image.texels, index_of(pixel)).front()) -
                         reference_occlusion(here));
            if (agree == 4 && interior(seen_before, {.x = base_x, .y = base_y}, 2)) {
                ++kept;
                kept_wrong += core::at(length, 1) == static_cast<float>(render::vulkan::kAoMaxHistory) ? 0 : 1;
                kept_error += error;
            } else if (disagree == 4) {
                ++fresh;
                fresh_wrong += core::at(length, 1) == 1.0F ? 0 : 1;
                fresh_error += error;
            }
        }
    }
    constexpr int kEnoughKept = 2000;
    constexpr int kEnoughFresh = 100;
    tally->check(kept >= kEnoughKept && kept_wrong == 0,
                 text::format("seen the frame before: the history kept (%u long) - %d of %d not",
                              render::vulkan::kAoMaxHistory, kept_wrong, kept));
    tally->check(fresh >= kEnoughFresh && fresh_wrong == 0,
                 text::format("disoccluded: the history started again (1 long) - %d of %d not", fresh_wrong, fresh));
    constexpr double kKeptBound = 0.04;
    constexpr double kFreshBound = 0.2;
    const double kept_mean = kept_error / std::max(1, kept);
    const double fresh_mean = fresh_error / std::max(1, fresh);
    tally->check(kept_mean <= kKeptBound && fresh_mean <= kFreshBound,
                 text::format("errors after the move: kept %.4f (at most %.2f), disoccluded %.4f (at most %.2f - no "
                              "ghost of what was there)",
                              kept_mean, kKeptBound, fresh_mean, kFreshBound));
}

// --- the cases -----------------------------------------------------------------------------------------------------

void use_settings(const render::vulkan::RtAoSettings& settings, VkAccelerationStructureKHR scene)
{
    render::vulkan::set_rt_ao_settings(settings);
    render::vulkan::rt_effects_set_scene(scene);
}

void run_raw(render::Gpu& gpu, const Scene& scene, uint32_t samples, Tally* tally)
{
    text::print("the rays as they are (%u rays, %s):\n", kRawRays, samples == 0 ? "one sample" : "four samples");
    use_settings({.on = true, .strength = kStrength, .radius = kRadius, .rays = kRawRays, .denoise = false},
                 scene.top.handle);
    Targets targets;
    const bool made = make_targets(gpu, samples, &targets);
    tally->check(made, "the targets made");
    if (made) {
        tally->check(draw_frame(gpu, targets, kEye) == 1, "the listener recorded the passes");
        std::vector<uint8_t> texels;
        const bool read = read_colour(gpu, targets, &texels);
        tally->check(read, "the target read back");
        if (read) {
            // (four samples: the depth read at sample 0 - Vulkan's standard place, 0.375, 0.125 in the pixel)
            constexpr Offset kSampleZero{-0.125, -0.375};
            compare_raw(texels, samples == 0 ? Offset{} : kSampleZero, tally);
        }
    }
    release_targets(gpu, targets);
}

void run_denoised(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    text::print("denoised (%u rays a frame), a still camera then a moved one:\n", kDenoisedRays);
    use_settings({.on = true, .strength = kStrength, .radius = kRadius, .rays = kDenoisedRays, .denoise = true},
                 scene.top.handle);
    Targets targets;
    const bool made = make_targets(gpu, 0, &targets);
    tally->check(made, "the targets made");
    if (made) {
        int passes = 0;
        for (int frame = 0; frame < kStillFrames; ++frame) {
            passes += static_cast<int>(draw_frame(gpu, targets, kEye));
        }
        tally->check(passes == kStillFrames, text::format("%d frames, %d passes", kStillFrames, passes));
        std::vector<uint8_t> texels;
        const bool read = read_colour(gpu, targets, &texels);
        tally->check(read, "the target read back");
        if (read) {
            compare_still(texels, tally);
        }
        tally->check(draw_frame(gpu, targets, kMovedEye) == 1, "the moved camera's frame");
        compare_moved(tally);
    }
    release_targets(gpu, targets);
}

void run_untouched(render::Gpu& gpu, const char* name, const render::vulkan::RtAoSettings& settings,
                   VkAccelerationStructureKHR scene, Tally* tally)
{
    text::print("%s:\n", name);
    use_settings(settings, scene);
    Targets targets;
    const bool made = make_targets(gpu, 0, &targets);
    tally->check(made, "the targets made");
    if (made) {
        tally->check(draw_frame(gpu, targets, kEye) == 0, "no pass recorded");
        std::vector<uint8_t> texels;
        const bool read = read_colour(gpu, targets, &texels);
        tally->check(read, "the target read back");
        if (read) {
            compare_untouched(texels, tally);
        }
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
    render::vulkan::set_rt_ao_settings({.on = true, .strength = kStrength, .radius = kRadius});
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
    if (!state.context.ray_tracing_enabled()) {
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
    render::vulkan::Backend& state = render::vulkan::backend();
    Tally tally;
    Scene scene;
    bool built = false;
    {
        const std::scoped_lock lock(state.mutex);
        built = make_scene(state, &scene);
    }
    tally.check(built, "the check's acceleration structures built (3 BLAS, a TLAS of 3 instances)");
    if (built) {
        render::Gpu& gpu = render::vulkan::gpu();
        run_raw(gpu, scene, 0, &tally);
        constexpr uint32_t kFourSamples = 4;
        run_raw(gpu, scene, kFourSamples, &tally);
        run_denoised(gpu, scene, &tally);
        const render::vulkan::RtAoSettings on{.on = true, .strength = kStrength, .radius = kRadius};
        run_untouched(gpu, "no scene: the passes skipped", on, VK_NULL_HANDLE, &tally);
        render::vulkan::RtAoSettings off = on;
        off.on = false;
        run_untouched(gpu, "rt_ao off: no pass", off, scene.top.handle, &tally);
    }
    render::vulkan::rt_effects_set_scene(VK_NULL_HANDLE);
    render::vulkan::set_rt_ao_settings({});
    {
        const std::scoped_lock lock(state.mutex);
        render::vulkan::finish_work(state);
        release_scene(state, &scene);
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
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rt_effects_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan ray-traced ambient occlusion check", kWindowWidth, kWindowHeight,
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
