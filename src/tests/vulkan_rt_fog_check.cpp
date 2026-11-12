// NOT ORIGINAL - thug2_vulkan_rt_fog_check.exe: the Vulkan Gpu's volumetric fog and light shafts
// (gpu_vulkan/vulkan_rt_fog.h) tried on this machine against references worked out on the processor.
//
// A wall with a window in it, standing on a ground square, the sun low behind it (world, instance mask 0x01): the light
// comes through the window as a shaft into an even fog (the level's linear fog from kFogStart to kFogEnd, no height
// falloff). The camera looks along the wall's shadowed side, across the shaft. The scene is drawn through render::Gpu as
// render_pc draws a level; its acceleration structures are built here and handed over with rt_fog_set_scene;
// rt::world_view_drawn runs the passes through the listener, as draw_viewport does. The processor answers each pixel
// from the camera's own position and lens (not the matrices): the surface its ray hits, and the fog along it.
//   - One frame (the march as it is): every low-resolution texel's distance against the processor's; its transmittance
//     against exp(-integral) of the extinction - the game's own (end - d) / (end - start); its light against the
//     processor's march of the same pixel with the same jitter and steps (the shaft's edges may land on either side of a
//     sample: a share of texels may differ by up to one step's light); no NaN.
//   - The shaft: texels whose rays cross it are lighter than those that don't, by about what the processor's exact
//     integral says.
//   - A still camera: after kStillFrames frames the history against the exact integral (2048 steps): nearer than one
//     frame's march (the temporal accumulation converges), mean and 95th-percentile error bounded; the colour target
//     the composite: surface x T + light within a few levels where the pixel's neighbours are on its surface.
//   - A moved camera: no NaN, the error after it bounded (no ghost).
//   - Shafts mode (rt_fog_mode=1): T = 1 everywhere, pixels whose rays never cross sunlit fog unchanged.
//   - The sky: never dimmed; lighter only by the shafts' share.
//   - No fog in the level: with rt_fog_haze=0 no pass, nothing changed; with the haze nothing darker, the shaft
//     lighter. No scene, rt_fog off: no pass, nothing changed.
//   - --perf: the pass's cost at 1920 x 1080 with 16, 32 and 64 steps (frames timed with and without it).
//   thug2_vulkan_rt_fog_check [--device N] [--display N] [--no-validation] [--perf]
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
#include "gpu_vulkan/vulkan_rt_fog.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
using Vec3 = std::array<double, 3>;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr uint32_t kTargetWidth = 320;
constexpr uint32_t kTargetHeight = 240;
constexpr uint32_t kPerfWidth = 1920;
constexpr uint32_t kPerfHeight = 1080;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x042; // XYZ | DIFFUSE
constexpr uint32_t kClearColour = 0xFF405878;
constexpr float kNearZ = 2.0F;
constexpr float kFarZ = 2000.0F;
constexpr double kHalfFovTan = 0.57735026918962576; // tan 30 degrees: a 60-degree horizontal field of view
constexpr double kNoHit = std::numeric_limits<double>::infinity();
constexpr std::size_t kNoSurface = 0;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint32_t kRedShift = 16;
constexpr uint32_t kGreenShift = 8;
constexpr std::size_t kPixelBytes = 4;
constexpr float kFogStart = 10.0F;
constexpr float kFogEnd = 500.0F;
constexpr std::array<float, 3> kFogColour{0.6F, 0.65F, 0.7F};
constexpr float kAnisotropy = 0.3F;
constexpr unsigned kSteps = 32;
constexpr int kStillFrames = 40;
constexpr double kPerByte = 1.0 / 255.0;
constexpr uint32_t kScale = render::vulkan::kFogScale;

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

constexpr Vec3 kLightTravels{0.15, -0.45, 1.0}; // (the sun behind the wall, low: the shaft goes down into +z)
constexpr Vec3 kEye{160.0, 70.0, 150.0};
constexpr Vec3 kMovedEye{140.0, 80.0, 175.0};
constexpr Vec3 kAt{0.0, 40.0, 60.0};
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
// A ray against a box (slabs): the distance to its first hit (infinity: none, or the origin inside it).
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
View view_from(const Vec3& eye, uint32_t width, uint32_t height)
{
    View view;
    view.eye = eye;
    view.back = normalised(minus(eye, kAt));
    view.right = normalised(cross(kUp, view.back));
    view.up = cross(view.back, view.right);
    view.half_width = kHalfFovTan;
    view.half_height = kHalfFovTan * static_cast<double>(height) / static_cast<double>(width);
    view.width = width;
    view.height = height;
    return view;
}

// A pixel of the target (or the viewport: the whole target here).
struct Pixel {
    int x = 0;
    int y = 0;
};

// The direction through a pixel (Direct3D 9's pixel centres: whole coordinates).
Vec3 direction_at(const View& view, Pixel pixel)
{
    const double ndc_x = (2.0 * pixel.x / view.width) - 1.0;
    const double ndc_y = 1.0 - (2.0 * pixel.y / view.height);
    return normalised(step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height),
                           view.back, -1.0));
}

// The first surface along a ray: its solid (index + 1; 0: the sky) and distance.
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

// --- the fog, as the pass marches it -----------------------------------------------------------------------------

const Vec3& to_sun()
{
    static const Vec3 kToward = normalised(step(Vec3{}, kLightTravels, -1.0));
    return kToward;
}

bool sunlit(const Vec3& at)
{
    return std::ranges::none_of(kSolids, [&](const Solid& solid) {
        return intersect(solid, {.origin = at, .direction = to_sun()}) <=
               static_cast<double>(render::vulkan::kFogSunDistance);
    });
}

constexpr double kLeastPhaseDenominator = 1e-4; // (the shader's)
constexpr double kLeastLeft = 1e-4;             // the shortest way to the fog's end a segment counts (x its length)
constexpr double kMostDepth = 40.0;             // a segment's optical depth at most

double phase(double cosine)
{
    const double anisotropy = kAnisotropy;
    const double denominator =
        std::max(1.0 + (anisotropy * anisotropy) - (2.0 * anisotropy * cosine), kLeastPhaseDenominator);
    return (1.0 - (anisotropy * anisotropy)) / (denominator * std::sqrt(denominator));
}

// (the pass's tint: the baked sun has no colour)
std::array<double, 3> tint() { return {1.0, 1.0, 1.0}; }

struct Fogged {
    std::array<double, 3> light{};
    double transmittance = 1.0;
};

// A view ray: from the eye along the direction to the surface `distance` away (-1: the sky).
struct ViewRay {
    Vec3 eye{};
    Vec3 direction{};
    double distance = -1.0;
};
// How it is marched: `steps` segments, the light sampled at `jitter` in each (0 - 1); shafts mode or not.
struct Marching {
    uint32_t steps = 0;
    double jitter = 0.0;
    bool shafts = false;
};

// The share of the fog's light the sun gives where the replacing mode isn't drawing (the sky, shafts mode).
double sunlit_share(bool sky, bool replace)
{
    if (sky) {
        return render::vulkan::kFogSkyShare;
    }
    return replace ? 1.0 : render::vulkan::kFogShaftShare;
}

// The fog along a view ray as the pass marches it (or, with many steps and the middle, the exact integral's stand-in).
Fogged march(const ViewRay& ray, const Marching& how)
{
    Fogged out;
    const bool sky = ray.distance < 0.0;
    const bool replace = !how.shafts && !sky;
    const double start = kFogStart;
    const double end = kFogEnd;
    const double first = std::max(start, 0.0);
    const double last = sky ? end : std::min(ray.distance, end);
    if (!(last > first)) {
        return out;
    }
    const double span = last - first;
    const double fade = end - start;
    const double shaped = phase(dot(ray.direction, to_sun()));
    const double share = sunlit_share(sky, replace);
    const double ambient = render::vulkan::kFogAmbientShare;
    const std::array<double, 3> sun_tint = tint();
    for (uint32_t i = 0; i < how.steps; ++i) {
        const double near_end = first + (span * (static_cast<double>(i) / how.steps));
        const double far_end = first + (span * (static_cast<double>(i + 1) / how.steps));
        const double sampled = first + (span * ((static_cast<double>(i) + how.jitter) / how.steps));
        const Vec3 at = step(ray.eye, ray.direction, sampled);
        const double depth =
            std::log(std::max(end - near_end, kLeastLeft * fade) / std::max(end - far_end, kLeastLeft * fade));
        const double kept = std::exp(-std::min(std::max(depth, 0.0), kMostDepth));
        const double lit = sunlit(at) ? shaped : 0.0;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const double fog = core::at(kFogColour, channel);
            const double here = replace ? fog * (ambient + ((1.0 - ambient) * lit * core::at(sun_tint, channel)))
                                        : fog * (1.0 - ambient) * lit * share * core::at(sun_tint, channel);
            core::at(out.light, channel) += out.transmittance * (1.0 - kept) * here;
        }
        out.transmittance *= kept;
    }
    if (!replace) {
        out.transmittance = 1.0;
    }
    return out;
}

// The pass's jitter for a pixel (the viewport's) in frame `frame` (interleaved gradient noise + the golden ratio).
double jitter_of(Pixel pixel, uint64_t frame)
{
    constexpr float kNoiseScale = 52.9829189F;
    constexpr float kNoiseX = 0.06711056F;
    constexpr float kNoiseY = 0.00583715F;
    constexpr double kGoldenRatio = 0.6180339887498949;
    const float noise =
        kNoiseScale *
        std::fmod((kNoiseX * static_cast<float>(pixel.x)) + (kNoiseY * static_cast<float>(pixel.y)), 1.0F);
    const double frame_offset = std::fmod(static_cast<double>(frame) * kGoldenRatio, 1.0);
    const double sum = static_cast<double>(noise - std::floor(noise)) + static_cast<float>(frame_offset);
    return sum - std::floor(sum);
}

// The exact integral (2048 steps, each sampled at its middle) of the fog through a pixel.
constexpr uint32_t kFineSteps = 2048;
constexpr double kMiddle = 0.5;
Fogged exact_at(const View& view, Pixel pixel, bool shafts)
{
    const Vec3 direction = direction_at(view, pixel);
    const Seen seen = seen_along({.origin = view.eye, .direction = direction});
    return march({.eye = view.eye, .direction = direction, .distance = seen.distance},
                 {.steps = kFineSteps, .jitter = kMiddle, .shafts = shafts});
}

// The exact transmittance: the game's linear fog, ((end - distance) / (end - start)) clamped.
double exact_transmittance(double distance)
{
    if (distance < 0.0) {
        return 1.0;
    }
    const double clamped = std::clamp(distance, static_cast<double>(kFogStart), static_cast<double>(kFogEnd));
    return (kFogEnd - clamped) / (kFogEnd - kFogStart);
}

// --- the acceleration structures (the check's own) -----------------------------------------------------------------

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
    if (!state.memory.create_buffer(
            bytes, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
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

// One BLAS of every solid and the TLAS of its one instance (mask 0x01).
bool make_scene(render::vulkan::Backend& state, Scene* scene)
{
    std::vector<std::array<float, 3>> vertices;
    for (const Solid& solid : kSolids) {
        const std::vector<std::array<float, 3>> more = triangles_of(solid);
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
    instance.instanceCustomIndex = 0;
    instance.mask = kMaskWorld;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = bottom.address;
    const std::array<VkAccelerationStructureInstanceKHR, 1> instances{instance};
    render::vulkan::DeviceBuffer& list = scene->inputs.emplace_back();
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
    return build(state, scene,
                 {.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, .geometry = &top, .primitives = 1},
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
Matrices matrices_for(const Vec3& eye, uint32_t width, uint32_t height)
{
    Matrices out;
    maths::matrix_look_at_rh(out.view, floats(eye), floats(kAt), floats(kUp));
    const double lens_width = 2.0 * kNearZ * kHalfFovTan;
    const double lens_height = lens_width * static_cast<double>(height) / static_cast<double>(width);
    maths::matrix_perspective_rh(out.projection, static_cast<float>(lens_width), static_cast<float>(lens_height),
                                 kNearZ, kFarZ);
    return out;
}

void draw_scene(render::Gpu& gpu, const Vec3& eye, uint32_t width, uint32_t height)
{
    using render::GpuRenderState;
    using render::GpuStageState;
    gpu.set_viewport({.x = 0, .y = 0, .width = width, .height = height, .min_z = 0.0F, .max_z = 1.0F});
    const Matrices matrices = matrices_for(eye, width, height);
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

render::rt::ViewDrawn view_drawn(const Vec3& eye, uint32_t width, uint32_t height, bool fog_on)
{
    const Matrices matrices = matrices_for(eye, width, height);
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
    view.rect = {0, 0, width, height};
    view.depth_range = {0.0F, 1.0F};
    view.fog = {.on = fog_on, .start = kFogStart, .end = kFogEnd, .colour = kFogColour};
    return view;
}

// --- the targets and a frame ---------------------------------------------------------------------------------------

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

uint64_t draw_frame(render::Gpu& gpu, const Targets& targets, const Vec3& eye, bool fog_on = true)
{
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = targets.width, .height = targets.height, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kClearColour, 1.0F, 0);
    draw_scene(gpu, eye, targets.width, targets.height);
    // (the sun as the console's `rt sun` sets it: the level's baked one otherwise - none in this scene)
    const std::array<float, 3> toward = floats(to_sun());
    render::rt::set_sun_override(&toward);
    const uint64_t before = render::vulkan::rt_fog_passes();
    render::rt::world_view_drawn(view_drawn(eye, targets.width, targets.height, fog_on));
    const uint64_t passes = render::vulkan::rt_fog_passes() - before;
    gpu.end_scene();
    return passes;
}

bool read_colour(const Targets& targets, std::vector<uint8_t>* texels)
{
    return render::vulkan::read_surface(targets.colour, texels) &&
           texels->size() == static_cast<std::size_t>(targets.width) * targets.height * kPixelBytes;
}

bool read_fog(render::vulkan::RtFogImage which, render::vulkan::RtFogTexels* out)
{
    render::vulkan::Backend& state = render::vulkan::backend();
    const std::scoped_lock lock(state.mutex);
    const Rect whole{0, 0, kTargetWidth, kTargetHeight};
    return render::vulkan::read_rt_fog_image(state, whole, which, out) &&
           out->width == (kTargetWidth + kScale - 1) / kScale && out->height == (kTargetHeight + kScale - 1) / kScale;
}

// --- comparing ---------------------------------------------------------------------------------------------------

// The limits the comparisons hold the pass to.
constexpr int kMostSkyDifferences = 4; // texels whose sky / surface verdict differs from the processor's
constexpr int kLeastSurfaceTexels = 10000;
constexpr double kNinetyEighth = 0.98; // percentiles
constexpr double kNinetyFifth = 0.95;
constexpr double kNinetyNinth = 0.99;
constexpr double kAll = 1.0;
constexpr double kMostDistanceError = 2e-3;      // relative
constexpr double kMostTransmittanceError = 4e-3; // (16-bit floats)
constexpr double kMostLightError = 4e-3;         // 98% of the texels
constexpr double kStepSlack = 0.01;              // all of them: one step's light and this
constexpr double kInsideShaft = 0.05;            // the exact integral's light above the ambient: crosses the shaft
constexpr double kOutsideShaft = 0.002;          // ... crosses no sunlit fog
constexpr std::size_t kLeastShaftTexels = 200;
constexpr double kShaftContrast = 0.05; // inside above outside by at least this
constexpr double kShaftError = 0.02;    // inside's mean against the exact integral's
constexpr double kMostConvergedMean = 0.006;
constexpr double kMostConverged95 = 0.025;
constexpr std::size_t kLeastInteriorPixels = 2000;
constexpr double kMostCompositeMean = 2.5; // levels
constexpr double kMostComposite95 = 7.0;
constexpr int kLeastSkyPixels = 100;
constexpr double kLevelSlack = 1.5; // levels: a channel changed when it moved by more
constexpr double kLevels = 255.0;
constexpr double kNoLight = 1e-6;
constexpr int kLeastDarkPixels = 300;
constexpr int kPerMille = 1000;
constexpr int kChannelsPerMillePixel = 3; // (a ray query grazing the wall's edge may decide otherwise)
constexpr float kRestarted = 1.5F;        // a history shorter than this started again
constexpr int kLeastKept = 5000;
constexpr double kMostGhost95 = 0.05;
constexpr int kInteriorRadius = 2;         // the pixels around one that must see its surface
constexpr int kDarkRadius = 3;             // ... whose rays must cross no sunlit fog
constexpr uint32_t kDarkSteps = 256;       // the march that tells which rays cross sunlit fog
constexpr double kBlockPixels = 4.0;       // in a texel's 2 x 2 block
constexpr int kLeastLighterChannels = 300; // the haze's shaft

using Rgb = std::array<double, 3>;
Rgb rgb_of(uint32_t colour)
{
    return {static_cast<double>((colour >> kRedShift) & kByteMask) * kPerByte,
            static_cast<double>((colour >> kGreenShift) & kByteMask) * kPerByte,
            static_cast<double>(colour & kByteMask) * kPerByte};
}
Rgb pixel_at(const std::vector<uint8_t>& texels, Pixel pixel)
{
    const std::size_t at =
        ((static_cast<std::size_t>(pixel.y) * kTargetWidth) + static_cast<std::size_t>(pixel.x)) * kPixelBytes;
    return {core::at(texels, at + 2) * kPerByte, core::at(texels, at + 1) * kPerByte, core::at(texels, at) * kPerByte};
}

// The pixel a texel's info names (its x and y at `first`, `first` + 1).
Pixel pixel_of(const std::array<float, 4>& info, std::size_t first)
{
    return {.x = static_cast<int>(std::lround(core::at(info, first))),
            .y = static_cast<int>(std::lround(core::at(info, first + 1)))};
}

int count_not_finite(const render::vulkan::RtFogTexels& image)
{
    int count = 0;
    for (const std::array<float, 4>& texel : image.texels) {
        count += std::ranges::any_of(texel, [](float value) { return !std::isfinite(value); }) ? 1 : 0;
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

double mean(const std::vector<double>& values)
{
    double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

double light_error(const std::array<float, 4>& got, const Fogged& want)
{
    double error = 0.0;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        error = std::max(error, std::abs(static_cast<double>(core::at(got, channel)) - core::at(want.light, channel)));
    }
    return error;
}

Seen seen_at(const View& view, Pixel pixel)
{
    return seen_along({.origin = view.eye, .direction = direction_at(view, pixel)});
}

// One frame's march, texel by texel, against the processor's.
void compare_raw(const View& view, uint64_t frame, bool shafts, Tally* tally)
{
    render::vulkan::RtFogTexels raw;
    render::vulkan::RtFogTexels info;
    const bool read =
        read_fog(render::vulkan::RtFogImage::kRaw, &raw) && read_fog(render::vulkan::RtFogImage::kRawInfo, &info);
    tally->check(read, "the march's images read back");
    if (!read) {
        return;
    }
    tally->check(
        count_not_finite(raw) == 0 && count_not_finite(info) == 0,
        text::format("no NaN or infinity in the march (%d, %d texels)", count_not_finite(raw), count_not_finite(info)));
    std::vector<double> distance_errors;
    std::vector<double> transmittance_errors;
    std::vector<double> light_errors;
    int sky_wrong = 0;
    int surface_pixels = 0;
    int sky_pixels = 0;
    double one_step = 0.0;
    for (std::size_t i = 0; i < raw.texels.size(); ++i) {
        const std::array<float, 4>& got = core::at(raw.texels, i);
        const std::array<float, 4>& where = core::at(info.texels, i);
        const Pixel pixel = pixel_of(where, 1);
        const Vec3 direction = direction_at(view, pixel);
        const Seen seen = seen_along({.origin = view.eye, .direction = direction});
        const double gpu_distance = core::at(where, 0);
        if (seen.solid == kNoSurface || gpu_distance < 0.0) {
            sky_wrong += (seen.solid == kNoSurface) != (gpu_distance < 0.0) ? 1 : 0;
            ++sky_pixels;
            continue;
        }
        ++surface_pixels;
        distance_errors.push_back(std::abs(gpu_distance - seen.distance) / seen.distance);
        const double transmittance = shafts ? 1.0 : exact_transmittance(seen.distance);
        transmittance_errors.push_back(std::abs(static_cast<double>(core::at(got, 3)) - transmittance));
        const Fogged want = march({.eye = view.eye, .direction = direction, .distance = gpu_distance},
                                  {.steps = kSteps, .jitter = jitter_of(pixel, frame), .shafts = shafts});
        light_errors.push_back(light_error(got, want));
        one_step = std::max(one_step, (1.0 - exact_transmittance(seen.distance)) / kSteps);
    }
    tally->check(sky_wrong <= kMostSkyDifferences,
                 text::format("the sky where the processor sees it (%d of %d texels differ)", sky_wrong, sky_pixels));
    tally->check(surface_pixels > kLeastSurfaceTexels, text::format("%d texels on a surface", surface_pixels));
    tally->check(percentile(distance_errors, kNinetyNinth) < kMostDistanceError,
                 text::format("the distance marched to: 99th percentile relative error %.2g",
                              percentile(distance_errors, kNinetyNinth)));
    tally->check(percentile(transmittance_errors, kAll) < kMostTransmittanceError,
                 text::format("the transmittance = exp(-integral) = %s: largest error %.2g (16-bit floats)",
                              shafts ? "1 (shafts mode)" : "(end - d) / (end - start)",
                              percentile(transmittance_errors, kAll)));
    tally->check(percentile(light_errors, kNinetyEighth) < kMostLightError &&
                     percentile(light_errors, kAll) < one_step + kStepSlack,
                 text::format("the light against the processor's march (same jitter, %u steps): 98%% within %.2g, all "
                              "within %.3f (one step's %.3f)",
                              kSteps, percentile(light_errors, kNinetyEighth), percentile(light_errors, kAll),
                              one_step));
}

// The shaft: texels whose ray crosses sunlit fog against those that don't (the exact integral says which).
void compare_shaft(const View& view, Tally* tally)
{
    render::vulkan::RtFogTexels history;
    render::vulkan::RtFogTexels info;
    if (!read_fog(render::vulkan::RtFogImage::kHistory, &history) ||
        !read_fog(render::vulkan::RtFogImage::kHistoryInfo, &info)) {
        tally->check(false, "the history read back");
        return;
    }
    // the light the ambient alone gives along each ray, against what the GPU found: the shaft's extra
    std::vector<double> inside;
    std::vector<double> outside;
    std::vector<double> inside_want;
    const uint32_t width = history.width;
    for (std::size_t i = 0; i < history.texels.size(); ++i) {
        const Pixel pixel{
            .x = std::min(static_cast<int>(i % width) * static_cast<int>(kScale), static_cast<int>(kTargetWidth) - 1),
            .y = std::min(static_cast<int>(i / width) * static_cast<int>(kScale), static_cast<int>(kTargetHeight) - 1)};
        const Seen seen = seen_at(view, pixel);
        if (seen.solid == kNoSurface) {
            continue;
        }
        const Fogged exact = exact_at(view, pixel, false);
        const double ambient_only =
            core::at(kFogColour, 0) * render::vulkan::kFogAmbientShare * (1.0 - exact_transmittance(seen.distance));
        const double extra_want = core::at(exact.light, 0) - ambient_only;
        const double extra_got = static_cast<double>(core::at(core::at(history.texels, i), 0)) - ambient_only;
        if (extra_want > kInsideShaft) {
            inside.push_back(extra_got);
            inside_want.push_back(extra_want);
        } else if (extra_want < kOutsideShaft) {
            outside.push_back(extra_got);
        }
    }
    tally->check(
        inside.size() > kLeastShaftTexels && outside.size() > kLeastShaftTexels,
        text::format("%zu texels' rays cross the shaft, %zu cross no sunlit fog", inside.size(), outside.size()));
    tally->check(mean(inside) > mean(outside) + kShaftContrast &&
                     std::abs(mean(inside) - mean(inside_want)) < kShaftError,
                 text::format("the shaft: its rays' light above the ambient %.3f (the exact integral's %.3f), the "
                              "others' %.3f",
                              mean(inside), mean(inside_want), mean(outside)));
}

// The exact light for a texel's 2 x 2 block (the four pixels its frames march, averaged); false when they don't all see
// one surface.
bool block_light(const View& view, Pixel pixel, Fogged* out)
{
    const int scale = static_cast<int>(kScale);
    const Pixel corner{.x = (pixel.x / scale) * scale, .y = (pixel.y / scale) * scale};
    *out = Fogged{};
    std::size_t first = kNoSurface;
    for (int which = 0; which < 4; ++which) {
        const Pixel each{.x = std::min(corner.x + (which & 1), static_cast<int>(kTargetWidth) - 1),
                         .y = std::min(corner.y + (which >> 1), static_cast<int>(kTargetHeight) - 1)};
        const Seen seen = seen_at(view, each);
        if (which == 0) {
            first = seen.solid;
        }
        if (seen.solid != first || seen.solid == kNoSurface) {
            return false;
        }
        const Fogged one = exact_at(view, each, false);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            core::at(out->light, channel) += core::at(one.light, channel) / kBlockPixels;
        }
    }
    return true;
}

// The history after a still camera's frames against the exact integral, and one frame's march against it.
void compare_converged(const View& view, Tally* tally)
{
    render::vulkan::RtFogTexels history;
    render::vulkan::RtFogTexels info;
    render::vulkan::RtFogTexels raw;
    const bool read = read_fog(render::vulkan::RtFogImage::kHistory, &history) &&
                      read_fog(render::vulkan::RtFogImage::kHistoryInfo, &info) &&
                      read_fog(render::vulkan::RtFogImage::kRaw, &raw);
    tally->check(read, "the history read back");
    if (!read) {
        return;
    }
    tally->check(count_not_finite(history) == 0, text::format("no NaN in the history (%d)", count_not_finite(history)));
    std::vector<double> history_errors;
    std::vector<double> raw_errors;
    int full = 0;
    int counted = 0;
    for (std::size_t i = 0; i < history.texels.size(); ++i) {
        const std::array<float, 4>& was = core::at(info.texels, i);
        Fogged want;
        if (!block_light(view, pixel_of(was, 2), &want)) {
            continue;
        }
        ++counted;
        full += core::at(was, 1) >= static_cast<float>(render::vulkan::kFogMaxHistory) ? 1 : 0;
        history_errors.push_back(light_error(core::at(history.texels, i), want));
        raw_errors.push_back(light_error(core::at(raw.texels, i), want));
    }
    tally->check(full == counted, text::format("every history full (%d of %d at %u frames)", full, counted,
                                               render::vulkan::kFogMaxHistory));
    tally->check(mean(history_errors) < kMostConvergedMean &&
                     percentile(history_errors, kNinetyFifth) < kMostConverged95,
                 text::format("converged against the exact integral: mean error %.4f, 95th percentile %.4f",
                              mean(history_errors), percentile(history_errors, kNinetyFifth)));
    tally->check(mean(history_errors) < mean(raw_errors),
                 text::format("nearer than one frame's march (its mean error %.4f)", mean(raw_errors)));
}

// Whether the pixels around one (within `radius`) see its surface.
bool interior(const View& view, Pixel pixel, std::size_t solid)
{
    for (int dy = -kInteriorRadius; dy <= kInteriorRadius; ++dy) {
        for (int dx = -kInteriorRadius; dx <= kInteriorRadius; ++dx) {
            if (seen_at(view, {.x = pixel.x + dx, .y = pixel.y + dy}).solid != solid) {
                return false;
            }
        }
    }
    return true;
}

// The colour target: surface x T + light (from the processor's exact answers), where the pixel's neighbours share its
// surface; the sky never dimmed.
void compare_composite(const View& view, const std::vector<uint8_t>& texels, bool shafts, Tally* tally)
{
    std::vector<double> errors;
    int sky_dimmed = 0;
    int sky = 0;
    const Rgb clear = rgb_of(kClearColour);
    for (int row = kInteriorRadius; row < static_cast<int>(kTargetHeight) - kInteriorRadius; row += 2) {
        for (int column = kInteriorRadius; column < static_cast<int>(kTargetWidth) - kInteriorRadius; column += 2) {
            const Pixel pixel{.x = column, .y = row};
            const Seen seen = seen_at(view, pixel);
            const Rgb got = pixel_at(texels, pixel);
            if (seen.solid == kNoSurface) {
                ++sky;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    sky_dimmed += core::at(got, channel) + (kLevelSlack * kPerByte) < core::at(clear, channel) ? 1 : 0;
                }
                continue;
            }
            if (!interior(view, pixel, seen.solid)) {
                continue;
            }
            const Fogged exact = exact_at(view, pixel, shafts);
            const Rgb surface = rgb_of(core::at(kSolids, seen.solid - 1).colour);
            double error = 0.0;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const double want =
                    std::min(1.0, (core::at(surface, channel) * exact.transmittance) + core::at(exact.light, channel));
                error = std::max(error, std::abs(core::at(got, channel) - want));
            }
            errors.push_back(error * kLevels);
        }
    }
    tally->check(errors.size() > kLeastInteriorPixels, text::format("%zu interior pixels compared", errors.size()));
    tally->check(mean(errors) < kMostCompositeMean && percentile(errors, kNinetyFifth) < kMostComposite95,
                 text::format("the colour = surface x T + light: mean %.2f levels, 95th percentile %.2f", mean(errors),
                              percentile(errors, kNinetyFifth)));
    tally->check(sky > kLeastSkyPixels && sky_dimmed == 0,
                 text::format("the sky never dimmed (%d of %d pixels' channels darker)", sky_dimmed, sky));
}

// Shafts mode: where no ray crosses sunlit fog the colour is the surface's own.
void compare_shafts_untouched(const View& view, const std::vector<uint8_t>& texels, Tally* tally)
{
    // (which pixels' rays cross sunlit fog: a march of each at the middle of its segments)
    const int columns = static_cast<int>(kTargetWidth);
    const int rows = static_cast<int>(kTargetHeight);
    const auto index = [columns](Pixel pixel) {
        return (static_cast<std::size_t>(pixel.y) * static_cast<std::size_t>(columns)) +
               static_cast<std::size_t>(pixel.x);
    };
    std::vector<bool> lit(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const Pixel pixel{.x = column, .y = row};
            const Vec3 direction = direction_at(view, pixel);
            const Seen seen = seen_along({.origin = view.eye, .direction = direction});
            const Fogged fogged = march({.eye = view.eye, .direction = direction, .distance = seen.distance},
                                        {.steps = kDarkSteps, .jitter = kMiddle, .shafts = true});
            lit.at(index(pixel)) = core::at(fogged.light, 0) > kNoLight;
        }
    }
    const auto dark_round = [&](Pixel pixel) {
        for (int dy = -kDarkRadius; dy <= kDarkRadius; ++dy) {
            for (int dx = -kDarkRadius; dx <= kDarkRadius; ++dx) {
                const Pixel around{.x = std::clamp(pixel.x + dx, 0, columns - 1),
                                   .y = std::clamp(pixel.y + dy, 0, rows - 1)};
                if (lit.at(index(around))) {
                    return false;
                }
            }
        }
        return true;
    };
    int compared = 0;
    int changed = 0;
    double largest = 0.0;
    for (int row = 1; row < rows - 1; row += 2) {
        for (int column = 1; column < columns - 1; column += 2) {
            const Pixel pixel{.x = column, .y = row};
            const Seen seen = seen_at(view, pixel);
            if (seen.solid == kNoSurface || !dark_round(pixel)) {
                continue;
            }
            ++compared;
            const Rgb got = pixel_at(texels, pixel);
            const Rgb surface = rgb_of(core::at(kSolids, seen.solid - 1).colour);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const double difference = std::abs(core::at(got, channel) - core::at(surface, channel));
                changed += difference > kLevelSlack * kPerByte ? 1 : 0;
                largest = std::max(largest, difference / kPerByte);
            }
        }
    }
    tally->check(compared > kLeastDarkPixels && changed * kPerMille <= compared * kChannelsPerMillePixel,
                 text::format("shafts mode: %d pixels in no sunlit fog unchanged (%d channels changed, at most %.1f "
                              "levels)",
                              compared, changed, largest));
}

// After the camera moved: no NaN, no ghost (the history near the new view's exact answer).
void compare_moved(const View& view, Tally* tally)
{
    render::vulkan::RtFogTexels history;
    render::vulkan::RtFogTexels info;
    if (!read_fog(render::vulkan::RtFogImage::kHistory, &history) ||
        !read_fog(render::vulkan::RtFogImage::kHistoryInfo, &info)) {
        tally->check(false, "the history read back after the move");
        return;
    }
    std::vector<double> errors;
    int kept = 0;
    int restarted = 0;
    for (std::size_t i = 0; i < history.texels.size(); ++i) {
        const std::array<float, 4>& was = core::at(info.texels, i);
        const Pixel pixel = pixel_of(was, 2);
        if (seen_at(view, pixel).solid == kNoSurface) {
            continue;
        }
        kept += core::at(was, 1) > kRestarted ? 1 : 0;
        restarted += core::at(was, 1) < kRestarted ? 1 : 0;
        errors.push_back(light_error(core::at(history.texels, i), exact_at(view, pixel, false)));
    }
    tally->check(count_not_finite(history) == 0, "no NaN after the move");
    tally->check(kept > kLeastKept, text::format("histories kept across the move: %d (restarted %d)", kept, restarted));
    tally->check(percentile(errors, kNinetyFifth) < kMostGhost95,
                 text::format("no ghost: 95th percentile error after the move %.4f", percentile(errors, kNinetyFifth)));
}

void compare_untouched(const std::vector<uint8_t>& texels, const std::vector<uint8_t>& plain, Tally* tally)
{
    constexpr std::size_t kAlphaByte = 3;
    int differ = 0;
    for (std::size_t i = 0; i < texels.size(); ++i) {
        differ += (i % kPixelBytes) != kAlphaByte && core::at(texels, i) != core::at(plain, i) ? 1 : 0;
    }
    tally->check(differ == 0, text::format("the picture as without the pass (%d bytes differ)", differ));
}

// --- the cases -----------------------------------------------------------------------------------------------------

render::vulkan::RtFogSettings settings_of(bool shafts)
{
    return {.on = true,
            .density = 1.0F,
            .height = 0.0F,
            .anisotropy = kAnisotropy,
            .steps = kSteps,
            .mode = shafts ? render::vulkan::RtFogMode::kShafts : render::vulkan::RtFogMode::kReplace,
            .hdr = false,
            .haze = false};
}

void use_settings(const render::vulkan::RtFogSettings& settings, VkAccelerationStructureKHR scene)
{
    render::vulkan::set_rt_fog_settings(settings);
    render::vulkan::rt_fog_set_scene(scene);
}

void run_replace(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    text::print("the level's fog replaced (mode 0, %u steps):\n", kSteps);
    use_settings(settings_of(false), scene.top.handle);
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, &targets);
    tally->check(made, "the targets made");
    if (made) {
        const View view = view_from(kEye, kTargetWidth, kTargetHeight);
        tally->check(draw_frame(gpu, targets, kEye) == 1, "the listener recorded the passes");
        tally->check(render::vulkan::rt_fog_replaces_level_fog(), "the draws leave the level's fog out");
        compare_raw(view, 0, false, tally);
        int passes = 1;
        for (int frame = 1; frame < kStillFrames; ++frame) {
            passes += static_cast<int>(draw_frame(gpu, targets, kEye));
        }
        tally->check(passes == kStillFrames, text::format("%d frames, %d passes", kStillFrames, passes));
        compare_raw(view, kStillFrames - 1, false, tally);
        compare_converged(view, tally);
        compare_shaft(view, tally);
        std::vector<uint8_t> texels;
        const bool read = read_colour(targets, &texels);
        tally->check(read, "the target read back");
        if (read) {
            compare_composite(view, texels, false, tally);
        }
        tally->check(draw_frame(gpu, targets, kMovedEye) == 1, "the moved camera's frame");
        compare_moved(view_from(kMovedEye, kTargetWidth, kTargetHeight), tally);
    }
    release_targets(gpu, targets);
}

void run_shafts(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    text::print("shafts over the level's fog (mode 1):\n");
    use_settings(settings_of(true), scene.top.handle);
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, &targets);
    tally->check(made, "the targets made");
    if (made) {
        const View view = view_from(kEye, kTargetWidth, kTargetHeight);
        tally->check(draw_frame(gpu, targets, kEye) == 1, "the listener recorded the passes");
        tally->check(!render::vulkan::rt_fog_replaces_level_fog(), "the draws keep the level's fog");
        compare_raw(view, 0, true, tally);
        for (int frame = 1; frame < kStillFrames; ++frame) {
            draw_frame(gpu, targets, kEye);
        }
        std::vector<uint8_t> texels;
        const bool read = read_colour(targets, &texels);
        tally->check(read, "the target read back");
        if (read) {
            compare_composite(view, texels, true, tally);
            compare_shafts_untouched(view, texels, tally);
        }
    }
    release_targets(gpu, targets);
}

// The pass skipped: the picture as drawn without it.
void run_untouched(render::Gpu& gpu, const char* name, const render::vulkan::RtFogSettings& settings,
                   VkAccelerationStructureKHR scene, bool fog_on, Tally* tally)
{
    text::print("%s:\n", name);
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, &targets);
    tally->check(made, "the targets made");
    if (made) {
        use_settings({}, VK_NULL_HANDLE);
        draw_frame(gpu, targets, kEye, fog_on);
        std::vector<uint8_t> plain;
        const bool read_plain = read_colour(targets, &plain);
        use_settings(settings, scene);
        tally->check(draw_frame(gpu, targets, kEye, fog_on) == 0, "no pass recorded");
        tally->check(!render::vulkan::rt_fog_replaces_level_fog(), "the draws keep the level's fog");
        std::vector<uint8_t> texels;
        const bool read = read_plain && read_colour(targets, &texels);
        tally->check(read, "the target read back");
        if (read) {
            compare_untouched(texels, plain, tally);
        }
    }
    release_targets(gpu, targets);
}

// No fog in the level, rt_fog_haze=1: the haze's sunlit light added - nothing darker, the shaft lighter, the draws'
// own fog kept.
void run_haze(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    text::print("no fog in the level, the haze (rt_fog_haze=1):\n");
    Targets targets;
    const bool made = make_targets(gpu, kTargetWidth, kTargetHeight, &targets);
    tally->check(made, "the targets made");
    if (!made) {
        return;
    }
    use_settings({}, VK_NULL_HANDLE);
    draw_frame(gpu, targets, kEye, false);
    std::vector<uint8_t> plain;
    const bool read_plain = read_colour(targets, &plain);
    render::vulkan::RtFogSettings settings = settings_of(false);
    settings.haze = true;
    use_settings(settings, scene.top.handle);
    tally->check(draw_frame(gpu, targets, kEye, false) == 1, "the pass recorded");
    tally->check(!render::vulkan::rt_fog_replaces_level_fog(), "the draws keep the level's fog");
    render::vulkan::RtFogTexels raw;
    tally->check(
        read_fog(render::vulkan::RtFogImage::kRaw, &raw) && count_not_finite(raw) == 0 &&
            std::ranges::all_of(raw.texels, [](const std::array<float, 4>& texel) { return texel.back() == 1.0F; }),
        "the march: no NaN, nothing dimmed (T = 1)");
    std::vector<uint8_t> texels;
    const bool read = read_plain && read_colour(targets, &texels);
    tally->check(read, "the target read back");
    if (read) {
        int darker = 0;
        int lighter = 0;
        for (std::size_t i = 0; i < texels.size(); ++i) {
            const int difference = static_cast<int>(core::at(texels, i)) - static_cast<int>(core::at(plain, i));
            darker += difference < -1 ? 1 : 0;
            lighter += difference > 1 ? 1 : 0;
        }
        tally->check(darker == 0 && lighter > kLeastLighterChannels,
                     text::format("nothing darker (%d channels), the shaft lighter (%d channels)", darker, lighter));
    }
    release_targets(gpu, targets);
}

// The passes at 1920 x 1080 timed on the device (their own timestamps) with 16, 32, 64 steps.
void run_perf(render::Gpu& gpu, const Scene& scene)
{
    text::print("cost at %ux%u (the passes' timestamps on the device, 60 frames each):\n", kPerfWidth, kPerfHeight);
    use_settings(settings_of(false), scene.top.handle); // (on before the targets: their depth made samplable)
    Targets targets;
    if (!make_targets(gpu, kPerfWidth, kPerfHeight, &targets)) {
        text::print("  the targets couldn't be made\n");
        return;
    }
    constexpr int kFrames = 60;
    render::vulkan::Backend& state = render::vulkan::backend();
    const auto timing = [&state] {
        const std::scoped_lock lock(state.mutex);
        render::vulkan::finish_work(state);
        return render::vulkan::rt_fog_timing();
    };
    for (const unsigned steps : {16U, 32U, 64U}) {
        render::vulkan::RtFogSettings settings = settings_of(false);
        settings.steps = steps;
        use_settings(settings, scene.top.handle);
        const render::vulkan::RtFogTiming before = timing();
        for (int frame = 0; frame < kFrames; ++frame) {
            draw_frame(gpu, targets, kEye);
            const std::scoped_lock lock(state.mutex);
            render::vulkan::finish_work(state);
        }
        const render::vulkan::RtFogTiming after = timing();
        const uint64_t samples = after.samples - before.samples;
        text::print("  %2u steps: %.3f ms (%llu passes timed)\n", steps,
                    samples == 0 ? 0.0 : (after.milliseconds - before.milliseconds) / static_cast<double>(samples),
                    static_cast<unsigned long long>(samples));
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
    render::vulkan::set_rt_fog_settings(settings_of(false));
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
    tally.check(built, "the check's acceleration structures built (a BLAS of the wall and the ground, a TLAS)");
    if (built) {
        render::Gpu& gpu = render::vulkan::gpu();
        run_replace(gpu, scene, &tally);
        run_shafts(gpu, scene, &tally);
        run_untouched(gpu, "no fog in the level, rt_fog_haze=0: the pass skipped", settings_of(false), scene.top.handle,
                      false, &tally);
        run_haze(gpu, scene, &tally);
        run_untouched(gpu, "no scene: the pass skipped", settings_of(false), VK_NULL_HANDLE, true, &tally);
        render::vulkan::RtFogSettings off = settings_of(false);
        off.on = false;
        run_untouched(gpu, "rt_fog off: no pass", off, scene.top.handle, true, &tally);
        if (options.perf) {
            run_perf(gpu, scene);
        }
    }
    render::vulkan::rt_fog_set_scene(VK_NULL_HANDLE);
    render::vulkan::set_rt_fog_settings({});
    render::rt::set_sun_override(nullptr);
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
    text::print("thug2_vulkan_rt_fog_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan volumetric fog check", kWindowWidth, kWindowHeight,
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
