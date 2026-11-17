#pragma once

// NOT ORIGINAL - the precipitation occlusion map (docs/design/vulkan.md "Rain" - "Where the rain reaches"): where what
// falls from the sky reaches. Shared by every weather effect that needs it - the rain's drops, splashes and wet ground
// now; the snow's settling and the weather surfaces later (renderer-hack-audit.md "exposure from physics": its first
// piece). API-neutral: the map's geometry, its GLSL twin, and a processor reference over triangles (the checks').
//
// The model: precipitation falls along one direction d (rain.h Fall: the median drop's fall speed against the wind at
// the drops' height - slanted downwind). Seen along d, the level is a height field: for each line of fall, the height
// at which it first meets something (the level and its cut-out casters: roofs, awnings, trees, walls a slanted rain
// meets). A point is reached where it lies at or above that height on its own line - nothing under a roof or an
// overhang, nothing indoors, a rain shadow on a wall's lee side in the wind.
//
// The lines of fall are numbered by where they cross the plane y = kReferenceHeight: a point P lies on the line
// uv = P.xz + slant (P.y - kReferenceHeight), slant = d.xz / -d.y (the sideways move a unit of fall). The map holds
// kCascades square windows of kMapSize texels a side round the camera's line (each texel a line of fall, its height
// where that line first meets something - kOpenHeight where it meets nothing), the near one's texels kCascadeTexel[0]
// units across, the far one's kCascadeTexel[1]; beyond the far one everything is reached. The windows are toroidal and
// kept frame to frame (the level is static): a texel holds which line it was traced for (its tag - the line's absolute
// texel and the map's generation), so a backend traces only the lines new to the window as the camera moves, a slice
// of the others each frame (kRefreshSlices: every line traced again within that many frames - a level loaded, a door
// opened), and every line when the fall's direction changes (a new generation).
#include <array>
#include <cstdint>
#include <span>

namespace render::precip {

inline constexpr uint32_t kMapSize = 1024; // texels a side (each cascade)
inline constexpr uint32_t kCascades = 2;
inline constexpr std::array<float, kCascades> kCascadeTexel{3.0F,
                                                            12.0F}; // world units: +-39 m and +-156 m round the camera
inline constexpr float kReferenceHeight = 0.0F;
inline constexpr float kTraceAbove = 8000.0F; // a line traced from this far above the camera ...
inline constexpr float kTraceBelow = 8000.0F; // ... down to this far below it
inline constexpr float kOpenHeight = -1.0e9F; // a line that meets nothing
inline constexpr uint32_t kRefreshSlices = 16;
inline constexpr uint32_t kTraceMask = 0x05U; // the level (0x01) and the cut-out casters (0x04) - vulkan_rt_scene.h
inline constexpr float kSlantChange = 0.002F; // a new generation when the slant moves this far (units a unit)

// The line of fall through a point (its uv), and a point's place on a cascade's grid.
[[nodiscard]] std::array<float, 2> line_of(const std::array<float, 3>& point, const std::array<float, 2>& slant);
// A cascade's window round the camera: its first texel (absolute: floor(uv / texel)) and its texel.
struct Window {
    std::array<int32_t, 2> first{};
    float texel = 1.0F;
};
[[nodiscard]] Window window_of(const std::array<float, 3>& camera, const std::array<float, 2>& slant, uint32_t cascade);
// A texel's tag: its absolute texel and the generation (0 never a valid tag: an untraced texel).
[[nodiscard]] uint32_t tag_of(int32_t x, int32_t z, uint32_t generation);
// Whether a texel (absolute) is refreshed in `frame`'s slice.
[[nodiscard]] bool in_slice(int32_t x, int32_t z, uint64_t frame);
// Where along the line of fall through texel (x, z)'s centre a ray starts and how far it goes (the backend's trace).
struct LineTrace {
    std::array<float, 3> origin{};
    std::array<float, 3> direction{}; // d (unit)
    float reach = 0.0F;
};
[[nodiscard]] LineTrace line_trace(int32_t x, int32_t z, float texel, const std::array<float, 2>& slant,
                                   const std::array<float, 3>& direction, float camera_y);
// A point reached given its line's height (the map's texel): no lower than the height less the slack (a texel's rise -
// a ramp's across it, half a texel along the slant - and kReachSlack: a surface is its own line's first hit).
inline constexpr float kReachSlack = 2.0F;
[[nodiscard]] float reach_slack(float texel, const std::array<float, 2>& slant);

// --- the processor's reference (the checks') -------------------------------------------------------------------------
using Triangle = std::array<std::array<float, 3>, 3>;
// The height at which the line of fall through `point` first meets one of `triangles` coming down from `top` (kOpenHeight:
// none above `bottom`).
[[nodiscard]] float line_height(std::span<const Triangle> triangles, const std::array<float, 3>& point,
                                const std::array<float, 3>& direction, float top, float bottom);
[[nodiscard]] bool reached(std::span<const Triangle> triangles, const std::array<float, 3>& point,
                           const std::array<float, 3>& direction, float top);

// The GLSL twin: precip_reached(world) (0 - 1, the four texels round its line blended; 1 beyond the far cascade or
// without a map). Needs `PRECIP_BLOCK` (the uniform block's name) with vec4 members precip_fall (xyz d, w 1 with a map),
// precip_slant (xy slant, z the reference height, w unused), precip_near / precip_far (xy the first texel, z the texel,
// w the slack) and `PRECIP_MAP` - a usampler2D of kCascades x kMapSize by kMapSize texels, the cascades side by side (r: the height's
// bits, g: the tag), and PRECIP_SIZE (kMapSize, an int).
[[nodiscard]] const char* reached_glsl();

} // namespace render::precip
