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
//   - a level whose bake has no sun (a, the sun's light, 0: indoors) gets no model (it keeps its bake);
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
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace value = render::gpu_value;
namespace rt = render::rt;
using rt::Rgbf;
using rt::Vec3;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x042;   // XYZ | DIFFUSE
constexpr uint32_t kStride = 16;   // the position, the colour at 12
constexpr uint32_t kColourOffset = 12;
constexpr uint32_t kFormatIndex32 = 102; // D3DFMT_INDEX32
constexpr uint32_t kGroundQuads = 24;
constexpr float kGroundHalf = 1200.0F;
constexpr uint32_t kWallSteps = 6; // a face's quads a side
constexpr double kEpsilon = 1e-9;
constexpr int kBakeRounds = 8;     // the bake iterated this often (its bounce reads its own colours)
constexpr float kLampRadius = 400.0F;
constexpr float kBytes = 128.0F;
constexpr float kMaxByte = 255.0F;
constexpr int kRedShift = 16;
constexpr int kGreenShift = 8;
constexpr uint32_t kOpaqueAlpha = 0xFF000000U;
constexpr uint32_t kByteMask = 0xFF;
constexpr rt::MeshId kCpuIds = 0x7F000000U; // (the processor's meshes' ids: never the device's)

// The bake's light: the sun's colour (a), the sky's (b), the bounce's weight (g); the sun; the lamp.
constexpr Rgbf kSunLight{1.00F, 0.95F, 0.85F};
constexpr Rgbf kSkyLight{0.35F, 0.40F, 0.50F};
constexpr Rgbf kBounceWeight{0.5F, 0.5F, 0.5F};
constexpr Vec3 kSunToward{0.55F, 0.70F, 0.45F};
constexpr Vec3 kMovedSun{-0.60F, 0.55F, 0.20F};
constexpr Rgbf kLampLight{0.70F, 0.55F, 0.30F};

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

float luma(const Rgbf& c) { return (0.299F * c[0]) + (0.587F * c[1]) + (0.114F * c[2]); }

Vec3 unit(const Vec3& v)
{
    const float length = std::sqrt(rt::dot(v, v));
    return rt::scaled(v, 1.0F / length);
}

uint32_t to_argb(const Rgbf& light)
{
    std::array<uint32_t, 3> bytes{};
    for (std::size_t c = 0; c < bytes.size(); ++c) {
        core::at(bytes, c) =
            static_cast<uint32_t>(std::lround(std::clamp(core::at(light, c) * kBytes, 0.0F, kMaxByte)));
    }
    return kOpaqueAlpha | (bytes[0] << kRedShift) | (bytes[1] << kGreenShift) | bytes[2];
}

// --- the level -------------------------------------------------------------------------------------------------------

struct Level {
    std::vector<rt::RelitMesh> meshes;
    std::vector<std::vector<Rgbf>> tints; // the known ones
    std::vector<std::vector<Rgbf>> lamps; // the lamp's light added at each vertex
};

// A quad's vertices (counterclockwise seen from its front, so (b - a) x (c - a) points out) as `steps` x `steps`
// quads, added to `mesh`.
void add_face(rt::RelitMesh* mesh, const Vec3& corner, const Vec3& across, const Vec3& up, uint32_t steps)
{
    const auto first = static_cast<uint32_t>(mesh->positions.size());
    for (uint32_t j = 0; j <= steps; ++j) {
        for (uint32_t i = 0; i <= steps; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(steps);
            const float v = static_cast<float>(j) / static_cast<float>(steps);
            mesh->positions.push_back(rt::plus(corner, rt::plus(rt::scaled(across, u), rt::scaled(up, v))));
        }
    }
    const uint32_t row = steps + 1;
    for (uint32_t j = 0; j < steps; ++j) {
        for (uint32_t i = 0; i < steps; ++i) {
            const uint32_t a = first + (j * row) + i;
            mesh->triangles.push_back({a, a + 1, a + row + 1});
            mesh->triangles.push_back({a, a + row + 1, a + row});
        }
    }
}

// A box standing on the ground: four walls and a roof, each face its own vertices.
rt::RelitMesh box(const Vec3& low, const Vec3& size)
{
    rt::RelitMesh mesh;
    const float x = low[0];
    const float y = low[1];
    const float z = low[2];
    const float w = size[0];
    const float h = size[1];
    const float d = size[2];
    // (counterclockwise from outside: across x up = outward)
    add_face(&mesh, {x, y, z}, {0, 0, d}, {0, h, 0}, kWallSteps);         // -x: (0,0,d) x (0,h,0) = (-dh, 0, 0)
    add_face(&mesh, {x + w, y, z + d}, {0, 0, -d}, {0, h, 0}, kWallSteps); // +x
    add_face(&mesh, {x + w, y, z}, {-w, 0, 0}, {0, h, 0}, kWallSteps);    // -z: (-w,0,0) x (0,h,0) = (0,0,-wh)
    add_face(&mesh, {x, y, z + d}, {w, 0, 0}, {0, h, 0}, kWallSteps);     // +z
    add_face(&mesh, {x, y + h, z}, {0, 0, d}, {w, 0, 0}, kWallSteps);     // roof: (0,0,d) x (w,0,0) = (0, dw, 0)
    return mesh;
}

Level make_level(const Vec3& lamp_at)
{
    Level level;
    rt::RelitMesh ground;
    add_face(&ground, {-kGroundHalf, 0, -kGroundHalf}, {0, 0, 2 * kGroundHalf}, {2 * kGroundHalf, 0, 0}, kGroundQuads);
    // (a degenerate triangle and an unused vertex: nothing may break on them)
    ground.triangles.push_back({0, 0, 1});
    ground.positions.push_back({0.0F, -50.0F, 0.0F});
    level.meshes.push_back(std::move(ground));
    level.meshes.push_back(box({-450, -40, -450}, {400, 360, 360})); // (sunk into the ground: no ray starts on its plane)
    level.meshes.push_back(box({350, -40, -150}, {90, 460, 90}));
    for (const rt::RelitMesh& mesh : level.meshes) {
        std::vector<Rgbf> tints;
        std::vector<Rgbf> lamps;
        for (const Vec3& p : mesh.positions) {
            Rgbf tint{0.90F, 0.86F, 0.76F};
            if (&mesh == &level.meshes[1]) {
                tint = {0.82F, 0.80F, 0.92F};
            } else if (&mesh == &level.meshes[2]) {
                tint = {0.62F, 0.62F, 0.60F};
            } else if (p[0] > 200.0F && p[0] < 700.0F && p[2] > 200.0F && p[2] < 700.0F) {
                tint = {1.30F, 0.62F, 0.50F}; // the red-painted square
            }
            tints.push_back(tint);
            const Vec3 off = rt::minus(p, lamp_at);
            const float distance = std::sqrt(rt::dot(off, off));
            const float fall = std::max(0.0F, 1.0F - (distance / kLampRadius));
            lamps.push_back(rt::scaled(kLampLight, fall * fall));
        }
        level.tints.push_back(std::move(tints));
        level.lamps.push_back(std::move(lamps));
    }
    return level;
}

// --- a ray tracer on the processor -------------------------------------------------------------------------------------

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
        for (std::size_t r = 0; r < rays.size(); ++r) {
            core::at(*results, r) = trace(core::at(rays, r));
        }
    }

private:
    [[nodiscard]] rt::ProbeResult trace(const rt::ProbeRequest& ray) const
    {
        rt::ProbeResult out{.traced = true};
        const Vec3 d = unit(ray.direction);
        double best = ray.max_distance;
        for (std::size_t m = 0; m < meshes_.size(); ++m) {
            const rt::RelitMesh& mesh = core::at(meshes_, m);
            for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
                const auto [ia, ib, ic] = core::at(mesh.triangles, t);
                const std::array<double, 3> o{ray.origin[0], ray.origin[1], ray.origin[2]};
                const auto at = [&](uint32_t i) {
                    const Vec3& p = core::at(mesh.positions, i);
                    return std::array<double, 3>{p[0], p[1], p[2]};
                };
                const std::array<double, 3> a = at(ia);
                const std::array<double, 3> b = at(ib);
                const std::array<double, 3> c = at(ic);
                const std::array<double, 3> e1{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const std::array<double, 3> e2{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const std::array<double, 3> dd{d[0], d[1], d[2]};
                const std::array<double, 3> pv{(dd[1] * e2[2]) - (dd[2] * e2[1]), (dd[2] * e2[0]) - (dd[0] * e2[2]),
                                               (dd[0] * e2[1]) - (dd[1] * e2[0])};
                const double det = (e1[0] * pv[0]) + (e1[1] * pv[1]) + (e1[2] * pv[2]);
                if (std::abs(det) < kEpsilon) {
                    continue;
                }
                const double inv = 1.0 / det;
                const std::array<double, 3> tv{o[0] - a[0], o[1] - a[1], o[2] - a[2]};
                const double u = ((tv[0] * pv[0]) + (tv[1] * pv[1]) + (tv[2] * pv[2])) * inv;
                if (u < 0.0 || u > 1.0) {
                    continue;
                }
                const std::array<double, 3> qv{(tv[1] * e1[2]) - (tv[2] * e1[1]), (tv[2] * e1[0]) - (tv[0] * e1[2]),
                                               (tv[0] * e1[1]) - (tv[1] * e1[0])};
                const double v = ((dd[0] * qv[0]) + (dd[1] * qv[1]) + (dd[2] * qv[2])) * inv;
                if (v < 0.0 || u + v > 1.0) {
                    continue;
                }
                const double distance = ((e2[0] * qv[0]) + (e2[1] * qv[1]) + (e2[2] * qv[2])) * inv;
                if (distance > 0.0 && distance < best) {
                    best = distance;
                    out.hit = true;
                    out.distance = static_cast<float>(distance);
                    out.instance = static_cast<uint32_t>(m);
                    out.mesh = mesh.id;
                    out.primitive = static_cast<uint32_t>(t);
                }
            }
        }
        return out;
    }

    std::span<const rt::RelitMesh> meshes_;
};

// The bake made on the processor for `toward`: colours = tint x (a S + b K + g B) + lamp, iterated (B reads the
// colours). `sun_light` the bake's a (zero: a level without a sun).
std::vector<rt::RelitVertices> bake(Level* level, const Vec3& toward, const Rgbf& sun_light, const rt::RelitParams& params)
{
    // (start from the tints alone; ids of the processor's own, so its hits find their meshes)
    for (std::size_t m = 0; m < level->meshes.size(); ++m) {
        rt::RelitMesh& mesh = core::at(level->meshes, m);
        mesh.id = kCpuIds + static_cast<rt::MeshId>(m);
        mesh.colours.resize(mesh.positions.size());
        for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
            core::at(mesh.colours, v) = to_argb(core::at(core::at(level->tints, m), v));
        }
    }
    CpuTracer cpu(level->meshes);
    std::vector<rt::RelitVertices> light;
    rt::RelitStats stats;
    rt::trace_relit_light(level->meshes, {}, toward, params, cpu, &light, &stats);
    rt::RelitModel truth;
    truth.valid = true;
    truth.toward = unit(toward);
    truth.sun = sun_light;
    truth.sky = kSkyLight;
    truth.bounce = kBounceWeight;
    for (int round = 0; round < kBakeRounds; ++round) {
        // (the bounce again from the colours now: the hits are the same, only what they bring changes)
        if (round > 0) {
            rt::trace_relit_light(level->meshes, {}, toward, params, cpu, &light, &stats);
        }
        for (std::size_t m = 0; m < level->meshes.size(); ++m) {
            rt::RelitMesh& mesh = core::at(level->meshes, m);
            for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
                const rt::RelitVertex& vertex = core::at(core::at(light, m), v);
                const Rgbf e = rt::relit_light(truth, vertex, {.toward = truth.toward});
                Rgbf baked{};
                for (std::size_t c = 0; c < 3; ++c) {
                    core::at(baked, c) = (core::at(core::at(core::at(level->tints, m), v), c) * core::at(e, c)) +
                                         core::at(core::at(core::at(level->lamps, m), v), c);
                }
                core::at(mesh.colours, v) = to_argb(baked);
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
    for (std::size_t v = 0; v < mesh->positions.size(); ++v) {
        std::memcpy(&core::at(bytes, v * kStride), core::at(mesh->positions, v).data(), sizeof(Vec3));
        const uint32_t colour = mesh->colours.empty() ? kOpaqueAlpha : core::at(mesh->colours, v);
        std::memcpy(&core::at(bytes, (v * kStride) + kColourOffset), &colour, sizeof(colour));
    }
    std::vector<uint32_t> indices;
    for (const auto& [a, b, c] : mesh->triangles) {
        indices.insert(indices.end(), {a, b, c});
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
    rt::ray_tracing()->add_mesh(out->id, {.vertices = out->vertices,
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

bool finite_vertex(const rt::RelitVertex& v)
{
    const std::array<float, 14> all{v.normal[0], v.normal[1], v.normal[2], v.sun,         v.sky,
                                    v.bounce[0], v.bounce[1], v.bounce[2], v.tint[0],    v.tint[1],
                                    v.tint[2],   v.residual[0], v.residual[1], v.residual[2]};
    return std::ranges::all_of(all, [](float f) { return std::isfinite(f); });
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

double share(std::size_t part, std::size_t whole) { return whole == 0 ? 0.0 : static_cast<double>(part) / whole; }

// --- the checks ------------------------------------------------------------------------------------------------------

void run_checks(render::Gpu& gpu, const Options& options, Tally* tally)
{
    rt::RayTracing* const backend = rt::ray_tracing();
    tally->check(backend != nullptr && backend->available(), "the ray tracing backend is installed");
    if (backend == nullptr) {
        return;
    }
    const rt::RelitParams params{.sky_rays = rt::kRelitSkyRays, .sun_rays = rt::kRelitSunRays, .detail = 1.0F};
    const Vec3 sun = unit(kSunToward);
    // the lamp in the building's shadow (the building's top throws it 250 x and 200 z away from the sun)
    const Vec3 lamp_at{-600.0F, 0.0F, -550.0F};
    Level level = make_level(lamp_at);
    const auto bake_start = std::chrono::steady_clock::now();
    const std::vector<rt::RelitVertices> truth = bake(&level, sun, kSunLight, params);
    text::print("  (the bake on the processor: %.0f ms)\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bake_start).count());
    std::size_t lamp_shaded = 0;
    for (std::size_t v = 0; v < level.meshes[0].positions.size(); ++v) {
        if (luma(level.lamps[0][v]) > 0.2F && truth[0][v].sun == 0.0F) {
            ++lamp_shaded;
        }
    }
    tally->check(lamp_shaded >= 4, text::format("the lamp lights %zu ground vertices in the building's shadow", lamp_shaded));

    // the level on the device
    std::vector<DeviceMesh> device(level.meshes.size());
    bool made = true;
    for (std::size_t m = 0; m < level.meshes.size(); ++m) {
        made = made && make_device_mesh(gpu, &level.meshes[m], &device[m]);
    }
    tally->check(made, "the level's meshes made and registered");
    if (!made) {
        return;
    }
    for (uint64_t f = 1; f <= 3; ++f) { // (a few frames: the BLASes built and compacted)
        frame(gpu, device, f);
    }

    // 1. the traced light: the device's against the processor's, the same rays
    std::vector<rt::RelitVertices> light;
    rt::RelitStats stats;
    const auto start = std::chrono::steady_clock::now();
    rt::trace_relit_light(level.meshes, {}, sun, params, *backend, &light, &stats);
    const double trace_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    text::print("  (the analysis's rays on the device: %zu vertices, %zu rays, %.1f ms)\n", stats.vertices, stats.rays,
                trace_ms);
    std::size_t vertices = 0;
    std::size_t same_sun = 0;
    std::size_t same_sky = 0;
    std::size_t same_bounce = 0;
    std::size_t on_plane = 0;
    bool finite = true;
    for (std::size_t m = 0; m < level.meshes.size(); ++m) {
        for (std::size_t v = 0; v < light[m].size(); ++v) {
            const rt::RelitVertex& gpu_vertex = light[m][v];
            const rt::RelitVertex& cpu_vertex = truth[m][v];
            finite = finite && finite_vertex(gpu_vertex);
            if (!gpu_vertex.used) {
                continue;
            }
            // (a wall's lowest vertices: their rays start on the ground's plane - whether a ray going down meets it at
            // 0 is too close to call)
            if (std::abs(level.meshes[m].positions[v][1]) < 1e-3F && std::abs(gpu_vertex.normal[1]) < 1e-3F) {
                ++on_plane;
                continue;
            }
            ++vertices;
            same_sun += std::abs(gpu_vertex.sun - cpu_vertex.sun) < 1e-4F ? 1 : 0;
            const bool sky_same = std::abs(gpu_vertex.sky - cpu_vertex.sky) <= (1.0F / rt::kRelitSkyRays) + 1e-4F;
            same_sky += sky_same ? 1 : 0;
            if (!sky_same && vertices - same_sky <= 5) {
                const Vec3& p = level.meshes[m].positions[v];
                text::print("  (mesh %zu vertex %zu at %.0f %.0f %.0f normal %.2f %.2f %.2f: sky %.3f, the processor's %.3f)\n",
                            m, v, static_cast<double>(p[0]), static_cast<double>(p[1]), static_cast<double>(p[2]),
                            static_cast<double>(gpu_vertex.normal[0]), static_cast<double>(gpu_vertex.normal[1]),
                            static_cast<double>(gpu_vertex.normal[2]), static_cast<double>(gpu_vertex.sky),
                            static_cast<double>(cpu_vertex.sky));
            }
            same_bounce += std::abs(luma(gpu_vertex.bounce) - luma(cpu_vertex.bounce)) < 0.02F ? 1 : 0;
        }
    }
    tally->check(!light[0].back().used, "the unused vertex is left out");
    tally->check(share(same_sun, vertices) >= 0.99,
                 text::format("the sun's share: the device's the processor's at %zu of %zu vertices", same_sun, vertices));
    tally->check(share(same_sky, vertices) >= 0.99,
                 text::format("the sky's share within a ray of the processor's at %zu of %zu vertices (%zu too close to "
                              "call left out)",
                              same_sky, vertices, on_plane));
    tally->check(share(same_bounce, vertices) >= 0.99,
                 text::format("the bounce within 0.02 of the processor's at %zu of %zu vertices", same_bounce, vertices));

    // 2. the model and the tints
    rt::RelitModel model = rt::fit_relit_model(level.meshes, light, sun);
    tally->check(model.valid, text::format("the model fitted (%zu vertices, median residual %.4f; without the sun %.1f "
                                           "times worse)",
                                           model.vertices, static_cast<double>(model.error),
                                           static_cast<double>(model.sun_needed)));
    // (each channel's light and the tints share a factor the colours can't tell apart - a bluer light over yellower
    // tints shows the same: what the fit must find is each light's part against the others, channel by channel)
    bool parts = true;
    for (std::size_t c = 0; c < 3; ++c) {
        const float sky_over_sun = model.sky[c] / std::max(model.sun[c], 1e-6F);
        const float bounce_over_sun = model.bounce[c] / std::max(model.sun[c], 1e-6F);
        parts = parts && std::abs(sky_over_sun - (kSkyLight[c] / kSunLight[c])) < 0.08F * (kSkyLight[c] / kSunLight[c]) &&
                std::abs(bounce_over_sun - (kBounceWeight[c] / kSunLight[c])) < 0.2F * (kBounceWeight[c] / kSunLight[c]);
    }
    tally->check(parts, text::format("each channel's sky and bounce against its sun within 8%% / 20%% of the bake's: sky "
                                     "%.3f %.3f %.3f (%.3f %.3f %.3f), bounce %.3f %.3f %.3f (%.3f %.3f %.3f)",
                                     static_cast<double>(model.sky[0] / model.sun[0]),
                                     static_cast<double>(model.sky[1] / model.sun[1]),
                                     static_cast<double>(model.sky[2] / model.sun[2]),
                                     static_cast<double>(kSkyLight[0] / kSunLight[0]),
                                     static_cast<double>(kSkyLight[1] / kSunLight[1]),
                                     static_cast<double>(kSkyLight[2] / kSunLight[2]),
                                     static_cast<double>(model.bounce[0] / model.sun[0]),
                                     static_cast<double>(model.bounce[1] / model.sun[1]),
                                     static_cast<double>(model.bounce[2] / model.sun[2]),
                                     static_cast<double>(kBounceWeight[0] / kSunLight[0]),
                                     static_cast<double>(kBounceWeight[1] / kSunLight[1]),
                                     static_cast<double>(kBounceWeight[2] / kSunLight[2])));
    rt::decompose_relit(level.meshes, {}, params, &model, &light, true);
    // the factor the fit can't tell (each channel's: the tints' level against the light's) - the median of recovered over
    // known
    Rgbf factor{};
    for (std::size_t c = 0; c < 3; ++c) {
        std::vector<float> factors;
        for (std::size_t m = 0; m < level.meshes.size(); ++m) {
            for (std::size_t v = 0; v < light[m].size(); ++v) {
                if (light[m][v].used && luma(level.lamps[m][v]) == 0.0F) {
                    factors.push_back(light[m][v].tint[c] / level.tints[m][v][c]);
                }
            }
        }
        std::ranges::nth_element(factors, factors.begin() + static_cast<std::ptrdiff_t>(factors.size() / 2));
        factor[c] = factors[factors.size() / 2];
    }
    std::size_t tints = 0;
    std::size_t close = 0;
    std::size_t lamp_vertices = 0;
    std::size_t lamp_found = 0;
    double lamp_sum = 0.0;
    double lamp_back = 0.0;
    std::size_t others = 0;
    double others_residual = 0.0;
    Rgbf red_square{};
    Rgbf ground{};
    std::size_t red_count = 0;
    std::size_t ground_count = 0;
    for (std::size_t m = 0; m < level.meshes.size(); ++m) {
        for (std::size_t v = 0; v < light[m].size(); ++v) {
            const rt::RelitVertex& vertex = light[m][v];
            finite = finite && finite_vertex(vertex);
            if (!vertex.used) {
                continue;
            }
            const Rgbf& known = level.tints[m][v];
            const float lamp = luma(level.lamps[m][v]);
            if (lamp == 0.0F) {
                ++tints;
                bool fits = true;
                for (std::size_t c = 0; c < 3; ++c) {
                    fits = fits && std::abs((vertex.tint[c] / factor[c]) - known[c]) < 0.06F * known[c] + 0.02F;
                }
                close += fits ? 1 : 0;
                ++others;
                others_residual += luma(vertex.residual);
            } else if (lamp >= 0.25F * luma(rt::colour_light(level.meshes[m].colours[v]))) {
                // (where the lamp is a good part of the light: its faint edge can't be told from a lighter tint)
                lamp_sum += lamp;
                lamp_back += luma(vertex.residual);
                if (lamp > 0.4F) { // (the pool's middle: all of it back)
                    ++lamp_vertices;
                    lamp_found += std::abs(luma(vertex.residual) - lamp) < 0.15F * lamp ? 1 : 0;
                }
            }
            if (m == 0 && known[0] > 1.0F) {
                red_square = rt::plus(red_square, vertex.tint);
                ++red_count;
            } else if (m == 0 && lamp == 0.0F) {
                ground = rt::plus(ground, vertex.tint);
                ++ground_count;
            }
        }
    }
    tally->check(share(close, tints) >= 0.95,
                 text::format("the tints (over the fit's factors %.3f %.3f %.3f) within 6%% of the known ones at %zu of %zu "
                              "vertices",
                              static_cast<double>(factor[0]), static_cast<double>(factor[1]),
                              static_cast<double>(factor[2]), close, tints));
    red_square = rt::scaled(red_square, 1.0F / static_cast<float>(std::max<std::size_t>(red_count, 1)));
    ground = rt::scaled(ground, 1.0F / static_cast<float>(std::max<std::size_t>(ground_count, 1)));
    const float contrast = (red_square[0] / red_square[1]) / (ground[0] / ground[1]);
    tally->check(std::abs(contrast - ((1.30F / 0.62F) / (0.90F / 0.86F))) < 0.15F,
                 text::format("the red square's red over green against the ground's: %.2f (painted %.2f)",
                              static_cast<double>(contrast), static_cast<double>((1.30F / 0.62F) / (0.90F / 0.86F))));
    tally->check(lamp_vertices >= 2 && lamp_found == lamp_vertices && lamp_back >= 0.4 * lamp_sum,
                 text::format("the lamp's light back as the residual: within 15%% at the pool's %zu middle vertices (%zu), "
                              "%.0f%% of its light where it is a quarter of the bake or more",
                              lamp_vertices, lamp_found, 100.0 * lamp_back / std::max(lamp_sum, 1e-9)));
    const double mean_other = others == 0 ? 0.0 : others_residual / static_cast<double>(others);
    tally->check(mean_other < 0.01, text::format("the residual elsewhere: %.4f on average", mean_other));
    tally->check(finite, "no value NaN or infinite");

    // 3. relit at the bake's own sun: the bake again; strength 0: the bake exactly
    std::size_t within = 0;
    std::size_t exact = 0;
    std::size_t total = 0;
    double baked_mean = 0.0;
    double relit_mean = 0.0;
    for (std::size_t m = 0; m < level.meshes.size(); ++m) {
        for (std::size_t v = 0; v < light[m].size(); ++v) {
            if (!light[m][v].used) {
                continue;
            }
            const uint32_t baked = level.meshes[m].colours[v];
            const uint32_t relit =
                rt::relit_colour(model, light[m][v], {.toward = sun, .strength = 1.0F, .residual = true}, baked);
            const uint32_t none =
                rt::relit_colour(model, light[m][v], {.toward = sun, .strength = 0.0F, .residual = true}, baked);
            ++total;
            within += channel_difference(relit, baked) <= 1 ? 1 : 0;
            exact += none == baked ? 1 : 0;
            baked_mean += luma(rt::colour_light(baked));
            relit_mean += luma(rt::colour_light(relit));
        }
    }
    tally->check(share(within, total) >= 0.97,
                 text::format("relit at the bake's sun: within a byte of the bake at %zu of %zu vertices (mean %.4f "
                              "against %.4f)",
                              within, total, relit_mean / static_cast<double>(total), baked_mean / static_cast<double>(total)));
    tally->check(exact == total, text::format("strength 0: the bake exactly at %zu of %zu vertices", exact, total));

    // 4. relit by another sun: the processor's bake for that sun - the sun's rays again, then the bounce again from the
    // relit colours (what the game does after the sun moves)
    {
        Level moved = make_level(lamp_at);
        const Vec3 other = unit(kMovedSun);
        const std::vector<rt::RelitVertices> moved_truth = bake(&moved, other, kSunLight, params);
        const auto retrace_start = std::chrono::steady_clock::now();
        rt::RelitStats sun_stats;
        rt::trace_relit_sun(level.meshes, {}, other, params, *backend, &light, &sun_stats);
        const double retrace_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - retrace_start).count();
        text::print("  (the sun's rays again on the device: %zu vertices, %zu rays, %.1f ms)\n", sun_stats.vertices,
                    sun_stats.rays, retrace_ms);
        const rt::RelitLighting lighting{.toward = other, .strength = 1.0F};
        const auto compare = [&](std::size_t* agree, std::size_t* count, std::size_t* changed) {
            *agree = 0;
            *count = 0;
            *changed = 0;
            for (std::size_t m = 0; m < level.meshes.size(); ++m) {
                for (std::size_t v = 0; v < light[m].size(); ++v) {
                    if (!light[m][v].used) {
                        continue;
                    }
                    const uint32_t relit = rt::relit_colour(model, light[m][v], lighting, level.meshes[m].colours[v]);
                    ++*count;
                    *agree += channel_difference(relit, moved.meshes[m].colours[v]) <= 4 ? 1 : 0;
                    *changed += channel_difference(moved.meshes[m].colours[v], level.meshes[m].colours[v]) > 8 ? 1 : 0;
                }
            }
        };
        std::size_t stale = 0;
        std::size_t count = 0;
        std::size_t changed = 0;
        compare(&stale, &count, &changed);
        // the bounce from the relit colours
        std::vector<rt::RelitMesh> relit_meshes = level.meshes;
        for (std::size_t m = 0; m < relit_meshes.size(); ++m) {
            for (std::size_t v = 0; v < light[m].size(); ++v) {
                relit_meshes[m].colours[v] = rt::relit_colour(model, light[m][v], lighting, level.meshes[m].colours[v]);
            }
        }
        const std::vector<rt::RelitVertices> before = light;
        rt::RelitStats sky_stats;
        const auto bounce_start = std::chrono::steady_clock::now();
        rt::trace_relit_sky(relit_meshes, {}, params, *backend, &light, &sky_stats);
        const double bounce_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bounce_start).count();
        bool sky_kept = true;
        for (std::size_t m = 0; m < light.size(); ++m) {
            for (std::size_t v = 0; v < light[m].size(); ++v) {
                sky_kept = sky_kept && light[m][v].sky == before[m][v].sky;
            }
        }
        std::size_t agree = 0;
        compare(&agree, &count, &changed);
        text::print("  (the bounce again on the device: %zu rays, %.1f ms)\n", sky_stats.rays, bounce_ms);
        tally->check(changed > count / 10,
                     text::format("the moved sun changes the bake at %zu of %zu vertices", changed, count));
        tally->check(sky_kept, "the bounce traced again: the sky's shares unchanged");
        tally->check(share(agree, count) >= 0.95 && agree >= stale,
                     text::format("relit by the moved sun: within 4 bytes of the processor's bake for it at %zu of %zu "
                                  "vertices (%zu with the bake's bounce)",
                                  agree, count, stale));
    }

    // 5. a level whose bake has no sun: no model
    {
        Level indoor = make_level(lamp_at);
        (void)bake(&indoor, sun, {0.0F, 0.0F, 0.0F}, params);
        std::vector<rt::RelitVertices> indoor_light;
        rt::RelitStats indoor_stats;
        rt::trace_relit_light(indoor.meshes, {}, sun, params, *backend, &indoor_light, &indoor_stats);
        const rt::RelitModel none = rt::fit_relit_model(indoor.meshes, indoor_light, sun);
        tally->check(!none.valid, text::format("a bake without a sun: no model (sun %.4f, sky %.4f, bounce %.4f; the fit without the sun %.2f times the one with it)",
                                               static_cast<double>(luma(none.sun)), static_cast<double>(luma(none.sky)),
                                               static_cast<double>(luma(none.bounce)), static_cast<double>(none.sun_needed)));
    }

    // 6. the per-level list
    {
        std::vector<rt::RelitLevel> levels;
        const bool built_in = rt::parse_relit_levels(rt::built_in_relit_levels(), &levels);
        std::vector<rt::RelitLevel> listed;
        const bool read = rt::parse_relit_levels(
            R"({"_about": "x \"quoted\"", "levels": {"z_dt": {"relit": false, "why": [1, {"a": 2}]}, "BO": {"strength": 0.4}}})",
            &listed);
        tally->check(built_in && read && listed.size() == 2 && listed[0].name == "z_dt" && listed[0].off &&
                         listed[1].name == "BO" && !listed[1].off && std::abs(listed[1].strength - 0.4F) < 1e-6F,
                     text::format("the level list read (built in: %zu levels; a sample's two)", levels.size()));
        std::vector<rt::RelitLevel> broken;
        tally->check(!rt::parse_relit_levels(R"({"levels": {"x": })", &broken), "a broken list refused");
    }

    if (options.perf) {
        // a level of the game's size: the ground's vertices over many meshes (a few hundred thousand vertices)
        Level big;
        constexpr uint32_t kTiles = 12;
        constexpr float kTile = 2000.0F;
        for (uint32_t i = 0; i < kTiles * kTiles; ++i) {
            rt::RelitMesh tile;
            const float x = (static_cast<float>(i % kTiles) - (kTiles / 2.0F)) * kTile;
            const float z = (static_cast<float>(i / kTiles) - (kTiles / 2.0F)) * kTile;
            add_face(&tile, {x, 0, z}, {0, 0, kTile}, {kTile, 0, 0}, 40);
            tile.colours.assign(tile.positions.size(), 0xFF808080U);
            big.meshes.push_back(std::move(tile));
        }
        std::vector<DeviceMesh> big_device(big.meshes.size());
        for (std::size_t m = 0; m < big.meshes.size(); ++m) {
            (void)make_device_mesh(gpu, &big.meshes[m], &big_device[m]);
        }
        for (uint64_t f = 10; f < 13; ++f) {
            frame(gpu, big_device, f);
        }
        std::vector<rt::RelitVertices> big_light;
        rt::RelitStats big_stats;
        const auto big_start = std::chrono::steady_clock::now();
        rt::trace_relit_light(big.meshes, {}, sun, params, *backend, &big_light, &big_stats);
        const double big_trace = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - big_start).count();
        const auto fit_start = std::chrono::steady_clock::now();
        rt::RelitModel big_model = rt::fit_relit_model(big.meshes, big_light, sun);
        rt::decompose_relit(big.meshes, {}, params, &big_model, &big_light, true);
        const double big_fit = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - fit_start).count();
        const auto sun_start = std::chrono::steady_clock::now();
        rt::RelitStats big_sun;
        rt::trace_relit_sun(big.meshes, {}, unit(kMovedSun), params, *backend, &big_light, &big_sun);
        const double big_retrace =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sun_start).count();
        text::print("  perf: %zu vertices - traced %zu rays in %.0f ms, fit and taken apart in %.0f ms, the sun again "
                    "(%zu rays) in %.0f ms\n",
                    big_stats.vertices, big_stats.rays, big_trace, big_fit, big_sun.rays, big_retrace);
        for (DeviceMesh& mesh : big_device) {
            release_device_mesh(gpu, &mesh);
        }
        frame(gpu, device, 20);
    }
    for (DeviceMesh& mesh : device) {
        release_device_mesh(gpu, &mesh);
    }
    frame(gpu, {}, 30);
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
