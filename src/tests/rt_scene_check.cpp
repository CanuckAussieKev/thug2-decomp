// NOT ORIGINAL - thug2_rt_scene_check.exe: ray tracing's pure helpers (engine/gfx/rt_checks.h) - a Direct3D matrix as
// object-to-world rows, strips as lists, the geometry and flag tests the contract check makes - and the baked sun's fit
// (engine/gfx/rt_sun.h) against a small scene traced on the processor. Exit code 0 when every check passes.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_checks.h"
#include "engine/gfx/rt_coverage.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"
#include "engine/gfx/rt_surface_class.h"
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
    expect(rt::flags_problem(rt::kWorld | rt::kTransparent | rt::kAddsLight).empty(), "a glow: adds light");
    expect(!rt::flags_problem(rt::kWorld | rt::kOpaque | rt::kAddsLight).empty(), "adds light but opaque");
}

// (round PT-B) The surfaces' classes for rays (engine/gfx/rt_surface_class.h) of sample materials - each as the game
// draws it - and the instance flags each class takes.
rt::Material sample_material(uint32_t flags, uint32_t blend, float least_texel = 1.0F, float fixed_alpha = 1.0F)
{
    rt::Material made;
    made.flags = flags;
    made.blend = blend;
    made.colour = {1.0F, 1.0F, 1.0F, fixed_alpha};
    made.least_texel_alpha = least_texel;
    return made;
}

void check_surface_classes()
{
    using C = rt::SurfaceClass;
    constexpr uint32_t kNone = 0;
    constexpr uint32_t kAlpha = 5;
    constexpr uint32_t kFixedAlpha = 6;
    constexpr uint32_t kModulate = 7;
    constexpr uint32_t kAdd = 1;
    constexpr uint32_t kBrighten = 9;
    constexpr uint32_t kVertexTexAlpha = 11;
    constexpr uint32_t kColourModulate = 15;
    constexpr float kHalfVertex = 128.0F / 255.0F; // (0x80: 1 once doubled)
    constexpr float kFadedVertex = 64.0F / 255.0F;  // (0x40: a half once doubled)
    constexpr float kHalfTexel = 128.0F / 255.0F;   // (a texel of 0x80: 1 once doubled with a vertex alpha of 0xFF)
    constexpr float kLowTexel = 1.0F / 255.0F;
    struct Case {
        rt::Material material;
        float vertex_alpha;
        C expected;
        const char* what;
    };
    const std::array<Case, 17> cases{{
        {sample_material(0, kNone), 1.0F, C::kOpaque, "drawn solid: opaque"},
        {sample_material(rt::kAlphaTested, kNone), 1.0F, C::kCutOut, "alpha tested: a cut-out"},
        {sample_material(rt::kTransparent, kAlpha), kHalfVertex, C::kBlendedOpaque,
         "a brick facade: blend 5, every texel opaque, vertex alpha 0x80 - blended opaque"},
        {sample_material(rt::kTransparent, kAlpha), kFadedVertex, C::kSeenThrough,
         "the same faded by its vertex alpha 0x40: seen through"},
        {sample_material(rt::kTransparent | rt::kTextureAlpha, kAlpha), kFadedVertex, C::kBlendedOpaque,
         "the same with the texel's alpha alone (pass flag 0x1000): the vertex alpha doesn't count"},
        {sample_material(rt::kTransparent, kAlpha, kHalfTexel), 1.0F, C::kBlendedOpaque,
         "texels at least 0x80 with vertex alpha 0xFF: doubled, drawn opaque"},
        {sample_material(rt::kTransparent, kAlpha, kHalfTexel), kHalfVertex, C::kSeenThrough,
         "texels at least 0x80 with vertex alpha 0x80: drawn half"},
        {sample_material(rt::kTransparent | rt::kTextureAlpha, kAlpha, kHalfTexel), 1.0F, C::kSeenThrough,
         "texels at least 0x80, the texel's alpha alone: drawn half"},
        {sample_material(rt::kTransparent, kAlpha, kLowTexel), 1.0F, C::kSeenThrough,
         "a window: blend 5, a texel of alpha 1 - seen through"},
        {sample_material(rt::kTransparent | rt::kAlphaTested, kAlpha, 0.0F), 1.0F, C::kCutOut,
         "a blended cut-out (leaves): a cut-out"},
        {sample_material(rt::kTransparent | rt::kAlphaTested, kAlpha), 1.0F, C::kBlendedOpaque,
         "a blended cut-out whose texels are all opaque: blended opaque"},
        {sample_material(rt::kTransparent, kFixedAlpha, 1.0F, 1.0F), 1.0F, C::kBlendedOpaque, "blend 6, fixed alpha 1"},
        {sample_material(rt::kTransparent, kFixedAlpha, 1.0F, 0.5F), 1.0F, C::kSeenThrough, "blend 6, fixed alpha 0.5"},
        {sample_material(rt::kTransparent, kVertexTexAlpha), 1.0F, C::kBlendedOpaque,
         "blend 11 (replaces what is behind): blended opaque"},
        {sample_material(rt::kTransparent, kModulate), 1.0F, C::kSeenThrough,
         "blend 7 (a multiply): seen through, whatever its texels"},
        {sample_material(rt::kTransparent, kColourModulate), 1.0F, C::kSeenThrough,
         "blend 15 (a colour filter): seen through"},
        {sample_material(rt::kTransparent, kAdd), 1.0F, C::kAddsLight, "blend 1 (a glow): adds light"},
    }};
    for (const Case& each : cases) {
        expect(rt::surface_class(each.material, each.vertex_alpha) == each.expected, each.what);
    }
    expect(rt::surface_class(sample_material(rt::kTransparent, kBrighten), 1.0F) == C::kAddsLight,
           "blend 9: adds light");
    expect(rt::surface_class(sample_material(rt::kTransparent, kAlpha, 0.0F), 1.0F) == C::kSeenThrough,
           "texels not known (least 0): seen through");
    // the vertex alpha read only where it counts
    expect(rt::vertex_alpha_counts(sample_material(rt::kTransparent, kAlpha)), "blend 5: the vertex alpha counts");
    expect(!rt::vertex_alpha_counts(sample_material(rt::kTransparent | rt::kTextureAlpha, kAlpha)),
           "blend 5 with the texel's alpha alone: it doesn't");
    expect(!rt::vertex_alpha_counts(sample_material(0, kNone)), "a solid surface: it doesn't");
    // the instance flags each class takes, every one passing the contract
    const rt::Material blended = sample_material(rt::kTransparent | rt::kAlphaTested, kAlpha, 0.0F);
    const rt::Material solid = sample_material(0, kNone);
    expect(rt::surface_instance_flags(C::kBlendedOpaque, blended) == (rt::kOpaque | rt::kCastsShadow),
           "blended opaque: an opaque instance that casts");
    expect(rt::surface_instance_flags(C::kCutOut, blended) == (rt::kAlphaTested | rt::kCastsShadow | rt::kTransparent),
           "a blended cut-out keeps kTransparent");
    expect(rt::surface_instance_flags(C::kCutOut, solid) == (rt::kAlphaTested | rt::kCastsShadow), "a cut-out");
    expect(rt::surface_instance_flags(C::kSeenThrough, blended) == rt::kTransparent, "seen through: transparent");
    expect(rt::surface_instance_flags(C::kAddsLight, blended) == (rt::kTransparent | rt::kAddsLight),
           "adds light: transparent, adds light");
    for (const C each : {C::kOpaque, C::kCutOut, C::kBlendedOpaque, C::kSeenThrough, C::kAddsLight}) {
        expect(rt::flags_problem(rt::kWorld | rt::surface_instance_flags(each, blended)).empty(),
               rt::surface_class_name(each));
    }
    // (round TRAIN) the least vertex alpha read from a mesh's own bytes: Barcelona's pool sheet (position, diffuse, one
    // pair of coordinates - 24 bytes; its vertex alphas 0x20 - 0x33, one 0x0D) with an opaque texture is seen through,
    // not blended opaque - the reading against a stride of 0 gave 1 for every mesh
    constexpr uint32_t kFvfXyzDiffuseTex1 = 0x142;
    constexpr uint32_t kSheetStride = 24;
    constexpr uint32_t kColourAt = 12;
    const std::array<uint32_t, 3> words = {0x206B716BU, 0x0D6D6F6CU, 0x3369776FU};
    std::vector<uint8_t> sheet(words.size() * kSheetStride, 0);
    for (std::size_t i = 0; i < words.size(); ++i) {
        std::memcpy(&core::at(sheet, (i * kSheetStride) + kColourAt), &core::at(words, i), sizeof(uint32_t));
    }
    const float least = rt::least_vertex_alpha_in(sheet, kFvfXyzDiffuseTex1, kSheetStride, 3);
    expect(std::abs(least - (13.0F / 255.0F)) < 1.0e-6F, "the sheet's least vertex alpha 0x0D");
    expect(rt::surface_class(sample_material(rt::kTransparent, kAlpha), least) == C::kSeenThrough,
           "the pool sheet (blend 5, opaque texels, vertex alpha 0x0D - 0x33): seen through");
    expect(rt::least_vertex_alpha_in(sheet, kFvfXyzDiffuseTex1, kSheetStride, 2) < 0.1F, "the first vertices alone");
    expect(rt::least_vertex_alpha_in(sheet, 0x102, 20, 3) == 1.0F, "no diffuse colour: 1");
    expect(rt::least_vertex_alpha_in(std::span<const uint8_t>(sheet).first(10), kFvfXyzDiffuseTex1, kSheetStride, 3) ==
               1.0F,
           "bytes short of a vertex: none read, 1");

    // (round FIXTURE - rt_lights.h light_volume) Training's fitting prism (FD86DE2E: blend 5, every vertex 808080, alpha
    // 0x00 at its foot - 0x20 at its top, its texture a plain 255,255,221 - linear luminance 0.97) and the pendant's cone
    // (6535F3A8: 808066 - a warm tint, alpha 0 - 0x40) are light volumes; the census's near misses are not
    const auto prism = [&](const std::array<uint32_t, 4>& colours) {
        std::vector<uint8_t> mesh(colours.size() * kSheetStride, 0);
        for (std::size_t i = 0; i < colours.size(); ++i) {
            std::memcpy(&core::at(mesh, (i * kSheetStride) + kColourAt), &core::at(colours, i), sizeof(uint32_t));
        }
        return rt::vertex_colours_in(mesh, kFvfXyzDiffuseTex1, kSheetStride, static_cast<uint32_t>(colours.size()));
    };
    const auto look_of = [](const rt::VertexColours& v, float texel, uint32_t blend = 5, bool moving = false) {
        return rt::VolumeLook{.first_pass_blend = blend,
                              .moving_texture = moving,
                              .brightest = v.known ? v.brightest : uint8_t{0},
                              .least_alpha = v.least_alpha,
                              .most_alpha = v.most_alpha,
                              .least_texel_luma = texel};
    };
    constexpr float kPlainLight = 0.971F;
    const rt::VertexColours fitting = prism({0x00808080U, 0x20808080U, 0x00808080U, 0x20808080U});
    expect(fitting.known && fitting.brightest == 0x80U && fitting.least_alpha == 0U && fitting.most_alpha == 0x20U,
           "the prism's colours read: every vertex full-bright, alpha 0 - 0x20");
    expect(rt::light_volume(look_of(fitting, kPlainLight)), "Training's fitting prism is a light volume");
    expect(rt::light_volume(look_of(prism({0x00808066U, 0x40808066U, 0x00808066U, 0x40808066U}), kPlainLight)),
           "the pendant's warm cone (808066) is a light volume");
    // a shadow blob or dirt fade: the same vertices, a dark texture (SC 789E3EAA's 4,2,2; AU's foliage 0.17)
    expect(!rt::light_volume(look_of(fitting, 0.001F)) && !rt::light_volume(look_of(fitting, 0.173F)),
           "a fade with a dark texture (a shadow, dirt, foliage) is no light volume");
    // a baked surface: its shade falls under full brightness
    expect(!rt::light_volume(look_of(prism({0x00808080U, 0x20707070U, 0x00808080U, 0x20808080U}), kPlainLight)),
           "a vertex in the bake's shade (707070) - no light volume");
    // (round HEADS) the pendant's cone as the game's scene lights left it in its buffer (their light added to the
    // level's 808066, more near the lamp): still a light volume
    const rt::VertexColours lit_cone = prism({0x00FFED9BU, 0x40FFF59EU, 0x00FFF09CU, 0x40FFEC9AU});
    expect(lit_cone.brightest == 0xFFU && rt::light_volume(look_of(lit_cone, kPlainLight)),
           "the pendant's cone lit by the scene lights (FFED9B - FFF59E) is a light volume");
    // under full brightness: a lit surface's colour
    expect(!rt::light_volume(look_of(prism({0x00606060U, 0x20606060U, 0x00606060U, 0x20606060U}), kPlainLight)),
           "a colour under full brightness - no light volume");
    // glass or a sheet that never fades to nothing
    expect(!rt::light_volume(look_of(prism({0x10808080U, 0x40808080U, 0x10808080U, 0x40808080U}), kPlainLight)),
           "an alpha that never reaches 0 (glass, a haze sheet) - no light volume");
    expect(!rt::light_volume(look_of(prism({0x00808080U, 0x00808080U, 0x00808080U, 0x00808080U}), kPlainLight)),
           "nothing drawn (alpha 0 throughout) - no light volume");
    // water moves; an additive card is a light card's rule; an opaque pass shows itself
    expect(!rt::light_volume(look_of(fitting, kPlainLight, 5, true)), "a moving texture (water) - no light volume");
    expect(!rt::light_volume(look_of(fitting, kPlainLight, 1)) && !rt::light_volume(look_of(fitting, kPlainLight, 0)),
           "blend modes other than 5 - no light volume");
    const rt::VertexColours none = rt::vertex_colours_in(sheet, kFvfXyzDiffuseTex1, kSheetStride, 0);
    expect(!none.known && !rt::light_volume(look_of(none, kPlainLight)), "no vertices: none");
    expect(!rt::vertex_colours_in(sheet, 0x102, 20, 3).known, "no diffuse colour: not known");
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
const Box kRoof{.low = {-100.0F, 50.0F, -100.0F}, .high = {100.0F, 52.0F, 100.0F}};
const rt::Vec3 kSun{0.62F, 0.55F, -0.56F};      // toward the sun the bake was lit by

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
    // (lighting round A) a level painted in many colours - each surface its own brightness, the sun's shadows within
    // them: the score against each surface's own still finds the sun
    std::vector<rt::BakeSample> painted = samples;
    for (std::size_t i = 0; i < painted.size(); ++i) {
        rt::BakeSample& sample = core::at(painted, i);
        constexpr std::size_t kSurfaces = 16;
        constexpr float kPaintStep = 0.35F;
        sample.group = static_cast<uint32_t>(i % kSurfaces);
        sample.brightness *= 0.3F + (kPaintStep * static_cast<float>(sample.group % 5));
    }
    const rt::SunFit painted_fit = rt::fit_baked_sun(painted, outdoors);
    expect(painted_fit.sun.found && degrees_between(painted_fit.sun.toward, kSun) < kFoundWithin,
           "fit_baked_sun: the sun found in a level painted in many colours (each surface's own brightness)");
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
    const rt::Vec3 fixed{0.6F, 0.8F, 0.0F};
    const rt::Vec3 east_30{0.86603F, 0.5F, 0.0F};        // heading 90, 30 degrees up
    constexpr float kLonger = 3.0F;                      // (a direction not normalised)
    const rt::Vec3 up_long{0.0F, 2.0F, 0.0F};
    // (round S: the game's own sun retired - the level's baked sun, or the clock's)
    rt::SunInputs in{.choice = {.source = rt::SunSource::kLevel},
                     .baked_scene = 1,
                     .baked_found = true,
                     .baked_toward = baked,
                     .relight = {.relights = true}};
    rt::SceneSun sun = rt::resolve_scene_sun(in);
    expect(sun.origin == rt::SunOrigin::kLevel && same_direction(sun.toward, baked), "scene sun: relit - the baked sun");
    in.baked_found = false;
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kNone, "scene sun: indoors (no baked sun) - none");
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
    in.choice = {.source = rt::SunSource::kLevel};
    in.relight = {.relights = true};
    sun = rt::resolve_scene_sun(in);
    expect(sun.origin == rt::SunOrigin::kClock && same_direction(sun.toward, east_30),
           "scene sun: the clock's light above the level's");
    in.relight = {};
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kLevel,
           "scene sun: the clock, the level not relit yet - the baked sun");
    in.relight = {.relights = true};
    in.baked_found = false;
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kClock,
           "scene sun: the clock, a bake with no sun - the clock's (the level relit by its sky, bounce and lamps)");
    in.baked_found = true;
    in.choice = {.source = rt::SunSource::kFixed, .fixed = fixed};
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kFixed, "scene sun: a fixed direction above the clock");
    in.choice = {.source = rt::SunSource::kLevel};
    in.clock_toward = {};
    expect(rt::resolve_scene_sun(in).origin == rt::SunOrigin::kLevel, "scene sun: a zero clock light ignored");
    in.clock_set = false;
    // rt_sun_source's text
    rt::SunChoice choice;
    expect(!rt::parse_sun_source("game", &choice), "rt sun source: the game's sun retired");
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

// (round SHINE - rt_surface_class.h reflection_class_on) Which reflection class an instance takes on its mesh: Training's
// quarter pipe (4D1670EA: an environment-mapped first pass, blend 5, the ramp's texture blended over it by its alpha -
// glass by its cover) on its blended-opaque mesh is a shine mask's coat; the same on a seen-through mesh stays glass;
// painted glass (the environment lerped into a wall texture's clear texels), water, the game's other shines and a
// surface that doesn't reflect keep their class.
void check_shine_masks()
{
    using C = rt::SurfaceClass;
    constexpr uint32_t kAlpha = 5;
    rt::Material ramp = sample_material(rt::kTransparent, kAlpha);
    ramp.reflectivity = 1.0F;
    ramp.environment_pass = 0;
    ramp.reflection_class = rt::kReflectGlass;
    ramp.reflection_mask = rt::kReflectEverywhere;
    ramp.mask_passes[0] = {.kind = rt::kMaskClear, .uv_set = 1};
    expect(rt::reflection_class_on(ramp, C::kBlendedOpaque) == rt::kReflectCoat,
           "the quarter pipe's shine mask on its blended-opaque mesh: a coat");
    expect(rt::reflection_class_on(ramp, C::kOpaque) == rt::kReflectCoat, "on an opaque mesh: a coat");
    expect(rt::reflection_class_on(ramp, C::kCutOut) == rt::kReflectCoat, "on a cut-out (its solid texels): a coat");
    expect(rt::reflection_class_on(ramp, C::kSeenThrough) == rt::kReflectGlass, "on a seen-through mesh: glass");
    expect(rt::reflection_class_on(ramp, C::kAddsLight) == rt::kReflectGlass, "on a light-adding mesh: unchanged");
    rt::Material painted = ramp;
    painted.reflection_mask = rt::kReflectWhereBaseClear;
    expect(rt::reflection_class_on(painted, C::kBlendedOpaque) == rt::kReflectGlass,
           "glass painted into a wall texture's clear texels: glass");
    rt::Material water = ramp;
    water.reflection_class = rt::kReflectWater;
    expect(rt::reflection_class_on(water, C::kBlendedOpaque) == rt::kReflectWater, "water: water");
    rt::Material shiny = ramp;
    shiny.reflection_class = rt::kReflectShiny;
    expect(rt::reflection_class_on(shiny, C::kOpaque) == rt::kReflectShiny, "the game's shine: unchanged");
    rt::Material dull = ramp;
    dull.reflectivity = 0.0F;
    expect(rt::reflection_class_on(dull, C::kBlendedOpaque) == rt::kReflectGlass, "no reflectivity: unchanged");
    // (round SKIN) the raster's coat for the same: the ramp's strength and its mask (pass 1's texel where clear, 2 x the
    // vertex alpha x it); none where the class isn't the coat
    const rt::SurfaceCoat coat = rt::surface_coat(ramp, rt::reflection_class_on(ramp, C::kBlendedOpaque));
    expect(coat.strength == 1.0F && coat.masks == (rt::kMaskClear | (1U << rt::kCoatMaskStageShift)),
           "the raster's coat on the quarter pipe: strength 1, pass 1 where clear");
    const rt::SurfaceCoat none = rt::surface_coat(ramp, rt::reflection_class_on(ramp, C::kSeenThrough));
    expect(none.strength == 0.0F && none.masks == 0U, "no coat on a seen-through mesh (glass)");
}

// (round SKIN - rt_coverage.h) A blended surface that writes depth, as the prepass keeps it: without a temporal resolve
// at least half opaque (an alpha test); under one each pixel keeps it in a frame with the probability of its alpha - over
// the cycle's frames the kept share is its alpha at every pixel (the resolve's blend - the path tracer's surface by its
// coverage), and over a block of pixels in one frame too (the pattern spreads: no frame drops a whole region).
void check_coverage()
{
    expect(rt::coverage_kept(0.5F, 3, 7, -1.0F) && !rt::coverage_kept(0.49F, 3, 7, -1.0F) &&
               rt::coverage_kept(1.0F, 0, 0, -1.0F) && !rt::coverage_kept(0.0F, 0, 0, -1.0F),
           "no temporal resolve: kept where at least half opaque");
    expect(rt::coverage_seed(129, false) < 0.0F && rt::coverage_seed(129, true) == 1.0F,
           "the seed: -1 without a temporal resolve, the frame within the cycle with one");
    double worst_pixel = 0.0;
    double worst_block = 0.0;
    bool ends = true;
    for (const float alpha : {0.1F, 0.25F, 0.5F, 0.75F, 0.9F}) {
        for (uint32_t y = 0; y < 8; ++y) {
            for (uint32_t x = 0; x < 8; ++x) {
                uint32_t kept = 0;
                for (uint32_t frame = 0; frame < rt::kCoverageFrames; ++frame) {
                    kept += rt::coverage_kept(alpha, x, y, static_cast<float>(frame)) ? 1U : 0U;
                }
                const double share = static_cast<double>(kept) / static_cast<double>(rt::kCoverageFrames);
                worst_pixel = std::max(worst_pixel, std::abs(share - static_cast<double>(alpha)));
            }
        }
        for (uint32_t frame = 0; frame < rt::kCoverageFrames; ++frame) {
            uint32_t kept = 0;
            for (uint32_t y = 0; y < 16; ++y) {
                for (uint32_t x = 0; x < 16; ++x) {
                    kept += rt::coverage_kept(alpha, x + 40, y + 9, static_cast<float>(frame)) ? 1U : 0U;
                }
            }
            worst_block = std::max(worst_block, std::abs((static_cast<double>(kept) / 256.0) - static_cast<double>(alpha)));
        }
    }
    for (uint32_t frame = 0; frame < rt::kCoverageFrames && ends; ++frame) {
        ends = rt::coverage_kept(1.0F, frame * 7, frame * 3, static_cast<float>(frame)) &&
               !rt::coverage_kept(0.0F, frame * 7, frame * 3, static_cast<float>(frame));
    }
    std::printf("  coverage: the kept share's worst error per pixel over %u frames %.3f, per 16 x 16 block in a frame %.3f\n",
                rt::kCoverageFrames, worst_pixel, worst_block);
    expect(worst_pixel <= 0.1 && worst_block <= 0.05 && ends,
           "a temporal resolve: each pixel kept with the probability of its alpha (per pixel over the cycle, per block in a "
           "frame); alpha 1 always, 0 never");
}

// (round HEADS - rt_surface_class.h baked_shadow_mesh) The bake's shadow drawn as geometry, which the relit level hides:
// a blended mesh, every vertex black, that lets what is under it show. Training's black box over the level's open edge
// (blend 5, opaque texels, vertex alpha 0x80 - blended opaque) is a surface: kept for every ray.
void check_baked_shadows()
{
    using C = rt::SurfaceClass;
    constexpr uint32_t kAlpha = 5;
    constexpr float kHalfVertex = 128.0F / 255.0F;
    constexpr float kShadowVertex = 51.0F / 255.0F;
    const std::array<uint32_t, 3> black{0x80000000U, 0x80000000U, 0x80000000U};
    const std::array<uint32_t, 3> shade{0x33000000U, 0x33000000U, 0x00000000U};
    const std::array<uint32_t, 3> lit{0x80000000U, 0x80404040U, 0x80000000U};
    const rt::Material blended = sample_material(rt::kTransparent, kAlpha);
    const C closure = rt::surface_class(blended, kHalfVertex);
    const C fence_shadow = rt::surface_class(blended, kShadowVertex);
    expect(closure == C::kBlendedOpaque && !rt::baked_shadow_mesh(true, closure, black),
           "Training's black box (blend 5, opaque texels, vertex alpha 0x80): blended opaque - a surface, not hidden");
    expect(fence_shadow == C::kSeenThrough && rt::baked_shadow_mesh(true, fence_shadow, shade),
           "a fence's shadow lines (black, vertex alpha 0x33 fading to 0): seen through - the bake's shadow");
    expect(rt::baked_shadow_mesh(true, C::kCutOut, shade), "a black blended cut-out: the bake's shadow");
    expect(!rt::baked_shadow_mesh(true, fence_shadow, lit), "a vertex not black: no shadow");
    expect(!rt::baked_shadow_mesh(false, C::kOpaque, black), "not blended (black paint): no shadow");
    expect(!rt::baked_shadow_mesh(true, fence_shadow, std::span<const uint32_t>{}), "no colours: no shadow");
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_transforms();
    check_strips();
    check_geometry();
    check_flags();
    check_surface_classes(); // (round PT-B)
    check_shine_masks();     // (round SHINE)
    check_coverage();        // (round SKIN)
    check_baked_shadows();   // (round HEADS)
    check_baked_sun();
    check_scene_sun();
    if (failures() != 0) {
        std::printf("%d check(s) failed\n", failures());
        return 1;
    }
    std::printf("rt_scene_check: all checks passed\n");
    return 0;
}
