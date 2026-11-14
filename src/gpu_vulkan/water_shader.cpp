// NOT ORIGINAL - the water's GLSL (water_shader.h; the formulas are explained in vulkan_water.h).
#include "gpu_vulkan/water_shader.h"

#include <cstdint>
#include <string>
#include <string_view>

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
} water;
layout(set = 0, binding = 12) uniform sampler2D water_depth;

const float kWaterFar = 1.0e8;      // the depth copy's sky (1e9) and anything as far
const float kWaterGravity = 386.0;  // units (inches) a second squared: deep water's waves' speeds
const float kWaterSharpness = 0.6;  // the crests' sharpening (Gerstner's Q)

// Wave i of the swell (four Gerstner waves about the wind's direction) at rest point p, time t: its direction (xy),
// wavenumber (z) and phase (w); `slope` its slope's amplitude (its height x k) - the wind raises the waves too (x 0.4
// still, x 1 at the default breeze, x 1.6 in a gale).
vec4 water_wave(int i, vec2 p, float t, out float slope)
{
    const vec4 lengths = vec4(1.0, 0.63, 0.41, 0.27);
    const vec4 turns = vec4(0.0, 0.42, -0.57, 1.05);
    const vec4 steepness = vec4(0.12, 0.10, 0.085, 0.07);
    vec2 wind = water.time.yz;
    float raised = water.waves.x * (0.4 + 0.6 * water.time.w);
    float wavelength = lengths[i] * water.misc.y;
    float k = 6.2831853 / wavelength;
    float speed = sqrt(kWaterGravity / k);
    float c = cos(turns[i]);
    float s = sin(turns[i]);
    vec2 d = vec2(wind.x * c - wind.y * s, wind.x * s + wind.y * c);
    slope = steepness[i] * raised;
    return vec4(d, k, k * (dot(d, p) - speed * t) + float(i) * 1.913);
}

// A wave faded out where a pixel spans a good part of its wavelength (no aliasing far away).
float water_wave_fade(float footprint, float wavelength) { return 1.0 - smoothstep(0.05, 0.2, footprint / wavelength); }
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
    if (water.target.z < 0.5 || eye.z <= 1.0e-3) {
        return 1.0;
    }
    vec4 clip = water.projection * vec4(eye, 1.0);
    vec2 ndc = clamp(clip.xy / clip.w, vec2(-1.0), vec2(1.0));
    vec2 pixel = water.viewport.xy + 0.5 + vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * max(water.viewport.zw - 1.0, 0.0);
    float behind = textureLod(water_depth, pixel * water.target.xy, 0.0).r;
    if (behind >= kWaterFar) {
        return 1.0;
    }
    float down = max(behind - eye.z, 0.0) * dist / eye.z * clamp(abs(to_eye.y) / dist, 0.0, 1.0);
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
    for (int i = 0; i < 4; ++i) {
        float slope;
        vec4 w = water_wave(i, world.xz, water.time.x, slope);
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
float water_hue_weight(vec3 mean)
{
    float luma = water_luma(mean);
    return (1.0 - smoothstep(0.04, 0.12, mean.r - mean.b)) * smoothstep(0.06, 0.12, luma) *
           (1.0 - smoothstep(0.58, 0.68, luma));
}

// The swell (water_wave) at rest point p as its normal's parts - x and z the slope, y the crests' sharpening -, each wave
// faded out where a pixel spans a good part of its wavelength. The moving surface's shape is the same waves'
// (water_lift): this is its normal.
vec3 water_swell(vec2 p, float t, float footprint)
{
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        float slope;
        vec4 w = water_wave(i, p, t, slope);
        float a = slope * water_wave_fade(footprint, 6.2831853 / w.z);
        sum += vec3(w.x * a * cos(w.w), kWaterSharpness * a * sin(w.w), w.y * a * cos(w.w));
    }
    return sum;
}

// The ripples: the tiling texture at two scales drifting with the wind - their slope (x, z) and the foam's pattern (z).
vec3 water_ripples(vec2 p, float t)
{
    vec2 along = water.time.yz;
    vec2 across = vec2(-along.y, along.x);
    vec2 q = vec2(dot(p, along), dot(p, across));
    float strength = water.time.w;
    float drift = t * (0.4 + 0.6 * strength);
    vec4 a = texture(water_waves, q / 263.0 + vec2(drift * 0.031, 0.0));
    vec4 b = texture(water_waves, q / 97.0 + vec2(drift * 0.083, drift * 0.019) + vec2(0.37, 0.71));
    // (above the default breeze the wind roughens them faster: x 2.2 in a gale; the waves' setting up to x 3)
    float windy = 0.3 + 0.7 * strength + 0.5 * max(strength - 1.0, 0.0);
    vec2 slope = (a.xy * 0.55 + b.xy * 0.45) * windy * min(water.waves.x * 2.0, 3.0);
    return vec3(along * slope.x + across * slope.y, a.w * 0.55 + b.w * 0.45);
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
    return vec4(textureLod(water_scene, uv, 0.0).rgb, 1.0 - smoothstep(0.85, 1.0, edge));
}

// The reflection found on the screen: a ray from camera-space `origin` along `d` marched over the depth copy in growing
// steps, the crossing refined by halving; the colour there, weight in w (fading toward the view's edges). None where the
// ray leaves the view, passes behind something thick or finds nothing.
vec4 water_screen_trace(vec3 origin, vec3 d)
{
    float travel = max(origin.z * 0.015, 6.0);
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
            return vec4(textureLod(water_scene, uv, 0.0).rgb, 1.0 - smoothstep(0.8, 1.0, edge));
        }
        before = travel;
        travel *= 1.32;
    }
    return vec4(0.0);
}

vec4 water_shade(vec4 colour, vec4 texel, vec2 uv, inout float alpha)
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
    vec3 swell = water_swell(rest.xz, t, footprint);
    vec3 ripple = water_ripples(rest.xz, t);
    vec3 n = normalize(vec3(-(swell.x + ripple.x), max(1.0 - swell.y, 0.2), -(swell.z + ripple.y)));
    // the reflection's normal: the waves' tilt lessened where the water is seen at a grazing angle (a pixel there spans
    // many facets, whose mean is flatter - the reflection is stretched along the water, not broken up), and Fresnel's
    // grazing reflectance held below 1 by the waves' slope (rough water never mirrors fully)
    float facing = clamp(dot(face, v), 0.0, 1.0);
    vec3 n_mirror = normalize(face + (n - face) * mix(0.3, 1.0, smoothstep(0.05, 0.45, facing)));
    float slope = clamp(length(n - face) * 1.5, 0.0, 0.5);
    float cos_v = clamp(dot(n_mirror, v), 0.0, 1.0);
    float fresnel = kWaterF0 + (max(1.0 - slope, kWaterF0) - kWaterF0) * pow(1.0 - cos_v, 5.0);

    // the water's colours: the scattering seen in deep water, from the tint (or the texture's own) at the light the game
    // gave the surface here (the program's brightness over its texel's: the bake, the pass colours)
    float gain = clamp(water_luma(colour.rgb) / max(water_luma(texel.rgb), 0.02), 0.0, 4.0);
    // auto: the game's own colour here without the texture's detail - the texture's mean through what the program did
    // to its texel (the bake's and the pass colours' tints: a level's painted shallows keep their colour)
    vec3 own = mean * clamp(colour.rgb / max(texel.rgb, vec3(0.02)), vec3(0.0), vec3(4.0));
    vec3 tint = water.tint.w > 0.5 ? own : water.tint.rgb * gain;
    vec3 hue = clamp(tint / max(max(tint.r, tint.g), max(tint.b, 1.0e-3)), 0.03, 1.0);
    float light = water_luma(tint);
    vec3 deep_hue = hue * hue; // (deep water's colour more saturated than the texture's mean, which has its highlights)
    vec3 deep = deep_hue * (light * 0.65 / max(water_luma(deep_hue), 0.05));

    // 3. refraction: what is behind the water (the copy made before the view's first water), moved by the waves' tilt
    // in proportion to the water behind; 4. absorption along the path through it
    bool copied = water.target.z > 0.5;
    float surface_w = max(eye.z, 1.0e-3);
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
        under = textureLod(water_scene, bent_uv, 0.0).rgb;
        behind = bent;
    }
    float path = max(behind - surface_w, 0.0) * dist / surface_w;
    float depth_down = path * clamp(v.y, 0.0, 1.0);
    vec3 sigma = (3.0 / max(water.waves.y, 1.0)) * (0.2 + 1.6 * (1.0 - hue));
    vec3 through = exp(-sigma * min(path, 1.0e5));
    vec3 lit_below = exp(-sigma * min(depth_down, 1.0e5));
    vec3 refracted = under * through * lit_below + deep * (1.0 - through);

    // 2. the reflection along the wave normal: on the screen where the ray finds something, else the sky - as the
    // screen shows it toward that direction, or as the level's sky was captured
    vec3 r = reflect(-v, n_mirror);
    r = normalize(vec3(r.x, max(r.y, 0.02), r.z));
    vec3 r_eye = (water.world_to_eye * vec4(r, 0.0)).xyz;
    vec3 sky = water.sky.w > 0.5 ? water_capture(r) : water.sky.rgb;
    if (copied) {
        vec4 screen_sky = water_screen_sky(r_eye);
        sky = mix(sky, screen_sky.rgb, screen_sky.w);
    }
    vec4 found = (copied && water.misc.x > 0.5) ? water_screen_trace(eye, r_eye) : vec4(0.0);
    vec3 reflected = mix(sky, found.rgb, found.w);

    // 6. the sun's glitter: a sharp highlight of the scene's sun on the wave normals (normalised Blinn-Phong, Fresnel
    // at the half vector), where the bake says the water is in the sun
    vec3 glitter = vec3(0.0);
    if (water.sun.w > 0.5) {
        vec3 l = water.sun.xyz;
        vec3 h = normalize(l + v);
        float nh = max(dot(n, h), 0.0);
        float f = kWaterF0 + (1.0 - kWaterF0) * pow(1.0 - max(dot(v, h), 0.0), 5.0);
        float sunlit = smoothstep(0.3, 0.6, water_luma(v_diffuse.rgb * 2.0)) * smoothstep(0.0, 0.05, l.y);
        float spec = 190.0 * pow(nh, 1500.0) * f * max(dot(n, l), 0.0);
        glitter = water.sun_colour.rgb * min(spec, water.sun_colour.w) * sunlit;
    }

    // 5. foam where the water is shallow (meets geometry), in the ripples' drifting pattern
    float shore = copied ? 1.0 - smoothstep(0.0, 24.0, depth_down) : 0.0;
    float pattern = smoothstep(0.3, 0.75, ripple.z + 0.2 * sin(t * 1.7 - depth_down * 0.4));
    // whitecaps above the default breeze: on the swell's sharpened crests, broken up by the ripples' pattern (none at
    // wind 1 or below)
    float caps = smoothstep(1.0, 2.0, water.time.w) * smoothstep(0.04, 0.14, swell.y) * smoothstep(0.35, 0.7, ripple.z);
    float foam = clamp(water.waves.z * max(shore * mix(0.3, 1.0, pattern), 0.85 * caps), 0.0, 1.0);
    vec3 foam_colour = vec3(0.9, 0.93, 0.95) * clamp(gain, 0.15, 1.2); // (white, at the light the game gave the surface)

    vec3 shaded = mix(refracted, reflected, fresnel) + glitter * (1.0 - foam);
    shaded = mix(shaded, foam_colour, foam);
    // the texture's own detail, faintly
    float detail = clamp(water_luma(texel.rgb) / max(water_luma(mean), 0.02), 0.5, 1.6);
    shaded *= mix(1.0, detail, water.waves.w);
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
    alpha = mix(colour.a, copied ? clamp(colour.a * 4.0, 0.0, 1.0) : colour.a, weight);
    return vec4(mix(colour.rgb, shaded, weight), colour.a);
})";

} // namespace

std::string water_declarations_glsl(bool moving)
{
    std::string out(kWaterCommonGlsl);
    out += moving ? kWaterRestMovingGlsl : kWaterRestFlatGlsl;
    out += kWaterGlsl;
    return out;
}

std::string water_vertex_declarations_glsl()
{
    std::string out(kWaterCommonGlsl);
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
