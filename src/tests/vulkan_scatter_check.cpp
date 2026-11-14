// NOT ORIGINAL - thug2_vulkan_scatter_check.exe: the decals and clutter (engine/gfx/scatter.h's placement,
// gpu_vulkan/vulkan_scatter.h's passes) tried on this machine.
//
// The placement, on a synthetic street of collision faces: an asphalt road in a grid of triangles, a kerb rising 6 to a
// paving sidewalk, a brick building wall behind it, a quarter pipe and a box ledge standing on the road, a grind rail
// over it, a flight of three steps, a dirt patch and a lawn. Cases:
//   - the same faces, rails and seed give the same decals and clutter byte for byte; another seed others;
//   - every decal's kind is one its surface takes (asphalt cracks, oil, tyre marks...; grime only on brick and
//     concrete walls), every decal vertex on its face's plane and none past an edge (the kerb's lip, the road's end);
//   - none on a skate line: no decal or item within reach of the quarter pipe, under the rail, on the ledge's top or
//     the steps, at the stairs' foot; clutter on hard ground only near the kerb or the wall, weeds at their very foot;
//     pebbles only on dirt, nothing on the lawn but leaves;
//   - the counts are what the rates give.
// The passes, over a road drawn in the view's depth with a wall standing on it:
//   - decals: only pixels over the road within a decal's footprint change, each only darker, none behind the wall;
//     decals set above the ground (over nothing) change nothing; the same picture from a new scatter;
//   - clutter: pixels change only near the kerb's foot, all finite; the same picture again;
//   - four samples: both passes run; off: no pass, no sink, the picture as before it was ever on; validation 0;
//   - the cost at 2560x1440 (printed).
//   thug2_vulkan_scatter_check [--device N] [--display N] [--no-validation] [--dump <folder>]
//   (--dump: the atlas over grey and the passes' pictures written there as .ppm, to look at)
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
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/scatter.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "gpu_vulkan/vulkan_rt_reflections.h"
#include "gpu_vulkan/vulkan_scatter.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuRenderState;
using render::GpuStageState;
using render::GpuSurface;
namespace value = render::gpu_value;
namespace vk = render::vulkan;
namespace scatter = render::scatter;
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
constexpr int kLearningPasses = 3;

// The view (Direct3D's left-handed projection, row vectors): near 1, far 10000, a vertical scale of 1.5.
constexpr float kNear = 1.0F;
constexpr float kFar = 10000.0F;
constexpr float kVerticalScale = 1.5F;
constexpr Vec3 kEye{0.0F, 140.0F, -150.0F};
constexpr Vec3 kLookAt{0.0F, 0.0F, 450.0F};
constexpr uint32_t kRoadColour = 0xFF8C8C8C;
constexpr uint32_t kWallColour = 0xFFB0A090;
constexpr uint32_t kSkyColour = 0xFF8090C0;

// --- the street (world units, y up) -------------------------------------------------------------------------------
constexpr float kRoadLeft = -900.0F;
constexpr float kRoadRight = 900.0F;
constexpr float kRoadNear = -60.0F;
constexpr float kKerbZ = 800.0F;     // the kerb's face (the road ends here)
constexpr float kKerbHeight = 6.0F;
constexpr float kBuildingZ = 1000.0F; // the building's wall (the sidewalk ends here)
constexpr float kBuildingHeight = 300.0F;
constexpr float kRoadCell = 150.0F;   // (the road's grid of quads)
constexpr std::array<float, 4> kPipe{300.0F, 500.0F, 200.0F, 300.0F}; // x from - to, z from - to (rising toward +z)
constexpr float kPipeHeight = 60.0F;
constexpr std::array<float, 4> kLedge{-300.0F, -200.0F, 300.0F, 400.0F};
constexpr float kLedgeHeight = 20.0F;
constexpr Vec3 kRailFrom{-600.0F, 30.0F, 550.0F};
constexpr Vec3 kRailTo{-350.0F, 30.0F, 550.0F};
constexpr std::array<float, 2> kStairsX{100.0F, 200.0F};
constexpr float kStairsZ = 450.0F; // the first riser; each step 20 deep, 8 high, three of them, a landing behind
constexpr float kStep = 20.0F;
constexpr float kRiser = 8.0F;
constexpr std::array<float, 4> kDirt{-900.0F, -700.0F, -400.0F, -200.0F};
constexpr std::array<float, 4> kLawn{700.0F, 900.0F, -400.0F, -200.0F};
constexpr uint32_t kSeed = 0x1234ABCDU;

// The GPU's street: the road (y 0) and a wall standing on it facing the camera.
constexpr std::array<float, 4> kOccluder{-100.0F, 100.0F, 200.0F, 80.0F}; // x from - to, z, its top

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
    std::string dump; // (a folder: the pictures written there)
};

std::string& dump_folder()
{
    static std::string folder;
    return folder;
}

// A picture as a binary PPM in the dump folder (nothing without one): rgb 0 - 1 each.
void dump_ppm(const char* name, uint32_t width, uint32_t height, const std::vector<std::array<float, 3>>& pixels)
{
    if (dump_folder().empty() || pixels.size() < static_cast<std::size_t>(width) * height) {
        return;
    }
    const std::string path = dump_folder() + "/" + name;
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return;
    }
    out << "P6\n" << width << ' ' << height << "\n255\n";
    std::string bytes;
    bytes.reserve(pixels.size() * 3);
    for (const auto& pixel : pixels) {
        for (const float channel : pixel) {
            const float shown = std::clamp(std::isfinite(channel) ? channel : 0.0F, 0.0F, 1.0F);
            bytes.push_back(static_cast<char>(static_cast<uint8_t>(std::lround(shown * 255.0F))));
        }
    }
    out << bytes;
}

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
        } else if (argument == "--dump" && has_value) {
            options.dump = core::at(arguments, ++i);
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

Vec3 minus(const Vec3& a, const Vec3& b)
{
    return {core::at(a, 0) - core::at(b, 0), core::at(a, 1) - core::at(b, 1), core::at(a, 2) - core::at(b, 2)};
}
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {(core::at(a, 1) * core::at(b, 2)) - (core::at(a, 2) * core::at(b, 1)),
            (core::at(a, 2) * core::at(b, 0)) - (core::at(a, 0) * core::at(b, 2)),
            (core::at(a, 0) * core::at(b, 1)) - (core::at(a, 1) * core::at(b, 0))};
}
float dot(const Vec3& a, const Vec3& b)
{
    return (core::at(a, 0) * core::at(b, 0)) + (core::at(a, 1) * core::at(b, 1)) + (core::at(a, 2) * core::at(b, 2));
}
Vec3 unit(const Vec3& a)
{
    const float length = std::sqrt(dot(a, a));
    return {core::at(a, 0) / length, core::at(a, 1) / length, core::at(a, 2) / length};
}

// --- the faces -----------------------------------------------------------------------------------------------------

void add_quad(std::vector<scatter::Face>* faces, const std::array<Vec3, 4>& corners, scatter::Surface surface,
              bool vert = false)
{
    faces->push_back({.corners = {core::at(corners, 0), core::at(corners, 1), core::at(corners, 2)},
                      .surface = surface,
                      .vert = vert});
    faces->push_back({.corners = {core::at(corners, 0), core::at(corners, 2), core::at(corners, 3)},
                      .surface = surface,
                      .vert = vert});
}

// A flat quad at `height` over x from - to, z from - to.
void add_flat(std::vector<scatter::Face>* faces, const std::array<float, 4>& area, float height, scatter::Surface surface)
{
    const auto [x0, x1, z0, z1] = area;
    add_quad(faces, {{{x0, height, z0}, {x1, height, z0}, {x1, height, z1}, {x0, height, z1}}}, surface);
}

// A wall across x (from - to) at z, from `bottom` to `top`.
void add_wall_x(std::vector<scatter::Face>* faces, std::array<float, 2> x, float z, float bottom, float top,
                scatter::Surface surface)
{
    add_quad(faces,
             {{{core::at(x, 0), bottom, z}, {core::at(x, 1), bottom, z}, {core::at(x, 1), top, z}, {core::at(x, 0), top, z}}},
             surface);
}
// A wall across z (from - to) at x.
void add_wall_z(std::vector<scatter::Face>* faces, std::array<float, 2> z, float x, float bottom, float top,
                scatter::Surface surface)
{
    add_quad(faces,
             {{{x, bottom, core::at(z, 0)}, {x, bottom, core::at(z, 1)}, {x, top, core::at(z, 1)}, {x, top, core::at(z, 0)}}},
             surface);
}

std::vector<scatter::Face> street_faces()
{
    std::vector<scatter::Face> faces;
    using S = scatter::Surface;
    // the road: a grid of quads (seams for the decals to cross)
    for (float x = kRoadLeft; x < kRoadRight; x += kRoadCell) {
        for (float z = kRoadNear; z < kKerbZ; z += kRoadCell) {
            add_flat(&faces, {x, std::min(x + kRoadCell, kRoadRight), z, std::min(z + kRoadCell, kKerbZ)}, 0.0F, S::kAsphalt);
        }
    }
    // the kerb, the sidewalk, the building
    add_wall_x(&faces, {kRoadLeft, kRoadRight}, kKerbZ, 0.0F, kKerbHeight, S::kConcrete);
    add_flat(&faces, {kRoadLeft, kRoadRight, kKerbZ, kBuildingZ}, kKerbHeight, S::kPaving);
    add_wall_x(&faces, {kRoadLeft, kRoadRight}, kBuildingZ, kKerbHeight, kBuildingHeight, S::kBrick);
    // the quarter pipe: rising toward +z in four slopes, vert at the top
    const auto [px0, px1, pz0, pz1] = kPipe;
    constexpr int kSlopes = 4;
    for (int k = 0; k < kSlopes; ++k) {
        const float t0 = static_cast<float>(k) / kSlopes;
        const float t1 = static_cast<float>(k + 1) / kSlopes;
        const float y0 = kPipeHeight * (1.0F - std::cos(t0 * 1.5707963F));
        const float y1 = kPipeHeight * (1.0F - std::cos(t1 * 1.5707963F));
        const float z0 = pz0 + ((pz1 - pz0) * std::sin(t0 * 1.5707963F));
        const float z1 = pz0 + ((pz1 - pz0) * std::sin(t1 * 1.5707963F));
        add_quad(&faces, {{{px0, y0, z0}, {px1, y0, z0}, {px1, y1, z1}, {px0, y1, z1}}}, S::kConcrete, k == kSlopes - 1);
    }
    // the ledge: a concrete box
    const auto [lx0, lx1, lz0, lz1] = kLedge;
    add_flat(&faces, kLedge, kLedgeHeight, S::kConcrete);
    add_wall_x(&faces, {lx0, lx1}, lz0, 0.0F, kLedgeHeight, S::kConcrete);
    add_wall_x(&faces, {lx0, lx1}, lz1, 0.0F, kLedgeHeight, S::kConcrete);
    add_wall_z(&faces, {lz0, lz1}, lx0, 0.0F, kLedgeHeight, S::kConcrete);
    add_wall_z(&faces, {lz0, lz1}, lx1, 0.0F, kLedgeHeight, S::kConcrete);
    // the steps: three risers and treads, a landing (each tread's sides drop to the road)
    const auto [sx0, sx1] = kStairsX;
    for (int k = 0; k < 3; ++k) {
        const float z = kStairsZ + (static_cast<float>(k) * kStep);
        const float bottom = static_cast<float>(k) * kRiser;
        add_wall_x(&faces, {sx0, sx1}, z, bottom, bottom + kRiser, S::kPaving);
        const float depth = k == 2 ? kStep * 3.0F : kStep;
        add_flat(&faces, {sx0, sx1, z, z + depth}, bottom + kRiser, S::kPaving);
        add_wall_z(&faces, {z, z + depth}, sx0, 0.0F, bottom + kRiser, S::kPaving);
        add_wall_z(&faces, {z, z + depth}, sx1, 0.0F, bottom + kRiser, S::kPaving);
    }
    // the dirt patch and the lawn, beside the road
    add_flat(&faces, kDirt, 0.0F, S::kDirt);
    add_flat(&faces, kLawn, 0.0F, S::kGrass);
    return faces;
}

std::vector<scatter::Rail> street_rails() { return {{.a = kRailFrom, .b = kRailTo}}; }

bool inside(float x, float z, const std::array<float, 4>& area, float margin)
{
    return x >= core::at(area, 0) - margin && x <= core::at(area, 1) + margin && z >= core::at(area, 2) - margin &&
           z <= core::at(area, 3) + margin;
}

float rail_distance(const Vec3& p)
{
    const float x = std::clamp(core::at(p, 0), core::at(kRailFrom, 0), core::at(kRailTo, 0));
    const float dx = core::at(p, 0) - x;
    const float dz = core::at(p, 2) - core::at(kRailFrom, 2);
    return std::sqrt((dx * dx) + (dz * dz));
}

// The kinds a surface's ground takes (the placement's table, said again).
bool kind_fits(scatter::DecalKind kind, scatter::Surface surface, bool wall)
{
    using K = scatter::DecalKind;
    using S = scatter::Surface;
    if (wall) {
        return kind == K::kGrime && (surface == S::kBrick || surface == S::kConcrete);
    }
    switch (surface) {
    case S::kAsphalt:
        return kind == K::kCrack || kind == K::kStain || kind == K::kOil || kind == K::kTyre || kind == K::kWet;
    case S::kConcrete:
        return kind == K::kCrack || kind == K::kStain || kind == K::kGum || kind == K::kWet;
    case S::kPaving:
        return kind == K::kCrack || kind == K::kStain || kind == K::kGum;
    case S::kBrick:
        return kind == K::kCrack || kind == K::kStain;
    case S::kDirt:
        return kind == K::kWet;
    default:
        return false;
    }
}

template <typename T>
bool same_bytes(const std::vector<T>& a, const std::vector<T>& b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

// The distance from a point to the quarter pipe's ramp proper - its slopes from the second (its first, flatter than a
// ramp, is ground within reach of the second) - sampled finely over them.
float pipe_distance(const Vec3& p)
{
    constexpr int kSlopes = 4;
    constexpr int kSamples = 40;
    const auto [px0, px1, pz0, pz1] = kPipe;
    float nearest = 1.0e9F;
    for (int i = 0; i <= kSamples; ++i) {
        const float x = px0 + ((px1 - px0) * static_cast<float>(i) / kSamples);
        for (int j = 0; j <= kSamples; ++j) {
            const float t = (1.0F + (3.0F * static_cast<float>(j) / kSamples)) / kSlopes; // (from the second slope)
            const float y = kPipeHeight * (1.0F - std::cos(t * 1.5707963F));
            const float z = pz0 + ((pz1 - pz0) * std::sin(t * 1.5707963F));
            const Vec3 d{core::at(p, 0) - x, core::at(p, 1) - y, core::at(p, 2) - z};
            nearest = std::min(nearest, std::sqrt(dot(d, d)));
        }
    }
    return nearest;
}

// Whether a ground point is on a skate line of the street (said independently of the placement's code).
struct LineCheck {
    bool pipe = false;
    bool rail = false;
    bool ledge = false;
    bool steps = false;
};
LineCheck on_line(const Vec3& p)
{
    const float x = core::at(p, 0);
    const float z = core::at(p, 2);
    LineCheck out;
    out.pipe = pipe_distance(p) < scatter::kRampReach - 1.0F;
    out.rail = rail_distance(p) < scatter::kRailReach - 1.0F;
    out.ledge = inside(x, z, kLedge, 0.0F) && core::at(p, 1) > 1.0F;
    out.steps = inside(x, z, {core::at(kStairsX, 0), core::at(kStairsX, 1), kStairsZ, kStairsZ + (kStep * 5.0F)}, 0.0F) &&
                core::at(p, 1) > 1.0F;
    return out;
}

void check_placement(Tally* tally)
{
    text::print("the placement:\n");
    const std::vector<scatter::Face> faces = street_faces();
    const std::vector<scatter::Rail> rails = street_rails();
    constexpr float kDensity = 2.0F;
    const scatter::Input input{.faces = faces, .rails = rails, .seed = kSeed, .decal_density = kDensity, .clutter_density = kDensity};
    const scatter::Scatter one = scatter::make_scatter(input);
    const scatter::Scatter two = scatter::make_scatter(input);
    tally->check(same_bytes(one.decal_vertices, two.decal_vertices) && same_bytes(one.items, two.items) &&
                     one.decals.size() == two.decals.size() && one.tiles.size() == two.tiles.size(),
                 text::format("the same faces give the same %zu decals (%zu vertices) and %zu items byte for byte",
                              one.decals.size(), one.decal_vertices.size(), one.items.size()));
    scatter::Input other = input;
    other.seed = kSeed + 1U;
    const scatter::Scatter moved = scatter::make_scatter(other);
    tally->check(!same_bytes(one.decal_vertices, moved.decal_vertices) && !same_bytes(one.items, moved.items),
                 "another level's seed places them elsewhere");
    // the faces given in another order: the same things (sorted, they are the same sets)
    std::vector<scatter::Face> reversed(faces.rbegin(), faces.rend());
    scatter::Input backwards = input;
    backwards.faces = reversed;
    const scatter::Scatter turned = scatter::make_scatter(backwards);
    tally->check(turned.decals.size() == one.decals.size() && turned.items.size() == one.items.size(),
                 text::format("the faces in reverse order: the same counts (%zu decals, %zu items)", turned.decals.size(),
                              turned.items.size()));
    // the kinds on their surfaces
    int wrong_kind = 0;
    int off_plane = 0;
    int past_edge = 0;
    int on_lines = 0;
    for (const scatter::Decal& decal : one.decals) {
        const scatter::Face& face = core::at(faces, decal.face);
        const bool wall = std::abs(core::at(decal.normal, 1)) < scatter::kWallUp;
        wrong_kind += kind_fits(decal.kind, face.surface, wall) ? 0 : 1;
        const Vec3& a = core::at(face.corners, 0);
        const Vec3 n = unit(cross(minus(core::at(face.corners, 1), a), minus(core::at(face.corners, 2), a)));
        for (uint32_t v = decal.first_vertex; v < decal.first_vertex + decal.vertex_count; ++v) {
            const Vec3& p = core::at(one.decal_vertices, v).position;
            off_plane += std::abs(dot(minus(p, a), n)) > 0.01F ? 1 : 0;
            if (!wall && face.surface == scatter::Surface::kAsphalt) {
                past_edge += (core::at(p, 2) > kKerbZ + 0.01F || core::at(p, 0) < kRoadLeft - 0.01F ||
                              core::at(p, 0) > kRoadRight + 0.01F)
                                 ? 1
                                 : 0;
            }
        }
        if (!wall) {
            const LineCheck line = on_line(decal.centre);
            if (line.pipe || line.rail || line.ledge || line.steps) {
                ++on_lines;
                text::print("  (a decal at %.0f %.0f %.0f: pipe %d rail %d ledge %d steps %d)\n",
                            static_cast<double>(core::at(decal.centre, 0)), static_cast<double>(core::at(decal.centre, 1)),
                            static_cast<double>(core::at(decal.centre, 2)), line.pipe ? 1 : 0, line.rail ? 1 : 0,
                            line.ledge ? 1 : 0, line.steps ? 1 : 0);
            }
        }
    }
    const auto kind = [&one](scatter::DecalKind k) { return core::at(one.decals_of, static_cast<std::size_t>(k)); };
    tally->check(wrong_kind == 0,
                 text::format("every decal's kind its surface's (%d wrong): cracks %u, stains %u, oil %u, tyre %u, gum "
                              "%u, wet %u, grime %u",
                              wrong_kind, kind(scatter::DecalKind::kCrack), kind(scatter::DecalKind::kStain),
                              kind(scatter::DecalKind::kOil), kind(scatter::DecalKind::kTyre),
                              kind(scatter::DecalKind::kGum), kind(scatter::DecalKind::kWet),
                              kind(scatter::DecalKind::kGrime)));
    tally->check(off_plane == 0 && past_edge == 0,
                 text::format("every decal vertex on its face's plane (%d off), none past the kerb or the road's end (%d)",
                              off_plane, past_edge));
    tally->check(kind(scatter::DecalKind::kGrime) > 0 && kind(scatter::DecalKind::kWet) > 0 &&
                     kind(scatter::DecalKind::kTyre) > 0 && kind(scatter::DecalKind::kGum) > 0,
                 "grime at the walls' feet, wet patches in the gutter, tyre marks on the road, gum on the sidewalk");
    tally->check(on_lines == 0, text::format("no decal on a skate line - the quarter pipe, the rail, the ledge's top, the "
                                             "steps (%d)",
                                             on_lines));
    // the clutter
    int item_lines = 0;
    int hard_far = 0;
    int weeds_far = 0;
    int pebbles_off_dirt = 0;
    int on_lawn_not_leaf = 0;
    int at_stairs_foot = 0;
    for (const scatter::Item& item : one.items) {
        const auto k = static_cast<scatter::ItemKind>((item.packed >> scatter::kItemKindShift) & 0xFU);
        const float x = core::at(item.root, 0);
        const float z = core::at(item.root, 2);
        const LineCheck line = on_line(item.root);
        item_lines += line.pipe || line.rail || line.ledge || line.steps ? 1 : 0;
        const bool on_dirt = inside(x, z, kDirt, 0.0F);
        const bool on_lawn = inside(x, z, kLawn, 0.0F);
        if (k == scatter::ItemKind::kPebble) {
            pebbles_off_dirt += on_dirt ? 0 : 1;
        }
        if (on_lawn && k != scatter::ItemKind::kLeaf) {
            ++on_lawn_not_leaf;
        }
        if (!on_dirt && !on_lawn) {
            // hard ground: near the kerb (the road side), the building (the sidewalk side) or the ledge's foot
            const float to_kerb = std::abs(z - kKerbZ);
            const float to_building = std::abs(z - kBuildingZ);
            const float to_ledge = std::max(
                {core::at(kLedge, 0) - x, x - core::at(kLedge, 1), core::at(kLedge, 2) - z, z - core::at(kLedge, 3), 0.0F});
            const std::array<float, 4> stairs{core::at(kStairsX, 0), core::at(kStairsX, 1), kStairsZ, kStairsZ + (kStep * 5.0F)};
            const float to_stairs = std::max(
                {core::at(stairs, 0) - x, x - core::at(stairs, 1), core::at(stairs, 2) - z, z - core::at(stairs, 3), 0.0F});
            const float nearest = std::min({to_kerb, to_building, to_ledge, to_stairs});
            hard_far += nearest > scatter::kNearWall + 0.5F ? 1 : 0;
            if (k == scatter::ItemKind::kWeed) {
                weeds_far += nearest > scatter::kWeedReach + 0.5F ? 1 : 0;
            }
            at_stairs_foot += inside(x, z, {core::at(kStairsX, 0), core::at(kStairsX, 1), kStairsZ - scatter::kNearWall,
                                             kStairsZ},
                                     0.0F)
                                  ? 1
                                  : 0;
        }
    }
    const auto items = [&one](scatter::ItemKind k) { return core::at(one.items_of, static_cast<std::size_t>(k)); };
    tally->check(item_lines == 0 && at_stairs_foot == 0,
                 text::format("no item on a skate line (%d) or at the stairs' foot (%d)", item_lines, at_stairs_foot));
    tally->check(hard_far == 0 && weeds_far == 0,
                 text::format("hard ground's items all near a kerb or wall (%d far), weeds at their foot (%d far)", hard_far,
                              weeds_far));
    tally->check(pebbles_off_dirt == 0 && on_lawn_not_leaf == 0,
                 text::format("pebbles only on dirt (%d off it), only leaves on the lawn (%d other)", pebbles_off_dirt,
                              on_lawn_not_leaf));
    tally->check(items(scatter::ItemKind::kLeaf) > 0 && items(scatter::ItemKind::kPaper) > 0 &&
                     items(scatter::ItemKind::kCan) > 0 && items(scatter::ItemKind::kWeed) > 0 &&
                     items(scatter::ItemKind::kPebble) > 0,
                 text::format("every kind placed: leaves %u, paper %u, cans %u, weeds %u, pebbles %u",
                              items(scatter::ItemKind::kLeaf), items(scatter::ItemKind::kPaper),
                              items(scatter::ItemKind::kCan), items(scatter::ItemKind::kWeed),
                              items(scatter::ItemKind::kPebble)));
    const scatter::Rejected& r = one.item_rejected;
    const scatter::Rejected& d = one.decal_rejected;
    tally->check(d.ramp > 0 && d.rail > 0 && d.drop > 0 && r.drop > 0 && r.stairs > 0 && r.far_from_wall > 0,
                 text::format("the rules turned some away: decals %u ramp, %u rail, %u edge, %u covered; items %u ramp, %u "
                              "rail, %u edge, %u stairs, %u covered, %u far from walls",
                              d.ramp, d.rail, d.drop, d.covered, r.ramp, r.rail, r.drop, r.stairs, r.covered,
                              r.far_from_wall));
    // the decals' count against the rates: the road's area x 1.2e-4 x the density, less those turned away
    const double road = static_cast<double>(kRoadRight - kRoadLeft) * static_cast<double>(kKerbZ - kRoadNear);
    const double expected = road * 1.2e-4 * kDensity;
    const auto on_road = static_cast<double>(std::ranges::count_if(one.decals, [&faces](const scatter::Decal& decal) {
        return core::at(faces, decal.face).surface == scatter::Surface::kAsphalt;
    }));
    tally->check(on_road > expected * 0.45 && on_road < expected * 1.1,
                 text::format("%.0f decals on the road for %.0f the rate gives (wet patches out of the gutter and the "
                              "rules take some)",
                              on_road, expected));
    // the tiles cover every decal vertex and item once
    uint32_t vertices = 0;
    uint32_t tiled = 0;
    for (const scatter::Tile& tile : one.tiles) {
        vertices += tile.vertex_count;
        tiled += tile.item_count;
    }
    tally->check(std::cmp_equal(vertices, one.decal_vertices.size()) && std::cmp_equal(tiled, one.items.size()),
                 "the tiles hold every decal vertex and item once");
    // off: nothing
    const scatter::Scatter none =
        scatter::make_scatter({.faces = faces, .rails = rails, .seed = kSeed, .decal_density = 0.0F, .clutter_density = 0.0F});
    tally->check(none.decals.empty() && none.items.empty() && none.tiles.empty(), "both densities 0: nothing placed");
    // the art: the same bytes, every texel's multiplier at most 1 by construction, the meshes' size
    const std::vector<uint8_t> atlas = scatter::make_decal_atlas();
    if (!dump_folder().empty()) {
        std::vector<std::array<float, 3>> shown(static_cast<std::size_t>(scatter::kAtlasWidth) * scatter::kAtlasHeight);
        for (std::size_t texel = 0; texel < shown.size(); ++texel) {
            const float a = static_cast<float>(core::at(atlas, (texel * 4) + 3)) / 255.0F;
            for (std::size_t c = 0; c < 3; ++c) {
                const float m = static_cast<float>(core::at(atlas, (texel * 4) + c)) / 255.0F;
                core::at(core::at(shown, texel), c) = 0.6F * (1.0F + ((m - 1.0F) * a));
            }
        }
        dump_ppm("atlas.ppm", scatter::kAtlasWidth, scatter::kAtlasHeight, shown);
    }
    tally->check(atlas == scatter::make_decal_atlas() &&
                     atlas.size() == static_cast<std::size_t>(scatter::kAtlasWidth) * scatter::kAtlasHeight * 4 &&
                     scatter::make_meshes().size() == scatter::kItemKinds * scatter::kMeshVertices,
                 text::format("the atlas (%ux%u) and the meshes made the same every time", scatter::kAtlasWidth,
                              scatter::kAtlasHeight));
}

// --- the view's matrices ---------------------------------------------------------------------------------------

Matrix look_at(const Vec3& eye, const Vec3& at)
{
    const Vec3 z = unit(minus(at, eye));
    const Vec3 x = unit(cross({0.0F, 1.0F, 0.0F}, z));
    const Vec3 y = cross(z, x);
    return {{{core::at(x, 0), core::at(y, 0), core::at(z, 0), 0.0F},
             {core::at(x, 1), core::at(y, 1), core::at(z, 1), 0.0F},
             {core::at(x, 2), core::at(y, 2), core::at(z, 2), 0.0F},
             {-dot(x, eye), -dot(y, eye), -dot(z, eye), 1.0F}}};
}

struct Shot {
    Vec3 eye{};
    Vec3 at{};
    uint32_t width = kWidth;
    uint32_t height = kHeight;
};

render::rt::ViewDrawn view_of(const Shot& shot)
{
    render::rt::ViewDrawn view;
    view.rect = {0, 0, shot.width, shot.height};
    view.output_rect = view.rect;
    view.depth_range = {0.0F, 1.0F};
    auto& projection = view.camera.projection;
    const float aspect = static_cast<float>(shot.width) / static_cast<float>(shot.height);
    core::at(core::at(projection, 0), 0) = kVerticalScale / aspect;
    core::at(core::at(projection, 1), 1) = kVerticalScale;
    core::at(core::at(projection, 2), 2) = kFar / (kFar - kNear);
    core::at(core::at(projection, 2), 3) = 1.0F;
    core::at(core::at(projection, 3), 2) = -kNear * kFar / (kFar - kNear);
    view.camera.view = look_at(shot.eye, shot.at);
    view.camera.position = shot.eye;
    view.game_seconds = 1.0;
    return view;
}

std::array<float, 4> to_clip(const render::rt::ViewDrawn& view, const Vec3& point)
{
    const std::array<float, 4> in{core::at(point, 0), core::at(point, 1), core::at(point, 2), 1.0F};
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

void add_screen_quad(std::vector<ScreenVertex>* out, const render::rt::ViewDrawn& view, const std::array<Vec3, 4>& corners,
                     uint32_t colour)
{
    const auto width = static_cast<float>(core::at(view.rect, 2));
    const auto tall = static_cast<float>(core::at(view.rect, 3));
    for (const std::size_t which : {0U, 1U, 2U, 0U, 2U, 3U}) {
        const std::array<float, 4> clip = to_clip(view, core::at(corners, which));
        const float w = core::at(clip, 3);
        out->push_back({.x = ((core::at(clip, 0) / w * kHalf) + kHalf) * width,
                        .y = (kHalf - (core::at(clip, 1) / w * kHalf)) * tall,
                        .z = core::at(clip, 2) / w,
                        .rhw = 1.0F / w,
                        .colour = colour});
    }
}

// The road (y 0, from just in front of the camera far out) and the wall standing on it.
void draw_street(render::Gpu& gpu, const render::rt::ViewDrawn& view)
{
    gpu.set_viewport(
        {.x = 0, .y = 0, .width = core::at(view.rect, 2), .height = core::at(view.rect, 3), .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, kSkyColour, 1.0F, 0);
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kZFunc, value::kCompareAlways); // (in order: the wall over the road)
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
    const float front = core::at(kEye, 2) + 20.0F;
    add_screen_quad(&quads, view,
                    {{{kRoadLeft, 0.0F, front}, {kRoadRight, 0.0F, front}, {kRoadRight, 0.0F, kKerbZ}, {kRoadLeft, 0.0F, kKerbZ}}},
                    kRoadColour);
    const auto [ox0, ox1, oz, top] = kOccluder;
    add_screen_quad(&quads, view, {{{ox0, 0.0F, oz}, {ox1, 0.0F, oz}, {ox1, top, oz}, {ox0, top, oz}}}, kWallColour);
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(quads.size() / 3), quads.data(),
                         sizeof(ScreenVertex));
}

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

// The street drawn, then `passes` frames of the passes (0: the street alone).
void run_frames(render::Gpu& gpu, const Targets& targets, const render::rt::ViewDrawn& view, int passes)
{
    for (int pass = 0; pass < std::max(passes, 1); ++pass) {
        gpu.begin_scene();
        gpu.set_render_target(0, targets.colour);
        gpu.set_depth_surface(targets.depth);
        draw_street(gpu, view);
        if (passes > 0) {
            render::rt::opaque_view_drawn(view);
        }
        gpu.set_render_target(0, nullptr);
        gpu.set_depth_surface(nullptr);
        gpu.end_scene();
    }
}

struct Ray {
    Vec3 origin{};
    Vec3 direction{};
};

Ray ray_through(const render::rt::ViewDrawn& view, uint32_t column, uint32_t row)
{
    vk::Float4x4 camera{};
    vk::Float4x4 projection{};
    for (std::size_t line = 0; line < 4; ++line) {
        for (std::size_t c = 0; c < 4; ++c) {
            core::at(camera, (line * 4) + c) = core::at(core::at(view.camera.view, line), c);
            core::at(projection, (line * 4) + c) = core::at(core::at(view.camera.projection, line), c);
        }
    }
    const vk::Float4x4 view_projection = vk::motion_multiply(camera, projection);
    vk::Float4x4 inverse{};
    vk::motion_inverse(view_projection, &inverse);
    const float x = ((static_cast<float>(column) + kHalf) / static_cast<float>(core::at(view.rect, 2)) * 2.0F) - 1.0F;
    const float y = 1.0F - ((static_cast<float>(row) + kHalf) / static_cast<float>(core::at(view.rect, 3)) * 2.0F);
    std::array<Vec3, 2> ends{};
    for (std::size_t end = 0; end < 2; ++end) {
        const std::array<float, 4> clip{x, y, end == 0 ? 0.0F : 1.0F, 1.0F};
        std::array<float, 4> world{};
        for (std::size_t out = 0; out < 4; ++out) {
            for (std::size_t k = 0; k < 4; ++k) {
                core::at(world, out) += core::at(clip, k) * core::at(inverse, (k * 4) + out);
            }
        }
        core::at(ends, end) = {core::at(world, 0) / core::at(world, 3), core::at(world, 1) / core::at(world, 3),
                               core::at(world, 2) / core::at(world, 3)};
    }
    return {.origin = core::at(ends, 0), .direction = minus(core::at(ends, 1), core::at(ends, 0))};
}

// Where the ray reaches the height (x, z), if it does.
std::optional<std::array<float, 2>> at_height(const Ray& ray, float height)
{
    if (std::abs(core::at(ray.direction, 1)) < 1.0e-9F) {
        return std::nullopt;
    }
    const float along = (height - core::at(ray.origin, 1)) / core::at(ray.direction, 1);
    if (!(along > 0.0F)) {
        return std::nullopt;
    }
    return std::array<float, 2>{core::at(ray.origin, 0) + (along * core::at(ray.direction, 0)),
                                core::at(ray.origin, 2) + (along * core::at(ray.direction, 2))};
}

// Whether the ray meets the standing wall before the road (generously: its pixels' edges).
bool behind_wall(const Ray& ray, float margin)
{
    const auto [ox0, ox1, oz, top] = kOccluder;
    if (std::abs(core::at(ray.direction, 2)) < 1.0e-9F) {
        return false;
    }
    const float along = (oz - core::at(ray.origin, 2)) / core::at(ray.direction, 2);
    if (!(along > 0.0F)) {
        return false;
    }
    const float x = core::at(ray.origin, 0) + (along * core::at(ray.direction, 0));
    const float y = core::at(ray.origin, 1) + (along * core::at(ray.direction, 1));
    return x >= ox0 - margin && x <= ox1 + margin && y >= -margin && y <= top + margin;
}

bool changed(const std::array<float, 3>& before, const std::array<float, 3>& after)
{
    constexpr float kSame = 1.0e-3F;
    return std::abs(core::at(before, 0) - core::at(after, 0)) > kSame ||
           std::abs(core::at(before, 1) - core::at(after, 1)) > kSame ||
           std::abs(core::at(before, 2) - core::at(after, 2)) > kSame;
}

bool finite(const std::array<float, 3>& c)
{
    return std::isfinite(core::at(c, 0)) && std::isfinite(core::at(c, 1)) && std::isfinite(core::at(c, 2));
}

// The road alone, as faces (its decals at `height`: 0 on it, else over nothing).
std::vector<scatter::Face> road_faces(float height)
{
    std::vector<scatter::Face> faces;
    for (float x = kRoadLeft; x < kRoadRight; x += kRoadCell) {
        for (float z = kRoadNear; z < kKerbZ; z += kRoadCell) {
            add_flat(&faces, {x, x + kRoadCell, z, z + kRoadCell}, height, scatter::Surface::kAsphalt);
        }
    }
    return faces;
}

// (a decal at a wall's foot may take the wall's lowest pixels: its match along the view - vulkan_scatter.h)
constexpr float kWallFoot = 3.0F;

// The decals' pass: only darker, only within decals' footprints over the road, none behind the wall.
void check_decals(render::Gpu& gpu, Tally* tally)
{
    text::print("the decals:\n");
    const render::rt::ViewDrawn view = view_of({.eye = kEye, .at = kLookAt});
    const Targets targets = make_targets(gpu, 0);
    if (targets.colour == nullptr || targets.depth == nullptr) {
        tally->check(false, "the targets");
        release(gpu, targets);
        return;
    }
    Picture before;
    run_frames(gpu, targets, view, 0);
    const bool read_before = read_picture(targets.colour, &before);
    vk::set_scatter_settings({.decals = true, .decal_density = 4.0F, .clutter = false, .clutter_density = 1.0F});
    const std::vector<scatter::Face> faces = road_faces(0.0F);
    const auto made = std::make_shared<scatter::Scatter>(
        scatter::make_scatter({.faces = faces, .rails = {}, .seed = kSeed, .decal_density = 4.0F, .clutter_density = 0.0F}));
    vk::scatter_sink().set_scatter(made);
    const uint64_t passes = vk::decal_passes();
    run_frames(gpu, targets, view, 1);
    Picture after;
    const bool read_after = read_picture(targets.colour, &after);
    tally->check(read_before && read_after && vk::decal_passes() == passes + 1,
                 text::format("a pass recorded: %zu decals, %u vertices drawn", made->decals.size(),
                              vk::decal_last_vertices()));
    int moved = 0;
    int lighter = 0;
    int stray = 0;
    int hidden = 0;
    int bad = 0;
    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t column = 0; column < kWidth; ++column) {
            const std::size_t pixel = (static_cast<std::size_t>(row) * kWidth) + column;
            const auto& was = core::at(before, pixel);
            const auto& now = core::at(after, pixel);
            bad += finite(now) ? 0 : 1;
            if (!changed(was, now)) {
                continue;
            }
            ++moved;
            for (std::size_t c = 0; c < kChannels; ++c) {
                lighter += core::at(now, c) > core::at(was, c) + 1.0e-3F ? 1 : 0;
            }
            const Ray ray = ray_through(view, column, row);
            hidden += behind_wall(ray, -kWallFoot) ? 1 : 0;
            const std::optional<std::array<float, 2>> ground = at_height(ray, 0.0F);
            // (the pixel's own footprint on the ground: a far pixel spans much of it)
            float spread = 0.0F;
            for (const std::array<uint32_t, 2>& next : {std::array<uint32_t, 2>{column + 1, row},
                                                        std::array<uint32_t, 2>{column, row + 1}}) {
                const std::optional<std::array<float, 2>> other =
                    at_height(ray_through(view, core::at(next, 0), core::at(next, 1)), 0.0F);
                if (ground && other) {
                    spread += std::hypot(core::at(*other, 0) - core::at(*ground, 0),
                                         core::at(*other, 1) - core::at(*ground, 1));
                }
            }
            bool within = false;
            if (ground) {
                for (const scatter::Decal& decal : made->decals) {
                    const float dx = core::at(*ground, 0) - core::at(decal.centre, 0);
                    const float dz = core::at(*ground, 1) - core::at(decal.centre, 2);
                    const float reach = kHalf * std::sqrt((core::at(decal.size, 0) * core::at(decal.size, 0)) +
                                                          (core::at(decal.size, 1) * core::at(decal.size, 1))) +
                                        4.0F + spread;
                    if ((dx * dx) + (dz * dz) <= reach * reach) {
                        within = true;
                        break;
                    }
                }
            }
            stray += within || behind_wall(ray, kWallFoot) ? 0 : 1; // (the wall's foot: counted above)
        }
    }
    constexpr int kLeastMoved = 300;
    dump_ppm("decals.ppm", kWidth, kHeight, after);
    tally->check(moved > kLeastMoved && lighter == 0 && bad == 0,
                 text::format("%d pixels marked, each only darker (%d channels lighter), all finite (%d not)", moved,
                              lighter, bad));
    tally->check(stray == 0 && hidden == 0,
                 text::format("marks only within a decal's footprint on the road (%d stray), none behind the wall (%d)",
                              stray, hidden));
    // the same picture from a new scatter of the same faces
    vk::scatter_sink().set_scatter(std::make_shared<scatter::Scatter>(
        scatter::make_scatter({.faces = faces, .rails = {}, .seed = kSeed, .decal_density = 4.0F, .clutter_density = 0.0F})));
    run_frames(gpu, targets, view, 1);
    Picture again;
    tally->check(read_picture(targets.colour, &again) && again == after, "the same picture again from a new scatter");
    // decals over nothing (30 units above the drawn road): no mark
    const std::vector<scatter::Face> floating = road_faces(30.0F);
    vk::scatter_sink().set_scatter(std::make_shared<scatter::Scatter>(scatter::make_scatter(
        {.faces = floating, .rails = {}, .seed = kSeed, .decal_density = 4.0F, .clutter_density = 0.0F})));
    run_frames(gpu, targets, view, 1);
    Picture over;
    int over_moved = 0;
    if (read_picture(targets.colour, &over)) {
        for (uint32_t row = 0; row < kHeight; ++row) {
            for (uint32_t column = 0; column < kWidth; ++column) {
                const std::size_t pixel = (static_cast<std::size_t>(row) * kWidth) + column;
                // (the wall stands through their plane: where it crosses it, it may be marked)
                if (changed(core::at(before, pixel), core::at(over, pixel)) &&
                    !behind_wall(ray_through(view, column, row), kWallFoot)) {
                    ++over_moved;
                }
            }
        }
    }
    tally->check(!over.empty() && over_moved == 0,
                 text::format("decals 30 units over the road (over nothing): %d pixels marked", over_moved));
    release(gpu, targets);
}

// The clutter's pass: items near the kerb's foot only, finite, the same picture again.
void check_clutter(render::Gpu& gpu, Tally* tally)
{
    text::print("the clutter:\n");
    const render::rt::ViewDrawn view = view_of({.eye = {0.0F, 60.0F, 450.0F}, .at = {0.0F, 0.0F, 800.0F}});
    const Targets targets = make_targets(gpu, 0);
    if (targets.colour == nullptr || targets.depth == nullptr) {
        tally->check(false, "the targets");
        release(gpu, targets);
        return;
    }
    vk::set_scatter_settings({});
    Picture before;
    run_frames(gpu, targets, view, 0);
    const bool read_before = read_picture(targets.colour, &before);
    vk::set_scatter_settings({.decals = false, .decal_density = 1.0F, .clutter = true, .clutter_density = 4.0F});
    std::vector<scatter::Face> faces = road_faces(0.0F);
    add_wall_x(&faces, {kRoadLeft, kRoadRight}, kKerbZ, 0.0F, kKerbHeight, scatter::Surface::kConcrete);
    const auto made = std::make_shared<scatter::Scatter>(
        scatter::make_scatter({.faces = faces, .rails = {}, .seed = kSeed, .decal_density = 0.0F, .clutter_density = 4.0F}));
    vk::scatter_sink().set_scatter(made);
    const uint64_t passes = vk::clutter_passes();
    run_frames(gpu, targets, view, kLearningPasses);
    Picture after;
    const bool read_after = read_picture(targets.colour, &after);
    tally->check(read_before && read_after && vk::clutter_passes() == passes + kLearningPasses && !made->items.empty(),
                 text::format("%d passes recorded: %zu items, %u drawn the last", kLearningPasses, made->items.size(),
                              vk::clutter_last_items()));
    int moved = 0;
    int stray = 0;
    int bad = 0;
    constexpr float kTallestItem = 12.0F;
    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t column = 0; column < kWidth; ++column) {
            const std::size_t pixel = (static_cast<std::size_t>(row) * kWidth) + column;
            const auto& now = core::at(after, pixel);
            bad += finite(now) ? 0 : 1;
            if (!changed(core::at(before, pixel), now)) {
                continue;
            }
            ++moved;
            const Ray ray = ray_through(view, column, row);
            bool near_kerb = false;
            constexpr int kSteps = 12;
            for (int step = 0; step <= kSteps && !near_kerb; ++step) {
                const std::optional<std::array<float, 2>> at =
                    at_height(ray, kTallestItem * static_cast<float>(step) / static_cast<float>(kSteps));
                near_kerb = at && core::at(*at, 1) >= kKerbZ - scatter::kNearWall - kTallestItem &&
                            core::at(*at, 1) <= kKerbZ + kTallestItem;
            }
            stray += near_kerb ? 0 : 1;
        }
    }
    dump_ppm("clutter.ppm", kWidth, kHeight, after);
    tally->check(moved > 50 && stray == 0 && bad == 0,
                 text::format("%d pixels show items, all along the kerb's foot (%d stray), all finite (%d not)", moved,
                              stray, bad));
    vk::scatter_sink().set_scatter(std::make_shared<scatter::Scatter>(
        scatter::make_scatter({.faces = faces, .rails = {}, .seed = kSeed, .decal_density = 0.0F, .clutter_density = 4.0F})));
    run_frames(gpu, targets, view, kLearningPasses);
    Picture again;
    tally->check(read_picture(targets.colour, &again) && again == after, "the same picture again from a new scatter");
    release(gpu, targets);
}

// A decal quad of one kind over x from - to, z from - to on the road (y 0), whole (opacity 1, no fade near).
void add_decal_quad(scatter::Scatter* made, scatter::DecalKind kind, const std::array<float, 4>& area)
{
    constexpr uint32_t kOpaque = 255U << 8U;
    constexpr uint32_t kFarFade = 255U << 16U;
    const uint32_t packed = static_cast<uint32_t>(kind) | kOpaque | kFarFade;
    const auto [x0, x1, z0, z1] = area;
    const uint32_t first = static_cast<uint32_t>(made->decal_vertices.size());
    const std::array<std::array<float, 4>, 6> corners{{{x0, z0, 0.0F, 0.0F},
                                                       {x1, z0, 1.0F, 0.0F},
                                                       {x1, z1, 1.0F, 1.0F},
                                                       {x0, z0, 0.0F, 0.0F},
                                                       {x1, z1, 1.0F, 1.0F},
                                                       {x0, z1, 0.0F, 1.0F}}};
    for (const auto& c : corners) {
        made->decal_vertices.push_back({.position = {core::at(c, 0), 0.0F, core::at(c, 1)},
                                        .uv = {core::at(c, 2), core::at(c, 3)},
                                        .packed = packed});
    }
    made->decals.push_back({.centre = {(x0 + x1) * kHalf, 0.0F, (z0 + z1) * kHalf},
                            .normal = {0.0F, 1.0F, 0.0F},
                            .kind = kind,
                            .variant = 0,
                            .size = {x1 - x0, z1 - z0},
                            .face = 0,
                            .first_vertex = first,
                            .vertex_count = 6});
    ++core::at(made->decals_of, static_cast<std::size_t>(kind));
    made->decal_density = 1.0F;
}

// The puddles (with ray tracing): the decals' pass draws the wet mask only under wet patches while the reflections run,
// and the reflections' trace with the puddles compiles.
void check_puddles(render::Gpu& gpu, Tally* tally)
{
    text::print("the puddles:\n");
    vk::Backend& state = vk::backend();
    if (!state.context.ray_tracing_enabled()) {
        text::print("  (no ray tracing on this device: the puddles' reflections not tried)\n");
        return;
    }
    const render::rt::ViewDrawn view = view_of({.eye = kEye, .at = kLookAt});
    const Targets targets = make_targets(gpu, 0);
    constexpr std::array<float, 4> kPuddle{-420.0F, -180.0F, 260.0F, 400.0F}; // (beside the wall, not behind it)
    constexpr std::array<float, 4> kStain{200.0F, 320.0F, 300.0F, 400.0F};
    auto made = std::make_shared<scatter::Scatter>();
    add_decal_quad(made.get(), scatter::DecalKind::kWet, kPuddle);
    add_decal_quad(made.get(), scatter::DecalKind::kStain, kStain);
    made->tiles.push_back({.low = {-420.0F, 0.0F, 260.0F},
                           .high = {320.0F, 0.0F, 400.0F},
                           .first_vertex = 0,
                           .vertex_count = static_cast<uint32_t>(made->decal_vertices.size()),
                           .first_item = 0,
                           .item_count = 0});
    vk::set_scatter_settings({.decals = true, .decal_density = 1.0F, .clutter = false, .clutter_density = 1.0F});
    vk::scatter_sink().set_scatter(made);
    // reflections off: no mask
    const uint64_t masks = vk::wet_masks_drawn();
    run_frames(gpu, targets, view, 1);
    tally->check(vk::wet_masks_drawn() == masks, "rt_reflections off: no wet mask drawn");
    vk::set_rt_reflection_settings({.on = true, .strength = 1.0F});
    run_frames(gpu, targets, view, 1);
    std::vector<uint8_t> wet;
    uint32_t wet_width = 0;
    bool read = false;
    {
        const std::scoped_lock lock(state.mutex);
        read = vk::read_wet_mask(state, &wet, &wet_width);
    }
    if (read && wet_width >= kWidth) {
        std::vector<std::array<float, 3>> shown(static_cast<std::size_t>(kWidth) * kHeight);
        for (uint32_t row = 0; row < kHeight; ++row) {
            for (uint32_t column = 0; column < kWidth; ++column) {
                const float w = static_cast<float>(core::at(wet, (static_cast<std::size_t>(row) * wet_width) + column)) / 255.0F;
                core::at(shown, (static_cast<std::size_t>(row) * kWidth) + column) = {w, w, w};
            }
        }
        dump_ppm("wet.ppm", kWidth, kHeight, shown);
        Picture picture;
        if (read_picture(targets.colour, &picture)) {
            dump_ppm("puddle.ppm", kWidth, kHeight, picture);
        }
    }
    int wet_pixels = 0;
    int stray = 0;
    for (uint32_t row = 0; row < kHeight && read && wet_width >= kWidth; ++row) {
        for (uint32_t column = 0; column < kWidth; ++column) {
            if (core::at(wet, (static_cast<std::size_t>(row) * wet_width) + column) == 0) {
                continue;
            }
            ++wet_pixels;
            const std::optional<std::array<float, 2>> at = at_height(ray_through(view, column, row), 0.0F);
            stray += at && inside(core::at(*at, 0), core::at(*at, 1), kPuddle, 8.0F) ? 0 : 1;
        }
    }
    tally->check(vk::wet_masks_drawn() == masks + 1 && read && wet_pixels > 100 && stray == 0,
                 text::format("rt_reflections on: the wet mask drawn - %d wet pixels, all over the puddle (%d not; the "
                              "stain beside it none)",
                              wet_pixels, stray));
    bool compiled = false;
    {
        const std::scoped_lock lock(state.mutex);
        compiled = vk::make_rt_reflection_wet_trace(state);
    }
    tally->check(compiled, "the reflections' trace with the puddles made (with and without the sky)");
    vk::set_rt_reflection_settings({});
    vk::set_scatter_settings({});
    release(gpu, targets);
}

void check_multisampled(render::Gpu& gpu, Tally* tally)
{
    text::print("four samples:\n");
    const Targets targets = make_targets(gpu, kFourSamples);
    if (targets.colour == nullptr || targets.depth == nullptr) {
        tally->check(false, "the multisampled targets");
        release(gpu, targets);
        return;
    }
    vk::set_scatter_settings({.decals = true, .decal_density = 2.0F, .clutter = true, .clutter_density = 2.0F});
    std::vector<scatter::Face> faces = road_faces(0.0F);
    add_wall_x(&faces, {kRoadLeft, kRoadRight}, kKerbZ, 0.0F, kKerbHeight, scatter::Surface::kConcrete);
    vk::scatter_sink().set_scatter(std::make_shared<scatter::Scatter>(
        scatter::make_scatter({.faces = faces, .rails = {}, .seed = kSeed, .decal_density = 2.0F, .clutter_density = 2.0F})));
    const uint64_t decals = vk::decal_passes();
    const uint64_t clutter = vk::clutter_passes();
    run_frames(gpu, targets, view_of({.eye = kEye, .at = kLookAt}), 2);
    tally->check(vk::decal_passes() == decals + 2 && vk::clutter_passes() == clutter + 2,
                 "both passes run on four samples");
    release(gpu, targets);
}

void check_off(render::Gpu& gpu, Tally* tally)
{
    text::print("off:\n");
    const render::rt::ViewDrawn view = view_of({.eye = kEye, .at = kLookAt});
    const Targets targets = make_targets(gpu, 0);
    vk::set_scatter_settings({});
    Picture first;
    run_frames(gpu, targets, view, 1);
    const bool read_first = read_picture(targets.colour, &first);
    vk::set_scatter_settings({.decals = true, .decal_density = 4.0F, .clutter = true, .clutter_density = 4.0F});
    std::vector<scatter::Face> faces = road_faces(0.0F);
    add_wall_x(&faces, {kRoadLeft, kRoadRight}, kKerbZ, 0.0F, kKerbHeight, scatter::Surface::kConcrete);
    vk::scatter_sink().set_scatter(std::make_shared<scatter::Scatter>(
        scatter::make_scatter({.faces = faces, .rails = {}, .seed = kSeed, .decal_density = 4.0F, .clutter_density = 4.0F})));
    run_frames(gpu, targets, view, 2);
    Picture on;
    const bool read_on = read_picture(targets.colour, &on);
    vk::set_scatter_settings({});
    const uint64_t decals = vk::decal_passes();
    const uint64_t clutter = vk::clutter_passes();
    run_frames(gpu, targets, view, 1);
    Picture off;
    const bool read_off = read_picture(targets.colour, &off);
    tally->check(read_first && read_on && on != first, "on: the picture changes");
    tally->check(read_off && off == first && vk::decal_passes() == decals && vk::clutter_passes() == clutter &&
                     render::scatter::sink() == nullptr,
                 "off: no pass, no sink, the picture exactly as before it was on");
    release(gpu, targets);
}

// The passes' time on the device at 2560x1440 (the defaults; a street of decals and clutter at a skater's eye): printed.
void measure_cost(render::Gpu& gpu, Tally* tally)
{
    constexpr int kTimed = 20;
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    constexpr uint32_t kBigWidth = 2560;
    constexpr uint32_t kBigHeight = 1440;
    vk::set_scatter_settings({.decals = true, .decal_density = 1.0F, .clutter = true, .clutter_density = 1.0F});
    std::vector<scatter::Face> faces = road_faces(0.0F);
    add_wall_x(&faces, {kRoadLeft, kRoadRight}, kKerbZ, 0.0F, kKerbHeight, scatter::Surface::kConcrete);
    const auto made = std::make_shared<scatter::Scatter>(
        scatter::make_scatter({.faces = faces, .rails = {}, .seed = kSeed, .decal_density = 1.0F, .clutter_density = 1.0F}));
    vk::scatter_sink().set_scatter(made);
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
        const render::rt::ViewDrawn view =
            view_of({.eye = {0.0F, 70.0F, 300.0F}, .at = {0.0F, 0.0F, 800.0F}, .width = kBigWidth, .height = kBigHeight});
        run_frames(gpu, targets, view, kLearningPasses);
        gpu.begin_scene();
        gpu.set_render_target(0, targets.colour);
        gpu.set_depth_surface(targets.depth);
        draw_street(gpu, view);
        {
            const std::scoped_lock lock(state.mutex);
            VkCommandBuffer commands = vk::outside_pass(state);
            vkCmdResetQueryPool(commands, pool, 0, 2);
            vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
        }
        for (int i = 0; i < kTimed; ++i) {
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
        const double each = static_cast<double>(core::at(stamps, 1) - core::at(stamps, 0)) *
                            static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond / kTimed;
        text::print("the cost: %.3f ms for both passes at %ux%u (%u decal vertices, %u items; density 1, a street at a "
                    "skater's eye)\n",
                    each, kBigWidth, kBigHeight, vk::decal_last_vertices(), vk::clutter_last_items());
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
    vk::set_context_options({.validation = options.validation,
                             .device_index = options.device,
                             .on_message = print_line,
                             .ray_tracing = true},
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
    check_placement(&tally);
    if (!open_device(options, window)) {
        return vk::loader_version() == 0 ? 2 : 1;
    }
    render::Gpu& gpu = vk::gpu();
    check_decals(gpu, &tally);
    check_clutter(gpu, &tally);
    check_puddles(gpu, &tally);
    check_multisampled(gpu, &tally);
    check_off(gpu, &tally);
    measure_cost(gpu, &tally);
    vk::set_scatter_settings({});
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
    dump_folder() = options.dump;
    text::print("thug2_vulkan_scatter_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    // (the placement needs no device)
    if (!vk::open_loader()) {
        Tally tally;
        check_placement(&tally);
        text::print("no Vulkan loader (vulkan-1.dll) for this process\n");
        return 2;
    }
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        text::print("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int position = window_position(options.display);
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan scatter check", kWindowWidth, kWindowHeight,
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
