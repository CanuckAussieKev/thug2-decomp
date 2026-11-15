// NOT ORIGINAL - thug2_vulkan_paint_check.exe: the paint trails (engine/gfx/paint_trails.h - the strokes, the splatter, the
// fields; gpu_vulkan/vulkan_paint.h - the windows and the coat on the world's draws) tried on this machine.
//
// On the processor (no device needed):
//   - the combo's curve: 0 at none, rising with the score (log-scaled: a small combo shows, a huge one keeps growing),
//     1 at kFullCombo;
//   - a slim line: a skater riding the ground with no combo coats a board-wide line along its path (in the field seen
//     from above, at the ground's height) and nothing beyond;
//   - the combo: the same ride with a growing combo - the line wider and the splatter (coat beyond the line, drops
//     counted) more as the combo grows;
//   - a bail: the paint kept, texel for texel, through 300 steps of a bail (no contact) and after it;
//   - a rail: a grind's stripe narrow, on the rail's top (its place 40 units over the ground) - the ground's own texels
//     under it untouched where they were painted before;
//   - a wall: a wallride coats the field seen from the side (its place the wall's), nothing from above;
//   - the same steps twice (cleared between) give the same fields, byte for byte; a jump (a restart) draws no line between;
//   - a remote player's landed combo: a burst round it; the colours: the slot's, the chosen one for this machine's player.
// On the device (a 320x240 16-bit float target with a depth surface; the camera 60 units up looking 35 degrees down):
//   - the windows: the three fields' windows read back hold exactly the processor's coats and places, again after the
//     camera moves 7 tiles - and only the tiles that came in copied, nothing the next frame;
//   - the coat: a dark ground drawn with the game's single-texture material shader and marked as the ground takes the
//     paint's colour on the line, stays dark beside it, under a rail's stripe (its place 30 units up) and when the draw
//     isn't marked or paint is off; a wall standing on the ground takes a wallride's coat;
//   - the validation layer (with synchronization validation) says nothing all along;
//   - the cost at 2560x1440: the ground and a wall drawn with the coat against without, a session's paint in the windows.
//   thug2_vulkan_paint_check [--device N] [--display N] [--no-validation]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan for this process.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <bit>
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
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/paint_trails.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/surface_trail.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_paint.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuRenderState;
using render::GpuSamplerState;
using render::GpuStageState;
using render::GpuSurface;
using render::GpuTexture;
namespace value = render::gpu_value;
namespace vk = render::vulkan;
namespace paint = render::paint;
namespace trail = render::surface_trail;
using Vec3 = std::array<float, 3>;
using Matrix = std::array<std::array<float, 4>, 4>;

constexpr int kWindowSize = 128;
constexpr uint32_t kWidth = 320;
constexpr uint32_t kHeight = 240;
constexpr int kBitsPerByte = 8;
constexpr std::size_t kHalfPixelBytes = 8;
constexpr uint32_t kHighByte = 8;
constexpr uint16_t kHalfSign = 0x8000;
constexpr float kByteMax = 255.0F;
constexpr uint32_t kByte = 0xFFU;
constexpr uint32_t kAlphaShift = 24;
constexpr float kVerticalScale = 1.7320508F; // 1 / tan(30 degrees)
constexpr float kNear = 1.0F;
constexpr float kFar = 4000.0F;
constexpr float kPitch = 0.61086524F; // 35 degrees down
constexpr Vec3 kCamera{0.0F, 60.0F, -100.0F};
constexpr float kHalf = 0.5F;
constexpr std::size_t kMatrixFloats = 16;
constexpr std::size_t kQuadCorners = 6;
constexpr uint32_t kFvfPosition = 0x002;
constexpr uint32_t kFvfDiffuse = 0x040;
constexpr uint32_t kFvfTex1 = 0x100;
constexpr uint32_t kHalfGrey = 0x80808080U; // (the vertex colour 128: 1 after the shader's doubling)
constexpr uint32_t kSkyColour = 0xFF203040U;
constexpr std::string_view kShader = "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\nmul_x4 r0.rgb,v0,r0\n";
constexpr double kStep = 1.0 / 60.0;
constexpr uint64_t kLevel = 1;
constexpr std::size_t kAxisX = 0;
constexpr std::size_t kAxisY = 1;
constexpr std::size_t kAxisZ = 2;

// The device's scene (world units).
constexpr float kGround = 2000.0F;
constexpr float kWallZ = 250.0F;     // a wall standing on the ground (drawn), x -60 - 60, up to kWallTop, facing the camera
constexpr float kWallRide = 12.0F;   // a wallride along it this high (the camera sees the wall up to about 29)
constexpr float kWallSide = 60.0F;
constexpr float kWallTop = 80.0F;
constexpr float kLineZ = 60.0F;      // the line on the ground: along x at this z
constexpr float kRailZ = 150.0F;     // a rail's stripe along x at this z, kRailTop up (no rail drawn: the ground under it)
constexpr float kRailTop = 30.0F;

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
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
        magnitude = mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    } else {
        magnitude = std::ldexp(static_cast<float>(mantissa + kImplicitOne), static_cast<int>(exponent) - kPowerBias);
    }
    return (bits & kHalfSign) != 0 ? -magnitude : magnitude;
}

float dot(const Vec3& a, const Vec3& b)
{
    return (core::at(a, 0) * core::at(b, 0)) + (core::at(a, 1) * core::at(b, 1)) + (core::at(a, 2) * core::at(b, 2));
}

// --- the processor ----------------------------------------------------------------------------------------------------

// A skater's ride: `steps` steps from `from` along `direction` at `speed`, on `contact` facing `normal`, its combo
// `combo` (a function of the step: a growing combo).
template <typename Combo>
void ride(const Vec3& from, const Vec3& direction, float speed, int steps, paint::Contact contact, const Vec3& normal, Combo combo,
          uint64_t id = 1)
{
    Vec3 at = from;
    for (int step = 0; step < steps; ++step) {
        const paint::Brush brush{.id = id,
                                 .slot = static_cast<int>(id - 1),
                                 .own = id == 1,
                                 .contact = contact,
                                 .at = at,
                                 .normal = normal,
                                 .forward = direction,
                                 .combo = combo(step)};
        paint::step(std::span(&brush, 1), kStep, kLevel);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            core::at(at, axis) += core::at(direction, axis) * speed * static_cast<float>(kStep);
        }
    }
}

void ride_ground(float z, float combo, uint64_t id = 1, float from_x = -300.0F, int steps = 60)
{
    ride({from_x, 0.0F, z}, {1.0F, 0.0F, 0.0F}, 600.0F, steps, paint::Contact::kGround, {0.0F, 1.0F, 0.0F},
         [combo](int /*step*/) { return combo; }, id);
}

// A field's texel holding a world point (the coat's word and the place).
struct Texel {
    uint32_t coat = 0;
    float place = 0.0F;
    [[nodiscard]] float cover() const { return static_cast<float>((coat >> kAlphaShift) & kByte) / kByteMax; }
};
Texel texel_at(std::size_t axis, const Vec3& point)
{
    Texel found;
    paint::read_fields([&](std::span<const trail::Field* const, paint::kAxes> fields) {
        const trail::Field& field = *core::at(fields, axis);
        const trail::Point uv = paint::plane_of(axis, point);
        const std::array<int, 2> at = field.texel_of(core::at(uv, 0), core::at(uv, 1));
        found.coat = field.texel_at(core::at(at, 0), core::at(at, 1), paint::kCoatPlane);
        found.place = std::bit_cast<float>(field.texel_at(core::at(at, 0), core::at(at, 1), paint::kDepthPlane));
    });
    return found;
}

// Every texel of every field, in order (the same fields: the same words).
std::vector<uint32_t> all_texels()
{
    std::vector<uint32_t> out;
    paint::read_fields([&out](std::span<const trail::Field* const, paint::kAxes> fields) {
        for (const trail::Field* field : fields) {
            out.push_back(static_cast<uint32_t>(field->tile_count()));
            field->read_tiles([&out](const trail::Tile& tile) {
                out.push_back(static_cast<uint32_t>(tile.x));
                out.push_back(static_cast<uint32_t>(tile.z));
                out.insert(out.end(), tile.texels.begin(), tile.texels.end());
            });
        }
    });
    return out;
}

// The coated texels of a field (cover over a half) and those beyond a line's half width round z = `z` (the splatter).
struct Coverage {
    std::size_t coated = 0;
    std::size_t beyond = 0;
    float widest = 0.0F; // the furthest coated texel from the line (units)
};
Coverage coverage(float z, float half)
{
    Coverage out;
    paint::read_fields([&](std::span<const trail::Field* const, paint::kAxes> fields) {
        const trail::Field& field = *core::at(fields, kAxisY);
        field.read_tiles([&](const trail::Tile& tile) {
            const std::span<const uint32_t> coats = tile.plane(paint::kCoatPlane);
            for (std::size_t i = 0; i < coats.size(); ++i) {
                if (((core::at(coats, i) >> kAlphaShift) & kByte) < kByte / 2) {
                    continue;
                }
                const auto row = static_cast<int>(i / trail::kTileTexels);
                const float texel_z = (static_cast<float>((tile.z * trail::kTileTexels) + row) + kHalf) * field.texel();
                const float off = std::abs(texel_z - z);
                ++out.coated;
                out.beyond += off > half + field.texel() ? 1U : 0U;
                out.widest = std::max(out.widest, off);
            }
        });
    });
    return out;
}

void check_curve(Tally* tally)
{
    const float none = paint::combo_size(0.0F);
    const float small = paint::combo_size(1000.0F);
    const float medium = paint::combo_size(100000.0F);
    const float huge = paint::combo_size(1000000.0F);
    const float full = paint::combo_size(paint::kFullCombo);
    tally->check(none == 0.0F && small > 0.1F && small < medium && medium < huge && huge < full && std::abs(full - 1.0F) < 1.0e-4F,
                 text::format("the combo's size: 0, %.2f at 1000, %.2f at 100,000, %.2f at 1,000,000, 1 at the full combo",
                              static_cast<double>(small), static_cast<double>(medium), static_cast<double>(huge)));
    tally->check(paint::colour_from_text("") == 0 && paint::colour_from_text("cyan") == paint::slot_colour(1) &&
                     paint::colour_from_text("#12ab34") == 0x12AB34U && paint::colour_from_text("12AB3") == 0,
                 "the colour's text: by player, a palette name, #RRGGBB; nonsense by player");
}

void check_lines(Tally* tally)
{
    paint::set_settings({.on = true});
    paint::clear();
    // a slim line: no combo
    ride_ground(0.0F, 0.0F);
    const Texel middle = texel_at(kAxisY, {0.0F, 0.0F, 0.0F});
    const Coverage slim = coverage(0.0F, paint::kBoardHalfWidth);
    tally->check(middle.cover() > 0.85F && std::abs(middle.place) < 0.01F && slim.beyond == 0 &&
                     slim.widest <= paint::kBoardHalfWidth + paint::kTexel && paint::counts().drops == 0,
                 text::format("no combo: a slim line on the ground (%zu texels, %.1f units from its middle at most), no splatter",
                              slim.coated, static_cast<double>(slim.widest)));
    tally->check(texel_at(kAxisX, {0.0F, 0.0F, 0.0F}).coat == 0 && texel_at(kAxisZ, {0.0F, 0.0F, 0.0F}).coat == 0,
                 "the ground's line in the field from above alone");
    // the combo widens and splatters it
    std::array<Coverage, 3> grown{};
    std::array<uint64_t, 3> drops{};
    constexpr std::array<float, 3> kScores{2000.0F, 60000.0F, 1500000.0F};
    for (std::size_t k = 0; k < kScores.size(); ++k) {
        paint::clear();
        ride_ground(0.0F, core::at(kScores, k));
        const float half = paint::kBoardHalfWidth + (paint::kComboHalfWidth * paint::combo_size(core::at(kScores, k)));
        core::at(grown, k) = coverage(0.0F, half);
        core::at(drops, k) = paint::counts().drops;
    }
    tally->check(slim.coated < core::at(grown, 0).coated && core::at(grown, 0).coated < core::at(grown, 1).coated &&
                     core::at(grown, 1).coated < core::at(grown, 2).coated,
                 text::format("wider with the combo: %zu texels, then %zu, %zu, %zu", slim.coated, core::at(grown, 0).coated,
                              core::at(grown, 1).coated, core::at(grown, 2).coated));
    tally->check(core::at(drops, 0) > 0 && core::at(drops, 0) < core::at(drops, 1) && core::at(drops, 1) < core::at(drops, 2) &&
                     core::at(grown, 2).beyond > core::at(grown, 0).beyond && core::at(grown, 2).widest > core::at(grown, 0).widest,
                 text::format("the splatter grows with it: %llu, %llu, %llu drops; %zu, %zu, %zu texels thrown past the line",
                              static_cast<unsigned long long>(core::at(drops, 0)), static_cast<unsigned long long>(core::at(drops, 1)),
                              static_cast<unsigned long long>(core::at(drops, 2)), core::at(grown, 0).beyond,
                              core::at(grown, 1).beyond, core::at(grown, 2).beyond));
    // a bail: the paint stays
    const std::vector<uint32_t> before = all_texels();
    ride({300.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 300.0F, 300, paint::Contact::kNone, {0.0F, 1.0F, 0.0F}, [](int) { return 0.0F; });
    tally->check(all_texels() == before, "a bail: the paint kept, texel for texel");
    // a rail: a narrow stripe on its top; the ground's line under it kept
    paint::clear();
    ride_ground(0.0F, 0.0F, 2);
    const Texel ground_before = texel_at(kAxisY, {20.0F, 0.0F, 0.0F});
    ride({-300.0F, 40.0F, 30.0F}, {1.0F, 0.0F, 0.0F}, 400.0F, 90, paint::Contact::kRail, {0.0F, 1.0F, 0.0F}, [](int) { return 0.0F; });
    const Texel stripe = texel_at(kAxisY, {0.0F, 40.0F, 30.0F});
    const Texel beside = texel_at(kAxisY, {0.0F, 40.0F, 30.0F + paint::kRailHalfWidth + (2.0F * paint::kTexel)});
    tally->check(stripe.cover() > 0.8F && std::abs(stripe.place - 40.0F) < 0.01F && beside.coat == 0,
                 text::format("a rail: a stripe on its top (its place %.1f), nothing beside it", static_cast<double>(stripe.place)));
    tally->check(texel_at(kAxisY, {20.0F, 0.0F, 0.0F}).coat == ground_before.coat, "the ground's own line kept");
    // a wall: the field from the side
    paint::clear();
    ride({-200.0F, 40.0F, kWallZ}, {1.0F, 0.0F, 0.0F}, 300.0F, 60, paint::Contact::kWall, {0.0F, 0.0F, -1.0F},
         [](int) { return 0.0F; });
    const Texel wall = texel_at(kAxisZ, {-100.0F, 40.0F, kWallZ});
    tally->check(wall.cover() > 0.8F && std::abs(wall.place - kWallZ) < 0.01F && texel_at(kAxisY, {-100.0F, 40.0F, kWallZ}).coat == 0,
                 text::format("a wallride: coated on the wall (its place %.1f), nothing from above", static_cast<double>(wall.place)));
    // a ramp's curve (45 degrees) in two fields
    paint::clear();
    constexpr float kRoot = 0.70710678F;
    ride({0.0F, 0.0F, -200.0F}, {0.0F, kRoot, kRoot}, 300.0F, 30, paint::Contact::kGround, {0.0F, kRoot, -kRoot}, [](int) { return 0.0F; });
    tally->check(texel_at(kAxisY, {0.0F, 50.0F, -150.0F}).cover() > 0.5F && texel_at(kAxisZ, {0.0F, 50.0F, -150.0F}).cover() > 0.5F,
                 "a 45-degree ramp: in the fields from above and the side");
    // the same steps twice
    const auto session = [] {
        paint::clear();
        ride_ground(0.0F, 0.0F);
        ride({-300.0F, 0.0F, 100.0F}, {1.0F, 0.0F, 0.0F}, 600.0F, 120, paint::Contact::kGround, {0.0F, 1.0F, 0.0F},
             [](int step) { return 500.0F * static_cast<float>(step); }, 2);
        ride({-200.0F, 40.0F, kWallZ}, {1.0F, 0.0F, 0.0F}, 300.0F, 60, paint::Contact::kWall, {0.0F, 0.0F, -1.0F},
             [](int) { return 300000.0F; }, 3);
        return all_texels();
    };
    const std::vector<uint32_t> first = session();
    const paint::Counts first_counts = paint::counts();
    tally->check(session() == first && paint::counts().drops == first_counts.drops && first_counts.drops > 0,
                 text::format("the same steps twice: the same paint, byte for byte (%llu drops)",
                              static_cast<unsigned long long>(first_counts.drops)));
    // a jump: no line between
    paint::clear();
    ride_ground(0.0F, 0.0F, 1, -300.0F, 10);
    ride_ground(0.0F, 0.0F, 1, 600.0F, 10);
    tally->check(texel_at(kAxisY, {300.0F, 0.0F, 0.0F}).coat == 0, "a restart: no line between where it was and where it is");
    // a remote player's landed combo: a burst
    paint::clear();
    const paint::Brush landed{.id = 7, .slot = 6, .contact = paint::Contact::kNone, .at = {0.0F, 0.0F, 300.0F}, .landed = 50000.0F};
    paint::step(std::span(&landed, 1), kStep, kLevel);
    tally->check(paint::counts().drops >= 10, text::format("a remote player's landed combo: a burst (%llu drops)",
                                                           static_cast<unsigned long long>(paint::counts().drops)));
    // the colours
    paint::clear();
    ride_ground(0.0F, 0.0F, 2);
    const uint32_t second = texel_at(kAxisY, {0.0F, 0.0F, 0.0F}).coat;
    paint::set_settings({.on = true, .colour = 0x00FF00U});
    paint::clear();
    ride_ground(0.0F, 0.0F, 1);
    const uint32_t chosen = texel_at(kAxisY, {0.0F, 0.0F, 0.0F}).coat;
    constexpr uint32_t kGreenShift = 8;
    tally->check(((second >> kGreenShift) & kByte) > 150 && (chosen & kByte) < 10 && ((chosen >> kGreenShift) & kByte) > 200,
                 "the colours: player 2's cyan, the chosen green for this machine's player");
    paint::set_settings({.on = true});
    paint::clear();
}

// --- the device -----------------------------------------------------------------------------------------------------

struct Axes {
    Vec3 right{1.0F, 0.0F, 0.0F};
    Vec3 up{};
    Vec3 forward{};
};
Axes camera_axes()
{
    return {.right = {1.0F, 0.0F, 0.0F},
            .up = {0.0F, std::cos(kPitch), std::sin(kPitch)},
            .forward = {0.0F, -std::sin(kPitch), std::cos(kPitch)}};
}

Matrix view_matrix(const Vec3& camera)
{
    const Axes axes = camera_axes();
    Matrix view{};
    for (std::size_t row = 0; row < 3; ++row) {
        core::at(core::at(view, row), 0) = core::at(axes.right, row);
        core::at(core::at(view, row), 1) = core::at(axes.up, row);
        core::at(core::at(view, row), 2) = core::at(axes.forward, row);
    }
    core::at(view, 3) = {-dot(axes.right, camera), -dot(axes.up, camera), -dot(axes.forward, camera), 1.0F};
    return view;
}
Matrix projection_matrix(uint32_t width, uint32_t height)
{
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    Matrix projection{};
    core::at(core::at(projection, 0), 0) = kVerticalScale / aspect;
    core::at(core::at(projection, 1), 1) = kVerticalScale;
    core::at(core::at(projection, 2), 2) = kFar / (kFar - kNear);
    core::at(core::at(projection, 2), 3) = 1.0F;
    core::at(core::at(projection, 3), 2) = -kNear * kFar / (kFar - kNear);
    return projection;
}
std::array<float, kMatrixFloats> flat(const Matrix& matrix)
{
    std::array<float, kMatrixFloats> out{};
    for (std::size_t i = 0; i < kMatrixFloats; ++i) {
        core::at(out, i) = core::at(core::at(matrix, i / 4), i % 4);
    }
    return out;
}

// A world point's pixel (kWidth x kHeight, the camera at kCamera).
std::array<uint32_t, 2> pixel_of(const Vec3& point)
{
    const Axes axes = camera_axes();
    const Vec3 d{core::at(point, 0) - core::at(kCamera, 0), core::at(point, 1) - core::at(kCamera, 1),
                 core::at(point, 2) - core::at(kCamera, 2)};
    const float depth = dot(d, axes.forward);
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    const float ndc_x = dot(d, axes.right) * kVerticalScale / aspect / depth;
    const float ndc_y = dot(d, axes.up) * kVerticalScale / depth;
    return {static_cast<uint32_t>(std::clamp((ndc_x + 1.0F) * kHalf * static_cast<float>(kWidth), 0.0F, static_cast<float>(kWidth - 1))),
            static_cast<uint32_t>(
                std::clamp((1.0F - ndc_y) * kHalf * static_cast<float>(kHeight), 0.0F, static_cast<float>(kHeight - 1)))};
}

struct Frame {
    Vec3 camera = kCamera;
    uint32_t width = kWidth;
    uint32_t height = kHeight;
    bool marked = true; // the draws marked as the ground
};

render::rt::ViewDrawn view_of(const Frame& frame)
{
    render::rt::ViewDrawn view;
    view.rect = {0, 0, frame.width, frame.height};
    view.output_rect = view.rect;
    view.depth_range = {0.0F, 1.0F};
    view.camera.view = view_matrix(frame.camera);
    view.camera.projection = projection_matrix(frame.width, frame.height);
    view.camera.position = frame.camera;
    return view;
}

struct Vertex {
    std::array<float, 3> position;
    uint32_t colour;
    std::array<float, 2> uv;
};

struct Scene {
    GpuSurface* colour = nullptr;
    GpuSurface* depth = nullptr;
    GpuTexture* dark = nullptr;
    render::GpuPixelShader* shader = nullptr;
    uint32_t width = kWidth;
    uint32_t height = kHeight;
};

GpuTexture* uniform_texture(render::Gpu& gpu, float level)
{
    constexpr uint32_t kTexels = 4;
    GpuTexture* texture = nullptr;
    if (gpu.create_texture({.width = kTexels, .height = kTexels, .levels = 1, .usage = 0, .format = value::kFormatA8R8G8B8,
                            .pool = value::kPoolManaged},
                           &texture) != value::kResultOk) {
        return nullptr;
    }
    render::GpuLockedRect locked{};
    gpu.lock_texture(texture, 0, &locked, nullptr, 0);
    const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked.bits),
                                   (static_cast<std::size_t>(locked.pitch) * (kTexels - 1)) + (std::size_t{kTexels} * 4));
    const auto byte = static_cast<uint8_t>(std::lround(level * kByteMax));
    constexpr uint8_t kOpaqueHalf = 128; // (the doubled alpha: 1)
    for (uint32_t row = 0; row < kTexels; ++row) {
        for (uint32_t column = 0; column < kTexels; ++column) {
            const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(locked.pitch)) + (std::size_t{column} * 4);
            core::at(bytes, at) = byte;
            core::at(bytes, at + 1) = byte;
            core::at(bytes, at + 2) = byte;
            core::at(bytes, at + 3) = kOpaqueHalf;
        }
    }
    gpu.unlock_texture(texture, 0);
    return texture;
}

void common_states(render::Gpu& gpu, const Scene& scene, const Frame& frame)
{
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaTestEnable, 0);
    gpu.set_render_state(GpuRenderState::kFogEnable, 0);
    gpu.set_render_state(GpuRenderState::kSpecularEnable, 0);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kZFunc, value::kCompareLessEqual);
    const std::array<float, kMatrixFloats> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    gpu.set_transform(render::GpuTransform::kWorld, identity.data());
    const std::array<float, kMatrixFloats> view = flat(view_matrix(frame.camera));
    gpu.set_transform(render::GpuTransform::kView, view.data());
    const std::array<float, kMatrixFloats> projection = flat(projection_matrix(frame.width, frame.height));
    gpu.set_transform(render::GpuTransform::kProjection, projection.data());
    for (const GpuSamplerState state : {GpuSamplerState::kMinFilter, GpuSamplerState::kMagFilter}) {
        gpu.set_sampler_state(0, state, value::kFilterLinear);
    }
    gpu.set_sampler_state(0, GpuSamplerState::kMipFilter, value::kFilterNone);
    gpu.set_stage_state(0, GpuStageState::kTexCoordIndex, 0);
    gpu.set_pixel_shader(scene.shader);
    const std::array<float, 4> c0{0.5F, 0.5F, 0.5F, 0.5F};
    gpu.set_pixel_constants(0, c0.data(), 1);
    gpu.set_vertex_format(kFvfPosition | kFvfDiffuse | kFvfTex1);
    gpu.set_texture(0, scene.dark);
}

// The frame's draws: the ground and the wall - marked as the ground or not.
void draw_world(render::Gpu& gpu, const Scene& scene, const Frame& frame)
{
    common_states(gpu, scene, frame);
    const auto at = [](float x, float y, float z) { return Vertex{.position = {x, y, z}, .colour = kHalfGrey, .uv = {x / 64.0F, z / 64.0F}}; };
    constexpr float kNearZ = -60.0F;
    const float x0 = core::at(frame.camera, 0) - kGround;
    const float x1 = core::at(frame.camera, 0) + kGround;
    const float z0 = core::at(frame.camera, 2) + kNearZ;
    const float z1 = core::at(frame.camera, 2) + kGround;
    const std::array<Vertex, kQuadCorners> ground{at(x0, 0.0F, z0), at(x1, 0.0F, z0), at(x0, 0.0F, z1),
                                                  at(x1, 0.0F, z0), at(x1, 0.0F, z1), at(x0, 0.0F, z1)};
    trail::set_current_ground(frame.marked);
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, ground.data(), sizeof(Vertex));
    const std::array<Vertex, kQuadCorners> wall{at(-kWallSide, 0.0F, kWallZ), at(kWallSide, 0.0F, kWallZ), at(-kWallSide, kWallTop, kWallZ),
                                                at(kWallSide, 0.0F, kWallZ), at(kWallSide, kWallTop, kWallZ), at(-kWallSide, kWallTop, kWallZ)};
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, wall.data(), sizeof(Vertex));
    trail::set_current_ground(false);
}

void draw_frame(render::Gpu& gpu, const Scene& scene, const Frame& frame)
{
    const render::rt::ViewDrawn view = view_of(frame);
    gpu.begin_scene();
    gpu.set_render_target(0, scene.colour);
    gpu.set_depth_surface(scene.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = frame.width, .height = frame.height, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, kSkyColour, 1.0F, 0);
    render::rt::view_begins(view);
    draw_world(gpu, scene, frame);
    render::rt::opaque_view_drawn(view);
    render::rt::world_view_drawn(view);
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    gpu.end_scene();
}

using Picture = std::vector<Vec3>;

Picture read_picture(GpuSurface* target, uint32_t width = kWidth, uint32_t height = kHeight)
{
    std::vector<uint8_t> bytes;
    Picture picture;
    if (!vk::read_surface(target, &bytes) || bytes.size() < static_cast<std::size_t>(width) * height * kHalfPixelBytes) {
        return picture;
    }
    picture.resize(static_cast<std::size_t>(width) * height);
    for (std::size_t pixel = 0; pixel < picture.size(); ++pixel) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const std::size_t first = (pixel * kHalfPixelBytes) + (channel * 2);
            core::at(core::at(picture, pixel), channel) =
                from_half(static_cast<uint16_t>(core::at(bytes, first) | (core::at(bytes, first + 1) << kHighByte)));
        }
    }
    return picture;
}

Vec3 pixel(const Picture& picture, const Vec3& world)
{
    if (picture.empty()) {
        return {-1.0F, -1.0F, -1.0F};
    }
    const std::array<uint32_t, 2> at = pixel_of(world);
    return core::at(picture, (static_cast<std::size_t>(core::at(at, 1)) * kWidth) + core::at(at, 0));
}

bool painted(const Vec3& colour, uint32_t rgb)
{
    constexpr uint32_t kRedShift = 16;
    constexpr uint32_t kGreenShift = 8;
    const std::array<float, 3> wanted{static_cast<float>((rgb >> kRedShift) & kByte) / kByteMax,
                                      static_cast<float>((rgb >> kGreenShift) & kByte) / kByteMax,
                                      static_cast<float>(rgb & kByte) / kByteMax};
    constexpr float kNearEnough = 0.12F;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        if (std::abs(core::at(colour, channel) - core::at(wanted, channel)) > kNearEnough) {
            return false;
        }
    }
    return true;
}
bool dark(const Vec3& colour)
{
    constexpr float kDark = 0.2F;
    constexpr float kNearEnough = 0.03F;
    return std::ranges::all_of(colour, [](float channel) { return std::abs(channel - kDark) < kNearEnough; });
}
bool finite_picture(const Picture& picture)
{
    return std::ranges::all_of(picture, [](const Vec3& c) {
        return std::isfinite(core::at(c, 0)) && std::isfinite(core::at(c, 1)) && std::isfinite(core::at(c, 2));
    });
}

constexpr vk::PaintSettings kOn{.on = true, .scale = 1.0F, .colour = 0};

void check_windows(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    vk::set_paint_settings(kOn);
    paint::clear();
    // a line near the camera, one far off along +x, a wallride
    ride_ground(kLineZ, 30000.0F);
    ride_ground(kLineZ, 0.0F, 2, 1800.0F);
    ride({-200.0F, kWallRide, kWallZ}, {1.0F, 0.0F, 0.0F}, 300.0F, 60, paint::Contact::kWall, {0.0F, 0.0F, -1.0F}, [](int) { return 0.0F; }, 3);
    const auto compare = [&](const char* what) {
        std::vector<uint32_t> coats;
        std::vector<uint32_t> places;
        uint32_t width = 0;
        uint32_t height = 0;
        {
            vk::Backend& state = vk::backend();
            const std::scoped_lock lock(state.mutex);
            vk::read_paint_windows(state, false, &coats, &width, &height);
            vk::read_paint_windows(state, true, &places, &width, &height);
        }
        const vk::PaintStats stats = vk::paint_stats();
        const auto n = static_cast<int>(height);
        std::size_t wrong = 0;
        std::size_t coated = 0;
        paint::read_fields([&](std::span<const trail::Field* const, paint::kAxes> fields) {
            for (std::size_t axis = 0; axis < paint::kAxes; ++axis) {
                const int first_x = core::at(core::at(stats.windows, axis), 0) * trail::kTileTexels;
                const int first_z = core::at(core::at(stats.windows, axis), 1) * trail::kTileTexels;
                for (int z = first_z; z < first_z + n; ++z) {
                    for (int x = first_x; x < first_x + n; ++x) {
                        const auto slot = (static_cast<std::size_t>(((z % n) + n) % n) * width) + (axis * static_cast<std::size_t>(n)) +
                                          static_cast<std::size_t>(((x % n) + n) % n);
                        const uint32_t coat = core::at(fields, axis)->texel_at(x, z, paint::kCoatPlane);
                        const uint32_t place = core::at(fields, axis)->texel_at(x, z, paint::kDepthPlane);
                        wrong += core::at(coats, slot) == coat && core::at(places, slot) == place ? 0U : 1U;
                        coated += coat != 0 ? 1U : 0U;
                    }
                }
            }
        });
        tally->check(height > 0 && width == 3 * height && wrong == 0 && coated > 0,
                     text::format("%s: the three windows' %ux%u texels as the fields hold them (%zu wrong, %zu coated)", what, height,
                                  height, wrong, coated));
    };
    draw_frame(gpu, scene, {});
    compare("the windows round the camera");
    const uint64_t copied = vk::paint_stats().tiles_copied;
    const float tile_units = paint::kTexel * static_cast<float>(trail::kTileTexels);
    const Frame moved{.camera = {7.0F * tile_units, core::at(kCamera, 1), core::at(kCamera, 2)}};
    draw_frame(gpu, scene, moved);
    compare("after the camera moved 7 tiles");
    const uint64_t moved_copies = vk::paint_stats().tiles_copied - copied;
    draw_frame(gpu, scene, moved);
    tally->check(vk::paint_stats().tiles_copied - copied == moved_copies && moved_copies <= 7U * paint::kWindowTiles,
                 text::format("only the tiles that came in were copied (%llu), nothing again the next frame",
                              static_cast<unsigned long long>(moved_copies)));
    paint::clear();
}

void check_coat(render::Gpu& gpu, const Scene& scene, Tally* tally)
{
    vk::set_paint_settings({});
    draw_frame(gpu, scene, {});
    const Picture off = read_picture(scene.colour);
    vk::set_paint_settings(kOn);
    paint::clear();
    ride_ground(kLineZ, 0.0F);                                                       // player 1's pink line
    ride({-300.0F, kRailTop, kRailZ}, {1.0F, 0.0F, 0.0F}, 400.0F, 90, paint::Contact::kRail, {0.0F, 1.0F, 0.0F},
         [](int) { return 0.0F; }, 2);                                               // a rail's stripe over the ground
    ride({-200.0F, kWallRide, kWallZ}, {1.0F, 0.0F, 0.0F}, 300.0F, 60, paint::Contact::kWall, {0.0F, 0.0F, -1.0F},
         [](int) { return 0.0F; }, 3);                                               // a wallride (lime)
    draw_frame(gpu, scene, {});
    const Picture on = read_picture(scene.colour);
    draw_frame(gpu, scene, {.marked = false});
    const Picture unmarked = read_picture(scene.colour);
    const uint32_t pink = paint::slot_colour(0);
    const uint32_t lime = paint::slot_colour(2);
    const Vec3 line_pixel = pixel(on, {0.0F, 0.0F, kLineZ});
    tally->check(painted(line_pixel, pink),
                 text::format("the line on the ground: the paint's colour (%.2f %.2f %.2f)", static_cast<double>(core::at(line_pixel, 0)),
                              static_cast<double>(core::at(line_pixel, 1)), static_cast<double>(core::at(line_pixel, 2))));
    tally->check(dark(pixel(on, {0.0F, 0.0F, kLineZ + 20.0F})) && dark(pixel(on, {0.0F, 0.0F, kLineZ - 20.0F})), "beside it: the ground");
    tally->check(dark(pixel(on, {0.0F, 0.0F, kRailZ})), "under the rail's stripe (its place 30 up): the ground's own colour");
    const Vec3 wall_pixel = pixel(on, {0.0F, kWallRide, kWallZ});
    tally->check(painted(wall_pixel, lime) && dark(pixel(on, {0.0F, kWallRide + 12.0F, kWallZ})),
                 text::format("the wall: the wallride's coat (%.2f %.2f %.2f), above it the wall",
                              static_cast<double>(core::at(wall_pixel, 0)), static_cast<double>(core::at(wall_pixel, 1)),
                              static_cast<double>(core::at(wall_pixel, 2))));
    tally->check(!off.empty() && dark(pixel(off, {0.0F, 0.0F, kLineZ})) && unmarked == off,
                 "paint off, or a draw not marked as the ground: the picture as before");
    tally->check(finite_picture(on), "every pixel finite");
    paint::clear();
}

void measure_cost(render::Gpu& gpu, Scene scene, Tally* tally)
{
    constexpr uint32_t kBigWidth = 2560;
    constexpr uint32_t kBigHeight = 1440;
    constexpr int kTimed = 20;
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    vk::Backend& state = vk::backend();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    if (properties.limits.timestampComputeAndGraphics == VK_FALSE) {
        text::print("the cost: no timestamps on this device\n");
        return;
    }
    VkQueryPool pool = VK_NULL_HANDLE;
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .pNext = nullptr, .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2, .pipelineStatistics = 0};
    if (vkCreateQueryPool(state.context.device(), &info, nullptr, &pool) != VK_SUCCESS) {
        return;
    }
    scene.width = kBigWidth;
    scene.height = kBigHeight;
    const bool made =
        gpu.create_render_target({.width = kBigWidth, .height = kBigHeight, .format = value::kFormatA16B16G16R16F, .samples = 0,
                                  .quality = 0, .lockable_or_discard = true},
                                 &scene.colour) == value::kResultOk &&
        gpu.create_depth_surface({.width = kBigWidth, .height = kBigHeight, .format = value::kFormatD24S8, .samples = 0, .quality = 0,
                                  .lockable_or_discard = false},
                                 &scene.depth) == value::kResultOk;
    if (made) {
        // a session's paint: lines every 40 units across the view, growing combos, a wallride
        vk::set_paint_settings(kOn);
        paint::clear();
        for (int line = 0; line < 30; ++line) {
            ride({-600.0F, 0.0F, -100.0F + (40.0F * static_cast<float>(line))}, {1.0F, 0.0F, 0.0F}, 900.0F, 80, paint::Contact::kGround,
                 {0.0F, 1.0F, 0.0F}, [](int step) { return 4000.0F * static_cast<float>(step); }, 1 + static_cast<uint64_t>(line % 4));
        }
        ride({-200.0F, kWallRide, kWallZ}, {1.0F, 0.0F, 0.0F}, 300.0F, 60, paint::Contact::kWall, {0.0F, 0.0F, -1.0F},
             [](int) { return 0.0F; }, 9);
        const Frame frame{.width = kBigWidth, .height = kBigHeight};
        const auto timed = [&](bool on) {
            vk::set_paint_settings(on ? kOn : vk::PaintSettings{});
            if (on) { // (the paint again: off wiped it)
                for (int line = 0; line < 30; ++line) {
                    ride({-600.0F, 0.0F, -100.0F + (40.0F * static_cast<float>(line))}, {1.0F, 0.0F, 0.0F}, 900.0F, 80,
                         paint::Contact::kGround, {0.0F, 1.0F, 0.0F}, [](int step) { return 4000.0F * static_cast<float>(step); },
                         1 + static_cast<uint64_t>(line % 4));
                }
            }
            for (int warm = 0; warm < 3; ++warm) {
                draw_frame(gpu, scene, frame);
            }
            const render::rt::ViewDrawn view = view_of(frame);
            gpu.begin_scene();
            gpu.set_render_target(0, scene.colour);
            gpu.set_depth_surface(scene.depth);
            gpu.set_viewport({.x = 0, .y = 0, .width = kBigWidth, .height = kBigHeight, .min_z = 0.0F, .max_z = 1.0F});
            render::rt::view_begins(view);
            {
                const std::scoped_lock lock(state.mutex);
                VkCommandBuffer commands = vk::outside_pass(state);
                vkCmdResetQueryPool(commands, pool, 0, 2);
                vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
            }
            for (int i = 0; i < kTimed; ++i) {
                draw_world(gpu, scene, frame);
            }
            std::array<uint64_t, 2> stamps{};
            {
                const std::scoped_lock lock(state.mutex);
                vkCmdWriteTimestamp(vk::outside_pass(state), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, 1);
                vk::finish_work(state);
                vkGetQueryPoolResults(state.context.device(), pool, 0, 2, sizeof(stamps), stamps.data(), sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            }
            render::rt::world_view_drawn(view);
            gpu.set_render_target(0, nullptr);
            gpu.set_depth_surface(nullptr);
            gpu.end_scene();
            return static_cast<double>(core::at(stamps, 1) - core::at(stamps, 0)) * static_cast<double>(properties.limits.timestampPeriod) /
                   kNanosecondsPerMillisecond / kTimed;
        };
        const double without = timed(false);
        const double with = timed(true);
        text::print("the cost: the ground and a wall at %ux%u - %.3f ms with the paint, %.3f without: %.3f ms a frame for the coat "
                    "(%llu tiles copied in all)\n",
                    kBigWidth, kBigHeight, with, without, with - without, static_cast<unsigned long long>(vk::paint_stats().tiles_copied));
        tally->check(std::isfinite(with) && with > 0.0 && std::isfinite(without), text::format("timed at %ux%u", kBigWidth, kBigHeight));
        paint::clear();
    }
    for (GpuSurface* surface : {scene.colour, scene.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
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
    vk::set_context_options({.validation = options.validation, .device_index = options.device, .on_message = print_line}, window);
    render::GpuContext& context = vk::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return false;
    }
    const vk::Backend& state = vk::backend();
    text::print("validation layer: %s\n", state.context.validation_on() ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWindowSize,
                       .height = kWindowSize,
                       .windowed = true,
                       .vsync = true,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24S8});
    const long made =
        context.create_device({.width = kWindowSize, .height = kWindowSize, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
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
    check_curve(&tally);
    check_lines(&tally);
    if (!open_device(options, window)) {
        return vk::loader_version() == 0 ? 2 : 1;
    }
    render::Gpu& gpu = vk::gpu();
    Scene scene;
    constexpr float kDark = 0.2F;
    const bool made =
        gpu.create_pixel_shader(kShader.data(), static_cast<uint32_t>(kShader.size()), &scene.shader) == value::kResultOk &&
        gpu.create_render_target({.width = kWidth, .height = kHeight, .format = value::kFormatA16B16G16R16F, .samples = 0, .quality = 0,
                                  .lockable_or_discard = true},
                                 &scene.colour) == value::kResultOk &&
        gpu.create_depth_surface({.width = kWidth, .height = kHeight, .format = value::kFormatD24S8, .samples = 0, .quality = 0,
                                  .lockable_or_discard = false},
                                 &scene.depth) == value::kResultOk;
    scene.dark = uniform_texture(gpu, kDark);
    tally.check(made && scene.dark != nullptr, "the material shader, a 16-bit float target, a depth surface and the texture made");
    if (made && scene.dark != nullptr) {
        check_windows(gpu, scene, &tally);
        check_coat(gpu, scene, &tally);
        measure_cost(gpu, scene, &tally);
    }
    vk::set_paint_settings({});
    for (GpuSurface* surface : {scene.colour, scene.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
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
    text::print("thug2_vulkan_paint_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    if (!vk::open_loader()) {
        text::print("no Vulkan loader (vulkan-1.dll) for this process\n");
        return 2;
    }
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        text::print("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int position = window_position(options.display);
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan paint check", kWindowSize, kWindowSize, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
