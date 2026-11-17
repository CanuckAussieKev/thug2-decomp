#pragma once

// NOT ORIGINAL - a level's sky capture (rt_scene.h's SkyCapture: render_pc draws the sky scene's dome from the origin
// into six square faces once per level - the water's reflections read it) as directions: each texel is a direction (its
// face's centre direction + the texel's place across and up) and a solid angle (a cube face's texel: 1 / (1 + a^2 +
// b^2)^1.5 of the face's centre one).
//
// (Round S2: the analysis of the painted dome - its bands' colours, its clouds' share - retired with the last thing that
// read it, rt_cloud_coverage=auto's cover: the clouds' cover is the weather's (rt_clouds.h weather_cloud_cover), never
// the original's painted texture; its colours were never matched in since the rt_sky_match setting retired.)
#include <array>
#include <cstdint>

namespace render::rt {

// A texel's direction (world, y up; normalised) and its solid angle relative to a face's centre texel.
struct CaptureTexel {
    std::array<double, 3> direction{};
    double weight = 0.0;
};
struct CaptureSpot {
    uint32_t face = 0;
    uint32_t x = 0; // in the face, from its left
    uint32_t y = 0; // from its top
};
[[nodiscard]] CaptureTexel capture_texel(const CaptureSpot& spot, uint32_t face_size);

} // namespace render::rt
