#pragma once

#include <array>

// NOT ORIGINAL - the fisheye lens' maths ([Graphics] rt_fisheye: a 90s skate video's fisheye adapter - the Vulkan Gpu's
// vulkan_fisheye.h draws it; docs/design/vulkan.md "Fisheye lens"). Pure: render_pc widens the game camera's field of view
// with it, the lens pass and its processor twin warp the picture with it, thug2_fisheye_check checks it.
//
// The game's camera is a pinhole (rectilinear): a direction at an angle phi off the view's axis lands at a distance
// tan(phi) from the picture's centre, in units of the focal length (f: the picture's half width / tan of the half angle).
// A fisheye lens lands it at radius_of(phi, s) instead - for every strength s the same scale at the centre (the slope 1
// at phi 0: what the camera looks at keeps its size), the edges squeezed more and more:
//     s 0         tan(phi)                              rectilinear (no distortion)
//     s 0 - 0.5   tan(k phi) / k,  k = 1 - 2 s          (s 0.25: stereographic)
//     s 0.5       phi                                   equidistant
//     s 0.5 - 1   sin(k phi) / k,  k = 2 s - 1          (s 0.75: equisolid, 2 sin(phi / 2); s 1: orthographic, sin(phi))
// So the fisheye picture holds more than the camera's: the camera's field of view is widened to match (fit below) - its
// tangents times `widen` - and the lens pass maps each output pixel back: its radius r (focal units) -> the angle
// angle_of(r) -> the widened picture's pixel at tan(angle) / widen x f along the same direction.
namespace render::fisheye {

inline constexpr float kMostAngle = 1.3089969F; // 75 degrees: the farthest off the axis the widened view reaches
inline constexpr float kStrengthDefault = 0.75F; // (rt_fisheye_strength's: equisolid)

// The lens: an angle off the axis (radians) -> the radius in the picture (focal units).
[[nodiscard]] float radius_of(float angle, float strength);
// ... and back (r >= 0; past the lens' reach - the orthographic side's 1 / k - its edge, pi / 2 / k), at most kMostAngle.
[[nodiscard]] float angle_of(float radius, float strength);

// What a view's lens needs: the original view's tangents of its half angles (x across, y down - the projection's), the
// strength, rt_fisheye_circle (0: the whole frame; 1: a round frame touching the picture's shorter sides).
struct Fit {
    float widen = 1.0F;     // the widened view's tangents over the original's (>= 1)
    float reach = 0.0F;     // the radius (focal units) the picture shows to: the frame's circle, the lens' reach, or the corner
    float corner = 0.0F;    // the corners' radius (focal units)
    bool framed = false;    // reach < corner: past it the picture is black (the round frame; the lens' edge)
};
[[nodiscard]] Fit fit(float tan_x, float tan_y, float strength, float circle);

// The widened half angle's tangent for a camera's (tan x widen) and its full angle in degrees (the game's camera angle).
[[nodiscard]] float widened_angle_degrees(float angle_degrees, float widen);

// Where a direction (its tangents x / z, y / z: a point of the original camera's image plane, focal units) lands in the
// fisheye picture (focal units): along the same line from the centre at radius_of(atan |t|).
[[nodiscard]] std::array<float, 2> lens_position(std::array<float, 2> tangents, float strength);
// The other way: a fisheye picture's place (focal units) -> the image plane's tangents (what the widened camera drew
// there, before the widening's scale).
[[nodiscard]] std::array<float, 2> plane_position(std::array<float, 2> place, float strength);

} // namespace render::fisheye
