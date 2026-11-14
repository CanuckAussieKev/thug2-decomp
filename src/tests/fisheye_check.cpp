// NOT ORIGINAL - thug2_fisheye_check.exe: the fisheye lens' maths (engine/gfx/fisheye_lens.h) without the game or a
// device - the lens family (strength 0 rectilinear, 0.5 equidistant, 0.75 equisolid, 1 orthographic; the centre's scale 1
// at every strength; radius and angle each other's inverse), the camera's widening (fit: the widened view holds every
// place the lens shows - tightly -, none at strength 0, a round frame needing less), the game camera's projection widened
// as render_pc's choose_camera makes it, and the whole chain - a world direction drawn by the widened camera, the lens
// pass's mapping back - landing where the lens puts it: the centre at the original camera's scale, straight lines bowed.
// Exit code 0 when every check passes.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "base/core/checked_index.h"
#include "engine/gfx/fisheye_lens.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace fisheye = render::fisheye;

constexpr float kPi = 3.14159265F;
constexpr float kHalfAngleOf72 = 0.62831853F; // 36 degrees: the game's 72-degree camera's half angle
constexpr float kWide = 16.0F / 9.0F;
constexpr float kFour = 4.0F / 3.0F;

int& failures()
{
    static int count = 0;
    return count;
}

void expect(bool ok, const std::string& what)
{
    if (!ok) {
        ++failures();
        std::printf("FAIL: %s\n", what.c_str());
    } else {
        std::printf("ok    %s\n", what.c_str());
    }
}

bool close(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

float height_of(const std::array<float, 2>& place) { return core::at(place, 1); }

// The lens family: its named members, the centre's scale, the inverse, barrel (never past the pinhole's radius).
void check_family()
{
    bool named = true;
    for (float phi = 0.05F; phi < 1.3F; phi += 0.05F) {
        named = named && close(fisheye::radius_of(phi, 0.0F), std::tan(phi), 1e-5F * (1.0F + std::tan(phi)));
        named = named && close(fisheye::radius_of(phi, 0.5F), phi, 1e-6F);
        named = named && close(fisheye::radius_of(phi, 0.75F), 2.0F * std::sin(phi * 0.5F), 1e-5F);
        named = named && close(fisheye::radius_of(phi, 1.0F), std::sin(phi), 1e-6F);
        named = named && close(fisheye::radius_of(phi, 0.25F), 2.0F * std::tan(phi * 0.5F), 1e-5F);
    }
    expect(named, "strength 0 rectilinear, 0.25 stereographic, 0.5 equidistant, 0.75 equisolid, 1 orthographic");
    bool centre = true;
    bool inverse = true;
    bool barrel = true;
    bool squeezes = true;
    for (int step = 0; step <= 20; ++step) {
        const float s = static_cast<float>(step) / 20.0F;
        centre = centre && close(fisheye::radius_of(1e-3F, s) / 1e-3F, 1.0F, 1e-4F);
        for (float phi = 0.02F; phi < fisheye::kMostAngle; phi += 0.02F) {
            const float r = fisheye::radius_of(phi, s);
            inverse = inverse && close(fisheye::angle_of(r, s), phi, 2e-5F * (1.0F + (phi * phi * 10.0F)));
            barrel = barrel && r <= std::tan(phi) * (1.0F + 1e-6F);
            if (step > 0) {
                squeezes = squeezes && fisheye::radius_of(phi, s) <= fisheye::radius_of(phi, s - 0.05F) * (1.0F + 1e-6F);
            }
        }
    }
    expect(centre, "every strength keeps the centre's scale (the slope 1 at the axis)");
    expect(inverse, "angle_of undoes radius_of at every strength");
    expect(barrel, "the lens never lands a direction further out than the pinhole camera");
    expect(squeezes, "more strength squeezes the edges more");
    expect(fisheye::angle_of(100.0F, 1.0F) == fisheye::kMostAngle && fisheye::angle_of(100.0F, 0.0F) == fisheye::kMostAngle,
           "past the lens' reach: its farthest angle");
}

// The widening: the widened view holds every place the lens shows, tightly; none needed at strength 0.
void check_fit(float aspect, float circle, float strength)
{
    const float tan_x = std::tan(kHalfAngleOf72);
    const float tan_y = tan_x / aspect;
    const fisheye::Fit fit = fisheye::fit(tan_x, tan_y, strength, circle);
    float farthest_x = 0.0F;
    float farthest_y = 0.0F;
    constexpr int kSteps = 400;
    for (int i = 0; i <= kSteps; ++i) {
        for (int j = 0; j <= kSteps; ++j) {
            const float x = tan_x * static_cast<float>(i) / kSteps;
            const float y = tan_y * static_cast<float>(j) / kSteps;
            if (std::hypot(x, y) > fit.reach) {
                continue;
            }
            const std::array<float, 2> plane = fisheye::plane_position({x, y}, strength);
            farthest_x = std::max(farthest_x, core::at(plane, 0) / (tan_x * fit.widen));
            farthest_y = std::max(farthest_y, core::at(plane, 1) / (tan_y * fit.widen));
        }
    }
    const float farthest = std::max(farthest_x, farthest_y);
    char what[200];
    std::snprintf(what, sizeof(what), "aspect %.2f, circle %.1f, strength %.2f: widened x%.4f, the farthest shown place at "
                                      "%.5f of the widened view's edge (reach %.3f%s)",
                  static_cast<double>(aspect), static_cast<double>(circle), static_cast<double>(strength),
                  static_cast<double>(fit.widen), static_cast<double>(farthest), static_cast<double>(fit.reach),
                  fit.framed ? ", framed" : "");
    const bool tight = fit.widen == 1.0F ? farthest <= 1.0F + 1e-4F : close(farthest, 1.0F, 2e-3F);
    expect(farthest <= 1.0F + 1e-4F && tight, what);
}

void check_fits()
{
    for (const float aspect : {kWide, kFour}) {
        for (const float circle : {0.0F, 0.5F, 1.0F}) {
            for (const float strength : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
                check_fit(aspect, circle, strength);
            }
        }
    }
    const float tan_x = std::tan(kHalfAngleOf72);
    const float tan_y = tan_x / kWide;
    const fisheye::Fit none = fisheye::fit(tan_x, tan_y, 0.0F, 0.0F);
    expect(none.widen == 1.0F && !none.framed, "strength 0, the whole frame: the camera as it was");
    float before = 1.0F;
    bool grows = true;
    for (int step = 1; step <= 20; ++step) {
        const float widen = fisheye::fit(tan_x, tan_y, static_cast<float>(step) / 20.0F, 0.0F).widen;
        grows = grows && widen >= before;
        before = widen;
    }
    expect(grows && before > 1.3F, "more strength widens the camera more (x" + std::to_string(before) + " at 1)");
    const fisheye::Fit round = fisheye::fit(tan_x, tan_y, 0.75F, 1.0F);
    const fisheye::Fit full = fisheye::fit(tan_x, tan_y, 0.75F, 0.0F);
    expect(round.framed && close(round.reach, tan_y, 1e-6F) && round.widen < full.widen,
           "the porthole: framed at the shorter side, less widening than the whole frame");
    // a very wide camera: the lens' reach (75 degrees) frames it even without a round frame; the widened view's shorter
    // side (the one the reach decides) reaches just 75 degrees
    const fisheye::Fit wide = fisheye::fit(std::tan(1.2F), std::tan(1.2F) / kWide, 1.0F, 0.0F);
    expect(wide.framed && close(std::atan(std::tan(1.2F) / kWide * wide.widen), fisheye::kMostAngle, 1e-4F),
           "a camera wider than the lens reaches: framed, the widened view's shorter side at 75 degrees");
}

// The game camera's projection as render_pc's choose_camera makes it: the near plane's width 2 near tan(half angle),
// the projection's x scale 2 near / width; the widened angle's tangents the original's times widen.
void check_projection()
{
    constexpr float kNearZ = 2.0F;
    constexpr float kAngle = 72.0F;
    const float tan_x = std::tan(kAngle * kPi / 360.0F);
    const float tan_y = tan_x / kWide;
    const fisheye::Fit fit = fisheye::fit(tan_x, tan_y, 0.75F, 0.0F);
    const float widened = fisheye::widened_angle_degrees(kAngle, fit.widen);
    const auto x_scale = [](float angle) {
        const double half_angle = static_cast<double>(angle) * 0.008726646F; // (choose_camera's)
        const auto width = static_cast<float>(std::tan(half_angle) * 4.0 * (kNearZ / 2.0));
        return 2.0F * kNearZ / width;
    };
    expect(close(x_scale(kAngle) / x_scale(widened), fit.widen, 1e-4F),
           "the widened angle's projection: its scale the original's / widen (" + std::to_string(widened) + " degrees)");
    expect(close(fisheye::widened_angle_degrees(kAngle, 1.0F), kAngle, 1e-4F), "widen 1: the angle as it was");
}

// The chain: a direction's tangents t drawn by the widened camera at the widened picture's t / widen (focal units of the
// original); the lens pass at an output place n reads plane_position(n) / widen. The place the lens shows t at -
// lens_position(t) - reads t back; the centre at the original camera's scale; a straight line bowed.
void check_chain()
{
    const float tan_x = std::tan(kHalfAngleOf72);
    const float tan_y = tan_x / kWide;
    for (const float strength : {0.3F, 0.5F, 0.75F, 1.0F}) {
        const fisheye::Fit fit = fisheye::fit(tan_x, tan_y, strength, 0.0F);
        float worst = 0.0F;
        for (float x = -0.9F; x <= 0.9F; x += 0.1F) {
            for (float y = -0.5F; y <= 0.5F; y += 0.1F) {
                const std::array<float, 2> shown = fisheye::lens_position({x, y}, strength);
                const std::array<float, 2> drawn = fisheye::plane_position(shown, strength);
                worst = std::max({worst, std::fabs(core::at(drawn, 0) - x), std::fabs(core::at(drawn, 1) - y)});
            }
        }
        // (and inside the widened view: t / widen within its edges)
        const bool inside = 0.9F / fit.widen <= tan_x && 0.5F / fit.widen <= tan_y;
        expect(worst < 2e-4F && inside, "strength " + std::to_string(strength) + ": the pass reads back what the camera drew there");
        // the centre: a small step d from the axis shows at d (the original camera's scale), so the widened picture
        // is read at d / widen there - the pass magnifies its middle by widen exactly
        const std::array<float, 2> near_axis = fisheye::lens_position({0.002F, 0.001F}, strength);
        expect(close(core::at(near_axis, 0), 0.002F, 1e-6F) && close(core::at(near_axis, 1), 0.001F, 1e-6F),
               "strength " + std::to_string(strength) + ": the centre at the original camera's scale");
        // a straight line across the upper picture: its ends drawn nearer the middle line than its middle (barrel)
        const float line = -0.3F;
        const float middle = height_of(fisheye::lens_position({0.0F, line}, strength));
        const float end = height_of(fisheye::lens_position({0.9F, line}, strength));
        const float mid_end = height_of(fisheye::lens_position({0.45F, line}, strength));
        expect(std::fabs(end) < std::fabs(mid_end) && std::fabs(mid_end) < std::fabs(middle),
               "strength " + std::to_string(strength) + ": a straight line bows (" + std::to_string(middle) + ", " +
                   std::to_string(mid_end) + ", " + std::to_string(end) + ")");
    }
    // strength 0: nothing moves
    const std::array<float, 2> same = fisheye::lens_position({0.7F, -0.3F}, 0.0F);
    expect(close(core::at(same, 0), 0.7F, 1e-6F) && close(core::at(same, 1), -0.3F, 1e-6F), "strength 0: the pinhole's picture");
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_family();
    check_fits();
    check_projection();
    check_chain();
    if (failures() != 0) {
        std::printf("%d check(s) failed\n", failures());
        return 1;
    }
    std::printf("fisheye_check: all checks passed\n");
    return 0;
}
