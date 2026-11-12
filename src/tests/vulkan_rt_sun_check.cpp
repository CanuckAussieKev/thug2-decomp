// NOT ORIGINAL - thug2_vulkan_rt_sun_check.exe: the Vulkan Gpu's sun, its glare, the screen-space rays, the bloom and the
// sky model (gpu_vulkan/vulkan_rt_sun.h, vulkan_rt_sky.h; engine/gfx/rt_sky.h) tried on this machine against references
// worked out on the processor.
//
// A wall with a window, standing on a ground square; the sun low behind it (the console's `rt sun` override), the camera
// on the near side looking at the sun through the window. The scene is drawn through render::Gpu as render_pc draws a
// level; rt::world_view_drawn runs the passes through the listeners, as draw_viewport does.
//   - The sky model: the device's table (32 segments, 16 toward the sun) against rt_sky.h's integrals with 1024 / 512 at
//     six elevations - every entry's radiance, the sun's transmittance and colour; the colour warmer toward the horizon,
//     the zenith bluer than the horizon.
//   - The sun's disc and glare (rt_sun): the sun's place on the screen against the camera's own lens; the light added
//     at every pixel against the formula (the processor's ray through the pixel, its angle to the sun, the sky test) away
//     from the silhouettes; the original's glare reported replaced; none without a sun.
//   - The rays (rt_shafts): every half-resolution texel against the processor's march of the same points (its own sky
//     mask, ray-cast); the texels whose way to the sun crosses the window brighter than those behind the wall.
//   - The bloom: the chain's top keeps the light it was given (X: the frame + the sun's light, the disc 24 x the game's
//     white - above 1, kept by the 16-bit images with an 8-bit target); the composite into a 16-bit float target
//     (rt_hdr=1) keeps the picture's sum, values above 1 kept; into an 8-bit target (rt_hdr=0) the frame's sum kept by
//     the bloom alone, the result clipped at 1; no NaN anywhere.
//   - The aerial haze (rt_fog + rt_sky_model, no level fog): every texel's transmittance against the closed-form integral
//     of the haze's exponential height profile; its light between the sky's share and that plus the sun's; the sky never
//     dimmed.
//   - Everything off, or no sun and no bloom: no pass, the picture unchanged.
//   - --perf: the passes' cost at 1920 x 1080 (their timestamps).
//   thug2_vulkan_rt_sun_check [--device N] [--display N] [--no-validation] [--perf]
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
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_fog.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "gpu_vulkan/vulkan_rt_sun.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
namespace vk = render::vulkan;
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
constexpr double kHalfFovTan = 0.57735026918962576; // tan 30 degrees: a 60-degree horizontal field of view
constexpr double kNoHit = std::numeric_limits<double>::infinity();
constexpr std::size_t kNoSurface = 0;
constexpr std::size_t kPixelBytes = 4;
constexpr std::size_t kHalfPixelBytes = 8;
constexpr double kPerByte = 1.0 / 255.0;
constexpr uint32_t kChannels = 3;
constexpr std::array<double, 3> kLuma{0.2126, 0.7152, 0.0722};

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

// --- the scene ---------------------------------------------------------------------------------------------------

struct Solid {
    Vec3 low{};
    Vec3 high{};
    uint32_t colour = 0;
};

constexpr double kGroundHalf = 300.0;
constexpr uint32_t kMaskWorld = 0x01;
// the ground, then the wall (z -2 - 2) round a window x -20 - 20, y 60 - 100
const std::array<Solid, 5> kSolids{{
    {.low = {-kGroundHalf, 0.0, -kGroundHalf}, .high = {kGroundHalf, 0.0, kGroundHalf}, .colour = 0xFF909090},
    {.low = {-150.0, 0.0, -2.0}, .high = {-20.0, 150.0, 2.0}, .colour = 0xFFA08060},
    {.low = {20.0, 0.0, -2.0}, .high = {150.0, 150.0, 2.0}, .colour = 0xFFA08060},
    {.low = {-20.0, 0.0, -2.0}, .high = {20.0, 60.0, 2.0}, .colour = 0xFFA08060},
    {.low = {-20.0, 100.0, -2.0}, .high = {20.0, 150.0, 2.0}, .colour = 0xFFA08060},
}};

constexpr Vec3 kToSun{-0.15, 0.45, -1.0}; // (normalised below: behind the wall, 24 degrees up)
constexpr Vec3 kWindowCentre{0.0, 80.0, 0.0};
constexpr double kEyeBack = 140.0; // the eye this far back from the window's centre, away from the sun
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
    return {(y_of(one) * z_of(other)) - (z_of(one) * y_of(other)),
            (z_of(one) * x_of(other)) - (x_of(one) * z_of(other)),
            (x_of(one) * y_of(other)) - (y_of(one) * x_of(other))};
}
Vec3 step(const Vec3& from, const Vec3& along, double scale)
{
    return {x_of(from) + (x_of(along) * scale), y_of(from) + (y_of(along) * scale), z_of(from) + (z_of(along) * scale)};
}
Vec3 minus(const Vec3& one, const Vec3& other) { return step(one, other, -1.0); }
std::array<float, 3> floats(const Vec3& v)
{
    return {static_cast<float>(x_of(v)), static_cast<float>(y_of(v)), static_cast<float>(z_of(v))};
}

const Vec3& to_sun()
{
    static const Vec3 kToward = normalised(kToSun);
    return kToward;
}

// The eye, back from the window away from the sun (it sees the sun through the window), and where it looks.
struct Pose {
    Vec3 eye{};
    Vec3 at{};
};
Pose sun_pose() { return {.eye = step(kWindowCentre, to_sun(), -kEyeBack), .at = kWindowCentre}; }
// Looking away from the sun (the sun behind the camera).
Pose away_pose()
{
    const Pose pose = sun_pose();
    return {.eye = pose.eye, .at = step(pose.eye, minus(pose.eye, pose.at), 1.0)};
}
// Looking along the ground across the scene (the aerial haze's: long rays over the ground, the sky above).
Pose ground_pose() { return {.eye = {250.0, 30.0, 250.0}, .at = {-100.0, 20.0, -150.0}}; }

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

struct Ray {
    Vec3 origin{};
    Vec3 direction{};
};
double intersect(const Solid& solid, const Ray& ray)
{
    constexpr double kParallel = 1e-12;
    double enter = 0.0;
    double leave = kNoHit;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double origin = core::at(ray.origin, axis);
        const double direction = core::at(ray.direction, axis);
        const double low = core::at(solid.low, axis);
        const double high = core::at(solid.high, axis);
        if (std::abs(direction) < kParallel) {
            if (origin < low || origin > high) {
                return kNoHit;
            }
            continue;
        }
        const bool rising = direction > 0.0;
        enter = std::max(enter, ((rising ? low : high) - origin) / direction);
        leave = std::min(leave, ((rising ? high : low) - origin) / direction);
        if (enter > leave) {
            return kNoHit;
        }
    }
    return enter <= 0.0 ? kNoHit : enter;
}

using Rect = std::array<uint32_t, 4>;

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

// The direction through a point of the target (Direct3D 9's pixel centres: whole coordinates).
Vec3 direction_at(const View& view, double x, double y)
{
    const double ndc_x = (2.0 * x / view.width) - 1.0;
    const double ndc_y = 1.0 - (2.0 * y / view.height);
    return normalised(step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height),
                           view.back, -1.0));
}

// Where a direction is on the target (false: behind the camera).
bool screen_of(const View& view, const Vec3& direction, double* x, double* y)
{
    const double forward = -dot(direction, view.back);
    if (forward <= 0.0) {
        return false;
    }
    const double ndc_x = dot(direction, view.right) / (forward * view.half_width);
    const double ndc_y = dot(direction, view.up) / (forward * view.half_height);
    *x = (ndc_x + 1.0) * view.width / 2.0;
    *y = (1.0 - ndc_y) * view.height / 2.0;
    return true;
}

struct Seen {
    std::size_t solid = kNoSurface;
    double distance = -1.0;
};
Seen seen_along(const Ray& ray)
{
    Seen seen;
    double nearest = kNoHit;
    for (std::size_t index = 0; index < kSolids.size(); ++index) {
        const double hit = intersect(core::at(kSolids, index), ray);
        if (hit < nearest) {
            nearest = hit;
            seen.solid = index + 1;
        }
    }
    if (seen.solid != kNoSurface) {
        seen.distance = nearest;
    }
    return seen;
}

// The sky test of every pixel (the processor's rays): true where nothing is in front.
std::vector<uint8_t> sky_mask(const View& view)
{
    std::vector<uint8_t> mask(static_cast<std::size_t>(view.width) * view.height);
    for (uint32_t y = 0; y < view.height; ++y) {
        for (uint32_t x = 0; x < view.width; ++x) {
            const Seen seen = seen_along({.origin = view.eye, .direction = direction_at(view, x, y)});
            core::at(mask, (static_cast<std::size_t>(y) * view.width) + x) = seen.solid == kNoSurface ? 1 : 0;
        }
    }
    return mask;
}

// Whether a pixel's 3 x 3 neighbourhood is all sky or all surface (away from the silhouettes).
bool clear_cut(const View& view, const std::vector<uint8_t>& mask, int x, int y)
{
    const uint8_t centre = core::at(mask, (static_cast<std::size_t>(y) * view.width) + x);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = std::clamp(x + dx, 0, static_cast<int>(view.width) - 1);
            const int ny = std::clamp(y + dy, 0, static_cast<int>(view.height) - 1);
            if (core::at(mask, (static_cast<std::size_t>(ny) * view.width) + nx) != centre) {
                return false;
            }
        }
    }
    return true;
}

double angle_between(const Vec3& one, const Vec3& other)
{
    return std::atan2(std::hypot(x_of(cross(one, other)), y_of(cross(one, other)), z_of(cross(one, other))),
                      dot(one, other));
}

// --- the acceleration structures (the check's own: the aerial haze's ray queries) -----------------------------------

struct Structure {
    vk::DeviceBuffer storage;
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
};

struct Scene {
    std::vector<vk::DeviceBuffer> inputs;
    std::vector<Structure> bottoms;
    Structure top;
    vk::DeviceBuffer scratch;
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

bool make_structure(vk::Backend& state, VkAccelerationStructureTypeKHR type, VkDeviceSize bytes, Structure* out)
{
    if (!state.memory.create_buffer(
            bytes, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            vk::BufferPlace::kDevice, &out->storage)) {
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

bool make_input(vk::Backend& state, std::span<const uint8_t> bytes, vk::DeviceBuffer* out, VkDeviceAddress* address)
{
    constexpr VkDeviceSize kAlignment = 16;
    if (!state.memory.create_buffer(bytes.size() + kAlignment,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    vk::BufferPlace::kUpload, out) ||
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
bool build(vk::Backend& state, Scene* scene, const BuildInput& input, Structure* out)
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
    constexpr VkDeviceSize kScratchAlignment = 256;
    if (scene->scratch.buffer != VK_NULL_HANDLE) {
        state.memory.destroy_buffer(&scene->scratch);
    }
    if (!state.memory.create_buffer(sizes.buildScratchSize + kScratchAlignment,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    vk::BufferPlace::kDevice, &scene->scratch)) {
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

bool make_scene(vk::Backend& state, Scene* scene)
{
    std::vector<std::array<float, 3>> vertices;
    for (const Solid& solid : kSolids) {
        const std::vector<std::array<float, 3>> more = triangles_of(solid);
        vertices.insert(vertices.end(), more.begin(), more.end());
    }
    vk::DeviceBuffer& input = scene->inputs.emplace_back();
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
    instance.mask = kMaskWorld;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = bottom.address;
    const std::array<VkAccelerationStructureInstanceKHR, 1> instances{instance};
    vk::DeviceBuffer& list = scene->inputs.emplace_back();
    VkDeviceAddress list_address = 0;
    if (!make_input(state, bytes_of(std::span<const VkAccelerationStructureInstanceKHR>(instances)), &list,
                    &list_address)) {
        return false;
    }
    VkAccelerationStructureGeometryKHR top{};
    top.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    top.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    top.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    top.geometry.instances.arrayOfPointers = VK_FALSE;
    top.geometry.instances.data.deviceAddress = list_address;
    return build(state, scene, {.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, .geometry = &top, .primitives = 1},
                 &scene->top);
}

void release_scene(vk::Backend& state, Scene* scene)
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
    for (vk::DeviceBuffer& buffer : scene->inputs) {
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
Matrices matrices_for(const Pose& pose, uint32_t width, uint32_t height)
{
    Matrices out;
    maths::matrix_look_at_rh(out.view, floats(pose.eye), floats(pose.at), floats(kUp));
    const double lens_width = 2.0 * kNearZ * kHalfFovTan;
    const double lens_height = lens_width * static_cast<double>(height) / static_cast<double>(width);
    maths::matrix_perspective_rh(out.projection, static_cast<float>(lens_width), static_cast<float>(lens_height),
                                 kNearZ, kFarZ);
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
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(vertices.size() / 3),
                         vertices.data(), sizeof(ColourVertex));
}

render::rt::ViewDrawn view_drawn(const Pose& pose, uint32_t width, uint32_t height)
{
    const Matrices matrices = matrices_for(pose, width, height);
    render::rt::ViewDrawn view;
    constexpr std::size_t kSide = 4;
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(view.camera.view, row), column) = core::at(matrices.view, (row * kSide) + column);
            core::at(core::at(view.camera.projection, row), column) =
                core::at(matrices.projection, (row * kSide) + column);
        }
    }
    view.camera.position = floats(pose.eye);
    view.rect = {0, 0, width, height};
    view.depth_range = {0.0F, 1.0F};
    view.fog = {}; // (no level fog)
    return view;
}

// --- the targets and a frame ---------------------------------------------------------------------------------------

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

// One frame: the scene, then the listeners; the sun's passes recorded.
uint64_t draw_frame(render::Gpu& gpu, const Targets& targets, const Pose& pose, bool sun = true)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = targets.width, .height = targets.height, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kClearColour, 1.0F, 0);
    draw_scene(gpu, pose, targets.width, targets.height);
    const std::array<float, 3> toward = floats(to_sun());
    render::rt::set_sun_override(sun ? &toward : nullptr);
    const uint64_t before = vk::rt_sun_passes();
    render::rt::world_view_drawn(view_drawn(pose, targets.width, targets.height));
    const uint64_t passes = vk::rt_sun_passes() - before;
    gpu.end_scene();
    return passes;
}

// The colour target as floats (rgb per pixel): 8-bit bytes / 255, or the 16-bit floats.
bool read_colour(const Targets& targets, std::vector<Rgb>* out)
{
    std::vector<uint8_t> bytes;
    const std::size_t pixels = static_cast<std::size_t>(targets.width) * targets.height;
    if (!vk::read_surface(targets.colour, &bytes) ||
        bytes.size() != pixels * (targets.floats ? kHalfPixelBytes : kPixelBytes)) {
        return false;
    }
    out->resize(pixels);
    for (std::size_t i = 0; i < pixels; ++i) {
        Rgb& colour = core::at(*out, i);
        if (targets.floats) {
            for (std::size_t c = 0; c < kChannels; ++c) {
                uint16_t half = 0;
                std::memcpy(&half, &core::at(bytes, (i * kHalfPixelBytes) + (c * sizeof(uint16_t))), sizeof(half));
                core::at(colour, c) = vk::half_to_float(half);
            }
        } else {
            // (B8G8R8A8 in memory)
            core::at(colour, 0) = core::at(bytes, (i * kPixelBytes) + 2) * kPerByte;
            core::at(colour, 1) = core::at(bytes, (i * kPixelBytes) + 1) * kPerByte;
            core::at(colour, 2) = core::at(bytes, (i * kPixelBytes) + 0) * kPerByte;
        }
    }
    return true;
}

bool read_sun(vk::RtSunImage which, uint32_t width, uint32_t height, vk::RtSunTexels* out)
{
    vk::Backend& state = vk::backend();
    const std::scoped_lock lock(state.mutex);
    return vk::read_rt_sun_image(state, {0, 0, width, height}, which, out);
}

bool read_fog(vk::RtFogImage which, vk::RtFogTexels* out)
{
    vk::Backend& state = vk::backend();
    const std::scoped_lock lock(state.mutex);
    return vk::read_rt_fog_image(state, {0, 0, kTargetWidth, kTargetHeight}, which, out);
}

int count_not_finite(const std::vector<std::array<float, 4>>& texels)
{
    int count = 0;
    for (const std::array<float, 4>& texel : texels) {
        count += std::ranges::all_of(texel, [](float v) { return std::isfinite(v); }) ? 0 : 1;
    }
    return count;
}

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

double luma(const Rgb& c) { return dot(c, kLuma); }

// --- the settings --------------------------------------------------------------------------------------------------

vk::RtSunSettings sun_settings(bool sun, bool shafts, bool bloom, bool hdr)
{
    vk::RtSunSettings settings;
    settings.sun = sun;
    settings.shafts = shafts;
    settings.bloom = bloom;
    settings.hdr = hdr;
    return settings;
}

// --- 1. the sky model ----------------------------------------------------------------------------------------------

constexpr double kMostTableError = 0.04;     // relative, mean over a table (16-bit floats, 32 segments)
constexpr double kMostTableError99 = 0.12;   // relative, 99th percentile
constexpr double kLeastTableValue = 1e-3;    // entries darker than this compared absolutely
constexpr double kMostColourError = 0.01;    // the sun's colour and transmittance, absolute
constexpr std::array<double, 6> kElevations{2.0, 5.0, 10.0, 20.0, 45.0, 80.0}; // degrees
constexpr double kDegrees = 3.14159265358979 / 180.0;
// the table's reference: 8 x the device's segments (rt_sky.h's kReferenceSteps for single values - 2048 entries of them
// would take minutes)
constexpr render::rt::SkySteps kTableReference{.view = 256, .light = 128};

void run_sky(Tally* tally)
{
    text::print("the sky model: the device's table against rt_sky.h's integrals (256 / 128 segments; single values 1024 / 512):\n");
    vk::Backend& state = vk::backend();
    double last_warmth = -1.0;
    bool warmer_lower = true;
    for (const double degrees : kElevations) {
        const double elevation = degrees * kDegrees;
        const std::array<float, 3> toward{static_cast<float>(std::cos(elevation)), static_cast<float>(std::sin(elevation)),
                                          0.0F};
        std::vector<std::array<float, 4>> table;
        bool made = false;
        {
            const std::scoped_lock lock(state.mutex);
            vk::invalidate_sky();
            made = vk::update_sky(state, vk::outside_pass(state), toward) && vk::read_sky_table(state, &table);
        }
        const std::size_t width = render::rt::kSkyLutWidth;
        const std::size_t rows = render::rt::kSkyLutHeight;
        if (!made || table.size() != width * (rows + 1)) {
            tally->check(false, text::format("%.0f degrees: the table made and read back", degrees));
            continue;
        }
        const render::rt::Dir sun = render::rt::sun_direction_at(elevation);
        const double exposure = render::rt::sky_exposure();
        std::vector<double> errors;
        for (std::size_t row = 0; row < rows; ++row) {
            for (std::size_t column = 0; column < width; ++column) {
                const render::rt::Dir dir =
                    render::rt::sky_lut_direction(static_cast<uint32_t>(column), static_cast<uint32_t>(row));
                const Rgb want = render::rt::sky_radiance(dir, sun, kTableReference);
                const std::array<float, 4>& got = core::at(table, (row * width) + column);
                for (std::size_t c = 0; c < kChannels; ++c) {
                    const double expected = core::at(want, c) * exposure;
                    const double difference = std::abs(static_cast<double>(core::at(got, c)) - expected);
                    errors.push_back(expected > kLeastTableValue ? difference / expected : difference / kLeastTableValue);
                }
            }
        }
        const Rgb transmittance = render::rt::sun_transmittance(elevation, render::rt::kReferenceSteps.light);
        const std::array<float, 3> colour = render::rt::sun_colour_at(elevation);
        const std::array<float, 4>& got_t = core::at(table, rows * width);
        const std::array<float, 4>& got_c = core::at(table, (rows * width) + 1);
        double colour_error = 0.0;
        for (std::size_t c = 0; c < kChannels; ++c) {
            colour_error = std::max(colour_error, std::abs(static_cast<double>(core::at(got_t, c)) - core::at(transmittance, c)));
            colour_error = std::max(colour_error,
                                    std::abs(static_cast<double>(core::at(got_c, c)) - static_cast<double>(core::at(colour, c))));
        }
        const double mean_error = mean_of(errors);
        const double error99 = percentile(errors, 0.99);
        tally->check(mean_error <= kMostTableError && error99 <= kMostTableError99 && count_not_finite(table) == 0,
                     text::format("%4.0f degrees: the table's radiance within %.2f%% on average, %.2f%% at the 99th "
                                  "percentile; no NaN",
                                  degrees, mean_error * 100.0, error99 * 100.0));
        tally->check(colour_error <= kMostColourError,
                     text::format("%4.0f degrees: the sun's transmittance %.3f %.3f %.3f and colour %.3f %.3f %.3f "
                                  "within %.4f",
                                  degrees, core::at(transmittance, 0), core::at(transmittance, 1),
                                  core::at(transmittance, 2), static_cast<double>(core::at(colour, 0)),
                                  static_cast<double>(core::at(colour, 1)), static_cast<double>(core::at(colour, 2)),
                                  colour_error));
        // the zenith bluer than the horizon (away from the sun)
        const Rgb zenith = render::rt::sky_radiance({0.0, 1.0, 0.0}, sun, render::rt::kReferenceSteps);
        const Rgb horizon = render::rt::sky_radiance(normalised({-1.0, 0.03, 0.0}), sun, render::rt::kReferenceSteps);
        const double zenith_blue = core::at(zenith, 2) / std::max(core::at(zenith, 0), 1e-12);
        const double horizon_blue = core::at(horizon, 2) / std::max(core::at(horizon, 0), 1e-12);
        tally->check(zenith_blue > horizon_blue,
                     text::format("%4.0f degrees: the zenith bluer than the horizon (blue / red %.2f > %.2f)", degrees,
                                  zenith_blue, horizon_blue));
        const double warmth = static_cast<double>(core::at(colour, 2)); // (the blue of a colour whose red is 1)
        warmer_lower = warmer_lower && warmth > last_warmth;
        last_warmth = warmth;
    }
    tally->check(warmer_lower, "the sun's colour warmer the lower it is (its blue rising with the elevation)");
}

// --- 2. the disc and the glare ---------------------------------------------------------------------------------------

constexpr double kMostScreenError = 0.05;  // pixels
constexpr double kMostAngleError = 1e-5;   // radians: a pixel's angle
constexpr double kMostLightError = 0.02;   // relative (16-bit floats), + kLightSlack absolutely
constexpr double kLightSlack = 2e-3;
constexpr double kMostBadShare = 0.002;    // of the clear-cut pixels
constexpr int kLeastDiscPixels = 20;

// The light the source pass adds at a sky pixel (the formula, vulkan_rt_sun.h step 2).
Rgb disc_light(const vk::RtSunFrame& frame, double theta)
{
    const double radius = frame.disc_radius;
    const double edge = std::clamp(((radius - theta) / std::max(static_cast<double>(frame.pixel_angle), 1e-6)) + 0.5, 0.0, 1.0);
    const double t = std::min(theta / radius, 1.0);
    const double limb = 1.0 - (0.6 * (1.0 - std::sqrt(std::max(1.0 - (t * t), 0.0))));
    const double glow = (vk::kSunAureoleNear * std::exp(-theta / vk::kSunAureoleNearWidth)) +
                        (vk::kSunAureoleWide * std::exp(-theta / vk::kSunAureoleWideWidth));
    const double amount = (vk::kSunDiscIntensity * limb * edge) + glow;
    return {core::at(frame.colour, 0) * amount, core::at(frame.colour, 1) * amount, core::at(frame.colour, 2) * amount};
}

void run_disc(render::Gpu& gpu, bool floats, Tally* tally)
{
    text::print("the sun's disc and glare (rt_sun=1, %s):\n", floats ? "a 16-bit float target, rt_hdr=1" : "an 8-bit target, rt_hdr=0");
    vk::set_rt_sun_settings(sun_settings(true, false, false, floats));
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, floats, &targets);
    tally->check(made, "the targets made");
    if (!made) {
        release_targets(gpu, targets);
        return;
    }
    const Pose pose = sun_pose();
    const View view = view_from(pose, kTargetWidth, kTargetHeight);
    tally->check(draw_frame(gpu, targets, pose) == 1, "the pass recorded");
    tally->check(render::rt::sun_glare_replaced(), "the original's sun and lens flare reported replaced");
    const vk::RtSunFrame frame = vk::rt_sun_frame();
    double x = 0.0;
    double y = 0.0;
    const bool in_front = screen_of(view, to_sun(), &x, &y);
    tally->check(in_front && frame.in_front && std::abs(static_cast<double>(core::at(frame.screen, 0)) - x) < kMostScreenError &&
                     std::abs(static_cast<double>(core::at(frame.screen, 1)) - y) < kMostScreenError,
                 text::format("the sun's place on the screen (%.3f, %.3f) against the lens's (%.3f, %.3f)",
                              static_cast<double>(core::at(frame.screen, 0)),
                              static_cast<double>(core::at(frame.screen, 1)), x, y));
    const double centre_angle =
        angle_between(direction_at(view, kTargetWidth / 2.0, kTargetHeight / 2.0),
                      direction_at(view, (kTargetWidth / 2.0) + 1.0, kTargetHeight / 2.0));
    tally->check(std::abs(static_cast<double>(frame.pixel_angle) - centre_angle) < kMostAngleError,
                 text::format("a pixel's angle %.6f against %.6f", static_cast<double>(frame.pixel_angle), centre_angle));
    vk::RtSunTexels light;
    const bool read = read_sun(vk::RtSunImage::kLight, kTargetWidth, kTargetHeight, &light) &&
                      light.width == kTargetWidth && light.height == kTargetHeight;
    tally->check(read && count_not_finite(light.texels) == 0, "the light read back, no NaN");
    if (read) {
        const std::vector<uint8_t> mask = sky_mask(view);
        int compared = 0;
        int bad = 0;
        int disc = 0;
        int lit_surface = 0;
        double worst = 0.0;
        for (uint32_t py = 0; py < kTargetHeight; ++py) {
            for (uint32_t px = 0; px < kTargetWidth; ++px) {
                if (!clear_cut(view, mask, static_cast<int>(px), static_cast<int>(py))) {
                    continue;
                }
                const std::size_t i = (static_cast<std::size_t>(py) * kTargetWidth) + px;
                const std::array<float, 4>& got = core::at(light.texels, i);
                Rgb want{};
                if (core::at(mask, i) != 0) {
                    const double theta = angle_between(direction_at(view, px, py), to_sun());
                    want = disc_light(frame, theta);
                    disc += theta < frame.disc_radius ? 1 : 0;
                } else {
                    lit_surface += got.front() > 0.0F ? 1 : 0;
                }
                ++compared;
                bool ok = true;
                for (std::size_t c = 0; c < kChannels; ++c) {
                    const double difference = std::abs(static_cast<double>(core::at(got, c)) - core::at(want, c));
                    worst = std::max(worst, difference / (core::at(want, c) + kLightSlack));
                    ok = ok && difference <= (kMostLightError * core::at(want, c)) + kLightSlack;
                }
                bad += ok ? 0 : 1;
            }
        }
        tally->check(compared > 0 && bad <= static_cast<int>(kMostBadShare * compared) && lit_surface == 0 &&
                         disc >= kLeastDiscPixels,
                     text::format("the light added: %d clear-cut pixels against the formula, %d off (the worst %.4f "
                                  "relative), the disc %d pixels, none added on surfaces (%d)",
                                  compared, bad, worst, disc, lit_surface));
    }
    // the composite: at the disc's centre the target holds the brightest value; with 8 bits, white
    std::vector<Rgb> colour;
    const bool read_target = read_colour(targets, &colour);
    tally->check(read_target, "the target read back");
    if (read_target && in_front) {
        const auto cx = static_cast<std::size_t>(std::lround(x));
        const auto cy = static_cast<std::size_t>(std::lround(y));
        const Rgb& centre = core::at(colour, (cy * kTargetWidth) + cx);
        const double brightest = std::min({core::at(centre, 0), core::at(centre, 1), core::at(centre, 2)});
        tally->check(floats ? brightest > 4.0 : brightest >= 1.0 - kPerByte,
                     text::format("the disc's centre %.3f %.3f %.3f (%s)", core::at(centre, 0), core::at(centre, 1),
                                  core::at(centre, 2), floats ? "above 1 kept" : "white"));
    }
    // the sun behind the camera: nothing to draw (no bloom of the frame) - no pass, still replaced
    tally->check(draw_frame(gpu, targets, away_pose()) == 0 && render::rt::sun_glare_replaced(),
                 "the sun behind the camera: no pass, the original's glare still left out");
    // no sun: the original's kept
    tally->check(draw_frame(gpu, targets, pose, false) == 0 && !render::rt::sun_glare_replaced(),
                 "no sun (none baked, no override): no pass, the original's sun kept");
    release_targets(gpu, targets);
}

// --- 3. the rays -----------------------------------------------------------------------------------------------------

constexpr double kMostShaftError = 0.01; // absolute, 97% of the texels
constexpr double kMostShaftMean = 2e-3;  // absolute, the mean
constexpr double kShaftShare = 0.97;
constexpr double kLeastContrast = 2.0;   // through the window / behind the wall

float jitter_of(int x, int y)
{
    constexpr float kNoiseScale = 52.9829189F;
    constexpr float kNoiseX = 0.06711056F;
    constexpr float kNoiseY = 0.00583715F;
    const float inner = (kNoiseX * static_cast<float>(x)) + (kNoiseY * static_cast<float>(y));
    const float scaled = kNoiseScale * (inner - std::floor(inner));
    return scaled - std::floor(scaled);
}

// The rays' value at a half-resolution texel (the pass's march; vulkan_rt_sun.h step 1), before the colour.
double shaft_at(const View& view, const std::vector<uint8_t>& mask, const vk::RtSunFrame& frame, int qx, int qy)
{
    const double at_x = (qx * 2.0) + 0.5;
    const double at_y = (qy * 2.0) + 0.5;
    const int n = static_cast<int>(vk::kSunShaftSamples);
    const double delta_x = (core::at(frame.screen, 0) - at_x) * (vk::kSunShaftReach / n);
    const double delta_y = (core::at(frame.screen, 1) - at_y) * (vk::kSunShaftReach / n);
    const double u = jitter_of(qx, qy);
    double sum = 0.0;
    double weights = 0.0;
    double w = 1.0;
    for (int i = 0; i < n; ++i) {
        const double x = at_x + (delta_x * (i + u));
        const double y = at_y + (delta_y * (i + u));
        const int px = std::clamp(static_cast<int>(std::floor(x + 0.5)), 0, static_cast<int>(view.width) - 1);
        const int py = std::clamp(static_cast<int>(std::floor(y + 0.5)), 0, static_cast<int>(view.height) - 1);
        double source = 0.0;
        if (core::at(mask, (static_cast<std::size_t>(py) * view.width) + px) != 0) {
            source = std::exp(-angle_between(direction_at(view, x, y), to_sun()) / vk::kSunShaftWidth);
        }
        sum += w * source;
        weights += w;
        w *= vk::kSunShaftDecay;
    }
    return (sum / weights) * vk::kRtShaftStrength * frame.fade;
}

// Whether the straight way from a texel's point toward the sun's place crosses the window (its pixels see through).
bool through_window(const View& view, int qx, int qy, const vk::RtSunFrame& frame)
{
    // the texel's ray, then toward the sun from the wall's plane
    const Vec3 direction = direction_at(view, (qx * 2.0) + 0.5, (qy * 2.0) + 0.5);
    (void)frame;
    const double t = -z_of(view.eye) / z_of(direction);
    const Vec3 at = step(view.eye, direction, t);
    return t > 0.0 && x_of(at) > -20.0 && x_of(at) < 20.0 && y_of(at) > 60.0 && y_of(at) < 100.0;
}

void run_shafts(render::Gpu& gpu, Tally* tally)
{
    text::print("the rays (rt_shafts=1):\n");
    vk::set_rt_sun_settings(sun_settings(false, true, false, false));
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, false, &targets);
    tally->check(made, "the targets made");
    if (!made) {
        release_targets(gpu, targets);
        return;
    }
    const Pose pose = sun_pose();
    const View view = view_from(pose, kTargetWidth, kTargetHeight);
    tally->check(draw_frame(gpu, targets, pose) == 1, "the pass recorded");
    tally->check(!render::rt::sun_glare_replaced(), "rt_sun=0: the original's sun kept");
    const vk::RtSunFrame frame = vk::rt_sun_frame();
    vk::RtSunTexels shafts;
    const uint32_t half_width = (kTargetWidth + 1) / 2;
    const uint32_t half_height = (kTargetHeight + 1) / 2;
    const bool read = read_sun(vk::RtSunImage::kShafts, kTargetWidth, kTargetHeight, &shafts) &&
                      shafts.width == half_width && shafts.height == half_height;
    tally->check(read && count_not_finite(shafts.texels) == 0, "the rays read back, no NaN");
    if (read) {
        const std::vector<uint8_t> mask = sky_mask(view);
        std::vector<double> errors;
        std::vector<double> through;
        std::vector<double> blocked;
        for (uint32_t qy = 0; qy < half_height; ++qy) {
            for (uint32_t qx = 0; qx < half_width; ++qx) {
                const double want = shaft_at(view, mask, frame, static_cast<int>(qx), static_cast<int>(qy));
                const std::array<float, 4>& got = core::at(shafts.texels, (static_cast<std::size_t>(qy) * half_width) + qx);
                const double got_luma = luma({got[0], got[1], got[2]});
                const double want_luma = want * luma({core::at(frame.colour, 0), core::at(frame.colour, 1), core::at(frame.colour, 2)});
                errors.push_back(std::abs(got_luma - want_luma));
                // the wall's pixels: their way to the sun crosses the window, or the wall
                const std::size_t pixel = (static_cast<std::size_t>(qy) * 2 * kTargetWidth) + (qx * 2);
                if (core::at(mask, pixel) == 0) {
                    (through_window(view, static_cast<int>(qx), static_cast<int>(qy), frame) ? through : blocked)
                        .push_back(got_luma);
                }
            }
        }
        const double error = percentile(errors, kShaftShare);
        const double mean_error = mean_of(errors);
        tally->check(error <= kMostShaftError && mean_error <= kMostShaftMean,
                     text::format("every texel against the processor's march: %.4f at the 97th percentile, %.5f mean",
                                  error, mean_error));
        (void)blocked;
        // (a wall pixel isn't sky: its rays start on the wall; the ones near the window see it)
        double lit_window = 0.0;
        double lit_far = 0.0;
        int near_count = 0;
        int far_count = 0;
        for (uint32_t qy = 0; qy < half_height; ++qy) {
            for (uint32_t qx = 0; qx < half_width; ++qx) {
                const double distance = std::hypot((qx * 2.0) - core::at(frame.screen, 0), (qy * 2.0) - core::at(frame.screen, 1));
                const std::array<float, 4>& got = core::at(shafts.texels, (static_cast<std::size_t>(qy) * half_width) + qx);
                const double value = luma({got[0], got[1], got[2]});
                const std::size_t pixel = (static_cast<std::size_t>(qy) * 2 * kTargetWidth) + (qx * 2);
                if (core::at(mask, pixel) != 0) {
                    continue;
                }
                if (distance < 40.0) {
                    lit_window += value;
                    ++near_count;
                } else if (distance > 120.0) {
                    lit_far += value;
                    ++far_count;
                }
            }
        }
        const double near_mean = near_count > 0 ? lit_window / near_count : 0.0;
        const double far_mean = far_count > 0 ? lit_far / far_count : 0.0;
        tally->check(near_mean > kLeastContrast * far_mean && near_mean > 0.01,
                     text::format("rays from the window: the wall near it %.4f, far from it %.4f", near_mean, far_mean));
        (void)through;
    }
    release_targets(gpu, targets);
}

// --- 4. the bloom ----------------------------------------------------------------------------------------------------

constexpr double kMostEnergyError = 0.015; // relative
constexpr double kMostLevels = 0.6;        // 8-bit levels: the frame's mean through the bloom

void run_bloom(render::Gpu& gpu, bool floats, Tally* tally)
{
    text::print("the bloom (rt_bloom=1, rt_sun=1, %s):\n", floats ? "a 16-bit float target, rt_hdr=1" : "an 8-bit target, rt_hdr=0");
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, floats, &targets);
    tally->check(made, "the targets made");
    if (!made) {
        release_targets(gpu, targets);
        return;
    }
    const Pose pose = sun_pose();
    // the frame without the passes
    vk::set_rt_sun_settings({});
    draw_frame(gpu, targets, pose);
    std::vector<Rgb> plain;
    const bool read_plain = read_colour(targets, &plain);
    vk::set_rt_sun_settings(sun_settings(true, true, true, floats));
    tally->check(draw_frame(gpu, targets, pose) == 1, "the pass recorded");
    vk::RtSunTexels base;
    vk::RtSunTexels light;
    vk::RtSunTexels top;
    vk::RtSunTexels first;
    const bool read = read_sun(vk::RtSunImage::kBase, kTargetWidth, kTargetHeight, &base) &&
                      read_sun(vk::RtSunImage::kLight, kTargetWidth, kTargetHeight, &light) &&
                      read_sun(vk::RtSunImage::kBloom, kTargetWidth, kTargetHeight, &top) &&
                      read_sun(vk::RtSunImage::kBloomInput, kTargetWidth, kTargetHeight, &first);
    tally->check(read && count_not_finite(base.texels) == 0 && count_not_finite(light.texels) == 0 &&
                     count_not_finite(top.texels) == 0 && count_not_finite(first.texels) == 0,
                 "the images read back, no NaN");
    if (!read || !read_plain) {
        release_targets(gpu, targets);
        return;
    }
    // what is spread, X (rt_bloom_strength x the frame + kSunGlareBloom x the light), and the chain's top: the same light
    double spread = 0.0;
    double brightest = 0.0;
    const double frame_share = vk::kRtBloomStrength;
    const double light_share = vk::kSunGlareBloom;
    for (std::size_t i = 0; i < base.texels.size(); ++i) {
        const std::array<float, 4>& b = core::at(base.texels, i);
        const std::array<float, 4>& l = core::at(light.texels, i);
        spread += luma({(frame_share * b[0]) + (light_share * l[0]), (frame_share * b[1]) + (light_share * l[1]),
                        (frame_share * b[2]) + (light_share * l[2])});
        brightest = std::max(brightest, luma({static_cast<double>(b[0]) + l[0], static_cast<double>(b[1]) + l[1],
                                              static_cast<double>(b[2]) + l[2]}));
    }
    spread /= static_cast<double>(base.texels.size());
    const auto mean_luma = [](const vk::RtSunTexels& image) {
        double sum = 0.0;
        for (const std::array<float, 4>& t : image.texels) {
            sum += luma({t[0], t[1], t[2]});
        }
        return sum / static_cast<double>(image.texels.size());
    };
    const double down = mean_luma(first);
    const double bloomed = mean_luma(top);
    tally->check(brightest > 10.0, text::format("the light spread goes above 1 (the disc: %.1f)", brightest));
    tally->check(std::abs(down - spread) <= kMostEnergyError * spread && std::abs(bloomed - spread) <= kMostEnergyError * spread,
                 text::format("the light kept: X's mean %.5f, the first level down %.5f, the chain's top %.5f", spread,
                              down, bloomed));
    // the frame's own colour copied out unchanged (8-bit: exactly)
    double base_error = 0.0;
    for (std::size_t i = 0; i < plain.size(); ++i) {
        for (std::size_t c = 0; c < kChannels; ++c) {
            base_error = std::max(base_error, std::abs(static_cast<double>(core::at(core::at(base.texels, i), c)) -
                                                       core::at(core::at(plain, i), c)));
        }
    }
    tally->check(base_error <= (floats ? 1e-3 : 0.5 * kPerByte),
                 text::format("the frame copied out as drawn (the largest difference %.5f)", base_error));
    std::vector<Rgb> after;
    const bool read_after = read_colour(targets, &after);
    tally->check(read_after, "the target read back");
    if (read_after) {
        double before_sum = 0.0;
        double after_sum = 0.0;
        double most = 0.0;
        int not_finite = 0;
        for (std::size_t i = 0; i < after.size(); ++i) {
            const Rgb& got = core::at(after, i);
            not_finite += std::ranges::all_of(got, [](double v) { return std::isfinite(v); }) ? 0 : 1;
            after_sum += luma(got);
            const std::array<float, 4>& b = core::at(base.texels, i);
            const std::array<float, 4>& l = core::at(light.texels, i);
            Rgb sum{static_cast<double>(b[0]) + l[0], static_cast<double>(b[1]) + l[1], static_cast<double>(b[2]) + l[2]};
            if (!floats) {
                for (double& v : sum) {
                    v = std::min(v, 1.0);
                }
            }
            before_sum += luma(sum);
            most = std::max(most, std::max({got[0], got[1], got[2]}));
        }
        const double n = static_cast<double>(after.size());
        if (floats) {
            tally->check(not_finite == 0 && std::abs(after_sum - before_sum) <= kMostEnergyError * before_sum && most > 1.0,
                         text::format("the picture's sum kept through the composite: %.5f before, %.5f after (mean); the "
                                      "brightest %.2f kept above 1; no NaN",
                                      before_sum / n, after_sum / n, most));
        } else {
            // (the 8-bit target clips the disc and the glare round it: the sum after is at most the clipped sum before
            // plus the spread light; at least the frame's own)
            double plain_sum = 0.0;
            for (const Rgb& p : plain) {
                plain_sum += luma(p);
            }
            tally->check(not_finite == 0 && most <= 1.0 && after_sum >= plain_sum && after_sum <= before_sum + (spread * n),
                         text::format("8 bits: no NaN, at most 1, brighter than the frame (%.4f -> %.4f mean), within "
                                      "what was added (%.4f)",
                                      plain_sum / n, after_sum / n, before_sum / n));
        }
    }
    // the frame's own bloom alone (no sun): the 8-bit frame's sum kept
    if (!floats) {
        vk::set_rt_sun_settings(sun_settings(false, false, true, false));
        tally->check(draw_frame(gpu, targets, pose) == 1, "the bloom alone: the pass recorded");
        std::vector<Rgb> bloomed_frame;
        if (read_colour(targets, &bloomed_frame)) {
            double plain_sum = 0.0;
            double after_sum = 0.0;
            for (std::size_t i = 0; i < plain.size(); ++i) {
                plain_sum += luma(core::at(plain, i));
                after_sum += luma(core::at(bloomed_frame, i));
            }
            const double n = static_cast<double>(plain.size());
            tally->check(std::abs(after_sum - plain_sum) / n <= kMostLevels * kPerByte,
                         text::format("the bloom alone keeps the frame's mean: %.5f -> %.5f (%.2f levels)",
                                      plain_sum / n, after_sum / n, std::abs(after_sum - plain_sum) / n / kPerByte));
        }
    }
    release_targets(gpu, targets);
}

// --- 5. the aerial haze ----------------------------------------------------------------------------------------------

constexpr float kAerialHazeDensity = 12.0F; // (the check's scene is small: a thick haze)
constexpr float kAerialHeight = 60.0F;
constexpr double kMostAerialError = 4e-3;

// The haze's optical depth along a ray from the eye to `distance` (the closed form of its height profile).
double aerial_depth(const Vec3& eye, const Vec3& direction, double distance)
{
    const double sigma = static_cast<double>(vk::kFogAerialExtinction) * kAerialHazeDensity;
    const double inv_h = 1.0 / kAerialHeight;
    const double base = sigma; // (no baked ground with the override's sun: the haze's ground is the camera's height)
    (void)eye;
    const double k = y_of(direction) * inv_h;
    if (std::abs(k) * distance < 1e-9) {
        return base * distance;
    }
    return base * (1.0 - std::exp(-k * distance)) / k;
}

void run_aerial(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    text::print("the aerial haze (rt_fog=1, rt_fog_haze=1, rt_sky_model=1, no level fog):\n");
    vk::RtSunSettings sky = sun_settings(false, false, false, false);
    sky.sky_model = true;
    vk::set_rt_sun_settings(sky);
    vk::RtFogSettings fog;
    fog.on = true;
    fog.steps = 32;
    fog.haze = true;
    fog.haze_density = kAerialHazeDensity;
    fog.haze_height = kAerialHeight;
    vk::set_rt_fog_settings(fog);
    vk::rt_fog_set_scene(scene.top.handle);
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, false, &targets);
    tally->check(made, "the targets made");
    if (made) {
        const Pose pose = ground_pose();
        // (the check's sun has no baked ground: the haze's ground is the camera's height - its base density sigma)
        const uint64_t before = vk::rt_fog_passes();
        draw_frame(gpu, targets, pose);
        tally->check(vk::rt_fog_passes() == before + 1, "the fog's pass recorded");
        vk::RtFogTexels raw;
        vk::RtFogTexels info;
        const bool read = read_fog(vk::RtFogImage::kRaw, &raw) && read_fog(vk::RtFogImage::kRawInfo, &info) &&
                          raw.texels.size() == info.texels.size();
        tally->check(read && count_not_finite(raw.texels) == 0, "the march read back, no NaN");
        if (read) {
            const View view = view_from(pose, kTargetWidth, kTargetHeight);
            const render::rt::Dir model_sun = render::rt::sun_direction_at(std::asin(y_of(to_sun())));
            double worst = 0.0;
            int sky_dimmed = 0;
            int surfaces = 0;
            int out_of_bounds = 0;
            double least_transmittance = 1.0;
            for (std::size_t i = 0; i < raw.texels.size(); ++i) {
                const std::array<float, 4>& r = core::at(raw.texels, i);
                const std::array<float, 4>& n = core::at(info.texels, i);
                const double distance = n[0];
                const Vec3 direction = direction_at(view, n[1], n[2]);
                if (distance < 0.0) {
                    sky_dimmed += r[3] != 1.0F ? 1 : 0;
                    continue;
                }
                ++surfaces;
                const double want = std::exp(-std::min(aerial_depth(view.eye, direction, distance), 40.0));
                worst = std::max(worst, std::abs(static_cast<double>(r[3]) - want));
                least_transmittance = std::min(least_transmittance, want);
                // the light between the sky's share and that + the sun's (the sky toward the ray's level direction, the
                // model's own integral; 16-bit floats and the table's few steps: 8% slack)
                const Vec3 level = normalised({x_of(direction), std::max(y_of(direction), static_cast<double>(vk::kFogAerialLowest)),
                                               z_of(direction)});
                // the model's frame: the sun along +x - the level direction's azimuth from the sun's and its elevation
                const double azimuth = std::acos(std::clamp(
                    ((x_of(level) * x_of(to_sun())) + (z_of(level) * z_of(to_sun()))) /
                        std::max(std::hypot(x_of(level), z_of(level)) * std::hypot(x_of(to_sun()), z_of(to_sun())), 1e-12),
                    -1.0, 1.0));
                const double elevation = std::asin(std::clamp(y_of(level), -1.0, 1.0));
                const render::rt::Dir model_dir{std::cos(elevation) * std::cos(azimuth), std::sin(elevation),
                                                std::cos(elevation) * std::sin(azimuth)};
                const Rgb radiance = render::rt::sky_radiance(model_dir, model_sun, {.view = 128, .light = 64});
                const double ambient_luma = std::pow(luma(radiance) * render::rt::sky_exposure(), 1.0 / render::rt::kDisplayGamma);
                const double got = luma({r[0], r[1], r[2]});
                const double low = (1.0 - want) * vk::kFogAerialSkyShare * ambient_luma;
                if (got < (low * 0.92) - 1e-3) {
                    if (out_of_bounds == 0) {
                        text::print("    (the first below: light %.4f %.4f %.4f, T %.3f, the sky's luma %.4f, at least %.4f)\n",
                                    static_cast<double>(r[0]), static_cast<double>(r[1]), static_cast<double>(r[2]),
                                    want, ambient_luma, low);
                    }
                    ++out_of_bounds;
                }
            }
            tally->check(surfaces > 1000 && worst <= kMostAerialError && least_transmittance < 0.5,
                         text::format("%d surface texels: the transmittance against the closed form within %.5f (the "
                                      "thinnest %.3f)",
                                      surfaces, worst, least_transmittance));
            tally->check(sky_dimmed == 0, text::format("the sky never dimmed (%d texels)", sky_dimmed));
            tally->check(out_of_bounds == 0,
                         text::format("the light at least the sky's share of the sky's colour (%d below)", out_of_bounds));
        }
    }
    release_targets(gpu, targets);
    vk::rt_fog_set_scene(VK_NULL_HANDLE);
    vk::set_rt_fog_settings({});
    vk::set_rt_sun_settings({});
}

// --- 6. nothing on ---------------------------------------------------------------------------------------------------

void run_untouched(render::Gpu& gpu, Tally* tally)
{
    text::print("everything off: no pass, nothing changed:\n");
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, false, &targets);
    tally->check(made, "the targets made");
    if (made) {
        vk::set_rt_sun_settings({});
        const Pose pose = sun_pose();
        tally->check(draw_frame(gpu, targets, pose) == 0 && !render::rt::sun_glare_replaced(),
                     "no pass, the original's sun kept");
        std::vector<Rgb> first;
        std::vector<Rgb> second;
        draw_frame(gpu, targets, pose);
        const bool read = read_colour(targets, &first);
        draw_frame(gpu, targets, pose);
        tally->check(read && read_colour(targets, &second) && first == second, "the picture as drawn");
    }
    release_targets(gpu, targets);
}

// --- --perf ----------------------------------------------------------------------------------------------------------

void run_perf(render::Gpu& gpu)
{
    text::print("cost at %ux%u (the passes' timestamps on the device, 60 frames, everything on):\n", kPerfWidth, kPerfHeight);
    vk::set_rt_sun_settings(sun_settings(true, true, true, false));
    Targets targets;
    if (!make_targets(gpu, kPerfWidth, kPerfHeight, false, &targets)) {
        text::print("  the targets couldn't be made\n");
        return;
    }
    vk::Backend& state = vk::backend();
    const auto timing = [&state] {
        const std::scoped_lock lock(state.mutex);
        vk::finish_work(state);
        return vk::rt_sun_timing();
    };
    constexpr int kFrames = 60;
    const vk::RtSunTiming before = timing();
    for (int frame = 0; frame < kFrames; ++frame) {
        draw_frame(gpu, targets, sun_pose());
        const std::scoped_lock lock(state.mutex);
        vk::finish_work(state);
    }
    const vk::RtSunTiming after = timing();
    const double samples = static_cast<double>(after.samples - before.samples);
    if (samples > 0.0) {
        std::array<double, 4> parts{};
        for (std::size_t i = 0; i < parts.size(); ++i) {
            core::at(parts, i) = (core::at(after.milliseconds, i) - core::at(before.milliseconds, i)) / samples;
        }
        text::print("  %.3f ms: rays %.3f, source %.3f, bloom %.3f, composite %.3f (%.0f passes timed)\n",
                    core::at(parts, 0) + core::at(parts, 1) + core::at(parts, 2) + core::at(parts, 3), core::at(parts, 0),
                    core::at(parts, 1), core::at(parts, 2), core::at(parts, 3), samples);
    }
    release_targets(gpu, targets);
    vk::set_rt_sun_settings({});
}

// --- the device ----------------------------------------------------------------------------------------------------

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
    // (on before the device: its depth surfaces are made samplable)
    vk::set_rt_sun_settings(sun_settings(true, true, true, false));
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
        return vk::loader_version() == 0 ? 2 : 1;
    }
    vk::Backend& state = vk::backend();
    Tally tally;
    Scene scene;
    bool built = false;
    {
        const std::scoped_lock lock(state.mutex);
        built = make_scene(state, &scene);
    }
    tally.check(built, "the check's acceleration structures built (a BLAS of the wall and the ground, a TLAS)");
    if (built) {
        render::Gpu& gpu = vk::gpu();
        run_sky(&tally);
        run_disc(gpu, false, &tally);
        run_disc(gpu, true, &tally);
        run_shafts(gpu, &tally);
        run_bloom(gpu, false, &tally);
        run_bloom(gpu, true, &tally);
        run_aerial(gpu, scene, &tally);
        run_untouched(gpu, &tally);
        if (options.perf) {
            run_perf(gpu);
        }
    }
    vk::set_rt_sun_settings({});
    render::rt::set_sun_override(nullptr);
    {
        const std::scoped_lock lock(state.mutex);
        vk::finish_work(state);
        release_scene(state, &scene);
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
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rt_sun_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan sun check", kWindowWidth, kWindowHeight,
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
