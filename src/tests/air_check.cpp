// NOT ORIGINAL - thug2_air_check.exe: round W's air transport (engine/gfx/air_volume.h) and the one wind
// (engine/gfx/wind.h) checked on the processor - no game, no device:
//   - a closed box on open ground: no air inside (wind 0), no sky; the ground outside: the free stream and the sky;
//   - a room with a door: the draught falls with the distance from the door, the at_end end stiller than the door's;
//   - glass blocks air (the same room with a glass wall in place of the door's: still) but the material table says it
//     lets light through; a chain-link fence lets air through at its open share; a grating roof lets the sky's fall
//     through at its open share;
//   - the one wind: the speed at_door the ground from the weather's 10 m wind by the log profile, sheltered by the volume;
//   - the cost of a level-sized volume.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <ratio>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/air_volume.h"
#include "engine/gfx/rt_material_table.h"
#include "engine/gfx/wind.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace air = render::air;
using air::Face;
using air::Vec3;
using render::rt::MaterialKind;

int g_failures = 0;
int g_passes = 0;

void expect(bool ok, const std::string& what)
{
    if (ok) {
        ++g_passes;
    } else {
        ++g_failures;
    }
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

std::string num(double value) { return std::to_string(value).substr(0, 6); }

// A quad (a, b, c, d in order round it) as two faces.
void quad(std::vector<Face>* faces, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, float air)
{
    faces->push_back({.corners = {a, b, c}, .air = air});
    faces->push_back({.corners = {a, c, d}, .air = air});
}

// An axis-aligned rectangle in the plane x = at (y0..y1, z0..z1), z = at (x0..x1, y0..y1) or y = at.
void wall_x(std::vector<Face>* f, float x, float y0, float y1, float z0, float z1, float air)
{
    quad(f, {x, y0, z0}, {x, y1, z0}, {x, y1, z1}, {x, y0, z1}, air);
}
void wall_z(std::vector<Face>* f, float z, float x0, float x1, float y0, float y1, float air)
{
    quad(f, {x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}, air);
}
void floor_y(std::vector<Face>* f, float y, float x0, float x1, float z0, float z1, float air)
{
    quad(f, {x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}, air);
}

constexpr float kGround = 4000.0F;
// The room: x 0..800, z 0..800, y 0..300 on open ground; its -z wall holds the door (x 360..440, y 0..200) when `door`,
// its +x wall is of `east_air`, its roof of `roof_air`.
std::vector<Face> room(bool door, float door_air, float east_air, float roof_air)
{
    std::vector<Face> f;
    floor_y(&f, 0.0F, -kGround, kGround, -kGround, kGround, 0.0F);
    constexpr float kW = 800.0F;
    constexpr float kH = 300.0F;
    if (door) {
        wall_z(&f, 0.0F, 0.0F, 360.0F, 0.0F, kH, 0.0F);
        wall_z(&f, 0.0F, 440.0F, kW, 0.0F, kH, 0.0F);
        wall_z(&f, 0.0F, 360.0F, 440.0F, 200.0F, kH, 0.0F);
        if (door_air < 1.0F) {
            wall_z(&f, 0.0F, 360.0F, 440.0F, 0.0F, 200.0F, door_air);
        }
    } else {
        wall_z(&f, 0.0F, 0.0F, kW, 0.0F, kH, 0.0F);
    }
    wall_z(&f, kW, 0.0F, kW, 0.0F, kH, 0.0F);
    wall_x(&f, 0.0F, 0.0F, kH, 0.0F, kW, 0.0F);
    wall_x(&f, kW, 0.0F, kH, 0.0F, kW, east_air);
    floor_y(&f, kH, 0.0F, kW, 0.0F, kW, roof_air);
    return f;
}

void check_closed_box()
{
    air::Report report;
    const air::Volume v = air::Volume::build(room(false, 0.0F, 0.0F, 0.0F), &report);
    const air::Sample inside = v.at({400.0F, 100.0F, 400.0F});
    const air::Sample corner = v.at({60.0F, 30.0F, 60.0F});
    const air::Sample outside = v.at({-1500.0F, 60.0F, -1500.0F});
    const air::Sample beside = v.at({-40.0F, 40.0F, 400.0F});
    expect(inside.wind == 0.0F && corner.wind == 0.0F,
           "a closed box: no air connected inside (wind " + num(inside.wind) + ", corner " + num(corner.wind) + ")");
    expect(inside.sky == 0.0F, "a closed box: no sky inside (" + num(inside.sky) + ")");
    expect(outside.wind > 0.99F && outside.sky > 0.99F,
           "open ground outside: the free stream (wind " + num(outside.wind) + ", sky " + num(outside.sky) + ")");
    expect(beside.wind > 0.9F && beside.sky > 0.99F,
           "beside the box's wall outside: the air of the street (wind " + num(beside.wind) + ")");
    expect(report.stilled > 0 && report.sheltered >= 0,
           "the box's cells counted still (" + std::to_string(report.stilled) + ")");
}

void check_door()
{
    air::Report report;
    const air::Volume v = air::Volume::build(room(true, 1.0F, 0.0F, 0.0F), &report);
    // along the room's middle from the door (z 0) to the at_end wall (z 800), at hip height
    std::vector<float> along;
    for (float z = 40.0F; z <= 760.0F; z += 40.0F) {
        along.push_back(v.at({400.0F, 60.0F, z}).wind);
    }
    bool falling = true;
    for (std::size_t i = 1; i < along.size(); ++i) {
        falling = falling && core::at(along, i) <= core::at(along, i - 1) + 0.02F;
    }
    const float at_door = along.front();
    const float at_end = along.back();
    std::string row;
    for (const float w : along) {
        row += num(w) + " ";
    }
    expect(at_door > 0.8F, "a room with a door: the draught at the door (" + num(at_door) + ")");
    expect(falling, "... falls with the distance from the door (" + row + ")");
    expect(at_end < 0.6F * at_door && at_end > 0.0F, "... the at_end end stiller (" + num(at_end) + ") but connected");
    const float side = v.at({60.0F, 60.0F, 740.0F}).wind;
    expect(side <= at_end + 0.05F, "... the corner away from the door as still (" + num(side) + ")");
    expect(v.at({400.0F, 100.0F, 400.0F}).sky == 0.0F, "... no sky under its roof");
    std::printf("     (door room: %zu mixed bricks, %zu sheltered cells, %.1f ms)\n", report.mixed, report.sheltered,
                report.milliseconds);
}

void check_glass_and_screens()
{
    // glass in the doorway: no air (a window shut), though the light goes through it
    const air::Volume glass =
        air::Volume::build(room(true, render::rt::material_values(MaterialKind::kGlass).air_transmission, 0.0F, 0.0F));
    expect(glass.at({400.0F, 100.0F, 400.0F}).wind == 0.0F,
           "glass in the doorway blocks the air (wind inside " + num(glass.at({400.0F, 100.0F, 400.0F}).wind) + ")");
    expect(render::rt::material_values(MaterialKind::kGlass).light_transmission > 0.85F,
           "... while glass passes light (" +
               num(render::rt::material_values(MaterialKind::kGlass).light_transmission) + ")");
    expect(render::rt::kind_of_terrain("TERRAIN_PLEXIGLASS") == MaterialKind::kGlass &&
               render::rt::kind_of_terrain("TERRAIN_CHAINLINK") == MaterialKind::kChainLink &&
               render::rt::kind_of_terrain("TERRAIN_METALGRATING") == MaterialKind::kGrating &&
               render::rt::kind_of_terrain("TERRAIN_SOMETHING") == MaterialKind::kPainted,
           "the terrains' kinds: plexiglass glass, chain-link, grating, an unknown one solid");
    // a chain-link fence for the whole east wall: the air comes through it at its open share
    const float fence_air = render::rt::material_values(MaterialKind::kChainLink).air_transmission;
    const air::Volume fence = air::Volume::build(room(false, 0.0F, fence_air, 0.0F));
    const float by_fence = fence.at({760.0F, 100.0F, 400.0F}).wind;
    expect(by_fence > 0.8F * fence_air && by_fence <= fence_air + 0.01F,
           "a chain-link wall lets the air through at its open share (" + num(by_fence) + " of " + num(fence_air) +
               ")");
    // a grating roof: the sky's fall through it at its open share
    const float grating_air = render::rt::material_values(MaterialKind::kGrating).air_transmission;
    const air::Volume grating = air::Volume::build(room(false, 0.0F, 0.0F, grating_air));
    const float sky = grating.at({400.0F, 100.0F, 400.0F}).sky;
    expect(std::fabs(sky - grating_air) < 0.02F,
           "a grating roof passes the sky's fall at its open share (" + num(sky) + ")");
}

void check_wind()
{
    // the one wind: at_door the ground the weather's 10 m wind by the log profile; sheltered by the volume
    render::wind::set_wind_setting(5.0);
    const double at_two = render::wind::speed_at_height(2.0);
    const double expected = 5.0 * std::log(2.0 / render::wind::kRoughness) / std::log(10.0 / render::wind::kRoughness);
    expect(std::fabs(at_two - expected) < 1e-9 && at_two > 2.0 && at_two < 2.6,
           "the wind 2 m up from 5 m/s at 10 m by the log profile: " + num(at_two) + " m/s");
    expect(render::wind::speed_at_height(0.1) == 0.0, "... none under the roughness length");
    const air::Volume v = air::Volume::build(room(false, 0.0F, 0.0F, 0.0F));
    air::set_volume(std::make_shared<air::Volume>(v));
    const render::wind::Air inside = render::wind::air_at({400.0F, 100.0F, 400.0F}, 2.0);
    const render::wind::Air outside = render::wind::air_at({-1500.0F, 80.0F, -1500.0F}, 2.0);
    expect(inside.speed == 0.0F && outside.speed > 2.0F,
           "the wind inside the closed box 0, outside " + num(outside.speed) + " m/s");
    air::set_volume(nullptr);
    render::wind::set_wind_setting(render::wind::kWindAuto);
}

void check_cost()
{
    // a level-sized street grid: 600 x 600 m of ground, 400 buildings (closed boxes, 8 - 40 m tall)
    std::vector<Face> f;
    floor_y(&f, 0.0F, -12000.0F, 12000.0F, -12000.0F, 12000.0F, 0.0F);
    for (int i = 0; i < 20; ++i) {
        for (int j = 0; j < 20; ++j) {
            const float x0 = -11000.0F + (static_cast<float>(i) * 1100.0F);
            const float z0 = -11000.0F + (static_cast<float>(j) * 1100.0F);
            const float h = 300.0F + (static_cast<float>((i * 7 + j * 13) % 10) * 130.0F);
            wall_z(&f, z0, x0, x0 + 700.0F, 0.0F, h, 0.0F);
            wall_z(&f, z0 + 700.0F, x0, x0 + 700.0F, 0.0F, h, 0.0F);
            wall_x(&f, x0, 0.0F, h, z0, z0 + 700.0F, 0.0F);
            wall_x(&f, x0 + 700.0F, 0.0F, h, z0, z0 + 700.0F, 0.0F);
            floor_y(&f, h, x0, x0 + 700.0F, z0, z0 + 700.0F, 0.0F);
        }
    }
    air::Report report;
    const air::Volume v = air::Volume::build(f, &report);
    std::printf("     (a 600 m street grid: %zu faces, %d x %d x %d bricks, %zu mixed, %zu free cells, %zu sheltered, "
                "%zu still, "
                "%.0f ms, %.1f MB kept)\n",
                report.faces, report.bricks[0], report.bricks[1], report.bricks[2], report.mixed, report.free_cells,
                report.sheltered, report.stilled, report.milliseconds, static_cast<double>(report.bytes) / 1048576.0);
    std::printf("     (phases at: faces %.0f, widths %.0f, sky %.0f, free stream %.0f, the rest %.0f ms)\n",
                report.phases[0], report.phases[1], report.phases[2], report.phases[3], report.phases[4]);
    expect(v.at({-10650.0F, 100.0F, -10650.0F}).wind == 0.0F && v.at({-10100.0F, 60.0F, -10650.0F}).wind > 0.9F,
           "the street grid: a building's inside still, its street open");
    const auto started = std::chrono::steady_clock::now();
    float sum = 0.0F;
    for (int i = 0; i < 100000; ++i) {
        sum += v.at({static_cast<float>((i * 37) % 20000) - 10000.0F, 60.0F,
                     static_cast<float>((i * 91) % 20000) - 10000.0F})
                   .wind;
    }
    const double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count() / 100000.0;
    std::printf("     (a sample: %.3f us; mean wind at 1.5 m %.2f)\n", us, static_cast<double>(sum) / 100000.0);
    expect(report.milliseconds < 20000.0, "the volume builds in under 20 s (" + num(report.milliseconds) + " ms)");
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_closed_box();
    check_door();
    check_glass_and_screens();
    check_wind();
    check_cost();
    std::printf("%d passed, %d failed\n", g_passes, g_failures);
    return g_failures == 0 ? 0 : 1;
}
