// NOT ORIGINAL - the fixed-function emulation's GLSL generator (ff_shaders.h). The formulas are Direct3D 9's as its
// documentation states them ("Mathematics of Lighting", "Fog Formulas", D3DTEXTUREOP, D3DTSS_TCI_, ps_1_1); where the
// documentation leaves something open, the comment beside the code says what is assumed.
#include "gpu_vulkan/ff_shaders.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

#include "base/core/checked_index.h"
#include "base/core/enum_ops.h"
#include "engine/gfx/mesh_formats.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"

namespace render::vulkan {

namespace {

constexpr uint8_t kAlphaFlagBit = 1U;

template <typename Number>
std::string num(Number value)
{
    return std::to_string(value);
}

// (added by the upscaler) The motion's two varyings, after the four stages' coordinates (3 - 6) and rt_pbr's four
// (7 - 10: kSurfaceVaryingsOut) - a draw can have both.
constexpr uint32_t kMotionNowVarying = 11;
constexpr uint32_t kMotionBeforeVarying = 12;

// One line of GLSL: the parts, then a new line.
void add(std::string& out, std::initializer_list<std::string_view> parts)
{
    for (const std::string_view part : parts) {
        out.append(part);
    }
    out += '\n';
}

bool has_stage(uint32_t mask, std::size_t stage) { return (mask & (1U << stage)) != 0U; }

// (NOT ORIGINAL - rt_pbr) the per-pixel lighting's inputs (the vertex shader writes them, the fragment shader reads
// them: "the per-pixel lighting" below).
constexpr const char* kSurfaceVaryingsOut =
    "layout(location = 7) out vec3 v_surface_normal;\nlayout(location = 8) out vec3 v_surface_eye;\n"
    "layout(location = 9) out vec4 v_surface_base;\nlayout(location = 10) out vec4 v_surface_cd;";
constexpr const char* kSurfaceVaryingsIn =
    "layout(location = 7) in vec3 v_surface_normal;\nlayout(location = 8) in vec3 v_surface_eye;\n"
    "layout(location = 9) in vec4 v_surface_base;\nlayout(location = 10) in vec4 v_surface_cd;";

// --- the vertex shader -------------------------------------------------------------------------------------------

void vertex_declarations(std::string& out, const VertexKey& key)
{
    add(out, {"layout(set = 0, binding = ", num(kVertexUniformsBinding), ", std140) uniform VertexUniforms {"});
    add(out, {"    mat4 world_view;\n    mat4 projection;\n    mat4 normal_matrix;\n    mat4 texture_matrix[",
              num(kStageCount), "];"});
    add(out, {"    vec4 pixel;\n    vec4 ambient;\n    vec4 material_diffuse;\n    vec4 material_ambient;\n"
              "    vec4 material_specular;\n    vec4 material_emissive;\n    vec4 material_power;\n    vec4 fog;\n"
              "    vec4 point;\n    vec4 point_scale;"});
    for (const char* name : {"light_direction", "light_diffuse", "light_specular", "light_ambient"}) {
        add(out, {"    vec4 ", name, "[", num(kMaxLights), "];"});
    }
    add(out, {"    mat4 previous_clip;\n    vec4 motion_jitter;"});
    if (key.retro != 0U) {
        add(out, {"    vec4 retro;"}); // (NOT ORIGINAL - the retro art style: VertexUniforms::retro)
    }
    add(out, {"} u;"});
    const bool pre_transformed = fvf_pre_transformed(key.fvf);
    add(out, {"layout(location = ", num(kPositionLocation), ") in ", pre_transformed ? "vec4" : "vec3", " a_position;"});
    if ((key.fvf & kFvfNormal) != 0U) {
        add(out, {"layout(location = ", num(kNormalLocation), ") in vec3 a_normal;"});
    }
    if ((key.fvf & kFvfDiffuse) != 0U) {
        add(out, {"layout(location = ", num(kDiffuseLocation), ") in vec4 a_diffuse;"});
    }
    if ((key.fvf & kFvfSpecular) != 0U) {
        add(out, {"layout(location = ", num(kSpecularLocation), ") in vec4 a_specular;"});
    }
    static constexpr std::array<const char*, 4> kSizes = {"float", "vec2", "vec3", "vec4"};
    for (uint32_t set = 0; set < fvf_tex_count(key.fvf); ++set) {
        add(out, {"layout(location = ", num(kFirstTexSetLocation + set), ") in ", core::at(kSizes, fvf_tex_size(key.fvf, set) - 1),
                  " a_tex", num(set), ";"});
    }
    add(out, {"layout(location = 0) out vec4 v_diffuse;\nlayout(location = 1) out vec4 v_specular;\n"
              "layout(location = 2) out vec2 v_fog;"});
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        add(out, {"layout(location = ", num(3 + stage), ") out vec4 v_tex", num(stage), ";"});
    }
    if (key.motion != 0U) {
        add(out, {"layout(location = ", num(kPreviousPositionLocation), ") in vec3 a_previous;"});
        add(out, {"layout(location = ", num(kMotionNowVarying), ") out vec4 v_motion_now;"});
        add(out, {"layout(location = ", num(kMotionBeforeVarying), ") out vec4 v_motion_before;"});
    }
    if (key.surface != 0) {
        add(out, {kSurfaceVaryingsOut});
    }
    add(out, {key.points ? "out gl_PerVertex { vec4 gl_Position; float gl_PointSize; };"
                         : "out gl_PerVertex { vec4 gl_Position; };"});
}

// A coordinate set of n floats as Direct3D 9 hands it to the texture transform: the component after the last is 1,
// the rest 0 - (u) -> (u, 1, 0, 0), (u, v) -> (u, v, 1, 0), (u, v, w) -> (u, v, w, 1). A set the vertex doesn't have
// reads as zeros (assumed: Direct3D 9 leaves it undefined).
std::string expanded_set(const VertexKey& key, uint32_t set)
{
    if (set >= fvf_tex_count(key.fvf)) {
        return "vec4(0.0, 0.0, 1.0, 0.0)";
    }
    std::string name = "a_tex" + num(set);
    switch (fvf_tex_size(key.fvf, set)) {
    case 1:
        return "vec4(" + name + ", 1.0, 0.0, 0.0)";
    case 2:
        return "vec4(" + name + ", 1.0, 0.0)";
    case 3:
        return "vec4(" + name + ", 1.0)";
    default:
        return name;
    }
}

// The number of floats a stage's untransformed coordinates have (its set's size, or 3 for the generated vectors).
uint32_t source_size(const VertexKey& key, const TexCoordKey& coords)
{
    if (coords.gen != TexGen::kPassThrough) {
        return 3;
    }
    return coords.set < fvf_tex_count(key.fvf) ? fvf_tex_size(key.fvf, coords.set) : 2;
}

void vertex_position(std::string& out, const VertexKey& key)
{
    if (fvf_pre_transformed(key.fvf)) {
        // Pre-transformed: x, y in the render target's pixels (Direct3D 9's pixel centres at whole numbers, Vulkan's at
        // halves: the half pixel added), z the depth, the fourth the reciprocal of w (perspective-correct
        // interpolation keeps it). A zero reciprocal is taken as 1 (assumed: Direct3D 9 leaves it undefined).
        add(out, {"    float w = a_position.w != 0.0 ? 1.0 / a_position.w : 1.0;"});
        add(out, {"    vec2 ndc = (a_position.xy + 0.5 - u.pixel.zw) * 2.0 * u.pixel.xy - 1.0;"});
        add(out, {"    gl_Position = vec4(ndc * w, a_position.z * w, w);"});
        add(out, {"    vec4 eye = vec4(0.0, 0.0, w, 1.0);"});
        return;
    }
    // Camera space, then clip space. Direct3D's clip-space y points up and Vulkan's down; Vulkan's pixel centres are half
    // a pixel from Direct3D 9's, so the position moves half a pixel right and down (u.pixel.xy = 1 / the viewport's size;
    // zero turns the offset off). Depth is 0 - 1 in both.
    add(out, {"    vec4 eye = u.world_view * vec4(a_position, 1.0);"});
    add(out, {"    vec4 clip = u.projection * eye;"});
    add(out, {"    gl_Position = vec4(clip.x + clip.w * u.pixel.x, -clip.y + clip.w * u.pixel.y, clip.z, clip.w);"});
    if (key.retro != 0U) {
        // (NOT ORIGINAL - the retro art style, vulkan_style_looks.h) A position in front of the camera: its x, y on the
        // screen moved toward the low-resolution grid's nearest corner (u.retro.xy cells), then the whole position
        // scaled by a positive factor, mix(1, 1 / w, u.retro.w). A positive factor leaves the point where it is on the
        // screen and its depth (x / w, y / w, z / w unchanged: the triangle covers the same pixels, clipped the same
        // way), but the rasteriser's perspective correction divides by the new w: at 1 every attribute - the texture
        // coordinates, the colours, the fog - is interpolated linearly across the screen (affine mapping).
        add(out, {"    if (gl_Position.w > 0.0) {"});
        add(out, {"        vec2 ndc = gl_Position.xy / gl_Position.w;"});
        add(out, {"        vec2 snapped = round((ndc * 0.5 + 0.5) * u.retro.xy) / u.retro.xy * 2.0 - 1.0;"});
        add(out, {"        ndc = mix(ndc, snapped, u.retro.z);"});
        add(out, {"        gl_Position = vec4(ndc * gl_Position.w, gl_Position.zw) * mix(1.0, 1.0 / gl_Position.w, "
                  "u.retro.w);"});
        add(out, {"    }"});
    }
    if (key.motion != 0U) {
        // (Direct3D's clip space both: this frame's without its jitter, the frame before's from its own position)
        add(out, {"    v_motion_now = vec4(clip.xy - u.motion_jitter.xy * clip.w, 0.0, clip.w);"});
        add(out, {"    v_motion_before = u.previous_clip * vec4(a_previous, 1.0);"});
    }
}

void vertex_normal(std::string& out, const VertexKey& key)
{
    if ((key.fvf & kFvfNormal) == 0U || fvf_pre_transformed(key.fvf)) {
        add(out, {"    vec3 normal = vec3(0.0);"}); // (no normal: lit by ambient and emissive alone)
        return;
    }
    // Into camera space by the inverse transpose of world x view; renormalised only with D3DRS_NORMALIZENORMALS.
    add(out, {"    vec3 normal = mat3(u.normal_matrix) * a_normal;"});
    if (key.normalize_normals) {
        add(out, {"    if (dot(normal, normal) > 0.0) normal = normalize(normal);"});
    }
}

std::string source_colour(ColourSource source, const char* material)
{
    switch (source) {
    case ColourSource::kColour1:
        return "diffuse_in";
    case ColourSource::kColour2:
        return "specular_in";
    case ColourSource::kMaterial:
        break;
    }
    return std::string("u.") + material;
}

// Direct3D 9's lighting (camera space, directional lights only):
//   diffuse  = Ce + Ca * (Ga + sum La) + Cd * sum(Ld * max(N.L, 0)), alpha Cd's alpha, clamped to 0 - 1
//   specular = Cs * sum(Ls * max(N.H, 0)^P) for the lights with N.L > 0, H = normalize(L + V), clamped
// L = -normalize(the light's direction); V = the direction to the eye: -normalize(the position) with the local viewer,
// else (0, 0, -1). Uncertain: Direct3D 9's documentation writes the non-local V as (0, 0, 1); (0, 0, -1) - towards the
// camera in Direct3D's left-handed camera space - is assumed to be what it means. A specular power of 0 gives 1.
void vertex_lighting(std::string& out, const VertexKey& key)
{
    add(out, {"    vec4 diffuse_in = ", (key.fvf & kFvfDiffuse) != 0U ? "a_diffuse" : "vec4(1.0)", ";"});
    add(out, {"    vec4 specular_in = ", (key.fvf & kFvfSpecular) != 0U ? "a_specular" : "vec4(0.0)", ";"});
    if (!key.lighting || fvf_pre_transformed(key.fvf)) {
        // Unlit: the vertex's colours (a missing diffuse is opaque white, a missing specular black).
        add(out, {"    v_diffuse = diffuse_in;\n    v_specular = specular_in;"});
        if (key.surface != 0) {
            add(out, {"    v_surface_base = vec4(0.0);\n    v_surface_cd = diffuse_in;"}); // (NOT ORIGINAL - rt_pbr)
        }
        return;
    }
    add(out, {"    vec4 cd = ", source_colour(key.diffuse_source, "material_diffuse"), ";"});
    add(out, {"    vec4 ca = ", source_colour(key.ambient_source, "material_ambient"), ";"});
    add(out, {"    vec4 cs = ", source_colour(key.specular_source, "material_specular"), ";"});
    add(out, {"    vec4 ce = ", source_colour(key.emissive_source, "material_emissive"), ";"});
    add(out, {"    vec3 lit_ambient = vec3(0.0);\n    vec3 lit_diffuse = vec3(0.0);\n    vec3 lit_specular = vec3(0.0);"});
    if (key.specular) {
        add(out, {"    vec3 to_eye = ", key.local_viewer ? "-normalize(eye.xyz)" : "vec3(0.0, 0.0, -1.0)", ";"});
    }
    add(out, {"    for (int i = 0; i < ", num(static_cast<uint32_t>(key.light_count)), "; ++i) {"});
    add(out, {"        vec3 to_light = -normalize(u.light_direction[i].xyz);"});
    add(out, {"        float n_dot_l = max(dot(normal, to_light), 0.0);"});
    add(out, {"        lit_ambient += u.light_ambient[i].rgb;"});
    add(out, {"        lit_diffuse += u.light_diffuse[i].rgb * n_dot_l;"});
    if (key.specular) {
        add(out, {"        if (n_dot_l > 0.0) {"});
        add(out, {"            float n_dot_h = max(dot(normal, normalize(to_light + to_eye)), 0.0);"});
        add(out, {"            float power = u.material_power.x;"});
        add(out, {"            lit_specular += u.light_specular[i].rgb * (power > 0.0 ? pow(n_dot_h, power) : 1.0);"});
        add(out, {"        }"});
    }
    add(out, {"    }"});
    add(out, {"    v_diffuse = clamp(vec4(ce.rgb + ca.rgb * (u.ambient.rgb + lit_ambient) + cd.rgb * lit_diffuse, cd.a), "
              "0.0, 1.0);"});
    if (key.surface != 0) {
        // (NOT ORIGINAL - rt_pbr) the formula's parts that don't depend on the normal, for the fragment shader to finish
        add(out, {"    v_surface_base = vec4(ce.rgb + ca.rgb * (u.ambient.rgb + lit_ambient), 0.0);\n    v_surface_cd = cd;"});
    }
    if (key.specular) {
        add(out, {"    v_specular = clamp(vec4(cs.rgb * lit_specular, cs.a), 0.0, 1.0);"});
    } else {
        add(out, {"    v_specular = vec4(0.0);"}); // (lit without D3DRS_SPECULARENABLE: no specular colour)
    }
}

// Vertex fog's factor (1: no fog, 0: all fog):
//   linear f = (end - d) / (end - start), exp f = e^-(d density), exp2 f = e^-((d density)^2), clamped to 0 - 1
// d = the camera-space depth, or the distance from the eye with range fog. start == end gives 1 (assumed: no fog; the
// division is undefined). Pre-transformed vertices carry their factor in the specular colour's alpha, as Direct3D 9
// takes it (0 - all fog - without a specular colour: Direct3D 9's default specular colour, uncertain). v_fog.y is the
// camera-space depth (for table fog; 1 / rhw for pre-transformed vertices).
void vertex_fog(std::string& out, const VertexKey& key)
{
    add(out, {"    v_fog.y = eye.z;"});
    if (key.fog == FogMode::kNone) {
        add(out, {"    v_fog.x = 1.0;"});
        return;
    }
    if (fvf_pre_transformed(key.fvf)) {
        add(out, {"    v_fog.x = specular_in.a;"});
        return;
    }
    add(out, {"    float fog_distance = ", key.range_fog ? "length(eye.xyz)" : "eye.z", ";"});
    switch (key.fog) {
    case FogMode::kLinear:
        add(out, {"    float fog_factor = u.fog.y != u.fog.x ? (u.fog.y - fog_distance) / (u.fog.y - u.fog.x) : 1.0;"});
        break;
    case FogMode::kExp:
        add(out, {"    float fog_factor = exp(-(fog_distance * u.fog.z));"});
        break;
    case FogMode::kExp2:
        add(out, {"    float fog_factor = exp(-((fog_distance * u.fog.z) * (fog_distance * u.fog.z)));"});
        break;
    case FogMode::kNone:
        break;
    }
    add(out, {"    v_fog.x = clamp(fog_factor, 0.0, 1.0);"});
}

// A stage's coordinates: the source (a set, or the camera-space normal / position / reflection vector - their fourth
// component 1), through the texture matrix with COUNTn (the first n results kept, the rest 0), with the divisor in w
// for PROJECTED (the last kept result, or the source's last component untransformed). The reflection vector is
// R = I - 2 (N.I) N with N normalised and I the direction from the eye to the vertex: normalize(position) with the
// local viewer, else (0, 0, 1) (uncertain: whether Direct3D 9 normalises N here).
void vertex_coords(std::string& out, const VertexKey& key)
{
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        const std::string name = "v_tex" + num(stage);
        if (!has_stage(key.coord_mask, stage)) {
            add(out, {"    ", name, " = vec4(0.0, 0.0, 0.0, 1.0);"});
            continue;
        }
        const TexCoordKey& coords = core::at(key.coords, stage);
        std::string source;
        switch (coords.gen) {
        case TexGen::kCameraSpaceNormal:
            source = "vec4(normal, 1.0)";
            break;
        case TexGen::kCameraSpacePosition:
            source = "vec4(eye.xyz, 1.0)";
            break;
        case TexGen::kCameraSpaceReflection:
            source = std::string("vec4(reflect(") + (key.local_viewer ? "normalize(eye.xyz)" : "vec3(0.0, 0.0, 1.0)") +
                     ", dot(normal, normal) > 0.0 ? normalize(normal) : vec3(0.0)), 1.0)";
            break;
        case TexGen::kPassThrough:
        case TexGen::kSphereMap:
            source = expanded_set(key, coords.set);
            break;
        }
        add(out, {"    {\n        vec4 source = ", source, ";"});
        static constexpr std::array<const char*, 4> kKept = {"vec4(t.x, 0.0, 0.0, 1.0)", "vec4(t.xy, 0.0, 1.0)",
                                                             "vec4(t.xyz, 1.0)", "t"};
        static constexpr std::array<const char*, 4> kLast = {"x", "y", "z", "w"};
        if (coords.count != 0) {
            add(out, {"        vec4 t = u.texture_matrix[", num(stage), "] * source;"});
            add(out, {"        ", name, " = ", core::at(kKept, coords.count - 1), ";"});
            if (coords.projected) {
                add(out, {"        ", name, ".w = t.", core::at(kLast, coords.count - 1), ";"});
            }
        } else {
            add(out, {"        ", name, " = vec4(source.xyz, 1.0);"});
            if (coords.projected) {
                add(out, {"        ", name, ".w = source.", core::at(kLast, source_size(key, coords) - 1), ";"});
            }
        }
        add(out, {"    }"});
    }
}

// The point size: D3DRS_POINTSIZE, or with point scaling Vh * size * sqrt(1 / (A + B d + C d^2)) (d the distance from
// the eye, Vh the viewport's height), clamped to the minimum and maximum.
void vertex_point_size(std::string& out, const VertexKey& key)
{
    if (!key.points) {
        return;
    }
    add(out, {"    float point_size = u.point.x;"});
    if (key.point_scale) {
        add(out, {"    float point_distance = length(eye.xyz);"});
        add(out, {"    point_size = u.point.w * point_size * sqrt(1.0 / (u.point_scale.x + u.point_scale.y * point_distance"
                  " + u.point_scale.z * point_distance * point_distance));"});
    }
    add(out, {"    gl_PointSize = clamp(point_size, u.point.y, u.point.z);"});
}

// --- the fragment shader ------------------------------------------------------------------------------------------

std::string coord_of(const FragmentKey& key, std::size_t stage)
{
    if (key.point_sprite) {
        return "gl_PointCoord"; // (every stage's coordinates are the point's, 0 - 1 from its top left)
    }
    const std::string name = "v_tex" + num(stage);
    return has_stage(key.projected_mask, stage) ? name + ".xy / " + name + ".w" : name + ".xy";
}

// --- (NOT ORIGINAL - [Graphics] rt_pbr) the per-pixel lighting ------------------------------------------------------
// (vulkan_surface.h; docs/design/vulkan.md "Per-pixel lighting and surface maps") A key with FragmentKey::surface set
// works out the light per pixel and puts it where the program reads the vertex colour (v0), then adds a specular term:
//   - the normal: the vertex's (camera space, interpolated; a format without normals: the triangle's own, from the
//     position's screen derivatives, toward the eye), bent by the surface map's tangent-space normal (kSurfaceMap)
//     through a tangent frame made per pixel from the screen derivatives of the position and stage 0's texture
//     coordinates: the gradients of u and v along the surface, (du/dx (dP/dy x N) + du/dy (N x dP/dx)) / det with det
//     = N . (dP/dx x dP/dy) - only its sign kept, the frame scaled by its longer axis. A mirrored mapping turns the
//     frame with it; no tangents are stored (the game's meshes have none, and the frame follows the CPU's skinning);
//   - kSurfaceLit (objects): Direct3D 9's diffuse formula of vertex_lighting per pixel - the parts that don't depend on
//     the normal come from the vertex shader - with the draw's lights;
//   - without it (the world): the bake (the vertex colour) kept and scaled by how much more or less the bent normal
//     faces the bake's sun than the surface does, (a + N'.L) / (a + N.L), where the bake is sunlit (its light level
//     between shade.x and shade.y), and elsewhere by a softer one toward the sky, (1.5 + N'.up) / (1.5 + N.up), at
//     sky.w; the ratio clamped to shade.z - shade.w;
//   - specular: GGX (Smith-Schlick G with k = alpha / 2, Schlick's Fresnel with F0), pi x the BRDF x N.L - the
//     convention in which the diffuse term is the light x N.L - per light (or the sun, where sunlit), the roughness the
//     map's (B) or params.z, clamped to 0.08 - 1; added after the program, x params.x.
// The names a later part may read (the relit mode, kSurfaceRelit): surface_n (the bent normal), surface_geo (the
// surface's), surface_v (toward the eye), surface_rough, surface_v0, surface_specular.
constexpr const char* kSurfaceBlock = R"(layout(set = 0, binding = 6, std140) uniform SurfaceUniforms {
    vec4 light_toward[8];
    vec4 light_colour[8];
    vec4 sun;
    vec4 sun_colour;
    vec4 sky;
    vec4 params;
    vec4 shade;
    vec4 relit;
} s;
float surface_ggx(vec3 n, vec3 v, vec3 l, float rough, float f0)
{
    float nl = max(dot(n, l), 0.0);
    if (nl <= 0.0) return 0.0;
    vec3 h = normalize(v + l);
    float nv = max(dot(n, v), 1.0e-4);
    float nh = max(dot(n, h), 0.0);
    float vh = max(dot(v, h), 0.0);
    float a = max(rough * rough, 0.0064);
    float a2 = a * a;
    float q = nh * nh * (a2 - 1.0) + 1.0;
    float d = a2 / (3.14159265 * q * q);
    float k = 0.5 * a;
    float g = (nl / (nl * (1.0 - k) + k)) * (nv / (nv * (1.0 - k) + k));
    float f = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
    return min(3.14159265 * d * g * f / (4.0 * nv), 16.0);
})";

void surface_declarations(std::string& out, const FragmentKey& key)
{
    constexpr uint32_t kBlockBinding = 6; // (kSurfaceBlock's numbers)
    constexpr uint32_t kMapBinding = 7;
    constexpr std::size_t kBlockLights = 8;
    static_assert(kSurfaceUniformsBinding == kBlockBinding && kSurfaceMapBinding == kMapBinding && kMaxLights == kBlockLights,
                  "kSurfaceBlock names them");
    add(out, {kSurfaceVaryingsIn});
    if ((key.surface & kSurfaceMap) != 0U) {
        add(out, {"layout(set = 0, binding = 7) uniform sampler2D surface_map;"});
    }
    add(out, {kSurfaceBlock});
}

void surface_lighting(std::string& out, const FragmentKey& key)
{
    add(out, {"    vec3 surface_eye = v_surface_eye;\n    vec3 surface_dx = dFdx(surface_eye);\n"
              "    vec3 surface_dy = dFdy(surface_eye);\n    vec3 surface_face = cross(surface_dx, surface_dy);\n"
              "    surface_face = dot(surface_face, surface_face) > 0.0 ? normalize(surface_face) : vec3(0.0, 0.0, -1.0);\n"
              "    if (dot(surface_face, surface_eye) > 0.0) surface_face = -surface_face;\n"
              "    vec3 surface_geo = dot(v_surface_normal, v_surface_normal) > 1.0e-12 ? normalize(v_surface_normal) "
              ": surface_face;\n"
              "    vec3 surface_n = surface_geo;\n    vec3 surface_v = normalize(-surface_eye);\n"
              "    float surface_rough = s.params.z;"});
    if ((key.surface & kSurfaceMap) != 0U) {
        add(out, {"    {\n        vec2 uv = ", coord_of(key, 0), ";\n        vec4 m = texture(surface_map, uv);"});
        add(out, {"        surface_rough = m.b;\n        vec2 xy = (m.rg * 255.0 - 127.5) / 127.5;\n"
                  "        vec3 nt = vec3(xy * s.params.y, sqrt(max(1.0 - dot(xy, xy), 0.0)));\n"
                  "        vec2 uv_dx = dFdx(uv);\n        vec2 uv_dy = dFdy(uv);\n"
                  "        vec3 c1 = cross(surface_dy, surface_geo);\n        vec3 c2 = cross(surface_geo, surface_dx);\n"
                  "        float det = dot(surface_geo, cross(surface_dx, surface_dy));\n"
                  "        vec3 t = (c1 * uv_dx.x + c2 * uv_dy.x) * sign(det);\n"
                  "        vec3 b = (c1 * uv_dx.y + c2 * uv_dy.y) * sign(det);\n"
                  "        float scale = max(dot(t, t), dot(b, b));\n"
                  "        vec4 uv_change = abs(vec4(uv_dx, uv_dy));\n"
                  "        float uv_floor = 1.0e-6 * max(1.0, max(abs(uv.x), abs(uv.y)));\n"
                  "        if (scale > 0.0 && det != 0.0 && max(max(uv_change.x, uv_change.y), max(uv_change.z, uv_change.w)) > "
                  "uv_floor) {\n            float inv = inversesqrt(scale);\n"
                  "            surface_n = normalize(t * (inv * nt.x) + b * (inv * nt.y) + surface_geo * nt.z);\n"
                  "        }\n    }"});
    }
    add(out, {"    surface_rough = clamp(surface_rough, 0.08, 1.0);\n    vec3 surface_specular = vec3(0.0);"});
    if ((key.surface & kSurfaceLit) != 0U) {
        add(out, {"    vec3 surface_light = vec3(0.0);\n    for (int i = 0; i < 8; ++i) {\n"
                  "        vec3 l = s.light_toward[i].xyz;\n        if (dot(l, l) == 0.0) continue;\n"
                  "        surface_light += s.light_colour[i].rgb * max(dot(surface_n, l), 0.0);\n"
                  "        surface_specular += s.light_colour[i].rgb * surface_ggx(surface_n, surface_v, l, surface_rough, "
                  "s.params.w);\n    }\n"
                  "    vec4 surface_v0 = vec4(clamp(v_surface_base.rgb + v_surface_cd.rgb * surface_light, 0.0, 1.0), "
                  "v_surface_cd.a);"});
        return;
    }
    add(out, {"    vec3 surface_bake = v_diffuse.rgb;\n"
              "    float surface_level = dot(surface_bake * 2.0, vec3(0.2126, 0.7152, 0.0722));\n"
              "    float surface_sunlit = s.sun.w * smoothstep(s.shade.x, s.shade.y, surface_level);\n"
              "    float surface_a = max(s.sun_colour.w, 0.05);\n"
              "    float surface_sun_detail = (surface_a + max(dot(surface_n, s.sun.xyz), 0.0)) / "
              "(surface_a + max(dot(surface_geo, s.sun.xyz), 0.0));\n"
              "    float surface_sky_detail = (1.5 + dot(surface_n, s.sky.xyz)) / (1.5 + dot(surface_geo, s.sky.xyz));\n"
              "    float surface_detail = mix(mix(1.0, surface_sky_detail, s.sky.w), surface_sun_detail, surface_sunlit);\n"
              "    surface_detail = clamp(surface_detail, s.shade.z, s.shade.w);\n"
              "    vec4 surface_v0 = vec4(clamp(surface_bake * surface_detail, 0.0, 1.0), v_diffuse.a);\n"
              "    surface_specular = s.sun_colour.rgb * surface_ggx(surface_n, surface_v, s.sun.xyz, surface_rough, "
              "s.params.w) * surface_sunlit;"});
}

void fragment_declarations(std::string& out, const FragmentKey& key, uint32_t sampled)
{
    add(out, {"layout(set = 0, binding = ", num(kFragmentUniformsBinding), ", std140) uniform FragmentUniforms {"});
    add(out, {"    vec4 constant[8];\n    vec4 texture_factor;\n    vec4 fog_colour;\n    vec4 fog;\n"
              "    vec4 alpha_reference;\n} u;"});
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        if (has_stage(sampled, stage)) {
            add(out, {"layout(set = 0, binding = ", num(kFirstTextureBinding + stage), ") uniform sampler2D t", num(stage),
                      "_map;"});
        }
    }
    add(out, {"layout(location = 0) in vec4 v_diffuse;\nlayout(location = 1) in vec4 v_specular;\n"
              "layout(location = 2) in vec2 v_fog;"});
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        if (has_stage(sampled, stage) && !key.point_sprite) {
            add(out, {"layout(location = ", num(3 + stage), ") in vec4 v_tex", num(stage), ";"});
        }
    }
    if (key.surface != 0) {
        surface_declarations(out, key);
    }
    add(out, {"layout(location = 0) out vec4 o_colour;"});
    if (key.motion != 0U) {
        add(out, {"layout(location = ", num(kMotionNowVarying), ") in vec4 v_motion_now;"});
        add(out, {"layout(location = ", num(kMotionBeforeVarying), ") in vec4 v_motion_before;"});
        add(out, {"layout(location = 1) out vec4 o_motion;"});
    }
}

// An argument's value: its source, then 1 - it (D3DTA_COMPLEMENT), then its alpha in every channel
// (D3DTA_ALPHAREPLICATE). D3DTA_TEMP reads 0 (nothing writes the temporary register: D3DTSS_RESULTARG isn't emulated);
// D3DTA_CONSTANT is refused by supported().
std::string arg_value(uint8_t value, bool alpha)
{
    std::string source;
    switch (arg_source(value)) {
    case ArgSource::kDiffuse:
        source = "v_diffuse";
        break;
    case ArgSource::kCurrent:
        source = "current";
        break;
    case ArgSource::kTexture:
        source = "tex";
        break;
    case ArgSource::kFactor:
        source = "u.texture_factor";
        break;
    case ArgSource::kSpecular:
        source = "v_specular";
        break;
    case ArgSource::kTemp:
    case ArgSource::kConstant:
        source = "vec4(0.0)";
        break;
    }
    const bool complement = (value & kArgComplement) != 0U;
    if (alpha || (value & kArgAlphaReplicate) != 0U) {
        std::string channel = source + ".a";
        if (complement) {
            channel = "(1.0 - " + channel + ")";
        }
        return alpha ? channel : "vec3(" + channel + ")";
    }
    return complement ? "(vec3(1.0) - " + source + ".rgb)" : source + ".rgb";
}

struct OpArgs {
    std::string a0;
    std::string a1;
    std::string a2;
    std::string a1_alpha; // arg1's alpha (the colour operations that add or scale by it)
};

// D3DTEXTUREOP's formulas (arguments a0, a1, a2; the result clamped to 0 - 1 after the stage):
//   SELECTARG1 a1, SELECTARG2 a2, MODULATE a1 a2, MODULATE2X 2 a1 a2, MODULATE4X 4 a1 a2, ADD a1 + a2,
//   ADDSIGNED a1 + a2 - 0.5, ADDSIGNED2X 2 (a1 + a2 - 0.5), SUBTRACT a1 - a2, ADDSMOOTH a1 + a2 - a1 a2,
//   BLEND*ALPHA a1 x + a2 (1 - x) with x the diffuse's / this stage's texture's / the factor's / CURRENT's alpha,
//   BLENDTEXTUREALPHAPM a1 + a2 (1 - the texture's alpha), MODULATEALPHA_ADDCOLOR a1 + a1.a a2,
//   MODULATECOLOR_ADDALPHA a1 a2 + a1.a, MODULATEINVALPHA_ADDCOLOR (1 - a1.a) a2 + a1,
//   MODULATEINVCOLOR_ADDALPHA (1 - a1) a2 + a1.a, DOTPRODUCT3 4 ((a1.r - .5)(a2.r - .5) + ... b) in every channel
//   (alpha too), MULTIPLYADD a0 + a1 a2, LERP a0 a1 + (1 - a0) a2.
// The four MODULATE..._ADD... operations are colour operations; as alpha operations (undefined in Direct3D 9) they are
// taken as SELECTARG1.
std::string op_value(StageOp op, const OpArgs& args, bool alpha)
{
    const std::string& a1 = args.a1;
    const std::string& a2 = args.a2;
    switch (op) {
    case StageOp::kSelectArg1:
        return a1;
    case StageOp::kSelectArg2:
        return a2;
    case StageOp::kModulate:
        return a1 + " * " + a2;
    case StageOp::kModulate2x:
        return "2.0 * " + a1 + " * " + a2;
    case StageOp::kModulate4x:
        return "4.0 * " + a1 + " * " + a2;
    case StageOp::kAdd:
        return a1 + " + " + a2;
    case StageOp::kAddSigned:
        return a1 + " + " + a2 + " - 0.5";
    case StageOp::kAddSigned2x:
        return "2.0 * (" + a1 + " + " + a2 + " - 0.5)";
    case StageOp::kSubtract:
        return a1 + " - " + a2;
    case StageOp::kAddSmooth:
        return a1 + " + " + a2 + " - " + a1 + " * " + a2;
    case StageOp::kBlendDiffuseAlpha:
        return "mix(" + a2 + ", " + a1 + ", v_diffuse.a)";
    case StageOp::kBlendTextureAlpha:
        return "mix(" + a2 + ", " + a1 + ", tex.a)";
    case StageOp::kBlendFactorAlpha:
        return "mix(" + a2 + ", " + a1 + ", u.texture_factor.a)";
    case StageOp::kBlendTextureAlphaPm:
        return a1 + " + " + a2 + " * (1.0 - tex.a)";
    case StageOp::kBlendCurrentAlpha:
        return "mix(" + a2 + ", " + a1 + ", current.a)";
    case StageOp::kModulateAlphaAddColour:
        return alpha ? a1 : a1 + " + " + args.a1_alpha + " * " + a2;
    case StageOp::kModulateColourAddAlpha:
        return alpha ? a1 : a1 + " * " + a2 + " + " + args.a1_alpha;
    case StageOp::kModulateInvAlphaAddColour:
        return alpha ? a1 : "(1.0 - " + args.a1_alpha + ") * " + a2 + " + " + a1;
    case StageOp::kModulateInvColourAddAlpha:
        return alpha ? a1 : "(vec3(1.0) - " + a1 + ") * " + a2 + " + " + args.a1_alpha;
    case StageOp::kDotProduct3:
        return alpha ? a1 : "vec3(4.0 * dot(" + a1 + " - 0.5, " + a2 + " - 0.5))";
    case StageOp::kMultiplyAdd:
        return args.a0 + " + " + a1 + " * " + a2;
    case StageOp::kLerp:
        return "mix(" + a2 + ", " + a1 + ", " + args.a0 + ")";
    case StageOp::kNone:
    case StageOp::kDisable:
    case StageOp::kPremodulate:
    case StageOp::kBumpEnvMap:
    case StageOp::kBumpEnvMapLuminance:
        break;
    }
    return alpha ? "current.a" : "current.rgb";
}

// The texture stages: CURRENT starts as the diffuse colour; each enabled stage computes its colour and alpha from its
// arguments and writes CURRENT, clamped to 0 - 1 (Direct3D 9's stages hold 0 - 1); the first disabled stage ends them.
// A colour DOTPRODUCT3 writes the alpha too. Then the specular colour is added (D3DRS_SPECULARENABLE), clamped.
void fragment_stages(std::string& out, const FragmentKey& key, uint32_t sampled)
{
    add(out, {"    vec4 current = v_diffuse;"});
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        const StageKey& ops = core::at(key.stages, stage);
        if (ops.colour_op == StageOp::kDisable || ops.colour_op == StageOp::kNone) {
            break;
        }
        add(out, {"    {"});
        if (has_stage(sampled, stage)) {
            add(out, {"        vec4 tex = texture(t", num(stage), "_map, ", coord_of(key, stage), ");"});
        }
        const OpArgs colour = {.a0 = arg_value(ops.colour_arg0, false),
                               .a1 = arg_value(ops.colour_arg1, false),
                               .a2 = arg_value(ops.colour_arg2, false),
                               .a1_alpha = arg_value(ops.colour_arg1, true)};
        const OpArgs alpha = {.a0 = arg_value(ops.alpha_arg0, true),
                              .a1 = arg_value(ops.alpha_arg1, true),
                              .a2 = arg_value(ops.alpha_arg2, true),
                              .a1_alpha = arg_value(ops.alpha_arg1, true)};
        add(out, {"        vec3 colour_result = ", op_value(ops.colour_op, colour, false), ";"});
        if (ops.colour_op == StageOp::kDotProduct3) {
            add(out, {"        float alpha_result = colour_result.r;"});
        } else {
            add(out, {"        float alpha_result = ", op_value(ops.alpha_op, alpha, true), ";"});
        }
        add(out, {"        current = clamp(vec4(colour_result, alpha_result), 0.0, 1.0);\n    }"});
    }
    if (key.specular_add) {
        add(out, {"    current.rgb = clamp(current.rgb + v_specular.rgb, 0.0, 1.0);"});
    }
    add(out, {"    vec4 colour = current;"});
}

// --- the ps.1.1 shaders (render_pc/pixel_shaders.cpp), line by line -------------------------------------------------
// ps.1.1's registers: v0 the diffuse colour, v1 the specular, t<n> stage n's texel, c<n> the constants - which ps.1.x
// reads clamped to -1 - 1 - and r0 (the result) / r1 the temporaries. _x2 / _x4 double / quadruple an instruction's
// result, _sat clamps it to 0 - 1, a '+' line is co-issued (its alpha written beside the line before's colour). The
// result r0 is clamped to 0 - 1 when written out. Uncertain: ps.1.x hardware clamped every intermediate result to its
// range (D3DCAPS9.PixelShader1xMaxValue: 1 on the first ps.1.1 cards, at least 8 or unbounded since); the ports keep
// intermediates unclamped, as current Direct3D 9 drivers compute them in floats.

void ps_common(std::string& out, const FragmentKey& key, uint32_t textures)
{
    add(out, {"    vec4 v0 = ", key.surface != 0 ? "surface_v0" : "v_diffuse",
              ";\n    vec4 v1 = v_specular;\n    vec4 r0 = vec4(0.0);\n    vec4 r1 = vec4(0.0);"});
    if (key.unlit != 0) {
        // The unlit twin (render_pc/view_mode.cpp): each whole v0 operand is c7 = 0.5 there. The game's shaders read v0
        // whole only into colour (rgb) results and its alpha as v0.a, which the twin leaves.
        add(out, {"    v0.rgb = vec3(0.5);"});
    }
    for (uint32_t index = 0; index < kStageCount; ++index) {
        add(out, {"    vec4 c", num(index), " = clamp(u.constant[", num(index), "], -1.0, 1.0);"});
    }
    add(out, {"    vec4 c4 = clamp(u.constant[4], -1.0, 1.0);"});
    for (uint32_t index = 0; index < textures; ++index) {
        add(out, {"    vec4 t", num(index), " = texture(t", num(index), "_map, ", coord_of(key, index), ");"});
    }
}

// The fixed shaders (one pass).
void ps_fixed(std::string& out, PixelProgram program)
{
    switch (program) {
    case PixelProgram::kTextureVertexX4: // tex t0 / mul r0.rgb,t0,c0 / +mul_x2 r0.a,v0.a,t0.a / mul_x4 r0.rgb,v0,r0
        add(out, {"    r0.rgb = t0.rgb * c0.rgb;\n    r0.a = 2.0 * (v0.a * t0.a);\n    r0.rgb = 4.0 * (v0.rgb * r0.rgb);"});
        break;
    case PixelProgram::kVertexFactor: // mul_x2 r0.rgb,v0,c0 / +mov r0.a,v0.a
        add(out, {"    r0.rgb = 2.0 * (v0.rgb * c0.rgb);\n    r0.a = v0.a;"});
        break;
    case PixelProgram::kBloomCutoff:
        // def c1 1,1,1,1 / def c2 0,0,0,0 / tex t0 / sub r0,t0,c0 / dp3 r1,r0,c1 / add r0.a,r1.a,c0.a /
        // cnd r0,r0.a,t0,c2 (r0 = r0.a > 0.5 ? t0 : c2)
        add(out, {"    r0 = t0 - c0;\n    r1 = vec4(dot(r0.rgb, vec3(1.0)));\n    r0.a = r1.a + c0.a;\n"
                  "    r0 = r0.a > 0.5 ? t0 : vec4(0.0);"});
        break;
    case PixelProgram::kVertexAlphaTextureAlpha: // tex t0 / mul_x2 r0,v0.a,t0.a
        add(out, {"    r0 = vec4(2.0 * (v0.a * t0.a));"});
        break;
    case PixelProgram::kConstantAlphaTextureAlpha: // tex t0 / mul_x2 r0,c4.a,t0.a
        add(out, {"    r0 = vec4(2.0 * (c4.a * t0.a));"});
        break;
    case PixelProgram::kTextureConstantAlpha: // tex t0 / mul r0.rgb,t0,c0 / mul_x4 r0.rgb,v0,r0 / +mul_x2 r0.a,c4.a,t0.a
        add(out, {"    r0.rgb = t0.rgb * c0.rgb;\n    r0.rgb = 4.0 * (v0.rgb * r0.rgb);\n    r0.a = 2.0 * (c4.a * t0.a);"});
        break;
    case PixelProgram::kStages:
    case PixelProgram::kMaterial:
        break;
    }
}

// One further pass p of a material's generated shader (generated_pixel_shader): its texture into r1 - the pass colour
// x the texel x2, the alpha (unless the pass blends with the one before: modes 0xC, 0xD) the vertex alpha x the
// texel's x2 (saturated in mode 5), or the texel's alone with the pass's alpha flag - then blended into r0 by its mode.
// "The pass before" of modes 0xC / 0xD is r0 for pass 1 and r1 - this pass's own register, as the original names it -
// after.
void ps_material_pass(std::string& out, const FragmentKey& key, uint32_t pass)
{
    const uint32_t mode = core::at(key.material_modes, pass);
    const bool with_previous = mode == static_cast<uint32_t>(kBlendModeLerpPrevious) ||
                               mode == static_cast<uint32_t>(kBlendModeLerpFromPrevious);
    const std::string texel = "t" + num(pass);
    const std::string colour = "c" + num(pass);
    add(out, {"    r1.rgb = 2.0 * (", colour, ".rgb * ", texel, ".rgb);"}); // mul_x2 r1.rgb, c<p>, t<p>
    if (!with_previous) {
        if ((key.material_alpha_flags & (kAlphaFlagBit << pass)) == 0U) {
            if (mode == static_cast<uint32_t>(kBlendModeAlpha)) {
                add(out, {"    r1.a = clamp(2.0 * (v0.a * ", texel, ".a), 0.0, 1.0);"}); // +mul_x2_sat r1.a, v0.a, t<p>.a
            } else {
                add(out, {"    r1.a = 2.0 * (v0.a * ", texel, ".a);"}); // +mul_x2 r1.a, v0.a, t<p>.a
            }
        } else {
            add(out, {"    r1.a = ", texel, ".a;"}); // +mov r1.a, t<p>
        }
    }
    const char* previous = pass == 1 ? "r0" : "r1";
    switch (mode) {
    case 1: // mad r0.rgb,r1,r1.a,r0
        add(out, {"    r0.rgb = r1.rgb * r1.a + r0.rgb;"});
        break;
    case 2: // mad r0.rgb,r1,c<p>.a,r0
        add(out, {"    r0.rgb = r1.rgb * ", colour, ".a + r0.rgb;"});
        break;
    case 3: // mad r0.rgb,r1,-r1.a,r0
        add(out, {"    r0.rgb = r1.rgb * -r1.a + r0.rgb;"});
        break;
    case 4: // mad r0.rgb,-r1,c<p>.a,r0
        add(out, {"    r0.rgb = -r1.rgb * ", colour, ".a + r0.rgb;"});
        break;
    case kBlendModeAlpha: // lrp r0.rgb,r1.a,r1,r0
        add(out, {"    r0.rgb = mix(r0.rgb, r1.rgb, r1.a);"});
        break;
    case kBlendModeFixBlend: // lrp r0.rgb,c<p>.a,r1,r0
        add(out, {"    r0.rgb = mix(r0.rgb, r1.rgb, ", colour, ".a);"});
        break;
    case kBlendModeModulate: // mul r0.rgb,r0,r1.a
        add(out, {"    r0.rgb = r0.rgb * r1.a;"});
        break;
    case kBlendModeFixModulate: // mul r0.rgb,r0,c<p>.a
        add(out, {"    r0.rgb = r0.rgb * ", colour, ".a;"});
        break;
    case kBlendModeBrighten: // mad r0.rgb,r0,r1.a,r0
        add(out, {"    r0.rgb = r0.rgb * r1.a + r0.rgb;"});
        break;
    case kBlendModeFixBrighten: // mad r0.rgb,r0,c<p>.a,r0
        add(out, {"    r0.rgb = r0.rgb * ", colour, ".a + r0.rgb;"});
        break;
    case kBlendModeVertexTexAlpha:
        // mul v1.rgb,v1,r1.a: writes the specular input, which nothing reads after - r0 unchanged. (ps.1.1's v
        // registers are read-only: D3DX would refuse the line, so no shipped material is expected to use this mode
        // past its first pass.)
        add(out, {"    v1.rgb = v1.rgb * r1.a;"});
        break;
    case kBlendModeLerpPrevious: // lrp r0.rgb, <previous>.a, r1, r0
        add(out, {"    r0.rgb = mix(r0.rgb, r1.rgb, ", previous, ".a);"});
        break;
    case kBlendModeLerpFromPrevious: // lrp r0.rgb, <previous>.a, r0, r1
        add(out, {"    r0.rgb = mix(r1.rgb, r0.rgb, ", previous, ".a);"});
        break;
    default: // (any other mode: no blend line)
        break;
    }
}

// A material's generated shader: pass 0 into r0 (its colour x the texel x2; the alpha the vertex alpha x the texel's
// x2, or the texel's alone with its alpha flag), each further pass blended in, then r0's colour x the vertex colour x2.
// r1 starts at 0 (ps.1.1 leaves an unwritten register undefined; the original's passes always write r1 before reading
// it but for mode 0xC / 0xD on pass 2 after mode 0xC / 0xD on pass 1).
void ps_material(std::string& out, const FragmentKey& key)
{
    if ((key.material_alpha_flags & kAlphaFlagBit) == 0U) {
        add(out, {"    r0.a = 2.0 * (v0.a * t0.a);"}); // mul_x2 r0.a, v0.a, t0.a
    } else {
        add(out, {"    r0.a = t0.a;"}); // mov r0.a, t0.a
    }
    add(out, {"    r0.rgb = 2.0 * (c0.rgb * t0.rgb);"}); // +mul_x2 r0.rgb, c0, t0
    for (uint32_t pass = 1; pass < key.material_passes; ++pass) {
        ps_material_pass(out, key, pass);
    }
    add(out, {"    r0.rgb = 2.0 * (r0.rgb * v0.rgb);"}); // mul_x2 r0.rgb, r0, v0
}

void fragment_program(std::string& out, const FragmentKey& key, uint32_t sampled)
{
    if (key.program == PixelProgram::kStages) {
        fragment_stages(out, key, sampled);
        return;
    }
    uint32_t textures = sampled != 0U ? 1U : 0U;
    if (key.program == PixelProgram::kMaterial) {
        textures = key.material_passes;
    }
    if (key.surface != 0) {
        surface_lighting(out, key); // (NOT ORIGINAL - rt_pbr)
    }
    ps_common(out, key, textures);
    if (key.program == PixelProgram::kMaterial) {
        ps_material(out, key);
    } else {
        ps_fixed(out, key.program);
    }
    add(out, {"    vec4 colour = r0;"});
    if (key.surface != 0) {
        add(out, {"    colour.rgb += surface_specular * s.params.x;"});
    }
}

// Fog (after the stages or the pixel shader, as Direct3D 9 applies it): colour = f colour + (1 - f) the fog colour. Table
// fog computes f per pixel from the camera-space depth (assumed: Direct3D 9's w-based fog; with a projection it can't
// take w from, it would use the depth buffer's z), with the fog block's start, end and density.
void fragment_fog(std::string& out, const FragmentKey& key)
{
    if (!key.fog) {
        return;
    }
    switch (key.table_fog) {
    case FogMode::kNone:
        add(out, {"    float fog_factor = v_fog.x;"});
        break;
    case FogMode::kLinear:
        add(out, {"    float fog_factor = u.fog.y != u.fog.x ? (u.fog.y - v_fog.y) / (u.fog.y - u.fog.x) : 1.0;"});
        break;
    case FogMode::kExp:
        add(out, {"    float fog_factor = exp(-(v_fog.y * u.fog.z));"});
        break;
    case FogMode::kExp2:
        add(out, {"    float fog_factor = exp(-((v_fog.y * u.fog.z) * (v_fog.y * u.fog.z)));"});
        break;
    }
    add(out, {"    colour.rgb = mix(u.fog_colour.rgb, colour.rgb, clamp(fog_factor, 0.0, 1.0));"});
}

// The alpha test: the alpha as an 8-bit value (rounded) against the 0 - 255 reference, by the comparison; the pixel is
// discarded when it fails. Uncertain: how Direct3D 9 drivers quantise the alpha (rounded here; the reference is
// D3DRS_ALPHAREF's integer).
void fragment_alpha_test(std::string& out, const FragmentKey& key)
{
    if (key.alpha_test == CompareFunc::kOff || key.alpha_test == CompareFunc::kAlways) {
        return;
    }
    if (key.alpha_test == CompareFunc::kNever) {
        add(out, {"    discard;"});
        return;
    }
    static constexpr std::array<const char*, 9> kTests = {"", "", "<", "==", "<=", ">", "!=", ">=", ""};
    add(out, {"    float alpha8 = floor(colour.a * 255.0 + 0.5);"});
    add(out, {"    if (!(alpha8 ", core::at(kTests, core::to_underlying(key.alpha_test)), " u.alpha_reference.x)) discard;"});
}

} // namespace

std::string vertex_shader_glsl(const VertexKey& key)
{
    std::string out = "#version 450\n// generated: " + describe(key) + "\n";
    vertex_declarations(out, key);
    add(out, {"void main()\n{"});
    vertex_position(out, key);
    vertex_normal(out, key);
    vertex_lighting(out, key);
    if (key.surface != 0) {
        add(out, {"    v_surface_normal = normal;\n    v_surface_eye = eye.xyz;"}); // (NOT ORIGINAL - rt_pbr)
    }
    vertex_fog(out, key);
    vertex_coords(out, key);
    vertex_point_size(out, key);
    add(out, {"}"});
    return out;
}

std::string fragment_shader_glsl(const FragmentKey& key)
{
    const uint32_t sampled = sampled_stages(key);
    std::string out = "#version 450\n// generated: " + describe(key) + "\n";
    fragment_declarations(out, key, sampled);
    add(out, {"void main()\n{"});
    fragment_program(out, key, sampled);
    add(out, {"    colour = clamp(colour, 0.0, 1.0);"});
    fragment_fog(out, key);
    fragment_alpha_test(out, key);
    if (key.motion != 0U) {
        // the move to where the point was the frame before, in 0 - 1 of the viewport (Direct3D's y up: down on screen);
        // a point behind the camera then: none
        add(out, {"    vec2 now = v_motion_now.xy / v_motion_now.w;"});
        add(out, {"    vec2 before = v_motion_before.w > 0.0 ? v_motion_before.xy / v_motion_before.w : now;"});
        add(out, {"    o_motion = vec4((before - now) * vec2(0.5, -0.5), colour.a, 1.0);"}); // (z: a blended draw's share)
    }
    add(out, {"    o_colour = colour;\n}"});
    return out;
}

} // namespace render::vulkan
