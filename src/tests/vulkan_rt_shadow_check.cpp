// NOT ORIGINAL - thug2_vulkan_rt_shadow_check.exe: the Vulkan Gpu's ray-traced sun shadows (gpu_vulkan/
// vulkan_rt_shadows.h) tried on this machine against a reference worked out on the processor.
//
// A small scene - a ground square, a box floating over it (a world caster, instance mask 0x01), a box standing on it
// (an object, 0x02) and one that never casts (0x80) - is drawn through render::Gpu as render_pc draws a level (the
// world's view and projection from the same D3DX-style matrices, unlit vertex colours, depth tested and written); its
// acceleration structures are built here (one BLAS per mask, a TLAS of three instances) and handed over with
// rt_shadows_set_scene; the frame record gets a directional light; rt::opaque_view_drawn runs the pass through the
// listener, as draw_viewport does. The target is read back and each pixel compared with the processor's answer: the
// pixel's ray from the camera through it (from the camera's own position and lens, not the matrices) hits the nearest
// surface, whose ray toward the light is blocked or not:
//   - pixels whose 5x5 neighbourhood agrees (same face, same answer) - "clear-cut" - must be the surface's colour, or
//     that colour x (1 - strength) when shadowed, within 2 per channel;
//   - the others (silhouettes and shadow edges) must lie between the darkest and lightest answer around them;
//   - the sky (nothing drawn) is untouched.
// Cases: one sample; four samples (resolved for the read-back); the objects casting (rt_shadow_objects); soft shadows
// (an angular radius and several rays: only the clear-cut pixels further than the penumbra are compared); a split
// view (the pass on the right viewport only - the left one, drawn the same, must be left alone); no scene (the pass
// skipped, nothing changed) - all of these with rt_shadow_covered (shadows under roofs too) - and the sky test (the
// default: a shadowed point that sees no sky - under the floating box - left lit; one that sees some of it, either).
//   thug2_vulkan_rt_shadow_check [--device N] [--display N] [--no-validation]
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
#include "gpu_vulkan/vulkan_rt_shadows.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
using Vec3 = std::array<double, 3>;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr uint32_t kTargetWidth = 320;
constexpr uint32_t kTargetHeight = 240;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x042; // XYZ | DIFFUSE
constexpr uint32_t kClearColour = 0xFF102030;
constexpr float kStrength = 0.5F;
constexpr int kTolerance = 2;
constexpr int kClearCut = 2;      // the neighbourhood's radius for a hard shadow's clear-cut pixels
constexpr int kClearCutSoft = 6;  // and a soft one's
constexpr float kNearZ = 2.0F;
constexpr float kFarZ = 2000.0F;
constexpr double kHalfFovTan = 0.57735026918962576; // tan 30 degrees: a 60-degree horizontal field of view
constexpr double kNoHit = std::numeric_limits<double>::infinity();
constexpr std::size_t kNoSurface = 0;
constexpr int kMinShadowed = 400; // (the comparison isn't empty)
constexpr int kMinLit = 400;
constexpr int kMinCovered = 30;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint32_t kRedShift = 16;
constexpr uint32_t kGreenShift = 8;
constexpr std::size_t kPixelBytes = 4;

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
constexpr uint32_t kMaskNever = 0x80; // (never a caster: the pass's mask leaves it out)
const std::array<Solid, 4> kSolids{{
    {.low = {-kGroundHalf, 0.0, -kGroundHalf},
     .high = {kGroundHalf, 0.0, kGroundHalf},
     .colour = 0xFFC8C8C8,
     .mask = render::vulkan::kMaskWorld},
    {.low = {-15.0, 12.0, -15.0}, .high = {15.0, 30.0, 15.0}, .colour = 0xFF5078B4, .mask = render::vulkan::kMaskWorld},
    {.low = {38.0, 0.0, 28.0}, .high = {54.0, 24.0, 44.0}, .colour = 0xFFB45050, .mask = render::vulkan::kMaskObjects},
    {.low = {-62.0, 0.0, 30.0}, .high = {-46.0, 20.0, 46.0}, .colour = 0xFF50B450, .mask = kMaskNever},
}};

constexpr Vec3 kLightTravels{0.45, -1.0, 0.3}; // (the frame record's: the way the light travels)
constexpr Vec3 kEye{10.0, 120.0, 150.0};
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
Vec3 toward_light() { return normalised({-x_of(kLightTravels), -y_of(kLightTravels), -z_of(kLightTravels)}); }
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
        // (entered through the low face going up the axis, through the high one going down)
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

// A viewport: x, y, width, height in the target's pixels.
using Rect = std::array<uint32_t, 4>;
uint32_t left_of(const Rect& rect) { return core::at(rect, 0); }
uint32_t top_of(const Rect& rect) { return core::at(rect, 1); }
uint32_t width_of(const Rect& rect) { return core::at(rect, 2); }
uint32_t height_of(const Rect& rect) { return core::at(rect, 3); }

// The camera: its axes (Direct3D's right-handed look-at: z backward), and its lens for a viewport of this aspect.
struct View {
    Vec3 right{};
    Vec3 up{};
    Vec3 back{};
    double half_width = 0.0; // at distance 1
    double half_height = 0.0;
    Rect rect{};
};
View view_for(const Rect& rect)
{
    View view;
    view.back = normalised({x_of(kEye) - x_of(kAt), y_of(kEye) - y_of(kAt), z_of(kEye) - z_of(kAt)});
    view.right = normalised(cross(kUp, view.back));
    view.up = cross(view.back, view.right);
    view.half_width = kHalfFovTan;
    view.half_height = kHalfFovTan * static_cast<double>(height_of(rect)) / static_cast<double>(width_of(rect));
    view.rect = rect;
    return view;
}

struct Pixel {
    int x = 0;
    int y = 0;
};

// What the processor says about a pixel: the surface seen (index + 1; 0: the sky), the face's normal and whether it is
// in shadow (facing away from the light, or its ray toward the light blocked by a caster).
struct Answer {
    std::size_t surface = kNoSurface;
    Vec3 normal{};
    bool shadowed = false;
    bool covered = false;   // (the sky test) the sun blocked, but no sky seen: left lit
    bool uncertain = false; // (the sky test) some of the sky seen: the pass's few rays decide
};

// Whether a ray from `origin` along `direction` meets a caster.
bool caster_hit(const Vec3& origin, const Vec3& direction, uint32_t casters)
{
    return std::ranges::any_of(kSolids, [&](const Solid& solid) {
        return (solid.mask & casters) != 0 && intersect(solid, {.origin = origin, .direction = direction}).distance < kNoHit;
    });
}

// The sky test's answer for a shadowed point (vulkan_rt_shadows.cpp: rays straight up and slanted round it by the
// pass's SKY_TILT): none of them reaching the sky - covered, no shadow; all of them - the shadow; some - either.
constexpr double kSkyTilt = 0.6;
constexpr int kSkyAzimuths = 64;
void sky_test(const Vec3& origin, uint32_t casters, Answer* answer)
{
    int blocked = caster_hit(origin, {0.0, 1.0, 0.0}, casters) ? 1 : 0;
    for (int i = 0; i < kSkyAzimuths; ++i) {
        const double angle = 6.283185307179586 * i / kSkyAzimuths;
        blocked += caster_hit(origin, normalised({std::cos(angle) * kSkyTilt, 1.0, std::sin(angle) * kSkyTilt}), casters)
                       ? 1
                       : 0;
    }
    if (blocked == kSkyAzimuths + 1) {
        answer->shadowed = false;
        answer->covered = true;
    } else if (blocked > 0) {
        answer->uncertain = true;
    }
}

Answer reference(const View& view, Pixel pixel, uint32_t casters, bool sky)
{
    // (Direct3D 9's pixel centres: whole coordinates)
    const double ndc_x = (2.0 * (pixel.x - static_cast<double>(left_of(view.rect))) / width_of(view.rect)) - 1.0;
    const double ndc_y = 1.0 - (2.0 * (pixel.y - static_cast<double>(top_of(view.rect))) / height_of(view.rect));
    const Vec3 direction = normalised(
        step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height), view.back, -1.0));
    Answer answer;
    Hit nearest;
    for (std::size_t index = 0; index < kSolids.size(); ++index) {
        const Hit hit = intersect(core::at(kSolids, index), {.origin = kEye, .direction = direction});
        if (hit.distance < nearest.distance) {
            nearest = hit;
            answer.surface = index + 1;
        }
    }
    if (answer.surface == kNoSurface) {
        return answer;
    }
    answer.normal = nearest.normal;
    const Vec3 light = toward_light();
    constexpr double kOffSurface = 1e-6;
    const Vec3 origin = step(step(kEye, direction, nearest.distance), nearest.normal, kOffSurface);
    // (an object's face - the skater's - takes the world's shadows only, and is never shadowed for facing away from the
    // light: the pass's normal is the triangle's, flat - vulkan_rt_shadows.cpp)
    const bool object = (core::at(kSolids, answer.surface - 1).mask & render::vulkan::kMaskObjects) != 0;
    answer.shadowed = object ? caster_hit(origin, light, casters & ~render::vulkan::kMaskObjects)
                             : dot(nearest.normal, light) <= 0.0 || caster_hit(origin, light, casters);
    if (answer.shadowed && sky) {
        sky_test(origin, casters, &answer);
    }
    return answer;
}

// A colour's red, green, blue (0 - 255).
using Rgb = std::array<int, 3>;
Rgb rgb_of(uint32_t colour)
{
    return {static_cast<int>((colour >> kRedShift) & kByteMask), static_cast<int>((colour >> kGreenShift) & kByteMask),
            static_cast<int>(colour & kByteMask)};
}
Rgb darkened(const Rgb& colour, float strength)
{
    Rgb out{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        core::at(out, channel) = static_cast<int>(
            std::lround(core::at(colour, channel) * (1.0 - static_cast<double>(strength))));
    }
    return out;
}
Rgb colour_of(std::size_t surface, bool shadowed, float strength)
{
    if (surface == kNoSurface) {
        return rgb_of(kClearColour);
    }
    const Rgb colour = rgb_of(core::at(kSolids, surface - 1).colour);
    return shadowed ? darkened(colour, strength) : colour;
}
bool close_to(const Rgb& got, const Rgb& want)
{
    for (std::size_t channel = 0; channel < 3; ++channel) {
        if (std::abs(core::at(got, channel) - core::at(want, channel)) > kTolerance) {
            return false;
        }
    }
    return true;
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

// An array's bytes.
template <typename T>
std::span<const uint8_t> bytes_of(std::span<const T> items)
{
    const void* const start = items.data();
    return {static_cast<const uint8_t*>(start), items.size_bytes()};
}

// A buffer the build reads, filled from `bytes` at a 16-byte aligned address (instances need it), which goes to
// `address`.
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

// One build: its geometry and sizes asked, the structure made, built at once (waited for).
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
    // (at least any device's minAccelerationStructureScratchOffsetAlignment)
    constexpr VkDeviceSize kScratchAlignment = 256;
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

// The three BLASes (one per mask: the ground and the floating box share 0x01) and the TLAS of their instances.
bool make_scene(render::vulkan::Backend& state, Scene* scene)
{
    constexpr std::array<std::size_t, 2> kWorld{0, 1};
    constexpr std::array<std::size_t, 1> kObject{2};
    constexpr std::array<std::size_t, 1> kNever{3};
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

// The world's matrices for a viewport (Direct3D's row-vector layout, as render_pc's choose_camera makes them).
constexpr std::size_t kMatrixFloats = 16;
using Matrix = std::array<float, kMatrixFloats>;
struct Matrices {
    Matrix view{};
    Matrix projection{};
};
Matrices matrices_for(const Rect& rect)
{
    Matrices out;
    maths::matrix_look_at_rh(out.view, floats(kEye), floats(kAt), floats(kUp));
    const double width = 2.0 * kNearZ * kHalfFovTan;
    const double height = width * static_cast<double>(height_of(rect)) / static_cast<double>(width_of(rect));
    maths::matrix_perspective_rh(out.projection, static_cast<float>(width), static_cast<float>(height), kNearZ, kFarZ);
    return out;
}

void draw_scene(render::Gpu& gpu, const Rect& rect)
{
    using render::GpuRenderState;
    using render::GpuStageState;
    gpu.set_viewport({.x = left_of(rect),
                      .y = top_of(rect),
                      .width = width_of(rect),
                      .height = height_of(rect),
                      .min_z = 0.0F,
                      .max_z = 1.0F});
    const Matrices matrices = matrices_for(rect);
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

render::rt::ViewDrawn view_drawn(const Rect& rect)
{
    const Matrices matrices = matrices_for(rect);
    render::rt::ViewDrawn view;
    constexpr std::size_t kSide = 4;
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(view.camera.view, row), column) = core::at(matrices.view, (row * kSide) + column);
            core::at(core::at(view.camera.projection, row), column) =
                core::at(matrices.projection, (row * kSide) + column);
        }
    }
    view.camera.position = floats(kEye);
    view.rect = rect;
    view.depth_range = {0.0F, 1.0F};
    return view;
}

// --- a case ------------------------------------------------------------------------------------------------------

struct Case {
    const char* name = "";
    uint32_t samples = 0; // D3DMULTISAMPLE_TYPE
    render::vulkan::RtShadowSettings settings;
    bool split = false;    // two viewports drawn, the pass on the right one only
    bool no_scene = false; // rt_shadows_set_scene(null): the pass skipped
};

constexpr uint32_t kHalfWidth = kTargetWidth / 2;
constexpr int kColumns = static_cast<int>(kTargetWidth);
constexpr int kRows = static_cast<int>(kTargetHeight);
constexpr int kFirstRightColumn = static_cast<int>(kHalfWidth);
constexpr Rect kWhole{0, 0, kTargetWidth, kTargetHeight};
constexpr Rect kLeft{0, 0, kHalfWidth, kTargetHeight};
constexpr Rect kRight{kHalfWidth, 0, kTargetWidth - kHalfWidth, kTargetHeight};

// The pixel read back (X8R8G8B8: blue, green, red, unused).
Rgb pixel_at(const std::vector<uint8_t>& texels, Pixel pixel)
{
    const std::size_t at =
        ((static_cast<std::size_t>(pixel.y) * kTargetWidth) + static_cast<std::size_t>(pixel.x)) * kPixelBytes;
    return {core::at(texels, at + 2), core::at(texels, at + 1), core::at(texels, at)};
}

// What a pixel should be: its answer, whether its neighbourhood agrees (clear-cut), and the darkest and lightest
// colours its neighbourhood could give it.
struct Expected {
    Answer answer;
    bool clear_cut = true;
    float strength = 0.0F;
    Rgb darkest{};
    Rgb lightest{};
};
// What the pixels are compared with: the casters' mask, the neighbourhood's radius.
struct Comparison {
    uint32_t casters = 0;
    int radius = 0;
};
Expected expected_at(const Case& test, const Comparison& comparison, Pixel pixel)
{
    const uint32_t casters = comparison.casters;
    const int radius = comparison.radius;
    const bool in_left = test.split && pixel.x < kFirstRightColumn;
    Rect rect = kWhole;
    if (test.split) {
        rect = in_left ? kLeft : kRight;
    }
    const View view = view_for(rect);
    Expected out;
    // (the left viewport of a split isn't passed over: its pixels stay as drawn)
    out.strength = in_left || test.no_scene ? 0.0F : test.settings.strength;
    const bool sky = !test.settings.covered;
    out.answer = reference(view, pixel, casters, sky);
    out.clear_cut = !out.answer.uncertain;
    out.darkest = colour_of(out.answer.surface, out.answer.shadowed, out.strength);
    out.lightest = out.darkest;
    for (int down = -radius; down <= radius; ++down) {
        for (int across = -radius; across <= radius; ++across) {
            const Pixel neighbour{.x = std::clamp(pixel.x + across, 0, kColumns - 1),
                                  .y = std::clamp(pixel.y + down, 0, kRows - 1)};
            if ((test.split && neighbour.x < kFirstRightColumn) != in_left) {
                out.clear_cut = false;
                continue;
            }
            const Answer there = reference(view, neighbour, casters, sky);
            // (one face of one solid: the normal the pass rebuilds from the depth is the face's away from its creases)
            out.clear_cut = out.clear_cut && there.surface == out.answer.surface &&
                            there.normal == out.answer.normal && there.shadowed == out.answer.shadowed &&
                            !there.uncertain;
            for (const bool dark : {false, true}) {
                const Rgb colour = colour_of(there.surface, dark, out.strength);
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    core::at(out.darkest, channel) = std::min(core::at(out.darkest, channel), core::at(colour, channel));
                    core::at(out.lightest, channel) =
                        std::max(core::at(out.lightest, channel), core::at(colour, channel));
                }
            }
        }
    }
    return out;
}

struct Counts {
    int clear_cut = 0;
    int clear_wrong = 0;
    int shadowed = 0;
    int lit = 0;
    int edges = 0;
    int edge_wrong = 0;
    int sky_wrong = 0;
    int covered = 0; // clear-cut pixels the sky test leaves lit
    std::string first_wrong;
};

void count_pixel(const Expected& expected, const Rgb& got, Pixel pixel, Counts* counts)
{
    if (expected.answer.surface == kNoSurface && expected.clear_cut) {
        counts->sky_wrong += close_to(got, rgb_of(kClearColour)) ? 0 : 1;
        return;
    }
    if (expected.clear_cut) {
        ++counts->clear_cut;
        const Rgb want = colour_of(expected.answer.surface, expected.answer.shadowed, expected.strength);
        if (!close_to(got, want)) {
            ++counts->clear_wrong;
            if (counts->first_wrong.empty()) {
                counts->first_wrong =
                    text::format(" (first at %d,%d: %d %d %d, %d %d %d wanted)", pixel.x, pixel.y, core::at(got, 0),
                                 core::at(got, 1), core::at(got, 2), core::at(want, 0), core::at(want, 1),
                                 core::at(want, 2));
            }
        }
        if (expected.answer.shadowed && expected.strength > 0.0F) {
            ++counts->shadowed;
        } else {
            ++counts->lit;
        }
        counts->covered += expected.answer.covered ? 1 : 0;
        return;
    }
    ++counts->edges;
    bool between = true;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        between = between && core::at(got, channel) >= core::at(expected.darkest, channel) - kTolerance &&
                  core::at(got, channel) <= core::at(expected.lightest, channel) + kTolerance;
    }
    counts->edge_wrong += between ? 0 : 1;
}

// The picture read back compared with the reference.
void compare(const Case& test, const std::vector<uint8_t>& texels, Tally* tally)
{
    const Comparison comparison{
        .casters = render::vulkan::kMaskWorld | (test.settings.objects ? render::vulkan::kMaskObjects : 0U),
        .radius = test.settings.softness > 0.0F ? kClearCutSoft : kClearCut};
    Counts counts;
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const Pixel pixel{.x = column, .y = row};
            count_pixel(expected_at(test, comparison, pixel), pixel_at(texels, pixel), pixel, &counts);
        }
    }
    const bool enough = test.no_scene || (counts.shadowed >= kMinShadowed && counts.lit >= kMinLit);
    tally->check(counts.clear_wrong == 0 && enough,
                 text::format("clear-cut pixels as the reference: %d of %d wrong (%d shadowed, %d lit)%s",
                              counts.clear_wrong, counts.clear_cut, counts.shadowed, counts.lit,
                              counts.first_wrong.c_str()));
    tally->check(counts.edge_wrong == 0,
                 text::format("edge pixels between their darkest and lightest answers: %d of %d outside",
                              counts.edge_wrong, counts.edges));
    tally->check(counts.sky_wrong == 0, text::format("the sky left alone: %d pixels changed", counts.sky_wrong));
    if (!test.settings.covered && !test.no_scene) {
        tally->check(counts.covered >= kMinCovered,
                     text::format("the sky test: %d clear-cut pixels under the floating box left lit", counts.covered));
    }
}

// The case's targets: the colour and depth drawn into (multisampled or not), and a single-sampled copy for the
// read-back when they are.
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

// The frame: cleared, the scene drawn (into both halves for a split), the light recorded, the pass through the
// listener (draw_viewport's call); the number of passes it recorded.
uint64_t draw_frame(render::Gpu& gpu, const Case& test, const Targets& targets)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kClearColour, 1.0F, 0);
    if (test.split) {
        draw_scene(gpu, kLeft);
        draw_scene(gpu, kRight);
    } else {
        draw_scene(gpu, kWhole);
    }
    render::rt::FrameRecord& record = render::rt::frame_record();
    record.lights.clear();
    record.lights.push_back({.kind = render::rt::Light::Kind::kDirectional,
                             .colour = {1.0F, 1.0F, 1.0F},
                             .direction = floats(normalised(kLightTravels)),
                             .position = {},
                             .radius = 0.0F});
    const uint64_t before = render::vulkan::rt_shadow_passes();
    render::rt::opaque_view_drawn(view_drawn(test.split ? kRight : kWhole));
    const uint64_t passes = render::vulkan::rt_shadow_passes() - before;
    gpu.end_scene();
    return passes;
}

void run_case(render::Gpu& gpu, const Case& test, const Scene& scene, Tally* tally)
{
    text::print("%s:\n", test.name);
    render::vulkan::set_rt_shadow_settings(test.settings);
    render::vulkan::rt_shadows_set_scene(test.no_scene ? VK_NULL_HANDLE : scene.top.handle);
    Targets targets;
    const bool made = make_targets(gpu, test.samples, &targets);
    tally->check(made, "the targets made");
    if (made) {
        const uint64_t passes = draw_frame(gpu, test, targets);
        tally->check(passes == (test.no_scene ? 0U : 1U),
                     text::format("the listener %s the pass", passes == 1 ? "recorded" : "skipped"));
        std::vector<uint8_t> texels;
        GpuSurface* read_from = targets.colour;
        bool read = true;
        if (targets.resolved != nullptr) {
            read = gpu.copy_surface(targets.colour, nullptr, targets.resolved, nullptr, value::kFilterNone) ==
                   value::kResultOk;
            read_from = targets.resolved;
        }
        read = read && render::vulkan::read_surface(read_from, &texels) &&
               texels.size() == static_cast<std::size_t>(kTargetWidth) * kTargetHeight * kPixelBytes;
        tally->check(read, "the target read back");
        if (read) {
            compare(test, texels, tally);
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


// The cases, in order.
std::vector<Case> cases()
{
    constexpr float kSoftDegrees = 2.0F;
    constexpr unsigned kSoftRays = 8;
    constexpr uint32_t kFourSamples = 4;
    // (shadows under roofs too: the reference's plain rule - the sky test has a case of its own)
    // (the frame record's light as the scene's sun - a fixed rt_sun_source: there's no level bake here to find one in)
    render::rt::set_sun_choice({.source = render::rt::SunSource::kFixed, .fixed = floats(toward_light())});
    const render::vulkan::RtShadowSettings hard{.on = true, .strength = kStrength, .covered = true};
    render::vulkan::RtShadowSettings roofed = hard;
    roofed.covered = false;
    render::vulkan::RtShadowSettings objects = hard;
    objects.objects = true;
    render::vulkan::RtShadowSettings soft = hard;
    soft.softness = kSoftDegrees;
    soft.rays = kSoftRays;
    return {
        {.name = "one sample", .samples = 0, .settings = hard},
        {.name = "four samples", .samples = kFourSamples, .settings = hard},
        {.name = "the objects cast too", .samples = 0, .settings = objects},
        {.name = "soft (2 degrees, 8 rays)", .samples = 0, .settings = soft},
        {.name = "a split view: the pass on the right one", .samples = 0, .settings = hard, .split = true},
        {.name = "no scene: the pass skipped", .samples = 0, .settings = hard, .no_scene = true},
        {.name = "the sky test: no shadow where no sky is seen", .samples = 0, .settings = roofed},
    };
}

// The device made with ray tracing asked for; false (and why printed) when it can't be.
enum class Opened : uint8_t { kYes, kNoDevice, kNoRayQueries };
Opened open_device(const Options& options, SDL_Window* window)
{
    render::vulkan::set_context_options({.validation = options.validation,
                                         .device_index = options.device,
                                         .on_message = print_line,
                                         .ray_tracing = true},
                                        window);
    // (on before the device: its depth surfaces are made samplable)
    render::vulkan::set_rt_shadow_settings({.on = true, .strength = kStrength});
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
    text::print("sample-rate shading: %s\n", state.context.enabled_features().sampleRateShading == VK_TRUE
                                                 ? "yes (per-sample rays)"
                                                 : "no (per pixel)");
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
        for (const Case& test : cases()) {
            run_case(render::vulkan::gpu(), test, scene, &tally);
        }
    }
    render::vulkan::rt_shadows_set_scene(VK_NULL_HANDLE);
    render::vulkan::set_rt_shadow_settings({});
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
    text::print("thug2_vulkan_rt_shadow_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan ray-traced shadow check", kWindowWidth, kWindowHeight,
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
