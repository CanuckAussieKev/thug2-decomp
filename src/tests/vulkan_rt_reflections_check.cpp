// NOT ORIGINAL - thug2_vulkan_rt_reflections_check.exe: the Vulkan Gpu's ray-traced reflections
// (gpu_vulkan/vulkan_rt_reflections.h) and the geometry table they read (gpu_vulkan/vulkan_rt_scene.h) tried on this
// machine against references worked out on the processor. Everything goes through the installed render::rt::RayTracing
// (R2's acceleration structures, R3's material table), as render_pc's frame record feeds it.
//   - The geometry table: meshes made through render::Gpu in every layout the scene builds - indexed lists with 16- and
//     32-bit indices, indexed strips (16-bit at an odd first index: a word's upper half) and unindexed lists and strips,
//     with vertex and index offsets, texture coordinates and colours at offsets of their own, one mesh with neither -
//     each instanced (moved, one scaled); rays traced from above (trace_surfaces_now) and, for each hit, the texture
//     coordinates and colour the table's GLSL interpolates compared with the processor's from the mesh's own data at
//     the hit's triangle and barycentrics, and the position the table's vertices give with where the ray says it hit.
//   - The reflections: a mirror floor (a 32-bit indexed strip, reflectivity 1) and a box standing on it, textured with
//     an 8 x 8 texture of distinct texels (sampled nearest, one level) and grey vertex colours, drawn through the Gpu as
//     render_pc draws a level; rt::opaque_view_drawn runs the passes through the listener. The processor follows each
//     pixel's ray to the floor, reflects it, finds the box face it meets and the texel at that point: pixels whose answer
//     doesn't change when the ray moves a tenth of a pixel ("clear-cut") must be that texel times the vertex colour
//     doubled (mixed with the floor by reflectivity x strength); floor pixels whose reflection misses, the box and the
//     sky must be exactly as with the pass off. One sample and four (resolved for the read-back); strength 1 and a half.
//   - Fresnel (rt_reflection_fresnel): Schlick's curve against the exact reflectance of water and glass from face-on to
//     grazing; the mirror again as water, glass (four samples) and a shiny surface - each reflected pixel's colour and
//     the pass's own weight (its image's alpha) against the processor's weight for the angle it is seen at, so the
//     floor's own colour shows through face-on; the glass mask (the first pass's alpha: the floor's white texture
//     opaque) mirroring everywhere one way and nowhere the other; the floor under a cover pass (a wall texture drawn
//     over its environment pass by its alpha, through the floor's second texture coordinate set - stripes of clear
//     and opaque texels): reflected through the clear stripes only, the opaque ones exactly as without the pass, with
//     Fresnel and without; rough glass (rt_glass_roughness) against the same
//     glass as a mirror - the light it adds within 0.8 - 1.05 of the mirror's, softer, no pixel black, no NaN, every
//     weight at most the mirror's.
//   - Nothing reflective (the floor's reflectivity 0): no pass, nothing changed; rt_reflections off: no pass.
//   - (added for the skinned meshes' attributes) The box as the skater's skinned meshes' vertices (format 0x152: the
//     normal at 12, the colour at 24, the uv at 28 - not the scene file's offsets 24 / 28 / 32 the mesh itself gives) in
//     a distinctive vertex colour, its geometry's attributes worked out from the format (engine/gfx/rt_vertex_attributes.h,
//     as render_pc's frame record does): reflected pixels its texel x that colour doubled; the same box given the
//     mesh's own offsets (as before) fails the comparison.
//   - rt_sky (the sky's misses): the floor's reflections that miss the box show the sky drawn - the sky-view table toward
//     the reflected direction, as the game's colour - where they kept the floor's own colour without it.
//   - (added for the surface reflections) The floor without a shine of its own, its texture given a surface map (rt_pbr):
//     a smooth glazed tile reflects by Fresnel at the map's F0 (the weights against the processor's, as above); a rough
//     tile, a concrete map, rt_reflection_surfaces=0 and rt_pbr off leave every pixel as drawn (concrete and pbr off:
//     no pass at all).
//   thug2_vulkan_rt_reflections_check [--device N] [--display N] [--no-validation] [--seed N]
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
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
#include "engine/gfx/rt_vertex_attributes.h"
#include "engine/gfx/surface_maps.h"
#include "formats/surface_map_format.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_reflections.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_rt_sky.h"
#include "gpu_vulkan/vulkan_rt_sky_pass.h"
#include "platform/run_log.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
namespace rt = render::rt;
using Vec3 = std::array<double, 3>;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr uint32_t kTargetWidth = 320;
constexpr uint32_t kTargetHeight = 240;
constexpr int kColumns = static_cast<int>(kTargetWidth);
constexpr int kRows = static_cast<int>(kTargetHeight);
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvfColour = 0x042;   // XYZ | DIFFUSE (the raster draws)
constexpr uint32_t kFvfTextured = 0x142; // XYZ | DIFFUSE | TEX1 (the meshes the rays see)
constexpr uint32_t kFormatIndex32 = 102; // D3DFMT_INDEX32
constexpr uint32_t kClearColour = 0xFF102030;
constexpr int kTolerance = 2;
constexpr float kNearZ = 2.0F;
constexpr float kFarZ = 2000.0F;
constexpr double kHalfFovTan = 0.57735026918962576; // tan 30 degrees: a 60-degree horizontal field of view
constexpr double kNoHit = std::numeric_limits<double>::infinity();
constexpr uint32_t kByteMask = 0xFF;
constexpr uint32_t kRedShift = 16;
constexpr uint32_t kGreenShift = 8;
constexpr std::size_t kPixelBytes = 4;
constexpr double kOffsetScale = 1.0;       // (the pass's start offset)
constexpr uint32_t kTextureSide = 8;
constexpr uint32_t kGrey = 0x80808080U;    // the box's vertex colour: 128 doubled is 1.004
constexpr uint32_t kFloorColour = 0xFF305070;
constexpr uint32_t kBoxColour = 0xFF806040;
constexpr uint32_t kWorldOpaque = rt::kWorld | rt::kOpaque | rt::kCastsShadow;
constexpr float kJunk = 1.0e30F;
constexpr uint8_t kJunkByte = 0xEE;
constexpr double kByteRange = 255.0;
constexpr uint32_t kAlphaShift = 24;
constexpr uint32_t kOpaqueAlpha = 0xFF000000U;
constexpr uint32_t kAllBits = 0xFFFFFFFFU;
constexpr uint32_t kEveryInstance = 0xFF; // a ray's cull mask
constexpr float kUvSpread = 2.0F;         // the grids' texture coordinates: -2 - 2
constexpr uint32_t kGridSeed = 0x52355;
constexpr double kScaled = 1.5;          // the second grid instance's scale
constexpr float kAbove = 100.0F;          // the rays start this high
constexpr float kRayLength = 1000.0F;
constexpr float kAmbientGrey = 0.5F;
constexpr uint32_t kTexturedStride = 24; // XYZ | DIFFUSE | TEX1: the position, the colour at 12, the uv at 16
constexpr uint32_t kTexturedColour = 12;
constexpr uint32_t kTexturedUv = 16;
constexpr uint32_t kTwoSetStride = 32;   // ... and a second uv at 24 (the floor: its cover's set)
constexpr double kCoverTiling = 2.0;     // the floor's second set: the first x 2
constexpr uint32_t kCoverClearColumns = 4; // the cover texture: its alpha 0 in columns 0 - 3 (the windows), 255 after
constexpr int kCoverClear = 0;             // (Reflection::cover)
constexpr int kCoverOpaque = 1;
constexpr int kCoverBlend = 2;
// (added for the skinned meshes' attributes) The vertex the CPU skinning writes (render_pc/pc_mesh_load.cpp
// set_up_skinning, pc_mesh_render.cpp get_skin): XYZ | NORMAL | DIFFUSE | TEX1 - and the offsets the scene file gives
// such a mesh (its packed vertex's), which the geometry table was once filled from.
constexpr uint32_t kFvfSkinned = 0x152;
constexpr uint32_t kSkinnedStride = 36;
constexpr uint32_t kSkinnedNormal = 12;
constexpr uint32_t kSkinnedColour = 24;
constexpr uint32_t kSkinnedUv = 28;
constexpr uint32_t kSceneFileColour = 28;
constexpr uint32_t kSceneFileUv = 32;
constexpr uint32_t kSkinnedVertexColour = 0x803090B0U; // (alpha 128 as the grey's; red, green, blue each its own)

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
    uint32_t seed = kGridSeed; // the geometry part's random meshes and rays
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

// The backend's log lines (why a pass was skipped, its first pass) shown with the checks'.
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
Vec3 step(const Vec3& from, const Vec3& along, double scale)
{
    return {x_of(from) + (x_of(along) * scale), y_of(from) + (y_of(along) * scale), z_of(from) + (z_of(along) * scale)};
}
Vec3 minus(const Vec3& one, const Vec3& other) { return step(one, other, -1.0); }
std::array<float, 3> floats(const Vec3& v)
{
    return {static_cast<float>(x_of(v)), static_cast<float>(y_of(v)), static_cast<float>(z_of(v))};
}

void put_float(std::span<uint8_t> bytes, std::size_t at, float value)
{
    std::memcpy(bytes.subspan(at, sizeof(value)).data(), &value, sizeof(value));
}
void put_word(std::span<uint8_t> bytes, std::size_t at, uint32_t value)
{
    std::memcpy(bytes.subspan(at, sizeof(value)).data(), &value, sizeof(value));
}

// --- meshes through the Gpu ------------------------------------------------------------------------------------------

// How a mesh is laid out in its buffers (attribute offsets: kAbsent for none).
constexpr uint32_t kAbsent = 0xFFFFFFFFU;
struct Layout {
    bool indexed = true;
    bool wide = false;
    bool strip = false;
    uint32_t stride = 24;
    uint32_t position_offset = 0;
    uint32_t uv_offset = kAbsent;
    uint32_t colour_offset = kAbsent;
    uint32_t leading_vertices = 0;
    uint32_t leading_indices = 0;
    uint32_t uv_sets = 1; // (the sets after uv_offset: MeshData::uvs, then second_uvs)
    // (added for the skinned meshes' attributes) the vertex normals (MeshData::normals) and a vertex format: when one
    // is given, the geometry's attributes are worked out from it (rt::set_vertex_attributes) and the offsets above
    // only say where the bytes are written
    uint32_t normal_offset = kAbsent;
    uint32_t fvf = 0;
};

struct MeshData {
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 2>> uvs;
    std::vector<std::array<float, 2>> second_uvs; // (a second texture coordinate set, where the layout has one)
    std::vector<uint32_t> colours;
    std::vector<std::array<float, 3>> normals; // (where the layout has them)
    std::vector<uint32_t> corners; // the indices drawn (a list's or a strip's); unindexed: 0..n-1
};

struct TestMesh {
    rt::MeshId id = 0;
    MeshData data;
    Layout layout;
    render::GpuVertexBuffer* vertices = nullptr;
    render::GpuIndexBuffer* indices = nullptr;
    rt::Geometry geometry;
};

// The triangles as the BLAS has them: a strip's odd ones turned back round (vulkan_rt_scene.cpp).
std::vector<std::array<uint32_t, 3>> triangles_of(const TestMesh& mesh)
{
    std::vector<std::array<uint32_t, 3>> triangles;
    const std::vector<uint32_t>& corners = mesh.data.corners;
    if (mesh.layout.strip) {
        for (std::size_t i = 0; i + 2 < corners.size(); ++i) {
            const bool odd = (i & 1U) != 0;
            triangles.push_back(
                {core::at(corners, odd ? i + 1 : i), core::at(corners, odd ? i : i + 1), core::at(corners, i + 2)});
        }
    } else {
        for (std::size_t i = 0; i + 2 < corners.size(); i += 3) {
            triangles.push_back({core::at(corners, i), core::at(corners, i + 1), core::at(corners, i + 2)});
        }
    }
    return triangles;
}

std::vector<uint8_t> vertex_bytes(const MeshData& data, const Layout& layout)
{
    const std::size_t count = layout.leading_vertices + data.positions.size();
    std::vector<uint8_t> bytes(count * layout.stride);
    for (std::size_t at = 0; at + sizeof(float) <= bytes.size(); at += sizeof(float)) {
        put_float(bytes, at, kJunk);
    }
    for (std::size_t i = 0; i < data.positions.size(); ++i) {
        const std::size_t base = (layout.leading_vertices + i) * layout.stride;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            put_float(bytes, base + layout.position_offset + (axis * sizeof(float)),
                      core::at(core::at(data.positions, i), axis));
        }
        if (layout.uv_offset != kAbsent) {
            put_float(bytes, base + layout.uv_offset, core::at(data.uvs, i).front());
            put_float(bytes, base + layout.uv_offset + sizeof(float), core::at(data.uvs, i).back());
            if (layout.uv_sets > 1) {
                put_float(bytes, base + layout.uv_offset + (2 * sizeof(float)), core::at(data.second_uvs, i).front());
                put_float(bytes, base + layout.uv_offset + (3 * sizeof(float)), core::at(data.second_uvs, i).back());
            }
        }
        if (layout.colour_offset != kAbsent) {
            put_word(bytes, base + layout.colour_offset, core::at(data.colours, i));
        }
        if (layout.normal_offset != kAbsent) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                put_float(bytes, base + layout.normal_offset + (axis * sizeof(float)),
                          core::at(core::at(data.normals, i), axis));
            }
        }
    }
    return bytes;
}

std::vector<uint8_t> index_bytes(const MeshData& data, const Layout& layout)
{
    const std::size_t size = layout.wide ? sizeof(uint32_t) : sizeof(uint16_t);
    std::vector<uint8_t> bytes((layout.leading_indices + data.corners.size()) * size, kJunkByte);
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

bool make_mesh(render::Gpu& gpu, TestMesh* mesh)
{
    const Layout& layout = mesh->layout;
    const std::vector<uint8_t> vertices = vertex_bytes(mesh->data, layout);
    void* data = nullptr;
    if (gpu.create_vertex_buffer(static_cast<uint32_t>(vertices.size()), 0, layout.fvf != 0 ? layout.fvf : kFvfTextured,
                                 value::kPoolDefault, &mesh->vertices) != value::kResultOk ||
        gpu.lock_vertex_buffer(mesh->vertices, 0, 0, &data, 0) != value::kResultOk) {
        return false;
    }
    std::memcpy(data, vertices.data(), vertices.size());
    if (gpu.unlock_vertex_buffer(mesh->vertices) != value::kResultOk) {
        return false;
    }
    if (layout.indexed) {
        const std::vector<uint8_t> indices = index_bytes(mesh->data, layout);
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
                      .dynamic = false,
                      .uv_offset = layout.uv_offset == kAbsent ? 0U : layout.uv_offset,
                      .uv_format = layout.uv_offset == kAbsent ? rt::kAttributeNone : rt::kUvFloat2,
                      .colour_offset = layout.colour_offset == kAbsent ? 0U : layout.colour_offset,
                      .colour_format = layout.colour_offset == kAbsent ? rt::kAttributeNone : rt::kColourA8R8G8B8,
                      .uv_sets = layout.uv_sets};
    if (layout.fvf != 0 && !rt::set_vertex_attributes(layout.fvf, &mesh->geometry)) {
        return false;
    }
    mesh->id = rt::next_mesh_id();
    rt::ray_tracing()->add_mesh(mesh->id, mesh->geometry);
    return true;
}

void release_mesh(render::Gpu& gpu, TestMesh* mesh)
{
    if (mesh->id != 0 && rt::ray_tracing() != nullptr) {
        rt::ray_tracing()->remove_mesh(mesh->id);
    }
    if (mesh->vertices != nullptr) {
        gpu.release_vertex_buffer(mesh->vertices);
    }
    if (mesh->indices != nullptr) {
        gpu.release_index_buffer(mesh->indices);
    }
    *mesh = TestMesh{};
}

// --- part 1: the geometry table ------------------------------------------------------------------------------------

constexpr uint32_t kGridSide = 6;
constexpr double kGridSize = 12.0;

MeshData grid(uint32_t side, double size, double phase, std::mt19937& random)
{
    std::uniform_real_distribution<float> spread(-kUvSpread, kUvSpread);
    std::uniform_int_distribution<uint32_t> any_colour(0, kAllBits);
    MeshData data;
    for (uint32_t row = 0; row <= side; ++row) {
        for (uint32_t column = 0; column <= side; ++column) {
            const double x = ((static_cast<double>(column) / side) - 0.5) * size;
            const double z = ((static_cast<double>(row) / side) - 0.5) * size;
            const double y = std::sin((x * 0.7) + phase) * std::cos((z * 0.5) - phase) * (size * 0.1);
            data.positions.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
            data.uvs.push_back({spread(random), spread(random)});
            data.colours.push_back(any_colour(random));
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
            data->corners.insert(data->corners.end(), {near_left, far_left, near_right, near_right, far_left, far_right});
        }
    }
}

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

MeshData unindexed(const MeshData& indexed)
{
    MeshData data;
    for (const uint32_t corner : indexed.corners) {
        data.positions.push_back(core::at(indexed.positions, corner));
        data.uvs.push_back(core::at(indexed.uvs, corner));
        data.colours.push_back(core::at(indexed.colours, corner));
        data.corners.push_back(static_cast<uint32_t>(data.corners.size()));
    }
    return data;
}

struct Placed {
    std::size_t mesh = 0;
    double x = 0.0;     // moved along x
    double scale = 1.0; // and scaled about its centre
    uint32_t flags = kWorldOpaque;
};

std::array<std::array<float, 4>, 3> transform_of(const Placed& placed)
{
    const auto scale = static_cast<float>(placed.scale);
    return {{{scale, 0.0F, 0.0F, static_cast<float>(placed.x)},
             {0.0F, scale, 0.0F, 0.0F},
             {0.0F, 0.0F, scale, 0.0F}}};
}

std::string attribute_text(const std::array<float, 2>& uv, const std::array<float, 4>& colour)
{
    return text::format("uv %.5f %.5f colour %.4f %.4f %.4f %.4f", static_cast<double>(uv.front()),
                        static_cast<double>(uv.back()), static_cast<double>(core::at(colour, 0)),
                        static_cast<double>(core::at(colour, 1)), static_cast<double>(core::at(colour, 2)),
                        static_cast<double>(core::at(colour, 3)));
}

struct LayoutCase {
    Layout layout;
    const char* name = "";
};
// (uv / colour offsets on 4-byte boundaries, as the game's are; one mesh with neither)
constexpr std::array<LayoutCase, 7> kLayoutCases{{
    {.layout = {.indexed = true, .wide = false, .strip = false, .stride = 24, .position_offset = 0, .uv_offset = 16,
                .colour_offset = 12},
     .name = "indexed list, 16-bit"},
    {.layout = {.indexed = true, .wide = true, .strip = false, .stride = 36, .position_offset = 12, .uv_offset = 0,
                .colour_offset = 8, .leading_vertices = 7, .leading_indices = 11},
     .name = "indexed list, 32-bit, offsets"},
    {.layout = {.indexed = true, .wide = false, .strip = true, .stride = 28, .position_offset = 8, .uv_offset = 20,
                .colour_offset = 4, .leading_vertices = 3, .leading_indices = 5},
     .name = "indexed strip, 16-bit from an odd index"},
    {.layout = {.indexed = true, .wide = true, .strip = true, .stride = 24, .position_offset = 0, .uv_offset = 16,
                .colour_offset = 12, .leading_indices = 2},
     .name = "indexed strip, 32-bit"},
    {.layout = {.indexed = false, .strip = false, .stride = 28, .position_offset = 4, .uv_offset = 16,
                .colour_offset = 24, .leading_vertices = 2},
     .name = "unindexed list"},
    {.layout = {.indexed = false, .strip = true, .stride = 24, .position_offset = 0, .uv_offset = 16,
                .colour_offset = 12},
     .name = "unindexed strip"},
    {.layout = {.indexed = true, .wide = false, .strip = false, .stride = 16, .position_offset = 4},
     .name = "no texture coordinates or colour"},
}};

// The attributes the processor works out for a hit: from the mesh's own data at the hit's triangle and barycentrics.
struct Attributes {
    std::array<double, 2> uv{};
    std::array<double, 4> colour{}; // r, g, b, a (0 - 1)
    Vec3 position{};                // the object's
};
Attributes attributes_at(const TestMesh& mesh, const std::array<uint32_t, 3>& corners,
                         const std::array<float, 2>& barycentrics)
{
    const bool has_uv = mesh.layout.uv_offset != kAbsent;
    const bool has_colour = mesh.layout.colour_offset != kAbsent;
    const double second = barycentrics.front();
    const double third = barycentrics.back();
    const std::array<double, 3> weights{1.0 - second - third, second, third};
    // (r, g, b, a from A8R8G8B8)
    constexpr std::array<uint32_t, 4> kShifts{kRedShift, kGreenShift, 0, kAlphaShift};
    Attributes out;
    for (std::size_t k = 0; k < 3; ++k) {
        const uint32_t corner = core::at(corners, k);
        const double weight = core::at(weights, k);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            core::at(out.position, axis) +=
                weight * static_cast<double>(core::at(core::at(mesh.data.positions, corner), axis));
        }
        if (has_uv) {
            core::at(out.uv, 0) += weight * static_cast<double>(core::at(mesh.data.uvs, corner).front());
            core::at(out.uv, 1) += weight * static_cast<double>(core::at(mesh.data.uvs, corner).back());
        }
        const uint32_t packed = has_colour ? core::at(mesh.data.colours, corner) : kAllBits;
        for (std::size_t channel = 0; channel < kShifts.size(); ++channel) {
            core::at(out.colour, channel) +=
                weight * static_cast<double>((packed >> core::at(kShifts, channel)) & kByteMask) / kByteRange;
        }
    }
    return out;
}

double largest_difference(std::span<const double> want, std::span<const float> got)
{
    double worst = 0.0;
    for (std::size_t k = 0; k < want.size() && k < got.size(); ++k) {
        worst = std::max(worst, std::abs(core::at(want, k) - static_cast<double>(core::at(got, k))));
    }
    return worst;
}

// What is wrong with a hit ("" when nothing is).
std::string hit_problem(const TestMesh& mesh, const Placed& where, std::size_t instance,
                        const render::vulkan::SurfaceRay& ray, const render::vulkan::SurfaceHit& hit,
                        rt::MaterialId material)
{
    constexpr double kUvSlack = 2e-4;
    constexpr double kColourSlack = 1e-4;
    constexpr double kPositionSlack = 2e-3;
    const std::vector<std::array<uint32_t, 3>> triangles = triangles_of(mesh);
    if (!hit.hit || hit.instance != instance || !hit.live || hit.primitive >= triangles.size()) {
        return text::format("hit %d instance %u live %d primitive %u", hit.hit ? 1 : 0, hit.instance, hit.live ? 1 : 0,
                            hit.primitive);
    }
    const Attributes want = attributes_at(mesh, core::at(triangles, hit.primitive), hit.barycentrics);
    // the world position the vertices give, against where the ray says it hit and where the shader put it
    const Vec3 world{(x_of(want.position) * where.scale) + where.x, y_of(want.position) * where.scale,
                     z_of(want.position) * where.scale};
    const Vec3 along{static_cast<double>(ray.origin.at(0)), static_cast<double>(ray.origin.at(1)) - hit.t,
                     static_cast<double>(ray.origin.at(2))};
    const std::array<float, 3> along_floats = floats(along);
    const double position_error =
        std::max(largest_difference(world, along_floats), largest_difference(world, hit.position));
    const double uv_error = largest_difference(want.uv, hit.uv);
    const double colour_error = largest_difference(want.colour, hit.colour);
    const bool has_uv = mesh.layout.uv_offset != kAbsent;
    const bool has_colour = mesh.layout.colour_offset != kAbsent;
    const uint32_t want_flags = render::vulkan::kGeometryLive | (has_uv ? render::vulkan::kGeometryUv : 0U) |
                                (has_colour ? render::vulkan::kGeometryColour : 0U);
    const uint32_t flag_mask =
        render::vulkan::kGeometryLive | render::vulkan::kGeometryUv | render::vulkan::kGeometryColour;
    if (position_error <= kPositionSlack * where.scale && uv_error <= kUvSlack && colour_error <= kColourSlack &&
        (hit.geometry_flags & flag_mask) == want_flags && hit.material == material) {
        return {};
    }
    const std::array<float, 2> want_uv{static_cast<float>(want.uv.front()), static_cast<float>(want.uv.back())};
    std::array<float, 4> want_colour{};
    for (std::size_t k = 0; k < want_colour.size(); ++k) {
        core::at(want_colour, k) = static_cast<float>(core::at(want.colour, k));
    }
    return text::format("primitive %u: position off by %.5f, %s (wanted %s), flags 0x%x", hit.primitive,
                        position_error, attribute_text(hit.uv, hit.colour).c_str(),
                        attribute_text(want_uv, want_colour).c_str(), hit.geometry_flags);
}

// The meshes made through the Gpu in each layout and registered; false when one couldn't be.
bool make_layout_meshes(render::Gpu& gpu, std::mt19937& random, std::vector<TestMesh>* meshes)
{
    meshes->assign(kLayoutCases.size(), TestMesh{});
    for (std::size_t i = 0; i < kLayoutCases.size(); ++i) {
        TestMesh& mesh = core::at(*meshes, i);
        mesh.layout = core::at(kLayoutCases, i).layout;
        MeshData data = grid(kGridSide, kGridSize, static_cast<double>(i), random);
        if (mesh.layout.strip) {
            grid_strip(&data, kGridSide);
        } else {
            grid_list(&data, kGridSide);
        }
        mesh.data = mesh.layout.indexed ? data : unindexed(data);
        if (!make_mesh(gpu, &mesh)) {
            return false;
        }
    }
    return true;
}

// Rays straight down over each instance's grid, inside its edge (and the instance each is over).
std::vector<render::vulkan::SurfaceRay> rays_over(const std::vector<Placed>& placed, std::mt19937& random,
                                                  std::vector<std::size_t>* over)
{
    constexpr int kRaysEach = 300;
    constexpr double kInside = 0.45;
    std::uniform_real_distribution<double> across(-kInside * kGridSize, kInside * kGridSize);
    std::vector<render::vulkan::SurfaceRay> rays;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        const Placed& each = core::at(placed, i);
        for (int count = 0; count < kRaysEach; ++count) {
            const double x = across(random) * each.scale;
            const double z = across(random) * each.scale;
            rays.push_back({.origin = {static_cast<float>(each.x + x), kAbove, static_cast<float>(z)},
                            .direction = {0.0F, -1.0F, 0.0F},
                            .t_max = kRayLength,
                            .cull_mask = kEveryInstance});
            over->push_back(i);
        }
    }
    return rays;
}

void run_geometry(render::Gpu& gpu, uint32_t seed, Tally* tally)
{
    text::print("the geometry table (texture coordinates and colours at a ray's hit):\n");
    std::mt19937 random(seed);
    std::vector<TestMesh> meshes;
    const bool made = rt::ray_tracing() != nullptr && make_layout_meshes(gpu, random, &meshes);
    tally->check(made, "the meshes made and registered");
    const rt::MaterialId material = rt::next_material_id();
    if (made) {
        // the instances: each mesh once, side by side; the second one scaled; the fifth an object
        std::vector<Placed> placed;
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            constexpr double kApart = 20.0;
            constexpr std::size_t kScaledOne = 1;
            constexpr std::size_t kObjectOne = 4;
            placed.push_back(
                {.mesh = i,
                 .x = static_cast<double>(i) * kApart,
                 .scale = i == kScaledOne ? kScaled : 1.0,
                 .flags = i == kObjectOne ? (rt::kObject | rt::kOpaque | rt::kCastsShadow) : kWorldOpaque});
        }
        rt::ray_tracing()->set_material(material, rt::Material{});
        rt::FrameRecord& record = rt::frame_record();
        record = rt::FrameRecord{};
        record.frame = 1;
        for (const Placed& each : placed) {
            record.instances.push_back({.mesh = core::at(meshes, each.mesh).id,
                                        .material = material,
                                        .flags = each.flags,
                                        .object_to_world = transform_of(each)});
        }
        gpu.begin_scene();
        rt::ray_tracing()->begin_frame(record);
        gpu.end_scene();
        gpu.present();
        std::vector<std::size_t> over;
        const std::vector<render::vulkan::SurfaceRay> rays = rays_over(placed, random, &over);
        std::vector<render::vulkan::SurfaceHit> hits;
        const bool traced = render::vulkan::trace_surfaces_now(rays, &hits) == render::vulkan::TraceResult::kTraced &&
                            hits.size() == rays.size();
        tally->check(traced, "the rays traced");
        for (std::size_t index = 0; traced && index < meshes.size(); ++index) {
            int compared = 0;
            int wrong = 0;
            std::string first_wrong;
            for (std::size_t ray = 0; ray < rays.size(); ++ray) {
                if (core::at(over, ray) != index) {
                    continue;
                }
                ++compared;
                const std::string why = hit_problem(core::at(meshes, index), core::at(placed, index), index,
                                                    core::at(rays, ray), core::at(hits, ray), material);
                if (!why.empty()) {
                    ++wrong;
                    if (first_wrong.empty()) {
                        first_wrong.append("; first: ").append(why);
                    }
                }
            }
            constexpr int kEnough = 250;
            tally->check(compared >= kEnough && wrong == 0,
                         text::format("%s: %d hits compared, %d wrong%s", core::at(kLayoutCases, index).name, compared,
                                      wrong, first_wrong.c_str()));
        }
    }
    for (TestMesh& mesh : meshes) {
        release_mesh(gpu, &mesh);
    }
    if (rt::ray_tracing() != nullptr) {
        rt::ray_tracing()->remove_material(material);
    }
}

// --- part 2: the mirror ------------------------------------------------------------------------------------------

constexpr double kFloorHalf = 60.0;
constexpr Vec3 kBoxLow{-12.0, 0.0, -12.0};
constexpr Vec3 kBoxHigh{12.0, 24.0, 12.0};
constexpr Vec3 kEye{6.0, 20.0, 70.0};
constexpr Vec3 kAt{0.0, 4.0, 0.0};
constexpr Vec3 kUp{0.0, 1.0, 0.0};

// The texture's texel (column, row) as A8R8G8B8: every one different.
uint32_t texel_colour(uint32_t column, uint32_t row)
{
    constexpr uint32_t kBase = 24;
    constexpr uint32_t kStep = 28;
    constexpr uint32_t kBlueTop = 220;
    constexpr uint32_t kBlueStep = 25;
    const uint32_t red = kBase + (kStep * column);
    const uint32_t green = kBase + (kStep * row);
    const uint32_t blue = kBlueTop - (kBlueStep * ((column + row) % kTextureSide));
    return kOpaqueAlpha | (red << kRedShift) | (green << kGreenShift) | blue;
}

// A box face: the axis it faces along and which side; its texture runs along the next two axes (u, v).
struct Face {
    std::size_t axis = 0;
    bool high = false;
};
std::array<std::size_t, 2> face_axes(std::size_t axis) { return {(axis + 1) % 3, (axis + 2) % 3}; }

// The box's face texture coordinates at a point on it.
std::array<double, 2> face_uv(const Face& face, const Vec3& point)
{
    const std::array<std::size_t, 2> axes = face_axes(face.axis);
    std::array<double, 2> uv{};
    for (std::size_t k = 0; k < 2; ++k) {
        const std::size_t along = core::at(axes, k);
        core::at(uv, k) = (core::at(point, along) - core::at(kBoxLow, along)) /
                          (core::at(kBoxHigh, along) - core::at(kBoxLow, along));
    }
    return uv;
}

// The box (24 vertices: four a face, with its texture coordinates; grey) and the floor square.
MeshData box_data()
{
    MeshData data;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        for (const bool high : {false, true}) {
            const std::array<std::size_t, 2> axes = face_axes(axis);
            const auto first = static_cast<uint32_t>(data.positions.size());
            for (uint32_t corner = 0; corner < 4; ++corner) {
                Vec3 point{};
                core::at(point, axis) = core::at(high ? kBoxHigh : kBoxLow, axis);
                const std::size_t across = axes.front();
                const std::size_t up = axes.back();
                core::at(point, across) = core::at((corner & 1U) != 0 ? kBoxHigh : kBoxLow, across);
                core::at(point, up) = core::at((corner & 2U) != 0 ? kBoxHigh : kBoxLow, up);
                data.positions.push_back(floats(point));
                const std::array<double, 2> uv = face_uv({.axis = axis, .high = high}, point);
                data.uvs.push_back({static_cast<float>(uv.front()), static_cast<float>(uv.back())});
                data.colours.push_back(kGrey);
            }
            data.corners.insert(data.corners.end(), {first, first + 1, first + 2, first + 2, first + 1, first + 3});
        }
    }
    return data;
}

MeshData floor_data()
{
    MeshData data;
    const auto half = static_cast<float>(kFloorHalf);
    data.positions = {{-half, 0.0F, -half}, {-half, 0.0F, half}, {half, 0.0F, -half}, {half, 0.0F, half}};
    data.uvs = {{0.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}};
    for (const std::array<float, 2>& uv : data.uvs) {
        data.second_uvs.push_back(
            {uv.front() * static_cast<float>(kCoverTiling), uv.back() * static_cast<float>(kCoverTiling)});
    }
    data.colours = {kGrey, kGrey, kGrey, kGrey};
    data.corners = {0, 1, 2, 3}; // (a strip: two triangles)
    return data;
}

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

// A ray against the box (slabs): the distance to its first hit and that face.
struct BoxHit {
    double distance = kNoHit;
    Face face;
};
BoxHit intersect_box(const Vec3& origin, const Vec3& direction)
{
    constexpr double kParallel = 1e-12;
    double enter = 0.0;
    double leave = kNoHit;
    Face face;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double from = core::at(origin, axis);
        const double along = core::at(direction, axis);
        const double low = core::at(kBoxLow, axis);
        const double high = core::at(kBoxHigh, axis);
        if (std::abs(along) < kParallel) {
            if (from < low || from > high) {
                return {};
            }
            continue;
        }
        const bool rising = along > 0.0;
        const double near_t = ((rising ? low : high) - from) / along;
        const double far_t = ((rising ? high : low) - from) / along;
        if (near_t > enter) {
            enter = near_t;
            face = {.axis = axis, .high = !rising};
        }
        leave = std::min(leave, far_t);
        if (enter > leave) {
            return {};
        }
    }
    if (enter <= 0.0) {
        return {};
    }
    return {.distance = enter, .face = face};
}

// What a pixel's reflection is, as the pass works it out (offset: where in the pixel the depth was read).
struct Reflection {
    bool floor = false;   // the pixel shows the floor
    bool hit = false;     // its reflection meets the box
    uint32_t column = 0;  // the texel there
    uint32_t row = 0;
    std::array<double, 2> uv{};
    double cosine = 0.0;  // between the view and the floor's normal (the Fresnel weight's)
    Vec3 mirrored{};      // the reflected direction (the floor's pixels)
    // the cover's texel at the floor point (its second set, filtered linearly): kCoverClear (between columns 0 - 3's
    // centres), kCoverOpaque (4 - 7's), or kCoverBlend (between the two: left out)
    int cover = 0;
};
using Offset = std::array<double, 2>;
Reflection reflection_at(const View& view, double px, double py)
{
    // (Direct3D 9's pixel centres: whole coordinates)
    const double ndc_x = (2.0 * px / kTargetWidth) - 1.0;
    const double ndc_y = 1.0 - (2.0 * py / kTargetHeight);
    const Vec3 direction = normalised(step(
        step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height), view.back, -1.0));
    Reflection out;
    if (y_of(direction) >= 0.0) {
        return out;
    }
    const double distance = -y_of(view.eye) / y_of(direction);
    const Vec3 point = step(view.eye, direction, distance);
    if (std::abs(x_of(point)) > kFloorHalf || std::abs(z_of(point)) > kFloorHalf ||
        intersect_box(view.eye, direction).distance < distance) {
        return out;
    }
    out.floor = true;
    out.cosine = -y_of(direction);
    // (the floor's first set: u from x, -half..half as 0..1; the second: that x kCoverTiling, wrapped)
    const double u = (x_of(point) + kFloorHalf) / (2.0 * kFloorHalf) * kCoverTiling;
    const double at = (u - std::floor(u)) * kTextureSide;
    constexpr double kHalfTexel = 0.5;
    if (at >= kHalfTexel && at <= kCoverClearColumns - kHalfTexel) {
        out.cover = kCoverClear;
    } else if (at >= kCoverClearColumns + kHalfTexel && at <= kTextureSide - kHalfTexel) {
        out.cover = kCoverOpaque;
    } else {
        out.cover = kCoverBlend;
    }
    const Vec3 normal{0.0, 1.0, 0.0};
    const Vec3 mirrored = step(direction, normal, -2.0 * dot(direction, normal));
    out.mirrored = mirrored;
    const double offset = kOffsetScale * (1.0 + (distance * 0.0005) + (distance * distance * 6e-8));
    const BoxHit hit = intersect_box(step(point, normal, offset), mirrored);
    if (hit.distance == kNoHit) {
        return out;
    }
    out.hit = true;
    out.uv = face_uv(hit.face, step(step(point, normal, offset), mirrored, hit.distance));
    out.column = std::min(kTextureSide - 1, static_cast<uint32_t>(std::max(0.0, out.uv.front() * kTextureSide)));
    out.row = std::min(kTextureSide - 1, static_cast<uint32_t>(std::max(0.0, out.uv.back() * kTextureSide)));
    return out;
}

bool same(const Reflection& one, const Reflection& other)
{
    return one.floor == other.floor && one.hit == other.hit && one.column == other.column && one.row == other.row &&
           one.cover == other.cover;
}

// --- drawing it through the Gpu ----------------------------------------------------------------------------------

struct ColourVertex {
    std::array<float, 3> position{};
    uint32_t colour = 0;
};

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

void draw_mesh(render::Gpu& gpu, const MeshData& data, bool strip, uint32_t colour)
{
    std::vector<ColourVertex> vertices;
    std::vector<uint32_t> list;
    if (strip) {
        for (std::size_t i = 0; i + 2 < data.corners.size(); ++i) {
            list.insert(list.end(), {core::at(data.corners, i), core::at(data.corners, i + 1), core::at(data.corners, i + 2)});
        }
    } else {
        list = data.corners;
    }
    vertices.reserve(list.size());
    for (const uint32_t corner : list) {
        vertices.push_back({.position = core::at(data.positions, corner), .colour = colour});
    }
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(vertices.size() / 3),
                         vertices.data(), sizeof(ColourVertex));
}

void draw_scene(render::Gpu& gpu, const MeshData& floor, const MeshData& box)
{
    using render::GpuRenderState;
    using render::GpuStageState;
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    const Matrices matrices = matrices_for(kEye);
    constexpr Matrix kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    gpu.set_transform(render::GpuTransform::kWorld, kIdentity.data());
    gpu.set_transform(render::GpuTransform::kView, matrices.view.data());
    gpu.set_transform(render::GpuTransform::kProjection, matrices.projection.data());
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_vertex_format(kFvfColour);
    gpu.set_texture(0, nullptr);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgDiffuse);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgDiffuse);
    draw_mesh(gpu, floor, true, kFloorColour);
    draw_mesh(gpu, box, false, kBoxColour);
}

rt::ViewDrawn view_drawn()
{
    const Matrices matrices = matrices_for(kEye);
    rt::ViewDrawn view;
    constexpr std::size_t kSide = 4;
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(view.camera.view, row), column) = core::at(matrices.view, (row * kSide) + column);
            core::at(core::at(view.camera.projection, row), column) =
                core::at(matrices.projection, (row * kSide) + column);
        }
    }
    view.camera.position = floats(kEye);
    view.rect = {0, 0, kTargetWidth, kTargetHeight};
    view.depth_range = {0.0F, 1.0F};
    return view;
}

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

// The mirror scene: its meshes, texture and materials, registered with the backend.
struct Mirror {
    TestMesh floor;
    TestMesh box;
    render::GpuTexture* texture = nullptr;
    render::GpuTexture* cover = nullptr; // (the floor's cover pass: the mask's texture)
    render::GpuTexture* surface = nullptr; // (added for the surface reflections) the floor's white texture with a map
    rt::MaterialId floor_material = 0;
    rt::MaterialId box_material = 0;
};

// The floor's surface: `mask` (blend modes 12 / 13) lerped in by its own white texture's alpha (mask pass 0: the base,
// untextured, set 0); `cover`: a wall texture drawn over it by its alpha (a mask pass, kMaskClear, set 1, the vertex
// alpha taken in), as render_pc/rt_record.cpp records BO's buildings.
rt::Material floor_surface(float reflectivity, uint32_t kind = rt::kReflectShiny,
                           uint32_t mask = rt::kReflectEverywhere, render::GpuTexture* cover = nullptr)
{
    rt::Material surface;
    surface.reflectivity = reflectivity;
    surface.environment_pass = reflectivity > 0.0F ? 1U : rt::kNoEnvironmentPass;
    surface.reflection_class = kind;
    surface.reflection_mask = mask;
    if (mask != rt::kReflectEverywhere) {
        surface.mask_passes.front() = {.texture = nullptr,
                                       .kind = mask == rt::kReflectWhereBaseClear ? rt::kMaskClear : rt::kMaskOpaque,
                                       .uv_set = 0,
                                       .texture_alpha = true};
    }
    if (cover != nullptr) {
        surface.mask_passes.back() = {.texture = cover,
                                      .kind = rt::kMaskClear,
                                      .uv_set = 1,
                                      .address_u = value::kAddressWrap,
                                      .address_v = value::kAddressWrap,
                                      .texture_alpha = false};
    }
    return surface;
}

// A mirror case: the pass's settings and the floor's surface.
struct MirrorCase {
    const char* name = "";
    uint32_t samples = 0;
    float strength = 1.0F;
    float reflectivity = 1.0F;
    bool fresnel = false;
    uint32_t kind = rt::kReflectShiny;
    uint32_t mask = rt::kReflectEverywhere;
    bool cover = false; // the floor under a cover pass: reflecting only through its clear texels
    float roughness = 0.0F;
    float glass_reflectance = render::vulkan::kRtGlassReflectance; // (rt_glass_reflectance: glass's F0)
    uint32_t box_colour = kGrey; // (added for the skinned meshes' attributes) the box's vertex colour
    // (added for the surface reflections) above 0: the floor has no shine of its own, its white texture a smooth
    // glazed-tile surface map of this F0 (Mirror::surface) - rt_reflection_surfaces `surfaces`
    float surface_f0 = 0.0F;
    float surfaces = 1.0F;
};

// Schlick's approximation of the Fresnel reflectance, and the exact one for unpolarised light from air into an index n
// (the reference the face-on values are chosen from: F0 = ((n - 1) / (n + 1))^2).
double schlick(double f0, double cosine) { return f0 + ((1.0 - f0) * std::pow(1.0 - std::clamp(cosine, 0.0, 1.0), 5.0)); }
double fresnel_exact(double n, double cosine)
{
    const double c = std::clamp(cosine, 0.0, 1.0);
    const double sine_t = std::sqrt(std::max(0.0, 1.0 - (c * c))) / n;
    const double cos_t = std::sqrt(std::max(0.0, 1.0 - (sine_t * sine_t)));
    const double s = ((c - (n * cos_t)) / (c + (n * cos_t)));
    const double p = ((cos_t - (n * c)) / (cos_t + (n * c)));
    return 0.5 * ((s * s) + (p * p));
}

// (rt::kReflectWaterF0: the record's f0 for water; real glass's 0.04 - rt_glass_reflectance, 0.2 by default)
constexpr double kWaterF0 = 0.02;
constexpr double kGlassF0 = 0.04;

// The weight the pass gives a floor pixel seen at `cosine` (its reflection_weight, worked out on the processor) - the
// floor's texture is white (alpha 1): the mask 1 - a mirrors nothing, a everything.
double reference_weight(const MirrorCase& test, double cosine)
{
    const double reflectivity = test.reflectivity;
    const double strength = test.strength;
    if (test.surface_f0 > 0.0F) {
        // (a smooth glazed tile: its gloss 1, Fresnel at its F0 whatever rt_reflection_fresnel says)
        return static_cast<double>(test.surfaces) * std::min(1.0, 2.0 * strength) * schlick(test.surface_f0, cosine);
    }
    double mask = 1.0;
    if (test.fresnel && test.mask == rt::kReflectWhereBaseClear) {
        mask = 0.0;
    }
    if (!test.fresnel) {
        return reflectivity * strength;
    }
    const double most = reflectivity * std::min(1.0, 2.0 * strength);
    if (test.kind == rt::kReflectWater || test.kind == rt::kReflectGlass) {
        return most * schlick(test.kind == rt::kReflectWater ? kWaterF0 : test.glass_reflectance, cosine) * mask;
    }
    const double face_on = reflectivity * strength;
    const double schlick_term = std::pow(1.0 - std::clamp(cosine, 0.0, 1.0), 5.0);
    return (face_on + ((std::max(most, face_on) - face_on) * schlick_term)) * mask;
}

// ... at a floor pixel: under the cover, nothing where its texel is opaque (alpha 255 x the grey vertex alpha doubled,
// 1.004, clamped to 1: 1 - a is 0), all of it where clear.
double weight_at(const MirrorCase& test, const Reflection& here)
{
    return reference_weight(test, here.cosine) * (test.cover && here.cover != kCoverClear ? 0.0 : 1.0);
}

// An 8 x 8 texture of colour(column, row) texels.
template <typename Colour>
bool make_texture(render::Gpu& gpu, Colour colour, render::GpuTexture** made)
{
    if (gpu.create_texture({.width = kTextureSide,
                            .height = kTextureSide,
                            .levels = 1,
                            .usage = 0,
                            .format = value::kFormatA8R8G8B8,
                            .pool = value::kPoolManaged},
                           made) != value::kResultOk) {
        *made = nullptr;
        return false;
    }
    render::GpuLockedRect locked;
    if (gpu.lock_texture(*made, 0, &locked, nullptr, 0) != value::kResultOk) {
        return false;
    }
    const std::span<uint8_t> into(static_cast<uint8_t*>(locked.bits),
                                  (static_cast<std::size_t>(kTextureSide - 1) * static_cast<uint32_t>(locked.pitch)) +
                                      (kTextureSide * kPixelBytes));
    for (uint32_t row = 0; row < kTextureSide; ++row) {
        for (uint32_t column = 0; column < kTextureSide; ++column) {
            put_word(into, (static_cast<std::size_t>(row) * static_cast<uint32_t>(locked.pitch)) + (column * kPixelBytes),
                     colour(column, row));
        }
    }
    gpu.unlock_texture(*made, 0);
    return true;
}

bool make_mirror(render::Gpu& gpu, Mirror* mirror)
{
    // (the cover: a wall texture over the floor's environment pass - clear (its windows) in its first columns)
    if (!make_texture(gpu, texel_colour, &mirror->texture) ||
        !make_texture(
            gpu, [](uint32_t column, uint32_t) { return column < kCoverClearColumns ? 0x00A04020U : 0xFFA04020U; },
            &mirror->cover)) {
        return false;
    }
    // the floor: a 32-bit indexed strip after junk; the box: a 16-bit list from an odd index
    mirror->floor.layout = {.indexed = true, .wide = true, .strip = true, .stride = kTwoSetStride,
                            .position_offset = 0, .uv_offset = kTexturedUv, .colour_offset = kTexturedColour,
                            .leading_vertices = 2, .leading_indices = 3, .uv_sets = 2};
    mirror->floor.data = floor_data();
    mirror->box.layout = {.indexed = true, .wide = false, .strip = false, .stride = kTexturedStride,
                          .position_offset = 0, .uv_offset = kTexturedUv, .colour_offset = kTexturedColour,
                          .leading_vertices = 1, .leading_indices = 3};
    mirror->box.data = box_data();
    if (!make_mesh(gpu, &mirror->floor) || !make_mesh(gpu, &mirror->box)) {
        return false;
    }
    mirror->floor_material = rt::next_material_id();
    mirror->box_material = rt::next_material_id();
    rt::ray_tracing()->set_material(mirror->floor_material, floor_surface(1.0F));
    rt::Material box;
    box.texture = mirror->texture;
    box.address_u = value::kAddressClamp;
    box.address_v = value::kAddressClamp;
    box.filter = value::kFilterPoint;
    rt::ray_tracing()->set_material(mirror->box_material, box);
    return true;
}

void release_mirror(render::Gpu& gpu, Mirror* mirror)
{
    release_mesh(gpu, &mirror->floor);
    release_mesh(gpu, &mirror->box);
    if (rt::ray_tracing() != nullptr) {
        rt::ray_tracing()->remove_material(mirror->floor_material);
        rt::ray_tracing()->remove_material(mirror->box_material);
    }
    if (mirror->texture != nullptr) {
        gpu.release_texture(mirror->texture);
    }
    if (mirror->cover != nullptr) {
        gpu.release_texture(mirror->cover);
    }
    if (mirror->surface != nullptr) {
        gpu.release_texture(mirror->surface);
    }
    *mirror = Mirror{};
}

// A frame: the record given, cleared, the scene drawn, the passes through the listener (draw_viewport's call). Returns
// the passes recorded.
uint64_t draw_frame(render::Gpu& gpu, const Targets& targets, const Mirror& mirror, uint64_t frame, bool sky = true)
{
    rt::FrameRecord& record = rt::frame_record();
    record = rt::FrameRecord{};
    record.frame = frame;
    constexpr std::array<std::array<float, 4>, 3> kIdentityRows{
        {{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
    record.instances.push_back({.mesh = mirror.floor.id,
                                .material = mirror.floor_material,
                                .flags = kWorldOpaque,
                                .object_to_world = kIdentityRows});
    record.instances.push_back(
        {.mesh = mirror.box.id, .material = mirror.box_material, .flags = kWorldOpaque, .object_to_world = kIdentityRows});
    record.lights.push_back(
        {.kind = rt::Light::Kind::kAmbient, .colour = {kAmbientGrey, kAmbientGrey, kAmbientGrey}});
    gpu.begin_scene();
    rt::ray_tracing()->begin_frame(record);
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kClearColour, 1.0F, 0);
    draw_scene(gpu, mirror.floor.data, mirror.box.data);
    const uint64_t before = render::vulkan::rt_reflection_passes();
    if (sky && rt::sky_replacement().on) {
        rt::sky_view_drawn(view_drawn()); // (rt_sky: its pass, as draw_viewport tells it after the backdrop)
    }
    rt::opaque_view_drawn(view_drawn());
    const uint64_t passes = render::vulkan::rt_reflection_passes() - before;
    gpu.end_scene();
    gpu.present();
    return passes;
}

using Rgb = std::array<int, 3>;
Rgb rgb_of(uint32_t colour)
{
    return {static_cast<int>((colour >> kRedShift) & kByteMask), static_cast<int>((colour >> kGreenShift) & kByteMask),
            static_cast<int>(colour & kByteMask)};
}
Rgb pixel_at(const std::vector<uint8_t>& texels, int x, int y)
{
    const std::size_t at = ((static_cast<std::size_t>(y) * kTargetWidth) + static_cast<std::size_t>(x)) * kPixelBytes;
    return {core::at(texels, at + 2), core::at(texels, at + 1), core::at(texels, at)};
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
std::string rgb_text(const Rgb& colour)
{
    return text::format("%d %d %d", core::at(colour, 0), core::at(colour, 1), core::at(colour, 2));
}

// What a reflected pixel should be: the floor (or `base`, the masked glass's own colour put back) mixed toward the
// texel x the box's vertex colour doubled by `weight`.
Rgb reflected_colour(const Reflection& seen, double weight, const Rgb& base, uint32_t vertex_colour)
{
    const Rgb texel = rgb_of(texel_colour(seen.column, seen.row));
    const Rgb floor = base;
    const Rgb vertex = rgb_of(vertex_colour);
    Rgb out{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const double factor = 2.0 * core::at(vertex, channel) / kByteRange;
        const double shaded = std::min(1.0, core::at(texel, channel) / kByteRange * factor);
        const double mixed = (shaded * weight) + ((core::at(floor, channel) / kByteRange) * (1.0 - weight));
        core::at(out, channel) = static_cast<int>(std::lround(mixed * kByteRange));
    }
    return out;
}

// The pass's result compared with the processor's answers and with the frame drawn without it.
void compare_mirror(const std::vector<uint8_t>& with, const std::vector<uint8_t>& without, const Offset& sample,
                    const MirrorCase& test, Tally* tally)
{
    const View view = view_from(kEye);
    int clear_cut = 0;
    int wrong = 0;
    int untouched = 0;
    int touched_wrong = 0;
    int borderline = 0;
    double lightest = 1.0;
    double heaviest = 0.0;
    // (glass masked where its texture is opaque - the floor's white, untextured: its own colour put back under the
    // reflection, white x the grey vertex colour doubled - 1.004 - clamped)
    const bool replaces = test.fresnel && test.kind == rt::kReflectGlass && test.mask == rt::kReflectWhereBaseOpaque;
    constexpr Rgb kWhite{255, 255, 255};
    std::string first_wrong;
    std::string first_touched;
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const double px = column + sample.front();
            const double py = row + sample.back();
            const Reflection here = reflection_at(view, px, py);
            // (a tenth of a pixel round the point the depth was read at; with four samples, round the whole pixel)
            constexpr double kShift = 0.1;
            constexpr double kWholePixel = 0.45;
            const double spread = sample == Offset{} ? kShift : kWholePixel;
            bool clear = true;
            for (const auto& [dx, dy] : std::array<std::pair<double, double>, 4>{
                     {{kShift, 0.0}, {-kShift, 0.0}, {0.0, kShift}, {0.0, -kShift}}}) {
                clear = clear && same(here, reflection_at(view, px + dx, py + dy));
            }
            for (const auto& [dx, dy] : std::array<std::pair<double, double>, 4>{
                     {{spread, spread}, {-spread, spread}, {spread, -spread}, {-spread, -spread}}}) {
                clear = clear && same(here, reflection_at(view, column + dx, row + dy));
            }
            const Rgb got = pixel_at(with, column, row);
            const Rgb before = pixel_at(without, column, row);
            if (!clear || (test.cover && here.floor && here.cover == kCoverBlend)) {
                ++borderline;
                continue;
            }
            if (here.floor && here.hit && weight_at(test, here) > 0.0) {
                ++clear_cut;
                lightest = std::min(lightest, weight_at(test, here));
                heaviest = std::max(heaviest, weight_at(test, here));
                const Rgb want =
                    reflected_colour(here, weight_at(test, here), replaces ? kWhite : rgb_of(kFloorColour),
                                     test.box_colour);
                if (!close_to(got, want)) {
                    ++wrong;
                    if (first_wrong.empty()) {
                        first_wrong = text::format(" (first at %d,%d: %s, %s wanted - texel %u,%u)", column, row,
                                                   rgb_text(got).c_str(), rgb_text(want).c_str(), here.column, here.row);
                    }
                }
            } else if (replaces && here.floor) {
                // (a floor pixel whose reflection misses: (1 - F) of the glass's own colour, F of the frame)
                ++clear_cut;
                const double f = weight_at(test, here);
                Rgb want{};
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    core::at(want, channel) = static_cast<int>(
                        std::lround((core::at(kWhite, channel) * (1.0 - f)) + (core::at(before, channel) * f)));
                }
                if (!close_to(got, want)) {
                    ++wrong;
                    if (first_wrong.empty()) {
                        first_wrong = text::format(" (first at %d,%d, a miss: %s, %s wanted)", column, row,
                                                   rgb_text(got).c_str(), rgb_text(want).c_str());
                    }
                }
            } else {
                ++untouched;
                if (got != before) {
                    ++touched_wrong;
                    if (first_touched.empty()) {
                        first_touched = text::format(" (first at %d,%d: %s, was %s; floor %d)", column, row,
                                                     rgb_text(got).c_str(), rgb_text(before).c_str(),
                                                     here.floor ? 1 : 0);
                    }
                }
            }
        }
    }
    constexpr int kEnoughReflected = 1500;
    constexpr int kEnoughUntouched = 20000;
    // (the floor's mask clear everywhere: nothing of it reflects - every pixel is checked as untouched)
    const bool none_expected = test.fresnel && test.mask == rt::kReflectWhereBaseClear;
    tally->check((clear_cut >= kEnoughReflected || (none_expected && clear_cut == 0)) && wrong == 0,
                 text::format("reflected pixels the box's texel x its vertex colour (weight %.2f - %.2f): %d of %d wrong%s",
                              lightest, heaviest, wrong, clear_cut, first_wrong.c_str()));
    tally->check(untouched >= kEnoughUntouched && touched_wrong == 0,
                 text::format("the box, the sky and the floor that mirrors nothing exactly as without the pass: %d of "
                              "%d changed%s (%d borderline pixels left out)",
                              touched_wrong, untouched, first_touched.c_str(), borderline));
}

// The pass's own image read back: every value finite; with one sample, each clear-cut reflected pixel's weight (its
// alpha; a rough pixel's negative, the share of its rays that hit x the weight) against the processor's.
void check_image(const MirrorCase& test, Tally* tally)
{
    render::vulkan::Backend& state = render::vulkan::backend();
    render::vulkan::RtReflectionTexels image;
    bool read = false;
    {
        const std::scoped_lock lock(state.mutex);
        read = render::vulkan::read_rt_reflection_image(state, {0, 0, kTargetWidth, kTargetHeight}, &image);
    }
    int not_finite = 0;
    int weighted = 0;
    for (const std::array<float, 4>& texel : image.texels) {
        for (const float channel : texel) {
            not_finite += std::isfinite(channel) ? 0 : 1;
        }
        weighted += texel.back() != 0.0F ? 1 : 0;
    }
    const bool none_expected = test.fresnel && test.mask == rt::kReflectWhereBaseClear;
    tally->check(read && image.width == kTargetWidth && not_finite == 0 && (weighted > 0 || none_expected),
                 text::format("the reflection image read back: %d values not finite, %d pixels weighted", not_finite,
                              weighted));
    if (!read || test.samples != 0 || image.texels.size() != static_cast<std::size_t>(kColumns) * kRows) {
        return;
    }
    // (half floats: 11 bits - a weight within 1e-3 of the processor's, relative to it at most 0.2%)
    constexpr double kWeightSlack = 1.5e-3;
    const View view = view_from(kEye);
    int compared = 0;
    int wrong = 0;
    double worst = 0.0;
    double lowest_cosine = 1.0;
    double highest_cosine = 0.0;
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const Reflection here = reflection_at(view, column, row);
            bool clear = here.floor && here.hit && !(test.cover && here.cover == kCoverBlend);
            constexpr double kShift = 0.1;
            for (const auto& [dx, dy] : std::array<std::pair<double, double>, 4>{
                     {{kShift, 0.0}, {-kShift, 0.0}, {0.0, kShift}, {0.0, -kShift}}}) {
                clear = clear && same(here, reflection_at(view, column + dx, row + dy));
            }
            if (!clear) {
                continue;
            }
            const double want = weight_at(test, here);
            const double alpha = core::at(core::at(image.texels, (static_cast<std::size_t>(row) * kColumns) + column), 3);
            // (a rough pixel: some of its rays may miss the box - at most the weight, its sign negative)
            const bool rough = test.roughness > 0.0F;
            const double got = rough ? -alpha : alpha;
            const double error = rough ? std::max(0.0, got - want) : std::abs(got - want);
            ++compared;
            worst = std::max(worst, error);
            wrong += (error > kWeightSlack + (want * 2e-3) || (rough && alpha > 0.0)) ? 1 : 0;
            lowest_cosine = std::min(lowest_cosine, here.cosine);
            highest_cosine = std::max(highest_cosine, here.cosine);
        }
    }
    constexpr int kEnoughWeighed = 1500;
    tally->check(compared >= kEnoughWeighed && wrong == 0,
                 text::format("the weights against the processor's%s (cosines %.2f - %.2f): %d of %d wrong, worst %.5f",
                              test.roughness > 0.0F ? " (rough: at most it)" : "", lowest_cosine, highest_cosine,
                              wrong, compared, worst));
}

void run_mirror(render::Gpu& gpu, Mirror& mirror, const MirrorCase& test, Tally* tally)
{
    text::print("the mirror (%s, %s, strength %.2f, reflectivity %.2f%s):\n", test.name,
                test.samples == 0 ? "one sample" : "four samples", static_cast<double>(test.strength),
                static_cast<double>(test.reflectivity), test.fresnel ? ", Fresnel" : "");
    rt::Material floor = floor_surface(test.reflectivity, test.kind, test.mask, test.cover ? mirror.cover : nullptr);
    if (test.surface_f0 > 0.0F) {
        floor.texture = mirror.surface;
    }
    rt::ray_tracing()->set_material(mirror.floor_material, floor);
    Targets targets;
    const bool made = make_targets(gpu, test.samples, &targets);
    tally->check(made, "the targets made");
    if (made) {
        render::vulkan::RtReflectionSettings settings{
            .on = false, .strength = test.strength, .fresnel = test.fresnel, .glass_roughness = test.roughness,
            .glass_reflectance = test.glass_reflectance, .surfaces = test.surfaces};
        render::vulkan::set_rt_reflection_settings(settings);
        tally->check(draw_frame(gpu, targets, mirror, 1) == 0, "off: no pass");
        std::vector<uint8_t> without;
        const bool read_without = read_colour(gpu, targets, &without);
        settings.on = true;
        render::vulkan::set_rt_reflection_settings(settings);
        tally->check(draw_frame(gpu, targets, mirror, 2) == 1, "on: the listener recorded the passes");
        std::vector<uint8_t> with;
        const bool read_with = read_colour(gpu, targets, &with);
        tally->check(read_without && read_with, "the targets read back");
        if (read_without && read_with) {
            // (four samples: the depth read at sample 0 - Vulkan's standard place, 0.375, 0.125 in the pixel)
            constexpr Offset kSampleZero{-0.125, -0.375};
            compare_mirror(with, without, test.samples == 0 ? Offset{} : kSampleZero, test, tally);
            check_image(test, tally);
        }
    }
    release_targets(gpu, targets);
}

// --- (added for the skinned meshes' attributes) the box as a skinned mesh's vertices -------------------------------

// The box in the skinned meshes' vertex (format 0x152), its own vertex colour, each face's normal; the geometry's
// attributes from the format, as the frame record works them out - then given the scene file's offsets instead, as
// they once were: the comparison must fail.
void run_skinned(render::Gpu& gpu, Mirror& mirror, Tally* tally)
{
    text::print("the box as a skinned mesh's vertices (format %X, stride %u: the normal at %u, the colour at %u, the uv "
                "at %u):\n",
                kFvfSkinned, kSkinnedStride, kSkinnedNormal, kSkinnedColour, kSkinnedUv);
    TestMesh skinned;
    skinned.layout = {.indexed = true, .wide = false, .strip = false, .stride = kSkinnedStride, .position_offset = 0,
                      .uv_offset = kSkinnedUv, .colour_offset = kSkinnedColour, .leading_vertices = 1,
                      .leading_indices = 3, .normal_offset = kSkinnedNormal, .fvf = kFvfSkinned};
    skinned.data = box_data();
    constexpr uint32_t kFaceCorners = 4;
    for (std::size_t vertex = 0; vertex < skinned.data.positions.size(); ++vertex) {
        core::at(skinned.data.colours, vertex) = kSkinnedVertexColour;
        // (box_data's faces in order: -x, +x, -y, +y, -z, +z, four corners each)
        const std::size_t face = vertex / kFaceCorners;
        std::array<float, 3> normal{};
        core::at(normal, face / 2) = (face % 2) != 0 ? 1.0F : -1.0F;
        skinned.data.normals.push_back(normal);
    }
    const bool made = rt::ray_tracing() != nullptr && make_mesh(gpu, &skinned);
    const rt::Geometry& geometry = skinned.geometry;
    tally->check(made && geometry.normal_format == rt::kNormalFloat3 && geometry.normal_offset == kSkinnedNormal &&
                     geometry.colour_format == rt::kColourA8R8G8B8 && geometry.colour_offset == kSkinnedColour &&
                     geometry.uv_format == rt::kUvFloat2 && geometry.uv_offset == kSkinnedUv && geometry.uv_sets == 1,
                 text::format("the format's attributes: normal %u, colour %u, uv %u (%u set)", geometry.normal_offset,
                              geometry.colour_offset, geometry.uv_offset, geometry.uv_sets));
    if (made) {
        std::swap(mirror.box, skinned);
        const MirrorCase test{.name = "the skinned box", .samples = 0, .strength = 1.0F, .reflectivity = 1.0F,
                              .box_colour = kSkinnedVertexColour};
        run_mirror(gpu, mirror, test, tally);
        // (the scene file's offsets, as the geometry table was filled before: the colour read from the uv's bytes, the
        // uv past the vertex - left out, so (0, 0))
        rt::Geometry scene_file = mirror.box.geometry;
        scene_file.colour_offset = kSceneFileColour;
        scene_file.uv_offset = kSceneFileUv;
        rt::ray_tracing()->update_mesh(mirror.box.id, scene_file);
        text::print("  (the same box given the mesh's own offsets - colour %u, uv %u -, which must fail:)\n",
                    kSceneFileColour, kSceneFileUv);
        Tally wrong;
        run_mirror(gpu, mirror, test, &wrong);
        tally->check(wrong.failed > 0,
                     text::format("the mesh's own offsets don't reflect its colours: %d of their checks failed",
                                  wrong.failed));
        std::swap(mirror.box, skinned);
    }
    release_mesh(gpu, &skinned);
}

// The rough glass against the same glass as a mirror: what the rays see averaged over a cone and blurred - the light
// the reflection adds over the floor about the same (no energy made or lost but the rays that miss past the box's
// edges), no pixel black or NaN, the box's reflection softened (its texels' edges spread: the squared differences
// between neighbours smaller - a blur keeps their plain sum).
void run_rough(render::Gpu& gpu, Mirror& mirror, Tally* tally)
{
    constexpr float kRough = 0.08F;
    text::print("rough glass (roughness %.2f against a mirror; strength 1, Fresnel):\n", static_cast<double>(kRough));
    rt::ray_tracing()->set_material(mirror.floor_material, floor_surface(1.0F, rt::kReflectGlass));
    Targets targets;
    const bool made = make_targets(gpu, 0, &targets);
    tally->check(made, "the targets made");
    if (!made) {
        release_targets(gpu, targets);
        return;
    }
    std::array<std::vector<uint8_t>, 3> frames; // off, the mirror, rough
    const std::array<float, 3> roughness{0.0F, 0.0F, kRough};
    bool read = true;
    for (std::size_t k = 0; k < frames.size(); ++k) {
        render::vulkan::set_rt_reflection_settings(
            {.on = k != 0, .strength = 1.0F, .fresnel = true, .glass_roughness = core::at(roughness, k)});
        draw_frame(gpu, targets, mirror, k + 1);
        read = read && read_colour(gpu, targets, &core::at(frames, k));
    }
    tally->check(read, "the targets read back");
    if (read) {
        const View view = view_from(kEye);
        std::array<double, 2> added{}; // the mirror's, the rough's: the light each adds (all channels) over the floor
        double mirror_steps = 0.0;
        double rough_steps = 0.0;
        int black = 0;
        int floor_pixels = 0;
        for (int row = 1; row + 1 < kRows; ++row) {
            for (int column = 1; column + 1 < kColumns; ++column) {
                const Reflection here = reflection_at(view, column, row);
                if (!here.floor) {
                    continue;
                }
                ++floor_pixels;
                const Rgb off = pixel_at(frames.front(), column, row);
                const Rgb mirror_rgb = pixel_at(core::at(frames, 1), column, row);
                const Rgb rough_rgb = pixel_at(frames.back(), column, row);
                const Rgb mirror_next = pixel_at(core::at(frames, 1), column + 1, row);
                const Rgb rough_next = pixel_at(frames.back(), column + 1, row);
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    added.front() += core::at(mirror_rgb, channel) - core::at(off, channel);
                    added.back() += core::at(rough_rgb, channel) - core::at(off, channel);
                    mirror_steps += std::pow(core::at(mirror_next, channel) - core::at(mirror_rgb, channel), 2);
                    rough_steps += std::pow(core::at(rough_next, channel) - core::at(rough_rgb, channel), 2);
                }
                black += (rough_rgb == Rgb{} && off != Rgb{}) ? 1 : 0;
            }
        }
        const double ratio = std::abs(added.front()) > 0.0 ? added.back() / added.front() : 0.0;
        constexpr double kLeastShare = 0.8;
        constexpr double kMostShare = 1.05;
        tally->check(ratio >= kLeastShare && ratio <= kMostShare,
                     text::format("the light the rough reflection adds against the mirror's: %.3f of it (%.0f / %.0f "
                                  "over %d floor pixels)",
                                  ratio, added.back(), added.front(), floor_pixels));
        tally->check(rough_steps < mirror_steps,
                     text::format("softened: the squared steps between neighbours %.0f against the mirror's %.0f", rough_steps,
                                  mirror_steps));
        tally->check(black == 0, text::format("no floor pixel black: %d", black));
        check_image({.name = "rough", .strength = 1.0F, .fresnel = true, .kind = rt::kReflectGlass, .roughness = kRough},
                    tally);
    }
    release_targets(gpu, targets);
}

// Schlick's curve, which the pass uses, against the exact Fresnel reflectance of water (n 1.33) and glass (n 1.5) at
// angles from face-on to grazing (unpolarised light) - and the face-on values the pass takes (F0).
void check_fresnel_curve(Tally* tally)
{
    text::print("Fresnel reflectance (Schlick's against the exact):\n");
    constexpr double kWaterIndex = 1.333;
    constexpr double kGlassIndex = 1.5;
    constexpr int kAngles = 90;
    constexpr double kDegree = 3.14159265358979323846 / 180.0;
    constexpr double kCurveSlack = 0.06; // (Schlick's is known within a few hundredths of the exact)
    double worst = 0.0;
    for (const auto& [index, f0] : std::array<std::pair<double, double>, 2>{{{kWaterIndex, kWaterF0}, {kGlassIndex, kGlassF0}}}) {
        for (int degrees = 0; degrees < kAngles; ++degrees) {
            const double cosine = std::cos(degrees * kDegree);
            worst = std::max(worst, std::abs(schlick(f0, cosine) - fresnel_exact(index, cosine)));
        }
    }
    const double water0 = fresnel_exact(kWaterIndex, 1.0);
    const double glass0 = fresnel_exact(kGlassIndex, 1.0);
    constexpr double kF0Slack = 0.003;
    tally->check(std::abs(water0 - kWaterF0) < kF0Slack && std::abs(glass0 - kGlassF0) < kF0Slack && worst < kCurveSlack,
                 text::format("face-on: water %.4f (F0 %.2f), glass %.4f (F0 %.2f); 0 - 89 degrees: Schlick's within "
                              "%.4f of the exact",
                              water0, kWaterF0, glass0, kGlassF0, worst));
}

void run_untouched(render::Gpu& gpu, Mirror& mirror, const char* name, bool on, float reflectivity, Tally* tally)
{
    text::print("%s:\n", name);
    rt::ray_tracing()->set_material(mirror.floor_material, floor_surface(reflectivity));
    Targets targets;
    const bool made = make_targets(gpu, 0, &targets);
    tally->check(made, "the targets made");
    if (made) {
        render::vulkan::set_rt_reflection_settings({.on = false, .strength = 1.0F});
        draw_frame(gpu, targets, mirror, 1);
        std::vector<uint8_t> without;
        const bool read_without = read_colour(gpu, targets, &without);
        render::vulkan::set_rt_reflection_settings({.on = on, .strength = 1.0F});
        const uint64_t skipped = render::vulkan::rt_reflection_views_without_mirrors();
        tally->check(draw_frame(gpu, targets, mirror, 2) == 0, "no pass recorded");
        if (on) {
            tally->check(render::vulkan::rt_reflection_views_without_mirrors() == skipped + 1,
                         "the view counted as having nothing reflective");
        }
        std::vector<uint8_t> with;
        const bool read_with = read_colour(gpu, targets, &with);
        tally->check(read_without && read_with && with == without, "the frame exactly as without the pass");
    }
    release_targets(gpu, targets);
}

// (added for the surface reflections) A frame with rt_reflections off, then on: whether the pass ran and whether the
// frame changed.
struct OnOff {
    bool read = false;
    uint64_t passes = 0;
    bool same = false;
};
OnOff draw_off_on(render::Gpu& gpu, Mirror& mirror, const render::vulkan::RtReflectionSettings& on)
{
    OnOff out;
    Targets targets;
    if (make_targets(gpu, 0, &targets)) {
        render::vulkan::RtReflectionSettings off = on;
        off.on = false;
        render::vulkan::set_rt_reflection_settings(off);
        draw_frame(gpu, targets, mirror, 1);
        std::vector<uint8_t> without;
        const bool read_without = read_colour(gpu, targets, &without);
        render::vulkan::set_rt_reflection_settings(on);
        out.passes = draw_frame(gpu, targets, mirror, 2);
        std::vector<uint8_t> with;
        out.read = read_without && read_colour(gpu, targets, &with);
        out.same = out.read && with == without;
    }
    release_targets(gpu, targets);
    return out;
}

// (added for the surface reflections) The floor without a shine of its own (no environment pass), its white texture
// given a surface map (rt_pbr): a smooth glazed tile reflects the box by Fresnel at the map's F0 (the pass's weights
// against the processor's, as the mirrors'); a rough map's texels, a concrete one, rt_reflection_surfaces=0 - and a
// surface map with rt_pbr off - leave every pixel as drawn.
void run_surface(render::Gpu& gpu, Mirror& mirror, Tally* tally)
{
    namespace smf = surface_map_format;
    constexpr float kTileF0 = 0.5F;
    constexpr uint8_t kFlat = 128;
    constexpr uint8_t kRoughest = 255;
    text::print("surfaces glossy by their surface maps (no shine of their own):\n");
    render::vulkan::set_rt_pbr_settings({.on = true});
    const bool textured = make_texture(gpu, [](uint32_t, uint32_t) { return 0xFFFFFFFFU; }, &mirror.surface);
    const auto attach = [&](uint8_t material_class, uint8_t roughness) {
        std::vector<uint8_t> map(static_cast<std::size_t>(kTextureSide) * kTextureSide * 4);
        for (std::size_t i = 0; i < map.size(); i += 4) {
            map.at(i) = kFlat;
            map.at(i + 1) = kFlat;
            map.at(i + 2) = roughness;
            map.at(i + 3) = kFlat;
        }
        return render::surface::surface_maps() != nullptr &&
               render::surface::surface_maps()->attach(
                   mirror.surface, {.width = kTextureSide, .height = kTextureSide, .material_class = material_class,
                                    .f0 = kTileF0},
                   map);
    };
    const bool tile = textured && attach(static_cast<uint8_t>(smf::MaterialClass::kTile), 0);
    tally->check(tile, "a white texture with a smooth glazed tile's surface map");
    if (tile) {
        run_mirror(gpu, mirror, {.name = "a glazed tile by its surface map", .samples = 0, .strength = 0.5F,
                                 .reflectivity = 0.0F, .fresnel = true, .surface_f0 = kTileF0},
                   tally);
        rt::Material floor = floor_surface(0.0F);
        floor.texture = mirror.surface;
        const render::vulkan::RtReflectionSettings on{.on = true, .strength = 0.5F, .fresnel = true};
        // rt_reflection_surfaces=0: no change
        render::vulkan::RtReflectionSettings none = on;
        none.surfaces = 0.0F;
        rt::ray_tracing()->set_material(mirror.floor_material, floor);
        OnOff got = draw_off_on(gpu, mirror, none);
        tally->check(got.read && got.same, "rt_reflection_surfaces=0: every pixel as drawn");
        // a rough tile (its map's roughness 1): no reflection
        const bool rough = attach(static_cast<uint8_t>(smf::MaterialClass::kTile), kRoughest);
        rt::ray_tracing()->set_material(mirror.floor_material, floor);
        got = draw_off_on(gpu, mirror, on);
        tally->check(rough && got.read && got.passes == 1 && got.same,
                     "a rough tile (roughness 1): the pass ran, every pixel as drawn");
        // concrete: nothing reflective - no pass
        const bool concrete = attach(static_cast<uint8_t>(smf::MaterialClass::kConcrete), 0);
        rt::ray_tracing()->set_material(mirror.floor_material, floor);
        got = draw_off_on(gpu, mirror, on);
        tally->check(concrete && got.read && got.passes == 0 && got.same,
                     "smooth concrete: nothing reflective, no pass, every pixel as drawn");
        // rt_pbr off (no maps used): no pass
        attach(static_cast<uint8_t>(smf::MaterialClass::kTile), 0);
        render::vulkan::set_rt_pbr_settings({});
        rt::ray_tracing()->set_material(mirror.floor_material, floor);
        got = draw_off_on(gpu, mirror, on);
        tally->check(got.read && got.passes == 0 && got.same, "rt_pbr off: no pass, every pixel as drawn");
    }
    render::vulkan::set_rt_pbr_settings({});
    rt::ray_tracing()->set_material(mirror.floor_material, floor_surface(0.0F));
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
    render::vulkan::set_rt_reflection_settings({.on = true, .strength = 1.0F});
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
    if (!state.context.ray_tracing_enabled() || rt::ray_tracing() == nullptr) {
        text::print("this device has no ray queries (VK_KHR_acceleration_structure + VK_KHR_ray_query) - nothing to "
                    "check\n");
        context.shutdown();
        return Opened::kNoRayQueries;
    }
    return Opened::kYes;
}

// The sky-view table read bilinearly as the shaders read it (vulkan_rt_sky.cpp sky_view_linear: the azimuth from the
// sun's, clamped, texel centres at (i + 0.5) / size).
std::array<double, 3> sky_view_at(const render::vulkan::AtmosphereTexels& table, const Vec3& dir, const Vec3& sun)
{
    constexpr double kFlat = 1e-5;
    constexpr double kPi = 3.14159265358979323846;
    const double hs = std::hypot(x_of(sun), z_of(sun));
    const double hv = std::hypot(x_of(dir), z_of(dir));
    const double cosine =
        (hs > kFlat && hv > kFlat) ? ((x_of(sun) * x_of(dir)) + (z_of(sun) * z_of(dir))) / (hs * hv) : 1.0;
    const double azimuth = std::acos(std::clamp(cosine, -1.0, 1.0));
    const double elevation = std::asin(std::clamp(y_of(dir), -1.0, 1.0));
    double sign = 0.0;
    if (elevation != 0.0) {
        sign = elevation < 0.0 ? -1.0 : 1.0;
    }
    const double u = std::sqrt(azimuth / kPi);
    const double v = 0.5 + (0.5 * sign * std::sqrt(std::abs(elevation) / (kPi / 2.0)));
    const double x = std::clamp((u * table.width) - 0.5, 0.0, table.width - 1.0);
    const double y = std::clamp((v * table.height) - 0.5, 0.0, table.height - 1.0);
    const auto x0 = static_cast<uint32_t>(x);
    const auto y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = std::min(x0 + 1, table.width - 1);
    const uint32_t y1 = std::min(y0 + 1, table.height - 1);
    const double fx = x - x0;
    const double fy = y - y0;
    const auto at = [&table](uint32_t column, uint32_t row) -> const std::array<float, 4>& {
        return core::at(table.texels, (static_cast<std::size_t>(row) * table.width) + column);
    };
    std::array<double, 3> out{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const double top = (core::at(at(x0, y0), ch) * (1.0 - fx)) + (core::at(at(x1, y0), ch) * fx);
        const double bottom = (core::at(at(x0, y1), ch) * (1.0 - fx)) + (core::at(at(x1, y1), ch) * fx);
        core::at(out, ch) = std::max(0.0, (top * (1.0 - fy)) + (bottom * fy));
    }
    return out;
}

// rt_sky: the floor's pixels whose reflection misses the box (clear-cut: the same a tenth of a pixel round) show the
// sky-view table toward the reflected direction as the game's colour (^ 1 / 2.2, at most 1), the weight 1 (strength 1,
// reflectivity 1, no Fresnel) - the sky drawn, not the floor kept. The model's sky alone (no capture), no clouds.
void run_sky_misses(render::Gpu& gpu, Mirror& mirror, Tally* tally)
{
    namespace vk = render::vulkan;
    text::print("the misses with rt_sky:\n");
    rt::ray_tracing()->set_material(mirror.floor_material, floor_surface(1.0F));
    const Vec3 sun = normalised({0.4, 0.6, 0.8}); // (behind the camera: the reflected sky away from the sun's glow)
    const std::array<float, 3> toward = floats(sun);
    rt::set_sun_override(&toward);
    vk::RtSkySettings sky;
    sky.on = true;
    sky.match = 0.0F;
    sky.clouds = false;
    vk::set_rt_sky_settings(sky);
    vk::set_rt_reflection_settings({.on = true, .strength = 1.0F});
    Targets targets; // (made with the reflections on: their depth samplable)
    const bool made = make_targets(gpu, 0, &targets);
    vk::AtmosphereTexels table;
    bool ready = false;
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        vk::prepare_rt_sky(state);
        vk::finish_work(state);
        ready = vk::read_atmosphere_table(state, vk::AtmosphereTable::kSkyView, &table);
    }
    // (no sky drawn - a level with no sky scene: the misses keep what was there, the floor's own colour)
    std::vector<uint8_t> unseen;
    std::vector<uint8_t> plain;
    vk::set_rt_reflection_settings({});
    const bool read_plain = made && draw_frame(gpu, targets, mirror, 3, false) == 0 && read_colour(gpu, targets, &plain);
    vk::set_rt_reflection_settings({.on = true, .strength = 1.0F});
    const bool read_unseen =
        made && draw_frame(gpu, targets, mirror, 4, false) == 1 && read_colour(gpu, targets, &unseen);
    int kept = 0;
    int moved = 0;
    if (read_plain && read_unseen) {
        const View view = view_from(kEye);
        for (int row = 0; row < kRows; ++row) {
            for (int column = 0; column < kColumns; ++column) {
                const Reflection here = reflection_at(view, column, row);
                if (here.floor && !here.hit) {
                    const bool same_pixel = close_to(pixel_at(unseen, column, row), pixel_at(plain, column, row));
                    kept += same_pixel ? 1 : 0;
                    moved += same_pixel ? 0 : 1;
                }
            }
        }
    }
    tally->check(read_plain && read_unseen && kept > 0 && moved * 100 <= kept,
                 text::format("rt_sky on but no sky drawn (indoors): the misses unchanged (%d kept, %d changed)", kept,
                              moved));
    tally->check(made && ready && draw_frame(gpu, targets, mirror, 5) == 1,
                 "the sky made (rt_sky on) and the passes recorded");
    std::vector<uint8_t> with;
    if (made && ready && read_colour(gpu, targets, &with)) {
        const View view = view_from(kEye);
        constexpr double kShift = 0.1;
        constexpr int kSlack = 3; // (8-bit, the table's 16-bit floats filtered)
        constexpr int kEnoughMisses = 2000;
        int compared = 0;
        int unclipped = 0; // (the sky under white there: a real comparison)
        int wrong = 0;
        std::string first;
        for (int row = 0; row < kRows; ++row) {
            for (int column = 0; column < kColumns; ++column) {
                const Reflection here = reflection_at(view, column, row);
                if (!here.floor || here.hit) {
                    continue;
                }
                bool clear = true;
                for (const auto& [dx, dy] : std::array<std::pair<double, double>, 4>{
                         {{kShift, 0.0}, {-kShift, 0.0}, {0.0, kShift}, {0.0, -kShift}}}) {
                    clear = clear && same(reflection_at(view, column + dx, row + dy), here);
                }
                if (!clear) {
                    continue;
                }
                const std::array<double, 3> linear = sky_view_at(table, here.mirrored, sun);
                constexpr double kDisplayGamma = 2.2;
                constexpr double kWhite = 255.0;
                Rgb want{};
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    core::at(want, ch) = static_cast<int>(
                        std::lround(std::min(1.0, std::pow(core::at(linear, ch), 1.0 / kDisplayGamma)) * kWhite));
                }
                const Rgb got = pixel_at(with, column, row);
                ++compared;
                constexpr int kNotWhite = 250;
                unclipped += std::ranges::any_of(want, [](int value) { return value < kNotWhite; }) ? 1 : 0;
                bool matches = true;
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    matches = matches && std::abs(core::at(got, ch) - core::at(want, ch)) <= kSlack;
                }
                if (!matches) {
                    if (wrong == 0) {
                        first = text::format(" (first at %d, %d: %s, wanted %s)", column, row, rgb_text(got).c_str(),
                                             rgb_text(want).c_str());
                    }
                    ++wrong;
                }
            }
        }
        tally->check(compared >= kEnoughMisses && unclipped * 2 > compared && wrong == 0,
                     text::format("every clear-cut miss the sky toward its reflection: %d compared (%d under white), "
                                  "%d wrong%s",
                                  compared, unclipped, wrong, first.c_str()));
    }
    vk::set_rt_sky_settings({});
    rt::set_sun_override(nullptr);
    release_targets(gpu, targets);
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
    Tally tally;
    render::Gpu& gpu = render::vulkan::gpu();
    run_geometry(gpu, options.seed, &tally);
    Mirror mirror;
    const bool made = make_mirror(gpu, &mirror);
    tally.check(made, "the mirror scene made (a floor, a textured box, their materials)");
    if (made) {
        constexpr uint32_t kFourSamples = 4;
        constexpr float kHalf = 0.5F;
        constexpr float kMostlyShiny = 0.8F;
        // (Fresnel off: the same weight at every angle, as the first version)
        run_mirror(gpu, mirror, {.name = "no Fresnel", .samples = 0, .strength = 1.0F, .reflectivity = 1.0F}, &tally);
        run_mirror(gpu, mirror, {.name = "no Fresnel", .samples = kFourSamples, .strength = 1.0F, .reflectivity = 1.0F},
                   &tally);
        run_mirror(gpu, mirror, {.name = "no Fresnel", .samples = 0, .strength = kHalf, .reflectivity = kMostlyShiny},
                   &tally);
        // (Fresnel: each pixel's weight from the angle it is seen at - the processor's Schlick)
        check_fresnel_curve(&tally);
        run_mirror(gpu, mirror,
                   {.name = "water", .samples = 0, .strength = kHalf, .reflectivity = 1.0F, .fresnel = true,
                    .kind = rt::kReflectWater},
                   &tally);
        run_mirror(gpu, mirror,
                   {.name = "glass as a mirror", .samples = kFourSamples, .strength = kHalf,
                    .reflectivity = kMostlyShiny, .fresnel = true, .kind = rt::kReflectGlass},
                   &tally);
        constexpr float kRealGlass = 0.04F;
        run_mirror(gpu, mirror,
                   {.name = "glass at real glass's F0", .samples = 0, .strength = kHalf, .reflectivity = 1.0F,
                    .fresnel = true, .kind = rt::kReflectGlass, .glass_reflectance = kRealGlass},
                   &tally);
        run_mirror(gpu, mirror,
                   {.name = "shiny", .samples = 0, .strength = kHalf, .reflectivity = kMostlyShiny, .fresnel = true},
                   &tally);
        // (the mask: the floor's white texture is opaque - a reflection where the base is opaque everywhere, none where
        // it is clear)
        run_mirror(gpu, mirror,
                   {.name = "glass where the base is opaque", .samples = 0, .strength = kHalf, .reflectivity = 1.0F,
                    .fresnel = true, .kind = rt::kReflectGlass, .mask = rt::kReflectWhereBaseOpaque},
                   &tally);
        run_mirror(gpu, mirror,
                   {.name = "glass where the base is clear", .samples = 0, .strength = kHalf, .reflectivity = 1.0F,
                    .fresnel = true, .kind = rt::kReflectGlass, .mask = rt::kReflectWhereBaseClear},
                   &tally);
        // (a wall texture drawn over the environment pass by its alpha - BO's buildings: a reflection only through its
        // clear texels (the windows), read with the floor's second texture coordinate set; with Fresnel and without)
        run_mirror(gpu, mirror,
                   {.name = "glass under a cover, its clear texels", .samples = 0, .strength = kHalf,
                    .reflectivity = 1.0F, .fresnel = true, .kind = rt::kReflectGlass, .cover = true},
                   &tally);
        run_mirror(gpu, mirror,
                   {.name = "under a cover, no Fresnel", .samples = 0, .strength = 1.0F, .reflectivity = 1.0F,
                    .cover = true},
                   &tally);
        run_rough(gpu, mirror, &tally);
        run_untouched(gpu, mirror, "nothing reflective: the pass skipped", true, 0.0F, &tally);
        run_untouched(gpu, mirror, "rt_reflections off: no pass", false, 1.0F, &tally);
        run_sky_misses(gpu, mirror, &tally);
        run_skinned(gpu, mirror, &tally);
        run_surface(gpu, mirror, &tally); // (added for the surface reflections)
    }
    release_mirror(gpu, &mirror);
    render::vulkan::set_rt_reflection_settings({});
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::finish_work(state);
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
    set_log_listener(print_log_line);
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rt_reflections_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan ray-traced reflections check", kWindowWidth, kWindowHeight,
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
