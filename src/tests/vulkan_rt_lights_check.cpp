// NOT ORIGINAL - thug2_vulkan_rt_lights_check.exe: the night lighting ([Graphics] rt_lights - gpu_vulkan/
// vulkan_rt_lights.h, engine/gfx/rt_lights.h) tried on this machine against references worked out on the processor.
// Everything goes through the installed render::rt::RayTracing (R2's acceleration structures and geometry table, R3's
// material table), as render_pc's frame record feeds it.
//
// First the light sources on the processor alone: the emitters' clustering (a lamp of many triangles is one light, two
// lamps apart are two, a glow's flat pool lifted, a self-lit pool left out, a dark level's rule), the falloff's values,
// the frustum test and the view's selection (a lamp behind the camera left out, the most important first, the cap).
// Then the scene: a dark floor and a box standing on it (drawn darker than their albedo: "the bake at night"), a lamp
// beside the box - the frame record's point light, or the level's emitter - and the pass through the listener, as
// draw_viewport runs it. The processor answers each pixel from the camera's own position and lens: the surface it sees,
// each lamp's light there (colour x intensity x falloff x N.L), whether the shadow ray from it (started off the surface
// as the pass does, stopped the lamp's size short) is blocked by the box:
//   - "clear-cut" pixels (their 5x5 neighbourhood on the same face, every lamp's visibility the same, no two lamps
//     nearly tied for the last traced place) must be the drawn
//     colour + strength x linear albedo x light (round F: added - the screen retired) within 2 per channel (one
//     sample) / 3 (four, resolved);
//   - the floor has a lit part and a part in the box's shadow (both a few hundred pixels);
//   - a red-textured lamp (an emitter whose material's texture is red) lights the floor red (its texture's mean);
//   - two lamps, one traced (rt_lights_shadowed=1): the other's light in the share of the traced one's that got through;
//   - (added for the cut-outs) the box made an alpha-tested caster: its texels' alpha 0 - the lamp's light goes through
//     it (no floor shadowed, as without it); alpha 255 - shadowed as the solid box;
//   - rt_lights off, or no lamp in view: every pixel exactly as drawn (no pass);
//   - many lamps (49 over the floor): as the processor's, their sum held under the knee, no floor pixel burnt white;
//   - (added by the volumetric light) the lamp in the air: rt_fog's haze (no level fog, no sun) lit by the lamps' set -
//     after still frames every half-resolution texel's light against the processor's march of the lamp's light through
//     the haze (its phase, its falloff, the box's shadow): texels whose rays cross the box's shadow behind it as dark as
//     the shadowed march says and darker than the unshadowed one (the lamp's cone of light in the air cut by the box),
//     the lit ones as the march says.
// Each lamp's light falls off from its core (rt_lights.h light_falloff): a point light's a tenth of its reach, an
// emitter's a disc of its area - a street-wide glow lights a pool, not the street.
//   thug2_vulkan_rt_lights_check [--device N] [--display N] [--no-validation] [--perf]
// --perf: then frames with 64 lamps in view, the pass's device time printed (its own log line, every 600 passes).
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
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_volume_light.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_fog.h"
#include "gpu_vulkan/vulkan_rt_lights.h"
#include "platform/run_log.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
namespace rt = render::rt;
using Vec3 = std::array<double, 3>;
using Rgb = std::array<double, 3>;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr uint32_t kTargetWidth = 320;
constexpr uint32_t kTargetHeight = 240;
constexpr int kColumns = static_cast<int>(kTargetWidth);
constexpr int kRows = static_cast<int>(kTargetHeight);
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvfColour = 0x042;   // XYZ | DIFFUSE (the raster draws)
constexpr uint32_t kFvfTextured = 0x142; // XYZ | DIFFUSE | TEX1 (the meshes the rays see)
constexpr uint32_t kTexturedStride = 24; // the position, the colour at 12, the uv at 16
constexpr uint32_t kTexturedColour = 12;
constexpr uint32_t kTexturedUv = 16;
constexpr uint32_t kGrey = 0x80808080U;
constexpr uint32_t kClearColour = 0xFF102030;
constexpr float kNearZ = 2.0F;
constexpr float kFarZ = 2000.0F;
constexpr double kHalfFovTan = 0.57735026918962576; // tan 30 degrees: a 60-degree horizontal field of view
constexpr double kNoHit = std::numeric_limits<double>::infinity();
constexpr std::size_t kNoSurface = 0;
constexpr std::size_t kPixelBytes = 4;
constexpr double kByteRange = 255.0;
constexpr double kOffsetScale = 1.0; // (the pass's shadow rays' start offset)
constexpr float kStrength = 1.5F;
constexpr int kClearCut = 2;
constexpr int kTolerance = 2;
constexpr int kToleranceSampled = 3;
constexpr int kMinLit = 300;
constexpr int kMinShadowed = 200;
constexpr uint32_t kWorldOpaque = rt::kWorld | rt::kOpaque | rt::kCastsShadow;
constexpr uint32_t kTextureSide = 2;
constexpr uint32_t kRedTexel = 0xFFFF2020U;

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
    bool perf = false; // --perf: 620 frames with 64 lamps in view, the pass's device time logged
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--perf") {
            options.perf = true;
        } else if (argument == "--no-validation") {
            options.validation = false;
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
double length_of(const Vec3& v) { return std::hypot(x_of(v), y_of(v), z_of(v)); }
std::array<float, 3> floats(const Vec3& v)
{
    return {static_cast<float>(x_of(v)), static_cast<float>(y_of(v)), static_cast<float>(z_of(v))};
}

// --- the light sources on the processor ----------------------------------------------------------------------------

// A quad (two triangles) of an emissive surface: its corner, its two sides.
void add_quad(std::vector<rt::EmitterTriangle>* out, const Vec3& corner, const Vec3& side, const Vec3& other,
              rt::EmitterKind kind, const std::array<float, 3>& colour)
{
    const Vec3 a = corner;
    const Vec3 b = step(corner, side, 1.0);
    const Vec3 c = step(step(corner, side, 1.0), other, 1.0);
    const Vec3 d = step(corner, other, 1.0);
    out->push_back({.corners = {floats(a), floats(b), floats(c)}, .colour = colour, .material = 7, .kind = kind});
    out->push_back({.corners = {floats(a), floats(c), floats(d)}, .colour = colour, .material = 7, .kind = kind});
}

// (lighting round A2 - indoor ceiling lights) The emitters' shapes from their triangles: a fluorescent tube (a long
// thin strip, 8 x 1 quads of 12 x 3 units, facing down) is a line along it; a ceiling's lit panel (4 x 4 quads facing
// down, 64 units across) a panel with its normal vertical - facing up, a pool of light baked on a floor, none; a lamp head (a small box) a point. And the geometry term: a line's light
// falls as 1/d near it and 1/d^2 far off (a point of the same intensity); a panel's is a two-sided disc's.
void check_shapes(Tally* tally)
{
    constexpr std::array<float, 3> kWhite{1.0F, 1.0F, 1.0F};
    std::vector<rt::EmitterTriangle> tube;
    for (int i = 0; i < 8; ++i) {
        add_quad(&tube, {i * 12.0, 300.0, 0.0}, {12.0, 0.0, 0.0}, {0.0, 0.0, 3.0}, rt::EmitterKind::kSelfLit, kWhite);
    }
    std::vector<rt::EmitterTriangle> panel;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            add_quad(&panel, {i * 16.0, 300.0, j * 16.0}, {16.0, 0.0, 0.0}, {0.0, 0.0, 16.0}, rt::EmitterKind::kSelfLit,
                     kWhite);
        }
    }
    std::vector<rt::EmitterTriangle> head;
    for (const auto& [corner, side, other] :
         {std::array<Vec3, 3>{Vec3{0.0, 0.0, 0.0}, Vec3{8.0, 0.0, 0.0}, Vec3{0.0, 8.0, 0.0}},
          std::array<Vec3, 3>{Vec3{0.0, 0.0, 8.0}, Vec3{8.0, 0.0, 0.0}, Vec3{0.0, 8.0, 0.0}},
          std::array<Vec3, 3>{Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, 8.0}, Vec3{0.0, 8.0, 0.0}},
          std::array<Vec3, 3>{Vec3{8.0, 0.0, 0.0}, Vec3{0.0, 0.0, 8.0}, Vec3{0.0, 8.0, 0.0}},
          std::array<Vec3, 3>{Vec3{0.0, 0.0, 0.0}, Vec3{8.0, 0.0, 0.0}, Vec3{0.0, 0.0, 8.0}},
          std::array<Vec3, 3>{Vec3{0.0, 8.0, 0.0}, Vec3{8.0, 0.0, 0.0}, Vec3{0.0, 0.0, 8.0}}}) {
        add_quad(&head, corner, side, other, rt::EmitterKind::kSelfLit, kWhite);
    }
    std::vector<uint32_t> owner;
    const std::vector<rt::Emitter> line = rt::cluster_emitters(tube, &owner, 1.0F);
    const bool line_ok = line.size() == 1 && line.front().shape == rt::EmitterShape::kLine &&
                         std::abs(std::abs(line.front().axis[0]) - 1.0F) < 0.01F &&
                         std::abs(line.front().half_length - 48.0F) < 8.0F &&
                         std::ranges::all_of(owner, [](uint32_t index) { return index == 0; });
    tally->check(line_ok, text::format("a tube is a line light along it (%zu lights; shape %d, axis %.2f %.2f %.2f, half "
                                       "length %.1f of 48; every triangle its)",
                                       line.size(), line.empty() ? -1 : static_cast<int>(line.front().shape),
                                       line.empty() ? 0.0 : static_cast<double>(line.front().axis[0]),
                                       line.empty() ? 0.0 : static_cast<double>(line.front().axis[1]),
                                       line.empty() ? 0.0 : static_cast<double>(line.front().axis[2]),
                                       line.empty() ? 0.0 : static_cast<double>(line.front().half_length)));
    const std::vector<rt::Emitter> flat = rt::cluster_emitters(panel, nullptr, 1.0F);
    const std::vector<rt::Emitter> pool = rt::cluster_emitters(panel, nullptr, -1.0F); // (the same, facing up)
    tally->check(flat.size() == 1 && flat.front().shape == rt::EmitterShape::kPanel && pool.empty() &&
                     std::abs(std::abs(flat.front().axis[1]) - 1.0F) < 0.01F,
                 text::format("a ceiling's lit panel is a panel light facing down (%zu lights; shape %d, normal y %.2f); "
                              "the same facing up - a pool baked on a floor - no light (%zu)",
                              flat.size(), flat.empty() ? -1 : static_cast<int>(flat.front().shape),
                              flat.empty() ? 0.0 : static_cast<double>(flat.front().axis[1]), pool.size()));
    const std::vector<rt::Emitter> point = rt::cluster_emitters(head);
    tally->check(point.size() == 1 && point.front().shape == rt::EmitterShape::kPoint,
                 text::format("a lamp head is a point light (%zu lights; shape %d)", point.size(),
                              point.empty() ? -1 : static_cast<int>(point.front().shape)));
    // the geometry: a long line near its middle and far off; a panel straight under it and to the side
    const rt::LampShape long_line{.position = {0.0F, 0.0F, 0.0F}, .shape = rt::EmitterShape::kLine,
                                  .axis = {1.0F, 0.0F, 0.0F}, .half_length = 1000.0F, .core = 2.0F, .reach = 1.0e6F};
    const auto line_at = [&](float distance) {
        return rt::lamp_geometry(long_line, {0.0F, -distance, 0.0F}, {0.0F, 1.0F, 0.0F}, nullptr);
    };
    const float near_ratio = line_at(20.0F) / line_at(40.0F); // (1/d: 2)
    const rt::LampShape short_line{.position = {0.0F, 0.0F, 0.0F}, .shape = rt::EmitterShape::kLine,
                                   .axis = {1.0F, 0.0F, 0.0F}, .half_length = 10.0F, .core = 2.0F, .reach = 1.0e6F};
    const auto short_at = [&](float distance) {
        return rt::lamp_geometry(short_line, {0.0F, -distance, 0.0F}, {0.0F, 1.0F, 0.0F}, nullptr);
    };
    const float far_ratio = short_at(2000.0F) / short_at(4000.0F); // (1/d^2: 4)
    const rt::LampShape disc{.position = {0.0F, 0.0F, 0.0F}, .shape = rt::EmitterShape::kPanel,
                             .axis = {0.0F, -1.0F, 0.0F}, .core = 10.0F, .reach = 1.0e6F};
    const float below = rt::lamp_geometry(disc, {0.0F, -10.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, nullptr); // (1/2: r = d)
    const float level = rt::lamp_geometry(disc, {30.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}, nullptr); // (edge-on: 0)
    tally->check(std::abs(near_ratio - 2.0F) < 0.05F && std::abs(far_ratio - 4.0F) < 0.05F &&
                     std::abs(below - 0.5F) < 0.01F && level < 1e-4F,
                 text::format("the lamps' geometry: a line's light 1/d near it (x%.3f at half the distance) and 1/d^2 far "
                              "(x%.3f); a panel's on its axis at its radius %.3f (a disc's 0.5), edge-on %.4f",
                              static_cast<double>(near_ratio), static_cast<double>(far_ratio),
                              static_cast<double>(below), static_cast<double>(level)));
}

void check_sources(Tally* tally)
{
    text::print("the light sources (on the processor):\n");
    constexpr std::array<float, 3> kOrange{1.0F, 0.6F, 0.2F};
    // a lamp head of 4 x 4 small quads standing up (a vertical sign, 32 x 32 units), and another 600 units away
    std::vector<rt::EmitterTriangle> triangles;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            add_quad(&triangles, {(i * 8.0) - 16.0, 200.0 + (j * 8.0), 0.0}, {8.0, 0.0, 0.0}, {0.0, 8.0, 0.0},
                     rt::EmitterKind::kGlow, kOrange);
        }
    }
    add_quad(&triangles, {600.0, 200.0, 0.0}, {16.0, 0.0, 0.0}, {0.0, 16.0, 0.0}, rt::EmitterKind::kGlow, kOrange);
    std::vector<rt::Emitter> found = rt::cluster_emitters(triangles);
    tally->check(found.size() == 2, text::format("two lamps apart are two lights (%zu)", found.size()));
    if (found.size() == 2) {
        const rt::Emitter& big = found.front();
        tally->check(std::abs(big.position[0]) < 0.01F && std::abs(big.position[1] - 216.0F) < 0.01F &&
                         std::abs(big.position[2]) < 0.01F,
                     text::format("the sign of 32 triangles is one light at its centre (%.2f %.2f %.2f), the bigger "
                                  "first",
                                  static_cast<double>(big.position[0]), static_cast<double>(big.position[1]),
                                  static_cast<double>(big.position[2])));
        tally->check(std::abs(big.colour[0] - 1.0F) < 1e-5F && std::abs(big.colour[1] - 0.6F) < 1e-5F &&
                         std::abs(big.intensity - 1.0F) < 1e-4F && big.material == 7,
                     text::format("its colour the triangles' (brightest 1), intensity %.3f (its brightness), its "
                                  "material",
                                  static_cast<double>(big.intensity)));
        const float size = std::hypot(32.0F, 32.0F) * 0.5F;
        const float core = std::sqrt(32.0F * 32.0F / 3.14159265F); // (a disc of its area)
        tally->check(std::abs(big.size - size) < 1e-3F && std::abs(big.core - core) < 1e-3F &&
                         std::abs(big.reach - std::clamp(core * rt::kEmitterReachPerCore, rt::kEmitterMinReach,
                                                         rt::kEmitterMaxReach)) < 1e-3F,
                     text::format("its size %.2f (half its diagonal), core %.2f (a disc of its area) and reach %.1f",
                                  static_cast<double>(big.size), static_cast<double>(big.core),
                                  static_cast<double>(big.reach)));
    }
    // a pool of light on the ground (flat, facing up): a glow is lifted by its core; a self-lit one is left out
    std::vector<rt::EmitterTriangle> pool;
    add_quad(&pool, {-40.0, 0.0, -40.0}, {80.0, 0.0, 0.0}, {0.0, 0.0, 80.0}, rt::EmitterKind::kGlow, kOrange);
    found = rt::cluster_emitters(pool);
    tally->check(found.size() == 1 && std::abs(found.front().position[1] - found.front().core) < 1e-3F &&
                     std::abs(found.front().core - rt::emitter_core(80.0F * 80.0F)) < 1e-3F,
                 "a glow's pool on the ground is a light lifted by its core (a disc of its area)");
    // (added for the washed-out streets) a big glow on the ground - as New Orleans' streets draw them, 200 units across -
    // lights a pool, not the street: an eighth of the light under it or less 600 units away (it was a third)
    std::vector<rt::EmitterTriangle> street;
    add_quad(&street, {-100.0, 0.0, -100.0}, {200.0, 0.0, 0.0}, {0.0, 0.0, 200.0}, rt::EmitterKind::kGlow,
             {1.0F, 1.0F, 1.0F});
    found = rt::cluster_emitters(street);
    if (found.size() == 1) {
        const rt::Emitter& glow = found.front();
        const float below = rt::light_falloff(glow.position[1], glow.core, glow.reach);
        const float away = rt::light_falloff(std::hypot(600.0F, glow.position[1]), glow.core, glow.reach);
        tally->check(away <= 0.125F * below && glow.reach <= rt::kEmitterMaxReach,
                     text::format("a street-wide glow: %.3f of its light under it, %.4f 600 units away (reach %.0f)",
                                  static_cast<double>(below), static_cast<double>(away),
                                  static_cast<double>(glow.reach)));
    } else {
        tally->check(false, text::format("a street-wide glow is one light (%zu)", found.size()));
    }
    for (rt::EmitterTriangle& triangle : pool) {
        triangle.kind = rt::EmitterKind::kSelfLit;
    }
    tally->check(rt::cluster_emitters(pool).empty(), "a self-lit pool on the ground is no light");
    // the self-lit candidates (round A3: in any level - the bake confirms them), the falloff
    tally->check(rt::self_lit_triangle(120.0F, 40.0F) && !rt::self_lit_triangle(120.0F, 90.0F) &&
                     !rt::self_lit_triangle(90.0F, 30.0F) && rt::candidate_kind(rt::EmitterKind::kSelfLit) &&
                     rt::candidate_kind(rt::EmitterKind::kTextured) && !rt::candidate_kind(rt::EmitterKind::kGlow),
                 "self-lit candidates: far brighter than the level's median, a candidate the bake must confirm");
    // (round A3) the original's light cards: an additive still mesh (hidden in the relit level, a glow's evidence for the
    // bake); not a blended one (the bake's shadows' rule), not water or a moving effect, not an opaque surface
    tally->check(rt::light_card(1, false) && rt::light_card(2, false) && !rt::light_card(1, true) &&
                     !rt::light_card(0, false) && !rt::light_card(5, false),
                 "a light card: drawn additively, its texture still - not opaque, blended or moving");
    // (round A3) a lamp drawn in a texture: near white or a lamp's colour, far over its texture's median
    tally->check(rt::lamp_texel({0.9F, 0.9F, 0.85F}, 0.1F) && rt::lamp_texel({1.0F, 0.7F, 0.3F}, 0.1F) &&
                     !rt::lamp_texel({0.9F, 0.9F, 0.85F}, 0.6F) && !rt::lamp_texel({1.0F, 0.1F, 0.1F}, 0.05F) &&
                     !rt::lamp_texel({0.3F, 0.9F, 0.2F}, 0.05F) && !rt::lamp_texel({0.5F, 0.5F, 0.5F}, 0.05F),
                 "a lamp texel: bright, a white or a blackbody's colour, far over its texture (not white paint, a red "
                 "sign, green, grey)");
    const float reach = 400.0F;
    const float core = rt::point_core(reach);
    tally->check(std::abs(rt::light_falloff(0.0F, core, reach) - 1.0F) < 1e-6F &&
                     rt::light_falloff(reach, core, reach) == 0.0F &&
                     std::abs(rt::light_falloff(core, core, reach) -
                              (0.5F * std::pow(1.0F - std::pow(rt::kPointCoreShare, 4.0F), 2.0F))) < 1e-5F &&
                     rt::light_falloff(10.0F * core, core, reach) < 0.01F,
                 "the falloff: 1 at the light, about 1/2 at its core's edge, under 1% ten cores away, 0 at its reach");
    // (lighting round A3 - each lamp by its own daylight, no "dark level": rt_lights.h lamp_on - one rule for the pass
    // and relit's per-vertex lamps) a street lamp whose light equals noon's sky x its surfaces' K at a sky of 0.02:
    // off at noon's 1, on at night's 0.001, half on at its switch; indoors (no sky) always; unknown: the open sky's rule
    {
        const float street_switch = 0.02F;
        const float noon = rt::lamp_on(street_switch, 1.0F, 0.0F);
        const float night = rt::lamp_on(street_switch, 0.001F, 0.0F);
        const float dusk = rt::lamp_on(street_switch, street_switch, 0.0F);
        const float indoors = rt::lamp_on(rt::kLampAlwaysOn, 1.0F, 0.0F);
        tally->check(noon == 0.0F && night == 1.0F && std::abs(dusk - 0.5F) < 1e-4F && indoors == 1.0F &&
                         rt::lamp_on(0.0F, 1.0F, 0.25F) == 0.25F &&
                         rt::lamp_sky_now({.on = true, .sky_light = {1.0F, 1.0F, 1.0F}}, nullptr) > 0.99F &&
                         rt::open_sky_lamps({.on = true, .lamps = 0.25F}, nullptr) == 0.25F,
                     text::format("a lamp by its own daylight: a street lamp %.2f at noon, %.2f at night, %.2f at its "
                                  "switch; indoors %.2f at noon; unknown - the open sky's rule",
                                  static_cast<double>(noon), static_cast<double>(night), static_cast<double>(dusk),
                                  static_cast<double>(indoors)));
    }
    // the view's selection: a camera at the origin looking down -z (D3D's right-handed look-at, as the game's)
    std::array<float, 16> view_matrix{};
    std::array<float, 16> projection{};
    constexpr std::array<float, 3> kOrigin{0.0F, 0.0F, 0.0F};
    constexpr std::array<float, 3> kAhead{0.0F, 0.0F, -1.0F};
    constexpr std::array<float, 3> kUpward{0.0F, 1.0F, 0.0F};
    maths::matrix_look_at_rh(view_matrix, kOrigin, kAhead, kUpward);
    maths::matrix_perspective_rh(projection, 2.0F, 1.5F, 1.0F, 5000.0F);
    rt::ViewLightRequest request;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += core::at(view_matrix, (row * 4) + k) * core::at(projection, (k * 4) + column);
            }
            core::at(core::at(request.view_projection, row), column) = sum;
        }
    }
    tally->check(rt::sphere_in_frustum(request.view_projection, {0.0F, 0.0F, -100.0F}, 1.0F) &&
                     !rt::sphere_in_frustum(request.view_projection, {0.0F, 0.0F, 3000.0F}, 400.0F) &&
                     rt::sphere_in_frustum(request.view_projection, {0.0F, 0.0F, 300.0F}, 400.0F),
                 "the frustum: ahead in, far behind out, a reach over the camera in");
    rt::FrameRecord record;
    record.lights.push_back({.kind = rt::Light::Kind::kPoint, .colour = {0.5F, 0.5F, 0.25F},
                             .position = {0.0F, 0.0F, -300.0F}, .radius = 400.0F});
    record.lights.push_back({.kind = rt::Light::Kind::kPoint, .colour = {1.0F, 1.0F, 1.0F},
                             .position = {0.0F, 0.0F, 3000.0F}, .radius = 400.0F}); // (behind: out)
    rt::LevelEmitters level;
    level.emitters.push_back({.position = {0.0F, 0.0F, -2000.0F}, .size = 10.0F, .colour = {1.0F, 1.0F, 1.0F},
                              .intensity = 1.0F, .reach = 500.0F, .material = 0, .kind = rt::EmitterKind::kGlow});
    request.camera = {0.0F, 0.0F, 0.0F};
    request.max = 8;
    std::vector<rt::Emitter> lights = rt::select_view_lights(record, &level, request);
    tally->check(lights.size() == 2 && lights.front().kind == rt::EmitterKind::kPoint &&
                     std::abs(lights.front().intensity - 0.5F) < 1e-6F &&
                     std::abs(lights.front().colour[2] - 0.5F) < 1e-6F,
                 text::format("the view's lights: the one behind left out, the near point light first (%zu)",
                              lights.size()));
    request.max = 1;
    lights = rt::select_view_lights(record, &level, request);
    request.emitters = false;
    const std::vector<rt::Emitter> points = rt::select_view_lights(record, &level, request);
    tally->check(lights.size() == 1 && points.size() == 1 && points.front().kind == rt::EmitterKind::kPoint,
                 "rt_lights_max caps them; the emitters can be left out");
}

// --- the scene ---------------------------------------------------------------------------------------------------

// A box (or, with no height, the floor square): its albedo (the material's colour), the colour it's drawn with.
struct Solid {
    Vec3 low{};
    Vec3 high{};
    Rgb albedo{};
    uint32_t drawn = 0;
};

constexpr double kFloorHalf = 150.0;
const std::array<Solid, 2> kScene{{
    {.low = {-kFloorHalf, 0.0, -kFloorHalf},
     .high = {kFloorHalf, 0.0, kFloorHalf},
     .albedo = {0.8, 0.8, 0.8},
     .drawn = 0xFF202020},
    {.low = {-10.0, 0.0, -20.0}, .high = {10.0, 30.0, 20.0}, .albedo = {0.5, 0.6, 0.7}, .drawn = 0xFF282C30},
}};

constexpr Vec3 kEye{0.0, 140.0, 190.0};
constexpr Vec3 kAt{0.0, 0.0, 0.0};
constexpr Vec3 kUp{0.0, 1.0, 0.0};

struct Hit {
    double distance = kNoHit;
    Vec3 normal{};
};
Hit intersect(const Solid& solid, const Vec3& origin, const Vec3& direction)
{
    constexpr double kParallel = 1e-12;
    double enter = 0.0;
    double leave = kNoHit;
    Vec3 normal{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double from = core::at(origin, axis);
        const double along = core::at(direction, axis);
        const double low = core::at(solid.low, axis);
        const double high = core::at(solid.high, axis);
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
            normal = {};
            core::at(normal, axis) = rising ? -1.0 : 1.0;
        }
        leave = std::min(leave, far_t);
        if (enter > leave) {
            return {};
        }
    }
    if (enter <= 0.0) {
        return {};
    }
    return {.distance = enter, .normal = normal};
}

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

struct Seen {
    std::size_t solid = kNoSurface;
    Vec3 normal{};
    Vec3 point{};
    double distance = 0.0;
};
Seen seen_at(const View& view, double px, double py)
{
    const double ndc_x = (2.0 * px / kTargetWidth) - 1.0;
    const double ndc_y = 1.0 - (2.0 * py / kTargetHeight);
    const Vec3 direction = normalised(
        step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height), view.back, -1.0));
    Seen seen;
    double nearest = kNoHit;
    for (std::size_t index = 0; index < kScene.size(); ++index) {
        const Hit hit = intersect(core::at(kScene, index), view.eye, direction);
        if (hit.distance < nearest) {
            nearest = hit.distance;
            seen = {.solid = index + 1, .normal = hit.normal, .point = step(view.eye, direction, hit.distance),
                    .distance = hit.distance};
        }
    }
    return seen;
}

// A lamp as the reference lights with it (its colour x intensity x its texture's tint).
struct Lamp {
    Vec3 position{};
    double size = 0.0;
    Rgb light{};
    double reach = 0.0;
    double core = 0.0; // (its falloff's: rt::Emitter::core)
};

// A point light's lamp (its core rt::point_core's).
Lamp point_lamp(const Vec3& at, const Rgb& light, double reach)
{
    return {.position = at, .size = 0.0, .light = light, .reach = reach,
            .core = static_cast<double>(rt::kPointCoreShare) * reach};
}

double falloff(double d, double core, double reach)
{
    if (!(d < reach) || !(core > 0.0)) {
        return 0.0;
    }
    const double x = d / reach;
    const double window = 1.0 - (x * x * x * x);
    return window * window * core * core / ((core * core) + (d * d));
}

// (added for the cut-outs) Whether the box blocks the lamps' rays in the reference: false while its material is cut out
// everywhere (its texels' alpha under the test's reference - the rays go through it).
bool& box_blocks()
{
    static bool blocks = true;
    return blocks;
}

bool lamp_blocked(const Seen& seen, const Lamp& lamp)
{
    const double offset = kOffsetScale * (1.0 + (seen.distance * 0.0005) + (seen.distance * seen.distance * 6e-8));
    const Vec3 origin = step(seen.point, seen.normal, offset);
    const Vec3 to = minus(lamp.position, origin);
    const double dist = length_of(to);
    const double stop = dist - lamp.size;
    if (stop <= 0.0) {
        return false;
    }
    const Vec3 direction = normalised(to);
    for (std::size_t i = 0; i < kScene.size(); ++i) {
        if (i == 1 && !box_blocks()) {
            continue;
        }
        const Hit hit = intersect(core::at(kScene, i), origin, direction);
        if (hit.distance <= stop) {
            return true;
        }
    }
    return false;
}

struct Answer {
    Rgb added{};                 // strength x albedo x light
    double raw = 0.0;            // the light's luminance before it was held under the knee
    std::vector<bool> blocked{}; // each lamp's shadow ray
    bool close = false;          // the last lamp traced and the first not nearly as bright: either may be picked
};
// The pass's answer at a surface: every lamp's light, the brightest `traced` traced, the rest in the traced ones' share.
Answer answer_at(const Seen& seen, std::span<const Lamp> lamps, unsigned traced)
{
    Answer answer;
    struct Each {
        Rgb light{};
        double weight = 0.0;
        bool blocked = false;
    };
    std::vector<Each> each;
    for (const Lamp& lamp : lamps) {
        const Vec3 to = minus(lamp.position, seen.point);
        const double dist = length_of(to);
        const double facing = dot(seen.normal, normalised(to));
        Each item;
        item.blocked = lamp_blocked(seen, lamp);
        answer.blocked.push_back(item.blocked);
        if (dist < lamp.reach && facing > 0.0) {
            const double scale = falloff(dist, lamp.core, lamp.reach) * facing;
            item.light = {core::at(lamp.light, 0) * scale, core::at(lamp.light, 1) * scale, core::at(lamp.light, 2) * scale};
            item.weight = (0.299 * item.light[0]) + (0.587 * item.light[1]) + (0.114 * item.light[2]);
        }
        each.push_back(item);
    }
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < each.size(); ++i) {
        if (core::at(each, i).weight > 0.0) {
            order.push_back(i);
        }
    }
    std::ranges::stable_sort(order, [&](std::size_t one, std::size_t other) {
        return core::at(each, one).weight > core::at(each, other).weight;
    });
    constexpr double kClose = 0.02;
    if (order.size() > traced && traced > 0) {
        const double last = core::at(each, core::at(order, traced - 1)).weight;
        const double next = core::at(each, core::at(order, traced)).weight;
        answer.close = last - next <= kClose * last;
    }
    Rgb lit{};
    Rgb rest{};
    double through = 0.0;
    double total = 0.0;
    for (std::size_t k = 0; k < order.size(); ++k) {
        const Each& item = core::at(each, core::at(order, k));
        if (k < traced) {
            const double seen_share = item.blocked ? 0.0 : 1.0;
            for (std::size_t c = 0; c < 3; ++c) {
                core::at(lit, c) += core::at(item.light, c) * seen_share;
            }
            through += seen_share * item.weight;
            total += item.weight;
        } else {
            for (std::size_t c = 0; c < 3; ++c) {
                core::at(rest, c) += core::at(item.light, c);
            }
        }
    }
    const Rgb& albedo = core::at(kScene, seen.solid - 1).albedo;
    const double share = total > 0.0 ? through / total : 0.0;
    Rgb light{};
    for (std::size_t c = 0; c < 3; ++c) {
        core::at(light, c) = core::at(lit, c) + (core::at(rest, c) * share);
    }
    // (lighting round A: the sum as it is - no knee)
    answer.raw = (0.299 * light[0]) + (0.587 * light[1]) + (0.114 * light[2]);
    for (std::size_t c = 0; c < 3; ++c) {
        // (round F: the albedo - the material's display-referred colour - decoded to linear light, as the pass does)
        core::at(answer.added, c) =
            static_cast<double>(kStrength) * render::rt::linear_from_display(core::at(albedo, c)) * core::at(light, c);
    }
    return answer;
}

// --- the meshes and materials through the backend -------------------------------------------------------------------

std::vector<std::array<float, 3>> triangles_of(const Solid& solid)
{
    using Corner = std::array<int, 3>;
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

struct SceneMesh {
    rt::MeshId id = 0;
    rt::MaterialId material = 0;
    render::GpuVertexBuffer* vertices = nullptr;
    std::vector<std::array<float, 3>> triangles;
    uint32_t drawn = 0;
    uint32_t flags = kWorldOpaque; // (added for the cut-outs) the instance's flags
};

void put_float(std::span<uint8_t> bytes, std::size_t at, float value)
{
    std::memcpy(bytes.subspan(at, sizeof(value)).data(), &value, sizeof(value));
}
void put_word(std::span<uint8_t> bytes, std::size_t at, uint32_t value)
{
    std::memcpy(bytes.subspan(at, sizeof(value)).data(), &value, sizeof(value));
}

bool make_solid(render::Gpu& gpu, const Solid& solid, SceneMesh* mesh)
{
    mesh->triangles = triangles_of(solid);
    mesh->drawn = solid.drawn;
    std::vector<uint8_t> bytes(mesh->triangles.size() * kTexturedStride);
    for (std::size_t i = 0; i < mesh->triangles.size(); ++i) {
        const std::size_t base = i * kTexturedStride;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            put_float(bytes, base + (axis * sizeof(float)), core::at(core::at(mesh->triangles, i), axis));
        }
        put_word(bytes, base + kTexturedColour, kGrey);
        put_float(bytes, base + kTexturedUv, 0.0F);
        put_float(bytes, base + kTexturedUv + sizeof(float), 0.0F);
    }
    void* data = nullptr;
    if (gpu.create_vertex_buffer(static_cast<uint32_t>(bytes.size()), 0, kFvfTextured, value::kPoolDefault,
                                 &mesh->vertices) != value::kResultOk ||
        gpu.lock_vertex_buffer(mesh->vertices, 0, 0, &data, 0) != value::kResultOk) {
        return false;
    }
    std::memcpy(data, bytes.data(), bytes.size());
    if (gpu.unlock_vertex_buffer(mesh->vertices) != value::kResultOk) {
        return false;
    }
    const rt::Geometry geometry{.vertices = mesh->vertices,
                                .vertex_offset = 0,
                                .vertex_stride = kTexturedStride,
                                .vertex_count = static_cast<uint32_t>(mesh->triangles.size()),
                                .position_offset = 0,
                                .indices = nullptr,
                                .index_offset = 0,
                                .index_count = 0,
                                .indices_32bit = false,
                                .strip = false,
                                .dynamic = false,
                                .uv_offset = kTexturedUv,
                                .uv_format = rt::kUvFloat2,
                                .colour_offset = kTexturedColour,
                                .colour_format = rt::kColourA8R8G8B8};
    mesh->id = rt::next_mesh_id();
    rt::ray_tracing()->add_mesh(mesh->id, geometry);
    mesh->material = rt::next_material_id();
    rt::Material surface;
    surface.colour = {static_cast<float>(core::at(solid.albedo, 0)), static_cast<float>(core::at(solid.albedo, 1)),
                      static_cast<float>(core::at(solid.albedo, 2)), 1.0F};
    surface.flags = rt::kOpaque;
    rt::ray_tracing()->set_material(mesh->material, surface);
    return true;
}

void release_solid(render::Gpu& gpu, SceneMesh* mesh)
{
    if (rt::ray_tracing() != nullptr) {
        if (mesh->id != 0) {
            rt::ray_tracing()->remove_mesh(mesh->id);
        }
        if (mesh->material != 0) {
            rt::ray_tracing()->remove_material(mesh->material);
        }
    }
    if (mesh->vertices != nullptr) {
        gpu.release_vertex_buffer(mesh->vertices);
    }
    *mesh = SceneMesh{};
}

// The red glow's material: a 2 x 2 texture all one red (its mean: the light's tint).
bool make_red_material(render::Gpu& gpu, render::GpuTexture** texture, rt::MaterialId* material)
{
    if (gpu.create_texture({.width = kTextureSide,
                            .height = kTextureSide,
                            .levels = 1,
                            .usage = 0,
                            .format = value::kFormatA8R8G8B8,
                            .pool = value::kPoolManaged},
                           texture) != value::kResultOk) {
        *texture = nullptr;
        return false;
    }
    render::GpuLockedRect locked;
    if (gpu.lock_texture(*texture, 0, &locked, nullptr, 0) != value::kResultOk) {
        return false;
    }
    const auto pitch = static_cast<std::size_t>(locked.pitch);
    const std::span<uint8_t> into(static_cast<uint8_t*>(locked.bits), pitch + (kTextureSide * kPixelBytes));
    for (uint32_t row = 0; row < kTextureSide; ++row) {
        for (uint32_t column = 0; column < kTextureSide; ++column) {
            put_word(into, (row * pitch) + (column * kPixelBytes), kRedTexel);
        }
    }
    gpu.unlock_texture(*texture, 0);
    *material = rt::next_material_id();
    rt::Material surface;
    surface.texture = *texture;
    surface.flags = rt::kTransparent;
    rt::ray_tracing()->set_material(*material, surface);
    return true;
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

void draw_scene(render::Gpu& gpu, std::span<const SceneMesh> meshes)
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
    for (const SceneMesh& mesh : meshes) {
        std::vector<ColourVertex> vertices;
        vertices.reserve(mesh.triangles.size());
        for (const std::array<float, 3>& position : mesh.triangles) {
            vertices.push_back({.position = position, .colour = mesh.drawn});
        }
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(vertices.size() / 3),
                             vertices.data(), sizeof(ColourVertex));
    }
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

// A frame: the record given (the instances, an ambient light, `points`), cleared, the scene drawn, the listeners
// (draw_viewport's call). Returns the lights passes recorded.
uint64_t draw_frame(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes,
                    std::span<const rt::Light> points, uint64_t frame, bool world = false,
                    const rt::ObjectLight* object_light = nullptr)
{
    rt::FrameRecord& record = rt::frame_record();
    record = rt::FrameRecord{};
    record.frame = frame;
    constexpr std::array<std::array<float, 4>, 3> kIdentityRows{
        {{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
    for (const SceneMesh& mesh : meshes) {
        record.instances.push_back(
            {.mesh = mesh.id, .material = mesh.material, .flags = mesh.flags, .object_to_world = kIdentityRows});
        // (round O) an object's lights, as render_pc/pc_instance.cpp records them
        if (object_light != nullptr && (mesh.flags & rt::kObject) != 0U) {
            record.instances.back().light = static_cast<uint32_t>(record.object_lights.size());
            record.object_lights.push_back(*object_light);
        }
    }
    record.lights.push_back({.kind = rt::Light::Kind::kAmbient, .colour = {0.1F, 0.1F, 0.15F}});
    for (const rt::Light& light : points) {
        record.lights.push_back(light);
    }
    gpu.begin_scene();
    rt::ray_tracing()->begin_frame(record);
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kClearColour, 1.0F, 0);
    draw_scene(gpu, meshes);
    const uint64_t before = render::vulkan::rt_lights_passes();
    rt::opaque_view_drawn(view_drawn());
    if (world) {
        rt::world_view_drawn(view_drawn()); // (the volumetric fog's)
    }
    const uint64_t passes = render::vulkan::rt_lights_passes() - before;
    gpu.end_scene();
    gpu.present();
    return passes;
}

Rgb pixel_at(const std::vector<uint8_t>& texels, int x, int y)
{
    const std::size_t at = ((static_cast<std::size_t>(y) * kTargetWidth) + static_cast<std::size_t>(x)) * kPixelBytes;
    return {core::at(texels, at + 2) / kByteRange, core::at(texels, at + 1) / kByteRange,
            core::at(texels, at) / kByteRange};
}

Rgb drawn_colour(uint32_t argb)
{
    return {static_cast<double>((argb >> 16U) & 0xFFU) / kByteRange,
            static_cast<double>((argb >> 8U) & 0xFFU) / kByteRange, static_cast<double>(argb & 0xFFU) / kByteRange};
}

render::vulkan::RtLightSettings settings_for(bool on, unsigned shadowed)
{
    render::vulkan::RtLightSettings settings;
    settings.on = on;
    settings.strength = kStrength;
    settings.shadowed = shadowed;
    return settings;
}

// What a frame's colours are against: every pixel exactly as drawn (nothing added).
int pixels_changed(const std::vector<uint8_t>& texels, const View& view)
{
    int changed = 0;
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const Seen seen = seen_at(view, x, y);
            if (seen.solid == kNoSurface) {
                continue;
            }
            const Rgb drawn = drawn_colour(core::at(kScene, seen.solid - 1).drawn);
            const Rgb got = pixel_at(texels, x, y);
            for (std::size_t c = 0; c < 3; ++c) {
                if (std::lround(core::at(got, c) * kByteRange) != std::lround(core::at(drawn, c) * kByteRange)) {
                    ++changed;
                    break;
                }
            }
        }
    }
    return changed;
}

// The frame against the reference: the clear-cut pixels within the tolerance. Returns false when the frame failed.
struct Compared {
    int clear_cut = 0;
    int wrong = 0;
    int lit_floor = 0;
    int shadowed_floor = 0;
    double worst = 0.0;
    double red = 0.0;   // the lit floor's added light, summed per channel
    double green = 0.0;
};
Compared compare(const std::vector<uint8_t>& texels, const View& view, std::span<const Lamp> lamps, unsigned traced,
                 int tolerance)
{
    Compared out;
    std::vector<Seen> seen(static_cast<std::size_t>(kColumns) * kRows);
    std::vector<Answer> answers(seen.size());
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * kColumns) + static_cast<std::size_t>(x);
            core::at(seen, at) = seen_at(view, x, y);
            if (core::at(seen, at).solid != kNoSurface) {
                core::at(answers, at) = answer_at(core::at(seen, at), lamps, traced);
            }
        }
    }
    for (int y = kClearCut; y < kRows - kClearCut; ++y) {
        for (int x = kClearCut; x < kColumns - kClearCut; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * kColumns) + static_cast<std::size_t>(x);
            const Seen& here = core::at(seen, at);
            if (here.solid == kNoSurface) {
                continue;
            }
            bool clear = true;
            for (int dy = -kClearCut; dy <= kClearCut && clear; ++dy) {
                for (int dx = -kClearCut; dx <= kClearCut && clear; ++dx) {
                    const std::size_t other = (static_cast<std::size_t>(y + dy) * kColumns) + static_cast<std::size_t>(x + dx);
                    clear = core::at(seen, other).solid == here.solid && core::at(seen, other).normal == here.normal &&
                            core::at(answers, other).blocked == core::at(answers, at).blocked &&
                            !core::at(answers, other).close;
                }
            }
            if (!clear) {
                continue;
            }
            ++out.clear_cut;
            const Answer& answer = core::at(answers, at);
            const Rgb drawn = drawn_colour(core::at(kScene, here.solid - 1).drawn);
            const Rgb got = pixel_at(texels, x, y);
            bool bad = false;
            for (std::size_t c = 0; c < 3; ++c) {
                const double added = std::max(0.0, core::at(answer.added, c));
                const double expected = std::min(1.0, core::at(drawn, c) + added); // (round F: light added, no screen)
                const double error = std::abs(core::at(got, c) - expected) * kByteRange;
                out.worst = std::max(out.worst, error);
                bad = bad || error > tolerance;
            }
            out.wrong += bad ? 1 : 0;
            const double light = (core::at(answer.added, 0) + core::at(answer.added, 1) + core::at(answer.added, 2));
            if (here.solid == 1 && y_of(here.normal) > 0.5 && light > 0.0) {
                out.red += core::at(got, 0) - core::at(drawn, 0);
                out.green += core::at(got, 1) - core::at(drawn, 1);
            }
            if (here.solid == 1 && y_of(here.normal) > 0.5 && !answer.blocked.empty()) {
                const bool any_blocked = std::ranges::any_of(answer.blocked, [](bool b) { return b; });
                out.shadowed_floor += any_blocked ? 1 : 0;
                out.lit_floor += any_blocked ? 0 : 1;
            }
        }
    }
    return out;
}

// One case: the frame drawn with `points` and `emitters`, compared with the reference for `lamps`.
Compared run_case(render::Gpu& gpu, std::span<const SceneMesh> meshes, uint32_t samples, std::span<const rt::Light> points,
                  std::span<const Lamp> lamps, unsigned traced, uint64_t* passes, std::vector<uint8_t>* texels)
{
    Targets targets;
    Compared compared;
    *passes = 0;
    if (make_targets(gpu, samples, &targets)) {
        *passes = draw_frame(gpu, targets, meshes, points, 1);
        if (read_colour(gpu, targets, texels)) {
            compared = compare(*texels, view_from(kEye), lamps, traced,
                               samples == 0 ? kTolerance : kToleranceSampled);
        }
    }
    release_targets(gpu, targets);
    return compared;
}

constexpr Vec3 kLampAt{-45.0, 40.0, 10.0};
constexpr double kLampReach = 400.0;

void run_point(render::Gpu& gpu, std::span<const SceneMesh> meshes, uint32_t samples, Tally* tally)
{
    text::print("a point light beside the box (%s):\n", samples == 0 ? "one sample" : "four samples, resolved");
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
    rt::set_level_emitters({});
    const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 0.9F, 0.7F},
                                            .position = floats(kLampAt),
                                            .radius = static_cast<float>(kLampReach)}}};
    const std::array<Lamp, 1> lamps{point_lamp(kLampAt, {1.0, 0.9, 0.7}, kLampReach)};
    uint64_t passes = 0;
    std::vector<uint8_t> texels;
    const Compared got = run_case(gpu, meshes, samples, points, lamps, 4, &passes, &texels);
    tally->check(passes == 1, text::format("the pass recorded (%llu)", static_cast<unsigned long long>(passes)));
    tally->check(got.clear_cut > 5000 && got.wrong == 0,
                 text::format("%d clear-cut pixels as the processor's lamp light, %d wrong (worst %.2f of 255)",
                              got.clear_cut, got.wrong, got.worst));
    tally->check(got.lit_floor >= kMinLit && got.shadowed_floor >= kMinShadowed,
                 text::format("the floor lit (%d) and in the box's shadow (%d)", got.lit_floor, got.shadowed_floor));
}

void run_emitter(render::Gpu& gpu, std::span<const SceneMesh> meshes, rt::MaterialId red, Tally* tally)
{
    text::print("a red glow (the level's emitter, its material's texture red):\n");
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
    rt::LevelEmitters level;
    level.serial = 1;
    constexpr float kSize = 6.0F;
    constexpr float kCore = 30.0F;
    level.emitters.push_back({.position = floats(kLampAt), .size = kSize, .colour = {1.0F, 1.0F, 1.0F},
                              .intensity = 1.2F, .reach = static_cast<float>(kLampReach), .material = red,
                              .kind = rt::EmitterKind::kGlow, .core = kCore});
    rt::set_level_emitters(level);
    // the texture's mean: (1, 0x20 / 0xFF, 0x20 / 0xFF), its brightest channel 1
    constexpr double kTintGreen = 32.0 / 255.0;
    const std::array<Lamp, 1> lamps{
        {{.position = kLampAt, .size = kSize, .light = {1.2, 1.2 * kTintGreen, 1.2 * kTintGreen}, .reach = kLampReach,
          .core = kCore}}};
    uint64_t passes = 0;
    std::vector<uint8_t> texels;
    const Compared got = run_case(gpu, meshes, 0, {}, lamps, 4, &passes, &texels);
    tally->check(passes == 1 && got.clear_cut > 5000 && got.wrong == 0,
                 text::format("%d clear-cut pixels as the processor's red glow, %d wrong (worst %.2f of 255)",
                              got.clear_cut, got.wrong, got.worst));
    tally->check(got.red > 4.0 * got.green && got.red > 1.0,
                 text::format("the floor lit red (red %.1f, green %.1f added)", got.red, got.green));
    rt::set_level_emitters({});
}

void run_two(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("two lamps, one traced (rt_lights_shadowed=1):\n");
    render::vulkan::set_rt_light_settings(settings_for(true, 1));
    rt::set_level_emitters({});
    constexpr Vec3 kOther{50.0, 25.0, -40.0};
    const std::array<rt::Light, 2> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 0.9F, 0.7F},
                                            .position = floats(kLampAt),
                                            .radius = static_cast<float>(kLampReach)},
                                           {.kind = rt::Light::Kind::kPoint,
                                            .colour = {0.3F, 0.4F, 0.8F},
                                            .position = floats(kOther),
                                            .radius = 300.0F}}};
    const std::array<Lamp, 2> lamps{point_lamp(kLampAt, {1.0, 0.9, 0.7}, kLampReach),
                                    point_lamp(kOther, {0.3, 0.4, 0.8}, 300.0)};
    uint64_t passes = 0;
    std::vector<uint8_t> texels;
    const Compared got = run_case(gpu, meshes, 0, points, lamps, 1, &passes, &texels);
    tally->check(passes == 1 && got.clear_cut > 3000 && got.wrong == 0,
                 text::format("%d clear-cut pixels as the processor's, %d wrong (worst %.2f of 255)", got.clear_cut,
                              got.wrong, got.worst));
}

// (added for the cut-outs - the one lighting path's ray test, vulkan_rt_scene.h cutout_rays_glsl) The box made a cut-out
// caster (alpha tested, its texels white x its albedo): with every texel's alpha 0 the lamp's light goes through it - the
// floor it shadowed lit, as a scene without it -, with every texel's alpha 255 it shadows as the solid box did.
bool make_cutout_material(render::Gpu& gpu, uint32_t texel, const Rgb& albedo, render::GpuTexture** texture,
                          rt::MaterialId* material)
{
    if (gpu.create_texture({.width = kTextureSide,
                            .height = kTextureSide,
                            .levels = 1,
                            .usage = 0,
                            .format = value::kFormatA8R8G8B8,
                            .pool = value::kPoolManaged},
                           texture) != value::kResultOk) {
        *texture = nullptr;
        return false;
    }
    render::GpuLockedRect locked;
    if (gpu.lock_texture(*texture, 0, &locked, nullptr, 0) != value::kResultOk) {
        return false;
    }
    const auto pitch = static_cast<std::size_t>(locked.pitch);
    const std::span<uint8_t> into(static_cast<uint8_t*>(locked.bits), pitch + (kTextureSide * kPixelBytes));
    for (uint32_t row = 0; row < kTextureSide; ++row) {
        for (uint32_t column = 0; column < kTextureSide; ++column) {
            put_word(into, (row * pitch) + (column * kPixelBytes), texel);
        }
    }
    gpu.unlock_texture(*texture, 0);
    *material = rt::next_material_id();
    rt::Material surface;
    surface.texture = *texture;
    surface.colour = {static_cast<float>(core::at(albedo, 0)), static_cast<float>(core::at(albedo, 1)),
                      static_cast<float>(core::at(albedo, 2)), 1.0F};
    surface.flags = rt::kAlphaTested;
    surface.alpha_ref = 0.5F;
    rt::ray_tracing()->set_material(*material, surface);
    return true;
}

void run_cutout(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("the box a cut-out caster (alpha tested):\n");
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
    rt::set_level_emitters({});
    const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 0.9F, 0.7F},
                                            .position = floats(kLampAt),
                                            .radius = static_cast<float>(kLampReach)}}};
    const std::array<Lamp, 1> lamps{point_lamp(kLampAt, {1.0, 0.9, 0.7}, kLampReach)};
    constexpr uint32_t kHole = 0x00FFFFFFU;  // white, alpha 0: every texel cut away
    constexpr uint32_t kSolid = 0xFFFFFFFFU; // white, alpha 255: every texel kept
    for (const uint32_t texel : {kHole, kSolid}) {
        render::GpuTexture* texture = nullptr;
        rt::MaterialId material = 0;
        if (!make_cutout_material(gpu, texel, core::at(kScene, 1).albedo, &texture, &material)) {
            tally->check(false, "the cut-out material made");
            return;
        }
        std::vector<SceneMesh> cut(meshes.begin(), meshes.end());
        core::at(cut, 1).material = material;
        core::at(cut, 1).flags = rt::kWorld | rt::kAlphaTested | rt::kCastsShadow;
        const bool hole = texel == kHole;
        box_blocks() = !hole;
        uint64_t passes = 0;
        std::vector<uint8_t> texels;
        const Compared got = run_case(gpu, cut, 0, points, lamps, 4, &passes, &texels);
        box_blocks() = true;
        // (added by lighting round 4) the relit level's probe rays (RayTracing::probe_many): through the cut-out's holes
        // when they ask for the cut-outs (ProbeRequest::cutouts - the same test), solid without; a ray at the box's side
        const std::array<rt::ProbeRequest, 2> probes{{
            {.origin = {-45.0F, 15.0F, 0.0F}, .direction = {1.0F, 0.0F, 0.0F}, .max_distance = 200.0F,
             .cull_mask = rt::kRelitWorldMask, .cutouts = true},
            {.origin = {-45.0F, 15.0F, 0.0F}, .direction = {1.0F, 0.0F, 0.0F}, .max_distance = 200.0F,
             .cull_mask = rt::kRelitWorldMask, .cutouts = false},
        }};
        std::vector<rt::ProbeResult> probed;
        rt::ray_tracing()->probe_many(probes, &probed);
        constexpr float kBoxSide = 35.0F; // (from x -45 to the box's side at -10)
        const auto at_box = [&](std::size_t index) {
            return probed.size() == probes.size() && core::at(probed, index).hit &&
                   std::abs(core::at(probed, index).distance - kBoxSide) < 0.5F;
        };
        const bool through = probed.size() == probes.size() && core::at(probed, 0).traced && !core::at(probed, 0).hit;
        tally->check(hole ? (through && at_box(1)) : (at_box(0) && at_box(1)),
                     hole ? "the probe's rays: through the cut-out's holes when they ask for the cut-outs, solid without"
                          : "the probe's rays: the solid texels stop them either way");
        if (hole) {
            tally->check(passes == 1 && got.clear_cut > 5000 && got.wrong == 0 && got.shadowed_floor == 0,
                         text::format("its texels' alpha 0: the lamp's light through it, as without it - %d clear-cut "
                                      "pixels, %d wrong (worst %.2f of 255), %d floor pixels shadowed",
                                      got.clear_cut, got.wrong, got.worst, got.shadowed_floor));
        } else {
            tally->check(passes == 1 && got.clear_cut > 5000 && got.wrong == 0 && got.shadowed_floor >= kMinShadowed,
                         text::format("its texels' alpha 255: it shadows as the solid box - %d clear-cut pixels, %d "
                                      "wrong (worst %.2f of 255), %d floor pixels shadowed",
                                      got.clear_cut, got.wrong, got.worst, got.shadowed_floor));
        }
        rt::ray_tracing()->remove_material(material);
        gpu.release_texture(texture);
    }
}

void run_off(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("off, and no lamp in view:\n");
    const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 1.0F, 1.0F},
                                            .position = floats(kLampAt),
                                            .radius = static_cast<float>(kLampReach)}}};
    render::vulkan::set_rt_light_settings(settings_for(false, 4));
    uint64_t passes = 0;
    std::vector<uint8_t> texels;
    run_case(gpu, meshes, 0, points, {}, 4, &passes, &texels);
    const int changed_off = texels.empty() ? -1 : pixels_changed(texels, view_from(kEye));
    tally->check(passes == 0 && changed_off == 0,
                 text::format("rt_lights=0: no pass, every pixel as drawn (%d changed)", changed_off));
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
    const std::array<rt::Light, 1> behind{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 1.0F, 1.0F},
                                            .position = {0.0F, 140.0F, 900.0F},
                                            .radius = 300.0F}}};
    run_case(gpu, meshes, 0, behind, {}, 4, &passes, &texels);
    const int changed_behind = texels.empty() ? -1 : pixels_changed(texels, view_from(kEye));
    tally->check(passes == 0 && changed_behind == 0,
                 text::format("a lamp behind the camera out of reach: no pass, every pixel as drawn (%d changed)",
                              changed_behind));
}

// (added by the time of day - engine/gfx/rt_daylight.h) The lamps with the clock: off at noon (every pixel as drawn), on
// at night (the processor's lamp light), half on at dusk (half its light); tod=level (the clock off) as before.
void run_time_of_day(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("the time of day (tod):\n");
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
    rt::set_level_emitters({});
    const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 0.9F, 0.7F},
                                            .position = floats(kLampAt),
                                            .radius = static_cast<float>(kLampReach)}}};
    uint64_t passes = 0;
    std::vector<uint8_t> texels;
    // noon: the lamps off
    rt::set_day_light({.on = true, .lamps = 0.0F});
    run_case(gpu, meshes, 0, points, {}, 4, &passes, &texels);
    const int changed = texels.empty() ? -1 : pixels_changed(texels, view_from(kEye));
    tally->check(changed == 0, text::format("  noon: the lamps off - every pixel as drawn (%d changed)", changed));
    // night and dusk: all of the lamp's light, and half
    constexpr std::array<double, 2> kLamps{1.0, 0.5};
    for (const double share : kLamps) {
        rt::set_day_light({.on = true, .lamps = static_cast<float>(share)});
        const std::array<Lamp, 1> lamps{point_lamp(kLampAt, {share * 1.0, share * 0.9, share * 0.7}, kLampReach)};
        const Compared got = run_case(gpu, meshes, 0, points, lamps, 4, &passes, &texels);
        tally->check(got.clear_cut > 5000 && got.wrong == 0,
                     text::format("  lamps %.1f: %d clear-cut pixels as the processor's lamp light x %.1f, %d wrong "
                                  "(worst %.2f of 255)",
                                  share, got.clear_cut, share, got.wrong, got.worst));
    }
    // tod=level: the lamps as the setting says
    rt::set_day_light({});
    const std::array<Lamp, 1> whole{point_lamp(kLampAt, {1.0, 0.9, 0.7}, kLampReach)};
    const Compared level = run_case(gpu, meshes, 0, points, whole, 4, &passes, &texels);
    tally->check(level.clear_cut > 5000 && level.wrong == 0,
                 text::format("  tod=level: the lamp's whole light as before (%d wrong)", level.wrong));
}

// --- (added by the volumetric light) the lamp in the air ----------------------------------------------------------------

constexpr int kFogFrames = 40;
constexpr Vec3 kFogLampAt{-30.0, 15.0, 0.0}; // (low beside the box: its shadow a wedge of the air behind it)
constexpr uint32_t kFineSteps = 512;
constexpr double kLeastLight = 1e-4;   // a texel the lamp lights at all (luma; the haze scatters forward - from the side
                                       // a lamp's cone is faint: changed by the physical air)
constexpr double kShadowed = 0.85;     // the shadowed march under this share of the unshadowed: across the box's shadow
constexpr double kLit = 0.99;          // ... over this: in its open light
constexpr std::size_t kLeastTexels = 40;
constexpr double kMostRatio = 1.3;     // the device's mean over the march's
constexpr double kLeastRatio = 1.0 / kMostRatio;
constexpr double kDarker = 0.85;       // the shadowed texels' light under this share of the unshadowed march's

double luma_of(const Rgb& c) { return (0.2126 * core::at(c, 0)) + (0.7152 * core::at(c, 1)) + (0.0722 * core::at(c, 2)); }

// Whether the way from `at` to the lamp is blocked (the scene's solids; stopped its size short).
bool air_blocked(const Vec3& at, const Vec3& lamp, double size)
{
    const Vec3 to = minus(lamp, at);
    const double stop = length_of(to) - size;
    if (stop <= 0.0) {
        return false;
    }
    const Vec3 direction = normalised(to);
    return std::ranges::any_of(kScene, [&](const Solid& solid) { return intersect(solid, at, direction).distance <= stop; });
}

// The lamp's light the haze sends the eye along a view ray (the pass's thin haze: a linear "fog" from the camera to
// kFogHazeEnd whose light alone is added - the medium's colour x its light share x the shafts' share), unshadowed and
// shadowed by the scene.
struct AirLight {
    double open = 0.0;
    double shadowed = 0.0;
};
// (added by the street air) with `air` the street air too (rt_volume_light.h street_air over the floor's height): a
// second medium, its light the haze's per unit of light (vulkan_rt_fog.cpp's march: mixed by each segment's depth)
AirLight air_light(const View& view, double px, double py, const rt::Emitter& lamp, float scale,
                   const rt::StreetAir& air = {})
{
    const Seen seen = seen_at(view, px, py);
    const double ndc_x = (2.0 * px / kTargetWidth) - 1.0;
    const double ndc_y = 1.0 - (2.0 * py / kTargetHeight);
    const Vec3 direction = normalised(
        step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height), view.back, -1.0));
    const double end = render::vulkan::kFogHazeEnd;
    const double last = seen.solid == kNoSurface ? end : std::min(seen.distance, end);
    const Vec3 lamp_at{core::at(lamp.position, 0), core::at(lamp.position, 1), core::at(lamp.position, 2)};
    const Rgb colour{core::at(lamp.colour, 0) * lamp.intensity * scale, core::at(lamp.colour, 1) * lamp.intensity * scale,
                     core::at(lamp.colour, 2) * lamp.intensity * scale};
    // (the sky's pixels: the shafts' sky share - vulkan_rt_fog.h kFogSkyShare)
    const double medium = core::at(render::vulkan::kFogHazeColour, 0) * (1.0 - render::vulkan::kFogAmbientShare) *
                          (seen.solid == kNoSurface ? render::vulkan::kFogSkyShare : render::vulkan::kFogShaftShare);
    const double air_medium =
        render::vulkan::kFogStreetAirLight * (seen.solid == kNoSurface ? render::vulkan::kFogSkyShare : 1.0);
    AirLight out;
    double kept = 1.0;
    for (uint32_t i = 0; i < kFineSteps; ++i) {
        const double s0 = last * i / kFineSteps;
        const double s1 = last * (i + 1) / kFineSteps;
        const double s = 0.5 * (s0 + s1);
        const double haze = std::log(std::max(end - s0, 1e-4 * end) / std::max(end - s1, 1e-4 * end));
        const Vec3 at = step(view.eye, direction, s);
        // (the street air: its extinction at the point's height over the floor, y 0 - fine steps)
        const double street =
            air.height > 0.0 ? air.extinction * std::exp(-std::max(y_of(at), 0.0) / air.height) * (s1 - s0) : 0.0;
        const double e = std::exp(-(haze + street));
        const Vec3 to = minus(lamp_at, at);
        const double d = length_of(to);
        // (the phase: the weather's aerosol's - rt_volume_light.h medium_phase)
        const double open = luma_of(colour) * falloff(d, lamp.core, lamp.reach);
        const double shaped = rt::medium_phase(dot(direction, normalised(to)), rt::aerosol().g);
        const double light =
            open * ((haze * medium * shaped) + (street * air_medium * shaped)) / std::max(haze + street, 1e-12);
        out.open += kept * (1.0 - e) * light;
        if (!air_blocked(at, lamp_at, lamp.size)) {
            out.shadowed += kept * (1.0 - e) * light;
        }
        kept *= e;
    }
    return out;
}

void run_fog_lamp(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    namespace vk = render::vulkan;
    text::print("the lamp in the air (rt_fog's haze lit by the lamps' set, the box's shadow in it):\n");
    vk::set_rt_light_settings(settings_for(true, 4));
    rt::set_level_emitters({});
    vk::RtFogSettings fog;
    fog.on = true;
    fog.haze = true;
    fog.scale = 2; // (the texels' 2 x 2 blocks below; no street air - no sun or moon here)
    vk::set_rt_fog_settings(fog);
    const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 0.9F, 0.7F},
                                            .position = floats(kFogLampAt),
                                            .radius = static_cast<float>(kLampReach)}}};
    Targets targets;
    const bool made = make_targets(gpu, 0, &targets);
    tally->check(made, "  the targets made (a depth the fog can read)");
    const uint64_t fog_before = vk::rt_fog_passes();
    for (int frame = 0; made && frame < kFogFrames; ++frame) {
        draw_frame(gpu, targets, meshes, points, static_cast<uint64_t>(frame) + 1, true);
    }
    const uint64_t fog_passes = vk::rt_fog_passes() - fog_before;
    tally->check(fog_passes == kFogFrames,
                 text::format("  the fog's pass recorded each frame (%llu)", static_cast<unsigned long long>(fog_passes)));
    vk::RtFogTexels history;
    vk::RtLitLamps lamps;
    bool read = false;
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        read = made && vk::read_rt_fog_image(state, {0, 0, kTargetWidth, kTargetHeight}, vk::RtFogImage::kHistory, &history);
        lamps = vk::rt_lights_lit_locked(state);
    }
    tally->check(read && lamps.lights.size() == 1, text::format("  the history read back, the lamps' set (%zu)", lamps.lights.size()));
    if (read && lamps.lights.size() == 1) {
        const View view = view_from(kEye);
        double shadowed_device = 0.0;
        double shadowed_march = 0.0;
        double shadowed_open = 0.0;
        double lit_device = 0.0;
        double lit_march = 0.0;
        std::size_t shadowed = 0;
        std::size_t lit = 0;
        for (uint32_t y = 0; y < history.height; ++y) {
            for (uint32_t x = 0; x < history.width; ++x) {
                // (the texel's 2 x 2 block, marched a pixel at a time: its middle)
                const AirLight want = air_light(view, (2.0 * x) + 0.5, (2.0 * y) + 0.5, lamps.lights.front(), lamps.scale);
                const std::array<float, 4>& got = core::at(history.texels, (static_cast<std::size_t>(y) * history.width) + x);
                const double device = luma_of({core::at(got, 0), core::at(got, 1), core::at(got, 2)});
                if (!(want.open > kLeastLight)) {
                    continue;
                }
                if (want.shadowed < kShadowed * want.open) {
                    shadowed_device += device;
                    shadowed_march += want.shadowed;
                    shadowed_open += want.open;
                    ++shadowed;
                } else if (want.shadowed > kLit * want.open) {
                    lit_device += device;
                    lit_march += want.shadowed;
                    ++lit;
                }
            }
        }
        const double shadow_ratio = shadowed_march > 0.0 ? shadowed_device / shadowed_march : 0.0;
        const double lit_ratio = lit_march > 0.0 ? lit_device / lit_march : 0.0;
        tally->check(lit >= kLeastTexels && lit_ratio >= kLeastRatio && lit_ratio <= kMostRatio,
                     text::format("  %zu texels in the lamp's open light: the device's light %.3f x the march's", lit,
                                  lit_ratio));
        tally->check(shadowed >= kLeastTexels && shadow_ratio >= kLeastRatio && shadow_ratio <= kMostRatio &&
                         shadowed_device < kDarker * shadowed_open,
                     text::format("  %zu texels across the box's shadow: %.3f x the shadowed march's, %.3f of the "
                                  "unshadowed (a cone cut by the box)",
                                  shadowed, shadow_ratio, shadowed_open > 0.0 ? shadowed_device / shadowed_open : 0.0));
    }
    release_targets(gpu, targets);
    vk::set_rt_fog_settings({});
}

// (added by the street air) The lamp at night in the street air: the clock's night (the moon given no light, the
// lamps on), rt_fog_air misty against clear - the light the mist adds round the lamp, the device's against the
// processor's march of the same two media. (Changed by the physical air: two weathers, the aerosol's phase.)
constexpr double kLeastAirLight = 0.02; // the street air's light summed over the texels at least (luma)

std::array<double, 2> fog_lamp_night_sums(render::Gpu& gpu, std::span<const SceneMesh> meshes,
                                          std::span<const rt::Light> points, rt::Weather air, std::size_t* texels_out,
                                          rt::StreetAir* air_out, double* processor_out)
{
    namespace vk = render::vulkan;
    vk::RtFogSettings fog;
    fog.on = true;
    fog.haze = true;
    fog.scale = 2;
    fog.weather = air;
    vk::set_rt_fog_settings(fog);
    Targets targets;
    std::array<double, 2> sums{};
    if (!make_targets(gpu, 0, &targets)) {
        return sums;
    }
    for (int frame = 0; frame < kFogFrames; ++frame) {
        draw_frame(gpu, targets, meshes, points, static_cast<uint64_t>(frame) + 1, true);
    }
    vk::RtFogTexels history;
    vk::RtLitLamps lamps;
    bool read = false;
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        read = vk::read_rt_fog_image(state, {0, 0, kTargetWidth, kTargetHeight}, vk::RtFogImage::kHistory, &history);
        lamps = vk::rt_lights_lit_locked(state);
    }
    release_targets(gpu, targets);
    if (!read || lamps.lights.size() != 1) {
        return sums;
    }
    const rt::MediumLight light = rt::medium_light();
    (void)light;
    *air_out = rt::street_air(rt::aerosol_for(air), {});
    const View view = view_from(kEye);
    *texels_out = history.texels.size();
    double processor = 0.0;
    for (uint32_t y = 0; y < history.height; ++y) {
        for (uint32_t x = 0; x < history.width; ++x) {
            const std::array<float, 4>& got = core::at(history.texels, (static_cast<std::size_t>(y) * history.width) + x);
            core::at(sums, 0) += luma_of({core::at(got, 0), core::at(got, 1), core::at(got, 2)});
            processor +=
                air_light(view, (2.0 * x) + 0.5, (2.0 * y) + 0.5, lamps.lights.front(), lamps.scale, *air_out).shadowed;
        }
    }
    core::at(sums, 1) = processor;
    *processor_out = processor;
    return sums;
}

void run_fog_lamp_night(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    namespace vk = render::vulkan;
    text::print("the lamp in the street air at night (the clock's night, the lamps on; rt_fog_air misty against clear):\n");
    vk::set_rt_light_settings(settings_for(true, 4));
    rt::set_level_emitters({});
    rt::set_baked_sun(
        {.scene = 1, .found = true, .toward = {0.0F, 1.0F, 0.0F}, .confidence = 1.0F, .ground = 0.0F, .ground_known = true});
    rt::set_sun_relight({.relights = true});
    rt::DayLight day;
    day.on = true;
    day.lamps = 1.0F;
    day.moonlit = true;
    day.light = {0.0F, 0.8F, 0.6F};
    day.moon = day.light;
    day.sun = {0.0F, -0.5F, 0.866F}; // (30 degrees under the horizon: the night's air)
    day.light_colour = {0.0F, 0.0F, 0.0F}; // (no moonlight: the lamp's light alone)
    rt::set_day_light(day);
    const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 0.9F, 0.7F},
                                            .position = floats(kFogLampAt),
                                            .radius = static_cast<float>(kLampReach)}}};
    std::size_t texels = 0;
    rt::StreetAir none;
    rt::StreetAir cinematic;
    double processor_without = 0.0;
    double processor_with = 0.0;
    const std::array<double, 2> without =
        fog_lamp_night_sums(gpu, meshes, points, rt::Weather::kClear, &texels, &none, &processor_without);
    const std::array<double, 2> with =
        fog_lamp_night_sums(gpu, meshes, points, rt::Weather::kMisty, &texels, &cinematic, &processor_with);
    const double device_air = core::at(with, 0) - core::at(without, 0);
    const double processor_air = processor_with - processor_without;
    const double ratio = processor_air > 0.0 ? device_air / processor_air : 0.0;
    tally->check(texels > 0 && cinematic.extinction > none.extinction && none.extinction > 0.0,
                 text::format("  the history read, the night's street air %.3g a unit at the floor (%.0f units its "
                              "height)",
                              cinematic.extinction, cinematic.height));
    tally->check(processor_air > kLeastAirLight && ratio >= kLeastRatio && ratio <= kMostRatio,
                 text::format("  the street air's light round the lamp: %.3f summed (the processor's march %.3f: x %.3f)",
                              device_air, processor_air, ratio));
    rt::set_day_light({});
    rt::set_sun_relight({});
    rt::set_baked_sun({});
    vk::set_rt_fog_settings({});
}

// (added for the washed-out streets) Many lamps over the floor - 49 bright point lights in a 7 x 7 grid 40 units up, as a
// lamp-lit street packs them: the pass as the processor's (the sum held under the knee), the knee reached (their raw sum
// past it), and no floor pixel burnt white - before, their summed light at the default strength washed it out.
// --- (added by the light energy audit) each light counted once on the relit world ------------------------------------
// docs/design/vulkan.md "Each light counted once": the floor given a light stream (engine/gfx/rt_cel.h - the relit
// world's parts: its colour's ambient, the sun's light and the visibility it was written with, the lamps' residual), the
// ray-traced passes read it (vulkan_rt_scene.h hit_world_light) and replace the parts they own instead of adding to them:
//   - the shadows: a floor whose colour already holds the box's shadow (seen 0) is left as it is in that shadow - the
//     traced shadow isn't put on the baked one - and lit where the pixel's own ray sees the sun (up to twice: the blend);
//     a floor written lit (seen 1) takes the shadow's share of its light, A / (A + S), not the whole strength;
//   - the lamps: the floor's residual (the bake's lamp) at least the traced lamp's light - nothing added, where the box
//     (no stream) takes the lamp's light as before.

constexpr std::array<double, 3> kStreamSun{1.0, 0.75, 0.4}; // (toward the sun - normalised below)
constexpr double kStreamAmbient = 0.5; // the floor's sky light (light units: 1 = a colour byte's 128)
constexpr double kStreamDirect = 0.4;  // the sun's light (the model's a)

// The floor's stream words for every vertex: tint 1, the sun seen `seen`, relit's own lamps' light `lamp` (L, lit
// whole: the pass not lighting).
std::vector<rt::CelWords> floor_stream(std::size_t vertices, float seen, float lamp)
{
    rt::RelitModel model;
    model.valid = true;
    model.sun = {static_cast<float>(kStreamDirect), static_cast<float>(kStreamDirect), static_cast<float>(kStreamDirect)};
    model.sky = {static_cast<float>(kStreamAmbient), static_cast<float>(kStreamAmbient),
                 static_cast<float>(kStreamAmbient)};
    rt::RelitVertex vertex;
    vertex.normal = {0.0F, 1.0F, 0.0F};
    vertex.sun = seen;
    vertex.sky = 1.0F;
    vertex.tint = {1.0F, 1.0F, 1.0F};
    vertex.lamp = {lamp, lamp, lamp};
    vertex.used = true;
    rt::RelitLighting lighting = rt::relit_own_lighting(model, 1.0F); // (round A2: the light in the global unit)
    lighting.toward = {0.0F, 1.0F, 0.0F}; // (N.L 1: the floor faces the sun the stream was written for)
    return std::vector<rt::CelWords>(vertices, rt::cel_stream_words(model, vertex, lighting));
}

bool set_floor_stream(render::Gpu& gpu, const SceneMesh& floor, std::span<const rt::CelWords> words,
                      render::GpuVertexBuffer** stream)
{
    const auto bytes = static_cast<uint32_t>(words.size() * rt::kCelStreamBytes);
    void* data = nullptr;
    if ((*stream == nullptr && gpu.create_vertex_buffer(bytes, 0, 0, value::kPoolDefault, stream) != value::kResultOk) ||
        gpu.lock_vertex_buffer(*stream, 0, 0, &data, 0) != value::kResultOk) {
        return false;
    }
    std::memcpy(data, words.data(), bytes);
    if (gpu.unlock_vertex_buffer(*stream) != value::kResultOk) {
        return false;
    }
    rt::ray_tracing()->set_light_stream(floor.id, *stream);
    return true;
}

// The floor's pixels well inside the sun's light or the box's shadow (no edge within kClearCut + 1 pixels): their
// drawn colour and whether they are shadowed.
struct FloorPixel {
    int x = 0;
    int y = 0;
    bool shadowed = false;
};
std::vector<FloorPixel> floor_pixels(const Vec3& toward)
{
    const View view = view_from(kEye);
    std::vector<int> state(static_cast<std::size_t>(kColumns) * kRows, -1); // -1 not floor, 0 lit, 1 shadowed
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const Seen seen = seen_at(view, x, y);
            if (seen.solid != 1 || y_of(seen.normal) < 0.5) {
                continue;
            }
            const Hit hit = intersect(core::at(kScene, 1), step(seen.point, seen.normal, 1.0), toward);
            core::at(state, (static_cast<std::size_t>(y) * kColumns) + static_cast<std::size_t>(x)) =
                hit.distance < kNoHit ? 1 : 0;
        }
    }
    std::vector<FloorPixel> out;
    constexpr int kMargin = kClearCut + 1;
    for (int y = kMargin; y < kRows - kMargin; ++y) {
        for (int x = kMargin; x < kColumns - kMargin; ++x) {
            const int here = core::at(state, (static_cast<std::size_t>(y) * kColumns) + static_cast<std::size_t>(x));
            bool clear = here >= 0;
            for (int dy = -kMargin; dy <= kMargin && clear; ++dy) {
                for (int dx = -kMargin; dx <= kMargin && clear; ++dx) {
                    clear = core::at(state, (static_cast<std::size_t>(y + dy) * kColumns) +
                                                static_cast<std::size_t>(x + dx)) == here;
                }
            }
            if (clear) {
                out.push_back({.x = x, .y = y, .shadowed = here == 1});
            }
        }
    }
    return out;
}

// The floor's clear-cut pixels against their expected colour: the drawn colour x `lit` where the sun reaches, x
// `shadowed` in the box's shadow (each held to [0, 1]). Counts the pixels of each and the wrong ones.
struct FloorCompared {
    int lit = 0;
    int shadowed = 0;
    int wrong = 0;
    double worst = 0.0;
};
FloorCompared compare_floor(const std::vector<uint8_t>& texels, std::span<const FloorPixel> pixels, double lit,
                            double shadowed)
{
    FloorCompared out;
    const Rgb drawn = drawn_colour(core::at(kScene, 0).drawn);
    for (const FloorPixel& pixel : pixels) {
        const Rgb got = pixel_at(texels, pixel.x, pixel.y);
        const double factor = pixel.shadowed ? shadowed : lit;
        bool bad = false;
        for (std::size_t c = 0; c < 3; ++c) {
            // (lighting round A2: the light store holds linear light - its factor is linear as it is; the check's 8-bit
            // target holds a multiplying factor to 1 - brightening needs the float frame)
            const double want = std::clamp(core::at(drawn, c) * std::min(factor, 1.0), 0.0, 1.0);
            const double error = std::abs(core::at(got, c) - want) * kByteRange;
            out.worst = std::max(out.worst, error);
            bad = bad || error > kTolerance;
        }
        out.wrong += bad ? 1 : 0;
        out.lit += pixel.shadowed ? 0 : 1;
        out.shadowed += pixel.shadowed ? 1 : 0;
    }
    return out;
}

void run_counted_once(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("each light counted once on the relit world (the floor's light stream):\n");
    const SceneMesh& floor = meshes.front();
    render::GpuVertexBuffer* stream = nullptr;
    const Vec3 toward = normalised({core::at(kStreamSun, 0), core::at(kStreamSun, 1), core::at(kStreamSun, 2)});
    const std::array<float, 3> toward_sun = floats(toward);
    rt::set_sun_override(&toward_sun);
    render::vulkan::set_rt_light_settings({});
    render::vulkan::RtShadowSettings shadows;
    shadows.on = true;
    shadows.softness = 0.0F;
    shadows.rays = 1;
    render::vulkan::set_rt_shadow_settings(shadows);
    const std::vector<FloorPixel> pixels = floor_pixels(toward);
    // (the stream's bytes: the expected factors from the same rounding the shader reads)
    const auto light_of = [](double value) { return std::round(value * 128.0) / 128.0; };
    const double ambient = light_of(kStreamAmbient);
    const double direct = light_of(kStreamDirect);
    struct Run {
        const char* what;
        float seen;
        double lit;
        double shadowed;
    };
    const std::array<Run, 2> runs{{
        {"written in the box's shadow (seen 0): the shadow not put on again, the sunlit floor lit",
         0.0F, std::min(2.0, (ambient + direct) / ambient), 1.0},
        {"written in the sun (seen 1): the shadow takes the sun's share", 1.0F, 1.0, ambient / (ambient + direct)},
    }};
    for (const Run& run : runs) {
        const std::vector<rt::CelWords> words = floor_stream(floor.triangles.size(), run.seen, 0.0F);
        bool ok = set_floor_stream(gpu, floor, words, &stream);
        Targets targets;
        std::vector<uint8_t> texels;
        ok = ok && make_targets(gpu, 0, &targets);
        if (ok) {
            draw_frame(gpu, targets, meshes, {}, 1);
            draw_frame(gpu, targets, meshes, {}, 2); // (the stream's write before the geometry table's frame)
            ok = read_colour(gpu, targets, &texels);
        }
        release_targets(gpu, targets);
        const FloorCompared got = ok ? compare_floor(texels, pixels, run.lit, run.shadowed) : FloorCompared{};
        tally->check(ok && got.lit > 1000 && got.shadowed > 300 && got.wrong == 0,
                     text::format("%s: %d lit and %d shadowed floor pixels, %d wrong (worst %.2f of 255)", run.what,
                                  got.lit, got.shadowed, got.wrong, got.worst));
    }
    render::vulkan::set_rt_shadow_settings({});
    rt::set_sun_override(nullptr);
    // (lighting round A) the lamps: lit by their data on the relit floor (its albedo the texture's x the store's tint)
    // and on the box - no subtraction of a baked residual
    {
        render::vulkan::set_rt_light_settings(settings_for(true, 4));
        rt::set_level_emitters({});
        const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                                .colour = {1.0F, 0.9F, 0.7F},
                                                .position = floats(kLampAt),
                                                .radius = static_cast<float>(kLampReach)}}};
        const std::vector<rt::CelWords> words = floor_stream(floor.triangles.size(), 1.0F, 0.0F);
        bool ok = set_floor_stream(gpu, floor, words, &stream);
        Targets targets;
        std::vector<uint8_t> texels;
        ok = ok && make_targets(gpu, 0, &targets);
        if (ok) {
            draw_frame(gpu, targets, meshes, points, 1);
            draw_frame(gpu, targets, meshes, points, 2);
            ok = read_colour(gpu, targets, &texels);
        }
        release_targets(gpu, targets);
        int floor_changed = 0;
        int box_changed = 0;
        const View view = view_from(kEye);
        for (int y = kClearCut; ok && y < kRows - kClearCut; ++y) {
            for (int x = kClearCut; x < kColumns - kClearCut; ++x) {
                const Seen seen = seen_at(view, x, y);
                if (seen.solid == kNoSurface) {
                    continue;
                }
                const Rgb drawn = drawn_colour(core::at(kScene, seen.solid - 1).drawn);
                const Rgb got = pixel_at(texels, x, y);
                const bool changed = std::abs(core::at(got, 0) - core::at(drawn, 0)) * kByteRange > kTolerance;
                floor_changed += seen.solid == 1 && changed ? 1 : 0;
                box_changed += seen.solid == 2 && changed ? 1 : 0;
            }
        }
        tally->check(ok && floor_changed > 200 && box_changed > 200,
                     text::format("the lamp over the relit floor: %d floor pixels and %d of the box's lit by it",
                                  floor_changed, box_changed));
        render::vulkan::set_rt_light_settings({});
    }
    rt::ray_tracing()->set_light_stream(floor.id, nullptr);
    if (stream != nullptr) {
        gpu.release_vertex_buffer(stream);
    }
}

// --- (round O - the audit's O1) the occlusion never takes the direct light --------------------------------------------
// The box drawn as an object lit by the one light (rt::ObjectLight, linear - render_pc/pc_lights.h OneLightSent),
// rt_ao on (its rays find the floor round the box's foot): lit by its key light alone - the sun from (1, 1, 1), nothing
// of the indirect -, the faces toward it are as drawn with the occlusion on and off (the occlusion takes the indirect
// light's share: none); lit by the indirect light alone (the probe's c0), the same faces are darkened near the floor.
void run_ao_direct(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("the occlusion takes the indirect light alone (round O):\n");
    std::vector<SceneMesh> scene(meshes.begin(), meshes.end());
    scene.back().flags = rt::kObject | rt::kOpaque | rt::kCastsShadow; // (the box: an object)
    render::vulkan::set_rt_light_settings({});
    const double third = 1.0 / std::sqrt(3.0);
    const auto f = [](double value) { return static_cast<float>(value); };
    const rt::ObjectLight key_only{.ambient = {},
                                   .key = {1.0F, 1.0F, 1.0F},
                                   .key_direction = {f(-third), f(-third), f(-third)},
                                   .linear = true};
    const rt::ObjectLight indirect_only{.ambient = {1.0F, 1.0F, 1.0F}, .linear = true};
    constexpr float kReach = 40.0F;
    constexpr unsigned kRays = 16;
    std::array<std::vector<uint8_t>, 3> shots;
    bool ok = true;
    for (std::size_t at = 0; at < shots.size() && ok; ++at) {
        if (at == 0) {
            render::vulkan::set_rt_ao_settings({});
        } else {
            render::vulkan::set_rt_ao_settings({.on = true, .radius = kReach, .rays = kRays, .denoise = false});
        }
        Targets targets;
        ok = make_targets(gpu, 0, &targets);
        const rt::ObjectLight* light = at == 2 ? &indirect_only : &key_only;
        ok = ok && draw_frame(gpu, targets, scene, {}, 1, false, light) == 0 &&
             draw_frame(gpu, targets, scene, {}, 2, false, light) == 0 && read_colour(gpu, targets, &core::at(shots, at));
        release_targets(gpu, targets);
    }
    render::vulkan::set_rt_ao_settings({});
    tally->check(ok, "three frames drawn and read (occlusion off; on with the key light alone; on with the indirect alone)");
    if (!ok) {
        return;
    }
    const View view = view_from(kEye);
    const Vec3 toward{third, third, third};
    const uint32_t kBoxDrawn = core::at(kScene, 1).drawn;
    int lit_faces = 0;
    int changed_direct = 0;
    int darkened_indirect = 0;
    for (int y = 0; y < kTargetHeight; ++y) {
        for (int x = 0; x < kTargetWidth; ++x) {
            const Seen here = seen_at(view, x + 0.5, y + 0.5);
            if (here.solid != 2 || dot(here.normal, toward) < 0.3) {
                continue;
            }
            const Rgb off = pixel_at(core::at(shots, 0), x, y);
            // (the box's own drawn colour there: not a pixel of its edge the rasteriser gave the floor)
            const Rgb box = {static_cast<double>((kBoxDrawn >> 16) & 0xFFU) / kByteRange,
                             static_cast<double>((kBoxDrawn >> 8) & 0xFFU) / kByteRange,
                             static_cast<double>(kBoxDrawn & 0xFFU) / kByteRange};
            if (std::abs(core::at(off, 0) - core::at(box, 0)) > 1.5 / kByteRange ||
                std::abs(core::at(off, 2) - core::at(box, 2)) > 1.5 / kByteRange) {
                continue;
            }
            ++lit_faces;
            const Rgb direct = pixel_at(core::at(shots, 1), x, y);
            const Rgb indirect = pixel_at(core::at(shots, 2), x, y);
            constexpr double kStep = 1.5 / kByteRange;
            bool same = true;
            bool darker = false;
            for (std::size_t ch = 0; ch < 3; ++ch) {
                same = same && std::abs(core::at(direct, ch) - core::at(off, ch)) <= kStep;
                darker = darker || core::at(indirect, ch) < core::at(off, ch) - kStep;
            }
            changed_direct += same ? 0 : 1;
            darkened_indirect += darker ? 1 : 0;
        }
    }
    tally->check(lit_faces > 500 && changed_direct == 0 && darkened_indirect > 100,
                 text::format("the box's faces toward its key light: %d pixels - lit by the key alone %d changed by the "
                              "occlusion (0); lit by the indirect alone %d darkened",
                              lit_faces, changed_direct, darkened_indirect));
}

void run_many(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    constexpr int kSide = 7;
    constexpr double kSpacing = 45.0;
    constexpr double kMiddle = 3.0;
    constexpr double kHeight = 40.0;
    constexpr double kReach = 300.0;
    text::print("many lamps (%d point lights over the floor):\n", kSide * kSide);
    render::vulkan::RtLightSettings settings = settings_for(true, 4);
    settings.max_lights = kSide * kSide;
    render::vulkan::set_rt_light_settings(settings);
    rt::set_level_emitters({});
    std::vector<rt::Light> points;
    std::vector<Lamp> lamps;
    for (int i = 0; i < kSide * kSide; ++i) {
        const Vec3 at{((i % kSide) - kMiddle) * kSpacing, kHeight, ((i / kSide) - kMiddle) * kSpacing};
        points.push_back({.kind = rt::Light::Kind::kPoint, .colour = {1.0F, 0.9F, 0.7F}, .position = floats(at),
                          .radius = static_cast<float>(kReach)});
        lamps.push_back(point_lamp(at, {1.0, 0.9, 0.7}, kReach));
    }
    uint64_t passes = 0;
    std::vector<uint8_t> texels;
    const Compared got = run_case(gpu, meshes, 0, points, lamps, 4, &passes, &texels);
    tally->check(passes == 1 && got.clear_cut > 1000 && got.wrong == 0,
                 text::format("%d clear-cut pixels as the processor's, %d wrong (worst %.2f of 255)", got.clear_cut,
                              got.wrong, got.worst));
    // the floor's brightest raw sum (the reference's)
    const View view = view_from(kEye);
    double most_raw = 0.0;
    int white = 0;
    int floor = 0;
    for (int y = 0; y < kRows && !texels.empty(); ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const Seen seen = seen_at(view, x, y);
            if (seen.solid != 1 || y_of(seen.normal) < 0.5) {
                continue;
            }
            most_raw = std::max(most_raw, answer_at(seen, lamps, 4).raw);
        }
    }
    tally->check(most_raw > 1.0,
                 text::format("their summed light reaches past 1 (%.2f) - drawn as it is, no cap", most_raw));
    // at the default strength (rt_lights_strength): no floor pixel burnt white
    settings.strength = render::vulkan::kRtLightsStrength;
    render::vulkan::set_rt_light_settings(settings);
    Targets targets;
    if (make_targets(gpu, 0, &targets)) {
        passes = draw_frame(gpu, targets, meshes, points, 1);
        if (!read_colour(gpu, targets, &texels)) {
            texels.clear();
        }
    }
    release_targets(gpu, targets);
    for (int y = 0; y < kRows && !texels.empty(); ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const Seen seen = seen_at(view, x, y);
            if (seen.solid != 1 || y_of(seen.normal) < 0.5) {
                continue;
            }
            ++floor;
            const Rgb got_colour = pixel_at(texels, x, y);
            white += std::ranges::any_of(got_colour, [](double c) { return c * kByteRange >= 250.0; }) ? 1 : 0;
        }
    }
    // (lighting round A: no knee - 49 lamps this close light the floor past white on an 8-bit target, as their data
    // says; the HDR tone map is what compresses it. Reported, not judged.)
    tally->check(passes == 1 && floor > 1000,
                 text::format("at the default strength (%.2f) the floor drawn: %d of %d pixels at 250 or more (no cap)",
                              static_cast<double>(render::vulkan::kRtLightsStrength), white, floor));
}

// --perf: frames with kPerfLamps lamps over the floor (all in view), the pass's own timing line (vulkan_rt_lights.cpp:
// every 600 passes) printed through the log.
void run_perf(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    constexpr int kPerfFrames = 700; // (some timings are lost when their frame hasn't finished: 600 measured)
    constexpr int kPerfLamps = 64;
    constexpr int kPerfSide = 8;
    constexpr double kPerfSpacing = 36.0;
    constexpr double kPerfMiddle = 3.5;
    text::print("timing: %d frames, %d lamps in view, 4 traced a pixel (%ux%u):\n", kPerfFrames, kPerfLamps,
                kTargetWidth, kTargetHeight);
    render::vulkan::RtLightSettings settings = settings_for(true, 4);
    settings.max_lights = kPerfLamps;
    render::vulkan::set_rt_light_settings(settings);
    std::vector<rt::Light> points;
    for (int i = 0; i < kPerfLamps; ++i) {
        const double x = ((i % kPerfSide) - kPerfMiddle) * kPerfSpacing;
        const double z = ((i / kPerfSide) - kPerfMiddle) * kPerfSpacing;
        points.push_back({.kind = rt::Light::Kind::kPoint,
                          .colour = {0.5F, 0.45F, 0.35F},
                          .position = {static_cast<float>(x), 35.0F, static_cast<float>(z)},
                          .radius = 250.0F});
    }
    Targets targets;
    uint64_t passes = 0;
    if (make_targets(gpu, 0, &targets)) {
        for (int frame = 0; frame < kPerfFrames; ++frame) {
            passes += draw_frame(gpu, targets, meshes, points, static_cast<uint64_t>(frame) + 1);
        }
    }
    release_targets(gpu, targets);
    tally->check(passes == kPerfFrames, text::format("%llu passes timed", static_cast<unsigned long long>(passes)));
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
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
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

int run(const Options& options, SDL_Window* window, Tally* tally)
{
    const Opened opened = open_device(options, window);
    if (opened == Opened::kNoRayQueries) {
        return 2;
    }
    if (opened == Opened::kNoDevice) {
        return render::vulkan::loader_version() == 0 ? 2 : 1;
    }
    render::Gpu& gpu = render::vulkan::gpu();
    std::array<SceneMesh, kScene.size()> meshes{};
    bool made = true;
    for (std::size_t i = 0; i < kScene.size(); ++i) {
        made = made && make_solid(gpu, core::at(kScene, i), &core::at(meshes, i));
    }
    render::GpuTexture* red_texture = nullptr;
    rt::MaterialId red = 0;
    made = made && make_red_material(gpu, &red_texture, &red);
    tally->check(made, "the scene made (a floor, a box, their materials, the red glow's)");
    if (made) {
        constexpr uint32_t kFourSamples = 4;
        run_point(gpu, meshes, 0, tally);
        run_point(gpu, meshes, kFourSamples, tally);
        run_emitter(gpu, meshes, red, tally);
        run_two(gpu, meshes, tally);
        run_cutout(gpu, meshes, tally); // (added for the cut-outs)
        run_off(gpu, meshes, tally);
        run_time_of_day(gpu, meshes, tally); // (added by the time of day)
        run_many(gpu, meshes, tally);        // (added for the washed-out streets)
        run_counted_once(gpu, meshes, tally); // (added by the light energy audit)
        run_ao_direct(gpu, meshes, tally);    // (round O)
        run_fog_lamp(gpu, meshes, tally);     // (added by the volumetric light)
        run_fog_lamp_night(gpu, meshes, tally); // (added by the street air)
        if (options.perf) {
            run_perf(gpu, meshes, tally);
        }
    }
    for (SceneMesh& mesh : meshes) {
        release_solid(gpu, &mesh);
    }
    if (red != 0) {
        rt::ray_tracing()->remove_material(red);
    }
    if (red_texture != nullptr) {
        gpu.release_texture(red_texture);
    }
    render::vulkan::set_rt_light_settings({});
    rt::set_level_emitters({});
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::finish_work(state);
    }
    render::vulkan::gpu_context().shutdown();
    return -1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    set_log_listener(print_log_line);
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rt_lights_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    Tally tally;
    check_sources(&tally);
    check_shapes(&tally); // (lighting round A2)
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
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan ray-traced lights check", kWindowWidth, kWindowHeight,
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
        result = run(options, window, &tally);
    } catch (const std::exception& error) {
        text::print("the check stopped: %s\n", error.what());
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (result >= 0) {
        return result;
    }
    tally.check(validation_messages() == 0, text::format("validation messages: %d", validation_messages()));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}
