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
//   - (round SHEETS) a strip 1 - 2 pixels wide (a thin post, a lamp in front of it): lit by its own face (the camera
//     ray's triangle - vulkan_rt_scene.h camera_face), not by the floor's round it; (round PANES) the same where the
//     rays' drawn rule leaves the post out (a cut-out for the rays, alpha 0.38 under its 0.5) - the depth's triangle;
//   - (added for the cut-outs) the box made an alpha-tested caster: its texels' alpha 0 - the lamp's light goes through
//     it (no floor shadowed, as without it); alpha 255 - shadowed as the solid box;
//   - rt_lights off, or no lamp in view: every pixel exactly as drawn (no pass);
//   - many lamps (49 over the floor): as the processor's, their sum held under the knee, no floor pixel burnt white;
//   - (added by the volumetric light) the lamp in the air: rt_fog's haze (no level fog, no sun) lit by the lamps' set -
//     after still frames every half-resolution texel's light against the processor's march of the lamp's light through
//     the haze (its phase, its falloff, the box's shadow): texels whose rays cross the box's shadow behind it as dark as
//     the shadowed march says and darker than the unshadowed one (the lamp's cone of light in the air cut by the box),
//     the lit ones as the march says;
//   - (round NK) the night air's grain: 16 lamps in the misty street air at night, the camera still - the history's
//     frame-to-frame deviation at most a 30-frame average's of the marches, its mean the marches'.
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
#include <memory>
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
#include "engine/gfx/light_cards.h"
#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_emission_map.h"
#include "engine/gfx/texel_decode.h"
#include "engine/gfx/rt_light_list.h"
#include "engine/gfx/rt_material_table.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_surface_class.h"
#include "engine/gfx/rt_volume_light.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_front_layer.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_fog.h"
#include "gpu_vulkan/vulkan_rt_lights.h"
#include "gpu_vulkan/vulkan_rt_shadows.h"
#include "gpu_vulkan/vulkan_surface_light.h"
#include "platform/run_log.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
namespace rt = render::rt;
namespace brdf = render::rt::brdf;
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

// (round light cards - engine/gfx/light_cards.h) The original's light cards laid on surfaces: a floor (a 4 x 4 grid of
// vertices, 400 units across, its texture mid grey) under a card 0.5 units over it lit by the card's light over its texture;
// the same card 20 units up - no surface's; a card standing up on it - not parallel; a card on a wall whose texture is all
// saturated (a lamp's luminous core) - none of it on the wall, but all of it on a floor. The colour word with light in it.
// The cone fit: a frustum's walls hanging from a lamp, against a flat card. No cap on the emitters (5000 lights).
void check_light_cards(Tally* tally)
{
    const auto floor_mesh = [](float y, const std::array<float, 3>& normal) {
        rt::RelitMesh mesh;
        mesh.id = 1;
        constexpr int kSide = 4;
        for (int i = 0; i < kSide; ++i) {
            for (int j = 0; j < kSide; ++j) {
                const float a = -200.0F + (static_cast<float>(i) * 400.0F / (kSide - 1));
                const float b = -200.0F + (static_cast<float>(j) * 400.0F / (kSide - 1));
                mesh.positions.push_back(normal[1] > 0.5F ? std::array<float, 3>{a, y, b} : std::array<float, 3>{a, b, y});
                mesh.normals.push_back(normal);
                mesh.colours.push_back(0xFF202020U);
            }
        }
        for (uint32_t i = 0; i + 1 < kSide; ++i) {
            for (uint32_t j = 0; j + 1 < kSide; ++j) {
                const uint32_t v = (i * kSide) + j;
                mesh.triangles.push_back({v, v + 1, v + kSide});
                mesh.triangles.push_back({v + 1, v + kSide + 1, v + kSide});
            }
        }
        return mesh;
    };
    const auto card_over = [](float lift, bool wall, uint32_t image) {
        rt::LightCards cards;
        const auto corner = [&](float a, float b) {
            return wall ? std::array<float, 3>{a, b, lift} : std::array<float, 3>{a, lift, b};
        };
        const std::array<float, 3> colour{0.5F, 0.5F, 0.5F};
        for (const auto& [p, q, r] : {std::array{std::array{-300.0F, -300.0F}, std::array{300.0F, -300.0F}, std::array{300.0F, 300.0F}},
                                      std::array{std::array{-300.0F, -300.0F}, std::array{300.0F, 300.0F}, std::array{-300.0F, 300.0F}}}) {
            cards.triangles.push_back({.corners = {corner(p[0], p[1]), corner(q[0], q[1]), corner(r[0], r[1])},
                                       .uvs = {std::array{0.0F, 0.0F}, std::array{1.0F, 0.0F}, std::array{1.0F, 1.0F}},
                                       .colours = {colour, colour, colour},
                                       .image = image,
                                       .mesh = 2});
        }
        auto grey = std::make_shared<render::texel::Image>();
        grey->width = 1;
        grey->height = 1;
        grey->texels = {{128, 128, 128, 255}};
        auto white = std::make_shared<render::texel::Image>();
        white->width = 1;
        white->height = 1;
        white->texels = {{255, 255, 255, 255}};
        cards.images = {grey, white};
        cards.surface_colours = {{1, {0.5F, 0.5F, 0.5F}}};
        return cards;
    };
    const std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    const std::array<float, 3> across{0.0F, 0.0F, 1.0F};
    const auto added_on = [&](const rt::RelitMesh& mesh, const rt::LightCards& cards, rt::CardReport* report) {
        std::vector<rt::RelitVertices> light(1);
        rt::relit_normals(mesh, 1.0F, &light.front());
        const std::vector<uint8_t> card_of{0};
        const std::vector<std::vector<rt::Rgbf>> added =
            rt::card_light_on_surfaces(std::span(&mesh, 1), light, card_of, cards, 1.0F, report);
        double sum = 0.0;
        std::size_t count = 0;
        if (!added.empty()) {
            for (const rt::Rgbf& light_here : added.front()) {
                sum += light_here[1];
                ++count;
            }
        }
        return count > 0 ? sum / static_cast<double>(count) : 0.0;
    };
    rt::CardReport report;
    const rt::RelitMesh ground = floor_mesh(0.0F, up);
    // the card's light 128/255 x 0.5 = 0.251 over the texture's 0.5: 0.502 of a byte of 128 at every vertex
    const double on = added_on(ground, card_over(0.5F, false, 0), &report);
    tally->check(std::abs(on - (128.0 / 255.0)) < 0.01 && report.vertices == ground.positions.size() &&
                     report.on_surface == 2,
                 text::format("a card laid on a floor: its light over the floor's texture at each vertex (%.3f of %.3f, "
                              "%zu vertices, %zu triangles)",
                              on, 128.0 / 255.0, report.vertices, report.on_surface));
    const double lifted = added_on(ground, card_over(20.0F, false, 0), &report);
    tally->check(lifted == 0.0 && report.vertices == 0, "a card 20 units over the floor: no surface's light");
    const rt::RelitMesh wall = floor_mesh(0.0F, across);
    const double standing = added_on(ground, card_over(0.0F, true, 0), &report);
    tally->check(standing == 0.0, "a card standing on the floor (not parallel): no light on it");
    const double core_on_wall = added_on(wall, card_over(0.5F, true, 1), &report);
    const double core_on_floor = added_on(ground, card_over(0.5F, false, 1), &report);
    tally->check(core_on_wall == 0.0 && core_on_floor > 0.9,
                 text::format("a card's saturated core: the lamp on a wall (%.3f), a light pool on a floor (%.3f)",
                              core_on_wall, core_on_floor));
    tally->check(rt::colour_with_light(0xFF102030U, {0.5F, 0.25F, 4.0F}) == 0xFF5040FFU,
                 "a colour word with light in it: 64 and 32 more, held at 255");
    // a four-sided frustum hanging from a lamp 10 units over its top (radius 10 there, 60 at its foot 200 below)
    rt::RelitMesh frustum;
    for (int k = 0; k < 4; ++k) {
        const float angle = static_cast<float>(k) * 1.5707963F;
        frustum.positions.push_back({10.0F * std::cos(angle), -10.0F, 10.0F * std::sin(angle)});
        frustum.positions.push_back({60.0F * std::cos(angle), -210.0F, 60.0F * std::sin(angle)});
    }
    for (uint32_t k = 0; k < 4; ++k) {
        const uint32_t top = 2 * k;
        const uint32_t next = 2 * ((k + 1) % 4);
        frustum.triangles.push_back({top, top + 1, next});
        frustum.triangles.push_back({next, top + 1, next + 1});
    }
    const rt::ConeFit cone = rt::cone_fit(frustum, {0.0F, 0.0F, 0.0F});
    rt::RelitMesh flat = ground;
    const rt::ConeFit card = rt::cone_fit(flat, {0.0F, 300.0F, 0.0F});
    tally->check(cone.fitted && cone.from > 0.0F && cone.radius_far > 1.5F * cone.radius_near && cone.residual < 0.05F &&
                     cone.gap_degrees <= 91.0F && cone.walls > 0.99F && (card.walls < 0.5F || card.gap_degrees > 120.0F),
                 text::format("a frustum hanging from a lamp: along %.0f - %.0f, radius %.0f - %.0f, misfit %.3f, gap %.0f, "
                              "walls %.2f; a flat card under a lamp: walls %.2f",
                              static_cast<double>(cone.from), static_cast<double>(cone.to),
                              static_cast<double>(cone.radius_near), static_cast<double>(cone.radius_far),
                              static_cast<double>(cone.residual), static_cast<double>(cone.gap_degrees),
                              static_cast<double>(cone.walls), static_cast<double>(card.walls)));
    std::vector<rt::EmitterTriangle> many;
    for (int i = 0; i < 5000; ++i) {
        add_quad(&many, {static_cast<double>(i % 100) * 500.0, 300.0, static_cast<double>(i / 100) * 500.0}, {8.0, 0.0, 0.0},
                 {0.0, 8.0, 0.0}, rt::EmitterKind::kSelfLit, {1.0F, 1.0F, 1.0F});
    }
    tally->check(rt::cluster_emitters(many).size() == 5000,
                 text::format("no cap on a level's lights: 5000 lamps far apart, %zu lights", rt::cluster_emitters(many).size()));
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
    {
        // (round FG - one lamp-texel test) the light store's code tells the shading the bake's own test: over a grid of
        // texels and medians the coded test refuses nothing the bake's takes, and takes over it only within the floor's
        // step (1/126 of the luminance); a pale facade's mortar and stone (bright, warm, its texture's median high) emit
        // nothing - the old shading's "brightest channel over 0.6 - 0.7" lit them at the lamp's luminance; a blended
        // surface's lamp texels only where drawn solid; code 0 - the whole surface
        std::size_t refused = 0;
        std::size_t over = 0;
        std::size_t tried = 0;
        constexpr int kSteps = 12;
        for (int m = 0; m <= kSteps; ++m) {
            const float median = 0.4F * static_cast<float>(m) / kSteps;
            const uint8_t code = rt::lamp_texel_code(rt::kTexelOverTexture * median, false);
            for (int r = 0; r <= kSteps; ++r) {
                for (int g = 0; g <= kSteps; ++g) {
                    for (int b = 0; b <= kSteps; ++b) {
                        const std::array<float, 3> linear{static_cast<float>(r) / kSteps, static_cast<float>(g) / kSteps,
                                                          static_cast<float>(b) / kSteps};
                        const bool bake = rt::lamp_texel(linear, median);
                        const bool shown = rt::lamp_texel_coded(linear, 1.0F, code);
                        ++tried;
                        refused += bake && !shown ? 1 : 0;
                        if (shown && !bake) {
                            const float luma = (0.2126F * linear[0]) + (0.7152F * linear[1]) + (0.0722F * linear[2]);
                            over += luma < rt::kTexelOverTexture * median - (1.0F / 126.0F) ? 1 : 0;
                        }
                    }
                }
            }
        }
        const uint8_t facade = rt::lamp_texel_code(rt::kTexelOverTexture * 0.35F, false);
        const uint8_t blended = rt::lamp_texel_code(0.0F, true);
        tally->check(refused == 0 && over == 0 && !rt::lamp_texel_coded({0.75F, 0.7F, 0.6F}, 1.0F, facade) &&
                         !rt::lamp_texel_coded({0.9F, 0.8F, 0.7F}, 1.0F, facade) &&
                         rt::lamp_texel_coded({1.0F, 0.95F, 0.9F}, 1.0F, facade) &&
                         rt::lamp_texel_coded({0.9F, 0.9F, 0.9F}, 0.75F, blended) &&
                         !rt::lamp_texel_coded({0.9F, 0.9F, 0.9F}, 0.2F, blended) &&
                         rt::lamp_texel_coded({0.1F, 0.1F, 0.1F}, 0.0F, 0U),
                     text::format("the shading's lamp texels are the bake's: %zu texels x medians, %zu the bake takes refused, "
                                  "%zu taken beyond the floor's step; a pale facade's stone and mortar dark, its lamp's "
                                  "white lit; a blended lamp's solid texels alone",
                                  tried, refused, over));
    }
    {
        // (round SUBPIX - one emission map) a panel of lamp dots - one lamp texel in 64 on a dark texture: its emission
        // map's every level holds level 0's mean (the flux kept at any distance), the 8 x 8 level a dot's whole emission
        // spread over each texel; the old rule - the lamp-texel test on the texel filtered to that level - emitted nothing
        // there. A blended surface's map: only the solid dots.
        constexpr uint32_t kSide = 64;
        constexpr uint32_t kEvery = 8;
        render::texel::Image panel;
        panel.width = kSide;
        panel.height = kSide;
        for (uint32_t y = 0; y < kSide; ++y) {
            for (uint32_t x = 0; x < kSide; ++x) {
                const bool dot = x % kEvery == 3 && y % kEvery == 5;
                const auto alpha = static_cast<uint8_t>(dot && x % (2 * kEvery) == 3 ? 255 : (dot ? 40 : 255));
                panel.texels.push_back(dot ? std::array<uint8_t, 4>{255, 250, 240, alpha} : std::array<uint8_t, 4>{40, 40, 40, 255});
            }
        }
        const float median = static_cast<float>(rt::linear_from_display(40.0 / 255.0));
        const uint8_t code = rt::lamp_texel_code(rt::kTexelOverTexture * median, false);
        const std::vector<rt::EmissionLevel> map = rt::emission_map(panel, code);
        const std::array<float, 3> mean = rt::emission_mean(map.front());
        double worst = 0.0;
        for (const rt::EmissionLevel& level : map) {
            const std::array<float, 3> at = rt::emission_mean(level);
            for (std::size_t k = 0; k < 3; ++k) {
                worst = std::max(worst, std::abs(static_cast<double>(at[k]) / static_cast<double>(mean[k]) - 1.0));
            }
        }
        const rt::EmissionLevel& eight = core::at(map, 3);
        double spread = 0.0;
        for (const std::array<float, 3>& texel : eight.texels) {
            spread = std::max(spread, std::abs(static_cast<double>(texel[1]) / static_cast<double>(mean[1]) - 1.0));
        }
        // the old rule at that level: the texels' display-referred mean (a mip of the game's texture) tested
        const float filtered = (63.0F * (40.0F / 255.0F) + (250.0F / 255.0F)) / 64.0F;
        const float lin = static_cast<float>(rt::linear_from_display(static_cast<double>(filtered)));
        const bool old_lit = rt::lamp_texel_coded({lin, lin, lin}, 1.0F, code);
        const std::array<float, 3> solid =
            rt::emission_mean(rt::emission_map(panel, rt::lamp_texel_code(rt::kTexelOverTexture * median, true)).front());
        const float dot_green = static_cast<float>(rt::linear_from_display(250.0 / 255.0));
        tally->check(map.size() == 7 && eight.width == kSide / kEvery && worst < 1e-5 && spread < 1e-5 && !old_lit &&
                         std::abs((mean[1] * static_cast<float>(kEvery * kEvery)) - dot_green) < 1e-4F &&
                         std::abs((solid[1] / mean[1]) - 0.5F) < 1e-4F,
                     text::format("an emission map keeps its flux at every level: %zu levels, each one's mean within %.2g of "
                                  "level 0's (%.5f), the 8 x 8 level's texels within %.2g of it - the old rule (the test on "
                                  "the filtered texel) there: %s; a blended surface's map %.3f of it (its solid dots)",
                                  map.size(), worst, static_cast<double>(mean[1]), spread, old_lit ? "lit" : "nothing",
                                  static_cast<double>(solid[1] / mean[1])));
        // (round SUBPIX) the split of a small emitter's directly seen light between the splat and the draws: whole at
        // most two pixels, none from eight, log-linear between (half at four), never above 1 or below 0
        tally->check(rt::emission_splat_share(0.01F) == 1.0F && rt::emission_splat_share(rt::kSplatWholeAtMost) == 1.0F &&
                         std::abs(rt::emission_splat_share(4.0F) - 0.5F) < 1e-6F &&
                         rt::emission_splat_share(rt::kSplatNoneAtLeast) == 0.0F && rt::emission_splat_share(1e6F) == 0.0F,
                     "a small emitter's splatted share: whole up to 2 drawn pixels, half at 4, none from 8");
    }
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
        // (the night colour round) the published state is pre-exposed: its sky's light over the pre-exposure is the
        // global unit's the switches are in - a night's sky, 65536 x in the state, still switches a street lamp on
        const float night_sky = 1.5e-6F;
        const float pre = 65536.0F;
        const float read = rt::lamp_sky_now(
            {.on = true, .sky_light = {night_sky * pre, night_sky * pre, night_sky * pre}, .pre_exposure = pre}, nullptr);
        tally->check(std::abs(read - night_sky) < 1e-9F && rt::lamp_on(street_switch, read, 0.0F) == 1.0F,
                     text::format("the lamps' sky now in the global unit, the pre-exposure divided out: %.3g (the "
                                  "state's %.3g), a street lamp on",
                                  static_cast<double>(read), static_cast<double>(night_sky * pre)));
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
    // (round SL) the game's own lights switched by the game: a night light the level's scripts turned on shines at the
    // brightness the game gives it whatever the photocell says (noon here), one it never turns on stays dark
    {
        constexpr uint32_t kOn = 0x51U;
        constexpr uint32_t kHalfOn = 0x52U;
        constexpr uint32_t kNeverOn = 0x53U;
        rt::FrameRecord game;
        game.game_lights.push_back({.owner = kOn, .strength = 1.0F});
        game.game_lights.push_back({.owner = kHalfOn, .strength = 0.5F});
        game.game_lights.push_back({.owner = kNeverOn, .strength = 0.0F});
        rt::LevelEmitters street;
        for (const uint32_t owner : {kOn, kHalfOn, kNeverOn}) {
            street.emitters.push_back({.position = {static_cast<float>(owner) - 82.0F, 0.0F, -300.0F},
                                       .size = 4.0F,
                                       .colour = {1.0F, 0.6F, 0.2F},
                                       .intensity = 2.0F,
                                       .reach = 400.0F,
                                       .kind = rt::EmitterKind::kPoint,
                                       .switch_sky = 0.01F, // (a photocell's: off under noon's sky)
                                       .game_light = owner});
        }
        rt::ViewLightRequest noon = request;
        noon.max = 8;
        noon.points = false;
        noon.emitters = true;
        noon.sky_now = 1.0F; // (far over the photocell's switch)
        noon.open_lamps = 0.0F;
        const std::vector<rt::Emitter> shining = rt::select_view_lights(game, &street, noon);
        float on = 0.0F;
        float half = 0.0F;
        bool never = false;
        for (const rt::Emitter& lamp : shining) {
            on = lamp.game_light == kOn ? lamp.intensity : on;
            half = lamp.game_light == kHalfOn ? lamp.intensity : half;
            never = never || lamp.game_light == kNeverOn;
        }
        tally->check(shining.size() == 2 && on == 2.0F && half == 1.0F && !never &&
                         rt::lamp_on(0.01F, 1.0F, 0.0F) < 0.01F,
                     text::format("the game's lights by the game's switch (%zu shining: on %.2f, half %.2f) - the "
                                  "photocell would have them off; one the game never turns on stays dark",
                                  shining.size(), static_cast<double>(on), static_cast<double>(half)));
    }
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

// (added for the cut-outs) The share of the lamps' light the box lets through in the reference: 0 solid, 1 while its
// material is cut out everywhere (its texels' alpha under the test's reference - the rays go through it); (round PT-B)
// between for a seen-through box (its coverage's complement).
double& box_through()
{
    static double through = 0.0;
    return through;
}

// The share of the lamp's light that reaches the surface (0: blocked).
double lamp_seen(const Seen& seen, const Lamp& lamp)
{
    const double offset = kOffsetScale * (1.0 + (seen.distance * 0.0005) + (seen.distance * seen.distance * 6e-8));
    const Vec3 origin = step(seen.point, seen.normal, offset);
    const Vec3 to = minus(lamp.position, origin);
    const double dist = length_of(to);
    const double stop = dist - lamp.size;
    if (stop <= 0.0) {
        return 1.0;
    }
    const Vec3 direction = normalised(to);
    double through = 1.0;
    for (std::size_t i = 0; i < kScene.size(); ++i) {
        const Hit hit = intersect(core::at(kScene, i), origin, direction);
        if (hit.distance <= stop) {
            // (a ray that meets the box crosses two of its sides, each letting its share through)
            through *= i == 1 ? box_through() * box_through() : 0.0;
        }
    }
    return through;
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
        double seen = 1.0; // (round PT-B: the share that gets through)
    };
    std::vector<Each> each;
    for (const Lamp& lamp : lamps) {
        const Vec3 to = minus(lamp.position, seen.point);
        const double dist = length_of(to);
        const double facing = dot(seen.normal, normalised(to));
        Each item;
        item.seen = lamp_seen(seen, lamp);
        item.blocked = item.seen < 1.0;
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
            const double seen_share = item.seen;
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

// (round DB) The camera draw_scene and view_drawn use: kEye, but for the decal layers' grazing view.
Vec3& scene_eye()
{
    static Vec3 eye = kEye;
    return eye;
}

void draw_scene(render::Gpu& gpu, std::span<const SceneMesh> meshes)
{
    using render::GpuRenderState;
    using render::GpuStageState;
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    const Matrices matrices = matrices_for(scene_eye());
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

// (round UNITS) The lamp-in-the-air case's level fog: the level's own fog (replaced by the pass - an even medium of
// Koschmieder's extinction for its distances), black - no light of its own -, from the eye to kFogHazeEnd. (Before, the
// case drew the haze - a 100-metre "fog" in shafts mode; a level without fog now has the weather's air alone.)
bool& lamp_fog()
{
    static bool on = false;
    return on;
}

rt::ViewDrawn view_drawn()
{
    const Matrices matrices = matrices_for(scene_eye());
    rt::ViewDrawn view;
    constexpr std::size_t kSide = 4;
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(view.camera.view, row), column) = core::at(matrices.view, (row * kSide) + column);
            core::at(core::at(view.camera.projection, row), column) =
                core::at(matrices.projection, (row * kSide) + column);
        }
    }
    view.camera.position = floats(scene_eye());
    view.rect = {0, 0, kTargetWidth, kTargetHeight};
    view.depth_range = {0.0F, 1.0F};
    if (lamp_fog()) {
        view.fog = {.on = true, .start = 0.0F, .end = render::vulkan::kFogHazeEnd, .colour = {0.0F, 0.0F, 0.0F}};
    }
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
    // (round GRIT: `auto` is ReSTIR DI now, with or without Ray Reconstruction - these cases are the lamps pass's)
    settings.direct = render::vulkan::RtDirectLight::kPasses;
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
            // (round SHEETS) A box drawn where the rays have none (cut out everywhere - the case's raster draws it whole):
            // where the camera ray, past it, meets the floor within the pass's match of the depth's point (vulkan_rt_scene.h
            // camera_face: 1 + 2 % of the distance - ReSTIR DI's primary's), the pass's surface there is the floor's, not
            // the box's the case drew - the scene contradicts itself there (the box's front face's lowest row, a unit over
            // the floor); not a clear-cut pixel
            if (clear && here.solid == 2 && box_through() >= 1.0) {
                const Vec3 ray = normalised(minus(here.point, view.eye));
                const Hit floor = intersect(core::at(kScene, 0), view.eye, ray);
                clear = !(std::abs(floor.distance - here.distance) <= 1.0 + (0.02 * here.distance));
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
                // (round PT-B: a seen-through box is no surface the pass's camera ray finds - its lamps' light is its
                // light store's, rt_relit_world lit_per_pixel: nothing added here)
                const bool unowned = here.solid == 2 && box_through() > 0.0 && box_through() < 1.0;
                const double added = unowned ? 0.0 : std::max(0.0, core::at(answer.added, c));
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

// (round SHEETS - vulkan_rt_scene.h camera_face: one normal for every pass that starts rays from the camera's surface)
// A thin post standing on the floor, 1 - 2 pixels wide on the screen, a lamp low in front of it: the post's face toward
// the camera faces the lamp, the floor round it (seen on both sides of the strip) faces up - away from a lamp below the
// post's middle. The lamps' light on the strip's pixels must be the processor's for the post's own face. Before, the pass
// took the normal from the neighbouring depths: on a strip one pixel wide both neighbours across it are the floor behind,
// so the post took a normal of the floor's (the pixels a lamp lights by its face got little or nothing - Training's glass
// box: the panes' frames, 1 - 2 pixels wide, lit by the background's normal).
const Solid kPost{.low = {59.6, 0.0, 59.6}, .high = {60.4, 50.0, 60.4}, .albedo = {0.7, 0.7, 0.7}, .drawn = 0xFF303030};
constexpr Vec3 kPostLampAt{60.0, 12.0, 150.0};
constexpr double kPostLampLight = 2.0;

// What a ray meets among the floor, the box and the post: the solid (1, 2 the scene's, 3 the post) and its face.
Seen seen_with_post(const View& view, double px, double py)
{
    Seen seen = seen_at(view, px, py);
    const double ndc_x = (2.0 * px / kTargetWidth) - 1.0;
    const double ndc_y = 1.0 - (2.0 * py / kTargetHeight);
    const Vec3 direction = normalised(
        step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height), view.back, -1.0));
    const Hit hit = intersect(kPost, view.eye, direction);
    if (hit.distance < (seen.solid == kNoSurface ? kNoHit : seen.distance)) {
        seen = {.solid = kScene.size() + 1, .normal = hit.normal, .point = step(view.eye, direction, hit.distance),
                .distance = hit.distance};
    }
    return seen;
}

// The lamp's light added on the post's surface at `seen` taken with `normal` (strength x linear albedo x light).
double post_added(const Seen& seen, const Vec3& normal, std::size_t channel)
{
    const Vec3 to = minus(kPostLampAt, seen.point);
    const double dist = length_of(to);
    const double facing = dot(normal, normalised(to));
    if (!(facing > 0.0)) {
        return 0.0;
    }
    constexpr double kReach = 400.0;
    const double light = kPostLampLight * falloff(dist, static_cast<double>(rt::kPointCoreShare) * kReach, kReach) * facing;
    return static_cast<double>(kStrength) * render::rt::linear_from_display(core::at(kPost.albedo, channel)) * light;
}

bool make_cutout_material(render::Gpu& gpu, uint32_t texel, const Rgb& albedo, render::GpuTexture** texture,
                          rt::MaterialId* material);

// (round PANES - vulkan_rt_scene.h depth_triangle) `thin_cover`: the post drawn by the raster as before but a cut-out for
// the rays whose texels' alpha is under its alpha test's reference - the rays' drawn rule leaves it out where the raster
// drew it (Training's glass box: a pane's border where the two decide differently). Its face is still the triangle the
// depth's point lies on (no opacity micromap in the check: the triangle is a candidate).
void run_strip(render::Gpu& gpu, std::span<const SceneMesh> scene, Tally* tally, bool thin_cover)
{
    if (thin_cover) {
        text::print("the strip where the rays' drawn rule leaves the post out (a cut-out, alpha 0.38 under 0.5):\n");
    } else {
        text::print("a strip one or two pixels wide (a thin post, a lamp in front of it):\n");
    }
    SceneMesh post;
    if (!make_solid(gpu, kPost, &post)) {
        tally->check(false, "the post made");
        release_solid(gpu, &post);
        return;
    }
    render::GpuTexture* cover_texture = nullptr;
    rt::MaterialId cover = 0;
    if (thin_cover) {
        constexpr uint32_t kThinWhite = 0x60FFFFFFU; // alpha 96: 0.376 - under the alpha test's reference, 0.5
        if (!make_cutout_material(gpu, kThinWhite, kPost.albedo, &cover_texture, &cover)) {
            tally->check(false, "the post's cut-out material made");
            release_solid(gpu, &post);
            return;
        }
        post.material = cover;
        post.flags = rt::kWorld | rt::kAlphaTested | rt::kCastsShadow;
    }
    std::vector<SceneMesh> meshes(scene.begin(), scene.end());
    meshes.push_back(post);
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
    rt::set_level_emitters({});
    const auto light = static_cast<float>(kPostLampLight);
    const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {light, light, light},
                                            .position = floats(kPostLampAt),
                                            .radius = 400.0F}}};
    Targets targets;
    std::vector<uint8_t> texels;
    uint64_t passes = 0;
    bool read = false;
    if (make_targets(gpu, 0, &targets)) {
        passes = draw_frame(gpu, targets, meshes, points, 1);
        read = read_colour(gpu, targets, &texels);
    }
    release_targets(gpu, targets);
    release_solid(gpu, &post);
    if (cover != 0) {
        rt::ray_tracing()->remove_material(cover);
        gpu.release_texture(cover_texture);
    }
    const View view = view_from(kEye);
    const Vec3 up{0.0, 1.0, 0.0};
    constexpr double kInside = 0.25; // (a pixel counts where its centre's neighbourhood is all on the post's face)
    int strip = 0;
    int widest = 0;
    int wrong = 0;
    int telling = 0; // (pixels where a normal of the floor's would be off by far more than the tolerance)
    double worst = 0.0;
    for (int y = 0; read && y < kRows; ++y) {
        int in_row = 0;
        for (int x = 0; x < kColumns; ++x) {
            const Seen here = seen_with_post(view, x, y);
            if (here.solid != kScene.size() + 1) {
                continue;
            }
            bool inside = true;
            for (const std::array<double, 2>& o : {std::array<double, 2>{-kInside, 0.0}, {kInside, 0.0}, {0.0, -kInside},
                                                   {0.0, kInside}}) {
                const Seen beside = seen_with_post(view, x + o[0], y + o[1]);
                inside = inside && beside.solid == here.solid && beside.normal == here.normal;
            }
            ++in_row;
            if (!inside) {
                continue;
            }
            ++strip;
            const Rgb drawn = drawn_colour(kPost.drawn);
            const Rgb got = pixel_at(texels, x, y);
            bool bad = false;
            bool tells = false;
            for (std::size_t c = 0; c < 3; ++c) {
                const double added = post_added(here, here.normal, c);
                const double expected = std::min(1.0, core::at(drawn, c) + added);
                const double error = std::abs(core::at(got, c) - expected) * kByteRange;
                worst = std::max(worst, error);
                bad = bad || error > kTolerance;
                tells = tells || std::abs(added - post_added(here, up, c)) * kByteRange > 4.0 * kTolerance;
            }
            wrong += bad ? 1 : 0;
            telling += tells ? 1 : 0;
        }
        widest = std::max(widest, in_row);
    }
    constexpr int kLeastStrip = 30;
    constexpr int kMostWide = 3;
    tally->check(read && passes == 1 && strip >= kLeastStrip && widest <= kMostWide && telling >= kLeastStrip,
                 text::format("the strip: %d pixels well on the post's face (at most %d across a row), %d of them where "
                              "the floor's normal would be off",
                              strip, widest, telling));
    tally->check(read && strip > 0 && wrong == 0,
                 text::format("the lamp's light on the strip by the post's own face: %d of %d wrong (worst %.2f of 255)",
                              wrong, strip, worst));
}

// (added for the cut-outs - the one lighting path's ray test, vulkan_rt_scene.h cutout_rays_glsl) The box made a cut-out
// caster (alpha tested, its texels white x its albedo): with every texel's alpha 0 the lamp's light goes through it - the
// floor it shadowed lit, as a scene without it -, with every texel's alpha 255 it shadows as the solid box did.
// A material of one texel colour (A8R8G8B8 `texel` everywhere) x `albedo`, its flags, blend and alpha reference from
// `given` - its texels' least alpha as the backend reads them (as render_pc's material_of sets it); `made`: the material
// set.
bool make_texture_material(render::Gpu& gpu, uint32_t texel, const Rgb& albedo, const rt::Material& given,
                           render::GpuTexture** texture, rt::MaterialId* material, rt::Material* made)
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
    rt::Material surface = given;
    surface.texture = *texture;
    surface.colour = {static_cast<float>(core::at(albedo, 0)), static_cast<float>(core::at(albedo, 1)),
                      static_cast<float>(core::at(albedo, 2)), 1.0F};
    const rt::TexelAlpha texels = rt::ray_tracing()->texture_alpha(*texture);
    surface.least_texel_alpha = texels.known ? static_cast<float>(texels.least) / 255.0F : 0.0F;
    rt::ray_tracing()->set_material(*material, surface);
    *made = surface;
    return true;
}

bool make_cutout_material(render::Gpu& gpu, uint32_t texel, const Rgb& albedo, render::GpuTexture** texture,
                          rt::MaterialId* material)
{
    rt::Material given;
    given.flags = rt::kAlphaTested;
    given.alpha_ref = 0.5F;
    rt::Material made;
    return make_texture_material(gpu, texel, albedo, given, texture, material, &made);
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
        box_through() = hole ? 1.0 : 0.0;
        uint64_t passes = 0;
        std::vector<uint8_t> texels;
        const Compared got = run_case(gpu, cut, 0, points, lamps, 4, &passes, &texels);
        box_through() = 0.0;
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
        const bool passed = probed.size() == probes.size() && core::at(probed, 0).traced && !core::at(probed, 0).hit;
        tally->check(hole ? (passed && at_box(1)) : (at_box(0) && at_box(1)),
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

// (round PT-B - one class of each surface for every ray, rt_surface_class.h) The box drawn alpha-blended (blend mode 5,
// its alpha the texel's): with every texel's alpha 255 it is opaque in effect - its instance an opaque one, shadowing
// the lamp's light as the solid box in the lamps pass and stopping the relit bake's probe rays; with every texel's alpha
// 128 it is seen through - the lamps pass's shadow rays and the bake's rays take what it lets through, 1 - 128/255.
void run_blended(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("the box drawn alpha-blended (round PT-B):\n");
    render::vulkan::set_rt_light_settings(settings_for(true, 4));
    rt::set_level_emitters({});
    const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 0.9F, 0.7F},
                                            .position = floats(kLampAt),
                                            .radius = static_cast<float>(kLampReach)}}};
    const std::array<Lamp, 1> lamps{point_lamp(kLampAt, {1.0, 0.9, 0.7}, kLampReach)};
    constexpr uint32_t kOpaqueWhite = 0xFFFFFFFFU; // alpha 255: drawn opaque
    constexpr uint32_t kHalfWhite = 0x80FFFFFFU;   // alpha 128: half seen through
    constexpr uint32_t kBlendAlpha = 5;
    constexpr double kHalfAlpha = 128.0 / 255.0;
    for (const uint32_t texel : {kOpaqueWhite, kHalfWhite}) {
        const bool opaque = texel == kOpaqueWhite;
        render::GpuTexture* texture = nullptr;
        rt::MaterialId material = 0;
        rt::Material surface;
        surface.blend = kBlendAlpha;
        surface.flags = rt::kTransparent | rt::kTextureAlpha;
        surface.alpha_ref = 1.0F / 255.0F;
        if (!make_texture_material(gpu, texel, core::at(kScene, 1).albedo, surface, &texture, &material, &surface)) {
            tally->check(false, "the blended material made");
            return;
        }
        const rt::SurfaceClass kind = rt::surface_class(surface, 1.0F);
        tally->check(opaque ? kind == rt::SurfaceClass::kBlendedOpaque : kind == rt::SurfaceClass::kSeenThrough,
                     text::format("its texels' alpha %s: the backend reads their least %.3f, class \"%s\"",
                                  opaque ? "255" : "128", static_cast<double>(surface.least_texel_alpha),
                                  rt::surface_class_name(kind)));
        std::vector<SceneMesh> cut(meshes.begin(), meshes.end());
        core::at(cut, 1).material = material;
        core::at(cut, 1).flags = rt::kWorld | rt::surface_instance_flags(kind, surface);
        const double through = opaque ? 0.0 : 1.0 - kHalfAlpha;
        box_through() = through;
        uint64_t passes = 0;
        std::vector<uint8_t> texels;
        const Compared got = run_case(gpu, cut, 0, points, lamps, 4, &passes, &texels);
        box_through() = 0.0;
        // the relit bake's probe rays (the one lighting path's test): the opaque box stops them; through the seen-through
        // one they go on, with its share
        const std::array<rt::ProbeRequest, 1> probes{{
            {.origin = {-45.0F, 15.0F, 0.0F}, .direction = {1.0F, 0.0F, 0.0F}, .max_distance = 200.0F,
             .cull_mask = rt::kRelitWorldMask, .cutouts = true},
        }};
        std::vector<rt::ProbeResult> probed;
        rt::ray_tracing()->probe_many(probes, &probed);
        constexpr float kBoxSide = 35.0F; // (from x -45 to the box's side at -10)
        const bool answered = probed.size() == probes.size() && core::at(probed, 0).traced;
        const rt::ProbeResult first = answered ? core::at(probed, 0) : rt::ProbeResult{};
        if (opaque) {
            tally->check(answered && first.hit && std::abs(first.distance - kBoxSide) < 0.5F,
                         text::format("the bake's probe ray stops at the opaque-in-effect box (%s at %.2f)",
                                      first.hit ? "hit" : "no hit", static_cast<double>(first.distance)));
        } else {
            // (the box's two sides each pass 1 - a: the ray crosses both)
            const double expected = through * through;
            tally->check(answered && !first.hit && std::abs(first.seen_through - expected) < 0.01,
                         text::format("the bake's probe ray through the seen-through box: share %.4f (expected %.4f, "
                                      "two sides of 1 - 128/255)",
                                      static_cast<double>(first.seen_through), expected));
        }
        tally->check(passes == 1 && got.clear_cut > 5000 && got.wrong == 0 && got.shadowed_floor >= kMinShadowed,
                     text::format("its texels' alpha %s: the lamps pass's shadow rays take %.3f of the light through it "
                                  "- %d clear-cut pixels, %d wrong (worst %.2f of 255), %d floor pixels in its shadow",
                                  opaque ? "255" : "128", through, got.clear_cut, got.wrong, got.worst,
                                  got.shadowed_floor));
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
    // (round UNITS - vulkan_rt_fog.cpp: the medium the weather's aerosol - its albedo -, the lamp's estimate x 1 / 4,
    // in shafts mode its share)
    const std::array<float, 3> albedo = rt::aerosol_albedo(rt::aerosol());
    const double albedo_luma = (0.2126 * core::at(albedo, 0)) + (0.7152 * core::at(albedo, 1)) + (0.0722 * core::at(albedo, 2));
    // (the level's fog replaced - all of its light on a surface; the haze's shafts mode its share; the sky's pixels theirs)
    const double share = seen.solid == kNoSurface ? render::vulkan::kFogSkyShare
                                                  : (lamp_fog() ? 1.0 : render::vulkan::kFogShaftShare);
    const double medium = 0.25 * albedo_luma * share;
    const double air_medium = medium;
    // (a level without fog: the weather's street air alone - kFogHazeEnd the grid's reach)
    const double sigma = lamp_fog() ? rt::level_fog_extinction(0.0F, static_cast<float>(end)) : 0.0;
    AirLight out;
    double kept = 1.0;
    for (uint32_t i = 0; i < kFineSteps; ++i) {
        const double s0 = last * i / kFineSteps;
        const double s1 = last * (i + 1) / kFineSteps;
        const double s = 0.5 * (s0 + s1);
        const double haze = sigma * (s1 - s0);
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
    text::print("the lamp in the air (the level's fog lit by the light list's lamp, the box's shadow in it):\n");
    vk::set_rt_light_settings(settings_for(true, 4));
    // (round WF: the air is lit by the light list - the frame's point light there as the lighting state switches it: the
    // level's own lamps on, no clock)
    rt::LevelEmitters lamps_on;
    lamps_on.serial = 1;
    lamps_on.own_lamps = 1.0F;
    rt::set_level_emitters(std::move(lamps_on));
    // (round FW: lamps light the air only where the light is physical - a relit world with current data; its sun none)
    rt::set_cel_world({.relit = true, .sun = {0.0F, 0.0F, 0.0F}, .toward = {0.0F, 1.0F, 0.0F}});
    vk::RtFogSettings fog;
    fog.on = true;
    fog.haze = true;
    fog.scale = 2; // (the texels' 2 x 2 blocks below)
    vk::set_rt_fog_settings(fog);
    lamp_fog() = true; // (round UNITS: the level's fog, lit by the lamp; the clear day's street air with it, over the floor)
    rt::set_baked_sun({.scene = 1, .found = false, .ground = 0.0F, .ground_known = true});
    const rt::StreetAir mist = rt::street_air(rt::aerosol_for(rt::Weather::kClear), {});
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
    vk::RtFogLights lamps;
    bool read = false;
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        read = made && vk::read_rt_fog_image(state, {0, 0, kTargetWidth, kTargetHeight}, vk::RtFogImage::kHistory, &history);
        lamps = vk::rt_fog_lights_locked(state);
    }
    tally->check(read && lamps.lamps.size() == 1, text::format("  the history read back, the lamps' set (%zu)", lamps.lamps.size()));
    if (read && lamps.lamps.size() == 1) {
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
                const AirLight want =
                    air_light(view, (2.0 * x) + 0.5, (2.0 * y) + 0.5, lamps.lamps.front(), lamps.scale, mist);
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
    rt::set_level_emitters({});
    rt::set_baked_sun({});
    rt::set_cel_world({});
    lamp_fog() = false;
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
    vk::RtFogLights lamps;
    bool read = false;
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        read = vk::read_rt_fog_image(state, {0, 0, kTargetWidth, kTargetHeight}, vk::RtFogImage::kHistory, &history);
        lamps = vk::rt_fog_lights_locked(state);
    }
    release_targets(gpu, targets);
    if (!read || lamps.lamps.size() != 1) {
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
                air_light(view, (2.0 * x) + 0.5, (2.0 * y) + 0.5, lamps.lamps.front(), lamps.scale, *air_out).shadowed;
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
    // (round FW: lamps light the air only where the light is physical - a relit world with current data; its sun none)
    rt::set_cel_world({.relit = true, .sun = {0.0F, 0.0F, 0.0F}, .toward = {0.0F, 1.0F, 0.0F}});
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
    rt::set_cel_world({});
    vk::set_rt_fog_settings({});
}

// (round NK) The night air's grain: many lamps in the street air at night, the camera still - the fog's light resampled
// each frame from the list (RIS over 16 lamps, one shadow ray a step pair), so each frame's march (step 1) is noisy;
// the history (step 2) must average it as a still camera's exponential history does (NVIDIA NRD ReLAX's slow history,
// clamped to the fast history's neighbourhood, not to one noisy frame's) - its frame-to-frame noise down by the
// accumulation, its mean the march's (the clamp not eating the bright samples: no darkening). Before round NK the
// history was clamped to the noisy frame's own 3 x 3 (mean +- 1.5 deviations): the "TV static" of a dark street at
// +12 stops.
constexpr int kNoiseFrames = 32;
constexpr double kNoiseLeast = 1e-3; // a texel with at least this mean light (luma) counts

struct FogNoise {
    double noise = 0.0; // the frames' deviation over the texels' mean light (root mean square / mean)
    double mean = 0.0;  // the texels' mean light
};

FogNoise fog_noise(const std::vector<std::vector<double>>& frames)
{
    FogNoise out;
    if (frames.empty()) {
        return out;
    }
    const std::size_t texels = frames.front().size();
    double variance = 0.0;
    double mean_sum = 0.0;
    std::size_t counted = 0;
    for (std::size_t t = 0; t < texels; ++t) {
        double sum = 0.0;
        double squares = 0.0;
        for (const std::vector<double>& frame : frames) {
            sum += core::at(frame, t);
            squares += core::at(frame, t) * core::at(frame, t);
        }
        const double n = static_cast<double>(frames.size());
        const double mean = sum / n;
        if (mean < kNoiseLeast) {
            continue;
        }
        variance += std::max((squares / n) - (mean * mean), 0.0);
        mean_sum += mean;
        ++counted;
    }
    if (counted > 0 && mean_sum > 0.0) {
        out.noise = std::sqrt(variance / static_cast<double>(counted)) / (mean_sum / static_cast<double>(counted));
        out.mean = mean_sum / static_cast<double>(counted);
    }
    return out;
}

void run_fog_noise(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    namespace vk = render::vulkan;
    text::print("the night air's grain (16 lamps in the misty street air, the camera still):\n");
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
    day.sun = {0.0F, -0.5F, 0.866F};
    day.light_colour = {0.0F, 0.0F, 0.0F};
    rt::set_day_light(day);
    // (round FW: lamps light the air only where the light is physical - a relit world with current data; its sun none)
    rt::set_cel_world({.relit = true, .sun = {0.0F, 0.0F, 0.0F}, .toward = {0.0F, 1.0F, 0.0F}});
    vk::RtFogSettings fog;
    fog.on = true;
    fog.haze = true;
    fog.scale = 2;
    fog.weather = rt::Weather::kMisty;
    vk::set_rt_fog_settings(fog);
    // 16 lamps round the box, some behind it (their air shadowed), of different colours and strengths
    std::vector<rt::Light> points;
    constexpr int kSide = 4;
    for (int i = 0; i < kSide * kSide; ++i) {
        const double x = -90.0 + (60.0 * static_cast<double>(i % kSide));
        const double z = -90.0 + (60.0 * static_cast<double>(i / kSide));
        const float strength = 0.25F + (0.25F * static_cast<float>(i % 3));
        points.push_back({.kind = rt::Light::Kind::kPoint,
                          .colour = {strength, 0.9F * strength, 0.7F * strength},
                          .position = floats({x, 15.0 + (10.0 * static_cast<double>(i % 2)), z}),
                          .radius = static_cast<float>(kLampReach) * 0.5F});
    }
    Targets targets;
    const bool made = make_targets(gpu, 0, &targets);
    std::vector<std::vector<double>> raw;
    std::vector<std::vector<double>> history;
    bool read = made;
    for (int frame = 0; read && frame < kFogFrames + kNoiseFrames; ++frame) {
        draw_frame(gpu, targets, meshes, points, static_cast<uint64_t>(frame) + 1, true);
        if (frame < kFogFrames) {
            continue;
        }
        vk::RtFogTexels raw_texels;
        vk::RtFogTexels history_texels;
        {
            vk::Backend& state = vk::backend();
            const std::scoped_lock lock(state.mutex);
            read = vk::read_rt_fog_image(state, {0, 0, kTargetWidth, kTargetHeight}, vk::RtFogImage::kRaw, &raw_texels) &&
                   vk::read_rt_fog_image(state, {0, 0, kTargetWidth, kTargetHeight}, vk::RtFogImage::kHistory,
                                         &history_texels);
        }
        const auto lumas = [](const vk::RtFogTexels& texels) {
            std::vector<double> out;
            out.reserve(texels.texels.size());
            for (const std::array<float, 4>& t : texels.texels) {
                out.push_back(luma_of({core::at(t, 0), core::at(t, 1), core::at(t, 2)}));
            }
            return out;
        };
        raw.push_back(lumas(raw_texels));
        history.push_back(lumas(history_texels));
    }
    release_targets(gpu, targets);
    const FogNoise kept = fog_noise(history);
    // (round FOG - the froxels: several samples a froxel a frame and its reprojected history; before, each pixel's march
    // took one resampled light a step pair and a ReLAX-style history: 0.072 of its light here, far more in the game's
    // dark street - round-nk.md, round-fog.md)
    constexpr double kMostFogGrain = 0.02;
    tally->check(read && kept.mean > 0.0 && kept.noise <= kMostFogGrain,
                 text::format("  the frames' grain: the fog's light deviates %.4f of itself frame to frame (at most "
                              "%.2f; its mean %.4g)",
                              kept.noise, kMostFogGrain, kept.mean));
    rt::set_day_light({});
    rt::set_sun_relight({});
    rt::set_baked_sun({});
    rt::set_cel_world({});
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

// --- (round V) the world lit per surface: the inputs read by the surfaces' own shading -------------------------------
// gpu_vulkan/vulkan_surface_light.h: in a view of a relit level (rt::cel_world().relit) drawn into the linear frame, the
// world's draws are kept, their depth drawn first, the sun's visibility made from it, then the draws shaded with it.
// The floor drawn here with a blended first pass (alpha 1, depth written - New Jersey's roads), unlit: its colour lit
// already takes the inputs by shares (kLightShare: the direct light's share x the visibility + the rest). Checked:
//   - order: the floor drawn before the box and after it give the same picture, and the box's shadow on the floor
//     either way (the floor's own light x (1 - the direct share) in the shadow, x 1 in the sun) - before round V the
//     pass multiplied what was drawn before it, and a floor drawn after it had no shadow;
//   - glass (blended by its alpha, no depth written, 60 units over the floor) over the shadow: its own colour is not
//     darkened - the pixel is half the glass's light and half the shadowed floor's;
//   - a glow (added) over the shadow: its light added whole.
// The check reads the float frame (linear light) before the tone mapping.
constexpr double kShareDirect = 0.5;  // the lighting state's direct share made here
constexpr double kOverFloor = 60.0;   // the glass's and the glow's height
constexpr uint32_t kGlassColour = 0x8060A0E0; // (alpha 0.5)
constexpr uint32_t kGlowColour = 0xFF302010;
constexpr double kLinearTolerance = 0.004; // (half floats near 0.01 - 0.3: a few steps)

double half_to_double(uint16_t half)
{
    constexpr int kBias = 15;
    constexpr uint32_t kMantissa = 0x3FFU;
    const uint32_t exponent = (static_cast<uint32_t>(half) >> 10U) & 0x1FU;
    const uint32_t mantissa = static_cast<uint32_t>(half) & kMantissa;
    double value = exponent == 0 ? std::ldexp(static_cast<double>(mantissa), -24)
                                 : std::ldexp(static_cast<double>(mantissa | 0x400U), static_cast<int>(exponent) - kBias - 10);
    return (half & 0x8000U) != 0 ? -value : value;
}

Rgb linear_pixel_at(const std::vector<uint8_t>& texels, int x, int y)
{
    constexpr std::size_t kHalfPixelBytes = 8;
    const std::size_t at = ((static_cast<std::size_t>(y) * kTargetWidth) + static_cast<std::size_t>(x)) * kHalfPixelBytes;
    Rgb out{};
    for (std::size_t c = 0; c < 3; ++c) {
        const auto half = static_cast<uint16_t>(core::at(texels, at + (2 * c)) | (core::at(texels, at + (2 * c) + 1) << 8U));
        core::at(out, c) = half_to_double(half);
    }
    return out;
}

// (round SHEETS) the alpha channel of an RGBA16F image's pixel
double half_alpha_at(const std::vector<uint8_t>& texels, int x, int y)
{
    constexpr std::size_t kHalfPixelBytes = 8;
    const std::size_t at = (((static_cast<std::size_t>(y) * kTargetWidth) + static_cast<std::size_t>(x)) * kHalfPixelBytes) + 6;
    return half_to_double(static_cast<uint16_t>(core::at(texels, at) | (core::at(texels, at + 1) << 8U)));
}

Rgb linear_of(uint32_t argb)
{
    const Rgb display = drawn_colour(argb);
    return {rt::linear_from_display(core::at(display, 0)), rt::linear_from_display(core::at(display, 1)),
            rt::linear_from_display(core::at(display, 2))};
}

// A horizontal quad at `height` over x in [x0, x1], z in [z0, z1] (two triangles).
std::vector<ColourVertex> quad_at(double height, double x0, double x1, double z0, double z1, uint32_t colour)
{
    const auto f = [](double v) { return static_cast<float>(v); };
    const std::array<std::array<float, 3>, 4> corners{{{f(x0), f(height), f(z0)},
                                                       {f(x1), f(height), f(z0)},
                                                       {f(x1), f(height), f(z1)},
                                                       {f(x0), f(height), f(z1)}}};
    std::vector<ColourVertex> out;
    for (const std::size_t i : {0U, 1U, 2U, 0U, 2U, 3U}) {
        out.push_back({.position = core::at(corners, i), .colour = colour});
    }
    return out;
}

// Whether the eye's ray through the pixel crosses the quad at `height` over x in [x0, x1], z in [z0, z1].
bool through_quad(int x, int y, double height, double x0, double x1, double z0, double z1)
{
    const View view = view_from(kEye);
    const double ndc_x = (2.0 * (x + 0.5) / kTargetWidth) - 1.0;
    const double ndc_y = 1.0 - (2.0 * (y + 0.5) / kTargetHeight);
    const Vec3 direction = normalised(
        step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height), view.back, -1.0));
    if (y_of(direction) >= 0.0) {
        return false;
    }
    const double t = (height - y_of(kEye)) / y_of(direction);
    const Vec3 at = step(kEye, direction, t);
    return x_of(at) > x0 + 2.0 && x_of(at) < x1 - 2.0 && z_of(at) > z0 + 2.0 && z_of(at) < z1 - 2.0;
}

// (round SKIN) kCover*: a quad alpha-blended over the floor that WRITES depth (a character's laces, a fence's soft edge -
// engine/gfx/rt_coverage.h), its vertex alpha kCoverColour's at three heights
enum class Over : uint8_t { kNothing, kGlass, kGlow, kLitGlow, kCoverFull, kCoverHigh, kCoverLow };
constexpr uint32_t kCoverColour = 0x00C08040U; // (rgb; the alpha per case: 0xFF, 0xC0 - 0.75 -, 0x40 - 0.25)

// (round RG) A lit glow - an added quad drawn with the fixed-function light, normals and coordinates (a lit object's draw: lit per
// pixel in such a view), kLitGlowOver over the floor: well within the 1 % of its distance at which a fragment counts as
// the surface the inputs were made for.
constexpr double kLitGlowOver = 0.5;
struct NormalVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    uint32_t colour = 0;
    std::array<float, 2> uv{};
};
constexpr uint32_t kFvfColourNormal = 0x152; // XYZ | NORMAL | DIFFUSE | TEX1 (stage 0's coordinates: lit per pixel)
// Its program the game's single-texture material shader (texture x vertex colour x 4 - one the per-pixel lighting
// takes), its texture white.
constexpr std::string_view kLitGlowShader = "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\nmul_x4 r0.rgb,v0,r0\n";
struct LitGlowDraw {
    render::GpuTexture* texture = nullptr;
    render::GpuPixelShader* shader = nullptr;
};
LitGlowDraw& lit_glow_draw()
{
    static LitGlowDraw draw;
    return draw;
}

// (decals) A mark over the whole view - as the decals' pass lays one where rt_decals places it, at kDecals between the
// inputs and the shading: (round DB) the decals' layers (the deferred decal's DBuffer - vulkan_surface_light.h) set to
// `albedo` (albedo x a + rgb) and `layer` (roughness x a + r, F0 x a + g). The default: a darkening by kMark (no colour
// of its own, the material's roughness and F0 kept - a wet patch's kind).
constexpr float kMark = 0.5F;
class MarkListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kDecals; }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        const render::vulkan::SurfaceInputs* const inputs = render::vulkan::surface_inputs(state);
        if (inputs == nullptr || inputs->decals == nullptr || inputs->layer == nullptr) {
            return;
        }
        VkCommandBuffer commands = render::vulkan::outside_pass(state);
        const VkImageSubresourceRange all{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
        for (const auto& [image, value] : {std::pair{inputs->decals, albedo}, std::pair{inputs->layer, layer},
                                           std::pair{inputs->lamps, lamps}}) {
            if (image == inputs->lamps && std::get<3>(lamps) < 0.0F) {
                continue; // (the lamps input left as the passes wrote it)
            }
            render::vulkan::Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL, true);
            const VkClearColorValue clear{.float32 = {core::at(value, 0), core::at(value, 1), core::at(value, 2), core::at(value, 3)}};
            vkCmdClearColorImage(commands, image->image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &all);
            render::vulkan::Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL, true);
        }
        state.bound = render::vulkan::BoundState{};
        ++marked;
    }
    std::array<float, 4> albedo{0.0F, 0.0F, 0.0F, kMark};
    std::array<float, 4> layer{0.0F, 0.0F, 0.0F, 1.0F};
    // (round BIAS) the lamps input (a white diffuser's light: ReSTIR DI's, the lamps pass's) set too, unless w < 0
    std::array<float, 4> lamps{0.0F, 0.0F, 0.0F, -1.0F};
    int marked = 0;
};

// (round CENSUS) `textured_floor` non-zero: the floor drawn as an unlit surface without a light store - its own colours
// (this vertex colour), stage 0's white texture through the lit glow's material program (lit_glow_draw; its pass colour
// kNoStorePass), opaque - as the game draws an unlit prop or a world mesh the relit data has no entry for.
constexpr float kNoStorePass = 0.25F; // (c0: the material's colour - x 4 with the neutral v0 0.5: display 0.5)
struct TexturedVertex {
    std::array<float, 3> position{};
    uint32_t colour = 0;
    std::array<float, 2> uv{};
};
constexpr uint32_t kFvfColourTexture = 0x142; // XYZ | DIFFUSE | TEX1

// A frame of the view lit per surface: the floor (blended first pass) before or after the box, and what is over it.
bool draw_per_surface(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, bool floor_first,
                      Over over, uint64_t frame, std::vector<uint8_t>* texels, std::vector<uint8_t>* guides = nullptr,
                      std::vector<uint8_t>* layer = nullptr, uint32_t textured_floor = 0)
{
    using render::GpuRenderState;
    rt::FrameRecord& record = rt::frame_record();
    record = rt::FrameRecord{};
    record.frame = frame;
    constexpr std::array<std::array<float, 4>, 3> kIdentityRows{
        {{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
    for (const SceneMesh& mesh : meshes) {
        record.instances.push_back(
            {.mesh = mesh.id, .material = mesh.material, .flags = mesh.flags, .object_to_world = kIdentityRows});
    }
    gpu.begin_scene();
    rt::ray_tracing()->begin_frame(record);
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, 0xFF000000U, 1.0F, 0);
    rt::view_begins(view_drawn());
    rt::set_world_surfaces_drawing(true);
    draw_scene(gpu, {}); // (the states and the camera)
    const auto draw_solid = [&gpu](const Solid& solid, bool blended) {
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, blended ? 1U : 0U);
        gpu.set_render_state(GpuRenderState::kSrcBlend, value::kBlendSrcAlpha);
        gpu.set_render_state(GpuRenderState::kDestBlend, value::kBlendInvSrcAlpha);
        std::vector<ColourVertex> vertices;
        for (const std::array<float, 3>& position : triangles_of(solid)) {
            vertices.push_back({.position = position, .colour = solid.drawn});
        }
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(vertices.size() / 3),
                             vertices.data(), sizeof(ColourVertex));
    };
    const auto draw_floor = [&gpu, &draw_solid, textured_floor] {
        if (textured_floor == 0) {
            draw_solid(core::at(kScene, 0), true);
            return;
        }
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
        gpu.set_vertex_format(kFvfColourTexture);
        gpu.set_texture(0, lit_glow_draw().texture);
        gpu.set_pixel_shader(lit_glow_draw().shader);
        constexpr std::array<float, 4> kPass{kNoStorePass, kNoStorePass, kNoStorePass, 0.5F};
        gpu.set_pixel_constants(0, kPass.data(), 1);
        std::vector<TexturedVertex> quad;
        for (const ColourVertex& corner : quad_at(0.0, -kFloorHalf, kFloorHalf, -kFloorHalf, kFloorHalf, textured_floor)) {
            quad.push_back({.position = corner.position, .colour = corner.colour});
        }
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(quad.size() / 3), quad.data(),
                             sizeof(TexturedVertex));
        gpu.set_pixel_shader(nullptr);
        gpu.set_texture(0, nullptr);
        gpu.set_vertex_format(kFvfColour);
    };
    if (floor_first) {
        draw_floor();
        draw_solid(core::at(kScene, 1), false);
    } else {
        draw_solid(core::at(kScene, 1), false);
        draw_floor();
    }
    if (over == Over::kLitGlow) {
        gpu.set_render_state(GpuRenderState::kZWriteEnable, 0);
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 1);
        gpu.set_render_state(GpuRenderState::kSrcBlend, value::kBlendOne);
        gpu.set_render_state(GpuRenderState::kDestBlend, value::kBlendOne);
        gpu.set_render_state(GpuRenderState::kLighting, 1);
        gpu.set_render_state(GpuRenderState::kDiffuseMaterialSource, value::kSourceColour1);
        gpu.set_material({.diffuse = {1, 1, 1, 1}, .ambient = {}, .specular = {}, .emissive = {}, .power = 0});
        render::GpuLight light{};
        light.type = value::kLightDirectional;
        light.diffuse = {1, 1, 1, 1};
        light.direction = {0.0F, -1.0F, 0.0F};
        gpu.set_light(0, light);
        gpu.enable_light(0, true);
        gpu.set_vertex_format(kFvfColourNormal);
        gpu.set_texture(0, lit_glow_draw().texture);
        gpu.set_pixel_shader(lit_glow_draw().shader);
        constexpr std::array<float, 4> kWhite{0.5F, 0.5F, 0.5F, 0.5F}; // (c0: the pass colour, x 4 with v0 - the texel)
        gpu.set_pixel_constants(0, kWhite.data(), 1);
        std::vector<NormalVertex> quad;
        for (const ColourVertex& corner : quad_at(kLitGlowOver, -kFloorHalf, 0.0, 0.0, kFloorHalf, kGlowColour)) {
            quad.push_back({.position = corner.position, .normal = {0.0F, 1.0F, 0.0F}, .colour = corner.colour});
        }
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(quad.size() / 3), quad.data(),
                             sizeof(NormalVertex));
        gpu.set_pixel_shader(nullptr);
        gpu.set_texture(0, nullptr);
        gpu.enable_light(0, false);
        gpu.set_render_state(GpuRenderState::kLighting, 0);
        gpu.set_vertex_format(kFvfColour);
        gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    } else if (over == Over::kCoverFull || over == Over::kCoverHigh || over == Over::kCoverLow) {
        // (round SKIN) blended by its alpha, its depth written - as the game draws such a surface
        const uint32_t alpha = over == Over::kCoverFull ? 0xFFU : (over == Over::kCoverHigh ? 0xC0U : 0x40U);
        gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 1);
        gpu.set_render_state(GpuRenderState::kSrcBlend, value::kBlendSrcAlpha);
        gpu.set_render_state(GpuRenderState::kDestBlend, value::kBlendInvSrcAlpha);
        const std::vector<ColourVertex> quad =
            quad_at(kOverFloor, -kFloorHalf, 0.0, 0.0, kFloorHalf, (alpha << 24U) | kCoverColour);
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(quad.size() / 3), quad.data(),
                             sizeof(ColourVertex));
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    } else if (over != Over::kNothing) {
        const bool glass = over == Over::kGlass;
        gpu.set_render_state(GpuRenderState::kZWriteEnable, 0);
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 1);
        gpu.set_render_state(GpuRenderState::kSrcBlend, glass ? value::kBlendSrcAlpha : value::kBlendOne);
        gpu.set_render_state(GpuRenderState::kDestBlend, glass ? value::kBlendInvSrcAlpha : value::kBlendOne);
        const std::vector<ColourVertex> quad =
            quad_at(kOverFloor, -kFloorHalf, 0.0, 0.0, kFloorHalf, glass ? kGlassColour : kGlowColour);
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(quad.size() / 3), quad.data(),
                             sizeof(ColourVertex));
        gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    }
    rt::set_world_surfaces_drawing(false);
    rt::opaque_view_drawn(view_drawn());
    bool read = true;
    if (guides != nullptr) { // (Ray Reconstruction's guides as the surfaces wrote them)
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::Image* const image = render::vulkan::surface_light_guides(state);
        guides->resize(static_cast<std::size_t>(kTargetWidth) * kTargetHeight * 16); // (RGBA32UI)
        const bool finished = image != nullptr && render::vulkan::finish_work(state);
        const bool copied = finished && state.memory.read_image_level(image, 0, guides);
        if (!copied) {
            text::print("  (the guides: %s)\n", image == nullptr ? "none" : (finished ? "not read back" : "work not finished"));
        }
        read = read && copied;
    }
    if (layer != nullptr) { // (round RG: Ray Reconstruction's transparency layer as the transparencies drew it)
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::Image* const image = render::vulkan::surface_light_layer(state);
        layer->resize(static_cast<std::size_t>(kTargetWidth) * kTargetHeight * 8); // (RGBA16F)
        const bool finished = image != nullptr && render::vulkan::finish_work(state);
        const bool copied = finished && state.memory.read_image_level(image, 0, layer);
        if (!copied) {
            text::print("  (the layer: %s)\n", image == nullptr ? "none" : (finished ? "not read back" : "work not finished"));
        }
        read = read && copied;
    }
    read = read && render::vulkan::read_surface(targets.colour, texels); // (after the guides: it ends the frame's work)
    gpu.end_scene();
    gpu.present();
    return read && texels->size() == static_cast<std::size_t>(kTargetWidth) * kTargetHeight * 8;
}

void run_decal_layers(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally);
void run_front_layer(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally);
void run_overlay_layers(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally);

void run_per_surface(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("round V - the world lit per surface (its light inputs read by its own shading):\n");
    const Vec3 toward = normalised({core::at(kStreamSun, 0), core::at(kStreamSun, 1), core::at(kStreamSun, 2)});
    const std::array<float, 3> toward_sun = floats(toward);
    rt::set_sun_override(&toward_sun);
    render::vulkan::set_rt_light_settings({});
    render::vulkan::RtShadowSettings shadows;
    shadows.on = true;
    shadows.rays = 1;
    render::vulkan::set_rt_shadow_settings(shadows);
    render::vulkan::set_hdr_settings({.on = true});
    const bool hdr_before = rt::hdr_colour();
    rt::set_hdr_colour(true); // (the game's device: the linear frame - this check's device was made without it)
    rt::set_cel_world({.relit = true, .sun = {1.0F, 1.0F, 1.0F}, .toward = toward_sun});
    // the lighting state: the direct light's share on a level surface kShareDirect (sun x N.L over sun x N.L + sky)
    rt::DayLight day;
    day.on = true;
    day.light = {core::at(toward_sun, 0), core::at(toward_sun, 1), core::at(toward_sun, 2)};
    day.sun = day.light;
    const float up = core::at(toward_sun, 1);
    day.sun_light = {1.0F, 1.0F, 1.0F};
    day.sky_light = {up, up, up}; // (equal to the direct light on a level surface: a share of 1/2)
    rt::set_day_light(day);
    const std::vector<FloorPixel> pixels = floor_pixels(toward);
    const render::GpuSurfaceDesc colour{.width = kTargetWidth,
                                        .height = kTargetHeight,
                                        .format = value::kFormatA16B16G16R16F,
                                        .samples = 0,
                                        .quality = 0,
                                        .lockable_or_discard = true};
    render::GpuSurfaceDesc depth = colour;
    depth.format = value::kFormatD24S8;
    depth.lockable_or_discard = false;
    Targets targets;
    bool ok = gpu.create_render_target(colour, &targets.colour) == value::kResultOk &&
              gpu.create_depth_surface(depth, &targets.depth) == value::kResultOk;
    const render::vulkan::SurfaceLightStats before = render::vulkan::surface_light_stats();
    std::array<std::vector<uint8_t>, 5> shots; // floor first, box first, glass, glow, marked
    std::vector<uint8_t> guides;
    std::vector<uint8_t> lit_glow_shot;
    std::vector<uint8_t> lit_glow_guides; // (round RG)
    {
        rt::MaterialId glow_material = 0;
        rt::Material glow_made;
        ok = ok &&
             make_texture_material(gpu, 0xFFFFFFFFU, {1.0, 1.0, 1.0}, rt::Material{}, &lit_glow_draw().texture,
                                   &glow_material, &glow_made) &&
             gpu.create_pixel_shader(kLitGlowShader.data(), static_cast<uint32_t>(kLitGlowShader.size()),
                                     &lit_glow_draw().shader) == value::kResultOk;
    }
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::set_surface_light_guides_forced(true);
    }
    uint64_t frame = 1;
    for (int pass = 0; ok && pass < 2; ++pass) { // (the second time round: the TLAS of the frame before built)
        ok = draw_per_surface(gpu, targets, meshes, true, Over::kNothing, frame++, &core::at(shots, 0)) &&
             draw_per_surface(gpu, targets, meshes, false, Over::kNothing, frame++, &core::at(shots, 1)) &&
             draw_per_surface(gpu, targets, meshes, false, Over::kGlass, frame++, &core::at(shots, 2), &guides) &&
             draw_per_surface(gpu, targets, meshes, false, Over::kGlow, frame++, &core::at(shots, 3)) &&
             draw_per_surface(gpu, targets, meshes, false, Over::kLitGlow, frame++, &lit_glow_shot, &lit_glow_guides);
    }
    // a decal (a mark over the whole view: the decals' input x kMark) - a material's edit, lit with its surface
    MarkListener mark;
    rt::add_view_listener(&mark);
    ok = ok && draw_per_surface(gpu, targets, meshes, false, Over::kNothing, frame++, &core::at(shots, 4));
    rt::remove_view_listener(&mark);
    const render::vulkan::SurfaceLightStats after = render::vulkan::surface_light_stats();
    tally->check(ok && after.views > before.views && after.prepassed > before.prepassed,
                 text::format("the views lit per surface: %llu views, %llu draws kept, %llu prepassed, %llu shaded",
                              static_cast<unsigned long long>(after.views - before.views),
                              static_cast<unsigned long long>(after.kept - before.kept),
                              static_cast<unsigned long long>(after.prepassed - before.prepassed),
                              static_cast<unsigned long long>(after.shaded - before.shaded)));
    const Rgb floor_light = linear_of(core::at(kScene, 0).drawn);
    // order and the shadow
    int lit = 0;
    int shadowed = 0;
    int wrong = 0;
    int differ = 0;
    double worst = 0.0;
    for (const FloorPixel& pixel : pixels) {
        if (!ok) {
            break;
        }
        const Rgb first = linear_pixel_at(core::at(shots, 0), pixel.x, pixel.y);
        const Rgb second = linear_pixel_at(core::at(shots, 1), pixel.x, pixel.y);
        const double factor = pixel.shadowed ? 1.0 - kShareDirect : 1.0;
        bool bad = false;
        for (std::size_t c = 0; c < 3; ++c) {
            const double want = core::at(floor_light, c) * factor;
            const double error = std::max(std::abs(core::at(first, c) - want), std::abs(core::at(second, c) - want));
            worst = std::max(worst, error);
            bad = bad || error > kLinearTolerance;
            differ += std::abs(core::at(first, c) - core::at(second, c)) > 1e-4 ? 1 : 0;
        }
        wrong += bad ? 1 : 0;
        (pixel.shadowed ? shadowed : lit) += 1;
    }
    tally->check(ok && lit > 1000 && shadowed > 300 && wrong == 0 && differ == 0,
                 text::format("the floor drawn blended before and after the box: %d lit and %d shadowed pixels, %d off "
                              "the light model (worst %.4f), %d channels differing between the orders",
                              lit, shadowed, wrong, worst, differ));
    // the mark lit with the floor: its albedo x kMark, in the sun and in the shadow alike (before round V the decals
    // multiplied the colour after the light - the same for a diffuse floor; the check is that the mark reaches the
    // surface's shading through its inputs)
    int mark_pixels = 0;
    int mark_wrong = 0;
    double mark_worst = 0.0;
    for (const FloorPixel& pixel : pixels) {
        if (!ok || mark.marked == 0) {
            break;
        }
        const Rgb unmarked = linear_pixel_at(core::at(shots, 1), pixel.x, pixel.y);
        const Rgb got = linear_pixel_at(core::at(shots, 4), pixel.x, pixel.y);
        bool bad = false;
        for (std::size_t c = 0; c < 3; ++c) {
            const double error = std::abs(core::at(got, c) - (core::at(unmarked, c) * kMark));
            mark_worst = std::max(mark_worst, error);
            bad = bad || error > kLinearTolerance;
        }
        ++mark_pixels;
        mark_wrong += bad ? 1 : 0;
    }
    tally->check(ok && mark.marked > 0 && mark_pixels > 1000 && mark_wrong == 0,
                 text::format("a decal is lit with its surface (its albedo x the mark, sun and shadow): %d pixels, %d "
                              "wrong (worst %.4f)",
                              mark_pixels, mark_wrong, mark_worst));
    // glass and glow over the shadow: their own light untouched
    const Rgb glass = linear_of(kGlassColour);
    const Rgb glow = linear_of(kGlowColour);
    constexpr double kGlassAlpha = 128.0 / 255.0;
    int glass_pixels = 0;
    int glass_wrong = 0;
    int glow_pixels = 0;
    int glow_wrong = 0;
    double glass_worst = 0.0;
    double glow_worst = 0.0;
    for (const FloorPixel& pixel : pixels) {
        if (!ok || !pixel.shadowed) {
            continue;
        }
        const Rgb under = linear_pixel_at(core::at(shots, 1), pixel.x, pixel.y);
        if (!through_quad(pixel.x, pixel.y, kOverFloor, -kFloorHalf, 0.0, 0.0, kFloorHalf)) {
            continue;
        }
        {
            const Rgb got = linear_pixel_at(core::at(shots, 2), pixel.x, pixel.y);
            bool bad = false;
            for (std::size_t c = 0; c < 3; ++c) {
                const double want = (kGlassAlpha * core::at(glass, c)) + ((1.0 - kGlassAlpha) * core::at(under, c));
                glass_worst = std::max(glass_worst, std::abs(core::at(got, c) - want));
                bad = bad || std::abs(core::at(got, c) - want) > kLinearTolerance;
            }
            ++glass_pixels;
            glass_wrong += bad ? 1 : 0;
        }
        {
            const Rgb got = linear_pixel_at(core::at(shots, 3), pixel.x, pixel.y);
            bool bad = false;
            for (std::size_t c = 0; c < 3; ++c) {
                const double want = core::at(glow, c) + core::at(under, c);
                glow_worst = std::max(glow_worst, std::abs(core::at(got, c) - want));
                bad = bad || std::abs(core::at(got, c) - want) > kLinearTolerance;
            }
            ++glow_pixels;
            glow_wrong += bad ? 1 : 0;
        }
    }
    tally->check(ok && glass_pixels > 100 && glass_wrong == 0,
                 text::format("glass over the shadow keeps its own light: %d pixels, %d darkened (worst %.4f)",
                              glass_pixels, glass_wrong, glass_worst));
    tally->check(ok && glow_pixels > 100 && glow_wrong == 0,
                 text::format("a glow over the shadow adds its light whole: %d pixels, %d darkened (worst %.4f)",
                              glow_pixels, glow_wrong, glow_worst));
    // (round SKIN - engine/gfx/rt_coverage.h) a blended surface that writes depth, without a temporal resolve (the check's
    // view: the half threshold): at alpha 0.75 it is the surface wholly - the picture the same as at alpha 1 -, at 0.25 it
    // is not there - the floor under it shaded as without it. Before, the prepass kept its pixels at least half opaque and
    // its shaded draw was blended over them: at 0.75 a quarter of what the target held there (the floor never shaded under
    // it - here the clear's black; Training's skater: up to 10^4 x its light), at 0.25 drawn over the floor without light
    // inputs of its own.
    std::array<std::vector<uint8_t>, 3> cover_shots; // alpha 1, 0.75, 0.25
    ok = ok && draw_per_surface(gpu, targets, meshes, false, Over::kCoverFull, frame++, &core::at(cover_shots, 0)) &&
         draw_per_surface(gpu, targets, meshes, false, Over::kCoverHigh, frame++, &core::at(cover_shots, 1)) &&
         draw_per_surface(gpu, targets, meshes, false, Over::kCoverLow, frame++, &core::at(cover_shots, 2));
    int cover_pixels = 0;
    int cover_high_wrong = 0;
    int cover_low_wrong = 0;
    int cover_seen = 0;
    double cover_worst = 0.0;
    for (const FloorPixel& pixel : pixels) {
        if (!ok || !through_quad(pixel.x, pixel.y, kOverFloor, -kFloorHalf, 0.0, 0.0, kFloorHalf)) {
            continue;
        }
        const Rgb full = linear_pixel_at(core::at(cover_shots, 0), pixel.x, pixel.y);
        const Rgb high = linear_pixel_at(core::at(cover_shots, 1), pixel.x, pixel.y);
        const Rgb low = linear_pixel_at(core::at(cover_shots, 2), pixel.x, pixel.y);
        const Rgb floor_alone = linear_pixel_at(core::at(shots, 1), pixel.x, pixel.y);
        bool high_bad = false;
        bool low_bad = false;
        bool seen = false;
        for (std::size_t c = 0; c < 3; ++c) {
            cover_worst = std::max({cover_worst, std::abs(core::at(high, c) - core::at(full, c)),
                                    std::abs(core::at(low, c) - core::at(floor_alone, c))});
            high_bad = high_bad || std::abs(core::at(high, c) - core::at(full, c)) > kLinearTolerance;
            low_bad = low_bad || std::abs(core::at(low, c) - core::at(floor_alone, c)) > kLinearTolerance;
            seen = seen || std::abs(core::at(full, c) - core::at(floor_alone, c)) > 4.0 * kLinearTolerance;
        }
        ++cover_pixels;
        cover_high_wrong += high_bad ? 1 : 0;
        cover_low_wrong += low_bad ? 1 : 0;
        cover_seen += seen ? 1 : 0;
    }
    tally->check(ok && cover_pixels > 100 && cover_seen > cover_pixels / 2 && cover_high_wrong == 0 && cover_low_wrong == 0,
                 text::format("a blended surface writing depth (no temporal resolve: a half): %d pixels (%d where it differs "
                              "from the floor) - at alpha 0.75 the alpha-1 picture: %d wrong; at 0.25 the floor alone: %d "
                              "wrong (worst %.4f)",
                              cover_pixels, cover_seen, cover_high_wrong, cover_low_wrong, cover_worst));
    // Ray Reconstruction's guides (written as with it on): each floor pixel's albedo guide is the floor's own - a colour
    // lit already, its light not separated: the colour lit, as the guide was before round V - also under the glass,
    // which is in front of the surface the inputs were made for and writes none
    int guided = 0;
    int guide_wrong = 0;
    double guide_worst = 0.0;
    constexpr std::size_t kGuideBytes = 16;
    for (const FloorPixel& pixel : pixels) {
        if (!ok || guides.size() < static_cast<std::size_t>(kTargetWidth) * kTargetHeight * kGuideBytes) {
            break;
        }
        const std::size_t at =
            ((static_cast<std::size_t>(pixel.y) * kTargetWidth) + static_cast<std::size_t>(pixel.x)) * kGuideBytes;
        uint32_t albedo = 0;
        uint32_t marked = 0;
        std::memcpy(&albedo, &core::at(guides, at), sizeof(albedo));
        std::memcpy(&marked, &core::at(guides, at + 4), sizeof(marked));
        const Rgb under = linear_pixel_at(core::at(shots, 1), pixel.x, pixel.y);
        bool bad = marked == 0U;
        for (std::size_t c = 0; c < 3; ++c) {
            const double encoded = static_cast<double>((albedo >> (8U * c)) & 0xFFU) / kByteRange;
            const double error = std::abs(rt::linear_from_display(encoded) - core::at(under, c));
            guide_worst = std::max(guide_worst, error);
            bad = bad || error > 0.01;
        }
        ++guided;
        guide_wrong += bad ? 1 : 0;
    }
    tally->check(ok && guided > 1000 && guide_wrong == 0,
                 text::format("Ray Reconstruction's albedo guide is the floor's own (under the glass too): %d pixels, %d "
                              "wrong (worst %.4f)",
                              guided, guide_wrong, guide_worst));
    // (round RG) an added glow lit per pixel, at the floor's depth: the floor's guides stay as they were (NVIDIA's DLSS-RR
    // guide 3.4.1 - the guides are the opaque surface's; the glow's light is in the colour), and its light is its own
    // over the floor's - it reads none of the floor's inputs (the lamps, the traced specular)
    int glow_guided = 0;
    int glow_guide_wrong = 0;
    for (const FloorPixel& pixel : pixels) {
        if (!ok || lit_glow_guides.size() != guides.size() || guides.empty() ||
            !through_quad(pixel.x, pixel.y, kLitGlowOver, -kFloorHalf, 0.0, 0.0, kFloorHalf)) {
            continue;
        }
        const std::size_t at =
            ((static_cast<std::size_t>(pixel.y) * kTargetWidth) + static_cast<std::size_t>(pixel.x)) * kGuideBytes;
        const bool same = std::memcmp(&core::at(guides, at), &core::at(lit_glow_guides, at), kGuideBytes) == 0;
        // (the glass shot's guides are the floor's: the glass writes none - checked above)
        ++glow_guided;
        glow_guide_wrong += same ? 0 : 1;
    }
    tally->check(ok && glow_guided > 1000 && glow_guide_wrong == 0,
                 text::format("a lit glow added at the floor's depth leaves the floor's Ray Reconstruction guides: %d pixels, "
                              "%d overwritten",
                              glow_guided, glow_guide_wrong));
    // (round RG) Ray Reconstruction's transparency layer (NVIDIA's DLSS-RR Integration Guide 3.4.10): with it, the glass
    // (alpha-blended, no depth written) and the glow (added) are drawn into the layer, not the colour - the colour the
    // floor's alone, the layer premultiplied light with in alpha how much it hides: the glass's alpha x its light and
    // its alpha, the glow's light and 0; the layer over the colour (rgb + (1 - a) x the colour) the picture as drawn
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::set_surface_light_layer_forced(true);
    }
    std::array<std::vector<uint8_t>, 2> layered_shots;
    std::array<std::vector<uint8_t>, 2> layers;
    ok = ok && draw_per_surface(gpu, targets, meshes, false, Over::kGlass, frame++, &core::at(layered_shots, 0), nullptr,
                                &core::at(layers, 0)) &&
         draw_per_surface(gpu, targets, meshes, false, Over::kGlow, frame++, &core::at(layered_shots, 1), nullptr,
                          &core::at(layers, 1));
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::set_surface_light_layer_forced(false);
    }
    for (std::size_t which = 0; which < 2; ++which) {
        const bool glass_case = which == 0;
        const Rgb own = glass_case ? glass : glow;
        const double alpha = glass_case ? kGlassAlpha : 0.0;
        const double light_share = glass_case ? kGlassAlpha : 1.0;
        int pixels_in = 0;
        int colour_wrong = 0;
        int layer_wrong = 0;
        int composed_wrong = 0;
        double layer_worst = 0.0;
        for (const FloorPixel& pixel : pixels) {
            if (!ok || core::at(layers, which).size() < static_cast<std::size_t>(kTargetWidth) * kTargetHeight * 8 ||
                !through_quad(pixel.x, pixel.y, kOverFloor, -kFloorHalf, 0.0, 0.0, kFloorHalf)) {
                continue;
            }
            const Rgb floor_only = linear_pixel_at(core::at(shots, 1), pixel.x, pixel.y);
            const Rgb colour_now = linear_pixel_at(core::at(layered_shots, which), pixel.x, pixel.y);
            const Rgb drawn = linear_pixel_at(core::at(shots, glass_case ? 2 : 3), pixel.x, pixel.y);
            const std::size_t at =
                ((static_cast<std::size_t>(pixel.y) * kTargetWidth) + static_cast<std::size_t>(pixel.x)) * 8;
            std::array<double, 4> texel{};
            for (std::size_t c = 0; c < 4; ++c) {
                uint16_t half = 0;
                std::memcpy(&half, &core::at(core::at(layers, which), at + (c * 2)), sizeof(half));
                core::at(texel, c) = half_to_double(half);
            }
            bool colour_bad = false;
            bool layer_bad = std::abs(core::at(texel, 3) - alpha) > kLinearTolerance;
            bool composed_bad = false;
            for (std::size_t c = 0; c < 3; ++c) {
                colour_bad = colour_bad || std::abs(core::at(colour_now, c) - core::at(floor_only, c)) > kLinearTolerance;
                layer_bad = layer_bad || std::abs(core::at(texel, c) - (light_share * core::at(own, c))) > kLinearTolerance;
                const double composed = core::at(texel, c) + ((1.0 - core::at(texel, 3)) * core::at(colour_now, c));
                layer_worst = std::max(layer_worst, std::abs(composed - core::at(drawn, c)));
                composed_bad = composed_bad || std::abs(composed - core::at(drawn, c)) > kLinearTolerance;
            }
            ++pixels_in;
            colour_wrong += colour_bad ? 1 : 0;
            layer_wrong += layer_bad ? 1 : 0;
            composed_wrong += composed_bad ? 1 : 0;
        }
        tally->check(ok && pixels_in > 100 && colour_wrong == 0 && layer_wrong == 0 && composed_wrong == 0,
                     text::format("Ray Reconstruction's transparency layer holds the %s (%d pixels: the colour the floor's "
                                  "alone %d wrong, the layer %d wrong, the layer over the colour vs as drawn %d wrong - "
                                  "worst %.4f)",
                                  glass_case ? "glass" : "glow", pixels_in, colour_wrong, layer_wrong, composed_wrong,
                                  layer_worst));
    }
    // (round CENSUS - the bake ratio retired) The floor as an unlit surface without a light store (its own colours: two
    // vertex colours - the bake's light, 3 x apart): lit per pixel as the rays see it - its material (the texel x the
    // pass colour under the neutral light) lit by the sun's traced visibility on the light model, the colour keeping only
    // its indirect share x the indirect visibility (1 here: no occlusion, no GI). So in the sun its light differs from
    // the shadow's by the light model's sun alone, whatever the vertex colour. Before, the colour was taken by shares:
    // its direct share (the bake x the field's ratio) stood for the sun - 3 x apart between the two colours.
    {
        const View view = view_from(kEye);
        const brdf::Rgb albedo{static_cast<float>(rt::linear_from_display(2.0 * kNoStorePass)),
                               static_cast<float>(rt::linear_from_display(2.0 * kNoStorePass)),
                               static_cast<float>(rt::linear_from_display(2.0 * kNoStorePass))};
        brdf::Material m;
        m.albedo = albedo;
        const float painted_f0 = std::get<0>(rt::material_f0(rt::MaterialKind::kPainted));
        m.f0 = {painted_f0, painted_f0, painted_f0};
        m.roughness = rt::material_values(rt::MaterialKind::kPainted).roughness;
        const float exposure = rt::frame_pre_exposure();
        const brdf::Vec3 up{0.0F, 1.0F, 0.0F};
        const brdf::Vec3 l{static_cast<float>(x_of(toward)), static_cast<float>(y_of(toward)), static_cast<float>(z_of(toward))};
        for (const uint32_t vertex_colour : {0xFF404040U, 0xFFC0C0C0U}) {
            std::vector<uint8_t> shot;
            bool drew = ok;
            for (int pass = 0; drew && pass < 2; ++pass) {
                drew = draw_per_surface(gpu, targets, meshes, true, Over::kNothing, frame++, &shot, nullptr, nullptr,
                                        vertex_colour);
            }
            const float bake = static_cast<float>(
                rt::linear_from_display(2.0 * static_cast<double>(vertex_colour & 0xFFU) / 255.0));
            int sunlit = 0;
            int shaded = 0;
            int wrong = 0;
            double worst = 0.0;
            for (const FloorPixel& pixel : pixels) {
                if (!drew) {
                    break;
                }
                const Seen seen = seen_at(view, pixel.x + 0.5, pixel.y + 0.5);
                const brdf::Vec3 v = brdf::normalise({static_cast<float>(x_of(kEye) - x_of(seen.point)),
                                                      static_cast<float>(y_of(kEye) - y_of(seen.point)),
                                                      static_cast<float>(z_of(kEye) - z_of(seen.point))});
                const float nv = std::max(brdf::dot(up, v), brdf::kLeastCosine);
                const brdf::Rgb kept = brdf::ambient(m, nv, {bake, bake, bake}, {bake, bake, bake});
                const float sun = pixel.shadowed ? 0.0F : exposure;
                const brdf::Rgb direct = brdf::direct(m, up, v, l, {sun, sun, sun});
                const Rgb got = linear_pixel_at(shot, pixel.x, pixel.y);
                bool bad = false;
                for (std::size_t c = 0; c < 3; ++c) {
                    const double want = (static_cast<double>(core::at(kept, c)) * (1.0 - kShareDirect)) +
                                        static_cast<double>(core::at(direct, c));
                    const double error = std::abs(core::at(got, c) - want);
                    worst = std::max(worst, error / std::max(want, 1e-3));
                    bad = bad || error > kLinearTolerance + (0.02 * want);
                }
                wrong += bad ? 1 : 0;
                (pixel.shadowed ? shaded : sunlit) += 1;
            }
            tally->check(drew && sunlit > 1000 && shaded > 300 && wrong == 0,
                         text::format("a surface without a light store (its own colours 0x%02X) lit by the rays' light: "
                                      "%d sunlit and %d shadowed pixels, %d off the light model (the sun on its material, "
                                      "its colour's indirect share; worst %.1f %%)",
                                      vertex_colour & 0xFFU, sunlit, shaded, wrong, 100.0 * worst));
        }
    }
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::set_surface_light_guides_forced(false);
    }
    run_decal_layers(gpu, meshes, tally); // (round DB: with the lit glow's texture and program)
    run_overlay_layers(gpu, meshes, tally); // (round DECAL: the same)
    run_front_layer(gpu, meshes, tally);  // (round FL: the same)
    if (lit_glow_draw().shader != nullptr) {
        gpu.release_pixel_shader(lit_glow_draw().shader);
    }
    if (lit_glow_draw().texture != nullptr) {
        gpu.release_texture(lit_glow_draw().texture);
    }
    lit_glow_draw() = {};
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    for (GpuSurface* surface : {targets.colour, targets.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
    rt::set_day_light({});
    rt::set_cel_world({});
    render::vulkan::set_hdr_settings({});
    rt::set_hdr_colour(hdr_before);
    render::vulkan::set_rt_shadow_settings({});
    rt::set_sun_override(nullptr);
}

// --- (round DB) decals and painted layers as material layers ---------------------------------------------------------
// The deferred decal's model (UE's DBuffer decals; vulkan_surface_light.h SurfaceInputs::decals / layer): a mark edits
// the surface's material before it is lit, and a layer the game paints over a surface is lit as that surface. A floor
// lit per pixel (a lit object's draw: the single-texture material shader, a white texture, the ambient light alone - an
// even environment of 1 for its diffuse and its specular; its one directional light black), seen from kLayerEye - low
// over it, the far floor at 3 - 7 degrees:
//   - an oil film over the whole view (albedo layer (A, 0), material layer (roughness, F0, 0, 0)): every floor pixel is
//     the light model's for oil alone - A (1 - E) + E, E the specular's directional albedo for oil's F0 and roughness at
//     the pixel's N.V (rt_brdf.h ambient) -, whatever the floor's own material: the layer replaces it (a multiply of the
//     lit colour can't give it), darker than the bare floor and with more specular than a rough layer of the same
//     colour; Ray Reconstruction's guides carry oil's albedo and roughness;
//   - a layer painted over the floor kPaintLift above it (alpha-blended, depth written, its alpha a checker crossing
//     one half - the prepass keeps the paint at some pixels and the floor at the others), both white, the decals'
//     layers a darkening by kMark: every pixel under the paint as the floor alone (the paint and the floor are one
//     surface lit once - before round DB the paint's fragments over the floor's prepass, 0.8 / sin 5 degrees along the
//     ray, kept their own light and showed the checker).
constexpr Vec3 kLayerEye{0.0, 25.0, 320.0};
constexpr double kPaintLift = 0.8;   // (Boston's crosswalk paint over its asphalt)
constexpr double kPaintInset = 20.0; // (the paint a little inside the floor's edge)
constexpr float kOilLinear = 0.033F; // (oil's albedo: the art's 0.2, display-encoded)
constexpr float kRoughLayer = 0.94F; // (a rough layer of oil's colour: the table's ground)
constexpr uint32_t kPaintLow = 0x28FFFFFFU;  // (white, alpha 40: drawn x 2 - 0.31)
constexpr uint32_t kPaintHigh = 0x64FFFFFFU; // (white, alpha 100: drawn x 2 - 0.78)
constexpr double kLayerTolerance = 0.006;
constexpr double kEdgeMargin = 30.0; // (units kept off the quads' edges)
constexpr float kLampsIn = 4.0F;     // (round BIAS) the lamps input set on the oil (a white diffuser's light)

// A 2 x 2 texture of two texels in a checker.
bool make_checker(render::Gpu& gpu, uint32_t one, uint32_t other, render::GpuTexture** texture)
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
            put_word(into, (row * pitch) + (column * kPixelBytes), ((row + column) % 2U) == 0U ? one : other);
        }
    }
    gpu.unlock_texture(*texture, 0);
    return true;
}

// The point on the floor (y = `height`) the eye's ray through the pixel's centre meets, and whether it does.
bool floor_point(const Vec3& eye, int x, int y, double height, Vec3* at)
{
    const View view = view_from(eye);
    const double ndc_x = (2.0 * (x + 0.5) / kTargetWidth) - 1.0;
    const double ndc_y = 1.0 - (2.0 * (y + 0.5) / kTargetHeight);
    const Vec3 direction = normalised(
        step(step(step(Vec3{}, view.right, ndc_x * view.half_width), view.up, ndc_y * view.half_height), view.back, -1.0));
    if (y_of(direction) >= 0.0) {
        return false;
    }
    *at = step(eye, direction, (height - y_of(eye)) / y_of(direction));
    return true;
}

// A frame of the view lit per surface (from kLayerEye): the floor lit per pixel, the paint over it or not - (round DECAL)
// or `overlay`: the paint as the game's overlay cards (alpha-blended, no depth written) kOverlayLift over the floor, and
// Ray Reconstruction's transparency layer read into `layer` (null: not read).
enum class Paint : uint8_t { kNone, kPainted, kOverlay };
constexpr double kOverlayLift = 3.3; // (Boston's painted shadow cards over their brick walls: 2.7 - 3.3)
bool draw_layers(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, render::GpuTexture* white,
                 render::GpuTexture* checker, Paint paint, uint64_t frame, std::vector<uint8_t>* texels,
                 std::vector<uint8_t>* guides, std::vector<uint8_t>* layer = nullptr)
{
    using render::GpuRenderState;
    rt::FrameRecord& record = rt::frame_record();
    record = rt::FrameRecord{};
    record.frame = frame;
    constexpr std::array<std::array<float, 4>, 3> kIdentityRows{
        {{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
    for (const SceneMesh& mesh : meshes) {
        record.instances.push_back(
            {.mesh = mesh.id, .material = mesh.material, .flags = mesh.flags, .object_to_world = kIdentityRows});
    }
    gpu.begin_scene();
    rt::ray_tracing()->begin_frame(record);
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, 0xFF000000U, 1.0F, 0);
    rt::view_begins(view_drawn());
    rt::set_world_surfaces_drawing(true);
    draw_scene(gpu, {}); // (the states and the camera)
    gpu.set_render_state(GpuRenderState::kLighting, 1);
    gpu.set_render_state(GpuRenderState::kAmbient, 0xFFFFFFFFU);
    gpu.set_render_state(GpuRenderState::kDiffuseMaterialSource, value::kSourceColour1);
    gpu.set_material({.diffuse = {1, 1, 1, 1}, .ambient = {1, 1, 1, 1}, .specular = {}, .emissive = {}, .power = 0});
    render::GpuLight light{}; // (black: a lit object's draw has a light - it adds nothing)
    light.type = value::kLightDirectional;
    light.direction = {0.0F, -1.0F, 0.0F};
    gpu.set_light(0, light);
    gpu.enable_light(0, true);
    gpu.set_vertex_format(kFvfColourNormal);
    gpu.set_pixel_shader(lit_glow_draw().shader);
    constexpr std::array<float, 4> kHalf{0.5F, 0.5F, 0.5F, 0.5F}; // (c0: x 4 with v0 0.5 - the texel itself)
    gpu.set_pixel_constants(0, kHalf.data(), 1);
    const auto quad = [&gpu](double height, double inset) {
        std::vector<NormalVertex> corners;
        for (const ColourVertex& corner :
             quad_at(height, -kFloorHalf + inset, kFloorHalf - inset, -kFloorHalf + inset, kFloorHalf - inset, 0xFFFFFFFFU)) {
            constexpr float kTiles = 0.1F; // (the checker's texels 5 units across)
            corners.push_back({.position = corner.position,
                               .normal = {0.0F, 1.0F, 0.0F},
                               .colour = corner.colour,
                               .uv = {core::at(corner.position, 0) * kTiles, core::at(corner.position, 2) * kTiles}});
        }
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(corners.size() / 3), corners.data(),
                             sizeof(NormalVertex));
    };
    gpu.set_texture(0, white);
    quad(0.0, 0.0);
    if (paint != Paint::kNone) {
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 1);
        gpu.set_render_state(GpuRenderState::kSrcBlend, value::kBlendSrcAlpha);
        gpu.set_render_state(GpuRenderState::kDestBlend, value::kBlendInvSrcAlpha);
        gpu.set_render_state(GpuRenderState::kZWriteEnable, paint == Paint::kOverlay ? 0U : 1U);
        gpu.set_texture(0, checker);
        quad(paint == Paint::kOverlay ? kOverlayLift : kPaintLift, kPaintInset);
        gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    }
    gpu.set_pixel_shader(nullptr);
    gpu.set_texture(0, nullptr);
    gpu.enable_light(0, false);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_vertex_format(kFvfColour);
    rt::set_world_surfaces_drawing(false);
    rt::opaque_view_drawn(view_drawn());
    bool read = true;
    if (guides != nullptr || layer != nullptr) {
        // (both found before the work is finished for the reads - a new submission: they are the drawing one's) (round
        // DECAL) and Ray Reconstruction's transparency layer (none drawn this view: empty)
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::Image* const guide_image = render::vulkan::surface_light_guides(state);
        render::vulkan::Image* const layer_image = render::vulkan::surface_light_layer(state);
        read = render::vulkan::finish_work(state);
        if (guides != nullptr) {
            guides->resize(static_cast<std::size_t>(kTargetWidth) * kTargetHeight * 16);
            read = read && guide_image != nullptr && state.memory.read_image_level(guide_image, 0, guides);
        }
        if (layer != nullptr) {
            layer->clear();
            if (layer_image != nullptr) {
                layer->resize(static_cast<std::size_t>(kTargetWidth) * kTargetHeight * 8);
                read = read && state.memory.read_image_level(layer_image, 0, layer);
            }
        }
    }
    read = read && render::vulkan::read_surface(targets.colour, texels);
    gpu.end_scene();
    gpu.present();
    return read && texels->size() == static_cast<std::size_t>(kTargetWidth) * kTargetHeight * 8;
}

void run_decal_layers(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("round DB - decals and painted layers as material layers (the deferred decal's model):\n");
    render::vulkan::set_rt_light_settings({});
    render::vulkan::set_rt_shadow_settings({});
    render::vulkan::set_hdr_settings({.on = true});
    const bool hdr_before = rt::hdr_colour();
    rt::set_hdr_colour(true);
    const std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    rt::set_cel_world({.relit = true, .sun = {1.0F, 1.0F, 1.0F}, .toward = up});
    const render::GpuSurfaceDesc colour{.width = kTargetWidth,
                                        .height = kTargetHeight,
                                        .format = value::kFormatA16B16G16R16F,
                                        .samples = 0,
                                        .quality = 0,
                                        .lockable_or_discard = true};
    render::GpuSurfaceDesc depth = colour;
    depth.format = value::kFormatD24S8;
    depth.lockable_or_discard = false;
    Targets targets;
    render::GpuTexture* checker = nullptr;
    bool ok = gpu.create_render_target(colour, &targets.colour) == value::kResultOk &&
              gpu.create_depth_surface(depth, &targets.depth) == value::kResultOk &&
              make_checker(gpu, kPaintLow, kPaintHigh, &checker) && lit_glow_draw().texture != nullptr &&
              lit_glow_draw().shader != nullptr;
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::set_surface_light_guides_forced(true);
    }
    scene_eye() = kLayerEye;
    const float oil_f0 = rt::brdf::f0_of_index(1.47F);
    constexpr float kOilRoughness = 0.3F;
    std::vector<uint8_t> bare;
    std::vector<uint8_t> oil;
    std::vector<uint8_t> rough;
    std::vector<uint8_t> oil_guides;
    std::vector<uint8_t> floor_marked;
    std::vector<uint8_t> painted_marked;
    uint64_t frame = 1000;
    MarkListener mark;
    for (int pass = 0; ok && pass < 2; ++pass) { // (the second time round: the TLAS of the frame before built)
        ok = draw_layers(gpu, targets, meshes, lit_glow_draw().texture, checker, Paint::kNone, frame++, &bare, nullptr);
    }
    rt::add_view_listener(&mark);
    mark.albedo = {kOilLinear, kOilLinear, kOilLinear, 0.0F};
    mark.layer = {kOilRoughness, oil_f0, 0.0F, 0.0F};
    ok = ok && draw_layers(gpu, targets, meshes, lit_glow_draw().texture, checker, Paint::kNone, frame++, &oil, &oil_guides);
    mark.layer = {kRoughLayer, oil_f0, 0.0F, 0.0F};
    ok = ok && draw_layers(gpu, targets, meshes, lit_glow_draw().texture, checker, Paint::kNone, frame++, &rough, nullptr);
    // (round BIAS) the lamps input on oil: none, then kLampsIn
    std::vector<uint8_t> oil_unlit;
    std::vector<uint8_t> oil_lamps;
    mark.layer = {kOilRoughness, oil_f0, 0.0F, 0.0F};
    mark.albedo = {kOilLinear, kOilLinear, kOilLinear, 0.0F};
    mark.lamps = {0.0F, 0.0F, 0.0F, 0.0F};
    ok = ok && draw_layers(gpu, targets, meshes, lit_glow_draw().texture, checker, Paint::kNone, frame++, &oil_unlit, nullptr);
    mark.lamps = {kLampsIn, kLampsIn, kLampsIn, 0.0F};
    ok = ok && draw_layers(gpu, targets, meshes, lit_glow_draw().texture, checker, Paint::kNone, frame++, &oil_lamps, nullptr);
    mark.lamps = {0.0F, 0.0F, 0.0F, -1.0F};
    mark.albedo = {0.0F, 0.0F, 0.0F, kMark};
    mark.layer = {0.0F, 0.0F, 0.0F, 1.0F};
    ok = ok && draw_layers(gpu, targets, meshes, lit_glow_draw().texture, checker, Paint::kNone, frame++, &floor_marked, nullptr) &&
         draw_layers(gpu, targets, meshes, lit_glow_draw().texture, checker, Paint::kPainted, frame++, &painted_marked, nullptr);
    rt::remove_view_listener(&mark);
    scene_eye() = kEye;
    tally->check(ok && mark.marked == 6, text::format("the layers' frames drawn and read (%d marked)", mark.marked));
    // the oil film: the light model's for oil alone at each floor pixel
    int oil_pixels = 0;
    int oil_wrong = 0;
    int darker = 0;
    int glossier = 0;
    int guide_wrong = 0;
    double oil_worst = 0.0;
    double specular_oil = 0.0;
    double specular_rough = 0.0;
    const rt::brdf::Material oil_material{.albedo = {kOilLinear, kOilLinear, kOilLinear},
                                          .f0 = {oil_f0, oil_f0, oil_f0},
                                          .roughness = kOilRoughness};
    rt::brdf::Material rough_material = oil_material;
    rough_material.roughness = kRoughLayer;
    constexpr rt::brdf::Rgb kEven{1.0F, 1.0F, 1.0F};
    for (int y = 0; ok && y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            Vec3 at{};
            // (kept off the floor's far edge: a pixel row there is 12 units deep)
            if (!floor_point(kLayerEye, x, y, 0.0, &at) || std::abs(x_of(at)) > kFloorHalf - kEdgeMargin ||
                std::abs(z_of(at)) > kFloorHalf - kEdgeMargin) {
                continue;
            }
            const Vec3 toward_eye = normalised(minus(kLayerEye, at));
            const auto n_dot_v = static_cast<float>(y_of(toward_eye));
            const rt::brdf::Rgb want = rt::brdf::ambient(oil_material, n_dot_v, kEven, kEven);
            const rt::brdf::Rgb want_rough = rt::brdf::ambient(rough_material, n_dot_v, kEven, kEven);
            const Rgb got = linear_pixel_at(oil, x, y);
            const Rgb got_rough = linear_pixel_at(rough, x, y);
            const Rgb was = linear_pixel_at(bare, x, y);
            bool bad = false;
            for (std::size_t c = 0; c < 3; ++c) {
                const double error = std::abs(core::at(got, c) - core::at(want, c));
                oil_worst = std::max(oil_worst, error);
                bad = bad || error > std::max(kLayerTolerance, 0.03 * core::at(want, c)) ||
                      std::abs(core::at(got_rough, c) - core::at(want_rough, c)) >
                          std::max(kLayerTolerance, 0.03 * core::at(want_rough, c));
            }
            ++oil_pixels;
            oil_wrong += bad ? 1 : 0;
            darker += luma_of(got) < luma_of(was) ? 1 : 0;
            glossier += luma_of(got) > luma_of(got_rough) ? 1 : 0;
            const rt::brdf::Rgb e_oil = rt::brdf::specular_albedo(oil_material.f0, n_dot_v, kOilRoughness);
            const rt::brdf::Rgb e_rough = rt::brdf::specular_albedo(rough_material.f0, n_dot_v, kRoughLayer);
            specular_oil += core::at(e_oil, 0);
            specular_rough += core::at(e_rough, 0);
            // Ray Reconstruction's guides: oil's albedo (display-encoded) and roughness
            const std::size_t g = ((static_cast<std::size_t>(y) * kTargetWidth) + static_cast<std::size_t>(x)) * 16;
            uint32_t albedo = 0;
            std::memcpy(&albedo, &core::at(oil_guides, g), sizeof(albedo));
            const double guide_albedo = rt::linear_from_display(static_cast<double>(albedo & 0xFFU) / kByteRange);
            const double guide_rough = static_cast<double>((albedo >> 24U) & 0xFFU) / kByteRange;
            guide_wrong += std::abs(guide_albedo - kOilLinear) > 0.01 || std::abs(guide_rough - kOilRoughness) > 1.5 / kByteRange ? 1 : 0;
        }
    }
    const double pixels = std::max(oil_pixels, 1);
    tally->check(ok && oil_pixels > 1000 && oil_wrong == 0,
                 text::format("an oil film is a layer of oil's material, not a multiply of the lit colour: %d floor pixels, %d "
                              "off the light model for oil (and for a rough layer of its colour) - worst %.4f",
                              oil_pixels, oil_wrong, oil_worst));
    tally->check(ok && darker == oil_pixels && glossier == oil_pixels && specular_oil > specular_rough,
                 text::format("... darker than the bare floor at %d of %d pixels, more light than the rough layer at %d (its "
                              "specular's directional albedo %.4f against %.4f, the mean over the floor)",
                              darker, oil_pixels, glossier, specular_oil / pixels, specular_rough / pixels));
    tally->check(ok && oil_pixels > 1000 && guide_wrong == 0,
                 text::format("Ray Reconstruction's albedo guide carries the oil's colour and roughness: %d pixels, %d wrong",
                              oil_pixels, guide_wrong));
    // (round BIAS) the lamps input on the diffuse lobe: A (1 - E) x it at each pixel's N.V - the light model's diffuse
    // (brdf_direct's, the reference's) - not A x it, which counted the light the specular lobe reflects a second time
    int lamp_pixels = 0;
    int lamp_wrong = 0;
    double lamp_got = 0.0;
    double lamp_want = 0.0;
    double lamp_old = 0.0;
    for (int y = 0; ok && y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            Vec3 at{};
            if (!floor_point(kLayerEye, x, y, 0.0, &at) || std::abs(x_of(at)) > kFloorHalf - kEdgeMargin ||
                std::abs(z_of(at)) > kFloorHalf - kEdgeMargin) {
                continue;
            }
            const auto n_dot_v = static_cast<float>(y_of(normalised(minus(kLayerEye, at))));
            constexpr rt::brdf::Rgb kLamps{kLampsIn, kLampsIn, kLampsIn};
            const rt::brdf::Rgb want = rt::brdf::ambient(oil_material, n_dot_v, kLamps, {0.0F, 0.0F, 0.0F});
            const Rgb lit = linear_pixel_at(oil_lamps, x, y);
            const Rgb unlit = linear_pixel_at(oil_unlit, x, y);
            const double got = core::at(lit, 1) - core::at(unlit, 1);
            const double expected = core::at(want, 1);
            ++lamp_pixels;
            lamp_wrong += std::abs(got - expected) > std::max(kLayerTolerance, 0.03 * expected) ? 1 : 0;
            lamp_got += got;
            lamp_want += expected;
            lamp_old += static_cast<double>(kOilLinear) * kLampsIn;
        }
    }
    tally->check(ok && lamp_pixels > 1000 && lamp_wrong == 0,
                 text::format("the lamps input on the diffuse lobe (A (1 - E), the light model's diffuse): %d pixels, %d off "
                              "it; mean %.5f against %.5f (A x it: %.5f)",
                              lamp_pixels, lamp_wrong, lamp_got / std::max(lamp_pixels, 1), lamp_want / std::max(lamp_pixels, 1),
                              lamp_old / std::max(lamp_pixels, 1)));
    // the painted layer: as the floor alone, at a grazing view
    int paint_pixels = 0;
    int paint_wrong = 0;
    double paint_worst = 0.0;
    for (int y = 0; ok && y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            Vec3 at{};
            const double inner = kFloorHalf - kPaintInset - kEdgeMargin;
            if (!floor_point(kLayerEye, x, y, kPaintLift, &at) || std::abs(x_of(at)) > inner || std::abs(z_of(at)) > inner) {
                continue;
            }
            const Rgb floor_only = linear_pixel_at(floor_marked, x, y);
            const Rgb painted = linear_pixel_at(painted_marked, x, y);
            bool bad = false;
            for (std::size_t c = 0; c < 3; ++c) {
                const double error = std::abs(core::at(painted, c) - core::at(floor_only, c));
                paint_worst = std::max(paint_worst, error);
                bad = bad || error > std::max(kLayerTolerance, 0.02 * core::at(floor_only, c));
            }
            ++paint_pixels;
            paint_wrong += bad ? 1 : 0;
        }
    }
    tally->check(ok && paint_pixels > 1000 && paint_wrong == 0,
                 text::format("a layer painted %.1f units over the floor is lit as the floor at a grazing view (the "
                              "checker's alpha either side of one half): %d pixels, %d off the floor alone (worst %.4f)",
                              kPaintLift, paint_pixels, paint_wrong, paint_worst));
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::set_surface_light_guides_forced(false);
    }
    if (checker != nullptr) {
        gpu.release_texture(checker);
    }
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    for (GpuSurface* surface : {targets.colour, targets.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
    rt::set_cel_world({});
    render::vulkan::set_hdr_settings({});
    rt::set_hdr_colour(hdr_before);
}

// --- (round DECAL) the game's overlay cards laid into the surface under them -------------------------------------------
// UE's DBuffer decals for the game's own overlays (vulkan_surface_light.h "laid"): the floor lit per pixel (as round DB's,
// from kLayerEye) and over it an overlay card kOverlayLift above - alpha-blended, no depth written, grey at half alpha:
// Boston's painted shadows. Laid, the floor is one surface with the composite material: its colour as the same floor
// with the decals' inputs set to the card's layer (albedo F (1 - a) + P a - MarkListener), Ray Reconstruction's albedo
// guide the composite, and nothing of the card in the transparency layer (before: the card was a transparency drawn
// there - not denoised - and the guide was the bare floor's).
constexpr uint32_t kOverlayTexel = 0x40404040U; // (grey 64 - 0.251 -, alpha 64: drawn x 2 - 0.502)
constexpr double kOverlayTolerance = 0.02;      // (a share of the expected; half floats and the guide's 8 bits)

void run_overlay_layers(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    text::print("round DECAL - the game's overlay cards laid into the surface under them (DBuffer decals):\n");
    render::vulkan::set_rt_light_settings({});
    render::vulkan::set_rt_shadow_settings({});
    render::vulkan::set_hdr_settings({.on = true});
    const bool hdr_before = rt::hdr_colour();
    rt::set_hdr_colour(true);
    const std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    rt::set_cel_world({.relit = true, .sun = {1.0F, 1.0F, 1.0F}, .toward = up});
    const render::GpuSurfaceDesc colour{.width = kTargetWidth,
                                        .height = kTargetHeight,
                                        .format = value::kFormatA16B16G16R16F,
                                        .samples = 0,
                                        .quality = 0,
                                        .lockable_or_discard = true};
    render::GpuSurfaceDesc depth = colour;
    depth.format = value::kFormatD24S8;
    depth.lockable_or_discard = false;
    Targets targets;
    render::GpuTexture* card = nullptr;
    bool ok = gpu.create_render_target(colour, &targets.colour) == value::kResultOk &&
              gpu.create_depth_surface(depth, &targets.depth) == value::kResultOk &&
              make_checker(gpu, kOverlayTexel, kOverlayTexel, &card) && lit_glow_draw().texture != nullptr &&
              lit_glow_draw().shader != nullptr;
    const auto forced = [](bool on) {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::set_surface_light_guides_forced(on);
        render::vulkan::set_surface_light_layer_forced(on);
        render::vulkan::set_surface_light_lay_lit_forced(on);
    };
    forced(true);
    scene_eye() = kLayerEye;
    std::vector<uint8_t> bare;
    std::vector<uint8_t> bare_guides;
    std::vector<uint8_t> laid;
    std::vector<uint8_t> laid_guides;
    std::vector<uint8_t> laid_layer;
    std::vector<uint8_t> marked;
    uint64_t frame = 2000;
    for (int pass = 0; ok && pass < 2; ++pass) { // (the second time round: the TLAS of the frame before built)
        ok = draw_layers(gpu, targets, meshes, lit_glow_draw().texture, card, Paint::kNone, frame++, &bare, &bare_guides);
    }
    const uint64_t laid_before = render::vulkan::surface_light_stats().laid;
    ok = ok && draw_layers(gpu, targets, meshes, lit_glow_draw().texture, card, Paint::kOverlay, frame++, &laid, &laid_guides,
                           &laid_layer);
    const uint64_t laid_draws = render::vulkan::surface_light_stats().laid - laid_before;
    // the card's layer as the decals' inputs: its albedo P x a over the floor's x (1 - a); its roughness and F0 the floor's
    // (the same program and material)
    const auto paint = static_cast<float>(rt::linear_from_display(64.0 / kByteRange)); // (its albedo the texel's colour)
    const auto a = static_cast<float>(2.0 * 64.0 / kByteRange);                       // (its alpha drawn x 2)
    MarkListener mark;
    mark.albedo = {paint * a, paint * a, paint * a, 1.0F - a};
    mark.layer = {0.0F, 0.0F, 0.0F, 1.0F};
    rt::add_view_listener(&mark);
    ok = ok && draw_layers(gpu, targets, meshes, lit_glow_draw().texture, card, Paint::kNone, frame++, &marked, nullptr);
    rt::remove_view_listener(&mark);
    scene_eye() = kEye;
    forced(false);
    tally->check(ok && mark.marked == 1 && laid_draws >= 1,
                 text::format("the overlay's frames drawn and read (%llu draws laid)", static_cast<unsigned long long>(laid_draws)));
    int pixels = 0;
    int colour_wrong = 0;
    int guide_wrong = 0;
    int in_layer = 0;
    double colour_worst = 0.0;
    double guide_got = 0.0;
    double guide_want = 0.0;
    double guide_floor = 0.0;
    for (int y = 0; ok && y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            Vec3 at{};
            const double inner = kFloorHalf - kPaintInset - kEdgeMargin;
            if (!floor_point(kLayerEye, x, y, kOverlayLift, &at) || std::abs(x_of(at)) > inner || std::abs(z_of(at)) > inner) {
                continue;
            }
            ++pixels;
            const Rgb got = linear_pixel_at(laid, x, y);
            const Rgb want = linear_pixel_at(marked, x, y);
            bool bad = false;
            for (std::size_t c = 0; c < 3; ++c) {
                const double error = std::abs(core::at(got, c) - core::at(want, c));
                colour_worst = std::max(colour_worst, error);
                bad = bad || error > std::max(kLayerTolerance, kOverlayTolerance * core::at(want, c));
            }
            colour_wrong += bad ? 1 : 0;
            const std::size_t g = ((static_cast<std::size_t>(y) * kTargetWidth) + static_cast<std::size_t>(x)) * 16;
            uint32_t floor_word = 0;
            uint32_t laid_word = 0;
            std::memcpy(&floor_word, &core::at(bare_guides, g), sizeof(floor_word));
            std::memcpy(&laid_word, &core::at(laid_guides, g), sizeof(laid_word));
            if (floor_word == 0U) {
                continue; // (no surface wrote the floor's guide there - its far edge)
            }
            const double floor_albedo = rt::linear_from_display(static_cast<double>(floor_word & 0xFFU) / kByteRange);
            const double got_albedo = rt::linear_from_display(static_cast<double>(laid_word & 0xFFU) / kByteRange);
            const double want_albedo = (floor_albedo * (1.0 - a)) + (static_cast<double>(paint) * a);
            guide_got += got_albedo;
            guide_floor += floor_albedo;
            guide_want += want_albedo;
            // (8 bits display-encoded, the floor's own guide read the same way: two steps)
            guide_wrong += std::abs(rt::display_from_linear(got_albedo) - rt::display_from_linear(want_albedo)) > 2.0 / kByteRange ? 1 : 0;
            if (!laid_layer.empty()) {
                const std::size_t l = ((static_cast<std::size_t>(y) * kTargetWidth) + static_cast<std::size_t>(x)) * 8;
                const auto alpha = static_cast<uint16_t>(core::at(laid_layer, l + 6) | (core::at(laid_layer, l + 7) << 8U));
                in_layer += half_to_double(alpha) > 0.0 ? 1 : 0;
            }
        }
    }
    tally->check(ok && pixels > 1000 && colour_wrong == 0,
                 text::format("under the card the floor is lit once with the composite material (as the floor with the card's "
                              "layer in the decals' inputs): %d pixels, %d off (worst %.4f)",
                              pixels, colour_wrong, colour_worst));
    tally->check(ok && pixels > 1000 && guide_wrong == 0,
                 text::format("Ray Reconstruction's albedo guide is the composite (mean %.4f, the floor's x (1 - a) + the "
                              "card's x a: %.4f; the floor's %.4f): %d of %d pixels off",
                              guide_got / std::max(pixels, 1), guide_want / std::max(pixels, 1),
                              guide_floor / std::max(pixels, 1), guide_wrong, pixels));
    tally->check(ok && pixels > 1000 && in_layer == 0,
                 text::format("nothing of the card in Ray Reconstruction's transparency layer (it isn't denoised): %d of %d "
                              "pixels covered (the layer %s)",
                              in_layer, pixels, laid_layer.empty() ? "not drawn" : "drawn, its other draws there"));
    if (card != nullptr) {
        gpu.release_texture(card);
    }
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    for (GpuSurface* surface : {targets.colour, targets.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
    rt::set_cel_world({});
    render::vulkan::set_hdr_settings({});
    rt::set_hdr_colour(hdr_before);
}

// --- (round FL) the front layer: see-through surfaces lit by the light list ------------------------------------------
// UE5 Lumen's Front Layer Translucency (vulkan_front_layer.h). The view lit per surface with ReSTIR DI and GI (direct_light
// / indirect_light restir), at night (the relit world's sun none, no sky) under one point lamp: the floor lit per pixel (a
// lit object's draw) and a glass pane kFrontGlassHeight over part of it (the same draw alpha-blended, no depth written).
// The glass is the view's front layer; its light there must be the lamp's as the light list's model sends it (rt_lights.h
// lamp_geometry on a white diffuser facing up, x the colour, the intensity and the frame's pre-exposure) - as the floor's
// lamps input (ReSTIR DI's) is the same model's at its points. The same frames with indirect_light passes (no ReSTIR GI,
// no list): no front layer lit. (round FRONT2) The pane again as a particle (rt::set_particles_drawing): the same light
// in the layer, drawn with its albedo x the layer's light, also when it is recorded again into Ray Reconstruction's
// transparency layer.
constexpr double kFrontGlassHeight = 40.0;
constexpr std::array<double, 4> kFrontGlass{30.0, 130.0, -70.0, 40.0}; // x0, x1, z0, z1
constexpr Vec3 kFrontLamp{-40.0, 120.0, 40.0};
constexpr double kFrontReach = 600.0;
constexpr int kFrontFrames = 40;
constexpr double kFrontLeast = 0.02;    // (pixels compared where the expected light is at least this)
constexpr double kFrontMargin = 4.0;    // (units kept off the quads' and the box's edges)
constexpr double kFrontRatioLow = 0.93;  // the device's light over the model's, its mean over the pixels
constexpr double kFrontRatioHigh = 1.07;
// (round SHEETS) the light the layer's other side finds on the floor over the floor's albedo x its lamps input: the hit is
// lit through its material's whole BSDF (the painted kind's specular lobe too) and the radiance cache's bounce, the input
// is a white diffuser's (measured 0.98)
constexpr double kBehindRatioLow = 0.93;
constexpr double kBehindRatioHigh = 1.07;
constexpr double kFrontGlassAlpha = 2.0 * 64.0 / 255.0; // (the pane's vertex alpha 0x40: the program's x 2 on v0.a x t0.a)
// (round FRONT2) the same pane as a particle: vertex alpha 0x40, its colour 0x20 grey (the game's light on it - never its
// albedo), the pass colour 0.25: its albedo the program's under v0 0.5 - 4 x 0.5 x the white texel x 0.25 = 0.5 (display)
constexpr uint32_t kParticleFrontColour = 0x40202020U;
constexpr float kParticleFrontC0 = 0.25F;

// A frame of the view: the floor, the glass over it or not; `front` / `lamps` read (RGBA16F each) when given.
bool draw_front(render::Gpu& gpu, const Targets& targets, std::span<const SceneMesh> meshes, std::span<const rt::Light> points,
                bool glass, uint64_t frame, std::vector<uint8_t>* front, std::vector<uint8_t>* lamps,
                std::vector<uint8_t>* colour = nullptr, bool particle = false, std::vector<uint8_t>* layer = nullptr,
                std::vector<uint8_t>* behind = nullptr)
{
    using render::GpuRenderState;
    rt::FrameRecord& record = rt::frame_record();
    record = rt::FrameRecord{};
    record.frame = frame;
    record.lights.assign(points.begin(), points.end());
    constexpr std::array<std::array<float, 4>, 3> kIdentityRows{
        {{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
    for (const SceneMesh& mesh : meshes) {
        record.instances.push_back(
            {.mesh = mesh.id, .material = mesh.material, .flags = mesh.flags, .object_to_world = kIdentityRows});
    }
    gpu.begin_scene();
    rt::ray_tracing()->begin_frame(record);
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kTargetWidth, .height = kTargetHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, 0xFF000000U, 1.0F, 0);
    rt::view_begins(view_drawn());
    rt::set_world_surfaces_drawing(true);
    draw_scene(gpu, {}); // (the states and the camera)
    gpu.set_render_state(GpuRenderState::kLighting, 1);
    gpu.set_render_state(GpuRenderState::kAmbient, 0x00000000U);
    gpu.set_render_state(GpuRenderState::kDiffuseMaterialSource, value::kSourceColour1);
    gpu.set_material({.diffuse = {1, 1, 1, 1}, .ambient = {1, 1, 1, 1}, .specular = {}, .emissive = {}, .power = 0});
    render::GpuLight light{}; // (black: a lit object's draw has a light - it adds nothing)
    light.type = value::kLightDirectional;
    light.direction = {0.0F, -1.0F, 0.0F};
    gpu.set_light(0, light);
    gpu.enable_light(0, true);
    gpu.set_vertex_format(kFvfColourNormal);
    gpu.set_pixel_shader(lit_glow_draw().shader);
    constexpr std::array<float, 4> kHalf{0.5F, 0.5F, 0.5F, 0.5F};
    gpu.set_pixel_constants(0, kHalf.data(), 1);
    gpu.set_texture(0, lit_glow_draw().texture);
    const auto quad = [&gpu](double height, double x0, double x1, double z0, double z1, uint32_t colour) {
        std::vector<NormalVertex> corners;
        for (const ColourVertex& corner : quad_at(height, x0, x1, z0, z1, colour)) {
            corners.push_back({.position = corner.position, .normal = {0.0F, 1.0F, 0.0F}, .colour = corner.colour});
        }
        gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(corners.size() / 3), corners.data(),
                             sizeof(NormalVertex));
    };
    quad(0.0, -kFloorHalf, kFloorHalf, -kFloorHalf, kFloorHalf, 0xFFFFFFFFU);
    if (glass) {
        gpu.set_render_state(GpuRenderState::kZWriteEnable, 0);
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 1);
        gpu.set_render_state(GpuRenderState::kSrcBlend, value::kBlendSrcAlpha);
        gpu.set_render_state(GpuRenderState::kDestBlend, value::kBlendInvSrcAlpha);
        if (particle) {
            // (round FRONT2) the pane drawn as the particles are: unlit, no normal, its vertex colour dark (the game's
            // light on it - not its albedo), the pass colour kParticleFrontC0, inside the particle passes
            gpu.set_render_state(GpuRenderState::kLighting, 0);
            gpu.set_vertex_format(kFvfColour);
            const std::array<float, 4> c0{kParticleFrontC0, kParticleFrontC0, kParticleFrontC0, kParticleFrontC0};
            gpu.set_pixel_constants(0, c0.data(), 1);
            rt::set_particles_drawing(true);
            const std::vector<ColourVertex> corners =
                quad_at(kFrontGlassHeight, core::at(kFrontGlass, 0), core::at(kFrontGlass, 1), core::at(kFrontGlass, 2),
                        core::at(kFrontGlass, 3), kParticleFrontColour);
            gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(corners.size() / 3),
                                 corners.data(), sizeof(ColourVertex));
            rt::set_particles_drawing(false);
            gpu.set_pixel_constants(0, kHalf.data(), 1);
            gpu.set_vertex_format(kFvfColourNormal);
            gpu.set_render_state(GpuRenderState::kLighting, 1);
        } else {
            quad(kFrontGlassHeight, core::at(kFrontGlass, 0), core::at(kFrontGlass, 1), core::at(kFrontGlass, 2),
                 core::at(kFrontGlass, 3), 0x40FFFFFFU);
        }
        gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
        gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    }
    gpu.set_pixel_shader(nullptr);
    gpu.set_texture(0, nullptr);
    gpu.enable_light(0, false);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_vertex_format(kFvfColour);
    rt::set_world_surfaces_drawing(false);
    rt::opaque_view_drawn(view_drawn());
    bool read = true;
    if (front != nullptr || lamps != nullptr) {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::FrontLayerImages images;
        render::vulkan::Image* const lamps_image = render::vulkan::surface_light_lamps_input();
        read = render::vulkan::surface_light_front_images(&images) && lamps_image != nullptr &&
               render::vulkan::finish_work(state);
        const std::size_t bytes = static_cast<std::size_t>(kTargetWidth) * kTargetHeight * 8;
        if (read && front != nullptr) {
            front->resize(bytes);
            read = state.memory.read_image_level(images.light.front(), 0, front);
        }
        if (read && lamps != nullptr) {
            lamps->resize(bytes);
            read = state.memory.read_image_level(lamps_image, 0, lamps);
        }
        if (read && behind != nullptr) { // (round SHEETS: what the layer lets through from behind it)
            behind->resize(bytes);
            read = state.memory.read_image_level(images.behind.front(), 0, behind);
        }
    }
    if (layer != nullptr) { // (round FRONT2: Ray Reconstruction's transparency layer as the transparencies drew it)
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::Image* const image = render::vulkan::surface_light_layer(state);
        layer->resize(static_cast<std::size_t>(kTargetWidth) * kTargetHeight * 8);
        read = read && image != nullptr && render::vulkan::finish_work(state) &&
               state.memory.read_image_level(image, 0, layer);
    }
    if (colour != nullptr) {
        read = read && render::vulkan::read_surface(targets.colour, colour);
    }
    gpu.end_scene();
    gpu.present();
    return read;
}

void run_front_layer(render::Gpu& gpu, std::span<const SceneMesh> meshes, Tally* tally)
{
    namespace vk = render::vulkan;
    text::print("round FL - the front layer (see-through surfaces lit by the light list, Lumen's front layer):\n");
    vk::RtLightSettings lights = settings_for(true, 4);
    lights.direct = vk::RtDirectLight::kRestir;
    vk::set_rt_light_settings(lights);
    vk::set_rt_shadow_settings({});
    vk::set_hdr_settings({.on = true});
    const bool hdr_before = rt::hdr_colour();
    rt::set_hdr_colour(true);
    // (the frame's point light switched on by the lighting state: the level's own lamps on, no clock - as the fog's case)
    rt::LevelEmitters lamps_on;
    lamps_on.serial = 1;
    lamps_on.own_lamps = 1.0F;
    rt::set_level_emitters(std::move(lamps_on));
    rt::set_day_light({});
    const std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    rt::set_cel_world({.relit = true, .sun = {0.0F, 0.0F, 0.0F}, .toward = up});
    const render::GpuSurfaceDesc colour{.width = kTargetWidth,
                                        .height = kTargetHeight,
                                        .format = value::kFormatA16B16G16R16F,
                                        .samples = 0,
                                        .quality = 0,
                                        .lockable_or_discard = true};
    render::GpuSurfaceDesc depth = colour;
    depth.format = value::kFormatD24S8;
    depth.lockable_or_discard = false;
    Targets targets;
    bool ok = gpu.create_render_target(colour, &targets.colour) == value::kResultOk &&
              gpu.create_depth_surface(depth, &targets.depth) == value::kResultOk && lit_glow_draw().texture != nullptr &&
              lit_glow_draw().shader != nullptr;
    const std::array<rt::Light, 1> points{{{.kind = rt::Light::Kind::kPoint,
                                            .colour = {1.0F, 0.9F, 0.7F},
                                            .position = floats(kFrontLamp),
                                            .radius = static_cast<float>(kFrontReach)}}};
    // without ReSTIR GI: no list, no layer lit
    vk::set_rt_gi_settings({.indirect = vk::RtIndirectLight::kPasses});
    const uint64_t passes_before = vk::front_layer_stats().passes;
    uint64_t frame = 5000;
    for (int k = 0; ok && k < 3; ++k) {
        ok = draw_front(gpu, targets, meshes, points, true, frame++, nullptr, nullptr);
    }
    tally->check(ok && vk::front_layer_stats().passes == passes_before,
                 text::format("  indirect_light passes: no front layer lit (%llu passes)",
                              static_cast<unsigned long long>(vk::front_layer_stats().passes - passes_before)));
    // with it: the frames for the sky's grid and the layer's history, then the layer and the floor's lamps read
    vk::set_rt_gi_settings({.indirect = vk::RtIndirectLight::kRestir});
    std::vector<uint8_t> front;
    std::vector<uint8_t> lamps;
    std::vector<uint8_t> with_glass;
    std::vector<uint8_t> without_glass;
    std::vector<uint8_t> behind;
    for (int k = 0; ok && k < kFrontFrames; ++k) {
        const bool last = k + 1 == kFrontFrames;
        ok = draw_front(gpu, targets, meshes, points, true, frame++, last ? &front : nullptr, last ? &lamps : nullptr,
                        last ? &with_glass : nullptr, false, nullptr, last ? &behind : nullptr);
    }
    ok = ok && draw_front(gpu, targets, meshes, points, false, frame++, nullptr, nullptr, &without_glass);
    const uint64_t lit = vk::front_layer_stats().passes - passes_before;
    tally->check(ok && lit > 0, text::format("  the layer lit (%llu passes of %d frames)", static_cast<unsigned long long>(lit),
                                             kFrontFrames));
    // the model's light at a point facing up
    rt::Emitter lamp;
    const bool emitter = rt::lights::point_emitter(core::at(points, 0), std::clamp(rt::open_sky_lamps(rt::day_light(), rt::level_emitters().get()), 0.0F, 1.0F), &lamp);
    const rt::LampShape shape{.position = lamp.position,
                              .shape = lamp.shape,
                              .axis = lamp.axis,
                              .half_length = lamp.half_length,
                              .core = lamp.core,
                              .reach = lamp.reach};
    const double exposure = rt::frame_pre_exposure();
    const auto model = [&](const Vec3& at) {
        std::array<float, 3> toward{};
        const double g = rt::lamp_geometry(shape, floats(at), up, &toward);
        return Rgb{core::at(lamp.colour, 0) * lamp.intensity * g * exposure, core::at(lamp.colour, 1) * lamp.intensity * g * exposure,
                   core::at(lamp.colour, 2) * lamp.intensity * g * exposure};
    };
    text::print("  (the lamp: %s, intensity %.4f, reach %.0f, core %.1f; the pre-exposure %.4g; at the glass's middle %.4g)\n",
                emitter ? "made" : "none", static_cast<double>(lamp.intensity), static_cast<double>(lamp.reach),
                static_cast<double>(lamp.core), exposure, emitter ? luma_of(model(Vec3{80.0, kFrontGlassHeight, -15.0})) : 0.0);
    const Solid& box = core::at(kScene, 1);
    double glass_got = 0.0;
    double glass_want = 0.0;
    double behind_got = 0.0;  // (round SHEETS) the layer's other side's light, and the floor's there
    double behind_want = 0.0;
    int behind_far = 0;       // ... pixels whose traced distance isn't the floor's
    double behind_worst = 0.0;
    double floor_got = 0.0;
    double floor_want = 0.0;
    int glass_pixels = 0;
    int floor_pixels = 0;
    int shaded_wrong = 0;
    const double x0 = core::at(kFrontGlass, 0);
    const double x1 = core::at(kFrontGlass, 1);
    const double z0 = core::at(kFrontGlass, 2);
    const double z1 = core::at(kFrontGlass, 3);
    for (int y = 0; ok && emitter && y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            Vec3 at{};
            if (through_quad(x, y, kFrontGlassHeight, x0, x1, z0, z1)) {
                if (!floor_point(kEye, x, y, kFrontGlassHeight, &at) || x_of(at) < x0 + kFrontMargin ||
                    x_of(at) > x1 - kFrontMargin || z_of(at) < z0 + kFrontMargin || z_of(at) > z1 - kFrontMargin) {
                    continue;
                }
                const double want = luma_of(model(at));
                if (want < kFrontLeast) {
                    continue;
                }
                const double layer = luma_of(linear_pixel_at(front, x, y));
                // (round SHEETS) the pane's other side traced on along the view: the floor under it, its distance from the
                // pane the view's to the floor, its light the floor's own there - its albedo x the lamps' light on it (the
                // same pixel's lamps input: the floor is the prepass's surface there)
                Vec3 floor_at{};
                if (floor_point(kEye, x, y, 0.0, &floor_at) && std::abs(x_of(floor_at)) < kFloorHalf - kEdgeMargin &&
                    std::abs(z_of(floor_at)) < kFloorHalf - kEdgeMargin) {
                    const double expected_travel = length_of(minus(floor_at, at));
                    const double travel = half_alpha_at(behind, x, y);
                    behind_worst = std::max(behind_worst, std::abs(travel - expected_travel));
                    behind_far += std::abs(travel - expected_travel) > 0.01 * expected_travel + 0.5 ? 1 : 0;
                    behind_got += luma_of(linear_pixel_at(behind, x, y));
                    behind_want += render::rt::linear_from_display(0.8) * luma_of(linear_pixel_at(lamps, x, y));
                }
                glass_got += layer;
                glass_want += want;
                ++glass_pixels;
                // the pane drawn with it: its white albedo's diffuse lobe x the layer's light - (round BIAS) A (1 - E),
                // E the specular's directional albedo for the painted kind at the pixel's N.V - a quarter over the
                // floor behind
                const double drawn = luma_of(linear_pixel_at(with_glass, x, y));
                const double behind = luma_of(linear_pixel_at(without_glass, x, y));
                const auto pane_nv = static_cast<float>(y_of(normalised(minus(kEye, at))));
                const rt::brdf::Rgb pane_e = rt::brdf::specular_albedo(
                    rt::material_f0(rt::MaterialKind::kPainted), pane_nv, rt::material_values(rt::MaterialKind::kPainted).roughness);
                const double expected =
                    (kFrontGlassAlpha * (1.0 - core::at(pane_e, 1)) * layer) + ((1.0 - kFrontGlassAlpha) * behind);
                shaded_wrong += std::abs(drawn - expected) > std::max(0.002, 0.03 * expected) ? 1 : 0;
                continue;
            }
            // the floor where the box neither stands nor shadows nor reflects much (away from it), in the open
            if (!floor_point(kEye, x, y, 0.0, &at) || std::abs(x_of(at)) > kFloorHalf - kEdgeMargin ||
                std::abs(z_of(at)) > kFloorHalf - kEdgeMargin) {
                continue;
            }
            const Seen seen = seen_at(view_from(kEye), x + 0.5, y + 0.5);
            const Vec3 to_lamp = minus(kFrontLamp, at);
            const Hit blocked = intersect(box, Vec3{x_of(at), 0.5, z_of(at)}, normalised(to_lamp));
            if (seen.solid != 1 || blocked.distance < length_of(to_lamp) ||
                (x_of(at) > core::at(box.low, 0) - kEdgeMargin && x_of(at) < core::at(box.high, 0) + kEdgeMargin &&
                 z_of(at) > core::at(box.low, 2) - kEdgeMargin && z_of(at) < core::at(box.high, 2) + kEdgeMargin)) {
                continue;
            }
            const double want = luma_of(model(at));
            if (want < kFrontLeast) {
                continue;
            }
            floor_got += luma_of(linear_pixel_at(lamps, x, y));
            floor_want += want;
            ++floor_pixels;
        }
    }
    const double glass_ratio = glass_want > 0.0 ? glass_got / glass_want : 0.0;
    const double behind_ratio = behind_want > 0.0 ? behind_got / behind_want : 0.0;
    tally->check(ok && glass_pixels > 500 && behind_far == 0,
                 text::format("  (round SHEETS) the pane's other side traced on along the view: its hit the floor under it at "
                              "every pixel within the floor - %d of %d off (worst %.3f units)",
                              behind_far, glass_pixels, behind_worst));
    tally->check(ok && glass_pixels > 500 && behind_ratio >= kBehindRatioLow && behind_ratio <= kBehindRatioHigh,
                 text::format("  ... its light the floor's there (its albedo x the lamps' light on it): %.4f x",
                              behind_ratio));
    const double floor_ratio = floor_want > 0.0 ? floor_got / floor_want : 0.0;
    tally->check(ok && emitter && glass_pixels > 500 && glass_ratio >= kFrontRatioLow && glass_ratio <= kFrontRatioHigh,
                 text::format("  the glass pane (the front layer) under the lamp: %d pixels, its light %.4f x the lamp "
                              "model's",
                              glass_pixels, glass_ratio));
    // (the floor behind is read from a separate frame: since ReSTIR DI traces one light ray a pixel with RTXDI's basic
    // bias correction - round BUDGET - its per-pixel frame-to-frame variation passes 3 % at a few pixels; ~4.6 % of them
    // measured. A wrong pane shading misses at most of them, so up to 8 % may be off.)
    tally->check(ok && glass_pixels > 500 && shaded_wrong * 100 <= glass_pixels * 8,
                 text::format("  ... and the pane drawn by it (its albedo x the layer's light over the floor behind): %d of %d "
                              "pixels off",
                              shaded_wrong, glass_pixels));
    tally->check(ok && floor_pixels > 500 && floor_ratio >= kFrontRatioLow && floor_ratio <= kFrontRatioHigh,
                 text::format("  the opaque floor under the same lamp (ReSTIR DI's lamps input): %d pixels, %.4f x the "
                              "model's - the glass / the floor %.4f",
                              floor_pixels, floor_ratio, floor_ratio > 0.0 ? glass_ratio / floor_ratio : 0.0));
    // (round FRONT2) the same pane drawn as a particle (rt::set_particles_drawing - a colour lit already, kLightShare):
    // in the front layer where it covers, its light there the lamp model's as the glass's; drawn as its albedo (the
    // program's under v0 0.5, not its dark vertex colour) x the layer's light over the floor behind by its alpha
    std::vector<uint8_t> particle_front;
    std::vector<uint8_t> with_particle;
    for (int k = 0; ok && k < kFrontFrames; ++k) {
        const bool last = k + 1 == kFrontFrames;
        ok = draw_front(gpu, targets, meshes, points, true, frame++, last ? &particle_front : nullptr, nullptr,
                        last ? &with_particle : nullptr, true);
    }
    const double particle_albedo = rt::linear_from_display(static_cast<float>(4.0 * 0.5 * kParticleFrontC0));
    double particle_got = 0.0;
    double particle_want = 0.0;
    int particle_pixels = 0;
    int particle_wrong = 0;
    for (int y = 0; ok && emitter && y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            Vec3 at{};
            if (!through_quad(x, y, kFrontGlassHeight, x0, x1, z0, z1) || !floor_point(kEye, x, y, kFrontGlassHeight, &at) ||
                x_of(at) < x0 + kFrontMargin || x_of(at) > x1 - kFrontMargin || z_of(at) < z0 + kFrontMargin ||
                z_of(at) > z1 - kFrontMargin) {
                continue;
            }
            const double want = luma_of(model(at));
            if (want < kFrontLeast) {
                continue;
            }
            const double layer = luma_of(linear_pixel_at(particle_front, x, y));
            particle_got += layer;
            particle_want += want;
            ++particle_pixels;
            const double drawn = luma_of(linear_pixel_at(with_particle, x, y));
            const double behind = luma_of(linear_pixel_at(without_glass, x, y));
            const double expected = (kFrontGlassAlpha * particle_albedo * layer) + ((1.0 - kFrontGlassAlpha) * behind);
            particle_wrong += std::abs(drawn - expected) > std::max(0.002, 0.03 * expected) ? 1 : 0;
        }
    }
    // the same frames with the transparencies drawn last into Ray Reconstruction's transparency layer (as in the game with
    // it on): the particle recorded again there is still a particle - its layer pixel its alpha x its albedo x the front
    // layer's light (premultiplied), not the game's light on it (its dark vertex colour)
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        vk::set_surface_light_layer_forced(true);
    }
    std::vector<uint8_t> particle_layer;
    for (int k = 0; ok && k < 4; ++k) {
        ok = draw_front(gpu, targets, meshes, points, true, frame++, nullptr, nullptr, nullptr, true,
                        k == 3 ? &particle_layer : nullptr);
    }
    {
        vk::Backend& state = vk::backend();
        const std::scoped_lock lock(state.mutex);
        vk::set_surface_light_layer_forced(false);
    }
    int layered_pixels = 0;
    int layered_wrong = 0;
    for (int y = 0; ok && emitter && y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            Vec3 at{};
            if (!through_quad(x, y, kFrontGlassHeight, x0, x1, z0, z1) || !floor_point(kEye, x, y, kFrontGlassHeight, &at) ||
                x_of(at) < x0 + kFrontMargin || x_of(at) > x1 - kFrontMargin || z_of(at) < z0 + kFrontMargin ||
                z_of(at) > z1 - kFrontMargin || luma_of(model(at)) < kFrontLeast) {
                continue;
            }
            const double expected = kFrontGlassAlpha * particle_albedo * luma_of(linear_pixel_at(particle_front, x, y));
            const double drawn = luma_of(linear_pixel_at(particle_layer, x, y));
            ++layered_pixels;
            layered_wrong += std::abs(drawn - expected) > std::max(0.002, 0.05 * expected) ? 1 : 0;
        }
    }
    tally->check(ok && layered_pixels > 500 && layered_wrong * 100 <= layered_pixels * 8,
                 text::format("  ... and drawn into Ray Reconstruction's transparency layer (round FRONT2: recorded again "
                              "there, still a particle - its alpha x albedo x the front layer's light): %d of %d pixels off",
                              layered_wrong, layered_pixels));
    const double particle_ratio = particle_want > 0.0 ? particle_got / particle_want : 0.0;
    tally->check(ok && emitter && particle_pixels > 500 && particle_ratio >= kFrontRatioLow &&
                     particle_ratio <= kFrontRatioHigh,
                 text::format("  a particle pane (round FRONT: the front layer where it covers): %d pixels, its light %.4f x "
                              "the lamp model's",
                              particle_pixels, particle_ratio));
    tally->check(ok && particle_pixels > 500 && particle_wrong * 100 <= particle_pixels * 8,
                 text::format("  ... and drawn by it (the program's albedo under v0 0.5, %.3f, x the layer's light over the "
                              "floor behind - not its vertex colour): %d of %d pixels off",
                              particle_albedo, particle_wrong, particle_pixels));
    vk::set_rt_gi_settings({.indirect = vk::RtIndirectLight::kPasses});
    vk::set_rt_light_settings({});
    rt::set_level_emitters({});
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    for (GpuSurface* surface : {targets.colour, targets.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
    rt::set_cel_world({});
    vk::set_hdr_settings({});
    rt::set_hdr_colour(hdr_before);
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
                                         .ray_tracing = true,
                                         // (round OM) no opacity micromaps: the cut-out box is drawn solid in the
                                         // raster while its texels are alpha 0 for the rays (a fixture: the game's
                                         // raster discards them too) - a micromap rightly drops it for every ray, the
                                         // "everything solid" ones too (vulkan_rt_micromaps.h; rt_check covers them)
                                         .opacity_micromaps = false},
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
    // (round GRIT: `auto` is ReSTIR GI now, with or without Ray Reconstruction - the cases that name no tier are the
    // passes')
    render::vulkan::set_rt_gi_settings({.indirect = render::vulkan::RtIndirectLight::kPasses});
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
        run_strip(gpu, meshes, tally, false); // (round SHEETS)
        run_strip(gpu, meshes, tally, true);  // (round PANES)
        run_cutout(gpu, meshes, tally); // (added for the cut-outs)
        run_blended(gpu, meshes, tally); // (round PT-B: blended surfaces in every ray)
        run_off(gpu, meshes, tally);
        run_time_of_day(gpu, meshes, tally); // (added by the time of day)
        run_many(gpu, meshes, tally);        // (added for the washed-out streets)
        run_counted_once(gpu, meshes, tally); // (added by the light energy audit)
        run_ao_direct(gpu, meshes, tally);    // (round O)
        run_per_surface(gpu, meshes, tally);  // (round V)
        run_fog_lamp(gpu, meshes, tally);     // (added by the volumetric light)
        run_fog_lamp_night(gpu, meshes, tally); // (added by the street air)
        run_fog_noise(gpu, meshes, tally);      // (round NK)
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
    check_light_cards(&tally); // (round light cards)
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
