// NOT ORIGINAL - thug2_vulkan_rt_check.exe: ray tracing's scene on Vulkan (gpu_vulkan/vulkan_rt_scene.h) tried on this
// machine. It opens the device with ray tracing (as [Graphics] ray_tracing=1 does), makes meshes through render::Gpu as
// the PC layer does - indexed lists (16- and 32-bit indices, vertex, index and position offsets), indexed and unindexed
// strips, unindexed lists, a dynamic mesh, many small meshes sharing two buffers - registers them with
// render::rt::RayTracing, and draws frames of instances with transforms and material ids. Each frame's TLAS is then
// traced from a compute shader with ray queries (trace_rays_now) and every ray compared with a reference traced here on
// the processor in double precision: hit or miss, distance, instance, primitive, material and barycentrics. Rays the
// reference finds too close to an edge or another surface to call are counted and left out. Also: the static meshes'
// compaction, the dynamic mesh refitted after its vertices change (and rebuilt after many refits), a mesh removed and
// added again, an empty scene, everything released, and the validation layer's messages (none allowed).
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
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tests/no_error_dialogs.h"
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
constexpr std::array<uint32_t, 6> kCullMasks{0xFF, 0x01, 0x02, 0x04, 0x80, 0x03}; // the rays' (one each, in turn)
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

void run_checks(render::Gpu& gpu, const Options& options, Tally* tally)
{
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
