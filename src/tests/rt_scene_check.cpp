// NOT ORIGINAL - thug2_rt_scene_check.exe: ray tracing's pure helpers (engine/gfx/rt_checks.h) - a Direct3D matrix as
// object-to-world rows, strips as lists, the geometry and flag tests the contract check makes - and the baked sun's fit
// (engine/gfx/rt_sun.h) against a small scene traced on the processor. Exit code 0 when every check passes.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_checks.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"
#include "tests/no_error_dialogs.h"

// The buffers' handles are incomplete types outside the backends: here two stand-ins, never read.
namespace render {
struct GpuVertexBuffer {
    int unused;
};
struct GpuIndexBuffer {
    int unused;
};
} // namespace render

namespace {

namespace rt = render::rt;

constexpr float kNear = 1.0e-5F;      // two points alike
constexpr float kTiny = 1.0e-6F;      // a change same_rows overlooks
constexpr float kMove = 1.0F;         // one it doesn't
constexpr std::size_t kTranslation = 12; // a Direct3D matrix's translation: its last row

int& failures()
{
    static int count = 0;
    return count;
}

void expect(bool ok, const char* what)
{
    if (!ok) {
        ++failures();
        std::printf("FAIL: %s\n", what);
    }
}

bool near3(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(std::fabs(core::at(a, i) - core::at(b, i)) < kNear)) {
            return false;
        }
    }
    return true;
}

// point * M with a Direct3D matrix (row vectors; the point's w 1).
std::array<float, 3> d3d_transform(const std::array<float, rt::kMatrixFloats>& matrix, const std::array<float, 3>& point)
{
    const std::array<float, 4> row_vector = {core::at(point, 0), core::at(point, 1), core::at(point, 2), 1.0F};
    std::array<float, 3> out{};
    for (std::size_t j = 0; j < 3; ++j) {
        for (std::size_t k = 0; k < 4; ++k) {
            core::at(out, j) += core::at(row_vector, k) * core::at(matrix, (k * 4) + j);
        }
    }
    return out;
}

float& cell(rt::Rows& rows, std::size_t row, std::size_t column) { return core::at(core::at(rows, row), column); }
float cell(const rt::Rows& rows, std::size_t row, std::size_t column) { return core::at(core::at(rows, row), column); }

void check_transforms()
{
    // A turn of 90 degrees about y, a scale of 2 and a move by (10, 20, 30), as the game's matrices hold them.
    const std::array<float, rt::kMatrixFloats> matrix = {0.0F, 0.0F, -2.0F, 0.0F, 0.0F,  2.0F,  0.0F,  0.0F,
                                                         2.0F, 0.0F, 0.0F,  0.0F, 10.0F, 20.0F, 30.0F, 1.0F};
    const std::array<float, rt::kMatrixFloats> identity = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                                           0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    const std::array<std::array<float, 3>, 4> points = {
        {{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {-3.5F, 2.25F, 7.0F}}};
    const rt::Rows rows = rt::object_to_world_rows(matrix);
    for (const std::array<float, 3>& point : points) {
        expect(near3(rt::apply_rows(rows, point), d3d_transform(matrix, point)), "object_to_world_rows agrees with p * M");
    }
    bool moved = true;
    for (std::size_t i = 0; i < 3; ++i) {
        moved = moved && cell(rows, i, 3) == core::at(matrix, kTranslation + i);
    }
    expect(moved, "the translation in the rows' last column");
    expect(rt::object_to_world_rows(identity) == rt::identity_rows(), "the identity");
    rt::Rows other = rows;
    cell(other, 1, 3) += kTiny;
    expect(rt::same_rows(rows, other), "same_rows within the tolerance");
    cell(other, 1, 3) += kMove;
    expect(!rt::same_rows(rows, other), "same_rows tells a moved transform");
    other = rows;
    cell(other, 2, 1) = std::nanf("");
    expect(!rt::finite_rows(other) && rt::finite_rows(rows), "finite_rows");
    expect(!rt::same_rows(other, other), "a NaN is never the same");
}

struct CountCase {
    uint32_t indices;
    bool strip;
    uint32_t triangles;
};

void check_strips()
{
    const std::array<CountCase, 6> counts = {{{.indices = 0, .strip = true, .triangles = 0},
                                              {.indices = 2, .strip = true, .triangles = 0},
                                              {.indices = 3, .strip = true, .triangles = 1},
                                              {.indices = 10, .strip = true, .triangles = 8},
                                              {.indices = 9, .strip = false, .triangles = 3},
                                              {.indices = 10, .strip = false, .triangles = 3}}};
    for (const CountCase& count : counts) {
        expect(rt::triangle_count(count.indices, count.strip) == count.triangles, "a draw's triangle count");
    }
    // Two strips joined by repeating 3 and 4: (0 1 2) (2 1 3) then the joins, then (4 5 6) (6 5 7).
    const std::array<uint16_t, 10> strip = {0, 1, 2, 3, 3, 4, 4, 5, 6, 7};
    const std::vector<uint32_t> wanted = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
    std::vector<uint32_t> list;
    const uint32_t kept = rt::strip_to_list<uint16_t>(strip, list);
    expect(kept == wanted.size() / 3, "the joins' degenerate triangles left out");
    expect(list == wanted, "every second triangle turned back");
    std::vector<uint32_t> none;
    expect(rt::strip_to_list<uint32_t>(std::span<const uint32_t>(), none) == 0 && none.empty(), "an empty strip");
}

void check_geometry()
{
    render::GpuVertexBuffer vertex_buffer{};
    render::GpuIndexBuffer index_buffer{};
    const std::array<uint16_t, 6> indices = {0, 1, 2, 3, 2, 1};
    const rt::Geometry strip{.vertices = &vertex_buffer,
                             .vertex_offset = 64,
                             .vertex_stride = 24,
                             .vertex_count = 4,
                             .position_offset = 0,
                             .indices = &index_buffer,
                             .index_offset = 0,
                             .index_count = 6,
                             .indices_32bit = false,
                             .strip = true,
                             .dynamic = false,
                             .vertex_buffer_bytes = 160,
                             .index_buffer_bytes = 12,
                             .cpu_indices = indices.data()};
    expect(rt::geometry_problem(strip).empty(), "a good mesh passes");
    rt::Geometry good = strip;
    good.strip = false;
    expect(rt::geometry_problem(good).empty(), "a list of 6 passes");
    rt::Geometry bad = good;
    bad.index_count = good.index_count - 1;
    expect(!rt::geometry_problem(bad).empty(), "a list of 5 indices");
    bad = good;
    bad.vertex_buffer_bytes = good.vertex_buffer_bytes - 1;
    expect(!rt::geometry_problem(bad).empty(), "vertices past the buffer");
    bad = good;
    bad.index_buffer_bytes = good.index_buffer_bytes - sizeof(uint16_t);
    expect(!rt::geometry_problem(bad).empty(), "indices past the buffer");
    bad = good;
    bad.vertex_count = good.vertex_count - 1;
    bad.vertex_buffer_bytes = 0;
    expect(!rt::geometry_problem(bad).empty(), "an index past the vertices");
    bad = good;
    bad.position_offset = good.vertex_stride - sizeof(float);
    expect(!rt::geometry_problem(bad).empty(), "a position past the vertex");
    bad = good;
    bad.vertices = nullptr;
    expect(!rt::geometry_problem(bad).empty(), "no vertex buffer");
    bad = good;
    bad.vertex_buffer_bytes = 0;
    bad.index_buffer_bytes = 0;
    bad.cpu_indices = nullptr;
    expect(rt::geometry_problem(bad).empty(), "sizes not known: not checked");
}

void check_flags()
{
    expect(rt::flags_problem(rt::kWorld | rt::kOpaque | rt::kCastsShadow).empty(), "an opaque world mesh");
    expect(rt::flags_problem(rt::kObject | rt::kSkinned | rt::kAlphaTested | rt::kCastsShadow).empty(),
           "an alpha-tested skinned object");
    expect(rt::flags_problem(rt::kObject | rt::kTransparent).empty(), "a transparent object");
    expect(!rt::flags_problem(rt::kOpaque).empty(), "neither world nor object");
    expect(!rt::flags_problem(rt::kWorld | rt::kObject | rt::kOpaque).empty(), "world and object");
    expect(!rt::flags_problem(rt::kWorld).empty(), "no surface kind");
    expect(!rt::flags_problem(rt::kWorld | rt::kOpaque | rt::kTransparent).empty(), "opaque and transparent");
    expect(!rt::flags_problem(rt::kWorld | rt::kSkinned | rt::kOpaque).empty(), "a skinned world mesh");
}

} // namespace

// --- the baked sun ---------------------------------------------------------------------------------------------

namespace {

constexpr float kFar = 1.0e6F;           // a ray's length
constexpr float kParallel = 1e-9F;       // a ray this close to parallel to a slab misses it unless inside
constexpr float kRayStart = 2.0F;        // the fit's rays start this far above the ground (rt_sun.cpp kOffset)
constexpr float kLit = 200.0F;           // the bake's brightness in the sun ...
constexpr float kShaded = 70.0F;         // ... and in a shadow
constexpr float kLampPool = 220.0F;      // an indoor bake: lamps' pools ...
constexpr float kLampDark = 90.0F;       // ... and the rest
constexpr std::size_t kLampEvery = 7;    // (every 7th sample in a pool)
constexpr float kNearOne = 0.99F;        // a score this near 1 (or -1)
constexpr float kUnitSlack = 1e-4F;      // a direction's length this near 1
constexpr std::size_t kSkyCount = 100;
constexpr float kConeDegrees = 10.0F;
constexpr std::size_t kConeCount = 30;
constexpr float kConeSlack = 0.01F;      // degrees
constexpr float kFoundWithin = 4.0F;     // degrees: the fit finds the sun again this close
constexpr float kAgrees = 0.9F;          // agreement this high in the bake's own shadow ...
constexpr float kDisagrees = 0.1F;       // ... and this low under a caster it left out
constexpr float kDegreesPerRadian = 57.2957795F;
constexpr float kRadiansPerDegree = 0.0174532925F;
constexpr int kGroundHalf = 60; // the ground's samples: a grid from -60 to 60 ...
constexpr int kGroundStep = 3;  // ... every 3 units

struct Box {
    rt::Vec3 low{};
    rt::Vec3 high{};
};

// The scenes: a wall and a pillar standing on the ground; a canopy over one corner (x, z 25 - 70); a roof over all.
const Box kWall{.low = {5.0F, 0.0F, -70.0F}, .high = {12.0F, 40.0F, 10.0F}};
const Box kPillar{.low = {-35.0F, 0.0F, 20.0F}, .high = {-28.0F, 30.0F, 27.0F}};
const Box kCanopy{.low = {25.0F, 20.0F, 25.0F}, .high = {70.0F, 22.0F, 70.0F}};
const Box kRoof{.low = {-100.0F, 50.0F, -100.0F}, .high = {100.0F, 52.0F, 100.0F}};
const rt::Vec3 kSun{0.62F, 0.55F, -0.56F};      // toward the sun the bake was lit by
const rt::Vec3 kInWallShadow{-10.0F, 0.0F, -10.0F}; // a point in the wall's shadow
const rt::Vec3 kUnderCanopy{40.0F, 0.0F, 50.0F};   // one in the canopy's

// A scene of boxes traced on the processor (slabs) - the fit's tracer.
class BoxTracer final : public rt::RayTracing {
public:
    explicit BoxTracer(std::vector<Box> boxes) : boxes_(std::move(boxes)) {}
    [[nodiscard]] bool available() const override { return true; }
    void add_mesh(rt::MeshId /*mesh*/, const rt::Geometry& /*geometry*/) override {}
    void remove_mesh(rt::MeshId /*mesh*/) override {}
    void mesh_changed(rt::MeshId /*mesh*/) override {}
    void begin_frame(const rt::FrameRecord& /*record*/) override {}
    rt::ProbeResult probe(const rt::ProbeRequest& ray) override
    {
        return {.traced = true, .hit = std::ranges::any_of(boxes_, [&ray](const Box& box) { return hits(box, ray); })};
    }
    [[nodiscard]] bool blocked(const rt::Vec3& from, const rt::Vec3& toward)
    {
        return probe({.origin = from, .direction = toward, .max_distance = kFar}).hit;
    }

private:
    static bool hits(const Box& box, const rt::ProbeRequest& ray)
    {
        float enter = 0.0F;
        float leave = ray.max_distance;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const float origin = core::at(ray.origin, axis);
            const float way = core::at(ray.direction, axis);
            if (std::fabs(way) < kParallel) {
                if (origin < core::at(box.low, axis) || origin > core::at(box.high, axis)) {
                    return false;
                }
                continue;
            }
            const float to_low = (core::at(box.low, axis) - origin) / way;
            const float to_high = (core::at(box.high, axis) - origin) / way;
            enter = std::max(enter, std::min(to_low, to_high));
            leave = std::min(leave, std::max(to_low, to_high));
            if (enter > leave) {
                return false;
            }
        }
        return true;
    }
    std::vector<Box> boxes_;
};

// Ground samples on a grid, bright where `sun` reaches them through `tracer`'s boxes, dark where it doesn't.
std::vector<rt::BakeSample> ground_samples(BoxTracer& tracer, const rt::Vec3& sun)
{
    std::vector<rt::BakeSample> samples;
    for (int across = -kGroundHalf; across <= kGroundHalf; across += kGroundStep) {
        for (int along = -kGroundHalf; along <= kGroundHalf; along += kGroundStep) {
            const rt::Vec3 at{static_cast<float>(across), 0.0F, static_cast<float>(along)};
            // (bright or dark by a ray from a little above, as the fit's own rays start)
            const bool lit = !tracer.blocked({rt::x_of(at), kRayStart, rt::z_of(at)}, sun);
            samples.push_back({.position = at, .normal = {0.0F, 1.0F, 0.0F}, .brightness = lit ? kLit : kShaded});
        }
    }
    return samples;
}

float degrees_between(const rt::Vec3& one, const rt::Vec3& other)
{
    const float lengths = std::sqrt(rt::dot(one, one) * rt::dot(other, other));
    return std::acos(std::clamp(rt::dot(one, other) / lengths, -1.0F, 1.0F)) * kDegreesPerRadian;
}

void check_sun_helpers()
{
    // the score: lit samples brighter than shaded ones score near 1; the other way round near -1; no shade at all 0
    const std::vector<rt::BakeSample> two{{.brightness = kLit}, {.brightness = kShaded}};
    const std::vector<uint8_t> right{1, 0};
    const std::vector<uint8_t> wrong{0, 1};
    const std::vector<uint8_t> none{1, 1};
    expect(rt::sun_score(two, right) > kNearOne, "sun_score: lit bright, shaded dark - near 1");
    expect(rt::sun_score(two, wrong) < -kNearOne, "sun_score: the other way round - near -1");
    expect(rt::sun_score(two, none) == 0.0F, "sun_score: no sample shaded - 0");
    // the sky's directions: above the lowest elevation, unit length; a cone's within its angle
    const float lowest = std::sin(rt::kMinElevationDegrees * kRadiansPerDegree) - kUnitSlack;
    expect(std::ranges::all_of(rt::sky_directions(kSkyCount),
                               [lowest](const rt::Vec3& toward) {
                                   return rt::y_of(toward) >= lowest &&
                                          std::fabs(rt::dot(toward, toward) - 1.0F) < kUnitSlack;
                               }),
           "sky_directions: unit length, above the lowest elevation");
    expect(std::ranges::all_of(rt::directions_round({.centre = kSun, .degrees = kConeDegrees, .count = kConeCount}),
                               [](const rt::Vec3& toward) {
                                   return degrees_between(toward, kSun) <= kConeDegrees + kConeSlack;
                               }),
           "directions_round: within the angle");
    expect(rt::agreement_at(rt::BakedSun{}, {}) == 1.0F, "agreement_at: no grid - 1");
}

void check_baked_sun()
{
    check_sun_helpers();
    // the bake lit from a known sun past a wall and a pillar: the fit finds it again
    BoxTracer outdoors({kWall, kPillar});
    const std::vector<rt::BakeSample> samples = ground_samples(outdoors, kSun);
    const rt::SunFit fit = rt::fit_baked_sun(samples, outdoors);
    const float off_by = degrees_between(fit.sun.toward, kSun);
    std::printf("baked sun: found %d toward %.3f %.3f %.3f (%.1f degrees off), confidence %.2f, %zu rays\n",
                fit.sun.found ? 1 : 0, static_cast<double>(rt::x_of(fit.sun.toward)),
                static_cast<double>(rt::y_of(fit.sun.toward)), static_cast<double>(rt::z_of(fit.sun.toward)),
                static_cast<double>(off_by), static_cast<double>(fit.sun.confidence), fit.rays);
    expect(fit.sun.found && off_by < kFoundWithin, "fit_baked_sun: the sun found again");
    // where the bake agrees: the wall's shadow is in the bake; the canopy's isn't
    BoxTracer canopied({kWall, kPillar, kCanopy});
    std::vector<uint8_t> lit(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const rt::Vec3& at = core::at(samples, i).position;
        core::at(lit, i) = canopied.blocked({rt::x_of(at), kRayStart, rt::z_of(at)}, kSun) ? 0 : 1;
    }
    rt::BakedSun agreed = fit.sun;
    rt::make_agreement(samples, lit, &agreed);
    const float in_shadow = rt::agreement_at(agreed, kInWallShadow);
    const float under_canopy = rt::agreement_at(agreed, kUnderCanopy);
    std::printf("agreement: %ux%u cells, %.2f in the wall's shadow, %.2f under the canopy\n", agreed.columns, agreed.rows,
                static_cast<double>(in_shadow), static_cast<double>(under_canopy));
    expect(in_shadow > kAgrees, "make_agreement: the bake's own shadow agrees");
    expect(under_canopy < kDisagrees, "make_agreement: lit under a caster the bake left out - no agreement");
    // a roof over everything and the bake from lamps (nothing of the sun): no sun
    BoxTracer indoors({kRoof});
    std::vector<rt::BakeSample> lamp_lit = ground_samples(indoors, kSun);
    for (std::size_t i = 0; i < lamp_lit.size(); ++i) {
        core::at(lamp_lit, i).brightness = (i % kLampEvery) == 0 ? kLampPool : kLampDark;
    }
    expect(!rt::fit_baked_sun(lamp_lit, indoors).sun.found, "fit_baked_sun: a roofed level - no sun");
}

// --- (added by the scene's sun) which sun lights the scene: rt_sun.h resolve_scene_sun, parse_sun_source -------------

bool same_direction(const rt::Vec3& a, const rt::Vec3& b) { return rt::dot(a, b) > 1.0F - kNear; }

void check_scene_sun()
{
    const rt::Vec3 baked{0.0F, 0.8F, 0.6F};
    const rt::Vec3 game{0.0F, 0.34202F, -0.93969F};   // 20 degrees up
    const rt::Vec3 low{0.0F, 0.08716F, -0.99619F};    // 5 degrees
    const rt::Vec3 fixed{0.6F, 0.8F, 0.0F};
    const rt::Vec3 under{0.0F, -0.5F, 0.86603F};         // 30 degrees under the horizon
    const rt::Vec3 east_30{0.86603F, 0.5F, 0.0F};        // heading 90, 30 degrees up
    constexpr float kLonger = 3.0F;                      // (a direction not normalised)
    const rt::Vec3 up_long{0.0F, 2.0F, 0.0F};
    rt::SunInputs in{.choice = {.source = rt::SunSource::kGame},
                     .game_set = true,
                     .game_toward = game,
                     .baked_scene = 1,
                     .baked_found = true,
                     .baked_toward = baked,
                     .relight = {.relights = true}};
    rt::SceneSun sun = rt::resolve_scene_sun(in);
    expect(sun.origin == rt::SunOrigin::kGame && same_direction(sun.toward, game), "scene sun: game, relit - the game's");
    in.relight.relights = false;
    sun = rt::resolve_scene_sun(in);
    expect(sun.origin == rt::SunOrigin::kLevel && same_direction(sun.toward, baked),
           "scene sun: game, not relit - the baked sun (its shadows are the bake's)");
    in.relight = {.relights = true, .level_keeps_bake = true};
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kLevel, "scene sun: game, the level's entry keeps the bake");
    in.relight = {.relights = true};
    in.game_toward = low;
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kLevel, "scene sun: game, its sun too low - the baked sun");
    in.game_toward = under;
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kLevel, "scene sun: game, its sun under the horizon");
    in.game_set = false;
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kLevel, "scene sun: game, none shown - the baked sun");
    in.game_set = true;
    in.game_toward = game;
    in.baked_found = false;
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kNone, "scene sun: game, indoors (no baked sun) - none");
    in.baked_found = true;
    in.baked_scene = 0;
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kWaiting, "scene sun: the fit not made yet - waiting");
    in.baked_scene = 1;
    in.choice = {.source = rt::SunSource::kLevel};
    sun = rt::resolve_scene_sun(in);
    expect(sun.origin == rt::SunOrigin::kLevel && same_direction(sun.toward, baked), "scene sun: level - the baked sun");
    in.choice = {.source = rt::SunSource::kFixed, .fixed = rt::scaled(fixed, kLonger)};
    in.baked_scene = 0;
    sun = rt::resolve_scene_sun(in);
    expect(sun.origin == rt::SunOrigin::kFixed && same_direction(sun.toward, fixed),
           "scene sun: fixed - normalised, not waiting for the fit");
    in.override_set = true;
    in.override_toward = up_long;
    sun = rt::resolve_scene_sun(in);
    expect(sun.origin == rt::SunOrigin::kOverride && same_direction(sun.toward, {0.0F, 1.0F, 0.0F}),
           "scene sun: the console's override above everything");
    in.override_toward = {};
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kFixed, "scene sun: a zero override ignored");
    // (added by the time of day) the clock's light: above game and level where the level is relit, the baked sun
    // until then, none indoors, below the override and a fixed direction
    in.override_set = false;
    in.baked_scene = 1;
    in.clock_set = true;
    in.clock_toward = east_30;
    in.choice = {.source = rt::SunSource::kGame};
    in.relight = {.relights = true, .level_keeps_bake = true};
    sun = rt::resolve_scene_sun(in);
    expect(sun.origin == rt::SunOrigin::kClock && same_direction(sun.toward, east_30),
           "scene sun: the clock's light above the game's (an entry's \"sun\": \"level\" is about the game's)");
    in.choice = {.source = rt::SunSource::kLevel};
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kClock, "scene sun: the clock's light above the level's");
    in.relight = {};
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kLevel,
           "scene sun: the clock, the level not relit yet - the baked sun");
    in.relight = {.relights = true};
    in.baked_found = false;
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kNone, "scene sun: the clock, indoors - none");
    in.baked_found = true;
    in.choice = {.source = rt::SunSource::kFixed, .fixed = fixed};
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kFixed, "scene sun: a fixed direction above the clock");
    in.choice = {.source = rt::SunSource::kLevel};
    in.clock_toward = {};
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kLevel, "scene sun: a zero clock light ignored");
    in.clock_set = false;
    // rt_sun_source's text
    rt::SunChoice choice;
    expect(rt::parse_sun_source("game", &choice) && choice.source == rt::SunSource::kGame, "rt_sun_source=game");
    expect(rt::parse_sun_source(" level ", &choice) && choice.source == rt::SunSource::kLevel, "rt_sun_source=level");
    expect(rt::parse_sun_source("bake", &choice) && choice.source == rt::SunSource::kLevel, "rt_sun_source=bake");
    expect(rt::parse_sun_source("0.6,0.8,0", &choice) && choice.source == rt::SunSource::kFixed &&
               same_direction(choice.fixed, fixed),
           "rt_sun_source=x,y,z");
    expect(rt::parse_sun_source("90, 30", &choice) && choice.source == rt::SunSource::kFixed &&
               same_direction(choice.fixed, east_30),
           "rt_sun_source=heading,elevation (90: +x)");
    const rt::SunChoice kept = choice;
    expect(!rt::parse_sun_source("sky", &choice) && !rt::parse_sun_source("1,2,3,4", &choice) &&
               !rt::parse_sun_source("1,x", &choice) && !rt::parse_sun_source("0,0,0", &choice) &&
               !rt::parse_sun_source("", &choice) && same_direction(choice.fixed, kept.fixed),
           "rt_sun_source: anything else refused, the choice kept");
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_transforms();
    check_strips();
    check_geometry();
    check_flags();
    check_baked_sun();
    check_scene_sun();
    if (failures() != 0) {
        std::printf("%d check(s) failed\n", failures());
        return 1;
    }
    std::printf("rt_scene_check: all checks passed\n");
    return 0;
}
