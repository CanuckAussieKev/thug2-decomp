// NOT ORIGINAL - thug2_vulkan_rt_relit_check.exe: the relit mode's analysis (engine/gfx/rt_relit.h, [Graphics] rt_relit)
// tried on this machine against a level whose bake is known.
//
// The level: a ground of 24 x 24 quads, a building and a pillar standing on it (their faces' own vertices, each mesh
// a vertex buffer and an index buffer made through render::Gpu, registered with the Vulkan RayTracing as render_pc's
// frame record registers the world's), a degenerate triangle and an unused vertex among them. Every vertex has a known
// tint (the ground's with a red-painted square, the building's, the pillar's) and the "bake" is made here on the
// processor: a ray tracer of the processor's own (render::rt::RayTracing over the same triangles, double precision)
// runs the analysis's own tracing (trace_relit_light: the same rays) for the level's sun, and the baked colour is
// tint x (a S + b K + g B) - a lamp's light added round a point in the building's shadow - rounded to bytes as the
// game's are (128: 1). The bounce B reads the baked colours themselves, so the bake is iterated until it holds still.
// Then the analysis runs as the game runs it, its rays through the Vulkan RayTracing (ray queries), and:
//   - its traced light (the sun's and the sky's shares, the bounce) is the processor's for the same rays;
//   - each channel's sky and bounce against its sun are the bake's, and the tints (up to the factor per channel the
//     colours can't tell from the light) the known ones, the red square told from the ground round it;
//   - the lamp's light comes back as the residual where it was added (its pool's middle in full, most of it overall),
//     and next to none elsewhere;
//   - relit at the bake's own sun (strength 1, detail 1, the residual kept) the colours are the bake's within a byte;
//     strength 0 gives the bake exactly;
//   - relit by another sun (the sun's rays traced again on the device) the colours are those of a bake made here for
//     that sun with the same tints and lamp;
//   - the game's own guard: rt_sun.h's fit finds the bake's sun in the level, and none in a level whose bake has no sun
//     (a, the sun's light, 0: indoors - it keeps its bake);
//   - no value is NaN or infinite; the per-level JSON reads as the game reads it; no validation message.
//   thug2_vulkan_rt_relit_check [--device N] [--display N] [--no-validation] [--perf]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan ray tracing for it.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/relit_data.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace value = render::gpu_value;
namespace rt = render::rt;
using rt::Rgbf;
using rt::Vec3;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x042; // XYZ | DIFFUSE
constexpr uint32_t kStride = 16; // the position, the colour at 12
constexpr uint32_t kColourOffset = 12;
constexpr uint32_t kFormatIndex32 = 102; // D3DFMT_INDEX32
constexpr double kEpsilon = 1e-9;
constexpr int kBakeRounds = 8; // the bake iterated this often (its bounce reads its own colours)
constexpr float kBytes = 128.0F;
constexpr float kMaxByte = 255.0F;
constexpr int kRedShift = 16;
constexpr int kGreenShift = 8;
constexpr uint32_t kOpaqueAlpha = 0xFF000000U;
constexpr uint32_t kByteMask = 0xFF;
constexpr rt::MeshId kCpuIds = 0x7F000000U; // (the processor's meshes' ids: never the device's)
constexpr float kLumaRed = 0.299F;
constexpr float kLumaGreen = 0.587F;
constexpr float kLumaBlue = 0.114F;
constexpr float kTiny = 1e-6F;
constexpr double kTinyDouble = 1e-9;

// The level: a ground of kGroundQuads x kGroundQuads quads, a building and a pillar (sunk into the ground, so that no
// ray starts on its plane), an unused vertex under the ground.
constexpr uint32_t kGroundQuads = 24;
constexpr float kGroundHalf = 1200.0F;
constexpr uint32_t kWallSteps = 6; // a face's quads a side
constexpr Vec3 kUnusedVertex{0.0F, -50.0F, 0.0F};
constexpr Vec3 kBuildingLow{-450.0F, -40.0F, -450.0F};
constexpr Vec3 kBuildingSize{400.0F, 360.0F, 360.0F};
constexpr Vec3 kPillarLow{350.0F, -40.0F, -150.0F};
constexpr Vec3 kPillarSize{90.0F, 460.0F, 90.0F};
constexpr std::size_t kGround = 0; // (the meshes' places)
constexpr std::size_t kBuilding = 1;
constexpr std::size_t kPillar = 2;
// The tints: the ground's (a red-painted square of it, x and z from - to), the building's, the pillar's.
constexpr Rgbf kGroundTint{0.90F, 0.86F, 0.76F};
constexpr Rgbf kRedTint{1.30F, 0.62F, 0.50F};
constexpr std::array<float, 2> kRedSquare{200.0F, 700.0F};
constexpr Rgbf kBuildingTint{0.82F, 0.80F, 0.92F};
constexpr Rgbf kPillarTint{0.62F, 0.62F, 0.60F};

// The bake's light: the sun's colour (a), the sky's (b), the bounce's weight (g); the sun and the moved sun; the lamp -
// in the building's shadow (the building's top throws it about 250 x and 200 z away from the sun), its light falling
// off to nothing at kLampRadius.
constexpr Rgbf kSunLight{1.00F, 0.95F, 0.85F};
constexpr Rgbf kSkyLight{0.35F, 0.40F, 0.50F};
constexpr Rgbf kBounceWeight{0.5F, 0.5F, 0.5F};
constexpr Vec3 kSunToward{0.55F, 0.70F, 0.45F};
constexpr Vec3 kMovedSun{-0.60F, 0.55F, 0.20F};
constexpr Rgbf kLampLight{0.70F, 0.55F, 0.30F};
constexpr Vec3 kLampAt{-600.0F, 0.0F, -550.0F};
constexpr float kLampRadius = 400.0F;

// What the checks allow.
constexpr float kLampShadedLeast = 0.2F;       // a ground vertex lit this much (luma) by the lamp in the shadow ...
constexpr std::size_t kLampShadedVertices = 4; // ... this many of them, at least
constexpr uint64_t kWarmFrames = 3;            // frames before tracing (the BLASes built and compacted)
constexpr float kOnPlane = 1e-3F;
constexpr float kSameShare = 1e-4F; // a sun's / sky's share the same
constexpr float kBounceSlack = 0.02F;
constexpr double kTracedShare = 0.99;     // of the vertices with the processor's light
constexpr float kSkySlack = 0.08F;        // the sky over the sun, relative
constexpr float kBounceRatioSlack = 0.2F; // the bounce over the sun, relative
constexpr float kTintSlack = 0.06F;       // a tint, relative ...
constexpr float kTintFloor = 0.02F;       // ... and absolute
constexpr double kTintsShare = 0.95;
constexpr float kContrastSlack = 0.15F;
constexpr float kLampSome = 0.25F;        // the lamp at least this share of the bake: its light counted
constexpr float kLampMiddle = 0.4F;       // the pool's middle (the lamp's luma above this) ...
constexpr float kLampMiddleSlack = 0.15F; // ... back within this share
constexpr double kLampEnergy = 0.4;       // of the counted lamp light back as the residual, at least
constexpr double kResidualElsewhere = 0.01;
constexpr double kWithinByteShare = 0.97; // relit at the bake's sun: within a byte
constexpr int kMovedBytes = 4;            // relit by the moved sun: within this many bytes ...
constexpr double kMovedShare = 0.95;      // ... at this share of the vertices
constexpr int kChangedBytes = 8;          // the moved sun's bake changed more than this ...
constexpr std::size_t kChangedPart = 10;  // ... at more than one vertex in this many
constexpr float kUpFacing = 0.7F;         // the baked sun's samples: triangles this much up (rt_sun.h)
constexpr float kSunFoundDegrees = 15.0F; // the bake's sun found within this of the one it was made with
constexpr float kDegreesPerRadian = 57.29578F;
constexpr float kThird = 1.0F / 3.0F;

// --perf: a level of the game's size.
constexpr uint32_t kPerfTiles = 12;
constexpr float kPerfTile = 2000.0F;
constexpr uint32_t kPerfTileQuads = 40;
constexpr uint32_t kGrey = 0xFF808080U;
constexpr uint64_t kPerfFrames = 40; // (after the asked traces' frames)
constexpr uint64_t kAskedFrame = kWarmFrames + 1; // (the asked trace's frames: up to kPerfFrames)
constexpr uint64_t kAfterPerfFrame = 50;
constexpr uint64_t kLastFrame = 60;

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
    bool perf = false;
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t iv = 1; iv < arguments.size(); ++iv) {
        const std::string_view argument = core::at(arguments, iv);
        const bool has_value = iv + 1 < arguments.size();
        if (argument == "--no-validation") {
            options.validation = false;
        } else if (argument == "--perf") {
            options.perf = true;
        } else if (argument == "--device" && has_value) {
            options.device = text::to_int(core::at(arguments, ++iv));
        } else if (argument == "--display" && has_value) {
            options.display = text::to_int(core::at(arguments, ++iv));
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

float luma(const Rgbf& colour)
{
    return (kLumaRed * std::get<0>(colour)) + (kLumaGreen * std::get<1>(colour)) + (kLumaBlue * std::get<2>(colour));
}

Rgbf times_rgb(const Rgbf& one, const Rgbf& other)
{
    return {std::get<0>(one) * std::get<0>(other), std::get<1>(one) * std::get<1>(other),
            std::get<2>(one) * std::get<2>(other)};
}

Vec3 unit(const Vec3& vi)
{
    const float length = std::sqrt(rt::dot(vi, vi));
    return rt::scaled(vi, 1.0F / length);
}

uint32_t to_argb(const Rgbf& light)
{
    std::array<uint32_t, 3> bytes{};
    for (std::size_t ch = 0; ch < bytes.size(); ++ch) {
        core::at(bytes, ch) =
            static_cast<uint32_t>(std::lround(std::clamp(core::at(light, ch) * kBytes, 0.0F, kMaxByte)));
    }
    return kOpaqueAlpha | (core::at(bytes, 0) << kRedShift) | (core::at(bytes, 1) << kGreenShift) | core::at(bytes, 2);
}

// --- the level -------------------------------------------------------------------------------------------------------

struct Level {
    std::vector<rt::RelitMesh> meshes;
    std::vector<std::vector<Rgbf>> tints; // the known ones
    std::vector<std::vector<Rgbf>> lamps; // the lamp's light added at each vertex
};

// A quad's vertices (counterclockwise seen from its front, so (b - a) x (c - a) points out) as `steps` x `steps`
// quads, added to `mesh`.
struct Face {
    Vec3 corner{};
    Vec3 across{};
    Vec3 up{};
};
void add_face(rt::RelitMesh* mesh, const Face& face, uint32_t steps)
{
    const auto first = static_cast<uint32_t>(mesh->positions.size());
    for (uint32_t row = 0; row <= steps; ++row) {
        for (uint32_t column = 0; column <= steps; ++column) {
            const float u = static_cast<float>(column) / static_cast<float>(steps);
            const float v = static_cast<float>(row) / static_cast<float>(steps);
            mesh->positions.push_back(
                rt::plus(face.corner, rt::plus(rt::scaled(face.across, u), rt::scaled(face.up, v))));
        }
    }
    const uint32_t stride = steps + 1;
    for (uint32_t row = 0; row < steps; ++row) {
        for (uint32_t column = 0; column < steps; ++column) {
            const uint32_t corner = first + (row * stride) + column;
            mesh->triangles.push_back({corner, corner + 1, corner + stride + 1});
            mesh->triangles.push_back({corner, corner + stride + 1, corner + stride});
        }
    }
}

// A box standing on the ground: four walls and a roof, each face its own vertices (counterclockwise from outside:
// across x up points out).
rt::RelitMesh box(const Vec3& low, const Vec3& size)
{
    rt::RelitMesh mesh;
    const auto [x, y, z] = low;
    const auto [width, height, depth] = size;
    add_face(&mesh, {.corner = {x, y, z}, .across = {0, 0, depth}, .up = {0, height, 0}}, kWallSteps); // -x
    add_face(&mesh, {.corner = {x + width, y, z + depth}, .across = {0, 0, -depth}, .up = {0, height, 0}},
             kWallSteps);                                                                                       // +x
    add_face(&mesh, {.corner = {x + width, y, z}, .across = {-width, 0, 0}, .up = {0, height, 0}}, kWallSteps); // -z
    add_face(&mesh, {.corner = {x, y, z + depth}, .across = {width, 0, 0}, .up = {0, height, 0}}, kWallSteps);  // +z
    add_face(&mesh, {.corner = {x, y + height, z}, .across = {0, 0, depth}, .up = {width, 0, 0}}, kWallSteps);  // roof
    return mesh;
}

bool in_red_square(const Vec3& point)
{
    const auto [low, high] = kRedSquare;
    return rt::x_of(point) > low && rt::x_of(point) < high && rt::z_of(point) > low && rt::z_of(point) < high;
}

Level make_level()
{
    Level level;
    rt::RelitMesh ground;
    add_face(
        &ground,
        {.corner = {-kGroundHalf, 0, -kGroundHalf}, .across = {0, 0, 2 * kGroundHalf}, .up = {2 * kGroundHalf, 0, 0}},
        kGroundQuads);
    // (a degenerate triangle and an unused vertex: nothing may break on them)
    ground.triangles.push_back({0, 0, 1});
    ground.positions.push_back(kUnusedVertex);
    level.meshes.push_back(std::move(ground));
    level.meshes.push_back(box(kBuildingLow, kBuildingSize));
    level.meshes.push_back(box(kPillarLow, kPillarSize));
    for (std::size_t index = 0; index < level.meshes.size(); ++index) {
        std::vector<Rgbf> tints;
        std::vector<Rgbf> lamps;
        for (const Vec3& point : core::at(level.meshes, index).positions) {
            Rgbf tint = kGroundTint;
            if (index == kBuilding) {
                tint = kBuildingTint;
            } else if (index == kPillar) {
                tint = kPillarTint;
            } else if (in_red_square(point)) {
                tint = kRedTint;
            }
            tints.push_back(tint);
            const Vec3 off = rt::minus(point, kLampAt);
            const float fall = std::max(0.0F, 1.0F - (std::sqrt(rt::dot(off, off)) / kLampRadius));
            lamps.push_back(rt::scaled(kLampLight, fall * fall));
        }
        level.tints.push_back(std::move(tints));
        level.lamps.push_back(std::move(lamps));
    }
    return level;
}

// --- a ray tracer on the processor -------------------------------------------------------------------------------------

using Double3 = std::array<double, 3>;
Double3 widened(const Vec3& v) { return {rt::x_of(v), rt::y_of(v), rt::z_of(v)}; }
Double3 minus(const Double3& a, const Double3& b)
{
    return {std::get<0>(a) - std::get<0>(b), std::get<1>(a) - std::get<1>(b), std::get<2>(a) - std::get<2>(b)};
}
double dot(const Double3& a, const Double3& b)
{
    return (std::get<0>(a) * std::get<0>(b)) + (std::get<1>(a) * std::get<1>(b)) + (std::get<2>(a) * std::get<2>(b));
}
Double3 cross(const Double3& a, const Double3& b)
{
    return {(std::get<1>(a) * std::get<2>(b)) - (std::get<2>(a) * std::get<1>(b)),
            (std::get<2>(a) * std::get<0>(b)) - (std::get<0>(a) * std::get<2>(b)),
            (std::get<0>(a) * std::get<1>(b)) - (std::get<1>(a) * std::get<0>(b))};
}

// A ray against a triangle (Moller-Trumbore, double precision): its distance, or a negative value for a miss.
struct Ray {
    Double3 origin{};
    Double3 direction{};
};
double hit_distance(const std::array<Double3, 3>& corners, const Ray& ray)
{
    const Double3& origin = ray.origin;
    const Double3& direction = ray.direction;
    constexpr double kMiss = -1.0;
    const auto& [first, second, third] = corners;
    const Double3 edge1 = minus(second, first);
    const Double3 edge2 = minus(third, first);
    const Double3 across = cross(direction, edge2);
    const double det = dot(edge1, across);
    if (std::abs(det) < kEpsilon) {
        return kMiss;
    }
    const double inverse = 1.0 / det;
    const Double3 from = minus(origin, first);
    const double u = dot(from, across) * inverse;
    if (u < 0.0 || u > 1.0) {
        return kMiss;
    }
    const Double3 up = cross(from, edge1);
    const double v = dot(direction, up) * inverse;
    if (v < 0.0 || u + v > 1.0) {
        return kMiss;
    }
    return dot(edge2, up) * inverse;
}

class CpuTracer final : public rt::RayTracing {
public:
    explicit CpuTracer(std::span<const rt::RelitMesh> meshes) : meshes_(meshes) {}
    [[nodiscard]] bool available() const override { return true; }
    void add_mesh(rt::MeshId mesh, const rt::Geometry& geometry) override { (void)mesh, (void)geometry; }
    void remove_mesh(rt::MeshId mesh) override { (void)mesh; }
    void mesh_changed(rt::MeshId mesh) override { (void)mesh; }
    void begin_frame(const rt::FrameRecord& record) override { (void)record; }
    void probe_many(std::span<const rt::ProbeRequest> rays, std::vector<rt::ProbeResult>* results) override
    {
        results->assign(rays.size(), rt::ProbeResult{});
        for (std::size_t index = 0; index < rays.size(); ++index) {
            core::at(*results, index) = trace(core::at(rays, index));
        }
    }

private:
    [[nodiscard]] rt::ProbeResult trace(const rt::ProbeRequest& ray) const
    {
        rt::ProbeResult out{.traced = true};
        const Double3 origin = widened(ray.origin);
        const Double3 direction = widened(unit(ray.direction));
        double best = ray.max_distance;
        for (std::size_t mesh_index = 0; mesh_index < meshes_.size(); ++mesh_index) {
            const rt::RelitMesh& mesh = core::at(meshes_, mesh_index);
            for (std::size_t triangle = 0; triangle < mesh.triangles.size(); ++triangle) {
                const auto [one, two, three] = core::at(mesh.triangles, triangle);
                const double distance =
                    hit_distance({widened(core::at(mesh.positions, one)), widened(core::at(mesh.positions, two)),
                                  widened(core::at(mesh.positions, three))},
                                 {.origin = origin, .direction = direction});
                if (distance > 0.0 && distance < best) {
                    best = distance;
                    out.hit = true;
                    out.distance = static_cast<float>(distance);
                    out.instance = static_cast<uint32_t>(mesh_index);
                    out.mesh = mesh.id;
                    out.primitive = static_cast<uint32_t>(triangle);
                }
            }
        }
        return out;
    }

    std::span<const rt::RelitMesh> meshes_;
};

// The bake made on the processor: colours = tint x (a S + b K + g B) + lamp, iterated (B reads the colours). The
// sun's light (a) zero: a level without a sun.
struct BakeSun {
    Vec3 toward{};
    Rgbf light{};
    Rgbf sky = kSkyLight; // (added by the time of day: a dimmer sky's)
};
std::vector<rt::RelitVertices> bake(Level* level, const BakeSun& sun, const rt::RelitParams& params)
{
    // (start from the tints alone; ids of the processor's own, so its hits find their meshes)
    for (std::size_t index = 0; index < level->meshes.size(); ++index) {
        rt::RelitMesh& mesh = core::at(level->meshes, index);
        mesh.id = kCpuIds + static_cast<rt::MeshId>(index);
        mesh.colours.resize(mesh.positions.size());
        for (std::size_t vertex = 0; vertex < mesh.positions.size(); ++vertex) {
            core::at(mesh.colours, vertex) = to_argb(core::at(core::at(level->tints, index), vertex));
        }
    }
    CpuTracer cpu(level->meshes);
    std::vector<rt::RelitVertices> light;
    rt::RelitStats stats;
    rt::RelitModel truth;
    truth.valid = true;
    truth.toward = unit(sun.toward);
    truth.sun = sun.light;
    truth.sky = sun.sky;
    truth.bounce = kBounceWeight;
    for (int round = 0; round < kBakeRounds; ++round) {
        // (the bounce again from the colours now: the hits are the same, only what they bring changes)
        rt::trace_relit_light(level->meshes, {}, sun.toward, params, cpu, &light, &stats);
        for (std::size_t index = 0; index < level->meshes.size(); ++index) {
            rt::RelitMesh& mesh = core::at(level->meshes, index);
            for (std::size_t vertex = 0; vertex < mesh.positions.size(); ++vertex) {
                const Rgbf lit =
                    rt::relit_light(truth, core::at(core::at(light, index), vertex), {.toward = truth.toward});
                const Rgbf& tint = core::at(core::at(level->tints, index), vertex);
                const Rgbf& lamp = core::at(core::at(level->lamps, index), vertex);
                Rgbf baked{};
                for (std::size_t channel = 0; channel < baked.size(); ++channel) {
                    core::at(baked, channel) =
                        (core::at(tint, channel) * core::at(lit, channel)) + core::at(lamp, channel);
                }
                core::at(mesh.colours, vertex) = to_argb(baked);
            }
        }
    }
    return light;
}

// --- the device's meshes -----------------------------------------------------------------------------------------------

struct DeviceMesh {
    render::GpuVertexBuffer* vertices = nullptr;
    render::GpuIndexBuffer* indices = nullptr;
    rt::MeshId id = 0;
};

bool make_device_mesh(render::Gpu& gpu, rt::RelitMesh* mesh, DeviceMesh* out)
{
    std::vector<uint8_t> bytes(mesh->positions.size() * kStride);
    for (std::size_t vi = 0; vi < mesh->positions.size(); ++vi) {
        std::memcpy(&core::at(bytes, vi * kStride), core::at(mesh->positions, vi).data(), sizeof(Vec3));
        const uint32_t colour = mesh->colours.empty() ? kOpaqueAlpha : core::at(mesh->colours, vi);
        std::memcpy(&core::at(bytes, (vi * kStride) + kColourOffset), &colour, sizeof(colour));
    }
    std::vector<uint32_t> indices;
    for (const auto& [pa, pb, ch] : mesh->triangles) {
        indices.insert(indices.end(), {pa, pb, ch});
    }
    void* data = nullptr;
    if (gpu.create_vertex_buffer(static_cast<uint32_t>(bytes.size()), 0, kFvf, value::kPoolDefault, &out->vertices) !=
            value::kResultOk ||
        gpu.lock_vertex_buffer(out->vertices, 0, 0, &data, 0) != value::kResultOk) {
        return false;
    }
    std::memcpy(data, bytes.data(), bytes.size());
    if (gpu.unlock_vertex_buffer(out->vertices) != value::kResultOk ||
        gpu.create_index_buffer(static_cast<uint32_t>(indices.size() * sizeof(uint32_t)), 0, kFormatIndex32,
                                value::kPoolDefault, &out->indices) != value::kResultOk ||
        gpu.lock_index_buffer(out->indices, 0, 0, &data, 0) != value::kResultOk) {
        return false;
    }
    std::memcpy(data, indices.data(), indices.size() * sizeof(uint32_t));
    if (gpu.unlock_index_buffer(out->indices) != value::kResultOk) {
        return false;
    }
    out->id = rt::next_mesh_id();
    mesh->id = out->id;
    rt::ray_tracing()->add_mesh(out->id,
                                {.vertices = out->vertices,
                                 .vertex_stride = kStride,
                                 .vertex_count = static_cast<uint32_t>(mesh->positions.size()),
                                 .indices = out->indices,
                                 .index_count = static_cast<uint32_t>(indices.size()),
                                 .indices_32bit = true,
                                 .vertex_buffer_bytes = static_cast<uint32_t>(bytes.size()),
                                 .index_buffer_bytes = static_cast<uint32_t>(indices.size() * sizeof(uint32_t)),
                                 .cpu_indices = nullptr,
                                 .colour_offset = kColourOffset,
                                 .colour_format = rt::kColourA8R8G8B8});
    return true;
}

void release_device_mesh(render::Gpu& gpu, DeviceMesh* mesh)
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
    *mesh = {};
}

void frame(render::Gpu& gpu, std::span<const DeviceMesh> meshes, uint64_t number)
{
    rt::FrameRecord record;
    record.frame = number;
    for (const DeviceMesh& mesh : meshes) {
        rt::Instance instance{.mesh = mesh.id, .flags = rt::kWorld | rt::kOpaque | rt::kCastsShadow};
        instance.object_to_world = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};
        record.instances.push_back(instance);
    }
    rt::ray_tracing()->begin_frame(record);
    gpu.begin_scene();
    gpu.end_scene();
    gpu.present();
}

bool finite_vertex(const rt::RelitVertex& vertex)
{
    const auto finite3 = [](const std::array<float, 3>& values) {
        return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
    };
    return finite3(vertex.normal) && std::isfinite(vertex.sun) && std::isfinite(vertex.sky) && finite3(vertex.bounce) &&
           finite3(vertex.tint) && finite3(vertex.residual);
}

int channel_difference(uint32_t one, uint32_t other)
{
    int most = 0;
    for (const int shift : {0, kGreenShift, kRedShift}) {
        most = std::max(most, std::abs(static_cast<int>((one >> shift) & kByteMask) -
                                       static_cast<int>((other >> shift) & kByteMask)));
    }
    return most;
}

double share(std::size_t part, std::size_t whole)
{
    return whole == 0 ? 0.0 : static_cast<double>(part) / static_cast<double>(whole);
}

double milliseconds_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// A wall's lowest vertices start their rays on the ground's plane: whether a ray going down meets it at 0 is too close
// to call (the level's walls are sunk into the ground so that none does).
bool on_plane(const Vec3& position, const rt::RelitVertex& vertex)
{
    return std::abs(rt::y_of(position)) < kOnPlane && std::abs(rt::y_of(vertex.normal)) < kOnPlane;
}

// --- the checks ------------------------------------------------------------------------------------------------------

struct Scene {
    Level level;
    std::vector<rt::RelitVertices> truth; // the processor's light for the bake's sun
    rt::RelitParams params{.sky_rays = rt::kRelitSkyRays, .sun_rays = rt::kRelitSunRays, .detail = 1.0F};
    Vec3 sun{};
};

// 1. the traced light: the device's against the processor's, the same rays
void check_traced(const Scene& scene, rt::RayTracing& backend, Tally* tally, std::vector<rt::RelitVertices>* light)
{
    rt::RelitStats stats;
    const auto start = std::chrono::steady_clock::now();
    rt::trace_relit_light(scene.level.meshes, {}, scene.sun, scene.params, backend, light, &stats);
    text::print("  (the analysis's rays on the device: %zu vertices, %zu rays, %.1f ms)\n", stats.vertices, stats.rays,
                milliseconds_since(start));
    std::size_t vertices = 0;
    std::size_t same_sun = 0;
    std::size_t same_sky = 0;
    std::size_t same_bounce = 0;
    std::size_t too_close = 0;
    for (std::size_t mesh = 0; mesh < scene.level.meshes.size(); ++mesh) {
        const rt::RelitVertices& device = core::at(*light, mesh);
        for (std::size_t vertex = 0; vertex < device.size(); ++vertex) {
            const rt::RelitVertex& gpu_vertex = core::at(device, vertex);
            const rt::RelitVertex& cpu_vertex = core::at(core::at(scene.truth, mesh), vertex);
            if (!gpu_vertex.used) {
                continue;
            }
            if (on_plane(core::at(core::at(scene.level.meshes, mesh).positions, vertex), gpu_vertex)) {
                ++too_close;
                continue;
            }
            ++vertices;
            same_sun += std::abs(gpu_vertex.sun - cpu_vertex.sun) < kSameShare ? 1 : 0;
            same_sky += std::abs(gpu_vertex.sky - cpu_vertex.sky) <= (1.0F / rt::kRelitSkyRays) + kSameShare ? 1 : 0;
            same_bounce += std::abs(luma(gpu_vertex.bounce) - luma(cpu_vertex.bounce)) < kBounceSlack ? 1 : 0;
        }
    }
    tally->check(!core::at(*light, kGround).back().used, "the unused vertex is left out");
    tally->check(
        share(same_sun, vertices) >= kTracedShare,
        text::format("the sun's share: the device's the processor's at %zu of %zu vertices", same_sun, vertices));
    tally->check(
        share(same_sky, vertices) >= kTracedShare,
        text::format("the sky's share within a ray of the processor's at %zu of %zu vertices (%zu too close to "
                     "call left out)",
                     same_sky, vertices, too_close));
    tally->check(share(same_bounce, vertices) >= kTracedShare,
                 text::format("the bounce within %.2f of the processor's at %zu of %zu vertices",
                              static_cast<double>(kBounceSlack), same_bounce, vertices));
}

// 2. the model: each channel's light and the tints share a factor the colours can't tell apart - a bluer light over
// yellower tints shows the same -, so what the fit must find is each light's part against the others, channel by channel
void check_model(const rt::RelitModel& model, Tally* tally)
{
    tally->check(model.valid,
                 text::format("the model fitted (%zu vertices, median residual %.4f; without the sun %.1f "
                              "times worse)",
                              model.vertices, static_cast<double>(model.error), static_cast<double>(model.sun_needed)));
    bool parts = true;
    std::string found;
    for (std::size_t channel = 0; channel < kSkyLight.size(); ++channel) {
        const float sun = std::max(core::at(model.sun, channel), kTiny);
        const float sky = core::at(model.sky, channel) / sun;
        const float bounce = core::at(model.bounce, channel) / sun;
        const float true_sky = core::at(kSkyLight, channel) / core::at(kSunLight, channel);
        const float true_bounce = core::at(kBounceWeight, channel) / core::at(kSunLight, channel);
        parts = parts && std::abs(sky - true_sky) < kSkySlack * true_sky &&
                std::abs(bounce - true_bounce) < kBounceRatioSlack * true_bounce;
        found +=
            text::format(" sky %.3f (%.3f), bounce %.3f (%.3f);", static_cast<double>(sky),
                         static_cast<double>(true_sky), static_cast<double>(bounce), static_cast<double>(true_bounce));
    }
    tally->check(parts, "each channel's sky and bounce against its sun within 8% / 20% of the bake's:" + found);
}

// Each channel's factor the fit can't tell (the tints' level against the light's): the median of recovered over known.
Rgbf tint_factor(const Scene& scene, const std::vector<rt::RelitVertices>& light)
{
    Rgbf factor{};
    for (std::size_t channel = 0; channel < factor.size(); ++channel) {
        std::vector<float> factors;
        for (std::size_t mesh = 0; mesh < scene.level.meshes.size(); ++mesh) {
            for (std::size_t vertex = 0; vertex < core::at(light, mesh).size(); ++vertex) {
                const rt::RelitVertex& found = core::at(core::at(light, mesh), vertex);
                if (found.used && luma(core::at(core::at(scene.level.lamps, mesh), vertex)) == 0.0F) {
                    factors.push_back(core::at(found.tint, channel) /
                                      core::at(core::at(core::at(scene.level.tints, mesh), vertex), channel));
                }
            }
        }
        const auto middle = factors.begin() + static_cast<std::ptrdiff_t>(factors.size() / 2);
        std::ranges::nth_element(factors, middle);
        core::at(factor, channel) = factors.empty() ? 1.0F : *middle;
    }
    return factor;
}

struct TintTally {
    std::size_t tints = 0;
    std::size_t close = 0;
    std::size_t others = 0;
    double others_residual = 0.0;
    std::size_t lamp_middle = 0;
    std::size_t lamp_found = 0;
    double lamp_sum = 0.0;
    double lamp_back = 0.0;
    Rgbf red_square{};
    std::size_t red_count = 0;
    Rgbf ground{};
    std::size_t ground_count = 0;
    bool finite = true;
};

void tally_vertex(const Scene& scene, std::size_t mesh, std::size_t vertex, const rt::RelitVertex& found,
                  const Rgbf& factor, TintTally* out)
{
    out->finite = out->finite && finite_vertex(found);
    if (!found.used) {
        return;
    }
    const Rgbf& known = core::at(core::at(scene.level.tints, mesh), vertex);
    const float lamp = luma(core::at(core::at(scene.level.lamps, mesh), vertex));
    const float baked = luma(rt::colour_light(core::at(core::at(scene.level.meshes, mesh).colours, vertex)));
    if (lamp == 0.0F) {
        ++out->tints;
        bool fits = true;
        for (std::size_t channel = 0; channel < known.size(); ++channel) {
            const float wanted = core::at(known, channel);
            fits = fits && std::abs((core::at(found.tint, channel) / core::at(factor, channel)) - wanted) <
                               (kTintSlack * wanted) + kTintFloor;
        }
        out->close += fits ? 1 : 0;
        ++out->others;
        out->others_residual += luma(found.residual);
    } else if (lamp >= kLampSome * baked) {
        // (where the lamp is a good part of the light: its faint edge can't be told from a lighter tint)
        out->lamp_sum += lamp;
        out->lamp_back += luma(found.residual);
        if (lamp > kLampMiddle) { // (the pool's middle: all of it back)
            ++out->lamp_middle;
            out->lamp_found += std::abs(luma(found.residual) - lamp) < kLampMiddleSlack * lamp ? 1 : 0;
        }
    }
    if (mesh == kGround && in_red_square(core::at(core::at(scene.level.meshes, mesh).positions, vertex))) {
        out->red_square = rt::plus(out->red_square, found.tint);
        ++out->red_count;
    } else if (mesh == kGround && lamp == 0.0F) {
        out->ground = rt::plus(out->ground, found.tint);
        ++out->ground_count;
    }
}

// 2. the tints and the lamp's residual
void check_tints(const Scene& scene, const std::vector<rt::RelitVertices>& light, Tally* tally)
{
    const Rgbf factor = tint_factor(scene, light);
    TintTally counted;
    for (std::size_t mesh = 0; mesh < scene.level.meshes.size(); ++mesh) {
        for (std::size_t vertex = 0; vertex < core::at(light, mesh).size(); ++vertex) {
            tally_vertex(scene, mesh, vertex, core::at(core::at(light, mesh), vertex), factor, &counted);
        }
    }
    tally->check(
        share(counted.close, counted.tints) >= kTintsShare,
        text::format("the tints (over the fit's factors %.3f %.3f %.3f) within 6%% of the known ones at %zu of "
                     "%zu vertices",
                     static_cast<double>(std::get<0>(factor)), static_cast<double>(std::get<1>(factor)),
                     static_cast<double>(std::get<2>(factor)), counted.close, counted.tints));
    const Rgbf red =
        rt::scaled(counted.red_square, 1.0F / static_cast<float>(std::max<std::size_t>(counted.red_count, 1)));
    const Rgbf ground =
        rt::scaled(counted.ground, 1.0F / static_cast<float>(std::max<std::size_t>(counted.ground_count, 1)));
    const float contrast = (std::get<0>(red) / std::get<1>(red)) / (std::get<0>(ground) / std::get<1>(ground));
    const float painted =
        (std::get<0>(kRedTint) / std::get<1>(kRedTint)) / (std::get<0>(kGroundTint) / std::get<1>(kGroundTint));
    tally->check(std::abs(contrast - painted) < kContrastSlack,
                 text::format("the red square's red over green against the ground's: %.2f (painted %.2f)",
                              static_cast<double>(contrast), static_cast<double>(painted)));
    tally->check(
        counted.lamp_middle >= 2 && counted.lamp_found == counted.lamp_middle &&
            counted.lamp_back >= kLampEnergy * counted.lamp_sum,
        text::format("the lamp's light back as the residual: within 15%% at the pool's %zu middle vertices (%zu), "
                     "%.0f%% of its light where it is a quarter of the bake or more",
                     counted.lamp_middle, counted.lamp_found,
                     100.0 * counted.lamp_back / std::max(counted.lamp_sum, kTinyDouble)));
    const double elsewhere = counted.others == 0 ? 0.0 : counted.others_residual / static_cast<double>(counted.others);
    tally->check(elsewhere < kResidualElsewhere, text::format("the residual elsewhere: %.4f on average", elsewhere));
    tally->check(counted.finite, "no value NaN or infinite");
}

// 3. relit at the bake's own sun: the bake again; strength 0: the bake exactly
void check_bake_sun(const Scene& scene, const rt::RelitModel& model, const std::vector<rt::RelitVertices>& light,
                    Tally* tally)
{
    std::size_t within = 0;
    std::size_t exact = 0;
    std::size_t total = 0;
    double baked_mean = 0.0;
    double relit_mean = 0.0;
    for (std::size_t mesh = 0; mesh < scene.level.meshes.size(); ++mesh) {
        for (std::size_t vertex = 0; vertex < core::at(light, mesh).size(); ++vertex) {
            const rt::RelitVertex& found = core::at(core::at(light, mesh), vertex);
            if (!found.used) {
                continue;
            }
            const uint32_t baked = core::at(core::at(scene.level.meshes, mesh).colours, vertex);
            const uint32_t relit = rt::relit_colour(model, found, {.toward = scene.sun, .strength = 1.0F}, baked);
            const uint32_t none = rt::relit_colour(model, found, {.toward = scene.sun, .strength = 0.0F}, baked);
            ++total;
            within += channel_difference(relit, baked) <= 1 ? 1 : 0;
            exact += none == baked ? 1 : 0;
            baked_mean += luma(rt::colour_light(baked));
            relit_mean += luma(rt::colour_light(relit));
        }
    }
    tally->check(share(within, total) >= kWithinByteShare,
                 text::format("relit at the bake's sun: within a byte of the bake at %zu of %zu vertices (mean %.4f "
                              "against %.4f)",
                              within, total, relit_mean / static_cast<double>(std::max<std::size_t>(total, 1)),
                              baked_mean / static_cast<double>(std::max<std::size_t>(total, 1))));
    tally->check(exact == total, text::format("strength 0: the bake exactly at %zu of %zu vertices", exact, total));
}

// 3b. (added by the light energy audit - docs/design/vulkan.md "Each light counted once") Every ray-traced light on at
// the bake's own time: the level's bake is exact lighting (the processor's), so the relit colour with the passes on must
// still be the bake. Each vertex as the passes see it through its light stream (rt_cel.h - the processor's twins of
// the shaders' formulas): the shadows put the pixel's own visibility (the sun's rays from the vertex - its traced S -,
// the missed sun kept) in place of the one the colour was written with, the occlusion and the bounce leave the stream's
// pixels to relit, and the lamps (the bake's own lamp, traced) draw only what the residual doesn't hold. For contrast,
// the passes as they were before (each on top: the shadow's whole strength where the rays are blocked, the occlusion's
// darkening by the sky the vertex doesn't see, the lamp added) - the doubles this replaced.
void check_counted_once(const Scene& scene, const rt::RelitModel& model, const std::vector<rt::RelitVertices>& light,
                        Tally* tally)
{
    constexpr float kShadowStrength = 1.0F;
    constexpr float kAoStrength = 0.8F;
    constexpr float kOnceBytes = 2.0F;    // within this many of the bake's bytes, each channel
    constexpr float kStackedBytes = 4.0F; // the old stack: off by more than this
    constexpr float kLampHeld = 0.9F;     // a lamp-lit vertex whose residual holds this much of the lamp's light
    const rt::RelitLighting lighting{.toward = scene.sun, .strength = 1.0F};
    std::size_t total = 0;
    std::size_t once = 0;
    std::size_t stacked_off = 0;
    std::size_t streams = 0;
    std::size_t lamp_vertices = 0;
    std::size_t lamp_off = 0;
    std::size_t used = 0;
    for (std::size_t mesh = 0; mesh < scene.level.meshes.size(); ++mesh) {
        for (std::size_t vertex = 0; vertex < core::at(light, mesh).size(); ++vertex) {
            const rt::RelitVertex& found = core::at(core::at(light, mesh), vertex);
            if (!found.used) {
                continue;
            }
            ++total;
            ++used;
            const uint32_t baked = core::at(core::at(scene.level.meshes, mesh).colours, vertex);
            const Rgbf bake = rt::colour_light(baked);
            const Rgbf drawn = rt::colour_light(rt::relit_colour(model, found, lighting, baked));
            const rt::StreamParts parts = rt::stream_parts(rt::cel_stream_words(model, found, lighting));
            streams += parts.valid ? 1 : 0;
            // the shadows: the pixel's visibility (its rays: the vertex's traced S) with the missed sun kept
            const float visible = std::clamp(found.sun + parts.missed, 0.0F, 1.0F);
            const Rgbf before = rt::stream_light(parts, parts.seen);
            const Rgbf after = rt::stream_light(parts, visible);
            const Rgbf& lamp = core::at(core::at(scene.level.lamps, mesh), vertex);
            float worst_once = 0.0F;
            float worst_stacked = 0.0F;
            for (std::size_t ch = 0; ch < 3; ++ch) {
                const float factor = core::at(before, ch) > 1e-4F ? core::at(after, ch) / core::at(before, ch) : 1.0F;
                // the occlusion and the bounce: relit's (nothing here); the lamps: what the residual doesn't hold
                const float lamps = std::max(core::at(lamp, ch) - core::at(parts.residual, ch), 0.0F);
                const float final_light = (core::at(drawn, ch) * std::min(factor, 2.0F)) + lamps;
                worst_once = std::max(worst_once, std::abs(final_light - core::at(bake, ch)) * 128.0F);
                // the old stack: the traced shadow's whole strength, the occlusion by the sky unseen, the lamp on top
                const float stacked = (core::at(drawn, ch) * (1.0F - (kShadowStrength * (1.0F - found.sun))) *
                                       (1.0F - (kAoStrength * (1.0F - found.sky)))) +
                                      core::at(lamp, ch);
                worst_stacked = std::max(worst_stacked, std::abs(stacked - core::at(bake, ch)) * 128.0F);
            }
            // (a lamp's light relit took into the tint - its fall-off within a mesh too gentle to tell from the surface's
            // own colour - isn't the residual: the lamps pass can't tell it is drawn. Counted apart: a known limit,
            // docs/design/vulkan.md "Each light counted once")
            const bool lamp_lit = luma(lamp) > 0.0F;
            const bool lamp_in_tint = lamp_lit && luma(parts.residual) < kLampHeld * luma(lamp);
            lamp_vertices += lamp_lit ? 1 : 0;
            lamp_off += lamp_in_tint ? 1 : 0;
            if (lamp_in_tint) {
                --total;
                continue;
            }
            once += worst_once <= kOnceBytes ? 1 : 0;
            stacked_off += worst_stacked > kStackedBytes ? 1 : 0;
        }
    }
    tally->check(streams == used, text::format("every used vertex has its light stream (%zu of %zu)", streams, used));
    tally->check(share(once, total) >= kWithinByteShare,
                 text::format("every light term on at the bake's time: the bake within %.0f bytes at %zu of %zu "
                              "vertices (each term counted once; %zu of the %zu lamp-lit left out: relit put their lamp "
                              "in the tint)",
                              static_cast<double>(kOnceBytes), once, total, lamp_off, lamp_vertices));
    tally->check(stacked_off > total / 10,
                 text::format("(contrast) the passes stacked on the relit colour as before: %zu of %zu vertices off "
                              "the bake by more than %.0f bytes - the doubles",
                              stacked_off, total, static_cast<double>(kStackedBytes)));
}

struct Agreement {
    std::size_t agree = 0;
    std::size_t count = 0;
    std::size_t changed = 0;
};

Agreement compare_moved(const Scene& scene, const Level& moved, const rt::RelitModel& model,
                        const std::vector<rt::RelitVertices>& light, const rt::RelitLighting& lighting)
{
    Agreement out;
    for (std::size_t mesh = 0; mesh < scene.level.meshes.size(); ++mesh) {
        for (std::size_t vertex = 0; vertex < core::at(light, mesh).size(); ++vertex) {
            if (!core::at(core::at(light, mesh), vertex).used) {
                continue;
            }
            const uint32_t baked = core::at(core::at(scene.level.meshes, mesh).colours, vertex);
            const uint32_t truth = core::at(core::at(moved.meshes, mesh).colours, vertex);
            const uint32_t relit = rt::relit_colour(model, core::at(core::at(light, mesh), vertex), lighting, baked);
            ++out.count;
            out.agree += channel_difference(relit, truth) <= kMovedBytes ? 1 : 0;
            out.changed += channel_difference(truth, baked) > kChangedBytes ? 1 : 0;
        }
    }
    return out;
}

// 4. relit by another sun: the processor's bake for that sun - the sun's rays again, then the bounce again from the
// relit colours (what the game does after the sun moves)
void check_moved(const Scene& scene, const rt::RelitModel& model, rt::RayTracing& backend,
                 std::vector<rt::RelitVertices>* light, Tally* tally)
{
    Level moved = make_level();
    const Vec3 other = unit(kMovedSun);
    (void)bake(&moved, {.toward = other, .light = kSunLight}, scene.params);
    const auto retrace_start = std::chrono::steady_clock::now();
    rt::RelitStats sun_stats;
    rt::trace_relit_sun(scene.level.meshes, {}, other, scene.params, backend, light, &sun_stats);
    text::print("  (the sun's rays again on the device: %zu vertices, %zu rays, %.1f ms)\n", sun_stats.vertices,
                sun_stats.rays, milliseconds_since(retrace_start));
    const rt::RelitLighting lighting{.toward = other, .strength = 1.0F};
    const Agreement stale = compare_moved(scene, moved, model, *light, lighting);
    // the bounce from the relit colours
    std::vector<rt::RelitMesh> relit_meshes = scene.level.meshes;
    for (std::size_t mesh = 0; mesh < relit_meshes.size(); ++mesh) {
        for (std::size_t vertex = 0; vertex < core::at(*light, mesh).size(); ++vertex) {
            uint32_t& colour = core::at(core::at(relit_meshes, mesh).colours, vertex);
            colour = rt::relit_colour(model, core::at(core::at(*light, mesh), vertex), lighting, colour);
        }
    }
    const std::vector<rt::RelitVertices> before = *light;
    rt::RelitStats sky_stats;
    const auto bounce_start = std::chrono::steady_clock::now();
    rt::trace_relit_sky(relit_meshes, {}, scene.params, backend, light, &sky_stats);
    text::print("  (the bounce again on the device: %zu rays, %.1f ms)\n", sky_stats.rays,
                milliseconds_since(bounce_start));
    bool sky_kept = true;
    for (std::size_t mesh = 0; mesh < light->size(); ++mesh) {
        for (std::size_t vertex = 0; vertex < core::at(*light, mesh).size(); ++vertex) {
            sky_kept = sky_kept &&
                       core::at(core::at(*light, mesh), vertex).sky == core::at(core::at(before, mesh), vertex).sky;
        }
    }
    const Agreement fresh = compare_moved(scene, moved, model, *light, lighting);
    tally->check(fresh.changed > fresh.count / kChangedPart,
                 text::format("the moved sun changes the bake at %zu of %zu vertices", fresh.changed, fresh.count));
    tally->check(sky_kept, "the bounce traced again: the sky's shares unchanged");
    tally->check(share(fresh.agree, fresh.count) >= kMovedShare && fresh.agree >= stale.agree,
                 text::format("relit by the moved sun: within 4 bytes of the processor's bake for it at %zu of %zu "
                              "vertices (%zu with the bake's bounce)",
                              fresh.agree, fresh.count, stale.agree));
    // (added by the time of day - engine/gfx/rt_daylight.h) the clock's strengths: the processor's bake for the moved
    // sun with the sun's and the sky's light at half (its bounce from those colours, the lamp as it was) against relit
    // with the clock's scales at half - the bounce brought back at the level's own strength (as rt_relit_world's
    // rebounce does), then scaled
    constexpr float kHalfLight = 0.5F;
    const auto halved = [](const Rgbf& colour) {
        return Rgbf{core::at(colour, 0) * kHalfLight, core::at(colour, 1) * kHalfLight, core::at(colour, 2) * kHalfLight};
    };
    Level dim = make_level();
    (void)bake(&dim, {.toward = other, .light = halved(kSunLight), .sky = halved(kSkyLight)}, scene.params);
    rt::RelitLighting clock = lighting;
    clock.direct_scale = {kHalfLight, kHalfLight, kHalfLight};
    clock.sky_scale = clock.direct_scale;
    clock.bounce_scale = clock.direct_scale;
    const Agreement dimmed = compare_moved(scene, dim, model, *light, clock);
    tally->check(share(dimmed.agree, dimmed.count) >= kMovedShare && dimmed.changed > dimmed.count / kChangedPart,
                 text::format("relit at half the light (the clock's scales): within 4 bytes of the processor's half-lit "
                              "bake at %zu of %zu vertices",
                              dimmed.agree, dimmed.count));
    // the scales at 1: the arithmetic as without them, to the bit
    rt::RelitLighting ones = lighting;
    ones.direct_scale = {1.0F, 1.0F, 1.0F};
    bool same = true;
    for (std::size_t mesh = 0; mesh < light->size(); ++mesh) {
        for (std::size_t vertex = 0; vertex < core::at(*light, mesh).size(); ++vertex) {
            const uint32_t baked = core::at(core::at(scene.level.meshes, mesh).colours, vertex);
            const rt::RelitVertex& lit = core::at(core::at(*light, mesh), vertex);
            same = same && rt::relit_colour(model, lit, ones, baked) == rt::relit_colour(model, lit, lighting, baked);
        }
    }
    tally->check(same, "the clock's scales at 1 (tod=level): every relit colour as without them");
}

// (added for the relit mode's moving light - render_pc/rt_relit_world.cpp) A trace in its two halves (rt_relit.h
// RelitAsk): the rays asked inside a frame (RayTracing::probe_later - recorded into it, nothing waited for: not
// answered in that frame), answered frames later (probe_ready), the answers taken - the light exactly as traced at once.
// The sun's share toward the moved sun, then the bounce.
void check_asked(render::Gpu& gpu, std::span<const DeviceMesh> device, const Scene& scene, rt::RayTracing& backend,
                 const std::vector<rt::RelitVertices>& light, Tally* tally)
{
    const Vec3 other = unit(kMovedSun);
    uint64_t number = kAskedFrame;
    bool asked_all = true;
    bool waited_none = true;
    bool answered_all = true;
    // the trace asked and answered over frames: false if it couldn't be asked or wasn't answered in time
    const auto over_frames = [&](rt::RelitAsk* ask) {
        rt::FrameRecord record;
        record.frame = number++;
        for (const DeviceMesh& mesh : device) {
            rt::Instance instance{.mesh = mesh.id, .flags = rt::kWorld | rt::kOpaque | rt::kCastsShadow};
            instance.object_to_world = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};
            record.instances.push_back(instance);
        }
        backend.begin_frame(record);
        gpu.begin_scene();
        const uint64_t ticket = backend.probe_later(ask->requests);
        std::vector<rt::ProbeResult> answers;
        const bool at_once = ticket != 0 && backend.probe_ready(ticket, &answers);
        gpu.end_scene();
        gpu.present();
        asked_all = asked_all && ticket != 0;
        waited_none = waited_none && !at_once;
        bool ready = at_once;
        while (ticket != 0 && !ready && number < kPerfFrames) {
            frame(gpu, device, number++);
            ready = backend.probe_ready(ticket, &answers);
        }
        answered_all = answered_all && ready && answers.size() == ask->requests.size();
        ask->answers = std::move(answers);
        ask->answered = true;
        return ready;
    };
    const auto same = [](const std::vector<rt::RelitVertices>& one, const std::vector<rt::RelitVertices>& other_light,
                         auto part) {
        for (std::size_t mesh = 0; mesh < one.size(); ++mesh) {
            for (std::size_t vertex = 0; vertex < core::at(one, mesh).size(); ++vertex) {
                if (!part(core::at(core::at(one, mesh), vertex), core::at(core::at(other_light, mesh), vertex))) {
                    return false;
                }
            }
        }
        return true;
    };
    rt::RelitStats stats;
    std::vector<rt::RelitVertices> now = light;
    rt::trace_relit_sun(scene.level.meshes, {}, other, scene.params, backend, &now, &stats);
    std::vector<rt::RelitVertices> later = light;
    rt::RelitAsk sun_ask;
    rt::trace_relit_sun(scene.level.meshes, {}, other, scene.params, backend, &later, &stats, &sun_ask);
    const bool untouched = same(later, light, [](const rt::RelitVertex& a, const rt::RelitVertex& b) {
        return a.sun == b.sun;
    });
    if (over_frames(&sun_ask)) {
        rt::trace_relit_sun(scene.level.meshes, {}, other, scene.params, backend, &later, &stats, &sun_ask);
    }
    const bool sun_same = same(later, now, [](const rt::RelitVertex& a, const rt::RelitVertex& b) {
        return a.sun == b.sun;
    });
    // the same in two shares that each end inside a mesh (spans: each mesh's first half, then the rest)
    std::vector<rt::RelitVertices> halves = light;
    for (const bool first_half : {true, false}) {
        rt::RelitAsk share;
        for (std::size_t mesh = 0; mesh < scene.level.meshes.size(); ++mesh) {
            const std::size_t size = core::at(scene.level.meshes, mesh).positions.size();
            share.spans.push_back(
                {.mesh = mesh, .first = first_half ? 0 : size / 2, .end = first_half ? size / 2 : size});
        }
        rt::trace_relit_sun(scene.level.meshes, {}, other, scene.params, backend, &halves, &stats, &share);
        if (over_frames(&share)) {
            rt::trace_relit_sun(scene.level.meshes, {}, other, scene.params, backend, &halves, &stats, &share);
        }
    }
    const bool halves_same = same(halves, now, [](const rt::RelitVertex& a, const rt::RelitVertex& b) {
        return a.sun == b.sun;
    });
    // the bounce, from the colours as they are
    rt::trace_relit_sky(scene.level.meshes, {}, scene.params, backend, &now, &stats);
    rt::RelitAsk sky_ask;
    rt::trace_relit_sky(scene.level.meshes, {}, scene.params, backend, &later, &stats, {}, &sky_ask);
    if (over_frames(&sky_ask)) {
        rt::trace_relit_sky(scene.level.meshes, {}, scene.params, backend, &later, &stats, {}, &sky_ask);
    }
    const bool sky_same = same(later, now, [](const rt::RelitVertex& a, const rt::RelitVertex& b) {
        return a.sky == b.sky && a.bounce == b.bounce;
    });
    tally->check(asked_all && !sun_ask.requests.empty() && !sky_ask.requests.empty(),
                 text::format("a trace's rays asked inside a frame (%zu toward the sun, %zu over the sky)",
                              sun_ask.requests.size(), sky_ask.requests.size()));
    tally->check(untouched, "asking leaves the light as it is");
    tally->check(waited_none, "the asked rays not answered in the frame they were asked in (nothing waited for)");
    tally->check(answered_all, text::format("the asked rays answered within %llu frames",
                                            static_cast<unsigned long long>(kPerfFrames - kAskedFrame)));
    tally->check(sun_same && sky_same, "the answers taken: the sun's share, the sky's and the bounce exactly as traced "
                                       "at once");
    tally->check(halves_same, "asked in two shares ending inside the meshes (spans): the sun's share exactly as traced "
                              "at once");
}

// The baked sun's samples (rt_sun.h, as render_pc's rt_record makes them): each upward-facing triangle's centre, its
// normal turned up and its corners' brightness (0 - 255).
std::vector<rt::BakeSample> bake_samples(const Level& level)
{
    std::vector<rt::BakeSample> samples;
    for (const rt::RelitMesh& mesh : level.meshes) {
        for (const auto& [one, two, three] : mesh.triangles) {
            if (one == two || two == three || one == three) {
                continue;
            }
            const Vec3& first = core::at(mesh.positions, one);
            const Vec3 normal = rt::cross(rt::minus(core::at(mesh.positions, two), first),
                                          rt::minus(core::at(mesh.positions, three), first));
            const float length = std::sqrt(rt::dot(normal, normal));
            if (!(length > 0.0F) || std::abs(rt::y_of(normal)) < kUpFacing * length) {
                continue;
            }
            const Vec3 centre = rt::scaled(
                rt::plus(rt::plus(first, core::at(mesh.positions, two)), core::at(mesh.positions, three)), 1.0F / 3.0F);
            float brightness = 0.0F;
            for (const uint32_t corner : {one, two, three}) {
                brightness += luma(rt::colour_light(core::at(mesh.colours, corner))) * kBytes * kThird;
            }
            samples.push_back({.position = centre,
                               .normal = rt::scaled(normal, (rt::y_of(normal) < 0.0F ? -1.0F : 1.0F) / length),
                               .brightness = brightness});
        }
    }
    return samples;
}

// 5. the bake's sun found from it (the fit the game runs first: rt_sun.h; where it finds none - indoors, the Foundry -
// the mode keeps the bake: in the game, rt_shadows_check.ts --effect relit), and a level without a model keeps its bake
void check_sunless(const Scene& scene, const std::vector<rt::RelitVertices>& light, rt::RayTracing& backend,
                   Tally* tally)
{
    const rt::SunFit sunlit = rt::fit_baked_sun(bake_samples(scene.level), backend);
    const float off = std::acos(std::clamp(rt::dot(sunlit.sun.toward, scene.sun), -1.0F, 1.0F)) * kDegreesPerRadian;
    tally->check(sunlit.sun.found && off < kSunFoundDegrees,
                 text::format("the bake's sun found %.1f degrees from the sun it was made with (confidence %.2f)",
                              static_cast<double>(off), static_cast<double>(sunlit.sun.confidence)));
    const rt::RelitModel none; // (not valid: the bake shows no sun, or the fit found no sunlight in it)
    std::size_t kept = 0;
    std::size_t total = 0;
    for (std::size_t mesh = 0; mesh < scene.level.meshes.size(); ++mesh) {
        for (std::size_t vertex = 0; vertex < core::at(light, mesh).size(); ++vertex) {
            const uint32_t baked = core::at(core::at(scene.level.meshes, mesh).colours, vertex);
            ++total;
            kept += rt::relit_colour(none, core::at(core::at(light, mesh), vertex),
                                     {.toward = scene.sun, .strength = 1.0F}, baked) == baked
                        ? 1
                        : 0;
        }
    }
    tally->check(kept == total, text::format("no model: the bake kept at %zu of %zu vertices", kept, total));
}

// 6. the per-level list
void check_level_list(Tally* tally)
{
    std::vector<rt::RelitLevel> levels;
    const bool built_in = rt::parse_relit_levels(rt::built_in_relit_levels(), &levels);
    std::vector<rt::RelitLevel> listed;
    const bool read = rt::parse_relit_levels(
        R"({"_about": "x \"quoted\"", "levels": {"z_dt": {"relit": false, "why": [1, {"a": 2}]}, "BO": {"strength": 0.4}}})",
        &listed);
    constexpr float kListedStrength = 0.4F;
    const bool right = listed.size() == 2 && listed.front().name == "z_dt" && listed.front().off &&
                       listed.back().name == "BO" && !listed.back().off &&
                       std::abs(listed.back().strength - kListedStrength) < kTiny;
    tally->check(built_in && read && right,
                 text::format("the level list read (built in: %zu levels; a sample's two)", levels.size()));
    std::vector<rt::RelitLevel> broken;
    tally->check(!rt::parse_relit_levels(R"({"levels": {"x": })", &broken), "a broken list refused");
}

// --perf: a level of the game's size - the ground's vertices over many meshes (a few hundred thousand vertices)
void run_perf(render::Gpu& gpu, const Scene& scene, rt::RayTracing& backend)
{
    Level big;
    for (uint32_t tile = 0; tile < kPerfTiles * kPerfTiles; ++tile) {
        rt::RelitMesh mesh;
        const float x = (static_cast<float>(tile % kPerfTiles) - (static_cast<float>(kPerfTiles) / 2.0F)) * kPerfTile;
        const float z =
            (static_cast<float>(static_cast<uint32_t>(tile / kPerfTiles)) - (static_cast<float>(kPerfTiles) / 2.0F)) *
            kPerfTile;
        add_face(&mesh, {.corner = {x, 0, z}, .across = {0, 0, kPerfTile}, .up = {kPerfTile, 0, 0}}, kPerfTileQuads);
        mesh.colours.assign(mesh.positions.size(), kGrey);
        big.meshes.push_back(std::move(mesh));
    }
    std::vector<DeviceMesh> device(big.meshes.size());
    for (std::size_t mesh = 0; mesh < big.meshes.size(); ++mesh) {
        (void)make_device_mesh(gpu, &core::at(big.meshes, mesh), &core::at(device, mesh));
    }
    for (uint64_t number = kPerfFrames; number < kPerfFrames + kWarmFrames; ++number) {
        frame(gpu, device, number);
    }
    std::vector<rt::RelitVertices> light;
    rt::RelitStats stats;
    const auto trace_start = std::chrono::steady_clock::now();
    rt::trace_relit_light(big.meshes, {}, scene.sun, scene.params, backend, &light, &stats);
    const double traced = milliseconds_since(trace_start);
    const auto fit_start = std::chrono::steady_clock::now();
    rt::RelitModel model = rt::fit_relit_model(big.meshes, light, scene.sun);
    rt::decompose_relit(big.meshes, {}, scene.params, &model, &light, true);
    const double fitted = milliseconds_since(fit_start);
    const auto sun_start = std::chrono::steady_clock::now();
    rt::RelitStats sun_stats;
    rt::trace_relit_sun(big.meshes, {}, unit(kMovedSun), scene.params, backend, &light, &sun_stats);
    text::print("  perf: %zu vertices - traced %zu rays in %.0f ms, fit and taken apart in %.0f ms, the sun again (%zu "
                "rays) in %.0f ms\n",
                stats.vertices, stats.rays, traced, fitted, sun_stats.rays, milliseconds_since(sun_start));
    for (DeviceMesh& mesh : device) {
        release_device_mesh(gpu, &mesh);
    }
}

// (added by the one lighting path) The residual taken apart (rt_relit.h relit_split_residual): a face toward the bake's
// sun, in the tracer's shade, brighter by the sun's whole light in the sun's colour - the bake's sun the rays missed -
// is sun_missed, its residual gone, and lit as the bake at the bake's sun but not toward another light (no wedge); a
// dim lamp-coloured glow there, or anything on a face turned away, stays the residual.
void check_split(Tally* tally)
{
    text::print("the residual taken apart (on the processor):\n");
    rt::RelitModel model;
    model.valid = true;
    model.toward = {0.0F, 1.0F, 0.0F};
    model.sun = {0.50F, 0.48F, 0.42F};
    model.sky = {0.30F, 0.32F, 0.36F};
    model.bounce = {0.0F, 0.0F, 0.0F};
    model.scale = 1.0F;
    rt::RelitVertex wedge;
    wedge.used = true;
    wedge.normal = {0.0F, 1.0F, 0.0F};
    wedge.sun = 0.0F;
    wedge.sky = 0.8F;
    wedge.tint = {0.8F, 0.7F, 0.6F};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        core::at(wedge.residual, ch) = core::at(wedge.tint, ch) * core::at(model.sun, ch);
    }
    const rt::RelitVertex unsplit = wedge;
    rt::relit_split_residual(model, model.scale, &wedge);
    tally->check(std::abs(wedge.sun_missed - 1.0F) < 1e-4F && luma(wedge.residual) < 1e-4F,
                 text::format("the bake's sun the rays missed: sun_missed %.3f, residual %.4f left",
                              static_cast<double>(wedge.sun_missed), static_cast<double>(luma(wedge.residual))));
    constexpr uint32_t kGreyAlpha = 0x80000000U;
    const rt::RelitLighting at_bake{.toward = model.toward};
    const int same = channel_difference(rt::relit_colour(model, wedge, at_bake, kGreyAlpha),
                                        rt::relit_colour(model, unsplit, at_bake, kGreyAlpha));
    tally->check(same <= 1, text::format("at the bake's sun it is lit as before (%d apart)", same));
    // a light 30 degrees from the bake's: the rays' shade alone (sky), no wedge of the bake's sun
    const rt::RelitLighting moved{.toward = {0.5F, 0.8660254F, 0.0F}};
    rt::RelitVertex shaded = unsplit;
    shaded.residual = {};
    const int wedge_gone = channel_difference(rt::relit_colour(model, wedge, moved, kGreyAlpha),
                                              rt::relit_colour(model, shaded, moved, kGreyAlpha));
    tally->check(wedge_gone <= 1 && rt::relit_missed_trust(model, moved.toward) == 0.0F &&
                     rt::relit_missed_trust(model, model.toward) == 1.0F,
                 text::format("toward another light the missed sun is gone (%d from the shade alone)", wedge_gone));
    // a dim warm glow on the same face: the level's own light, kept
    rt::RelitVertex glow = unsplit;
    glow.residual = {0.30F, 0.20F, 0.05F};
    rt::relit_split_residual(model, model.scale, &glow);
    tally->check(glow.sun_missed == 0.0F && glow.residual == rt::Rgbf{0.30F, 0.20F, 0.05F},
                 "a dim warm glow stays the residual (the level's own light)");
    // a face turned from the bake's sun: nothing of the sun's
    rt::RelitVertex away = unsplit;
    away.normal = {0.0F, -1.0F, 0.0F};
    rt::relit_split_residual(model, model.scale, &away);
    tally->check(away.sun_missed == 0.0F && away.residual == unsplit.residual,
                 "on a face turned from the bake's sun the residual stays");
    // the moon's weak light: the missed sun follows the direct light's strength and colour
    rt::RelitLighting moon = at_bake;
    moon.direct_scale = {0.14F, 0.16F, 0.22F};
    const rt::Rgbf lit = rt::relit_light(model, wedge, moon);
    const float wanted = (0.16F * std::get<1>(model.sun)) + (std::get<1>(model.sky) * wedge.sky);
    tally->check(std::abs(std::get<1>(lit) - wanted) < 1e-4F,
                 text::format("the missed sun takes the lighting state's direct light (%.4f, %.4f wanted)",
                              static_cast<double>(std::get<1>(lit)), static_cast<double>(wanted)));
    // (added by lighting round 4) the bounce's local part - a lamp-lit wall's light bounced into a recess - keeps its
    // strength at night; the rest of the bounce takes the clock's
    rt::RelitModel bouncy = model;
    bouncy.bounce = {0.8F, 0.8F, 0.8F};
    rt::RelitVertex recess = unsplit;
    recess.sky = 0.0F;
    recess.bounce = {0.5F, 0.5F, 0.5F};
    recess.bounce_local = {0.3F, 0.3F, 0.3F};
    rt::RelitLighting night = at_bake;
    night.direct_scale = {0.1F, 0.1F, 0.1F};
    night.sky_scale = {0.1F, 0.1F, 0.1F};
    night.bounce_scale = {0.1F, 0.1F, 0.1F};
    const float at_night = std::get<1>(rt::relit_parts(bouncy, recess, night).indirect);
    const float by_day = std::get<1>(rt::relit_parts(bouncy, recess, at_bake).indirect);
    const float lamp_bounce = 0.8F * ((0.2F * 0.1F) + 0.3F);
    tally->check(std::abs(at_night - lamp_bounce) < 1e-5F && std::abs(by_day - (0.8F * 0.5F)) < 1e-5F,
                 text::format("the bounce's local part kept at night (%.4f, %.4f wanted; %.4f at the bake)",
                              static_cast<double>(at_night), static_cast<double>(lamp_bounce),
                              static_cast<double>(by_day)));
}

// (added by lighting round 4) A lamp's light across meshes (rt_relit.h relit_local_across): a wall facing the street, a
// lamp before its middle lighting a pool on it; a door recessed into the wall under the lamp, its own mesh, lit whole by
// the same lamp. Within a mesh only the wall's pool can be told from its tint (its far ends are unlit); the door's
// light is all the same lamp's, so it must come out as the residual as the wall's does - its tint the door's own, and
// at night (the sky's light a tenth) the door keeps the lamp's light as the wall beside it does.
void check_local_across(Tally* tally)
{
    text::print("the lamp's light across meshes (on the processor):\n");
    constexpr float kSkyShare = 0.5F;            // every vertex sees half the sky, no sun, no bounce
    constexpr Rgbf kWallTint{0.80F, 0.78F, 0.74F};
    constexpr Rgbf kDoorTint{0.45F, 0.30F, 0.20F}; // a dark wooden door
    constexpr Rgbf kLamp{0.90F, 0.75F, 0.45F};
    constexpr Vec3 kLampPlace{0.0F, 260.0F, 80.0F};
    constexpr float kReach = 420.0F;
    rt::RelitModel model;
    model.valid = true;
    model.toward = {0.0F, 1.0F, 0.0F}; // (overhead: no light on the walls)
    model.sun = {1.0F, 0.95F, 0.85F};
    model.sky = {0.60F, 0.62F, 0.70F};
    model.bounce = {};
    model.scale = 1.0F;
    const auto lamp_at = [&](const Vec3& point) {
        const Vec3 off = rt::minus(point, kLampPlace);
        const float fall = std::max(0.0F, 1.0F - (std::sqrt(rt::dot(off, off)) / kReach));
        return rt::scaled(kLamp, fall * fall);
    };
    std::vector<rt::RelitMesh> meshes(2);
    add_face(&core::at(meshes, 0), {.corner = {-900.0F, 0.0F, 0.0F}, .across = {1800.0F, 0.0F, 0.0F}, .up = {0.0F, 400.0F, 0.0F}},
             kWallSteps * 3);
    add_face(&core::at(meshes, 1), {.corner = {-60.0F, 0.0F, -30.0F}, .across = {120.0F, 0.0F, 0.0F}, .up = {0.0F, 220.0F, 0.0F}},
             2);
    std::vector<rt::RelitVertices> light(meshes.size());
    std::vector<std::vector<Rgbf>> lamps(meshes.size());
    for (std::size_t mesh = 0; mesh < meshes.size(); ++mesh) {
        rt::RelitMesh& made = core::at(meshes, mesh);
        const Rgbf& tint = mesh == 0 ? kWallTint : kDoorTint;
        for (const Vec3& point : made.positions) {
            rt::RelitVertex vertex;
            vertex.used = true;
            vertex.normal = {0.0F, 0.0F, 1.0F};
            vertex.sky = kSkyShare;
            core::at(light, mesh).push_back(vertex);
            const Rgbf lamp = lamp_at(point);
            core::at(lamps, mesh).push_back(lamp);
            Rgbf baked{};
            for (std::size_t ch = 0; ch < baked.size(); ++ch) {
                core::at(baked, ch) = core::at(tint, ch) * ((core::at(model.sky, ch) * kSkyShare) + core::at(lamp, ch));
            }
            made.colours.push_back(to_argb(baked));
        }
    }
    const rt::RelitParams params{.detail = 1.0F};
    rt::decompose_relit(meshes, {}, params, &model, &light, false);
    // the door's residual against the lamp's light on it (x its tint), and its tint against the wall's
    double door_lamp = 0.0;
    double door_back = 0.0;
    Rgbf door_tint{};
    for (std::size_t vertex = 0; vertex < core::at(light, 1).size(); ++vertex) {
        const rt::RelitVertex& found = core::at(core::at(light, 1), vertex);
        door_lamp += luma(times_rgb(kDoorTint, core::at(core::at(lamps, 1), vertex)));
        door_back += luma(found.residual);
        door_tint = rt::plus(door_tint, rt::scaled(found.tint, 1.0F / static_cast<float>(core::at(light, 1).size())));
    }
    Rgbf wall_tint{};
    std::size_t unlit = 0;
    for (std::size_t vertex = 0; vertex < core::at(light, 0).size(); ++vertex) {
        if (luma(core::at(core::at(lamps, 0), vertex)) == 0.0F) {
            wall_tint = rt::plus(wall_tint, core::at(core::at(light, 0), vertex).tint);
            ++unlit;
        }
    }
    wall_tint = rt::scaled(wall_tint, 1.0F / static_cast<float>(std::max<std::size_t>(unlit, 1)));
    const float tint_ratio = luma(door_tint) / std::max(luma(wall_tint), kTiny);
    const float true_ratio = luma(kDoorTint) / luma(kWallTint);
    tally->check(door_back >= 0.6 * door_lamp && door_back <= 1.3 * door_lamp,
                 text::format("the door lit whole by the lamp: its residual %.0f%% of the lamp's light on it",
                              100.0 * door_back / std::max(door_lamp, kTinyDouble)));
    tally->check(std::abs(tint_ratio - true_ratio) < 0.25F * true_ratio,
                 text::format("the door's tint against the wall's %.3f (painted %.3f)", static_cast<double>(tint_ratio),
                              static_cast<double>(true_ratio)));
    // at night: the door's colour over its bake against the wall's beside it (the same lamp, the sky a tenth)
    rt::RelitLighting night{.toward = model.toward};
    night.sky_scale = {0.1F, 0.1F, 0.1F};
    night.direct_scale = {0.0F, 0.0F, 0.0F};
    const auto kept = [&](std::size_t mesh, const Vec3& spot) {
        float best = 1e30F;
        float ratio = 0.0F;
        for (std::size_t vertex = 0; vertex < core::at(meshes, mesh).positions.size(); ++vertex) {
            const Vec3 off = rt::minus(core::at(core::at(meshes, mesh).positions, vertex), spot);
            const float distance = rt::dot(off, off);
            if (distance < best) {
                best = distance;
                const uint32_t baked = core::at(core::at(meshes, mesh).colours, vertex);
                const uint32_t lit = rt::relit_colour(model, core::at(core::at(light, mesh), vertex), night, baked);
                ratio = luma(rt::colour_light(lit)) / std::max(luma(rt::colour_light(baked)), kTiny);
            }
        }
        return ratio;
    };
    const float door_night = kept(1, {0.0F, 165.0F, -30.0F});
    const float wall_night = kept(0, {100.0F, 165.0F, 0.0F});
    tally->check(std::abs(door_night - wall_night) < 0.2F * wall_night,
                 text::format("at night the door keeps %.0f%% of its bake, the wall beside it %.0f%%",
                              100.0 * static_cast<double>(door_night), 100.0 * static_cast<double>(wall_night)));
}

// (added by lighting round 4) The fit against a bake whose colours are uneven (each vertex's tint up to 15% off its
// surface's - the bake's own unevenness, painted dirt): the sky and the bounce against the sun as the bake's still.
// An edge's error measured in its light's own units made the bounce - small everywhere - the cheapest term and took the
// light from the sun (Berlin's fit: a fifth of its bake's sun).
void check_uneven_fit(const Scene& scene, Tally* tally)
{
    text::print("the fit over uneven colours (on the processor):\n");
    Level level = make_level();
    constexpr float kUneven = 0.15F;
    for (std::size_t mesh = 0; mesh < level.tints.size(); ++mesh) {
        for (std::size_t vertex = 0; vertex < core::at(level.tints, mesh).size(); ++vertex) {
            const uint32_t hash = (static_cast<uint32_t>(mesh) * 0x9E3779B9U) ^ (static_cast<uint32_t>(vertex) * 0x85EBCA6BU);
            const float off = (static_cast<float>((hash >> 8U) & 0xFFFFU) / 65535.0F * 2.0F) - 1.0F;
            core::at(core::at(level.tints, mesh), vertex) =
                rt::scaled(core::at(core::at(level.tints, mesh), vertex), 1.0F + (kUneven * off));
        }
    }
    const std::vector<rt::RelitVertices> light = bake(&level, {.toward = scene.sun, .light = kSunLight}, scene.params);
    const rt::RelitModel model = rt::fit_relit_model(level.meshes, light, scene.sun);
    bool parts = model.valid;
    std::string found;
    for (std::size_t channel = 0; channel < kSkyLight.size(); ++channel) {
        const float sun = std::max(core::at(model.sun, channel), kTiny);
        const float sky = core::at(model.sky, channel) / sun;
        const float true_sky = core::at(kSkyLight, channel) / core::at(kSunLight, channel);
        const float bounce = core::at(model.bounce, channel) / sun;
        const float true_bounce = core::at(kBounceWeight, channel) / core::at(kSunLight, channel);
        parts = parts && std::abs(sky - true_sky) < 0.2F * true_sky && std::abs(bounce - true_bounce) < 0.25F * true_bounce;
        found += text::format(" sky over sun %.3f (%.3f), bounce over sun %.3f (%.3f);", static_cast<double>(sky),
                              static_cast<double>(true_sky), static_cast<double>(bounce),
                              static_cast<double>(true_bounce));
    }
    tally->check(parts, "each channel's sky and bounce against its sun within 20% / 25% of the bake's:" + found);
}

// (added by lighting round 4) The rays meeting casters the bake didn't have (the traced geometry's own: cut-outs, night
// pieces, other meshes): slabs high up the bake never saw throw traced shadows over the ground where the bake is sunlit
// (hiding little of its sky) - more traced borders than the bake's own. Their borders show no step in the bake, and
// they mustn't tell the sun's strength: each channel's sky against its sun still the bake's (told by them, the fit
// took the sun to nothing - Berlin's to a fifth).
void check_unbaked_casters(const Scene& scene, Tally* tally)
{
    text::print("the fit with casters the bake didn't have (on the processor):\n");
    Level level = make_level();
    (void)bake(&level, {.toward = scene.sun, .light = kSunLight}, scene.params);
    std::vector<rt::RelitMesh> traced = level.meshes;
    for (const Vec3& low : {Vec3{-500.0F, 700.0F, -500.0F}, Vec3{100.0F, 700.0F, -300.0F}, Vec3{-300.0F, 700.0F, 300.0F},
                            Vec3{500.0F, 700.0F, 500.0F}, Vec3{900.0F, 700.0F, -100.0F}, Vec3{300.0F, 700.0F, 900.0F}}) {
        rt::RelitMesh wall = box(low, {350.0F, 10.0F, 350.0F});
        wall.id = kCpuIds + static_cast<rt::MeshId>(traced.size());
        traced.push_back(std::move(wall));
    }
    CpuTracer cpu(traced);
    std::vector<rt::RelitVertices> light;
    rt::RelitStats stats;
    rt::trace_relit_light(level.meshes, {}, scene.sun, scene.params, cpu, &light, &stats);
    const rt::RelitModel model = rt::fit_relit_model(level.meshes, light, scene.sun);
    bool parts = model.valid;
    std::string found;
    for (std::size_t channel = 0; channel < kSkyLight.size(); ++channel) {
        const float sun = std::max(core::at(model.sun, channel), kTiny);
        const float sky = core::at(model.sky, channel) / sun;
        const float true_sky = core::at(kSkyLight, channel) / core::at(kSunLight, channel);
        parts = parts && std::abs(sky - true_sky) < 0.2F * true_sky;
        found += text::format(" sky over sun %.3f (%.3f);", static_cast<double>(sky), static_cast<double>(true_sky));
    }
    tally->check(parts, text::format("each channel's sky against its sun within 20%% of the bake's (%zu traced shadow "
                                     "edges, %zu with the bake's step):",
                                     model.shadow_edges, model.shadow_edges_kept) +
                            found);
}

// --- (the offline relit data - engine/gfx/relit_data.h) the lamps' term, the data file -------------------------------

// 6. the lamps' term: a lamp the lamps pass would shine, its light traced (trace_relit_lamps) and put into a copy of the
// level's bake as tint x c x L; the fit finds c (fit_relit_lamps) and the lamp's light ends as the lamps' term - the
// tints the known ones there too, not the lamp's light hidden in them.
constexpr Vec3 kTermLampAt{800.0F, 120.0F, -700.0F};
constexpr Rgbf kTermLampLight{1.2F, 1.0F, 0.8F};
constexpr float kTermLampCore = 250.0F;
constexpr float kTermLampReach = 1000.0F;
constexpr float kTermLampC = 0.8F;
constexpr float kTermLit = 0.1F;          // a vertex the lamp lights this much (luma of L) counted
constexpr float kTermCSlack = 0.25F;      // c over the sky's b: within this share of the bake's
constexpr float kTermTintSlack = 0.1F;    // the lamp-lit vertices' tints within this share of the known ones ...
constexpr double kTermTintShare = 0.8;    // ... at this share of them
constexpr float kTermOwnShare = 0.7F;     // the lamps' term at least this share of their own light
void check_lamp_term(const Scene& scene, const std::vector<rt::RelitVertices>& traced, rt::RayTracing& backend,
                     Tally* tally)
{
    const std::vector<rt::RelitLamp> lamps{
        {.position = kTermLampAt, .size = 0.0F, .light = kTermLampLight, .core = kTermLampCore, .reach = kTermLampReach}};
    std::vector<rt::RelitVertices> light = traced;
    rt::RelitStats stats;
    rt::trace_relit_lamps(scene.level.meshes, {}, lamps, backend, &light, &stats);
    Level lit = scene.level;
    std::size_t reached = 0;
    std::size_t behind = 0;
    for (std::size_t mesh = 0; mesh < lit.meshes.size(); ++mesh) {
        rt::RelitMesh& one = core::at(lit.meshes, mesh);
        for (std::size_t vertex = 0; vertex < one.colours.size() && vertex < core::at(light, mesh).size(); ++vertex) {
            const rt::RelitVertex& found = core::at(core::at(light, mesh), vertex);
            reached += luma(found.lamp) > kTermLit ? 1 : 0;
            // (a face turned from the lamp: none)
            const Vec3 toward = rt::minus(kTermLampAt, core::at(one.positions, vertex));
            behind += found.used && rt::dot(found.normal, toward) < 0.0F && luma(found.lamp) > 0.0F ? 1 : 0;
            const Rgbf added = rt::scaled(times_rgb(core::at(core::at(lit.tints, mesh), vertex), found.lamp), kTermLampC);
            core::at(one.colours, vertex) = to_argb(rt::plus(rt::colour_light(core::at(one.colours, vertex)), added));
        }
    }
    tally->check(reached >= 20 && behind == 0,
                 text::format("the lamps' light traced: %zu vertices lit by the lamp, %zu turned from it lit (%zu rays)",
                              reached, behind, stats.rays));
    rt::RelitModel model = rt::fit_relit_model(lit.meshes, light, scene.sun);
    rt::fit_relit_lamps(lit.meshes, light, &model);
    bool c_close = model.valid;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const float found = core::at(model.lamps, ch) / std::max(core::at(model.sky, ch), kTiny);
        const float wanted = kTermLampC / core::at(kSkyLight, ch);
        c_close = c_close && std::abs(found - wanted) <= kTermCSlack * wanted;
    }
    tally->check(c_close, text::format("the lamps' c found: %.3f %.3f %.3f (over the sky's b %.2f %.2f %.2f; the bake's "
                                       "%.2f %.2f %.2f)",
                                       static_cast<double>(std::get<0>(model.lamps)),
                                       static_cast<double>(std::get<1>(model.lamps)),
                                       static_cast<double>(std::get<2>(model.lamps)),
                                       static_cast<double>(std::get<0>(model.lamps) / std::get<0>(model.sky)),
                                       static_cast<double>(std::get<1>(model.lamps) / std::get<1>(model.sky)),
                                       static_cast<double>(std::get<2>(model.lamps) / std::get<2>(model.sky)),
                                       static_cast<double>(kTermLampC / std::get<0>(kSkyLight)),
                                       static_cast<double>(kTermLampC / std::get<1>(kSkyLight)),
                                       static_cast<double>(kTermLampC / std::get<2>(kSkyLight))));
    rt::decompose_relit(lit.meshes, {}, scene.params, &model, &light, true);
    // the tints over the factor per channel the colours can't tell (the vertices the lamp doesn't light)
    Rgbf factor{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        std::vector<float> factors;
        for (std::size_t mesh = 0; mesh < lit.meshes.size(); ++mesh) {
            for (std::size_t vertex = 0; vertex < core::at(light, mesh).size(); ++vertex) {
                const rt::RelitVertex& found = core::at(core::at(light, mesh), vertex);
                if (found.used && luma(found.lamp) == 0.0F && luma(core::at(core::at(lit.lamps, mesh), vertex)) == 0.0F) {
                    factors.push_back(core::at(found.tint, ch) / core::at(core::at(core::at(lit.tints, mesh), vertex), ch));
                }
            }
        }
        const auto middle = factors.begin() + static_cast<std::ptrdiff_t>(factors.size() / 2);
        std::ranges::nth_element(factors, middle);
        core::at(factor, ch) = factors.empty() ? 1.0F : *middle;
    }
    std::size_t counted = 0;
    std::size_t close = 0;
    double term = 0.0;
    double own = 0.0;
    for (std::size_t mesh = 0; mesh < lit.meshes.size(); ++mesh) {
        for (std::size_t vertex = 0; vertex < core::at(light, mesh).size(); ++vertex) {
            const rt::RelitVertex& found = core::at(core::at(light, mesh), vertex);
            if (!found.used || luma(found.lamp) <= kTermLit || luma(core::at(core::at(lit.lamps, mesh), vertex)) > 0.0F) {
                continue;
            }
            ++counted;
            bool within = true;
            for (std::size_t ch = 0; ch < 3; ++ch) {
                const float known = core::at(core::at(core::at(lit.tints, mesh), vertex), ch) * core::at(factor, ch);
                within = within && std::abs(core::at(found.tint, ch) - known) <= kTermTintSlack * known;
            }
            close += within ? 1 : 0;
            term += luma(rt::scaled(times_rgb(found.tint, times_rgb(model.lamps, found.lamp)), model.scale));
            own += luma(rt::relit_own_light(model, found));
        }
    }
    tally->check(counted > 0 && share(close, counted) >= kTermTintShare,
                 text::format("the lamp-lit vertices' tints the known ones within 10%% at %zu of %zu (the lamp not in "
                              "the tints)",
                              close, counted));
    tally->check(own > 0.0 && term >= kTermOwnShare * own,
                 text::format("their lamp light the lamps' term: %.0f%% of their own light", 100.0 * term / std::max(own, kTinyDouble)));
}

// 7. the data file: the level's light and model written and read back - the colours relit from what was read the
// same; the half floats; a level whose meshes changed tells by its content key
void check_data_file(const Scene& scene, const rt::RelitModel& model, const std::vector<rt::RelitVertices>& light,
                     Tally* tally)
{
    rt::RelitLevelData data;
    data.scene = 0x12345678U;
    data.verdict = rt::RelitVerdict::kRelit;
    data.model = model;
    data.facing = rt::relit_facing(scene.level.meshes);
    data.sun.found = true;
    data.sun.toward = scene.sun;
    data.sun.confidence = 0.5F;
    data.sun.columns = 2;
    data.sun.rows = 2;
    data.sun.agreement = {0.0F, 0.25F, 0.5F, 1.0F};
    std::vector<uint64_t> contents;
    for (std::size_t mesh = 0; mesh < scene.level.meshes.size(); ++mesh) {
        const uint64_t content = rt::relit_mesh_content(core::at(scene.level.meshes, mesh), {});
        contents.push_back(content);
        data.meshes.push_back({.content = content, .vertices = core::at(light, mesh)});
    }
    data.content = rt::relit_level_content(contents);
    const std::string bytes = rt::write_relit_data(data);
    rt::RelitLevelData read;
    std::string why;
    const bool ok = rt::read_relit_data(bytes, &read, &why);
    tally->check(ok && read.scene == data.scene && read.content == data.content && read.meshes.size() == data.meshes.size() &&
                     read.sun.agreement == data.sun.agreement && read.model.lamps == model.lamps &&
                     read.model.scale == model.scale,
                 text::format("the data file read back (%zu bytes)%s", bytes.size(), ok ? "" : (": " + why).c_str()));
    std::string cut_why;
    rt::RelitLevelData cut;
    const bool refused = !rt::read_relit_data(std::string_view(bytes).substr(0, bytes.size() / 2), &cut, &cut_why);
    tally->check(refused && !cut_why.empty(), "a file cut short refused (" + cut_why + ")");
    const rt::RelitLighting lighting{.toward = scene.sun, .strength = 1.0F};
    std::size_t total = 0;
    std::size_t same = 0;
    for (std::size_t mesh = 0; ok && mesh < scene.level.meshes.size(); ++mesh) {
        const rt::RelitMesh& one = core::at(scene.level.meshes, mesh);
        rt::RelitVertices again;
        rt::relit_normals(one, read.facing, &again);
        const rt::RelitVertices& stored = core::at(read.meshes, mesh).vertices;
        for (std::size_t vertex = 0; vertex < again.size() && vertex < stored.size(); ++vertex) {
            rt::RelitVertex& v = core::at(again, vertex);
            const rt::RelitVertex& s = core::at(stored, vertex);
            v.sun = s.sun_baked;
            v.sun_baked = s.sun_baked;
            v.sky = s.sky;
            v.bounce = s.bounce;
            v.bounce_local = s.bounce_local;
            v.tint = s.tint;
            v.residual = s.residual;
            v.lamp = s.lamp;
            v.sun_missed = s.sun_missed;
            if (!v.used) {
                continue;
            }
            const uint32_t baked = core::at(one.colours, vertex);
            rt::RelitVertex analysed = core::at(core::at(light, mesh), vertex);
            analysed.sun = analysed.sun_baked; // (the file keeps the sun toward the bake's: the light's own time)
            ++total;
            same += channel_difference(rt::relit_colour(read.model, v, lighting, baked),
                                       rt::relit_colour(model, analysed, lighting, baked)) <= 1
                        ? 1
                        : 0;
        }
    }
    tally->check(total > 0 && share(same, total) >= 0.995,
                 text::format("relit from the file: within a byte of the analysis's colours at %zu of %zu vertices", same,
                              total));
    bool halves = true;
    for (const float value : {0.0F, 1.0F, -2.0F, 0.333F, 1.999F, 65504.0F, 6.1e-5F, 3.0e-7F}) {
        const float back = rt::from_half(rt::to_half(value));
        halves = halves && std::abs(back - value) <= std::max(std::abs(value) * 1e-3F, 6e-8F);
    }
    halves = halves && rt::from_half(rt::to_half(1e9F)) == 65504.0F;
    tally->check(halves, "half floats: round trips within their precision, out of range held at the largest");
    rt::RelitMesh moved = core::at(scene.level.meshes, kPillar);
    core::at(moved.positions, 0) = rt::plus(core::at(moved.positions, 0), Vec3{0.0F, 1.0F, 0.0F});
    tally->check(rt::relit_mesh_content(moved, {}) != rt::relit_mesh_content(core::at(scene.level.meshes, kPillar), {}),
                 "a mesh moved by a unit: another content (its data no longer matches)");
}

void run_checks(render::Gpu& gpu, const Options& options, Tally* tally)
{
    check_split(tally);
    check_local_across(tally);
    {
        Scene uneven;
        uneven.sun = unit(kSunToward);
        check_uneven_fit(uneven, tally);
        check_unbaked_casters(uneven, tally);
    }
    rt::RayTracing* const backend = rt::ray_tracing();
    tally->check(backend != nullptr && backend->available(), "the ray tracing backend is installed");
    if (backend == nullptr) {
        return;
    }
    Scene scene;
    scene.sun = unit(kSunToward);
    scene.level = make_level();
    const auto bake_start = std::chrono::steady_clock::now();
    scene.truth = bake(&scene.level, {.toward = scene.sun, .light = kSunLight}, scene.params);
    text::print("  (the bake on the processor: %.0f ms)\n", milliseconds_since(bake_start));
    std::size_t lamp_shaded = 0;
    for (std::size_t vertex = 0; vertex < core::at(scene.level.meshes, kGround).positions.size(); ++vertex) {
        lamp_shaded += luma(core::at(core::at(scene.level.lamps, kGround), vertex)) > kLampShadedLeast &&
                               core::at(core::at(scene.truth, kGround), vertex).sun == 0.0F
                           ? 1
                           : 0;
    }
    tally->check(lamp_shaded >= kLampShadedVertices,
                 text::format("the lamp lights %zu ground vertices in the building's shadow", lamp_shaded));

    // the level on the device
    std::vector<DeviceMesh> device(scene.level.meshes.size());
    bool made = true;
    for (std::size_t mesh = 0; mesh < scene.level.meshes.size(); ++mesh) {
        made = made && make_device_mesh(gpu, &core::at(scene.level.meshes, mesh), &core::at(device, mesh));
    }
    tally->check(made, "the level's meshes made and registered");
    if (made) {
        for (uint64_t number = 1; number <= kWarmFrames; ++number) { // (a few frames: the BLASes built and compacted)
            frame(gpu, device, number);
        }
        std::vector<rt::RelitVertices> light;
        check_traced(scene, *backend, tally, &light);
        const std::vector<rt::RelitVertices> traced_light = light; // (the lamps' term's check starts from it)
        rt::RelitModel model = rt::fit_relit_model(scene.level.meshes, light, scene.sun);
        check_model(model, tally);
        rt::decompose_relit(scene.level.meshes, {}, scene.params, &model, &light, true);
        check_tints(scene, light, tally);
        check_asked(gpu, device, scene, *backend, light, tally); // (added for the relit mode's moving light)
        check_bake_sun(scene, model, light, tally);
        check_counted_once(scene, model, light, tally); // (added by the light energy audit)
        check_moved(scene, model, *backend, &light, tally);
        check_sunless(scene, light, *backend, tally);
        check_lamp_term(scene, traced_light, *backend, tally); // (the offline relit data)
        check_data_file(scene, model, light, tally);
        check_level_list(tally);
        if (options.perf) {
            run_perf(gpu, scene, *backend);
            frame(gpu, device, kAfterPerfFrame);
        }
    }
    for (DeviceMesh& mesh : device) {
        release_device_mesh(gpu, &mesh);
    }
    frame(gpu, {}, kLastFrame);
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
    Tally tally;
    run_checks(gpu, options, &tally);
    context.shutdown();
    tally.check(rt::ray_tracing() == nullptr && state.memory.statistics().total.statistics.allocationCount == 0,
                "the device shut down: the backend uninstalled, nothing left allocated");
    tally.check(validation_messages() == 0, text::format("validation messages: %d%s", validation_messages(),
                                                         layer_on ? "" : " (the layer wasn't on)"));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rt_relit_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan relit check", kWindowWidth, kWindowHeight,
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
