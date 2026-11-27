// NOT ORIGINAL - the water's GLSL (water_shader.h; the formulas are explained in vulkan_water.h).
#include "gpu_vulkan/water_shader.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "engine/gfx/rain.h"
#include "engine/gfx/rt_volume_light.h"
#include "gpu_vulkan/ff_uniforms.h"

namespace render::vulkan {

namespace {

constexpr uint32_t kGlslUniforms = 10; // (the bindings the GLSL below names)
constexpr uint32_t kGlslScene = 11;
constexpr uint32_t kGlslDepth = 12;
constexpr uint32_t kGlslSky = 13;
constexpr uint32_t kGlslWaves = 14;
static_assert(kWaterUniformsBinding == kGlslUniforms && kWaterSceneBinding == kGlslScene && kWaterDepthBinding == kGlslDepth &&
                  kWaterSkyBinding == kGlslSky && kWaterWavesBinding == kGlslWaves,
              "the GLSL below names the bindings");

// (round WAVES) The water's surface - one definition for the raster water (both stages: the moving surface's shape and its
// normal) and the rays that meet it (the reference path tracer, vulkan_path_reference.cpp): a body's waves (WaterSea -
// engine/gfx/water_waves.h SeaWaves and the wind's way) at a rest point and time. The ripple texture is read by the
// includer (a fragment shader by its derivatives, a ray at its full detail): water_ripple_places gives where, water_ripples_of
// what its two reads make.
constexpr std::string_view kWaterWavesGlsl = R"(struct WaterSea {
    vec4 swell;       // the four Gerstner waves' slopes (amplitude x k; 0: still)
    vec4 swell_speed; // their phase speeds (units a second)
    vec4 sea;         // x the significant height (units), y the peak's wavelength (units), z x the ripple texture's slope,
                      // w the whitecaps' share
    vec2 wind;        // the wind's direction (world x, z)
};
const float kWaterSharpness = 0.6;  // the crests' sharpening (Gerstner's Q)

// Wave i of the swell (four Gerstner waves about the wind's direction, standing for the wind sea's spectrum round its
// peak - engine/gfx/water_waves.h sea_waves: the body's fetch, depth and wind) at rest point p, time t: its direction
// (xy), wavenumber (z) and phase (w); `slope` its slope's amplitude (its height x k, the energy of its band of the
// spectrum - 0 on still water), its speed the dispersion's at the body's depth.
vec4 water_wave(WaterSea sea, int i, vec2 p, float t, out float slope)
{
    const vec4 lengths = vec4(1.0, 0.63, 0.41, 0.27);
    const vec4 turns = vec4(0.0, 0.42, -0.57, 1.05);
    vec2 wind = sea.wind;
    float wavelength = max(lengths[i] * sea.sea.y, 0.05);
    float k = 6.2831853 / wavelength;
    float c = cos(turns[i]);
    float s = sin(turns[i]);
    vec2 d = vec2(wind.x * c - wind.y * s, wind.x * s + wind.y * c);
    slope = sea.swell[i];
    return vec4(d, k, k * (dot(d, p) - sea.swell_speed[i] * t) + float(i) * 1.913);
}

// A wave faded out where a pixel spans a good part of its wavelength (no aliasing far away; a ray's footprint 0: none).
float water_wave_fade(float footprint, float wavelength) { return 1.0 - smoothstep(0.05, 0.2, footprint / wavelength); }

// The swell at rest point p as its normal's parts - x and z the slope, y the crests' sharpening -, each wave faded out
// where `footprint` spans a good part of its wavelength. The moving surface's shape is the same waves' (water_lift):
// this is its normal.
vec3 water_swell(WaterSea sea, vec2 p, float t, float footprint)
{
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        float slope;
        vec4 w = water_wave(sea, i, p, t, slope);
        float a = slope * water_wave_fade(footprint, 6.2831853 / w.z);
        sum += vec3(w.x * a * cos(w.w), kWaterSharpness * a * sin(w.w), w.y * a * cos(w.w));
    }
    return sum;
}

// The ripples: the tiling texture at two scales drifting with the wind - where its two reads are at rest point p, time t,
// and their slope (x, z) and the foam's pattern (z) from the two texels read there. Their slope is the spectrum's short
// waves' (sea.z: the equilibrium tail past the four waves over the texture's band - water_waves.h; none on still water
// or where the body's waves are all shorter than the texture's).
void water_ripple_places(WaterSea sea, vec2 p, float t, out vec2 a, out vec2 b)
{
    vec2 along = sea.wind;
    vec2 across = vec2(-along.y, along.x);
    vec2 q = vec2(dot(p, along), dot(p, across));
    a = q / 263.0 + vec2(t * 0.031, 0.0);
    b = q / 97.0 + vec2(t * 0.083, t * 0.019) + vec2(0.37, 0.71);
}
vec3 water_ripples_of(WaterSea sea, vec4 a, vec4 b)
{
    vec2 along = sea.wind;
    vec2 across = vec2(-along.y, along.x);
    vec2 slope = (a.xy * 0.55 + b.xy * 0.45) * sea.sea.z;
    return vec3(along * slope.x + across * slope.y, a.w * 0.55 + b.w * 0.45);
}

// Where the calm surface's mirror shows what a ray reflected by the waves at `surface` along `reflected` meets `distance`
// on: that point mirrored by the rest plane through `surface` (normal `face`) - the flat mirror's image of it (the sky:
// a far distance). vulkan_water.h water_mirror_image is the processor's twin.
vec3 water_mirror_image(vec3 surface, vec3 face, vec3 reflected, float distance)
{
    return surface + reflect(reflected, face) * distance;
}

// The waves' normal (world, facing up) from the swell's parts and the ripples' (and the rain's rings') slope.
vec3 water_wave_normal(vec3 swell, vec2 ripple)
{
    return normalize(vec3(-(swell.x + ripple.x), max(1.0 - swell.y, 0.2), -(swell.z + ripple.y)));
}
)";

// Both stages': the view's block, the depth behind the water, and the swell - the same waves shape the moving surface
// (the vertex shader) and its normal (the fragment shader).
constexpr std::string_view kWaterCommonGlsl = R"(layout(set = 0, binding = 10, std140) uniform WaterUniforms {
    mat4 eye_to_world;
    mat4 world_to_eye;
    mat4 projection;
    vec4 viewport;
    vec4 target;
    vec4 time;
    vec4 waves;
    vec4 tint;
    vec4 sun;
    vec4 sun_colour;
    vec4 sky;
    vec4 camera;
    vec4 misc;
    vec4 swell;
    vec4 swell_speed;
    vec4 sea;
} water;
layout(set = 0, binding = 12) uniform sampler2D water_depth;

const float kWaterFar = 1.0e8;      // the depth copy's sky (1e9) and anything as far

// (round WATER) How far along the view camera-space `eye` lies: the projection's w - the depth copy's measure. Camera
// space's z is no distance: Direct3D's left-handed views look along +z, the game's right-handed ones along -z (every
// distance below was 0.001 there: the bed read as 10^5 x deeper than it is - no bed, the water's deep colour and its
// texture's pattern in its place -, the refraction and the mirror image moved to the view's edge by the waves' tilt).
float water_view_depth(vec3 eye) { return (water.projection * vec4(eye, 1.0)).w; }
// (round WAVES) The body drawn's waves as the shared surface (water_waves_glsl) takes them.
WaterSea water_sea() { return WaterSea(water.swell, water.swell_speed, water.sea, water.time.yz); }
)";

// The vertex shader's (the moving surface: FragmentKey::water kWaterMoving, vulkan_water.h): the rest point moved by
// the swell's Gerstner waves - each faded where a pixel spans a good part of it (as the normal's) and where the grid is
// too coarse for it (water_tess.h's spacing), their slopes together held at most 1 (no loops), the whole moved less
// toward the shore (the water's depth below the point, from the copy behind the water: none where it meets geometry).
// Handed on: the move, so the fragment shader shades the rest point (v_water_shift: x, z - beside v_fog; v_water_rise:
// y - beside v_surface_eye).
constexpr std::string_view kWaterVertexGlsl = R"(layout(location = 2, component = 2) out vec2 v_water_shift;
layout(location = 8, component = 3) out float v_water_rise;

const float kWaterCell = 40.0;        // the grid's spacing (water_tess.h kDefaultSpacing)
const float kWaterShoreDepth = 48.0;  // the water's depth below a point at which it moves fully
const float kWaterMostSlope = 1.0;    // the waves' slopes together at most this

// How much a point moves: 0 at the shore, 1 where the water below it is kWaterShoreDepth deep (found as the shore foam's
// depth, from the copy behind the water at the point's place on the screen - the nearest edge's off the screen); 1
// without a copy or with nothing behind.
float water_shore(vec3 eye, float dist, vec3 to_eye)
{
    float eye_w = water_view_depth(eye);
    if (water.target.z < 0.5 || eye_w <= 1.0e-3) {
        return 1.0;
    }
    vec4 clip = water.projection * vec4(eye, 1.0);
    vec2 ndc = clamp(clip.xy / clip.w, vec2(-1.0), vec2(1.0));
    vec2 pixel = water.viewport.xy + 0.5 + vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * max(water.viewport.zw - 1.0, 0.0);
    float behind = textureLod(water_depth, pixel * water.target.xy, 0.0).r;
    if (behind >= kWaterFar) {
        return 1.0;
    }
    float down = max(behind - eye_w, 0.0) * dist / eye_w * clamp(abs(to_eye.y) / dist, 0.0, 1.0);
    return smoothstep(0.0, kWaterShoreDepth, down);
}

// The move of the rest point `world` (camera space `eye`) by the swell: x, z along the water, y up.
vec3 water_lift(vec3 world, vec3 eye)
{
    vec3 to_eye = water.camera.xyz - world;
    float dist = max(length(to_eye), 1.0e-3);
    // (a pixel's footprint on the water there, as the fragment shader's derivatives find it: stretched at grazing angles)
    float focal = max(abs(water.projection[1][1]) * water.viewport.w * 0.5, 1.0);
    float footprint = dist / (focal * max(abs(to_eye.y) / dist, 0.1));
    float slopes = 0.0;
    vec3 sum = vec3(0.0);
    WaterSea sea = water_sea();
    for (int i = 0; i < 4; ++i) {
        float slope;
        vec4 w = water_wave(sea, i, world.xz, water.time.x, slope);
        float wavelength = 6.2831853 / w.z;
        slope *= water_wave_fade(footprint, wavelength) * (1.0 - smoothstep(0.25, 0.5, kWaterCell / wavelength));
        slopes += slope;
        float height = slope / w.z;
        float across = kWaterSharpness * height * cos(w.w);
        sum += vec3(w.x * across, height * sin(w.w), w.y * across);
    }
    return sum * (min(1.0, kWaterMostSlope / max(slopes, 1.0e-6)) * water_shore(eye, dist, to_eye));
}
)";

// The rest point the fragment shader shades: where the surface was before it moved (the moving surface), or where it
// is (flat).
constexpr std::string_view kWaterRestMovingGlsl = R"(layout(location = 2, component = 2) in vec2 v_water_shift;
layout(location = 8, component = 3) in float v_water_rise;
vec3 water_rest(vec3 world) { return world - vec3(v_water_shift.x, v_water_rise, v_water_shift.y); }
)";
constexpr std::string_view kWaterRestFlatGlsl = R"(vec3 water_rest(vec3 world) { return world; }
)";

constexpr std::string_view kWaterGlsl = R"(layout(set = 0, binding = 11) uniform sampler2D water_scene;
layout(set = 0, binding = 13) uniform sampler2D water_sky;
layout(set = 0, binding = 14) uniform sampler2D water_waves;

const float kWaterF0 = 0.02;        // water's Fresnel reflectance face-on

float water_luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Whether a texture's mean colour is water's (1) or not (0): not warm (red over blue: lava, fire, foliage, sunlight
// pools), not near white (foam, caustics, glints drawn over the water) nor near black (shadows) - vulkan_water.h
// water_hue_weight is the same on the processor.
// (round LW) Only a warm texture says a moving surface isn't water (lava, a fire's flow); its darkness or brightness
// doesn't: Barcelona's fountain pool (EDA1D05C, a dark pebbled water texture over the basin) was left a flat dark sheet
// by a "near black: a shadow" test - a shadow or a foam sheet drawn so is no water material (render_pc/water_materials:
// its first pass shows the surface, rt_lights.h blend_shows_surface), and whatever its colour, water's surface reflects
// by Fresnel; its colour is its body's.
float water_hue_weight(vec3 mean) { return 1.0 - smoothstep(0.04, 0.12, mean.r - mean.b); }

// The ripples (water_ripples_of) read by the fragment's derivatives.
vec3 water_ripples(vec2 p, float t)
{
    WaterSea sea = water_sea();
    vec2 a;
    vec2 b;
    water_ripple_places(sea, p, t, a, b);
    return water_ripples_of(sea, texture(water_waves, a), texture(water_waves, b));
}

// The level's sky as captured (3 x 2 faces of 90 degrees: +x, -x, +z, -z, +y, -y - rt::kSkyCaptureFaces) toward `d`.
vec3 water_capture(vec3 d)
{
    vec3 a = abs(d);
    int face;
    vec3 forward;
    vec3 up;
    if (a.y >= a.x && a.y >= a.z) {
        face = d.y > 0.0 ? 4 : 5;
        forward = vec3(0.0, sign(d.y), 0.0);
        up = vec3(0.0, 0.0, d.y > 0.0 ? -1.0 : 1.0);
    } else if (a.x >= a.z) {
        face = d.x > 0.0 ? 0 : 1;
        forward = vec3(sign(d.x), 0.0, 0.0);
        up = vec3(0.0, 1.0, 0.0);
    } else {
        face = d.z > 0.0 ? 2 : 3;
        forward = vec3(0.0, 0.0, sign(d.z));
        up = vec3(0.0, 1.0, 0.0);
    }
    vec3 right = cross(forward, up);
    float ahead = max(dot(d, forward), 1.0e-4);
    vec2 uv = vec2(dot(d, right), -dot(d, up)) / ahead * 0.5 + 0.5;
    vec2 size = vec2(textureSize(water_sky, 0));
    vec2 inset = vec2(3.0, 2.0) / size; // (half a texel of the 3 x 2 atlas, in a face's 0 - 1)
    uv = clamp(uv, inset, 1.0 - inset);
    return textureLod(water_sky, (vec2(float(face % 3), float(face / 3)) + uv) / vec2(3.0, 2.0), 0.0).rgb;
}

// Where camera-space `q` is on the screen (the copy's coordinates) and its depth along the view (clip w); false behind.
bool water_on_screen(vec4 clip, out vec2 uv, out float edge)
{
    if (clip.w <= 1.0e-3) {
        return false;
    }
    vec2 ndc = clip.xy / clip.w;
    edge = max(abs(ndc.x), abs(ndc.y));
    vec2 pixel = water.viewport.xy + vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * water.viewport.zw;
    uv = pixel * water.target.xy;
    return edge < 1.0;
}

// (round F - a linear frame) What was drawn before the water, as the water's colours are (the game's display-referred
// ones): the copy of a linear frame holds light - encoded back (ff_shaders.cpp frame_linear / display_from_linear).
vec3 water_scene_at(vec2 uv)
{
    vec3 c = textureLod(water_scene, uv, 0.0).rgb;
    return frame_linear() ? display_from_linear(c) : c;
}

// (round WAVES) A display-referred colour as the light the water's arithmetic works in: linear light in a linear frame
// (the path tracer's), the display's values in the original's look.
vec3 water_light_of(vec3 display) { return frame_linear() ? linear_from_display(display) : display; }

// The sky seen on the screen toward camera-space direction `d` (a point at infinity): its colour where the copy shows
// the sky there (nothing drawn), weight in w.
vec4 water_screen_sky(vec3 d)
{
    vec2 uv;
    float edge;
    if (!water_on_screen(water.projection * vec4(d, 0.0), uv, edge)) {
        return vec4(0.0);
    }
    if (textureLod(water_depth, uv, 0.0).r < kWaterFar) {
        return vec4(0.0);
    }
    return vec4(water_scene_at(uv), 1.0 - smoothstep(0.85, 1.0, edge));
}

// The reflection found on the screen: a ray from camera-space `origin` along `d` marched over the depth copy in growing
// steps, the crossing refined by halving; the colour there, weight in w (fading toward the view's edges). None where the
// ray leaves the view, passes behind something thick or finds nothing.
vec4 water_screen_trace(vec3 origin, vec3 d)
{
    float travel = max(water_view_depth(origin) * 0.015, 6.0);
    float before = 0.0;
    for (int i = 0; i < 22; ++i) {
        vec2 uv;
        float edge;
        vec4 clip = water.projection * vec4(origin + d * travel, 1.0);
        if (!water_on_screen(clip, uv, edge)) {
            return vec4(0.0);
        }
        float scene = textureLod(water_depth, uv, 0.0).r;
        float ahead = clip.w - scene;
        if (ahead > 0.0 && scene < kWaterFar) {
            if (ahead > max((travel - before) * 1.5, 20.0)) {
                return vec4(0.0); // (passed behind something: what is reflected there is hidden)
            }
            float low = before;
            float high = travel;
            for (int j = 0; j < 5; ++j) {
                float middle = 0.5 * (low + high);
                vec4 at = water.projection * vec4(origin + d * middle, 1.0);
                vec2 uv_middle;
                float edge_middle;
                if (water_on_screen(at, uv_middle, edge_middle) && at.w > textureLod(water_depth, uv_middle, 0.0).r) {
                    high = middle;
                } else {
                    low = middle;
                }
            }
            vec4 hit = water.projection * vec4(origin + d * high, 1.0);
            if (!water_on_screen(hit, uv, edge)) {
                return vec4(0.0);
            }
            return vec4(water_scene_at(uv), 1.0 - smoothstep(0.8, 1.0, edge));
        }
        before = travel;
        travel *= 1.32;
    }
    return vec4(0.0);
}

// `albedo`: (round POOL) the program's colour under the game's neutral light - the texel x the material's colour, the
// rays' albedo for this material (ff_shaders.cpp water_albedo_program); display-referred
vec4 water_shade(vec4 colour, vec4 texel, vec2 uv, inout float alpha, vec3 albedo)
{
    // where the surface is, which way it faces and from where it is seen (derivatives first: uniform control flow)
    vec3 eye = v_surface_eye;
    vec3 world = (water.eye_to_world * vec4(eye, 1.0)).xyz;
    // (the moving surface: which way it faces, its footprint and its waves are the rest point's - the swell's normal is
    // the moved surface's there; seen from where it has moved to)
    vec3 rest = water_rest(world);
    vec3 dx = dFdx(rest);
    vec3 dy = dFdy(rest);
    vec3 face = cross(dx, dy);
    face = dot(face, face) > 0.0 ? normalize(face) : vec3(0.0, 1.0, 0.0);
    vec3 to_eye = water.camera.xyz - world;
    float dist = max(length(to_eye), 1.0e-3);
    vec3 v = to_eye / dist;
    if (dot(face, v) < 0.0) {
        face = -face;
    }
    float footprint = max(length(dx), length(dy));
    // the texture's mean colour (its smallest level) - the water's own colour - says whether it looks like water at all
    vec3 mean = textureLod(t0_map, uv, 16.0).rgb;
    float weight = smoothstep(0.82, 0.94, face.y) * water_hue_weight(mean);

    // 1. the waves' normal
    float t = water.time.x;
    vec3 swell = water_swell(water_sea(), rest.xz, t, footprint);
    vec3 ripple = water_ripples(rest.xz, t);
    // (added by the rain - engine/gfx/rain.h rain_ripple) the drops' rings: misc.y the cell (0: no rain), misc.w the share
    // of the rain that reaches the body (round W: the air volume's sky reach over it)
    if (water.misc.y > 0.0) {
        ripple.xy += rain_ripple(rest.xz, vec4(water.misc.y, RAIN_RIPPLE_LAYERS, t, RAIN_RIPPLE_LIFE)) * water.misc.w;
    }
    vec3 n = water_wave_normal(swell, ripple.xy);
    // the reflection's normal: the waves' own facet, as the path tracer meets it (vulkan_path_reference.cpp
    // path_water_facet) - a facet turned away from the eye is the plane's; Fresnel (Schlick) at that facet.
    // (round SHEETS) Two grazing approximations the reference doesn't make removed: the waves' tilt cut to 0.3 at a
    // grazing view ("a pixel spans many facets, whose mean is flatter" - the reference averages the facets' reflections
    // over the pixel, it never flattens them; the footprint's fade of the waves - water_wave_fade, the normal's mip
    // filtering - stays), and Fresnel's grazing reflectance held to 1 - the slope ("rough water never mirrors fully" -
    // the reference's facets reflect by Fresnel to 1 at grazing).
    vec3 n_mirror = dot(n, v) > 0.0 ? n : face;
    float cos_v = clamp(dot(n_mirror, v), 0.0, 1.0);
    float fresnel = kWaterF0 + (1.0 - kWaterF0) * pow(1.0 - cos_v, 5.0);

    // the water's colours: the scattering seen in deep water, from the tint (or the texture's own) at the light the game
    // gave the surface here (the program's brightness over its texel's: the bake, the pass colours)
    float gain = clamp(water_luma(colour.rgb) / max(water_luma(texel.rgb), 0.02), 0.0, 4.0);
    // auto: the game's own colour here without the texture's detail - the texture's mean through what the program did
    // to its texel (the bake's and the pass colours' tints: a level's painted shallows keep their colour)
    vec3 own = mean * clamp(colour.rgb / max(texel.rgb, vec3(0.02)), vec3(0.0), vec3(4.0));
#ifdef LIGHT_FRONT
    // (NOT ORIGINAL - round FL: the front layer, ff_shaders.cpp kLightFrontGlsl) where this water is the view's nearest
    // see-through surface (the layer's depth along the view within the waves' reach of the fragment's - the layer holds
    // the rest surface), its light is the light list's there - the disc's and the rest's on a white diffuser - not the
    // vertex's: the body's colour is the texture's mean lit by it
    float front_z = light_front_depth();
    if (front_z < 1.0) {
        // (round POOL) compared as depths, as every other key's match is (light_front_find - one rule, the draw's own
        // depth against the layer's): the reach 1 % of the distance or the waves' (eye units over the distance), a share
        // of what is left of the depth, (1 - z) falling as 1 / distance
        float reach = max(0.01, (2.0 * water.sea.x + 1.0) / max(water_view_depth(eye), 1.0e-3));
        light_front_at = abs(gl_FragCoord.z - front_z) <= max(1.0e-6, (1.0 - front_z) * reach);
    }
    if (light_front_at) {
        vec3 front_light = max(light_front_now().rgb + light_front_sun_now().rgb, vec3(0.0));
        own = display_from_linear(linear_from_display(mean) * front_light);
        gain = clamp(water_luma(own) / max(water_luma(mean), 0.02), 0.0, 4.0);
    }
#endif
    vec3 tint = water.tint.w > 0.5 ? own : water.tint.rgb * gain;
    // (round WAVES) in a linear frame the water's light is worked in linear light, as the path tracer's: its hue, its
    // body's reflectance x the light on it, what is behind, the Fresnel mix (water_light_of; the original's look - a
    // display-referred frame - as before). In display-referred values the deep reflectance (0.65 a unit) multiplied the
    // encoded colour - 0.65^2.2 = 0.39 of the light - and the Fresnel mix weighed encoded colours.
    vec3 tint_light = water_light_of(tint);
    vec3 hue = clamp(tint_light / max(max(tint_light.r, tint_light.g), max(tint_light.b, 1.0e-3)), 0.03, 1.0);
    float light = water_luma(tint_light);
    // (round MEDIUM: the water's body's one definition with the path tracer's - rt_volume_light.h kWaterMediumGlsl)
    vec3 deep = water_deep_reflectance(hue) * light;
    // (round POOL) the texture's own detail, faintly, on the scattered colour (the covered body below has its texels)
    deep *= mix(1.0, clamp(water_luma(texel.rgb) / max(water_luma(mean), 0.02), 0.5, 1.6), water.waves.w);
    // (NOT ORIGINAL - round POOL: one water material with the rays and the path tracer - vulkan_rt_scene.cpp hit_blend,
    // vulkan_path_reference.cpp) the game's water sheet is an interface whose texture's coverage is the water's own
    // colour: the drawn alpha (the texel's x the vertex's, doubled - the rays' record_drawn_alpha; 1 for an opaque sheet:
    // water.target.w) is the share of what is under the surface seen as the body itself - its albedo (the rays': the
    // texel x the material's colour) lit as a diffuse surface by the front layer's light where it is this water (the
    // list's light on a white diffuser), else the game's light on it - and the rest is what is behind it, as the rays'
    // transmittance through the sheet has it for the light reaching the bed (hit_blend_transmittance). (Before, the whole
    // of the water was the copy behind it with the scattered colour - the sheet's own colour, which the path tracer
    // shows by its coverage, was nowhere.)
    float cover = water.target.w > 0.5 ? clamp(colour.a, 0.0, 1.0) : 1.0;
    vec3 body = water_light_of(colour.rgb);
#ifdef LIGHT_FRONT
    if (light_front_at) {
        body = water_light_of(display_from_linear(linear_from_display(clamp(albedo, 0.0, 1.0)) *
                                                  max(light_front_now().rgb + light_front_sun_now().rgb, vec3(0.0))));
    }
#endif

    // 3. refraction: what is behind the water (the copy made before the view's first water), moved by the waves' tilt
    // in proportion to the water behind; 4. absorption along the path through it
    bool copied = water.target.z > 0.5;
    float surface_w = max(water_view_depth(eye), 1.0e-3);
    vec3 tilt = (water.world_to_eye * vec4(n - face, 0.0)).xyz; // (the waves' tilt from the surface: none when calm)
    vec3 under = deep;
    float behind = kWaterFar * 10.0;
    if (copied) {
        vec2 here = gl_FragCoord.xy * water.target.xy;
        float straight = textureLod(water_depth, here, 0.0).r;
        float thick = clamp(straight - surface_w, 0.0, 300.0);
        float focal = abs(water.projection[1][1]) * water.viewport.w * 0.5;
        vec2 shift = vec2(tilt.x, -tilt.y) * (water.camera.w * thick * focal / surface_w);
        vec2 low = (water.viewport.xy + 0.5) * water.target.xy;
        vec2 high = (water.viewport.xy + water.viewport.zw - 0.5) * water.target.xy;
        vec2 bent_uv = clamp((gl_FragCoord.xy + shift) * water.target.xy, low, high);
        float bent = textureLod(water_depth, bent_uv, 0.0).r;
        if (bent <= surface_w) {
            bent_uv = here; // (something in front of the water there: not refracted)
            bent = straight;
        }
        under = water_light_of(water_scene_at(bent_uv));
        behind = bent;
    }
    bool seen_behind = copied;
    bool traced_through = false; // (round SHEETS: what is behind traced - the sheet a whole interface)
#ifdef LIGHT_FRONT
    // (NOT ORIGINAL - round SHEETS) where the front layer is this water, what is behind it is the layer's traced other
    // side (vulkan_front_layer.h): the view's ray gone on past the rest surface to what it meets - the path tracer's thin
    // interface, straight on - lit by the light list, through the see-through surfaces it crossed; its distance from the
    // surface the path through the water. Not the copy made before the view's first water draw: whatever was drawn after
    // that copy - a sheet under this one, a slope or the coins lying in the water among the transparencies - is missing
    // from it (each sheet replaced what was under it). Nothing met: deep water (the body's colour alone).
    // Where the trace meets the very surface the copy holds there (its depth along the view within the depth's precision
    // of the copy's, 0.2 % + 0.25 units: the coins a unit over the fountain's floor are not the floor), the copy is
    // kept: that surface lit by the view's own passes (ReSTIR's - not one light sample a frame), and the refraction's shift - screen space first, the trace where the screen lacks what is behind (as 2026's hybrid
    // screen / ray-traced translucency and reflections do).
    vec4 traced_behind = light_front_behind_at(ivec2(gl_FragCoord.xy));
    if (traced_behind.a != 0.0) {
        traced_through = true;
        // (a: the hit's distance - negative where a blended layer lies on it, whose raster draw may come after the copy:
        // the trace then; -65504: nothing met)
        bool met = traced_behind.a > -65000.0;
        bool layer_on_it = traced_behind.a < 0.0 && met;
        float traced_w = met ? surface_w * (1.0 + abs(traced_behind.a) / dist) : kWaterFar * 10.0;
        float copy_w = copied ? textureLod(water_depth, gl_FragCoord.xy * water.target.xy, 0.0).r : -1.0;
        if (layer_on_it || !(copied && abs(traced_w - copy_w) <= 0.002 * copy_w + 0.25)) {
            under = met ? water_light_of(display_from_linear(max(traced_behind.rgb, vec3(0.0)))) : deep;
            behind = traced_w;
            seen_behind = true;
        }
    }
#endif
    float path = max(behind - surface_w, 0.0) * dist / surface_w;
    float depth_down = path * clamp(v.y, 0.0, 1.0);
    vec3 sigma = water_extinction(hue, water.waves.y);
    vec3 through = exp(-sigma * min(path, 1.0e5));
    vec3 lit_below = exp(-sigma * min(depth_down, 1.0e5));
    vec3 refracted = under * through * lit_below + deep * (1.0 - through);
    // (round POOL) the covered share the body's own colour; without the copy the blend over what is behind is the game's
    // alpha's (the draw keeps it), so the colour is the body's alone
    refracted = seen_behind ? mix(refracted, body, cover) : body;

    // 2. the reflection along the wave normal: on the screen where the ray finds something, else the sky - as the
    // screen shows it toward that direction, or as the level's sky was captured
    vec3 r = reflect(-v, n_mirror);
    // (round SHEETS) a reflection the facet sends into the water meets the next wave's face and leaves upward - mirrored
    // by the plane, as the path tracer's (before: held at 0.02 over the horizon)
    if (dot(r, face) <= 0.0) {
        r = reflect(r, face);
    }
    r = normalize(r);
    vec3 r_eye = (water.world_to_eye * vec4(r, 0.0)).xyz;
    vec3 sky = water.sky.w > 0.5 ? water_capture(r) : water.sky.rgb;
    if (copied) {
        vec4 screen_sky = water_screen_sky(r_eye);
        sky = mix(sky, screen_sky.rgb, screen_sky.w);
    }
    vec4 found = (copied && water.misc.x > 0.5) ? water_screen_trace(eye, r_eye) : vec4(0.0);
    vec3 reflected = mix(sky, found.rgb, found.w);
#ifdef LIGHT_FRONT
    // (NOT ORIGINAL - round LW) where the front layer is this water, what its rest surface mirrors - a ray traced from it
    // and its hit lit by the light list as everything else is (vulkan_front_layer.h): the lamps, the lit walls, the
    // skater, the sky - not the screen's. The waves move the mirror image by twice their tilt x its distance (the image's
    // alpha), on the screen: (round WATER) seen where the image is - as far behind the surface along the view as the hit is
    // in front of it, its depth along the view surface_w x (1 + a / dist) -, so a far hit or the sky (a = 65504) moves by
    // twice the tilt's angle (2 x tilt x focal x dist / surface_w), not by its distance over the surface's (the sea's sky
    // read from the view's edge: streaks).
    // (round WAVES) Where: the point the waves' reflected ray reaches (its hit's distance along it) mirrored by the rest
    // plane - water_mirror_image -, seen on the screen: the flat mirror's image of that point is what the front layer
    // traced there. (Before, the image moved by the tilt's part across the screen alone: at a grazing view a wave turned
    // toward the eye tilts the normal mostly along the view - the mirror image moved by a sixth of its angle at 10
    // degrees, the reflection near the calm mirror's.)
    vec4 mirror_here = light_front_reflect_at(ivec2(gl_FragCoord.xy));
    if (mirror_here.a > 0.0) {
        ivec2 low = ivec2(water.viewport.xy);
        ivec2 high = ivec2(water.viewport.xy + water.viewport.zw) - 1;
        ivec2 image_at = ivec2(gl_FragCoord.xy);
        vec4 image_clip = water.projection * (water.world_to_eye * vec4(water_mirror_image(world, face, r, mirror_here.a), 1.0));
        if (image_clip.w > 1.0e-3) {
            vec2 ndc = image_clip.xy / image_clip.w;
            image_at = ivec2(floor(water.viewport.xy + vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * water.viewport.zw));
        }
        vec4 mirror = light_front_reflect_at(clamp(image_at, low, high));
        reflected = display_from_linear(max((mirror.a > 0.0 ? mirror : mirror_here).rgb, vec3(0.0)));
        found = vec4(0.0, 0.0, 0.0, 1.0);
    }
#endif

    // 6. the sun's glitter: a sharp highlight of the scene's sun on the wave normals (normalised Blinn-Phong, Fresnel
    // at the half vector), where the bake says the water is in the sun
    vec3 glitter = vec3(0.0);
    if (water.sun.w > 0.5) {
        vec3 l = water.sun.xyz;
        vec3 h = normalize(l + v);
        float nh = max(dot(n, h), 0.0);
        float f = kWaterF0 + (1.0 - kWaterF0) * pow(1.0 - max(dot(v, h), 0.0), 5.0);
        float sunlit = smoothstep(0.3, 0.6, water_luma(v_diffuse.rgb * 2.0)) * smoothstep(0.0, 0.05, l.y);
#ifdef LIGHT_FRONT
        if (light_front_at) {
            sunlit = light_front_sun_now().a * smoothstep(0.0, 0.05, l.y); // (round FL: the disc's visibility traced there)
        }
#endif
        float spec = 190.0 * pow(nh, 1500.0) * f * max(dot(n, l), 0.0);
        glitter = water.sun_colour.rgb * min(spec, water.sun_colour.w) * sunlit;
    }

    // 5. foam where the waves break at the shore - water shallower than the waves' height over 0.78 (McCowan): none on
    // still water -, in the ripples' drifting pattern
    float surf = water.sea.x / 0.78;
    float shore = copied && surf > 0.01 ? 1.0 - smoothstep(0.0, surf, depth_down) : 0.0;
    float pattern = smoothstep(0.3, 0.75, ripple.z + 0.2 * sin(t * 1.7 - depth_down * 0.4));
    // whitecaps: on the swell's sharpened crests, broken up by the ripples' pattern, as much as the wind whitens the sea
    // (sea.w - Monahan and O'Muircheartaigh's share)
    float caps = water.sea.w * smoothstep(0.04, 0.14, swell.y) * smoothstep(0.35, 0.7, ripple.z);
    float foam = clamp(water.waves.z * max(shore * mix(0.3, 1.0, pattern), 0.85 * caps), 0.0, 1.0);
    vec3 foam_colour = vec3(0.9, 0.93, 0.95) * clamp(gain, 0.15, 1.2); // (white, at the light the game gave the surface)

    vec3 shaded = mix(refracted, water_light_of(reflected), fresnel) + water_light_of(glitter) * (1.0 - foam);
    shaded = mix(shaded, water_light_of(foam_colour), foam);
    shaded = frame_linear() ? display_from_linear(shaded) : shaded; // (the draw's output: display-referred, decoded after)
    if (water.misc.z > 0.5) {
        // ([debug] rt_water_view=1) what the reflection found: red the screen's march, green the sky on the screen,
        // blue the captured sky; the water's depth below in the alpha-less grey of the rest
        shaded = vec3(found.w, copied ? water_screen_sky(r_eye).w : 0.0, water.sky.w * (1.0 - found.w)) * fresnel * 2.0 +
                 vec3(clamp(depth_down / 400.0, 0.0, 1.0)) * 0.25;
    }
    if (any(isnan(shaded)) || any(isinf(shaded))) {
        shaded = colour.rgb;
    }
    shaded = max(shaded, vec3(0.0));
    // where the game drew any water it is all shaded here (what is behind is in it); where it drew none, still none
    // (round SHEETS) where what is behind was traced the sheet is the whole interface, as the path tracer meets it -
    // its coverage the body's share above, the rest what the trace found: nothing of what was drawn under it is kept
    // (before, alpha x 4 of it replaced and the rest let the frame show - the frame without what was drawn after the copy)
    alpha = mix(colour.a, traced_through ? 1.0 : (copied ? clamp(colour.a * 4.0, 0.0, 1.0) : colour.a), weight);
    return vec4(mix(colour.rgb, shaded, weight), colour.a);
})";

} // namespace

std::string_view water_waves_glsl() { return kWaterWavesGlsl; }

std::string water_declarations_glsl(bool moving)
{
    std::string out(kWaterWavesGlsl);
    out += kWaterCommonGlsl;
    out += render::rain::glsl_constants(); // (added by the rain: the drops' rings)
    out += render::rain::ripple_glsl();
    out += moving ? kWaterRestMovingGlsl : kWaterRestFlatGlsl;
    out += render::rt::kWaterMediumGlsl; // (round MEDIUM)
    out += kWaterGlsl;
    return out;
}

std::string water_vertex_declarations_glsl()
{
    std::string out(kWaterWavesGlsl);
    out += kWaterCommonGlsl;
    out += kWaterVertexGlsl;
    return out;
}

std::string_view water_vertex_move_glsl()
{
    // (after the camera-space position `eye` is made, before it is projected: the point moved in the world - added in
    // camera space, so a still surface's position is the same to the bit - and the move handed on)
    return "    vec3 water_move = water_lift((water.eye_to_world * vec4(eye.xyz, 1.0)).xyz, eye.xyz);\n"
           "    vec3 water_move_eye = (water.world_to_eye * vec4(water_move, 0.0)).xyz;\n"
           "    eye.xyz += water_move_eye;\n"
           "    v_water_shift = water_move.xz;\n"
           "    v_water_rise = water_move.y;";
}

} // namespace render::vulkan
