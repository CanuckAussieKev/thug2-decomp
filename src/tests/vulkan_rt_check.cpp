// NOT ORIGINAL - thug2_vulkan_rt_check.exe: ray tracing's scene on Vulkan (gpu_vulkan/vulkan_rt_scene.h) tried on this
// machine. It opens the device with ray tracing (as [Graphics] ray_tracing=1 does), makes meshes through render::Gpu as
// the PC layer does - indexed lists (16- and 32-bit indices, vertex, index and position offsets), indexed and unindexed
// strips, unindexed lists, a dynamic mesh, many small meshes sharing two buffers - registers them with
// render::rt::RayTracing, and draws frames of instances with transforms and material ids. Each frame's TLAS is then
// traced from a compute shader with ray queries (trace_rays_now) and every ray compared with a reference traced here on
// the processor in double precision: hit or miss, distance, instance, primitive, material and barycentrics. Rays the
// reference finds too close to an edge or another surface to call are counted and left out. Also: the static meshes'
// compaction, the dynamic mesh refitted after its vertices change (and rebuilt after many refits), a mesh removed and
// added again, an empty scene, everything released, and the validation layer's messages (none allowed). (Round OM)
// Opacity micromaps where the device has them: their states against the shaders' own alpha test, the same rays with and
// without them, and baked again when the material or its texture changes.
//   thug2_vulkan_rt_check [--device N] [--display N] [--no-validation] [--rays N] [--seed N]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan ray tracing for it.
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
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_poses.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_rt_micromaps.h"
#include "gpu_vulkan/vulkan_rt_scene.h"

namespace {

namespace value = render::gpu_value;
namespace rt = render::rt;
using render::vulkan::TracedHit;
using render::vulkan::TracedRay;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFormatIndex32 = 102; // D3DFMT_INDEX32
constexpr uint32_t kFvf = 0x142;
constexpr uint32_t kDynamic = value::kUsageDynamic | value::kUsageWriteOnly;
constexpr int kDefaultRays = 4096;
constexpr uint32_t kDefaultSeed = 0x52325;
constexpr int kRefitRounds = 40; // past the refits before a rebuild (vulkan_rt_scene.cpp)
constexpr int kRefitRays = 512;
constexpr uint32_t kSmallMeshes = 1200; // (more than the backend's 1024 compaction queries: some wait for a later frame)
constexpr uint32_t kSide = 8;       // the grids' quads a side
constexpr double kGridSize = 12.0;
constexpr float kFar = 1000.0F;
constexpr double kEdgeMargin = 1e-3;   // a ray this close to an edge (in barycentrics) is too close to call
constexpr double kDepthMargin = 1e-3;  // two surfaces this close along a ray: too close to call
constexpr double kGrazing = 2e-2;      // |det| / (|e1| |e2|): a ray this close to a triangle's plane
constexpr double kNoArea = 1e-9;       // |e1 x e2| / (|e1| |e2|): a triangle with no area (a strip's join)
constexpr double kDistanceSlack = 1e-3;
constexpr double kDistanceRelative = 1e-4;
constexpr double kBarycentricSlack = 1e-2;
constexpr double kMostLeftOut = 0.1;   // of the rays at most this many too close to call
constexpr float kJunk = 1.0e30F;       // what the bytes around the positions hold (an offset misread shows)
constexpr uint8_t kJunkByte = 0xFF;    // and the indices before the first
constexpr double kTurn = 2.0 * 3.14159265358979323846;
constexpr double kLeastScale = 0.5;
constexpr double kMostScale = 2.0;
constexpr float kTetraSpread = 1.5F;
constexpr uint32_t kUnregistered = 0x7FFFFFF0U;     // a mesh id never given out
constexpr uint64_t kKilobyte = 1024;
constexpr int kTraceEvery = 8;                      // the refits' rounds between comparisons
constexpr double kSceneHalf = 40.0;     // instances inside [-40, 40]^3
constexpr double kOriginHalf = 70.0;    // ray origins inside [-70, 70]^3
constexpr double kInteriorLeast = 0.05; // a targeted ray's point: barycentrics at least this
constexpr double kInteriorSpan = 1.0 - (3.0 * kInteriorLeast); // what is left of a barycentric after the margins
constexpr uint32_t kMaterialBase = 1000;
constexpr double kMovedPhase = 1.9; // the dynamic mesh's heights in its other buffer
constexpr uint32_t kMaskWorld = 0x01; // the instance masks (vulkan_rt_scene.h)
constexpr uint32_t kMaskObjects = 0x02;
constexpr uint32_t kMaskTested = 0x04;
constexpr uint32_t kMaskNever = 0x80;
constexpr uint32_t kMaskAddsLight = 0x40; // (round PT-B: a glow - kAddsLight)
constexpr std::array<uint32_t, 7> kCullMasks{0xFF, 0x01, 0x02, 0x04, 0x80, 0x03, 0x40}; // the rays' (one each, in turn)
constexpr float kByteRange = 255.0F;
constexpr float kAlphaSlack = 1.0e-3F;
constexpr float kNoRejectAlphaRef = 1.0F / 255.0F; // the game's usual reference: rejects nothing drawn
constexpr float kRejectingAlphaRef = 0.5F;

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
    int rays = kDefaultRays;
    uint32_t seed = kDefaultSeed;
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
        } else if (argument == "--rays" && has_value) {
            options.rays = std::max(1, text::to_int(core::at(arguments, ++i)));
        } else if (argument == "--seed" && has_value) {
            options.seed = static_cast<uint32_t>(text::to_int(core::at(arguments, ++i)));
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

// --- double-precision vectors ------------------------------------------------------------------------------------

struct V3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};
V3 operator-(const V3& a, const V3& b) { return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z}; }
V3 operator+(const V3& a, const V3& b) { return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z}; }
V3 operator*(const V3& a, double scale) { return {.x = a.x * scale, .y = a.y * scale, .z = a.z * scale}; }
double dot(const V3& a, const V3& b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }
V3 cross(const V3& a, const V3& b)
{
    return {.x = (a.y * b.z) - (a.z * b.y), .y = (a.z * b.x) - (a.x * b.z), .z = (a.x * b.y) - (a.y * b.x)};
}
double length(const V3& a) { return std::sqrt(dot(a, a)); }

using Position = std::array<float, 3>;
using Transform = std::array<std::array<float, 4>, 3>;

V3 transformed(const Transform& matrix, const Position& point)
{
    const auto row = [&](std::size_t index) {
        const std::array<float, 4>& line = core::at(matrix, index);
        return (static_cast<double>(std::get<0>(line)) * std::get<0>(point)) +
               (static_cast<double>(std::get<1>(line)) * std::get<1>(point)) +
               (static_cast<double>(std::get<2>(line)) * std::get<2>(point)) + static_cast<double>(std::get<3>(line));
    };
    return {.x = row(0), .y = row(1), .z = row(2)};
}

// --- meshes ------------------------------------------------------------------------------------------------------

// How a mesh is laid out in its buffers.
struct Layout {
    bool indexed = true;
    bool wide = false; // 32-bit indices
    bool strip = false;
    bool dynamic = false;
    uint32_t stride = 24;
    uint32_t position_offset = 0;
    uint32_t leading_vertices = 0; // junk vertices before the first (vertex_offset)
    uint32_t leading_indices = 0;  // junk indices before the first (index_offset)
};

struct MeshData {
    std::vector<Position> positions;
    std::vector<uint32_t> corners; // the indices drawn (a list's or a strip's); unindexed: 0..n-1
};

// A mesh made through the Gpu, as the reference sees it.
struct TestMesh {
    rt::MeshId id = 0;
    MeshData data;
    Layout layout;
    render::GpuVertexBuffer* vertices = nullptr;
    render::GpuIndexBuffer* indices = nullptr;
    rt::Geometry geometry;
    bool registered = false;
};

// The triangles as the BLAS has them: a strip's odd ones turned back round (vulkan_rt_scene.cpp).
std::vector<std::array<uint32_t, 3>> triangles_of(const TestMesh& mesh)
{
    std::vector<std::array<uint32_t, 3>> triangles;
    const std::vector<uint32_t>& corners = mesh.data.corners;
    if (mesh.layout.strip) {
        for (std::size_t i = 0; i + 2 < corners.size(); ++i) {
            const bool odd = (i & 1U) != 0;
            triangles.push_back({core::at(corners, odd ? i + 1 : i), core::at(corners, odd ? i : i + 1), core::at(corners, i + 2)});
        }
    } else {
        for (std::size_t i = 0; i + 2 < corners.size(); i += 3) {
            triangles.push_back({core::at(corners, i), core::at(corners, i + 1), core::at(corners, i + 2)});
        }
    }
    return triangles;
}

void put_float(std::span<uint8_t> bytes, std::size_t at, float value)
{
    std::memcpy(bytes.subspan(at, sizeof(value)).data(), &value, sizeof(value));
}

// The vertex buffer's bytes: junk vertices, then each position at its offset with junk around it.
std::vector<uint8_t> vertex_bytes(const MeshData& data, const Layout& layout)
{
    const std::size_t count = layout.leading_vertices + data.positions.size();
    std::vector<uint8_t> bytes(count * layout.stride);
    for (std::size_t at = 0; at + sizeof(float) <= bytes.size(); at += sizeof(float)) {
        put_float(bytes, at, kJunk);
    }
    for (std::size_t i = 0; i < data.positions.size(); ++i) {
        const std::size_t base = ((layout.leading_vertices + i) * layout.stride) + layout.position_offset;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            put_float(bytes, base + (axis * sizeof(float)), core::at(core::at(data.positions, i), axis));
        }
    }
    return bytes;
}

std::vector<uint8_t> index_bytes(const MeshData& data, const Layout& layout)
{
    const std::size_t size = layout.wide ? sizeof(uint32_t) : sizeof(uint16_t);
    std::vector<uint8_t> bytes((layout.leading_indices + data.corners.size()) * size, kJunkByte); // (the largest)
    for (std::size_t i = 0; i < data.corners.size(); ++i) {
        const std::size_t at = (layout.leading_indices + i) * size;
        if (layout.wide) {
            const uint32_t index = core::at(data.corners, i);
            std::memcpy(std::span(bytes).subspan(at, size).data(), &index, size);
        } else {
            const auto index = static_cast<uint16_t>(core::at(data.corners, i));
            std::memcpy(std::span(bytes).subspan(at, size).data(), &index, size);
        }
    }
    return bytes;
}

bool fill_vertices(render::Gpu& gpu, render::GpuVertexBuffer* buffer, std::span<const uint8_t> bytes, bool dynamic)
{
    void* data = nullptr;
    if (gpu.lock_vertex_buffer(buffer, 0, 0, &data, dynamic ? value::kLockDiscard : 0) != value::kResultOk) {
        return false;
    }
    std::memcpy(data, bytes.data(), bytes.size());
    return gpu.unlock_vertex_buffer(buffer) == value::kResultOk;
}

// The mesh's buffers made and filled through the Gpu; its geometry.
bool make_mesh(render::Gpu& gpu, TestMesh* mesh)
{
    const Layout& layout = mesh->layout;
    const std::vector<uint8_t> vertices = vertex_bytes(mesh->data, layout);
    const uint32_t usage = layout.dynamic ? kDynamic : 0U;
    if (gpu.create_vertex_buffer(static_cast<uint32_t>(vertices.size()), usage, kFvf, value::kPoolDefault,
                                 &mesh->vertices) != value::kResultOk ||
        !fill_vertices(gpu, mesh->vertices, vertices, layout.dynamic)) {
        return false;
    }
    if (layout.indexed) {
        const std::vector<uint8_t> indices = index_bytes(mesh->data, layout);
        void* data = nullptr;
        if (gpu.create_index_buffer(static_cast<uint32_t>(indices.size()), 0,
                                    layout.wide ? kFormatIndex32 : value::kFormatIndex16, value::kPoolDefault,
                                    &mesh->indices) != value::kResultOk ||
            gpu.lock_index_buffer(mesh->indices, 0, 0, &data, 0) != value::kResultOk) {
            return false;
        }
        std::memcpy(data, indices.data(), indices.size());
        if (gpu.unlock_index_buffer(mesh->indices) != value::kResultOk) {
            return false;
        }
    }
    mesh->geometry = {.vertices = mesh->vertices,
                      .vertex_offset = layout.leading_vertices * layout.stride,
                      .vertex_stride = layout.stride,
                      .vertex_count = static_cast<uint32_t>(mesh->data.positions.size()),
                      .position_offset = layout.position_offset,
                      .indices = mesh->indices,
                      .index_offset = layout.leading_indices,
                      .index_count = layout.indexed ? static_cast<uint32_t>(mesh->data.corners.size()) : 0U,
                      .indices_32bit = layout.wide,
                      .strip = layout.strip,
                      .dynamic = layout.dynamic};
    return true;
}

void release_mesh(render::Gpu& gpu, TestMesh* mesh)
{
    if (mesh->vertices != nullptr) {
        gpu.release_vertex_buffer(mesh->vertices);
    }
    if (mesh->indices != nullptr) {
        gpu.release_index_buffer(mesh->indices);
    }
    mesh->vertices = nullptr;
    mesh->indices = nullptr;
}

// A grid of (side x side) quads, its heights from `phase`.
MeshData grid(uint32_t side, double size, double phase)
{
    MeshData data;
    for (uint32_t row = 0; row <= side; ++row) {
        for (uint32_t column = 0; column <= side; ++column) {
            const double x = ((static_cast<double>(column) / side) - 0.5) * size;
            const double z = ((static_cast<double>(row) / side) - 0.5) * size;
            const double y = std::sin((x * 0.7) + phase) * std::cos((z * 0.5) - phase) * (size * 0.1);
            data.positions.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        }
    }
    return data;
}

void grid_list(MeshData* data, uint32_t side)
{
    for (uint32_t row = 0; row < side; ++row) {
        for (uint32_t column = 0; column < side; ++column) {
            const uint32_t near_left = (row * (side + 1)) + column;
            const uint32_t near_right = near_left + 1;
            const uint32_t far_left = near_left + side + 1;
            const uint32_t far_right = far_left + 1;
            data->corners.insert(data->corners.end(),
                                 {near_left, far_left, near_right, near_right, far_left, far_right});
        }
    }
}

// The grid's rows as one strip, joined by degenerate triangles.
void grid_strip(MeshData* data, uint32_t side)
{
    for (uint32_t row = 0; row < side; ++row) {
        if (row > 0) {
            data->corners.push_back(data->corners.back());
            data->corners.push_back(row * (side + 1));
        }
        for (uint32_t column = 0; column <= side; ++column) {
            data->corners.push_back((row * (side + 1)) + column);
            data->corners.push_back(((row + 1) * (side + 1)) + column);
        }
    }
}

// An unindexed mesh: the corners' positions written out in order.
MeshData unindexed(const MeshData& indexed)
{
    MeshData data;
    for (const uint32_t corner : indexed.corners) {
        data.positions.push_back(core::at(indexed.positions, corner));
        data.corners.push_back(static_cast<uint32_t>(data.corners.size()));
    }
    return data;
}

// A tetrahedron about the origin.
MeshData tetrahedron(std::mt19937& random)
{
    std::uniform_real_distribution<float> spread(-kTetraSpread, kTetraSpread);
    MeshData data;
    data.positions = {{1.0F + spread(random), 0.0F, 0.0F},
                      {-1.0F, 1.0F + spread(random), 0.0F},
                      {-1.0F, -1.0F, 1.0F + spread(random)},
                      {0.0F, 0.0F, -1.0F - spread(random)}};
    data.corners = {0, 1, 2, 0, 3, 1, 1, 3, 2, 2, 3, 0};
    return data;
}

// --- the scene ------------------------------------------------------------------------------------------------------

struct SceneInstance {
    std::size_t mesh = 0; // in the meshes; past the end: a mesh id never registered
    rt::Instance instance;
    float alpha_ref = -1.0F; // its material's (set_material); below 0: never given
};

// The mask an instance should have (written here from the masks' definitions - vulkan_rt_scene.h - not with the
// backend's code): 0x80 transparent or no shadow, 0x04 an alpha test that can reject (a reference above 1/255, or a
// material never given), 0x02 an object, else 0x01.
uint32_t expected_mask(const SceneInstance& each)
{
    const uint32_t flags = each.instance.flags;
    if ((flags & rt::kAddsLight) != 0) {
        return kMaskAddsLight; // (round PT-B)
    }
    if ((flags & rt::kTransparent) != 0 || (flags & rt::kCastsShadow) == 0) {
        return kMaskNever;
    }
    const bool tests = (flags & rt::kAlphaTested) != 0 && each.instance.material != 0 &&
                       (each.alpha_ref < 0.0F || each.alpha_ref * kByteRange > 1.0F + kAlphaSlack);
    if (tests) {
        return kMaskTested;
    }
    return (flags & rt::kObject) != 0 ? kMaskObjects : kMaskWorld;
}

Transform random_transform(std::mt19937& random)
{
    std::uniform_real_distribution<double> angle(0.0, kTurn);
    std::uniform_real_distribution<double> place(-kSceneHalf, kSceneHalf);
    std::uniform_real_distribution<double> scale(kLeastScale, kMostScale);
    const double yaw = angle(random);
    const double pitch = angle(random);
    const double size = scale(random);
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    // yaw about y, then pitch about x, scaled
    const std::array<std::array<double, 3>, 3> rotation{{{cy, 0.0, sy}, {sy * sp, cp, -cy * sp}, {-sy * cp, sp, cy * cp}}};
    Transform made{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            core::at(core::at(made, row), column) = static_cast<float>(core::at(core::at(rotation, row), column) * size);
        }
        core::at(core::at(made, row), 3) = static_cast<float>(place(random));
    }
    return made;
}

// A triangle in the world, as the reference traces it.
struct WorldTriangle {
    V3 a;
    V3 b;
    V3 c;
    uint32_t instance = 0;
    uint32_t primitive = 0;
    uint32_t material = 0;
    uint32_t mask = 0;
};

std::vector<WorldTriangle> world_triangles(const std::vector<TestMesh>& meshes,
                                           const std::vector<SceneInstance>& instances)
{
    std::vector<WorldTriangle> triangles;
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const SceneInstance& each = core::at(instances, i);
        if (each.mesh >= meshes.size() || !core::at(meshes, each.mesh).registered) {
            continue;
        }
        const TestMesh& mesh = core::at(meshes, each.mesh);
        const std::vector<std::array<uint32_t, 3>> listed = triangles_of(mesh);
        for (std::size_t primitive = 0; primitive < listed.size(); ++primitive) {
            const std::array<uint32_t, 3>& corners = core::at(listed, primitive);
            const Transform& matrix = each.instance.object_to_world;
            triangles.push_back({.a = transformed(matrix, core::at(mesh.data.positions, std::get<0>(corners))),
                                 .b = transformed(matrix, core::at(mesh.data.positions, std::get<1>(corners))),
                                 .c = transformed(matrix, core::at(mesh.data.positions, std::get<2>(corners))),
                                 .instance = static_cast<uint32_t>(i),
                                 .primitive = static_cast<uint32_t>(primitive),
                                 .material = each.instance.material,
                                 .mask = expected_mask(each)});
        }
    }
    return triangles;
}

// The reference's answer for a ray: the nearest hit, and whether the ray is too close to call.
struct Reference {
    bool hit = false;
    bool unclear = false;
    double t = 0.0;
    uint32_t instance = 0;
    uint32_t primitive = 0;
    uint32_t material = 0;
    double u = 0.0;
    double v = 0.0;
};

Reference trace_reference(const std::vector<WorldTriangle>& triangles, const TracedRay& ray)
{
    const V3 origin{.x = std::get<0>(ray.origin), .y = std::get<1>(ray.origin), .z = std::get<2>(ray.origin)};
    const V3 direction{.x = std::get<0>(ray.direction), .y = std::get<1>(ray.direction), .z = std::get<2>(ray.direction)};
    Reference best;
    best.t = ray.t_max;
    struct Near {
        double t;
        bool edge;
    };
    std::vector<Near> candidates;
    for (const WorldTriangle& triangle : triangles) {
        if ((triangle.mask & ray.cull_mask) == 0) {
            continue;
        }
        const V3 e1 = triangle.b - triangle.a;
        const V3 e2 = triangle.c - triangle.a;
        const V3 across = cross(direction, e2);
        const double det = dot(e1, across);
        const double sides = length(e1) * length(e2);
        const V3 normal = cross(e1, e2);
        if (sides == 0.0 || length(normal) < kNoArea * sides) {
            continue; // (no area: a strip's joins - never hit)
        }
        if (det == 0.0) {
            continue;
        }
        // (a ray along the triangle's plane, or nearly: a hit on it is too close to call)
        const bool grazing = std::abs(det) < kGrazing * sides * length(direction);
        const double inverse = 1.0 / det;
        const V3 from_a = origin - triangle.a;
        const double u = dot(from_a, across) * inverse;
        const V3 turned = cross(from_a, e1);
        const double v = dot(direction, turned) * inverse;
        const double along = dot(e2, turned) * inverse;
        if (along < ray.t_min - kDepthMargin || along > ray.t_max + kDepthMargin) {
            continue;
        }
        const double w = 1.0 - u - v;
        const double least = std::min({u, v, w});
        if (least < -kEdgeMargin) {
            continue;
        }
        const bool edge =
            grazing || least < kEdgeMargin || along < ray.t_min + kDepthMargin || along > ray.t_max - kDepthMargin;
        candidates.push_back({.t = along, .edge = edge});
        if (!edge && along < best.t) {
            best = {.hit = true,
                    .unclear = false,
                    .t = along,
                    .instance = triangle.instance,
                    .primitive = triangle.primitive,
                    .material = triangle.material,
                    .u = u,
                    .v = v};
        }
    }
    // unclear: a near-edge surface at or before the nearest clear hit, or another surface as near as it
    for (const Near& seen : candidates) {
        const double limit = best.hit ? best.t : ray.t_max;
        if (seen.edge && seen.t <= limit + kDepthMargin) {
            best.unclear = true;
        }
        if (best.hit && !seen.edge && seen.t != best.t && std::abs(seen.t - best.t) < kDepthMargin) {
            best.unclear = true;
        }
    }
    return best;
}

TracedRay random_ray(std::mt19937& random, const std::vector<WorldTriangle>& triangles, bool targeted)
{
    std::uniform_real_distribution<double> place(-kOriginHalf, kOriginHalf);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const V3 origin{.x = place(random), .y = place(random), .z = place(random)};
    V3 direction{.x = place(random), .y = place(random), .z = place(random)};
    if (targeted && !triangles.empty()) {
        std::uniform_int_distribution<std::size_t> pick(0, triangles.size() - 1);
        const WorldTriangle& triangle = core::at(triangles, pick(random));
        double u = unit(random);
        double v = unit(random);
        if (u + v > 1.0) {
            u = 1.0 - u;
            v = 1.0 - v;
        }
        u = kInteriorLeast + (u * kInteriorSpan);
        v = kInteriorLeast + (v * kInteriorSpan);
        const V3 target = triangle.a + ((triangle.b - triangle.a) * u) + ((triangle.c - triangle.a) * v);
        direction = target - origin;
    }
    const double size = length(direction);
    if (size > 0.0) {
        direction = direction * (1.0 / size);
    }
    return {.origin = {static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z)},
            .direction = {static_cast<float>(direction.x), static_cast<float>(direction.y),
                          static_cast<float>(direction.z)},
            .t_min = 0.0F,
            .t_max = kFar};
}

// What a comparison found.
struct Comparison {
    int rays = 0;
    int hits = 0;
    int unclear = 0;
    int wrong = 0;
    std::string first_wrong;
};

std::string ray_text(const TracedRay& ray)
{
    return text::format("(%.4f %.4f %.4f) along (%.5f %.5f %.5f)", std::get<0>(ray.origin), std::get<1>(ray.origin), std::get<2>(ray.origin),
                        std::get<0>(ray.direction), std::get<1>(ray.direction), std::get<2>(ray.direction));
}

// The rays traced on the device and by the reference; each that the reference can call compared.
Comparison compare_rays(const std::vector<TestMesh>& meshes, const std::vector<SceneInstance>& instances, int count,
                        std::mt19937& random, bool* traced)
{
    const std::vector<WorldTriangle> triangles = world_triangles(meshes, instances);
    std::vector<TracedRay> rays;
    rays.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        rays.push_back(random_ray(random, triangles, (i % 4) != 3));
        rays.back().cull_mask = core::at(kCullMasks, static_cast<std::size_t>(i) % kCullMasks.size());
    }
    std::vector<TracedHit> hits;
    *traced = render::vulkan::trace_rays_now(rays, &hits) == render::vulkan::TraceResult::kTraced &&
              hits.size() == rays.size();
    Comparison found;
    if (!*traced) {
        return found;
    }
    for (std::size_t i = 0; i < rays.size(); ++i) {
        const TracedRay& ray = core::at(rays, i);
        const Reference reference = trace_reference(triangles, ray);
        ++found.rays;
        if (reference.unclear) {
            ++found.unclear;
            continue;
        }
        const TracedHit& hit = core::at(hits, i);
        found.hits += reference.hit ? 1 : 0;
        bool same = hit.hit == reference.hit;
        if (same && reference.hit) {
            const double slack = kDistanceSlack + (kDistanceRelative * reference.t);
            same = std::abs(static_cast<double>(hit.t) - reference.t) <= slack && hit.instance == reference.instance &&
                   hit.primitive == reference.primitive && hit.custom_index == reference.material &&
                   std::abs(static_cast<double>(std::get<0>(hit.barycentrics)) - reference.u) <= kBarycentricSlack &&
                   std::abs(static_cast<double>(std::get<1>(hit.barycentrics)) - reference.v) <= kBarycentricSlack;
        }
        if (!same) {
            ++found.wrong;
            if (found.first_wrong.empty()) {
                found.first_wrong = text::format(
                    "%s: device %s t %.5f instance %u primitive %u material %u bary %.4f %.4f; reference %s t %.5f "
                    "instance %u primitive %u material %u bary %.4f %.4f",
                    ray_text(ray).c_str(), hit.hit ? "hit" : "miss", hit.t, hit.instance, hit.primitive,
                    hit.custom_index, std::get<0>(hit.barycentrics), std::get<1>(hit.barycentrics), reference.hit ? "hit" : "miss",
                    reference.t, reference.instance, reference.primitive, reference.material, reference.u,
                    reference.v);
            }
        }
    }
    return found;
}

void check_rays(Tally* tally, const std::string& what, const std::vector<TestMesh>& meshes,
                const std::vector<SceneInstance>& instances, int count, std::mt19937& random)
{
    bool traced = false;
    const Comparison found = compare_rays(meshes, instances, count, random, &traced);
    const int called = found.rays - found.unclear;
    const bool ok = traced && found.wrong == 0 && called > 0 &&
                    static_cast<double>(found.unclear) <= kMostLeftOut * static_cast<double>(found.rays);
    tally->check(ok, text::format("%s: %d rays, %d compared (%d hits), %d too close to call, %d differ%s%s",
                                  what.c_str(), found.rays, called, found.hits, found.unclear, found.wrong,
                                  traced ? "" : " (NOT TRACED)",
                                  found.first_wrong.empty() ? "" : ("; first: " + found.first_wrong).c_str()));
}

// The frame record from the instances (instances of unregistered meshes keep their places: they hit nothing).
void draw_frame(render::Gpu& gpu, const std::vector<TestMesh>& meshes, const std::vector<SceneInstance>& instances,
                uint64_t frame, bool present)
{
    rt::FrameRecord record;
    record.frame = frame;
    for (const SceneInstance& each : instances) {
        rt::Instance instance = each.instance;
        instance.mesh = each.mesh < meshes.size() ? core::at(meshes, each.mesh).id : kUnregistered;
        record.instances.push_back(instance);
    }
    if (rt::RayTracing* backend = rt::ray_tracing()) {
        backend->begin_frame(record);
    }
    if (present) {
        gpu.begin_scene();
        gpu.end_scene();
        gpu.present();
    }
}

// --- the checks ------------------------------------------------------------------------------------------------------

// (path tracing, stage 3 - engine/gfx/rt_poses.h) One skinned mesh drawn by two instances, each in its own pose of the
// frame (a pose buffer each, the source's indices and layout - render_pc/rt_record.cpp's per-instance skinning): one a
// moving object (a new transform every frame), the other blended (kTransparent: the path tracer's rays meet it - cull
// mask 0x80 among the rays'); the poses rewritten every frame (refitted), and a pose not asked for released.
void check_poses(render::Gpu& gpu, Tally* tally, std::mt19937& random, uint64_t* frame)
{
    rt::RayTracing* const backend = rt::ray_tracing();
    TestMesh source;
    source.data = grid(kSide, kGridSize, 0.0);
    grid_list(&source.data, kSide);
    source.layout = {.indexed = true, .wide = false, .strip = false, .dynamic = false, .stride = 28,
                     .position_offset = 8};
    const bool made = make_mesh(gpu, &source); // (drawn from, never registered: only its poses are)
    rt::PoseStore poses;
    const std::array<int, 2> owners{}; // (two instances' addresses)
    std::vector<TestMesh> posed(2);
    std::vector<SceneInstance> instances;
    constexpr uint32_t kPoseMaterial = kMaterialBase + 900;
    instances.push_back({.mesh = 0,
                         .instance = {.mesh = 0,
                                      .material = kPoseMaterial,
                                      .flags = rt::kOpaque | rt::kObject | rt::kCastsShadow | rt::kSkinned,
                                      .object_to_world = random_transform(random)}});
    instances.push_back({.mesh = 1,
                         .instance = {.mesh = 0,
                                      .material = kPoseMaterial + 1,
                                      .flags = rt::kTransparent | rt::kObject | rt::kSkinned,
                                      .object_to_world = random_transform(random)}});
    constexpr int kPoseFrames = 6;
    constexpr double kPhaseA = 0.7;
    constexpr double kPhaseB = 3.1; // (the second instance's pose: other heights)
    int wrong = 0;
    bool ids_differ = true;
    bool ids_kept = true;
    const render::vulkan::SceneStats before = render::vulkan::scene_stats();
    for (int round = 0; made && round < kPoseFrames; ++round) {
        for (std::size_t k = 0; k < posed.size(); ++k) {
            TestMesh& pose = core::at(posed, k);
            pose.layout = source.layout;
            pose.data.corners = source.data.corners;
            pose.data.positions =
                grid(kSide, kGridSize, (k == 0 ? kPhaseA : kPhaseB) + (round * 0.45)).positions;
            const std::vector<uint8_t> bytes = vertex_bytes(pose.data, pose.layout);
            const rt::MeshId id = poses.pose(
                &core::at(owners, k), &source, source.geometry, *frame,
                [&bytes](std::span<uint8_t> out) {
                    std::memcpy(out.data(), bytes.data(), std::min(out.size(), bytes.size()));
                    return out.size() == bytes.size();
                },
                gpu, *backend);
            ids_kept = ids_kept && (round == 0 || id == pose.id);
            pose.id = id;
            pose.registered = id != 0;
        }
        ids_differ = ids_differ && posed.front().id != posed.back().id && posed.front().id != 0;
        poses.end_frame(*frame, gpu, *backend);
        instances.front().instance.object_to_world = random_transform(random); // (the moving object)
        draw_frame(gpu, posed, instances, (*frame)++, true);
        bool traced = false;
        const Comparison found = compare_rays(posed, instances, kRefitRays, random, &traced);
        wrong += traced ? found.wrong : 1;
    }
    const render::vulkan::SceneStats after = render::vulkan::scene_stats();
    tally->check(made && ids_differ && ids_kept && poses.size() == 2 && wrong == 0 && after.refits > before.refits,
                 text::format("one skinned mesh, two instances in their own poses (%zu poses, meshes %u and %u), one "
                              "of them moving and the other blended: %d frames, %llu refits, %d rays differ from the "
                              "poses drawn",
                              poses.size(), posed.front().id, posed.back().id, kPoseFrames,
                              static_cast<unsigned long long>(after.refits - before.refits), wrong));
    check_rays(tally, "the poses' last frame", posed, instances, kDefaultRays, random);
    // the blended instance found by the path tracer's mask (every mask: vulkan_path_reference.cpp PATH_SURFACES)
    {
        const Transform& matrix = instances.back().instance.object_to_world;
        const std::array<float, 3> centre{std::get<3>(std::get<0>(matrix)), std::get<3>(std::get<1>(matrix)),
                                          std::get<3>(std::get<2>(matrix))};
        const std::vector<WorldTriangle> triangles = world_triangles(posed, instances);
        int found = 0;
        int asked = 0;
        for (int i = 0; i < kRefitRays; ++i) {
            TracedRay ray = random_ray(random, triangles, true);
            ray.cull_mask = kMaskNever;
            const Reference reference = trace_reference(triangles, ray);
            if (reference.unclear || !reference.hit || reference.instance != 1) {
                continue;
            }
            std::vector<TracedHit> hits;
            ++asked;
            if (render::vulkan::trace_rays_now(std::span(&ray, 1), &hits) == render::vulkan::TraceResult::kTraced &&
                hits.size() == 1 && hits.front().hit && hits.front().instance == 1) {
                ++found;
            }
        }
        tally->check(asked > 0 && found == asked,
                     text::format("the blended pose met by the seen-through mask (0x80): %d of %d rays toward it near "
                                  "(%.1f %.1f %.1f)",
                                  found, asked, std::get<0>(centre), std::get<1>(centre), std::get<2>(centre)));
    }
    // a pose not asked for in a frame (its instance gone) released: its mesh removed
    {
        const uint32_t meshes_before = render::vulkan::scene_stats().meshes;
        TestMesh& kept = posed.front();
        const std::vector<uint8_t> bytes = vertex_bytes(kept.data, kept.layout);
        poses.pose(
            &owners.front(), &source, source.geometry, *frame,
            [&bytes](std::span<uint8_t> out) {
                std::memcpy(out.data(), bytes.data(), std::min(out.size(), bytes.size()));
                return true;
            },
            gpu, *backend);
        poses.end_frame(*frame, gpu, *backend);
        posed.back().registered = false;
        draw_frame(gpu, posed, instances, (*frame)++, true);
        const uint32_t meshes_after = render::vulkan::scene_stats().meshes;
        tally->check(poses.size() == 1 && meshes_after + 1 == meshes_before,
                     text::format("a pose not asked for released: %zu left, %u meshes registered (%u before)",
                                  poses.size(), meshes_after, meshes_before));
        check_rays(tally, "the one pose left", posed, instances, kRefitRays, random);
    }
    poses.release_all(gpu, backend);
    release_mesh(gpu, &source);
}

// (round GF) A buffer's device address and what the crash report says holds it (vulkan_memory.h describe_address).
VkDeviceAddress current_address(render::GpuVertexBuffer* handle)
{
    namespace vk = render::vulkan;
    vk::Buffer* const buffer = vk::object(handle);
    const VkBufferDeviceAddressInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                                         .pNext = nullptr,
                                         .buffer = core::at(buffer->versions, buffer->current).memory.buffer};
    return vkGetBufferDeviceAddress(vk::backend().context.device(), &info);
}
std::string held_by(VkDeviceAddress address) { return render::vulkan::backend().memory.describe_address(address); }

// (round GF - the user's lost device, Boston) What a frame's ray tracing scene names by address - the TLAS's BLASes, the
// geometry table's vertex / index / light-stream buffers - lives until the frame is over: a mesh removed and its buffers
// released after the frame's scene was built, then a read-back finishing the frame part-way (relit's hurried trace, any
// read-back), left them destroyed under the frame's later passes, which still read them by address (a page fault). And a
// dynamic buffer's memory the frame's geometry table names is not handed out again by a discard in the same frame; and
// a buffer released before its mesh is forgotten by the scene at once (not followed through a buffer made later at its
// address).
void check_frame_lifetime(render::Gpu& gpu, Tally* tally, std::mt19937& random, uint64_t* frame)
{
    rt::RayTracing* const backend = rt::ray_tracing();
    const std::array<TracedRay, 1> one{{{.origin = {0, 0, 0}, .direction = {0, 0, 1}, .t_min = 0, .t_max = 1}}};
    std::vector<TracedHit> hits;
    const auto make = [&](bool dynamic) {
        TestMesh mesh;
        mesh.data = grid(kSide, kGridSize, 0.5);
        grid_list(&mesh.data, kSide);
        mesh.layout = {.indexed = true, .wide = false, .strip = false, .dynamic = dynamic, .stride = 24};
        if (make_mesh(gpu, &mesh)) {
            mesh.id = rt::next_mesh_id();
            backend->add_mesh(mesh.id, mesh.geometry);
            mesh.registered = true;
        }
        return mesh;
    };
    const auto instance_of = [&random](std::size_t mesh) {
        return SceneInstance{.mesh = mesh,
                             .instance = {.mesh = 0,
                                          .material = kMaterialBase + 950,
                                          .flags = rt::kOpaque | rt::kObject | rt::kCastsShadow,
                                          .object_to_world = random_transform(random)}};
    };
    // 1. removed and released after the frame's scene was built, then a read-back mid-frame
    {
        std::vector<TestMesh> meshes{make(false)};
        const std::vector<SceneInstance> instances{instance_of(0)};
        draw_frame(gpu, meshes, instances, (*frame)++, true);
        draw_frame(gpu, meshes, instances, (*frame)++, false); // (this frame's scene names the mesh's buffers)
        const VkDeviceAddress address = current_address(meshes.front().vertices);
        backend->remove_mesh(meshes.front().id);
        release_mesh(gpu, &meshes.front());
        render::vulkan::trace_rays_now(one, &hits); // (the frame finished part-way: a read-back)
        const std::string during = held_by(address);
        gpu.begin_scene(); // (the frame goes on - its passes still read the geometry table)
        gpu.end_scene();
        gpu.present();
        std::string after;
        for (uint32_t i = 0; i < render::vulkan::Frames::kSlots + 1; ++i) {
            draw_frame(gpu, {}, {}, (*frame)++, true);
        }
        after = held_by(address);
        text::print("    the released vertex buffer, after the read-back: %s\n    ... once its frame was over: %s\n",
                    during.c_str(), after.c_str());
        tally->check(address != 0 && during.starts_with("in a vertex buffer") && after.starts_with("in a buffer FREED"),
                     "a mesh's buffer released after its frame's scene was built outlives a read-back that finishes "
                     "the frame part-way, and is destroyed once the frame is over (the report then names it freed)");
    }
    // 2. a dynamic buffer's memory named by the frame is not handed out again in it
    {
        std::vector<TestMesh> meshes{make(true)};
        const std::vector<SceneInstance> instances{instance_of(0)};
        draw_frame(gpu, meshes, instances, (*frame)++, true);
        draw_frame(gpu, meshes, instances, (*frame)++, false);
        const VkDeviceAddress named = current_address(meshes.front().vertices);
        render::vulkan::trace_rays_now(one, &hits);
        const std::vector<uint8_t> bytes = vertex_bytes(meshes.front().data, meshes.front().layout);
        const bool filled = fill_vertices(gpu, meshes.front().vertices, bytes, true); // (a discard)
        const VkDeviceAddress written = current_address(meshes.front().vertices);
        gpu.begin_scene();
        gpu.end_scene();
        gpu.present();
        tally->check(filled && named != 0 && written != named,
                     "a discard after a read-back in the same frame writes other memory than the one the frame's "
                     "geometry table names");
        backend->remove_mesh(meshes.front().id);
        release_mesh(gpu, &meshes.front());
    }
    // 3. a buffer released before its mesh: the scene forgets the mesh at once
    {
        std::vector<TestMesh> meshes{make(false)};
        const std::vector<SceneInstance> instances{instance_of(0)};
        draw_frame(gpu, meshes, instances, (*frame)++, true);
        const uint32_t refused_before = render::vulkan::scene_stats().refused;
        release_mesh(gpu, &meshes.front()); // (its owner forgot to remove it first)
        const uint32_t refused_after = render::vulkan::scene_stats().refused;
        tally->check(refused_after == refused_before + 1,
                     text::format("a mesh whose buffers are released before it is left out at once (%u -> %u refused)",
                                  refused_before, refused_after));
        backend->remove_mesh(meshes.front().id);
        draw_frame(gpu, {}, {}, (*frame)++, true);
    }
}

// (round PT-B - the surfaces' classes, engine/gfx/rt_surface_class.h) Whether a level's texels are all drawn opaque
// (vulkan_formats.h level_alpha_opaque), on hand-made levels of each format the game's textures use.
void check_texel_alpha(Tally* tally)
{
    namespace vk = render::vulkan;
    using Bytes = std::vector<uint8_t>;
    const vk::Extent four{.width = 4, .height = 4};
    const auto opaque = [](uint32_t format, vk::Extent extent, const Bytes& bytes) {
        const std::optional<bool> got = vk::level_alpha_opaque(format, extent, bytes);
        return got.has_value() && *got;
    };
    const auto see_through = [](uint32_t format, vk::Extent extent, const Bytes& bytes) {
        const std::optional<bool> got = vk::level_alpha_opaque(format, extent, bytes);
        return got.has_value() && !*got;
    };
    constexpr uint8_t kFull = 0xFF;
    constexpr std::size_t kTexels = 16;
    constexpr std::size_t kArgbBytes = 4;
    Bytes argb(kTexels * kArgbBytes, kFull);
    bool ok = opaque(render::gpu_value::kFormatA8R8G8B8, four, argb);
    core::at(argb, (5 * kArgbBytes) + 3) = 0xFE; // (one texel's alpha 254)
    ok = ok && see_through(render::gpu_value::kFormatA8R8G8B8, four, argb);
    tally->check(ok, "A8R8G8B8: every alpha 255 opaque; one 254 not");
    // DXT1: four-colour mode (colour0 > colour1) has no transparent index; three-colour mode's index 3 is transparent
    const Bytes dxt1_four{0x00, 0xF8, 0x1F, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    const Bytes dxt1_three_hole{0x1F, 0x00, 0x00, 0xF8, 0x00, 0x00, 0x00, 0xC0}; // (the last texel index 3)
    const Bytes dxt1_three_solid{0x1F, 0x00, 0x00, 0xF8, 0x55, 0x55, 0x55, 0x55}; // (every index 1)
    tally->check(opaque(render::gpu_value::kFormatDxt1, four, dxt1_four) &&
                     see_through(render::gpu_value::kFormatDxt1, four, dxt1_three_hole) &&
                     opaque(render::gpu_value::kFormatDxt1, four, dxt1_three_solid) &&
                     opaque(render::gpu_value::kFormatDxt1, {.width = 2, .height = 2}, dxt1_three_hole),
                 "DXT1: four-colour blocks opaque; a three-colour block with index 3 not, without it opaque; index 3 "
                 "outside a 2x2 level's texels doesn't count");
    // DXT3: sixteen 4-bit alphas, then a colour block
    Bytes dxt3(16, kFull);
    bool dxt3_ok = opaque(render::gpu_value::kFormatDxt3, four, dxt3);
    core::at(dxt3, 3) = 0xEF; // (texel 7's alpha 14)
    dxt3_ok = dxt3_ok && see_through(render::gpu_value::kFormatDxt3, four, dxt3);
    tally->check(dxt3_ok, "DXT3: every alpha 15 opaque; one 14 not");
    // DXT5: the two alphas, 48 bits of 3-bit indices, then a colour block
    const auto dxt5 = [](uint8_t alpha0, uint8_t alpha1, uint8_t every_index) {
        Bytes block(16, 0);
        core::at(block, 0) = alpha0;
        core::at(block, 1) = alpha1;
        uint64_t indices = 0;
        for (uint32_t texel = 0; texel < kTexels; ++texel) {
            indices |= uint64_t{every_index} << (3U * texel);
        }
        for (std::size_t k = 0; k < 6; ++k) {
            core::at(block, 2 + k) = static_cast<uint8_t>(indices >> (8U * k));
        }
        return block;
    };
    constexpr uint8_t kSeven = 7;
    constexpr uint8_t kSix = 6;
    constexpr uint8_t kHalfAlpha = 0x80;
    tally->check(opaque(render::gpu_value::kFormatDxt5, four, dxt5(kFull, kFull, 3)) &&
                     opaque(render::gpu_value::kFormatDxt5, four, dxt5(kFull, 0, 0)) &&
                     see_through(render::gpu_value::kFormatDxt5, four, dxt5(kFull, 0, 1)) &&
                     see_through(render::gpu_value::kFormatDxt5, four, dxt5(kFull, kHalfAlpha, 2)) &&
                     opaque(render::gpu_value::kFormatDxt5, four, dxt5(0, kHalfAlpha, kSeven)) &&
                     see_through(render::gpu_value::kFormatDxt5, four, dxt5(0, kHalfAlpha, kSix)),
                 "DXT5: 255 / 255 opaque at any index; index 0 of 255 opaque, index 1 of 0 not, an interpolation "
                 "below 255 not; the six-step mode's index 7 (255) opaque, 6 (0) not");
    const Bytes words(kTexels * 2, 0);
    tally->check(opaque(render::gpu_value::kFormatR5G6B5, four, words) &&
                     see_through(render::gpu_value::kFormatA1R5G5B5, four, words) &&
                     !vk::level_alpha_opaque(render::gpu_value::kFormatD16, four, words).has_value() &&
                     !vk::level_alpha_opaque(render::gpu_value::kFormatA8R8G8B8, four, words).has_value(),
                 "R5G6B5 opaque, A1R5G5B5 with its alpha bits clear not; a depth format or too few bytes: not known");
}

// --- (round OM) opacity micromaps ------------------------------------------------------------------------------------

constexpr uint32_t kMicroTexture = 64;      // the cut-out's texture's side
constexpr uint32_t kMicroGrid = 4;          // the mesh's quads a side
constexpr float kMicroSize = 16.0F;         // its side in units
constexpr float kMicroUvSpan = 2.0F;        // its UVs over the side (the texture wraps twice)
constexpr uint32_t kMicroStride = 24;       // position, colour, UV
constexpr uint32_t kMicroColourOffset = 12;
constexpr uint32_t kMicroUvOffset = 16;
constexpr int kMicroRays = 8192;
constexpr float kMicroAbove = 10.0F;        // the rays start this far above the mesh, straight down
constexpr float kMicroApart = 100.0F;       // the instances' spacing
constexpr std::array<uint32_t, 3> kMicroVertexAlphas{0x80, 0xFF, 0x40}; // (the game's usual 128, and others)

// The texture's alpha: a hard-edged disc with a soft ring round it, and a hard checker in one corner - parts wholly
// drawn, wholly empty and partly drawn, with edges at every angle.
uint8_t micro_alpha(uint32_t x, uint32_t y, bool inverted)
{
    constexpr uint32_t kChecker = 16;
    constexpr uint32_t kCell = 4;
    constexpr double kInner = 0.2;
    constexpr double kOuter = 0.35;
    uint8_t alpha = 0;
    if (x < kChecker && y < kChecker) {
        alpha = (((x / kCell) + (y / kCell)) & 1U) != 0 ? 255 : 0;
    } else {
        const double u = (x + 0.5) / kMicroTexture - 0.5;
        const double v = (y + 0.5) / kMicroTexture - 0.5;
        const double d = std::sqrt((u * u) + (v * v));
        const double ramp = std::clamp((kOuter - d) / (kOuter - kInner), 0.0, 1.0);
        alpha = static_cast<uint8_t>(std::lround(ramp * 255.0));
    }
    return inverted ? static_cast<uint8_t>(255 - alpha) : alpha;
}

bool fill_micro_texture(render::Gpu& gpu, render::GpuTexture* texture, bool inverted)
{
    render::GpuLockedRect locked;
    if (gpu.lock_texture(texture, 0, &locked, nullptr, 0) != value::kResultOk) {
        return false;
    }
    const auto pitch = static_cast<std::size_t>(locked.pitch);
    const std::span<uint8_t> into(static_cast<uint8_t*>(locked.bits), pitch * kMicroTexture);
    for (uint32_t y = 0; y < kMicroTexture; ++y) {
        for (uint32_t x = 0; x < kMicroTexture; ++x) {
            const uint32_t texel = (uint32_t{micro_alpha(x, y, inverted)} << 24U) | 0x00FFFFFFU;
            std::memcpy(into.subspan((y * pitch) + (x * sizeof(uint32_t)), sizeof(uint32_t)).data(), &texel,
                        sizeof(texel));
        }
    }
    return gpu.unlock_texture(texture, 0) == value::kResultOk;
}

// A flat grid in y = 0 with UVs and vertex colours (the game's FVF 0x142), an indexed list.
struct MicroMesh {
    rt::MeshId id = 0;
    render::GpuVertexBuffer* vertices = nullptr;
    render::GpuIndexBuffer* indices = nullptr;
    std::vector<Position> positions;
    std::vector<std::array<uint32_t, 3>> triangles;
};

bool make_micro_mesh(render::Gpu& gpu, MicroMesh* mesh)
{
    const uint32_t side = kMicroGrid + 1;
    std::vector<uint8_t> bytes(std::size_t{side} * side * kMicroStride);
    for (uint32_t z = 0; z < side; ++z) {
        for (uint32_t x = 0; x < side; ++x) {
            const uint32_t i = (z * side) + x;
            const float fx = static_cast<float>(x) / static_cast<float>(kMicroGrid);
            const float fz = static_cast<float>(z) / static_cast<float>(kMicroGrid);
            const Position at{fx * kMicroSize, 0.0F, fz * kMicroSize};
            mesh->positions.push_back(at);
            const std::size_t base = std::size_t{i} * kMicroStride;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                put_float(bytes, base + (axis * sizeof(float)), core::at(at, axis));
            }
            const uint32_t colour = (core::at(kMicroVertexAlphas, i % kMicroVertexAlphas.size()) << 24U) | 0x808080U;
            std::memcpy(std::span(bytes).subspan(base + kMicroColourOffset, sizeof(colour)).data(), &colour,
                        sizeof(colour));
            put_float(bytes, base + kMicroUvOffset, fx * kMicroUvSpan);
            put_float(bytes, base + kMicroUvOffset + sizeof(float), fz * kMicroUvSpan);
        }
    }
    std::vector<uint16_t> corners;
    for (uint32_t z = 0; z < kMicroGrid; ++z) {
        for (uint32_t x = 0; x < kMicroGrid; ++x) {
            const auto a = static_cast<uint16_t>((z * side) + x);
            const auto b = static_cast<uint16_t>(a + 1);
            const auto c = static_cast<uint16_t>(a + side);
            const auto d = static_cast<uint16_t>(c + 1);
            for (const std::array<uint16_t, 3>& t : {std::array<uint16_t, 3>{a, c, b}, std::array<uint16_t, 3>{b, c, d}}) {
                corners.insert(corners.end(), t.begin(), t.end());
                mesh->triangles.push_back({t[0], t[1], t[2]});
            }
        }
    }
    if (gpu.create_vertex_buffer(static_cast<uint32_t>(bytes.size()), 0, kFvf, value::kPoolDefault, &mesh->vertices) !=
            value::kResultOk ||
        !fill_vertices(gpu, mesh->vertices, bytes, false)) {
        return false;
    }
    void* data = nullptr;
    const auto index_bytes = static_cast<uint32_t>(corners.size() * sizeof(uint16_t));
    if (gpu.create_index_buffer(index_bytes, 0, value::kFormatIndex16, value::kPoolDefault, &mesh->indices) !=
            value::kResultOk ||
        gpu.lock_index_buffer(mesh->indices, 0, 0, &data, 0) != value::kResultOk) {
        return false;
    }
    std::memcpy(data, corners.data(), index_bytes);
    if (gpu.unlock_index_buffer(mesh->indices) != value::kResultOk) {
        return false;
    }
    mesh->id = rt::next_mesh_id();
    rt::ray_tracing()->add_mesh(mesh->id, {.vertices = mesh->vertices,
                                           .vertex_offset = 0,
                                           .vertex_stride = kMicroStride,
                                           .vertex_count = side * side,
                                           .position_offset = 0,
                                           .indices = mesh->indices,
                                           .index_offset = 0,
                                           .index_count = static_cast<uint32_t>(corners.size()),
                                           .indices_32bit = false,
                                           .strip = false,
                                           .dynamic = false,
                                           .uv_offset = kMicroUvOffset,
                                           .uv_format = rt::kUvFloat2,
                                           .colour_offset = kMicroColourOffset,
                                           .colour_format = rt::kColourA8R8G8B8,
                                           .uv_sets = 1});
    return true;
}

// A point of a triangle of the mesh (barycentrics u - corner 1's weight -, v - corner 2's) at an instance `apart` units
// along x and `row` along z.
std::array<float, 3> micro_point(const MicroMesh& mesh, std::size_t triangle, double u, double v, float apart, float row)
{
    const std::array<uint32_t, 3>& t = core::at(mesh.triangles, triangle);
    std::array<float, 3> at{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double value = ((1.0 - u - v) * core::at(core::at(mesh.positions, t[0]), axis)) +
                             (u * core::at(core::at(mesh.positions, t[1]), axis)) +
                             (v * core::at(core::at(mesh.positions, t[2]), axis));
        core::at(at, axis) = static_cast<float>(value);
    }
    at[0] += apart;
    at[1] += kMicroAbove;
    at[2] += row;
    return at;
}

TracedRay micro_ray(const std::array<float, 3>& from, uint32_t mask, bool cutouts)
{
    return {.origin = from,
            .direction = {0.0F, -1.0F, 0.0F},
            .t_min = 0.0F,
            .t_max = 2.0F * kMicroAbove,
            .cull_mask = mask,
            .cutouts = cutouts};
}

// The micro-triangle `index`'s corners at `level` (the bake's grid: barycentrics u, v) - found by the specification's
// curve at each grid cell's centre.
std::vector<std::array<std::array<double, 2>, 3>> micro_cells(uint32_t level)
{
    const uint32_t side = 1U << level;
    std::vector<std::array<std::array<double, 2>, 3>> cells(std::size_t{side} * side);
    const double step = 1.0 / side;
    for (uint32_t a = 0; a < side; ++a) {
        for (uint32_t b = 0; b < side; ++b) {
            std::array<std::array<double, 2>, 3> c{};
            if (a + b <= side - 1) {
                c = {{{a * step, b * step}, {(a + 1) * step, b * step}, {a * step, (b + 1) * step}}};
            } else {
                const uint32_t i = side - 1 - a;
                const uint32_t j = side - 1 - b;
                c = {{{(i + 1) * step, j * step}, {i * step, (j + 1) * step}, {(i + 1) * step, (j + 1) * step}}};
            }
            const double cu = (c[0][0] + c[1][0] + c[2][0]) / 3.0;
            const double cv = (c[0][1] + c[1][1] + c[2][1]) / 3.0;
            const uint32_t index =
                render::vulkan::micromap_index(static_cast<float>(cu), static_cast<float>(cv), level);
            if (index < cells.size()) {
                core::at(cells, index) = c;
            }
        }
    }
    return cells;
}

struct MicroStates {
    std::array<uint64_t, 4> counts{}; // transparent, opaque, unknown x 2
    int wrong = 0;
    std::string first_wrong;
};

// Each micro-triangle the micromap of `mesh` (baked for the instance at `owner`) calls transparent or opaque, tried at
// the other instance (`other`: the same mesh and an equal material - traced without the micromap, by the shader's test)
// at three points inside it: an opaque one must be kept there, a transparent one must not. `seen`: the seen-through
// instances' kind - kept is no light through (share 0), not kept all of it (share 1); else a cut-out's hit.
MicroStates check_micro_states(const MicroMesh& mesh, const render::vulkan::MicromapView& view, float other, float row,
                               bool seen)
{
    MicroStates found;
    const std::vector<std::array<std::array<double, 2>, 3>> cells = micro_cells(view.level);
    const std::size_t per = cells.size();
    std::vector<TracedRay> rays;
    std::vector<uint32_t> expected; // (the state each ray's micro-triangle has)
    for (std::size_t triangle = 0; triangle < view.triangles && triangle < mesh.triangles.size(); ++triangle) {
        for (std::size_t index = 0; index < per; ++index) {
            const std::size_t bit = ((triangle * per) + index) * 2;
            const uint32_t state = (core::at(view.words, bit / 32) >> (bit % 32)) & 3U;
            ++core::at(found.counts, state);
            if (state >= 2) {
                continue;
            }
            const std::array<std::array<double, 2>, 3>& c = core::at(cells, index);
            const double cu = (c[0][0] + c[1][0] + c[2][0]) / 3.0;
            const double cv = (c[0][1] + c[1][1] + c[2][1]) / 3.0;
            for (std::size_t k = 0; k < 3; ++k) {
                // (the centre, and two thirds of the way to each corner but the last)
                const double u = k == 0 ? cu : (cu * 0.4) + (c[k][0] * 0.6);
                const double v = k == 0 ? cv : (cv * 0.4) + (c[k][1] * 0.6);
                rays.push_back(micro_ray(micro_point(mesh, triangle, u, v, other, row),
                                         seen ? kMaskWorld : kMaskTested, true));
                expected.push_back(state);
            }
        }
    }
    std::vector<TracedHit> hits;
    if (render::vulkan::trace_rays_now(rays, &hits) != render::vulkan::TraceResult::kTraced || hits.size() != rays.size()) {
        found.wrong = -1;
        return found;
    }
    for (std::size_t i = 0; i < rays.size(); ++i) {
        const TracedHit& hit = core::at(hits, i);
        const bool kept = seen ? hit.seen_through <= 0.0F : hit.hit;
        const bool cleared = seen ? hit.seen_through >= 1.0F : !hit.hit;
        const bool ok = core::at(expected, i) == 1 ? kept : cleared;
        if (!ok) {
            ++found.wrong;
            if (found.first_wrong.empty()) {
                const TracedRay& ray = core::at(rays, i);
                found.first_wrong = text::format("%s micro-triangle at (%.3f %.3f): %s, share %.3f",
                                                 core::at(expected, i) == 1 ? "an opaque" : "a transparent",
                                                 std::get<0>(ray.origin), std::get<2>(ray.origin),
                                                 hit.hit ? "hit" : "no hit", static_cast<double>(hit.seen_through));
            }
        }
    }
    return found;
}

// The same random rays at the instance with the micromap, then at the same instance drawn with the equal material of
// another id (traced without it - DISABLE_OPACITY_MICROMAPS): the same answers, exactly - the same rays, the same
// transform.
struct MicroPairs {
    int differ = 0;
    int hits = 0;
    int misses = 0;
    int covered = 0; // (seen through: share 0)
    int clear = 0;   // (share 1)
    std::string first;
};

MicroPairs compare_micro_pairs(const MicroMesh& mesh, float owner, float row, bool seen, std::mt19937& random,
                               const std::function<void(bool)>& with_micromap)
{
    MicroPairs found;
    std::uniform_int_distribution<std::size_t> pick(0, mesh.triangles.size() - 1);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::vector<TracedRay> once;
    for (int i = 0; i < kMicroRays; ++i) {
        const std::size_t triangle = pick(random);
        double u = unit(random);
        double v = unit(random);
        if (u + v > 1.0) {
            u = 1.0 - u;
            v = 1.0 - v;
        }
        once.push_back(micro_ray(micro_point(mesh, triangle, u, v, owner, row), seen ? kMaskWorld : kMaskTested, true));
    }
    std::vector<TracedHit> with_hits;
    std::vector<TracedHit> without_hits;
    const bool traced = render::vulkan::trace_rays_now(once, &with_hits) == render::vulkan::TraceResult::kTraced;
    with_micromap(false);
    const bool traced_without = render::vulkan::trace_rays_now(once, &without_hits) == render::vulkan::TraceResult::kTraced;
    with_micromap(true);
    if (!traced || !traced_without || with_hits.size() != once.size() || without_hits.size() != once.size()) {
        found.differ = -1;
        return found;
    }
    std::vector<TracedRay> rays;
    std::vector<TracedHit> hits;
    for (std::size_t i = 0; i < once.size(); ++i) {
        rays.push_back(core::at(once, i));
        rays.push_back(core::at(once, i));
        hits.push_back(core::at(with_hits, i));
        hits.push_back(core::at(without_hits, i));
    }
    for (std::size_t i = 0; i + 1 < rays.size(); i += 2) {
        const TracedHit& with = core::at(hits, i);
        const TracedHit& without = core::at(hits, i + 1);
        bool same = with.hit == without.hit && with.seen_through == without.seen_through;
        if (same && with.hit) {
            same = with.t == without.t && with.primitive == without.primitive;
        }
        found.hits += with.hit ? 1 : 0;
        found.misses += with.hit ? 0 : 1;
        found.covered += with.seen_through <= 0.0F ? 1 : 0;
        found.clear += with.seen_through >= 1.0F ? 1 : 0;
        if (!same) {
            ++found.differ;
            if (found.first.empty()) {
                found.first = text::format("at (%.3f %.3f): with the micromap %s t %.4f share %.4f, without %s t %.4f "
                                           "share %.4f",
                                           std::get<0>(core::at(rays, i).origin), std::get<2>(core::at(rays, i).origin),
                                           with.hit ? "hit" : "miss", with.t, static_cast<double>(with.seen_through),
                                           without.hit ? "hit" : "miss", without.t,
                                           static_cast<double>(without.seen_through));
            }
        }
    }
    return found;
}

void micro_frame(render::Gpu& gpu, const std::vector<rt::Instance>& instances, uint64_t* frame)
{
    rt::FrameRecord record;
    record.frame = (*frame)++;
    record.instances = instances;
    rt::ray_tracing()->begin_frame(record);
    gpu.begin_scene();
    gpu.end_scene();
    gpu.present();
}

rt::Instance micro_instance(rt::MeshId mesh, rt::MaterialId material, uint32_t flags, float x, float z)
{
    rt::Instance made;
    made.mesh = mesh;
    made.material = material;
    made.flags = flags;
    made.object_to_world = {{{1.0F, 0.0F, 0.0F, x}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, z}}};
    return made;
}

// (round OM - gpu_vulkan/vulkan_rt_micromaps.h) Opacity micromaps: a cut-out (alpha tested at 0.5) and a seen-through
// overlay (blend 5, tested at 1/255) on a grid with a disc / ring / checker alpha and three vertex alphas, each mesh drawn
// twice - with the material that owns its micromap, and with an equal material of another id (traced without it, the
// shader's test as before). The micromaps' states against the shader's own test at the other instance (an opaque
// micro-triangle kept at three points of it, a transparent one rejected), the same random rays at both instances (the
// same hits, distances and shares), and both again after the material's alpha reference and then its texture changed
// (baked again).
void check_micromaps(render::Gpu& gpu, Tally* tally, std::mt19937& random, uint64_t* frame)
{
    if (!render::vulkan::scene_micromaps_on()) {
        tally->check(true, "opacity micromaps: not on this device (VK_EXT_opacity_micromap) - skipped");
        return;
    }
    rt::RayTracing* const backend = rt::ray_tracing();
    render::GpuTexture* texture = nullptr;
    if (gpu.create_texture({.width = kMicroTexture,
                            .height = kMicroTexture,
                            .levels = 1,
                            .usage = 0,
                            .format = value::kFormatA8R8G8B8,
                            .pool = value::kPoolManaged},
                           &texture) != value::kResultOk ||
        !fill_micro_texture(gpu, texture, false)) {
        tally->check(false, "opacity micromaps: the texture made");
        return;
    }
    MicroMesh cut;
    MicroMesh seen;
    if (!make_micro_mesh(gpu, &cut) || !make_micro_mesh(gpu, &seen)) {
        tally->check(false, "opacity micromaps: the meshes made");
        return;
    }
    constexpr float kHalfRef = 0.5F;
    constexpr uint32_t kBlendAlpha = 5;
    rt::Material cutout;
    cutout.texture = texture;
    cutout.flags = rt::kAlphaTested;
    cutout.alpha_ref = kHalfRef;
    rt::Material overlay;
    overlay.texture = texture;
    overlay.blend = kBlendAlpha;
    overlay.flags = rt::kTransparent | rt::kAlphaTested;
    overlay.alpha_ref = kNoRejectAlphaRef;
    const std::array<rt::MaterialId, 4> ids{rt::next_material_id(), rt::next_material_id(), rt::next_material_id(),
                                            rt::next_material_id()};
    const auto set_materials = [&] {
        backend->set_material(ids[0], cutout);
        backend->set_material(ids[1], cutout);
        backend->set_material(ids[2], overlay);
        backend->set_material(ids[3], overlay);
    };
    set_materials();
    constexpr float kSeenRow = 50.0F;
    const uint32_t cut_flags = rt::kWorld | rt::kAlphaTested | rt::kCastsShadow;
    const uint32_t seen_flags = rt::kWorld | rt::kTransparent | rt::kAlphaTested | rt::kCastsShadow;
    const std::vector<rt::Instance> instances{
        micro_instance(cut.id, ids[0], cut_flags, 0.0F, 0.0F),            // (first: its micromap's)
        micro_instance(cut.id, ids[1], cut_flags, kMicroApart, 0.0F),     // (traced without it)
        micro_instance(seen.id, ids[2], seen_flags, 0.0F, kSeenRow),
        micro_instance(seen.id, ids[3], seen_flags, kMicroApart, kSeenRow),
    };
    const auto round = [&](const char* when) {
        for (int i = 0; i < 3; ++i) {
            micro_frame(gpu, instances, frame);
        }
        for (const bool is_seen : {false, true}) {
            const MicroMesh& mesh = is_seen ? seen : cut;
            const float row = is_seen ? kSeenRow : 0.0F;
            render::vulkan::MicromapView view;
            const bool has = render::vulkan::scene_micromap(mesh.id, &view);
            const auto mode = static_cast<uint32_t>(is_seen ? render::vulkan::MicromapMode::kSeenBlended
                                                            : render::vulkan::MicromapMode::kCutout);
            tally->check(has && view.material == (is_seen ? ids[2] : ids[0]) && view.mode == mode,
                         text::format("%s, %s: a micromap baked for the first instance's material (%u, mode %u), level "
                                      "%u, %u triangles",
                                      when, is_seen ? "the seen-through overlay" : "the cut-out", view.material,
                                      view.mode, view.level, view.triangles));
            if (!has) {
                continue;
            }
            const MicroStates states = check_micro_states(mesh, view, kMicroApart, row, is_seen);
            const bool spread = states.counts[0] > 0 && states.counts[2] + states.counts[3] > 0 &&
                                (is_seen || states.counts[1] > 0);
            tally->check(states.wrong == 0 && spread,
                         text::format("%s: its states as the shader's test answers - transparent %llu, opaque %llu, "
                                      "unknown %llu micro-triangles; %d points of the decided ones wrong%s%s",
                                      when, static_cast<unsigned long long>(states.counts[0]),
                                      static_cast<unsigned long long>(states.counts[1]),
                                      static_cast<unsigned long long>(states.counts[2] + states.counts[3]),
                                      states.wrong, states.first_wrong.empty() ? "" : "; first: ",
                                      states.first_wrong.c_str()));
            const MicroPairs pairs = compare_micro_pairs(mesh, 0.0F, row, is_seen, random, [&](bool with) {
                std::vector<rt::Instance> drawn = instances;
                if (!with) { // (the owners drawn with the equal materials: their meshes' micromaps not taken)
                    core::at(drawn, 0).material = ids[1];
                    core::at(drawn, 2).material = ids[3];
                }
                micro_frame(gpu, drawn, frame);
            });
            tally->check(pairs.differ == 0 && (is_seen ? (pairs.covered > 0 && pairs.clear > 0)
                                                       : (pairs.hits > 0 && pairs.misses > 0)),
                         text::format("%s: %d ray pairs, with and without the micromap: %d differ (%s%d / %d)%s%s",
                                      when, kMicroRays, pairs.differ, is_seen ? "shares 0 / 1: " : "hits / misses: ",
                                      is_seen ? pairs.covered : pairs.hits, is_seen ? pairs.clear : pairs.misses,
                                      pairs.first.empty() ? "" : "; first: ", pairs.first.c_str()));
        }
    };
    const uint64_t bakes_before = render::vulkan::micromap_stats().bakes;
    round("baked");
    // the material changed (its alpha reference): baked again
    constexpr float kQuarterRef = 0.25F;
    cutout.alpha_ref = kQuarterRef;
    set_materials();
    round("the alpha reference changed");
    // the texture's level 0 written again (inverted): baked again
    fill_micro_texture(gpu, texture, true);
    round("the texture written again");
    const uint64_t bakes = render::vulkan::micromap_stats().bakes - bakes_before;
    tally->check(bakes >= 5, text::format("opacity micromaps baked again when their inputs changed: %llu bakes",
                                          static_cast<unsigned long long>(bakes)));
    for (const MicroMesh* mesh : {&cut, &seen}) {
        backend->remove_mesh(mesh->id);
        gpu.release_vertex_buffer(mesh->vertices);
        gpu.release_index_buffer(mesh->indices);
    }
    for (const rt::MaterialId id : ids) {
        backend->remove_material(id);
    }
    micro_frame(gpu, {}, frame);
    gpu.release_texture(texture);
}

void run_checks(render::Gpu& gpu, const Options& options, Tally* tally)
{
    check_texel_alpha(tally); // (round PT-B)
    std::mt19937 random(options.seed);
    rt::RayTracing* const backend = rt::ray_tracing();
    tally->check(backend != nullptr && backend->available(), "the ray tracing backend is installed");
    if (backend == nullptr) {
        return;
    }
    // no frame yet: nothing to trace against
    {
        std::vector<TracedHit> hits;
        const std::array<TracedRay, 1> one{{{.origin = {0, 0, 0}, .direction = {0, 0, 1}, .t_min = 0, .t_max = 1}}};
        tally->check(render::vulkan::trace_rays_now(one, &hits) == render::vulkan::TraceResult::kNoScene,
                     "before the first frame: no scene to trace");
    }

    std::vector<TestMesh> meshes;
    const auto add = [&](MeshData data, const Layout& layout) {
        TestMesh mesh;
        mesh.data = std::move(data);
        mesh.layout = layout;
        meshes.push_back(std::move(mesh));
    };
    // the big meshes: a grid each (its heights' phase), as a list or a strip, in these layouts
    struct GridMesh {
        double phase = 0.0;
        Layout layout;
    };
    constexpr std::array<GridMesh, 6> kGrids{{
        {.phase = 0.0, .layout = {.indexed = true, .wide = false, .strip = false, .dynamic = false, .stride = 24}},
        {.phase = 1.0,
         .layout = {.indexed = true,
                    .wide = true,
                    .strip = false,
                    .dynamic = false,
                    .stride = 32,
                    .position_offset = 12,
                    .leading_vertices = 7,
                    .leading_indices = 11}},
        {.phase = 2.0,
         .layout = {.indexed = true,
                    .wide = false,
                    .strip = true,
                    .dynamic = false,
                    .stride = 20,
                    .position_offset = 8,
                    .leading_vertices = 3,
                    .leading_indices = 5}},
        {.phase = 3.0, .layout = {.indexed = true, .wide = true, .strip = true, .dynamic = false, .stride = 12}},
        {.phase = 4.0,
         .layout = {.indexed = false,
                    .wide = false,
                    .strip = false,
                    .dynamic = false,
                    .stride = 16,
                    .position_offset = 4,
                    .leading_vertices = 2}},
        {.phase = 5.0, .layout = {.indexed = false, .wide = false, .strip = true, .dynamic = false, .stride = 24}},
    }};
    constexpr Layout kWaves{.indexed = true, .wide = false, .strip = false, .dynamic = true, .stride = 28,
                            .position_offset = 16};
    constexpr uint32_t kSmallStride = 24;
    constexpr uint32_t kSmallLeadingVertices = 5; // (the small meshes' junk before them: i % this)
    constexpr uint32_t kSmallLeadingIndices = 7;
    for (const GridMesh& each : kGrids) {
        MeshData data = grid(kSide, kGridSize, each.phase);
        if (each.layout.strip) {
            grid_strip(&data, kSide);
        } else {
            grid_list(&data, kSide);
        }
        add(each.layout.indexed ? data : unindexed(data), each.layout);
    }
    const std::size_t dynamic_mesh = meshes.size();
    {
        MeshData waves = grid(kSide, kGridSize, 0.0);
        grid_list(&waves, kSide);
        add(waves, kWaves);
    }
    const std::size_t small_first = meshes.size();
    for (uint32_t i = 0; i < kSmallMeshes; ++i) {
        add(tetrahedron(random), {.indexed = true,
                                  .wide = (i % 2) != 0,
                                  .strip = false,
                                  .dynamic = false,
                                  .stride = kSmallStride,
                                  .position_offset = 0,
                                  .leading_vertices = i % kSmallLeadingVertices,
                                  .leading_indices = i % kSmallLeadingIndices});
    }
    bool made = true;
    for (TestMesh& mesh : meshes) {
        made = made && make_mesh(gpu, &mesh);
        mesh.id = rt::next_mesh_id();
        backend->add_mesh(mesh.id, mesh.geometry);
        mesh.registered = true;
    }
    tally->check(made, text::format("%zu meshes made through the Gpu and registered (lists, strips, 16 / 32-bit, "
                                    "unindexed, offsets, a dynamic one, %u small)",
                                    meshes.size(), kSmallMeshes));

    // instances: two of each big mesh, one of each small one, two of a mesh never registered
    std::vector<SceneInstance> instances;
    uint32_t material = kMaterialBase;
    // (the flags and materials make every mask: world, objects, alpha-tested - the game's 1/255 reference counting as
    // opaque -, never; a material given with alpha_ref >= 0)
    const auto place = [&](std::size_t mesh, uint32_t flags, float alpha_ref) {
        const uint32_t id = material++;
        if (alpha_ref >= 0.0F) {
            backend->set_material(id, {.alpha_ref = alpha_ref, .flags = rt::kAlphaTested});
        }
        instances.push_back(
            {.mesh = mesh,
             .instance = {.mesh = 0, .material = id, .flags = flags, .object_to_world = random_transform(random)},
             .alpha_ref = alpha_ref});
    };
    constexpr std::size_t kTransparentEvery = 5;
    for (std::size_t i = 0; i < small_first; ++i) {
        place(i, rt::kOpaque | rt::kWorld | rt::kCastsShadow, -1.0F);
        place(i, rt::kAlphaTested | rt::kObject | rt::kCastsShadow, (i % 2) == 0 ? kNoRejectAlphaRef : kRejectingAlphaRef);
    }
    for (std::size_t i = small_first; i < meshes.size(); ++i) {
        const bool transparent = i % kTransparentEvery == 0;
        place(i, transparent ? rt::kTransparent | rt::kObject : rt::kOpaque | rt::kObject | rt::kCastsShadow,
              (i % 3) == 0 ? kNoRejectAlphaRef : -1.0F);
    }
    place(meshes.size(), rt::kOpaque | rt::kCastsShadow, -1.0F);
    place(meshes.size() + 1, rt::kTransparent, -1.0F);
    // (round PT-B) a glow - adds light: its own mask, 0x40
    place(small_first, rt::kTransparent | rt::kAddsLight | rt::kWorld, -1.0F);

    uint64_t frame = 1;
    draw_frame(gpu, meshes, instances, frame++, false);
    render::vulkan::SceneStats stats = render::vulkan::scene_stats();
    tally->check(stats.built == meshes.size() && stats.refused == 0 && stats.last_instances == instances.size(),
                 text::format("every mesh built (%u of %u, %u refused), %u instances in the TLAS", stats.built,
                              stats.meshes, stats.refused, stats.last_instances));
    check_rays(tally, "first frame (built, not yet compacted)", meshes, instances, options.rays, random);

    // the compactions: asked for with the builds, made at the next frame once those have run
    for (int i = 0; i < 3; ++i) { // (the sizes of the builds past the queries asked for a frame later)
        draw_frame(gpu, meshes, instances, frame++, true);
    }
    stats = render::vulkan::scene_stats();
    const uint32_t statics = static_cast<uint32_t>(meshes.size()) - 1;
    tally->check(stats.compacted == statics && stats.blas_bytes < stats.built_bytes,
                 text::format("the static meshes compacted (%u of %u): BLAS %llu KB, %llu KB as built", stats.compacted,
                              statics, static_cast<unsigned long long>(stats.blas_bytes / kKilobyte),
                              static_cast<unsigned long long>(stats.built_bytes / kKilobyte)));
    check_rays(tally, "after compaction", meshes, instances, options.rays, random);

    // the dynamic mesh rewritten (a discard: new memory each time) and refitted, past a rebuild
    {
        TestMesh& waves = core::at(meshes, dynamic_mesh);
        bool rewritten = true;
        const uint64_t refits_before = stats.refits;
        const uint64_t builds_before = stats.builds;
        int compared_wrong = 0;
        for (int round = 1; round <= kRefitRounds; ++round) {
            const MeshData moved = grid(kSide, kGridSize, round * 0.37);
            waves.data.positions = moved.positions;
            rewritten = rewritten && fill_vertices(gpu, waves.vertices, vertex_bytes(waves.data, waves.layout), true);
            backend->mesh_changed(waves.id);
            draw_frame(gpu, meshes, instances, frame++, true);
            if (round % kTraceEvery == 0 || round == kRefitRounds) {
                bool traced = false;
                const Comparison found = compare_rays(meshes, instances, kRefitRays, random, &traced);
                compared_wrong += traced ? found.wrong : 1;
            }
        }
        stats = render::vulkan::scene_stats();
        tally->check(rewritten && compared_wrong == 0 && stats.refits > refits_before && stats.builds > builds_before,
                     text::format("the dynamic mesh rewritten %d times: %llu refits, %llu rebuilds, %d rays differ",
                                  kRefitRounds, static_cast<unsigned long long>(stats.refits - refits_before),
                                  static_cast<unsigned long long>(stats.builds - builds_before), compared_wrong));
        check_rays(tally, "after the refits", meshes, instances, options.rays, random);
    }

    // update_mesh: the dynamic mesh's vertices in another buffer (as a skinned mesh's take turns) - its BLAS kept and
    // refitted -, and a static mesh given again (built again)
    {
        TestMesh& waves = core::at(meshes, dynamic_mesh);
        const uint64_t builds_before = render::vulkan::scene_stats().builds;
        waves.data.positions = grid(kSide, kGridSize, kMovedPhase).positions;
        const std::vector<uint8_t> moved = vertex_bytes(waves.data, waves.layout);
        render::GpuVertexBuffer* other = nullptr;
        const bool made_other = gpu.create_vertex_buffer(static_cast<uint32_t>(moved.size()), kDynamic, kFvf,
                                                         value::kPoolDefault, &other) == value::kResultOk &&
                                fill_vertices(gpu, other, moved, true);
        rt::Geometry geometry = waves.geometry;
        geometry.vertices = other;
        backend->update_mesh(waves.id, geometry);
        backend->mesh_changed(waves.id);
        gpu.release_vertex_buffer(waves.vertices);
        waves.vertices = other;
        waves.geometry = geometry;
        const TestMesh& first = meshes.front();
        backend->update_mesh(first.id, first.geometry);
        draw_frame(gpu, meshes, instances, frame++, true);
        const render::vulkan::SceneStats after = render::vulkan::scene_stats();
        tally->check(made_other && after.builds == builds_before + 1 && after.built == meshes.size(),
                     text::format("update_mesh: the dynamic mesh refitted from another buffer, the static one built "
                                  "again (%llu builds)",
                                  static_cast<unsigned long long>(after.builds - builds_before)));
        check_rays(tally, "after update_mesh", meshes, instances, options.rays / 2, random);
    }

    // (added for the skater's shadow) a skinned mesh's buffers taking turns, as render_pc/rt_record.cpp reports them:
    // each frame a new pose - a large deformation, the grid's heights several units apart - in the other buffer and
    // only update_mesh (no mesh_changed). The structure must follow every pose (refitted from the new buffer): before,
    // update_mesh swapped the buffer and kept the pose the structure was built with.
    {
        TestMesh& waves = core::at(meshes, dynamic_mesh);
        render::GpuVertexBuffer* other = nullptr;
        const auto turn_bytes = static_cast<uint32_t>(vertex_bytes(waves.data, waves.layout).size());
        bool made_other =
            gpu.create_vertex_buffer(turn_bytes, kDynamic, kFvf, value::kPoolDefault, &other) == value::kResultOk;
        const std::array<render::GpuVertexBuffer*, 2> turns{waves.vertices, other};
        const render::vulkan::SceneStats turns_before = render::vulkan::scene_stats();
        int compared_wrong = 0;
        constexpr int kTurnRounds = 8;
        constexpr double kPoseStep = 2.3; // (the phase between poses: every height moved)
        for (int round = 1; made_other && round <= kTurnRounds; ++round) {
            render::GpuVertexBuffer* now = core::at(turns, static_cast<std::size_t>(round % 2));
            waves.data.positions = grid(kSide, kGridSize, kMovedPhase + (round * kPoseStep)).positions;
            made_other = fill_vertices(gpu, now, vertex_bytes(waves.data, waves.layout), true);
            rt::Geometry geometry = waves.geometry;
            geometry.vertices = now;
            backend->update_mesh(waves.id, geometry);
            waves.vertices = now;
            waves.geometry = geometry;
            draw_frame(gpu, meshes, instances, frame++, true);
            bool traced = false;
            const Comparison found = compare_rays(meshes, instances, kRefitRays, random, &traced);
            compared_wrong += traced ? found.wrong : 1;
        }
        const render::vulkan::SceneStats turns_after = render::vulkan::scene_stats();
        const uint64_t refits = turns_after.refits - turns_before.refits;
        const uint64_t builds = turns_after.builds - turns_before.builds;
        tally->check(made_other && compared_wrong == 0 && refits + builds >= kTurnRounds,
                     text::format("a skinned mesh's two buffers taking turns (update_mesh only, %d poses): %llu refits, "
                                  "%llu rebuilds, %d rays differ from the pose drawn",
                                  kTurnRounds, static_cast<unsigned long long>(refits),
                                  static_cast<unsigned long long>(builds), compared_wrong));
        check_rays(tally, "after the buffers took turns", meshes, instances, options.rays / 2, random);
        // (the buffer not in use now let go of; the mesh keeps the one it was last given)
        render::GpuVertexBuffer* idle = waves.vertices == std::get<0>(turns) ? std::get<1>(turns) : std::get<0>(turns);
        if (idle != nullptr) {
            gpu.release_vertex_buffer(idle);
        }
    }

    // a mesh removed (its instances then hit nothing), and added again under a new id
    {
        TestMesh& strip = core::at(meshes, 2);
        backend->remove_mesh(strip.id);
        strip.registered = false;
        draw_frame(gpu, meshes, instances, frame++, true);
        stats = render::vulkan::scene_stats();
        tally->check(stats.meshes == meshes.size() - 1, "a mesh removed: one fewer registered");
        check_rays(tally, "a mesh removed", meshes, instances, options.rays / 2, random);
        strip.id = rt::next_mesh_id();
        backend->add_mesh(strip.id, strip.geometry);
        strip.registered = true;
        draw_frame(gpu, meshes, instances, frame++, true);
        check_rays(tally, "added again under a new id", meshes, instances, options.rays / 2, random);
    }

    // instances moved (new transforms each frame), a TLAS of more instances than before (made again larger)
    {
        for (int copy = 0; copy < 3; ++copy) {
            for (std::size_t i = 0; i < small_first; ++i) {
                place(i, rt::kAlphaTested | rt::kWorld | rt::kCastsShadow, kRejectingAlphaRef);
            }
        }
        for (SceneInstance& each : instances) {
            each.instance.object_to_world = random_transform(random);
        }
        draw_frame(gpu, meshes, instances, frame++, true);
        check_rays(tally, text::format("%zu instances moved", instances.size()), meshes, instances, options.rays,
                   random);
    }

    // an empty scene: every ray misses
    {
        const std::vector<SceneInstance> none;
        draw_frame(gpu, meshes, none, frame++, true);
        bool traced = false;
        const Comparison found = compare_rays(meshes, none, options.rays / 4, random, &traced);
        tally->check(traced && found.wrong == 0 && found.hits == 0,
                     text::format("an empty frame: %d rays, every one a miss on the device too", found.rays));
    }

    // the probe (rtprobe's): a ray straight at the first instance's centre
    {
        draw_frame(gpu, meshes, instances, frame++, true);
        const Transform& matrix = instances.front().instance.object_to_world;
        const std::array<float, 3> centre{std::get<3>(std::get<0>(matrix)), std::get<3>(std::get<1>(matrix)), std::get<3>(std::get<2>(matrix))};
        const std::array<float, 3> from{std::get<0>(centre), std::get<1>(centre) + 30.0F, std::get<2>(centre)};
        const rt::ProbeResult probed = backend->probe({.origin = from, .direction = {0.0F, -2.0F, 0.0F}, .max_distance = kFar});
        std::vector<TracedRay> one{
            {.origin = from, .direction = {0.0F, -1.0F, 0.0F}, .t_min = 0.0F, .t_max = kFar}};
        const Reference reference = trace_reference(world_triangles(meshes, instances), one.front());
        const bool same = probed.traced && probed.hit == reference.hit &&
                          (!reference.hit || (std::abs(probed.distance - reference.t) < kDistanceSlack * 10 &&
                                              probed.instance == reference.instance &&
                                              probed.primitive == reference.primitive &&
                                              probed.mesh == core::at(meshes, core::at(instances, probed.instance).mesh).id));
        tally->check(same || reference.unclear,
                     text::format("the probe: %s at %.3f, instance %u, mesh %u, primitive %u (reference %s at %.3f)",
                                  probed.hit ? "hit" : "miss", probed.distance, probed.instance, probed.mesh,
                                  probed.primitive, reference.hit ? "hit" : "miss", reference.t));
    }

    check_poses(gpu, tally, random, &frame);
    check_micromaps(gpu, tally, random, &frame); // (round OM)
    check_frame_lifetime(gpu, tally, random, &frame); // (round GF)

    // everything removed and released: the meshes' structures destroyed once their frames have run
    for (TestMesh& mesh : meshes) {
        backend->remove_mesh(mesh.id);
        release_mesh(gpu, &mesh);
    }
    for (int i = 0; i < 3; ++i) {
        draw_frame(gpu, meshes, {}, frame++, true);
    }
    std::vector<TracedHit> hits;
    const std::array<TracedRay, 1> one{{{.origin = {0, 0, 0}, .direction = {0, 0, 1}, .t_min = 0, .t_max = 1}}};
    render::vulkan::trace_rays_now(one, &hits); // (the frames' work finished)
    draw_frame(gpu, meshes, {}, frame++, false);
    stats = render::vulkan::scene_stats();
    tally->check(stats.meshes == 0 && stats.blas_bytes == 0,
                 text::format("every mesh removed: %u left, BLAS %llu bytes, %u retired awaiting their frames",
                              stats.meshes, static_cast<unsigned long long>(stats.blas_bytes), stats.retired_pending));
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
        position = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(core::at(listed, chosen - 1)));
    }
    SDL_free(ids);
    return position;
}

int run(const Options& options, SDL_Window* window)
{
    render::vulkan::set_context_options({.validation = options.validation,
                                         .device_index = options.device,
                                         .on_message = print_line,
                                         .ray_tracing = true},
                                        window);
    render::GpuContext& context = render::vulkan::gpu_context();
    render::Gpu& gpu = render::vulkan::gpu();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return render::vulkan::loader_version() == 0 ? 2 : 1;
    }
    const render::vulkan::Backend& state = render::vulkan::backend();
    const bool layer_on = state.context.validation_on();
    text::print("validation layer: %s\n", layer_on ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWindowWidth,
                       .height = kWindowHeight,
                       .windowed = true,
                       .vsync = false,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24X8});
    const long made = context.create_device(
        {.width = kWindowWidth, .height = kWindowHeight, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
    if (made != value::kResultOk) {
        text::print("create_device failed (%ld)\n", made);
        context.shutdown();
        return 1;
    }
    context.mark_ready();
    text::print("device: %s\n", context.adapter().description.data());
    if (!state.context.ray_tracing_enabled()) {
        text::print("this device has no ray tracing (VK_KHR_acceleration_structure + VK_KHR_ray_query)\n");
        context.shutdown();
        return 2;
    }
    const uint32_t baseline = state.memory.statistics().total.statistics.allocationCount;
    Tally tally;
    run_checks(gpu, options, &tally);
    context.shutdown();
    tally.check(rt::ray_tracing() == nullptr && state.memory.statistics().total.statistics.allocationCount == 0,
                text::format("the device shut down: the backend uninstalled, nothing left allocated (%u at the start)",
                             baseline));
    tally.check(validation_messages() == 0,
                text::format("validation messages: %d%s", validation_messages(),
                             layer_on ? "" : " (the layer wasn't on)"));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rt_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window =
        SDL_CreateWindow("thug2 Vulkan ray tracing check", kWindowWidth, kWindowHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
