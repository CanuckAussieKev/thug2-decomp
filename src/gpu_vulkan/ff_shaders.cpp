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
#include "engine/gfx/light_store.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/precip_occlusion.h"
#include "engine/gfx/rain.h"
#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_relit.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/water_shader.h"

namespace render::vulkan {

namespace {

// (round BAKEALB) The light store's tint word as a GLSL factor: a byte's worth of tint (rt_relit.h kRelitTintPerByte)
// times 255 - unpackUnorm's 0 - 1 to the tint.
const std::string& tint_per_unorm()
{
    static const std::string made = "(255.0 / " + std::to_string(rt::kRelitTintPerByte) + ")";
    return made;
}

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

// (added by cel shading - vulkan_cel.h, engine/gfx/rt_cel.h) The cel stream's words handed on (13 - 15, after the
// motion's), and the block both halves read (ff_uniforms.h CelUniforms).
// (round L - the physical light store, engine/gfx/light_store.h) v_cel_light and v_cel_residual carry light x the
// frame's pre-exposure (the frame's unit) - no longer bytes x 128; v_cel_emit the vertex's own emission likewise (its w 1:
// shown only on its texture's lamp texels - a lamp drawn in a texture)
constexpr const char* kCelVaryingsOut = "layout(location = 13) out vec4 v_cel_light;\nlayout(location = 14) out vec4 "
                                        "v_cel_tint;\nlayout(location = 15) out vec4 v_cel_residual;\n"
                                        "layout(location = 16) out vec4 v_cel_emit;";
constexpr const char* kCelVaryingsIn = "layout(location = 13) in vec4 v_cel_light;\nlayout(location = 14) in vec4 "
                                       "v_cel_tint;\nlayout(location = 15) in vec4 v_cel_residual;\n"
                                       "layout(location = 16) in vec4 v_cel_emit;";
constexpr const char* kCelBlock = R"(layout(set = 0, binding = 8, std140) uniform CelUniforms {
    vec4 bands;
    vec4 shadow;
    vec4 sun_world;
    vec4 sun_light;
    vec4 eye_to_world[3];
    vec4 ray;
    vec4 flatten;
    vec4 outline;
    vec4 limits;
} c;)";

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
    const bool store = (key.surface & (kVertexCelStream | kVertexRelitColour)) != 0U;
    if (key.retro != 0U || store) {
        add(out, {"    vec4 retro;"}); // (NOT ORIGINAL - the retro art style: VertexUniforms::retro)
    }
    if (store) {
        add(out, {"    vec4 light_store;"}); // (NOT ORIGINAL - round L: VertexUniforms::light_store)
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
    if ((key.surface & ~kVertexRelitColour) != 0) { // (the relit colour alone hands nothing on)
        add(out, {kSurfaceVaryingsOut});
    }
    // (round L) the light words are RGB9E5 (uint: engine/gfx/light_store.h), the tint and the flags D3DCOLORs
    if ((key.surface & kVertexCelStream) != 0U) { // (added by cel shading)
        add(out, {"layout(location = ", num(kCelLocation), ") in uint a_cel0;"});
        add(out, {"layout(location = ", num(kCelLocation + 1), ") in vec4 a_cel1;"});
        add(out, {"layout(location = ", num(kCelLocation + 2), ") in uint a_cel2;"});
        add(out, {"layout(location = ", num(kCelEmitLocation), ") in uint a_cel_emit;"});
        add(out, {kCelVaryingsOut});
    }
    if ((key.surface & kVertexRelitColour) != 0U) { // (NOT ORIGINAL - lighting round A: the relit light store)
        add(out, {"layout(location = ", num(kRelitColourLocation), ") in uint a_relit;"});
    }
    if ((key.surface & (kVertexCelStream | kVertexRelitColour)) != 0U) {
        add(out, {"layout(location = ", num(kCelFlagsLocation), ") in vec4 a_cel_flags;"});
        add(out, {rt::kLightStoreGlsl});
    }
    if ((key.surface & kVertexCelHull) != 0U) {
        add(out, {kCelBlock});
    }
    if ((key.surface & kVertexWaterMoving) != 0U) {
        add(out, {water_vertex_declarations_glsl()}); // (NOT ORIGINAL - rt_water_geometry, water_shader.h)
    }
    // (NOT ORIGINAL - round V: invariant, so a draw's prepass and its shaded draw - one vertex shader, two pipelines -
    // put every vertex at the same depth exactly; vulkan_surface_light.h)
    add(out, {key.points ? "out gl_PerVertex { invariant vec4 gl_Position; float gl_PointSize; };"
                         : "out gl_PerVertex { invariant vec4 gl_Position; };"});
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
    const bool water_moving = (key.surface & kVertexWaterMoving) != 0U;
    if (water_moving) {
        add(out, {water_vertex_move_glsl()}); // (NOT ORIGINAL - rt_water_geometry: the water's surface moved by its swell)
    }
    add(out, {"    vec4 clip = u.projection * eye;"});
    add(out, {"    gl_Position = vec4(clip.x + clip.w * u.pixel.x, -clip.y + clip.w * u.pixel.y, clip.z, clip.w);"});
    if (key.retro != 0U) {
        // (NOT ORIGINAL - the retro art style, vulkan_style_film.h) A position in front of the camera: its x, y on the
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
        // (rt_water_geometry: the moved water's point the frame before taken as moved the same - its own drift in a frame
        // left out -, into the vertex's own space by world x view's inverse: the normal matrix's transpose)
        add(out, {water_moving ? "    v_motion_before = u.previous_clip * vec4(a_previous + transpose(mat3(u.normal_matrix)) * "
                                 "water_move_eye, 1.0);"
                               : "    v_motion_before = u.previous_clip * vec4(a_previous, 1.0);"});
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
    if ((key.surface & kVertexRelitColour) != 0U) {
        // (NOT ORIGINAL - lighting round A) the relit light store's colour in place of the vertex colour's (its alpha 1
        // where the vertex has one; the game's alpha kept)
        // (a mesh relit hides - the bake's shadow drawn as geometry: alpha 128 - nothing drawn)
        // (round L: its light physical, x the frame's pre-exposure here - u.light_store.x -, encoded as the colour word
        // was: display-referred, 128 of 255 the game's white; not held at 2)
        add(out, {"    if (a_cel_flags.a > 0.75) {\n"
                  "        diffuse_in.rgb = display_from_linear(stored_light(a_relit) * u.light_store.x) * (128.0 / 255.0);\n"
                  "    } else if (a_cel_flags.a > 0.25) {\n"
                  "        diffuse_in = vec4(0.0);\n    }"});
    }
    add(out, {"    vec4 specular_in = ", (key.fvf & kFvfSpecular) != 0U ? "a_specular" : "vec4(0.0)", ";"});
    if (!key.lighting || fvf_pre_transformed(key.fvf)) {
        // Unlit: the vertex's colours (a missing diffuse is opaque white, a missing specular black).
        add(out, {"    v_diffuse = diffuse_in;\n    v_specular = specular_in;"});
        if ((key.surface & ~kVertexRelitColour) != 0) { // (the relit colour alone hands nothing on)
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
    // (NOT ORIGINAL - round O, render_pc/pc_lights.h OneLightSent) the lights are the light store's - linear light in the
    // global unit (u.ambient.w the flag): summed as light on the surface's colour (the material's diffuse - its albedo,
    // for the indirect light too) and encoded once, as the relit world's colour word is
    add(out, {"    bool one_light = u.ambient.w < -0.5;\n"
              "    if (one_light) {\n"
              "        v_diffuse = vec4(display_from_linear(linear_from_display(ce.rgb) + linear_from_display(cd.rgb) * "
              "(lit_ambient + lit_diffuse)), clamp(cd.a, 0.0, 1.0));\n    }"});
    if ((key.surface & ~kVertexRelitColour) != 0) { // (the relit colour alone hands nothing on)
        // (NOT ORIGINAL - rt_pbr) the formula's parts that don't depend on the normal, for the fragment shader to finish
        // (round O: with the one light, linear light and w -1 - one_light_v0)
        // (round ALBEDO: with the one light the ambient is light on the albedo alone - Cd is no albedo, surface_finish)
        add(out, {"    v_surface_base = one_light ? vec4(linear_from_display(ce.rgb) + lit_ambient, -1.0) : vec4(ce.rgb + ca.rgb * "
                  "(u.ambient.rgb + lit_ambient), 0.0);\n    v_surface_cd = cd;"});
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

// --- (NOT ORIGINAL - round V, per-surface lighting: vulkan_surface_light.h) the surface's light inputs ----------------
// A key with FragmentKey::light's kLightInputs / kLightShare reads, at its pixel, what the view's visibility passes made
// for the surface the prepass found there - only where this fragment is that surface (light_find, at the start of
// main: its depth the prepass's, within 1% of its distance - a later pass of its material drawn over it; or, round DB,
// a surface lit per pixel lying within kLayerLift of the prepass's surface along its own normal - a layer of paint over
// its asphalt, or the asphalt under the paint the prepass kept: one surface, lit once with its composite material; a
// glass pane, a glow or a particle in front of it is not, and keeps its own light): `light_in` (r: the sun's or the
// moon's visibility - its world casters', the objects' and the clouds' - or -1 where nothing was traced; g: the direct
// light's share of a colour lit already - the lighting state's; b: the indirect light's visibility, 1 - the
// occlusion; a: 1 where the inputs were made) and the lamps' light (a white diffuser's radiance; and on the surface's
// traced albedo for a colour lit already), and the decals' material layers (round DB - the deferred decal's DBuffer).
// The layer's test (LIGHT_LAYERS): the depth is affine in 1 / w (gl_FragCoord.w) over the whole view - z = a + b / w -,
// b is read off this fragment's own screen derivatives of both, the prepass surface's 1 / w then follows from its
// depth; along the pixel's ray the distance from the eye goes as w, so the two are |w_p / w - 1| x the eye distance
// apart, and that times |N.V| (N this fragment's face) is their distance along its normal.
// (Round FL: the view's light inputs are set 1 - ff_uniforms.h kLightInputsSet - one descriptor set a view.)
constexpr const char* kLightInputsGlsl = R"(layout(set = 1, binding = 0) uniform sampler2D light_visibility_map;
layout(set = 1, binding = 1) uniform sampler2D light_lamps_map;
layout(set = 1, binding = 2) uniform sampler2D light_lamps_on_map;
layout(set = 1, binding = 3) uniform sampler2D light_depth_map;
#ifndef LIGHT_LAYER_WRITE
layout(set = 1, binding = 4) uniform sampler2D light_decals_map;
layout(set = 1, binding = 7) uniform sampler2D light_layer_map;
#endif
layout(set = 1, binding = 6) uniform sampler2D light_specular_map;
bool light_at = false;    // the fragment is on the surface the inputs were made for
bool light_exact = false; // ... at its very depth (the surface the prepass kept - Ray Reconstruction's guides)
void light_find()
{
    float z = texelFetch(light_depth_map, ivec2(gl_FragCoord.xy), 0).r;
    light_exact = z < 1.0 && abs(gl_FragCoord.z - z) <= max(1.0e-6, (1.0 - z) * 0.01);
    light_at = light_exact;
#ifdef LIGHT_LAYERS
    // (the derivatives first: main's first statement, uniform control flow)
    vec2 dz = vec2(dFdx(gl_FragCoord.z), dFdy(gl_FragCoord.z));
    vec2 dq = vec2(dFdx(gl_FragCoord.w), dFdy(gl_FragCoord.w));
    vec3 face = cross(dFdx(v_surface_eye), dFdy(v_surface_eye));
    bool across = abs(dq.x) >= abs(dq.y);
    float q_step = across ? dq.x : dq.y;
    float z_step = across ? dz.x : dz.y;
    float eye_distance = length(v_surface_eye);
    if (!light_at && z < 1.0 && q_step != 0.0 && z_step != 0.0 && dot(face, face) > 0.0 && eye_distance > 0.0) {
        float q = gl_FragCoord.w + (z - gl_FragCoord.z) * (q_step / z_step); // (the prepass surface's 1 / w)
        if (q > 0.0) {
            float apart = eye_distance * abs(gl_FragCoord.w / q - 1.0);
            light_at = apart * abs(dot(normalize(face), v_surface_eye / eye_distance)) <= LIGHT_LAYER_LIFT;
        }
    }
#endif
}
bool light_here() { return light_at; }
vec4 light_in_now()
{
    return light_here() ? texelFetch(light_visibility_map, ivec2(gl_FragCoord.xy), 0) : vec4(-1.0, 0.0, 1.0, 0.0);
}
vec3 light_lamps_now() { return light_here() ? texelFetch(light_lamps_map, ivec2(gl_FragCoord.xy), 0).rgb : vec3(0.0); }
// (round DB) the decals' material layers over the surface (the deferred decal's DBuffer - vulkan_surface_light.h): the
// albedo's (albedo x a + rgb) and the roughness's and F0's (roughness x a + r, F0 x a + g); none: (0, 0, 0, 1)
#ifdef LIGHT_LAYER_WRITE
// (round DECAL: a layer being laid writes these inputs - it reads none of them)
vec4 light_decals_now() { return vec4(0.0, 0.0, 0.0, 1.0); }
vec4 light_layer_now() { return vec4(0.0, 0.0, 0.0, 1.0); }
#else
vec4 light_decals_now() { return light_here() ? texelFetch(light_decals_map, ivec2(gl_FragCoord.xy), 0) : vec4(0.0, 0.0, 0.0, 1.0); }
vec4 light_layer_now() { return light_here() ? texelFetch(light_layer_map, ivec2(gl_FragCoord.xy), 0) : vec4(0.0, 0.0, 0.0, 1.0); }
#endif
vec3 light_lamps_on_now()
{
    return light_here() ? texelFetch(light_lamps_on_map, ivec2(gl_FragCoord.xy), 0).rgb : vec3(0.0);
}
// (path tracing, stage 5 - ReSTIR GI) the environment's specular light over the specular albedo, where it was traced (its
// indirect visibility then 0: the store's environment - diffuse and specular - has no weight there); 0 elsewhere
vec3 light_specular_now() { return light_here() ? texelFetch(light_specular_map, ivec2(gl_FragCoord.xy), 0).rgb : vec3(0.0); }
)";
// (round V - Ray Reconstruction's guides, kLightGuides) The surface's material where it is the one the inputs were made
// for, written once the fragment is kept (after its alpha test): its albedo and specular albedo (display-encoded 8 bits,
// the roughness and a mark in their alphas) and its normal in the camera's space (halves; without one - a colour lit
// already - the upscaler's own from the depth). The surface's lighting sets guide_*; a fragment that set none writes none.
constexpr const char* kLightGuidesGlsl = R"(layout(set = 1, binding = 5, rgba32ui) uniform writeonly uimage2D light_guides_image;
vec3 guide_albedo = vec3(-1.0);
vec3 guide_specular = vec3(0.0);
vec3 guide_normal = vec3(0.0);
float guide_rough = 1.0;
float guide_has_normal = 0.0;
void light_write_guides()
{
    if (guide_albedo.x < 0.0 || !light_exact) {
        return;
    }
    vec3 a = display_from_linear(clamp(guide_albedo, 0.0, 1.0));
    vec3 s = display_from_linear(clamp(guide_specular, 0.0, 1.0));
    imageStore(light_guides_image, ivec2(gl_FragCoord.xy),
               uvec4(packUnorm4x8(vec4(a, clamp(guide_rough, 0.0, 1.0))), packUnorm4x8(vec4(s, 1.0)),
                     packHalf2x16(guide_normal.xy), packHalf2x16(vec2(guide_normal.z, guide_has_normal))));
})";

// (NOT ORIGINAL - round FL, the front layer: vulkan_front_layer.h; UE5 Lumen's Front Layer Translucency) A key with
// kLightFront - glass, a blended overlay, rt_water's water - reads the front layer's light where it is the nearest
// see-through surface (light_front_find: its depth the front layer's, within 1 % of its distance, and not the surface the
// inputs were made for - that one's own light wins): the light list's light there - all but the disc's, a white
// diffuser's radiance (light_front_now().rgb) - and the disc's on a white diffuser and its visibility
// (light_front_sun_now()). Water finds its match along the view itself (its waves move it off the layer's rest depth).
constexpr const char* kLightFrontGlsl = R"(layout(set = 1, binding = 8) uniform sampler2D light_front_depth_map;
layout(set = 1, binding = 9) uniform sampler2D light_front_map;
layout(set = 1, binding = 10) uniform sampler2D light_front_sun_map;
layout(set = 1, binding = 11) uniform sampler2D light_front_reflect_map;
bool light_front_at = false;
float light_front_depth() { return texelFetch(light_front_depth_map, ivec2(gl_FragCoord.xy), 0).r; }
void light_front_find(bool taken)
{
    float z = light_front_depth();
    light_front_at = !taken && z < 1.0 && abs(gl_FragCoord.z - z) <= max(1.0e-6, (1.0 - z) * 0.01);
}
vec4 light_front_now() { return light_front_at ? texelFetch(light_front_map, ivec2(gl_FragCoord.xy), 0) : vec4(0.0); }
vec4 light_front_sun_now() { return light_front_at ? texelFetch(light_front_sun_map, ivec2(gl_FragCoord.xy), 0) : vec4(0.0); }
// (round LW) what the layer's rest surface mirrors at `pixel` (the water's - its shading moves it by the waves' tilt)
vec4 light_front_reflect_at(ivec2 pixel) { return light_front_at ? texelFetch(light_front_reflect_map, pixel, 0) : vec4(0.0); }
)";

// (the inputs' use - a key lit per pixel has them whether it reads the inputs or not)
constexpr const char* kLightHelpersGlsl = R"(// the sun's visibility at the pixel, else `own` (the vertex's - the light store's)
float light_seen(vec4 l, float own) { return l.r >= 0.0 ? l.r : own; }
// a colour lit already (its light not separated): the direct light's share x its visibility, the rest x the indirect
// light's
float light_share(vec4 l) { return l.g * light_seen(l, 1.0) + (1.0 - l.g) * l.b; })";

// --- (NOT ORIGINAL - [Graphics] rt_pbr) the per-pixel lighting ------------------------------------------------------
// (vulkan_surface.h; docs/design/vulkan.md "Per-pixel lighting and surface maps", "One light model") A key with
// FragmentKey::surface set works out the light per pixel with round M's light model (engine/gfx/rt_brdf.h, its GLSL
// twin pasted in: brdf_direct, brdf_ambient, brdf_hair_direct):
//   - the normal: the vertex's (camera space, interpolated; a format without normals: the triangle's own, from the
//     position's screen derivatives, toward the eye), bent by the surface map's tangent-space normal (kSurfaceMap) as
//     the map holds it, through a tangent frame made per pixel from the screen derivatives of the position and stage
//     0's texture coordinates: the gradients of u and v along the surface, (du/dx (dP/dy x N) + du/dy (N x dP/dx)) / det
//     with det = N . (dP/dx x dP/dy) - only its sign kept, the frame scaled by its longer axis. A mirrored mapping
//     turns the frame with it; no tangents are stored (the game's meshes have none, the frame follows the CPU's
//     skinning);
//   - the material: the albedo is what the draw's program makes of its textures under white light (v0 = 0.5 - the
//     game's light 1), decoded to linear; the roughness the map's (B) or params.z; F0 params.w, or the albedo for a
//     metal (params.x its metalness - the material table's kind, rt_material_table.h); skin's mean free path
//     (scatter.rgb) with the surface's radius of curvature from the derivatives of the position and the normal; cloth's
//     sheen (sheen);
//   - the light (surface_finish, after the program):
//       kSurfaceLit (objects, characters): each of the draw's lights, its colour decoded to linear light, through
//         brdf_direct (hair: brdf_hair_direct along the strands); the parts of Direct3D 9's formula that don't depend
//         on the normal (Ce + Ca (Ga + sum La)) as the ambient light;
//       kSurfaceRelit (the relit world - its light store's words, rt_cel.h): the sun (sun_light per N.L toward sun.xyz,
//         x the vertex's visibility - the shadows pass replaces it per pixel - x the relief's own shadow, only above the
//         surface's own plane) through brdf_direct with the bent normal; the sky's, the bounce's and the lamps' light as
//         an environment through brdf_ambient, its irradiance on the bent normal that of an even hemisphere round the
//         surface's own normal, (1 + N'.N) / 2;
//       the world without the store (the bake): the vertex colour decoded as the environment's light, the same way;
//     the environment's specular is the light store's estimate, faded out where the reflections pass traces it (a
//     texel smoother than params.y: vulkan_rt_reflections.h kReflectionRoughest - the pass's reflection fades in there);
//   - the result is linear light, not clamped (in a linear frame; outside one it is encoded for the display and
//     clamped) - the fog and the blend follow.
// The names a later part may read: surface_n (the bent normal), surface_geo (the surface's), surface_v (toward the
// eye), surface_rough, surface_v0.
constexpr const char* kSurfaceBlock = R"(layout(set = 0, binding = 6, std140) uniform SurfaceUniforms {
    vec4 light_toward[8];
    vec4 light_colour[8];
    vec4 sun;
    vec4 sun_light;
    vec4 scatter;
    vec4 params;
    vec4 sheen;
    vec4 spare;
} s;
// (NOT ORIGINAL - round O) the lit paths' colour: with the one light (v_surface_base.w -1) linear light summed on the
// surface's colour and encoded once; else Direct3D's sum, held to 0 - 1
vec4 one_light_v0(vec4 base, vec4 cd, vec3 light)
{
    if (base.w < -0.5) {
        return vec4(display_from_linear(base.rgb + linear_from_display(cd.rgb) * light), clamp(cd.a, 0.0, 1.0));
    }
    return vec4(clamp(base.rgb + cd.rgb * light, 0.0, 1.0), cd.a);
})";

// --- (added by cel shading - [Graphics] rt_cel) the light in bands -----------------------------------------------------
// (vulkan_cel.h; engine/gfx/rt_cel.h has the formulas and their twins on the processor) A key with kSurfaceCel puts
// its light in bands where surface_lighting builds surface_v0:
//   - the world, with the cel stream (kSurfaceCelStream: the relit decomposition - ambient a, N.L, tint t, the sun's
//     share, residual r): q = the sun's light N.L x the visibility in bands (c.bands.x steps, crisp - the edge one
//     pixel wide from the derivatives -, the visibility a ray query toward the sun with kSurfaceCelShadow, else the
//     vertex's share), then t x (a in whole stops x mix(shadow colour, 1, q) + the sun's light x q) + r in steps;
//     a pixel whose stream is missing (an animated vertex) is banded as the bake;
//   - the world without it: the bake's luminance in steps of c.bands.w stops, its hue kept, the shadow colour over
//     the dark steps;
//   - lit draws (kSurfaceLit): each light's N.L in bands (the key light's - the first - times the visibility), the
//     shadow colour on the ambient where the key light is off, a rim band where 1 - N.V passes c.sun_light.w;
//   - the hull (kSurfaceCelHull): no lighting - the draw's own colour turned into ink after the program.
// Every derivative is taken in uniform control flow (the bands' widths before any branch on a pixel's values).
constexpr const char* kCelFunctions = R"(float cel_luma(vec3 v) { return dot(v, vec3(0.2126, 0.7152, 0.0722)); }
float cel_round(float t, float w) { float i = floor(t); return i + smoothstep(0.5 - w, 0.5 + w, t - i); }
float cel_width(float t) { return clamp(fwidth(t) * c.bands.z, 0.001, 0.5); }
vec3 cel_ink(vec3 colour)
{
    float y = cel_luma(colour);
    return clamp((vec3(y) + (colour - vec3(y)) * c.outline.z) * c.outline.y, 0.0, 1.0);
})";

constexpr const char* kCelVisibility = R"(layout(set = 0, binding = 9) uniform accelerationStructureEXT cel_scene;
float cel_visible(vec3 eye, vec3 eye_normal, uint mask)
{
    vec4 p = vec4(eye, 1.0);
    vec3 world = vec3(dot(c.eye_to_world[0], p), dot(c.eye_to_world[1], p), dot(c.eye_to_world[2], p));
    vec3 n = vec3(dot(c.eye_to_world[0].xyz, eye_normal), dot(c.eye_to_world[1].xyz, eye_normal),
                  dot(c.eye_to_world[2].xyz, eye_normal));
    vec3 to = c.sun_world.xyz;
    if (dot(n, to) < 0.0) n = -n;
    vec3 origin = world + n * c.ray.x + to * c.ray.y;
    rayQueryEXT q;
    rayQueryInitializeEXT(q, cel_scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.0, to,
                          c.ray.z);
    while (rayQueryProceedEXT(q)) {
    }
    return rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
})";

void cel_declarations(std::string& out, const FragmentKey& key)
{
    add(out, {kCelBlock});
    if ((key.surface & kSurfaceCelStream) != 0U) {
        add(out, {kCelVaryingsIn});
    }
    add(out, {kCelFunctions});
    if ((key.surface & kSurfaceCelShadow) != 0U) {
        add(out, {kCelVisibility});
    }
}

void cel_lighting(std::string& out, const FragmentKey& key)
{
    const bool shadow = (key.surface & kSurfaceCelShadow) != 0U;
    if ((key.surface & kSurfaceLit) != 0U) {
        add(out, {"    float cel_vis = 1.0;"});
        if (shadow) {
            add(out, {"    if (c.sun_world.w > 0.5 && dot(surface_geo, s.light_toward[0].xyz) > -0.2) cel_vis = "
                      "cel_visible(surface_eye, surface_geo, 0x01u);"});
        }
        add(out, {"    vec3 cel_light = vec3(0.0);\n    vec3 cel_key = vec3(0.0);\n    float cel_kq = 1.0;\n"
                  "    bool cel_first = true;\n"
                  "    for (int i = 0; i < 8; ++i) {\n"
                  "        vec3 l = s.light_toward[i].xyz;\n        if (dot(l, l) == 0.0) continue;\n"
                  "        float t = clamp(dot(surface_n, l), 0.0, 1.0) * c.bands.x + c.bands.y;\n"
                  "        float q = clamp(cel_round(t, cel_width(t)) / c.bands.x, 0.0, 1.0);\n"
                  "        if (cel_first) {\n            q *= cel_vis;\n            cel_kq = q;\n"
                  "            cel_key = s.light_colour[i].rgb;\n            cel_first = false;\n        }\n"
                  "        cel_light += s.light_colour[i].rgb * q;\n    }\n"
                  "    float cel_rt = 1.0 - max(dot(surface_n, surface_v), 0.0);\n"
                  "    float cel_rw = clamp(fwidth(cel_rt) * c.bands.z, 0.005, 0.25);\n"
                  "    float cel_rim = c.shadow.w * smoothstep(c.sun_light.w - cel_rw, c.sun_light.w + cel_rw, cel_rt);\n"
                  "    vec3 cel_rim_colour = (cel_key + vec3(0.3)) * (0.35 + 0.65 * cel_kq) * cel_rim;\n"
                  "    vec4 surface_v0 = one_light_v0(vec4(v_surface_base.rgb * mix(c.shadow.rgb, vec3(1.0), cel_kq), "
                  "v_surface_base.w), v_surface_cd, cel_light);\n"
                  "    surface_v0.rgb = clamp(surface_v0.rgb + cel_rim_colour, 0.0, max(1.0, max(surface_v0.r, "
                  "max(surface_v0.g, surface_v0.b))));"});
        return;
    }
    // the bake banded (also the stream's fallback)
    add(out, {"    vec3 cel_bake = v_diffuse.rgb * 2.0;\n    float cel_by = cel_luma(cel_bake);\n"
              "    float cel_bt = log2(max(cel_by, 1.0e-4)) / c.bands.w;\n"
              "    float cel_bq = exp2(cel_round(cel_bt, cel_width(cel_bt)) * c.bands.w);\n"
              "    vec3 cel_out = cel_by > 0.001 ? cel_bake * (cel_bq / cel_by) * mix(vec3(1.0), c.shadow.rgb, 1.0 - "
              "smoothstep(c.outline.w, c.limits.x, cel_bq)) : cel_bake;"});
    if ((key.surface & kSurfaceCelStream) != 0U) {
        add(out, {"    vec3 cel_amb = v_cel_light.rgb;\n    float cel_ndl = v_cel_light.a;\n"
                  "    vec3 cel_tint = v_cel_tint.rgb;\n    vec3 cel_res = v_cel_residual.rgb + v_cel_emit.rgb;\n"
                  "    float cel_vis = v_cel_tint.a;"});
        if (shadow) {
            add(out, {"    cel_vis = 1.0;\n"
                      "    if (c.sun_world.w > 0.5 && cel_ndl * c.bands.x + c.bands.y > 0.3) cel_vis = cel_visible(surface_eye, "
                      "surface_face, 0x01u);"});
        }
        add(out, {"    float cel_st = clamp(cel_ndl * cel_vis, 0.0, 1.0) * c.bands.x + c.bands.y;\n"
                  "    float cel_q = clamp(cel_round(cel_st, cel_width(cel_st)) / c.bands.x, 0.0, 1.0);\n"
                  "    float cel_ay = cel_luma(cel_amb);\n"
                  "    float cel_at = log2(max(cel_ay, 1.0e-4)) / c.ray.w;\n"
                  "    float cel_aq = cel_ay > 0.001 ? exp2(cel_round(cel_at, cel_width(cel_at)) * c.ray.w) / cel_ay : 1.0;\n"
                  "    float cel_ry = cel_luma(cel_res);\n    float cel_rt = cel_ry / c.flatten.z;\n"
                  "    float cel_rq = cel_ry > 0.001 ? cel_round(cel_rt, cel_width(cel_rt)) * c.flatten.z / cel_ry : 0.0;\n"
                  "    vec3 cel_shade = mix(c.shadow.rgb, vec3(1.0), cel_q);\n"
                  "    vec3 cel_relit = cel_tint * (cel_amb * cel_aq * cel_shade + c.sun_light.rgb * cel_q) + cel_res * "
                  "cel_rq;\n"
                  "    if (v_cel_residual.a > 0.5) cel_out = cel_relit;"});
    }
    add(out, {"    vec4 surface_v0 = vec4(clamp(cel_out * 0.5, 0.0, 1.0), v_diffuse.a);"});
}

// --- (NOT ORIGINAL - [Graphics] rt_detail) the detail layer ---------------------------------------------------------------
// (vulkan_detail.h; docs/design/vulkan.md "Detail materials") A key with FragmentKey::detail set (always with
// kSurfaceMap):
//   - kDetailMaterial: the surface kind's tiling detail texture (R, G its high-passed normal's x along +u and y along +v,
//     B its roughness swing about 0.5, A the albedo's luminance ratio x 0.5) read at stage 0's coordinates x scale.x;
//     its weight w = strength x (1 - smoothstep(scale.z, scale.w, its level of detail)) - log2 of its texels a pixel, so
//     it fades as it shrinks on the screen and the original texture is left alone far away. The normal: the map's
//     tangent-space normal with the detail's slope added (x the map's z) and renormalised; the roughness: the map's moved
//     toward the kind's (material.x by material.w) plus the detail's swing x material.y x w; stage 0's colour x
//     mix(1, A x 2, w) - its luminance only, so the colour stays the texture's; the specular tinted by the texel's colour
//     by the kind's metalness (material.z).
// (The block's `delight` vector is unused since rt_delight was retired - the de-lit texture sets take the painted light
// out offline; kept so the block's layout stays.)
constexpr const char* kDetailBlock = R"(layout(set = 0, binding = 15, std140) uniform DetailUniforms {
    vec4 scale;
    vec4 material;
    vec4 delight;
    vec4 spare;
} d;)";
// --- (added by surface relief - [Graphics] rt_relief, vulkan_surface.h) parallax occlusion mapping ------------------------
// (docs/design/vulkan.md "Surface relief") A key with kDetailRelief (with kSurfaceMap) moves stage 0's coordinates - and
// the surface map's and the detail's - to where the view ray meets the map's height field (A: 0 the deepest, 1 the top):
//   - the frame: the gradients of u and v along the surface, gu = (du/dx (dP/dy x N) + du/dy (N x dP/dx)) / det (as the
//     per-pixel lighting's, the length kept); the depth in the eye's units D = relief.x x w - the material's relief
//     (rt_material_table.h, millimetres) in world units; the ray r = the eye position normalised, rn = -r.N;
//     p = (gu.r, gv.r) D / rn: the coordinates' change from the height's top to its bottom along the ray;
//   - the fade w = (1 - smoothstep(relief.y, relief.z, the map's level of detail)) x smoothstep(0.05, 0.2, rn): D goes
//     to 0 with it, so the relief flattens smoothly far away and at grazing angles (no step, no swimming);
//   - the ray starts where the height is 1 at uv - p / 2 (the surface's 0.5 stays where the polygon is: the texture
//     doesn't slide on average), steps down in n = int(mix(32, 8, rn)) layers until the height is at or above the ray's,
//     then 5 halvings between the last layer above and the first below; the coordinates are the one below's. Every
//     read is textureGrad with the coordinates' own derivatives (a step in the result is no step in the mip level);
//   - the sun (the world's light store, kSurfaceLit off): from the hit toward the sun in 8 steps to the top,
//     q = (gu.L, gv.L) D / max(L.N, 0.1) a unit of height; where any read stands above the ray the sun is hidden there
//     (relief_shadow 0: a visibility - the sun's disc, 0.27 degrees across, is far sharper than the steps).
// No depth is written (the polygon's own: the relief has no silhouette and casts no shadow on other things).
constexpr const char* kDetailReliefBlock = R"(layout(set = 0, binding = 15, std140) uniform DetailUniforms {
    vec4 scale;
    vec4 material;
    vec4 delight;
    vec4 relief;
} d;
float relief_height(vec2 uv, vec2 dx, vec2 dy)
{
    return textureGrad(surface_map, uv, dx, dy).a;
})";
// --- (added for the characters - [Graphics] rt_characters, vulkan_surface.h "Characters") skin, cloth and hair ---------
// (docs/design/vulkan.md "Characters") A key with kDetailSkin / kDetailCloth / kDetailHair (with kSurfaceMap and
// kSurfaceLit: a character model's surface map, lit by the draw's lights) shades the material as what it is - round M's
// light model with the material table's values in SurfaceUniforms (rt_brdf.h): skin with its diffusion's wrap
// (scatter), cloth with its sheen (sheen), hair with Marschner's lobes along the strands (scatter.w: 1 along stage 0's
// u, else v). The block's `character` vector is unused (kept: the block's layout).
constexpr const char* kDetailCharacterBlock = R"(layout(set = 0, binding = 15, std140) uniform DetailUniforms {
    vec4 scale;
    vec4 material;
    vec4 delight;
    vec4 relief;
    vec4 character;
} d;)";

void detail_declarations(std::string& out, const FragmentKey& key)
{
    static_assert(kDetailUniformsBinding == 15 && kDetailMapBinding == 16, "kDetailBlock names them");
    if ((key.detail & kDetailCharacter) != 0U) { // (added for the characters: their block alone - ff_key.h)
        add(out, {kDetailCharacterBlock});
        return;
    }
    add(out, {(key.detail & kDetailRelief) != 0U ? kDetailReliefBlock : kDetailBlock});
    if ((key.detail & kDetailMaterial) != 0U) {
        add(out, {"layout(set = 0, binding = 16) uniform sampler2D detail_map;"});
    }
}

// --- (NOT ORIGINAL - [Graphics] snow) the snow's cover ------------------------------------------------------------------
// (vulkan_snow.h; docs/design/vulkan.md "Snow") A key with FragmentKey::snow works out snow_amount at the start (its
// derivatives in uniform control flow) and blends the snow's colour into the surface's before its light:
//   - the surface's world position from the eye-space one (v_surface_eye); its own normal from the position's screen
//     derivatives, turned toward the eye; its up share n.y, moved by a value noise over the ground (snow.albedo.w) so
//     the edge where a slope loses its snow isn't a straight line;
//   - how much it holds by its slope (snow::slope_cover's twin, the up share moved by the noise): full on flat ground,
//     thinning up ramps and banks to a light coat on steep faces (a quarter pipe's), none on a near-vertical wall; a
//     light coat lies in patches (a noise over the surface: where it is thin, only its higher patches hold snow);
//   - reached by the falling snow (snow_open): (round W) with kTrailSnowTraced the precipitation occlusion map's
//     precip_reached (the rain's and the snow's one map - engine/gfx/precip_occlusion.h: the lines of fall slanted by the
//     wind, traced through the level and the cut-out casters) from just off the surface (its start moved a little by a
//     smooth noise over the ground, so an eave's line isn't ruled); else the sky map's four cells round the point
//     (textureGather of the tops' heights) - each whose top is at most snow.range.z over the point, or which has no face
//     (0), counts; a quarter each, so a roof's edge is soft;
//   - snow_amount = the slope's cover (in patches) x the open share x snow.range.w;
//   - the programs' albedo: stage 0's texel (the fixed programs), or the material's passes' sum (kMaterial) before the
//     vertex colour multiplies it - mixed toward snow.albedo.rgb by snow_amount.
constexpr const char* kSnowDeclarations = R"(layout(set = 0, binding = 17, std140) uniform SnowUniforms {
    mat4 eye_to_world;
    vec4 map;
    vec4 range;
    vec4 size;
    vec4 albedo;
    vec4 slope;
    vec4 precip_fall;
    vec4 precip_slant;
    vec4 precip_near;
    vec4 precip_far;
} snow;
layout(set = 0, binding = 18) uniform sampler2D snow_sky;
)";
constexpr const char* kSnowBlock = R"(
float snow_hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float snow_noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(snow_hash(i), snow_hash(i + vec2(1.0, 0.0)), f.x), mix(snow_hash(i + vec2(0.0, 1.0)),
               snow_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float snow_slope(float up)
{
    return smoothstep(snow.size.z, snow.size.w, up) * (snow.slope.z + (1.0 - snow.slope.z) * smoothstep(snow.slope.x, snow.slope.y, up));
}
float snow_sky_open(vec3 world)
{
    vec2 uv = (world.xz - snow.map.xy) * snow.map.z / max(snow.size.xy, vec2(1.0));
    vec4 tops = textureGather(snow_sky, uv, 0);
    float open = 0.0;
    for (int i = 0; i < 4; ++i) {
        float t = tops[i] * 65535.0;
        float top = snow.range.x + max(t - 1.0, 0.0) / 65534.0 * snow.range.y;
        open += (t < 0.5 || world.y + snow.range.z >= top) ? 0.25 : 0.0;
    }
    return open;
}
#ifdef SNOW_TRACED
float snow_open(vec3 world, vec3 n)
{
    vec2 shift = (vec2(snow_noise(world.xz * 0.09), snow_noise(world.zx * 0.09 + 17.0)) - 0.5) * 3.0;
    return precip_reached(world + n * 0.6 + vec3(shift.x, 0.3, shift.y));
}
#else
float snow_open(vec3 world, vec3 n) { return snow_sky_open(world); }
#endif
float snow_cover(vec3 world, vec3 n)
{
    float edge = (snow_noise(world.xz * 0.027) - 0.5) * snow.albedo.w;
    float coat = snow_slope(n.y + edge);
    if (coat <= 0.0) {
        return 0.0;
    }
    float spot = snow_noise(world.xz * 0.31 + vec2(world.y * 0.37, -world.y * 0.29));
    float patchy = smoothstep(1.0 - coat - 0.18, 1.0 - coat + 0.18, spot) * min(coat * 2.5, 1.0);
    float amount = mix(patchy, coat, smoothstep(0.8, 1.0, coat));
    return amount * snow_open(world, n) * snow.range.w;
})";
// (NOT ORIGINAL - [Graphics] paint_trails, vulkan_paint.h; docs/design/vulkan.md "Paint trails") the paint's coat: a key
// with kTrailPaint reads the paint's three fields (engine/gfx/paint_trails.h: the level seen along x, y and z) where the
// surface faces their axis (its normal's share at least paint.first_z.z) - each field's window round the camera side by
// side in the coat and place textures (nearest; the window toroidal: a texel at its place modulo the side). The four
// texels round the point blended by hand, each counted only where its painted place is this surface's (within the slack
// - paint.first_z.w plus the texel's rise along this surface's slope): the ground under a rail keeps its own colour, a
// wall's other side none. The coat is premultiplied (blended texels stay the colour); the field with the most coat wins.
constexpr const char* kPaintBlock = R"(layout(set = 0, binding = 19, std140) uniform PaintUniforms {
    mat4 eye_to_world;
    vec4 window;
    vec4 first_xy;
    vec4 first_z;
} paint;
layout(set = 0, binding = 20) uniform sampler2D paint_coats;
layout(set = 0, binding = 21) uniform sampler2D paint_places;
vec4 paint_field(int axis, vec2 uv, float depth, float slack, vec2 first)
{
    float side = paint.window.y;
    vec2 t = uv * paint.window.z - 0.5;
    vec2 base = floor(t);
    vec2 f = t - base;
    if (any(lessThan(base, first)) || any(greaterThanEqual(base + 1.0, first + side))) return vec4(0.0);
    int column = axis * int(side);
    ivec2 a = ivec2(mod(base, side));
    ivec2 b = ivec2(mod(base + 1.0, side));
    ivec2 c00 = ivec2(column + a.x, a.y);
    ivec2 c10 = ivec2(column + b.x, a.y);
    ivec2 c01 = ivec2(column + a.x, b.y);
    ivec2 c11 = ivec2(column + b.x, b.y);
    vec4 w = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    w *= step(abs(vec4(texelFetch(paint_places, c00, 0).r, texelFetch(paint_places, c10, 0).r,
                       texelFetch(paint_places, c01, 0).r, texelFetch(paint_places, c11, 0).r) - depth), vec4(slack));
    return texelFetch(paint_coats, c00, 0) * w.x + texelFetch(paint_coats, c10, 0) * w.y +
           texelFetch(paint_coats, c01, 0) * w.z + texelFetch(paint_coats, c11, 0) * w.w;
}
float paint_slack(float share)
{
    return paint.first_z.w + paint.window.x * 1.5 * sqrt(max(1.0 - share * share, 0.0)) / max(share, 0.2);
}
vec4 paint_at(vec3 world, vec3 n)
{
    vec3 a = abs(n);
    vec4 best = vec4(0.0);
    if (a.y >= paint.first_z.z) {
        best = paint_field(1, world.xz, world.y, paint_slack(a.y), paint.first_xy.zw);
    }
    if (a.x >= paint.first_z.z) {
        vec4 c = paint_field(0, world.zy, world.x, paint_slack(a.x), paint.first_xy.xy);
        best = c.a > best.a ? c : best;
    }
    if (a.z >= paint.first_z.z) {
        vec4 c = paint_field(2, world.xy, world.z, paint_slack(a.z), paint.first_z.xy);
        best = c.a > best.a ? c : best;
    }
    best.rgb = best.a > 1.0e-4 ? best.rgb / best.a : vec3(0.0);
    best.a = clamp(best.a, 0.0, 1.0) * paint.window.w;
    return best;
})";

// (NOT ORIGINAL - [Graphics] rain, vulkan_rain.h; docs/design/vulkan.md "Rain") the wet ground: a key with kTrailWet works
// out at the start (trail_main) whether the rain reaches the surface (the precipitation occlusion map - precip_reached:
// precip_occlusion.h's twin), how hard it strikes it (its face against the fall over level ground's) and the water there
// (rain_wetness - rain.h's twin: the pores filled, a film, a puddle where the ground's small relief dips under the stored
// water's level); the albedo then darkens under the water (trail_albedo: Lekner and Dorf - rain_wet_albedo - on the linear
// albedo, by the pores' share x the surface's porosity, the film's, the puddle's), the per-pixel lighting's roughness goes
// toward still water's under a film or a puddle and a puddle's normal is the water's, with the drops' rings on it.
// WetUniforms: the view's eye-to-world, the map's fall / slant / windows (precip_*), the ground's water (rain.h Ground) and
// the rings (rain.h Ripples, the rain's clock).
std::string wet_block()
{
    static_assert(kWetUniformsBinding == 22 && kWetMapBinding == 23, "the wet block names them");
    std::string block = "layout(set = 0, binding = 22, std140) uniform WetUniforms {\n"
                        "    mat4 eye_to_world;\n    vec4 precip_fall;\n    vec4 precip_slant;\n    vec4 precip_near;\n"
                        "    vec4 precip_far;\n    vec4 ground;\n    vec4 ripple;\n} rain;\n"
                        "layout(set = 0, binding = 23) uniform usampler2D rain_map;\n"
                        "#define PRECIP_BLOCK rain\n#define PRECIP_MAP rain_map\n#define PRECIP_SIZE " +
                        std::to_string(precip::kMapSize) + "\n";
    block += render::rain::glsl_constants();
    block += precip::reached_glsl();
    block += render::rain::ripple_glsl();
    block += render::rain::surface_glsl();
    return block;
}

// (NOT ORIGINAL - the surface trails) At the start of a key with FragmentKey::trails (its derivatives in uniform control
// flow): the surface's world position (the eye-space one handed on, the view's inverse - either block's), its own normal
// from the position's screen derivatives turned toward the eye, then the snow's amount and the paint's coat there.
void trail_main(std::string& out, const FragmentKey& key)
{
    const char* const eye_to_world = (key.trails & kTrailSnow) != 0U    ? "snow.eye_to_world"
                                      : (key.trails & kTrailPaint) != 0U ? "paint.eye_to_world"
                                                                         : "rain.eye_to_world";
    add(out, {"    vec3 trail_world = (", eye_to_world, " * vec4(v_surface_eye, 1.0)).xyz;\n"
              "    vec3 trail_normal = cross(dFdx(trail_world), dFdy(trail_world));\n"
              "    float trail_length = length(trail_normal);\n"
              "    trail_normal = trail_length > 1.0e-12 ? trail_normal / trail_length : vec3(0.0, 1.0, 0.0);\n"
              "    if (dot(trail_normal, ", eye_to_world, "[3].xyz - trail_world) < 0.0) trail_normal = -trail_normal;"});
    if ((key.trails & kTrailSnow) != 0U) {
        add(out, {"    float snow_amount = snow_cover(trail_world, trail_normal);"});
    }
    if ((key.trails & kTrailPaint) != 0U) {
        add(out, {"    vec4 paint_coat = paint_at(trail_world, trail_normal);"});
    }
    if ((key.trails & kTrailWet) != 0U) { // (NOT ORIGINAL - [Graphics] rain: the water there; porous unless the surface map
                                          // says otherwise - surface_lighting)
        add(out, {"    float rain_flux = max(dot(trail_normal, -rain.precip_fall.xyz), 0.0) / max(-rain.precip_fall.y, 0.05);\n"
                  "    float rain_reach = rain_flux > 0.0 ? precip_reached(trail_world + trail_normal * 0.5) : 0.0;\n"
                  "    vec4 rain_wet = rain_wetness(trail_world, trail_normal, rain_reach, rain_flux);\n"
                  "    float rain_porous = 1.0;"});
    }
}

// (NOT ORIGINAL - the surface trails) An albedo (`target`: its rgb) covered: the snow over the surface, the paint over both.
void trail_albedo(std::string& out, const FragmentKey& key, const char* target, const char* indent, const char* own = nullptr)
{
    // the snow's colour over what else colours the surface besides `target`: `own` (a pass colour), the relit tint
    const std::string snow_colour = own != nullptr ? std::string("(snow.albedo.rgb / ") + own + ")" : std::string("snow.albedo.rgb");
    if ((key.trails & kTrailSnowTint) != 0U) {
        // (the relit tint the vertex colour carries replaced too: the snow over it, where the vertex has the stream -
        // word 2's alpha; the stream's tint decoded by the vertex stage)
        add(out, {indent, target, " = mix(", target, ", v_cel_residual.a > 0.5 ? ", snow_colour, " / max(v_cel_tint.rgb, "
                  "vec3(0.04)) : ", snow_colour, ", snow_amount);"});
    } else if ((key.trails & kTrailSnow) != 0U) {
        add(out, {indent, target, " = mix(", target, ", ", snow_colour, ", snow_amount);"});
    }
    if ((key.trails & kTrailPaint) != 0U) {
        add(out, {indent, target, " = mix(", target, ", paint_coat.rgb, paint_coat.a);"});
    }
    if ((key.trails & kTrailWet) != 0U) {
        // (NOT ORIGINAL - [Graphics] rain: the albedo under water darkened in linear light - the texel with what else colours
        // the surface besides it, `own`, as one albedo - by the water's share of the surface)
        const std::string colour = own != nullptr ? std::string("(") + target + " * " + own + ")" : std::string(target);
        const std::string back = own != nullptr ? std::string("rain_a / ") + own : std::string("rain_a");
        add(out, {indent, "{\n", indent, "    float rain_water = max(max(rain_wet.x * rain_porous, rain_wet.y), rain_wet.z);\n",
                  indent, "    vec3 rain_a = linear_from_display(clamp(", colour, ", 0.0, 1.0));\n", indent,
                  "    rain_a = display_from_linear(mix(rain_a, rain_wet_albedo(rain_a), rain_water));\n", indent, "    ", target,
                  " = ", back, ";\n", indent, "}"});
    }
}

// (added by surface relief) relief_uv / relief_dx / relief_dy (stage 0's coordinates moved, their derivatives) and
// relief_shadow (the sun's share the relief lets through) - kDetailReliefBlock's comment has the formulas.
void relief_march(std::string& out, const FragmentKey& key)
{
    add(out, {"    vec2 relief_uv = ", coord_of(key, 0), ";\n"
              "    vec2 relief_dx = dFdx(relief_uv);\n    vec2 relief_dy = dFdy(relief_uv);\n    float relief_shadow = 1.0;\n"
              "    {\n        vec3 c1 = cross(surface_dy, surface_geo);\n        vec3 c2 = cross(surface_geo, surface_dx);\n"
              "        float det = dot(surface_geo, cross(surface_dx, surface_dy));\n"
              "        vec2 size = vec2(textureSize(surface_map, 0));\n"
              "        float lod = log2(max(max(length(relief_dx * size), length(relief_dy * size)), 1.0e-6));\n"
              "        vec3 r = normalize(surface_eye);\n        float rn = -dot(r, surface_geo);\n"
              "        float w = (1.0 - smoothstep(d.relief.y, d.relief.z, lod)) * smoothstep(0.05, 0.2, rn);\n"
              "        if (w > 0.0 && det != 0.0) {\n"
              "            vec3 gu = (c1 * relief_dx.x + c2 * relief_dy.x) / det;\n"
              "            vec3 gv = (c1 * relief_dx.y + c2 * relief_dy.y) / det;\n"
              "            float texels = sqrt(length(gu) * size.x * length(gv) * size.y);\n"
              "            if (texels > 0.0) {\n                float depth = d.relief.x * w;\n"
              "                vec2 p = vec2(dot(gu, r), dot(gv, r)) * (depth / rn);\n"
              "                int steps = int(mix(32.0, 8.0, rn));\n                float layer = 1.0 / float(steps);\n"
              "                float above = 1.0;\n                float below = 1.0;\n"
              "                float h = relief_height(relief_uv - 0.5 * p, relief_dx, relief_dy);\n"
              "                for (int i = 0; i < steps && h < below; ++i) {\n                    above = below;\n"
              "                    below -= layer;\n"
              "                    h = relief_height(relief_uv + p * (0.5 - below), relief_dx, relief_dy);\n                }\n"
              "                for (int i = 0; i < 5; ++i) {\n                    float mid = 0.5 * (above + below);\n"
              "                    if (relief_height(relief_uv + p * (0.5 - mid), relief_dx, relief_dy) < mid) above = mid;\n"
              "                    else below = mid;\n                }\n"
              "                relief_uv += p * (0.5 - below);"});
    if ((key.surface & kSurfaceLit) == 0U) {
        add(out, {"                float ln = dot(s.sun.xyz, surface_geo);\n"
                  "                if (s.sun.w > 0.0 && ln > 0.0 && below < 1.0) {\n"
                  "                    vec2 q = vec2(dot(gu, s.sun.xyz), dot(gv, s.sun.xyz)) * (depth / max(ln, 0.1));\n"
                  "                    float blocked = 0.0;\n"
                  "                    for (int i = 1; i <= 8; ++i) {\n"
                  "                        float rise = (1.0 - below) * float(i) / 8.0;\n"
                  "                        blocked = max(blocked, relief_height(relief_uv + q * rise, relief_dx, relief_dy) - "
                  "(below + rise));\n                    }\n"
                  "                    relief_shadow = blocked > 0.0 ? 0.0 : 1.0;\n"
                  "                }"});
    }
    add(out, {"            }\n        }\n    }"});
}

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
    out += rt::brdf::brdf_glsl(); // (round M: the light model)
    if ((key.surface & kSurfaceCel) != 0U) {
        cel_declarations(out, key); // (added by cel shading)
    }
    if ((key.surface & kSurfaceRelit) != 0U && (key.surface & kSurfaceCel) == 0U && (key.trails & kTrailSnowTint) == 0U) {
        add(out, {kCelVaryingsIn}); // (round M: the relit world's light store, its words 0 - 2)
    }
    if (key.detail != 0U) {
        detail_declarations(out, key); // (NOT ORIGINAL - rt_detail / rt_delight)
    }
}

void surface_lighting(std::string& out, const FragmentKey& key)
{
    if ((key.surface & kSurfaceCelHull) != 0U) { // (added by cel shading: the hull is ink - no light)
        add(out, {"    vec4 surface_v0 = v_diffuse;"});
        return;
    }
    add(out, {"    vec3 surface_eye = v_surface_eye;\n    vec3 surface_dx = dFdx(surface_eye);\n"
              "    vec3 surface_dy = dFdy(surface_eye);\n    vec3 surface_face = cross(surface_dx, surface_dy);\n"
              "    surface_face = dot(surface_face, surface_face) > 0.0 ? normalize(surface_face) : vec3(0.0, 0.0, -1.0);\n"
              "    if (dot(surface_face, surface_eye) > 0.0) surface_face = -surface_face;\n"
              "    vec3 surface_geo = dot(v_surface_normal, v_surface_normal) > 1.0e-12 ? normalize(v_surface_normal) "
              ": surface_face;\n"
              "    vec3 surface_n = surface_geo;\n    vec3 surface_v = normalize(-surface_eye);\n"
              "    float surface_rough = s.params.z;"});
    if ((key.detail & kDetailSkin) != 0U) {
        // (round M) the surface's radius of curvature, millimetres: how far the position moves over how far the normal
        // turns between neighbouring pixels (sun_light.w: millimetres a world unit) - skin's diffusion wraps by it
        add(out, {"    float surface_curvature = length(fwidth(surface_eye)) / max(length(fwidth(surface_geo)), 1.0e-6) * "
                  "s.sun_light.w;"});
    }
    const bool hair = (key.detail & kDetailHair) != 0U; // (added for the characters: the strands' frame kept)
    if (hair) {
        add(out, {"    vec3 surface_t = vec3(0.0);\n    vec3 surface_b = vec3(0.0);"});
    }
    const bool detail = (key.detail & kDetailMaterial) != 0U; // (NOT ORIGINAL - rt_detail)
    if (detail) {
        add(out, {"    vec4 detail_m = vec4(0.5);\n    float detail_w = 0.0;"});
    }
    const bool relief = (key.detail & kDetailRelief) != 0U && (key.surface & kSurfaceMap) != 0U; // (added by surface relief)
    if (relief) {
        relief_march(out, key);
    }
    if ((key.surface & kSurfaceMap) != 0U) {
        if (relief) { // (added by surface relief: the moved coordinates, read with their own derivatives)
            add(out, {"    {\n        vec2 uv = relief_uv;\n        vec4 m = textureGrad(surface_map, uv, relief_dx, relief_dy);"});
        } else {
            add(out, {"    {\n        vec2 uv = ", coord_of(key, 0), ";\n        vec4 m = texture(surface_map, uv);"});
        }
        add(out, {"        surface_rough = m.b;\n        vec2 xy = (m.rg * 255.0 - 127.5) / 127.5;\n"
                  "        vec3 nt = vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));\n",
                  relief ? "        vec2 uv_dx = relief_dx;\n        vec2 uv_dy = relief_dy;"
                         : "        vec2 uv_dx = dFdx(uv);\n        vec2 uv_dy = dFdy(uv);"});
        if (detail) {
            add(out, {relief ? "        detail_m = textureGrad(detail_map, uv * d.scale.x, uv_dx * d.scale.x, uv_dy * d.scale.x);\n"
                             : "        detail_m = texture(detail_map, uv * d.scale.x);\n",
                      "        float detail_lod = log2(max(max(length(uv_dx), length(uv_dy)) * d.scale.x * "
                      "float(textureSize(detail_map, 0).x), 1.0e-6));\n"
                      "        detail_w = d.scale.y * (1.0 - smoothstep(d.scale.z, d.scale.w, detail_lod));\n"
                      "        vec2 detail_xy = (detail_m.rg * 255.0 - 127.5) / 127.5 * detail_w;\n"
                      "        nt = normalize(vec3(nt.xy + detail_xy * nt.z, nt.z));"});
        }
        add(out, {"        vec3 c1 = cross(surface_dy, surface_geo);\n        vec3 c2 = cross(surface_geo, surface_dx);\n"
                  "        float det = dot(surface_geo, cross(surface_dx, surface_dy));\n"
                  "        vec3 t = (c1 * uv_dx.x + c2 * uv_dy.x) * sign(det);\n"
                  "        vec3 b = (c1 * uv_dx.y + c2 * uv_dy.y) * sign(det);\n"
                  "        float scale = max(dot(t, t), dot(b, b));\n"
                  "        vec4 uv_change = abs(vec4(uv_dx, uv_dy));\n"
                  "        float uv_floor = 1.0e-6 * max(1.0, max(abs(uv.x), abs(uv.y)));\n"
                  "        if (scale > 0.0 && det != 0.0 && max(max(uv_change.x, uv_change.y), max(uv_change.z, uv_change.w)) > "
                  "uv_floor) {\n            float inv = inversesqrt(scale);\n"
                  "            surface_n = normalize(t * (inv * nt.x) + b * (inv * nt.y) + surface_geo * nt.z);",
                  hair ? "\n            surface_t = t;\n            surface_b = b;" : "", "\n        }\n    }"});
    }
    if (detail) {
        add(out, {"    surface_rough = mix(surface_rough, d.material.x, d.material.w) + (detail_m.b - 0.5) * 2.0 * "
                  "d.material.y * detail_w;"});
    }
    add(out, {"    surface_rough = clamp(surface_rough, 0.0, 1.0);"});
    if ((key.trails & kTrailSnow) != 0U) {
        // (NOT ORIGINAL - the snow: what it covers loses its own relief and gloss - the bricks' joints, the asphalt's
        // grain - under it: the surface's own shape, the snow's roughness)
        add(out, {"    surface_n = normalize(mix(surface_n, surface_geo, snow_amount));\n"
                  "    surface_rough = mix(surface_rough, 0.75, snow_amount);"});
    }
    if ((key.trails & kTrailWet) != 0U) {
        // (NOT ORIGINAL - [Graphics] rain: the surface's porosity from its roughness - rain.h porosity_of, the material
        // table's seam -; under a film or a puddle the water's roughness; a puddle's normal the water's, with its rings)
        add(out, {"    rain_porous = rain_porosity(surface_rough);\n"
                  "    surface_rough = mix(surface_rough, RAIN_WATER_ROUGHNESS, rain_wet.w);\n"
                  "    if (rain_wet.z > 0.0) {\n"
                  "        vec2 rain_rs = rain_ripple(trail_world.xz, rain.ripple);\n"
                  "        vec3 rain_up = normalize(transpose(mat3(rain.eye_to_world)) * vec3(-rain_rs.x, 1.0, -rain_rs.y));\n"
                  "        surface_n = normalize(mix(surface_n, rain_up, rain_wet.z));\n"
                  "    }"});
    }
    if ((key.surface & kSurfaceCel) != 0U) {
        cel_lighting(out, key); // (added by cel shading)
        return;
    }
    // (round M) the program makes the albedo under white light (the game's light 1: v0 0.5); surface_finish lights it
    add(out, {"    vec4 surface_v0 = vec4(0.5, 0.5, 0.5, ", (key.surface & kSurfaceLit) != 0U ? "v_surface_cd.a" : "v_diffuse.a",
              ");"});
}

// (NOT ORIGINAL - round DB) The decals' material layers laid over a material before it is lit - the deferred decal's
// DBuffer (UE's DBuffer decals; vulkan_surface_light.h SurfaceInputs::decals / layer): its albedo x a + rgb, its
// roughness x a + r, its F0 x a + g. `material` names a BrdfMaterial, `indent` the lines' indent. Laid where the
// surface's albedo is final (after a relit surface's tint, an object's material colour) - the decal's colour is its own.
std::string decal_layer_onto(std::string_view material, std::string_view indent)
{
    const std::string m(material);
    const std::string in(indent);
    return in + m + ".albedo = " + m + ".albedo * surface_decals.a + surface_decals.rgb;\n" + in + m + ".roughness = clamp(" +
           m + ".roughness * surface_layer.a + surface_layer.r, 0.0, 1.0);\n" + in + m + ".f0 = " + m +
           ".f0 * surface_layer.a + vec3(surface_layer.g);\n";
}

// (round M - the per-pixel lighting's comment has the model) After the program: `colour` (its albedo under white
// light, display-referred) lit - linear light in a linear frame, else encoded for the display.
void surface_finish(std::string& out, const FragmentKey& key)
{
    // (round ALBEDO) the game's colour on the geometry (pass_tint_of: the time of day's sector colour, an object's light
    // where it stands) - the original's light on it, never the albedo: it scales the light the original gave the surface
    // itself (the vertex's, the draw's own lights); the light this renderer makes (the store's, the lamps', the rays')
    // doesn't take it. Linear: the display's power law makes the colour's factor the light's.
    add(out, {"    vec3 surface_tint_light = linear_from_display(pass_tint_of(0));"});
    add(out, {"    vec3 surface_albedo = linear_from_display(clamp(colour.rgb, 0.0, 1.0));\n"
              "    float surface_nv = max(dot(surface_n, surface_v), 1.0e-4);\n"
              "    float surface_hemi = 0.5 + 0.5 * dot(surface_n, surface_geo);\n"
              "    vec3 surface_out = vec3(0.0);"});
    // a material from an albedo: a metal's F0 is its colour, its diffuse none
    add(out, {"    BrdfMaterial surface_m = brdf_material(surface_albedo * (1.0 - s.params.x), mix(vec3(s.params.w), "
              "surface_albedo, s.params.x), surface_rough);"});
    if ((key.trails & kTrailWet) != 0U) {
        // (NOT ORIGINAL - [Graphics] rain: rain.h wet_brdf_material's twin on the material's inputs - the albedo darkened and
        // the roughness set before; where the water lies flat its F0 is water's)
        add(out, {"    surface_m.f0 = mix(surface_m.f0, vec3(RAIN_WATER_F0), rain_wet.w);"});
    }
    if ((key.detail & kDetailSkin) != 0U) {
        add(out, {"    surface_m.scatter_mm = s.scatter.rgb;\n    surface_m.curvature_mm = surface_curvature;"});
    }
    if ((key.detail & kDetailCloth) != 0U) {
        add(out, {"    surface_m.sheen = s.sheen.rgb;\n    surface_m.sheen_roughness = s.sheen.w;"});
    }
    // the environment's specular: the light store's estimate, or the reflections pass's traced one (params.y)
    add(out, {"    float surface_env = s.params.y > 0.0 ? smoothstep(0.75 * s.params.y, s.params.y, surface_rough) : 1.0;"});
    // (NOT ORIGINAL - round V: the surface's light inputs at its pixel - kLightInputsGlsl; without them the vertex's own:
    // the sun's visibility as the store has it, nothing occluded, no lamps beyond the store's)
    const bool inputs = (key.light & kLightInputs) != 0U;
    add(out, {inputs ? "    vec4 surface_li = light_in_now();\n    vec3 surface_lamps = light_lamps_now();\n"
                       "    vec3 surface_specular_in = light_specular_now();"
                     : "    vec4 surface_li = vec4(-1.0, 0.0, 1.0, 0.0);\n    vec3 surface_lamps = vec3(0.0);\n"
                       "    vec3 surface_specular_in = vec3(0.0);"});
    // (NOT ORIGINAL - round FL: the front layer - a see-through surface lit by the light list as everything else is: its
    // sun's or moon's visibility traced there, the rest of the list's light - sky, emitters, lamps, the further bounces -
    // on its albedo as the lamps input's is; the store's environment has no weight there, as where ReSTIR GI lit a
    // surface - its specular environment is the reflections pass's for the in-place classes)
    const bool front = (key.light & kLightFront) != 0U;
    if (front) {
        add(out, {"    if (light_front_at) {\n        surface_li = vec4(light_front_sun_now().a, 0.0, 0.0, 1.0);\n"
                  "        surface_lamps = light_front_now().rgb;\n        surface_specular_in = vec3(0.0);\n    }"});
    }
    // (round DB: the decals' material layers - the environment's weight above stays the surface's own roughness's: what
    // the reflections pass, which sees the surface and not its decals, takes over; the layered material weighs the rest)
    std::string layer_m;
    std::string layer_lit;
    std::string layer_m_inner;
    if (inputs) {
        add(out, {"    vec4 surface_decals = light_decals_now();\n    vec4 surface_layer = light_layer_now();"});
        layer_m = decal_layer_onto("surface_m", "    ");
        layer_lit = decal_layer_onto("surface_lit", "    ");
        layer_m_inner = decal_layer_onto("surface_m", "        ");
        if ((key.light & kLightLayerWrite) != 0U) {
            // (round DECAL: a layer being laid - its material where the decals' layers would be laid over a surface's,
            // its albedo final there: what it writes over the decals' inputs - layer_write_out)
            layer_m = "    layer_write_m = surface_m;\n";
            layer_lit = "    layer_write_m = surface_lit;\n";
            layer_m_inner = "        layer_write_m = surface_m;\n";
        }
    }
    // (round BIAS) The lamps input - a white diffuser's light (ReSTIR DI's lamps, ReSTIR GI's diffuse, the front layer's)
    // - goes on the material's diffuse lobe as the light model has it: albedo x (1 - E) (brdf_ambient's diffuse part,
    // brdf_direct's), the share its specular lobe (E x the specular input) doesn't take. Before, albedo x it: every
    // lamp and bounce counted the light the specular reflects a second time in the diffuse (1 / (1 - E) too bright -
    // 4 - 30 % on a dielectric, most at grazing views), against the reference and the processor check's own model.
    if ((key.surface & kSurfaceLit) != 0U) {
        const bool hair = (key.detail & kDetailHair) != 0U;
        if (hair) {
            add(out, {"    vec3 surface_strand = mix(surface_b, surface_t, s.scatter.w);\n"
                      "    bool surface_fibre = dot(surface_strand, surface_strand) > 1.0e-12;\n"
                      "    if (surface_fibre) surface_strand = normalize(surface_strand);"});
        }
        // (round O's one light: v_surface_base.w -1 - the lights and the base are linear light already)
        add(out, {"    bool surface_one = v_surface_base.w < -0.5;\n"
                  "    vec3 surface_ambient = (surface_one ? v_surface_base.rgb : linear_from_display(v_surface_base.rgb)) * "
                  "surface_tint_light;\n"
                  // (round ALBEDO: one albedo definition - the draw's diffuse colour, Cd - the vertex colour or the
                  // game's material colour, 0.5 the game's neutral under its doubled light - is never the albedo: the
                  // texel x the material's own colour is, as the rays' hit_surface has it)
                  "    BrdfMaterial surface_lit = surface_m;\n",
                  layer_m, layer_lit,
                  "    surface_out = brdf_ambient(surface_m, surface_nv, surface_ambient * surface_hemi, surface_ambient * "
                  "surface_env) * surface_li.b;\n"
                  "    surface_out += brdf_ambient(surface_lit, surface_nv, surface_lamps, vec3(0.0));\n"
                  "    for (int i = 0; i < 8; ++i) {\n"
                  "        vec3 l = s.light_toward[i].xyz;\n        if (dot(l, l) == 0.0) continue;\n"
                  "        vec3 light = (surface_one ? max(s.light_colour[i].rgb, vec3(0.0)) : linear_from_display(s.light_colour[i].rgb)) "
                  "* surface_tint_light;\n"
                  // (round V: the one light's key - light 0 - is the sun or the moon: its visibility at the pixel)
                  "        if (i == 0 && surface_one) light *= light_seen(surface_li, 1.0);"});
        if (hair) {
            add(out, {"        if (surface_fibre) {\n"
                      "            surface_out += brdf_hair_direct(surface_lit.albedo, surface_rough, surface_strand, surface_v, l, "
                      "light);\n            continue;\n        }"});
        }
        add(out, {"        surface_out += brdf_direct(surface_lit, surface_n, surface_v, l, light);\n    }"});
    } else if ((key.surface & kSurfaceRelit) != 0U) {
        const bool relief = (key.detail & kDetailRelief) != 0U && (key.surface & kSurfaceMap) != 0U;
        // (round L: v_cel_light / v_cel_residual / v_cel_emit are light in the frame's unit - the store's physical light x
        // the frame's pre-exposure; the tint decoded by the vertex stage - round BAKEALB. The vertex's own emission - a lamp's lens, bulb or
        // panel at its luminance - added as it is (x the texel's colour: the lamp's texture), on the lamp texels alone for
        // a lamp drawn in a texture: their brightest channel at least rt_lights.h kTexelBright, the bake's own test)
        add(out, {"    const float surface_word = 255.0 / 128.0;\n"
                  "    if (v_cel_residual.a > 0.5) {\n"
                  "        vec3 tint = v_cel_tint.rgb;\n"
                  "        vec3 lamp_texel = surface_albedo;\n"
                  // (round FG - one lamp-texel test: the bake's, its texture's floor coded in the store's flags - the same
                  // code on every vertex of the mesh, so interpolated it is the code; rt_lights.h lamp_texel_glsl)
                  "        float lamp_lit = lamp_texel_shown(lamp_texel, colour.a, uint(v_cel_emit.a * 255.0 + 0.5)) ? 1.0 "
                  ": 0.0;\n"
                  "        surface_m.albedo *= tint;\n        surface_m.f0 = mix(surface_m.f0, surface_m.f0 * tint, s.params.x);\n",
                  layer_m_inner,
                  "        vec3 env = v_cel_light.rgb + v_cel_residual.rgb / max(tint, vec3(1.0 / 128.0));\n"
                  "        surface_out = brdf_ambient(surface_m, surface_nv, env * surface_hemi, env * surface_env) * surface_li.b;\n"
                  "        if (s.sun.w > 0.5 && dot(surface_geo, s.sun.xyz) > 0.0) {\n"
                  "            surface_out += brdf_direct(surface_m, surface_n, surface_v, s.sun.xyz, s.sun_light.rgb * "
                  "light_seen(surface_li, v_cel_tint.a)",
                  relief ? " * relief_shadow" : "", ");\n        }\n"
                  "        surface_out += v_cel_emit.rgb * lamp_texel * lamp_lit;\n"
                  "        surface_out += brdf_ambient(surface_m, surface_nv, surface_lamps, vec3(0.0));\n"
                  "    } else {\n",
                  layer_m_inner,
                  "        vec3 light = linear_from_display(v_diffuse.rgb * surface_word) * surface_tint_light;\n"
                  "        surface_out = brdf_ambient(surface_m, surface_nv, light * surface_hemi, light * surface_env) * "
                  "light_share(surface_li) + brdf_ambient(surface_m, surface_nv, surface_lamps, vec3(0.0));\n",
                  // (round FL: on the front layer the vertex's light has no share - the disc's light by the light model, as
                  // with a store)
                  front ? "        if (light_front_at && s.sun.w > 0.5 && dot(surface_geo, s.sun.xyz) > 0.0) {\n"
                          "            surface_out += brdf_direct(surface_m, surface_n, surface_v, s.sun.xyz, s.sun_light.rgb * "
                          "surface_li.r);\n        }\n"
                        : "",
                  "    }"});
    } else {
        // the bake: its light (the vertex colour doubled, display-referred) as the environment's (round V: its light not
        // separated - taken by shares, light_share)
        add(out, {layer_m, "    vec3 surface_bake = linear_from_display(v_diffuse.rgb * 2.0) * surface_tint_light;\n"
                  "    surface_out = brdf_ambient(surface_m, surface_nv, surface_bake * surface_hemi, surface_bake * surface_env) "
                  "* light_share(surface_li) + brdf_ambient(surface_m, surface_nv, surface_lamps, vec3(0.0));"});
    }
    if (inputs) {
        // (path tracing, stage 5 - ReSTIR GI: the environment's specular traced, on the material's specular albedo - in
        // place of the store's estimate, whose weight, the indirect visibility, is 0 where this was made)
        add(out, {"    surface_out += brdf_specular_albedo(surface_m.f0, surface_nv, surface_m.roughness) * surface_specular_in;"});
    }
    if ((key.light & kLightGuides) != 0U && (key.light & kLightInputs) != 0U) {
        // (round V: Ray Reconstruction's guides - the material the light model was given)
        add(out, {(key.surface & kSurfaceLit) != 0U ? "    guide_albedo = surface_lit.albedo;\n"
                                                    : "    guide_albedo = surface_m.albedo;\n",
                  "    guide_specular = brdf_specular_albedo(surface_m.f0, surface_nv, surface_m.roughness);\n"
                  "    guide_normal = surface_n;\n    guide_rough = surface_m.roughness;\n    guide_has_normal = 1.0;"});
    }
    add(out, {"    colour.rgb = frame_linear() ? surface_out : clamp(display_from_linear(surface_out), 0.0, 1.0);"});
}

// Whether the key's colour leaves surface_finish as light already (not clamped, not decoded again): a surface lit per
// pixel, not in bands, not the hull, not water (water's shading takes display colours: it gets the encoded result).
bool surface_light_out(const FragmentKey& key)
{
    return (key.surface & kSurfaceLighting) != 0U && (key.surface & (kSurfaceCel | kSurfaceCelHull)) == 0U && key.water == 0U;
}

// (NOT ORIGINAL - round DECAL) A see-through layer laid into the surface under it (ff_key.h kLightLayer): its laying
// (kLightLayerWrite - the decals' inputs written) and its shaded draw (discarded where it was laid). Only on a draw lit
// per pixel with the inputs (its material apart from its light, its face for the layer test).
bool layer_key(const FragmentKey& key)
{
    return (key.light & kLightLayer) != 0U && (key.light & kLightInputs) != 0U && surface_light_out(key);
}
bool layer_write(const FragmentKey& key) { return layer_key(key) && (key.light & kLightLayerWrite) != 0U; }
bool layer_laid(const FragmentKey& key) { return layer_key(key) && (key.light & kLightLayerWrite) == 0U; }

void fragment_declarations(std::string& out, const FragmentKey& key, uint32_t sampled)
{
    add(out, {"layout(set = 0, binding = ", num(kFragmentUniformsBinding), ", std140) uniform FragmentUniforms {"});
    add(out, {"    vec4 constant[8];\n    vec4 texture_factor;\n    vec4 fog_colour;\n    vec4 fog;\n"
              "    vec4 alpha_reference;\n    vec4 pass_tint;\n} u;"});
    // (round F - a linear, scene-referred frame: fragment_linear_output) the frame's transfer; whether this draw writes
    // linear light (u.alpha_reference.y); a stage's texel as the game's colour arithmetic takes it - a texture that
    // holds linear light (a copy of the world drawn this view - u.alpha_reference.z's bit) encoded back to the display's
    // colours. (The water's, snow's and paint's blocks below may call them.)
    out += rt::kDisplayTransferGlsl;
    if ((key.surface & kSurfaceRelit) != 0U) {
        out += rt::lamp_texel_glsl(); // (NOT ORIGINAL - round FG: the bake's lamp-texel test, a relit surface's emission)
    }
    // (NOT ORIGINAL - round ALBEDO) the factor the game put on pass `p`'s colour (Gpu::takes_pass_tint: a geometry's
    // or an instance's colour - the original's light on it), 1 where it put none
    add(out, {"vec3 pass_tint_of(int p) { return (int(u.pass_tint.w) & (1 << p)) != 0 ? u.pass_tint.rgb : vec3(1.0); }"});
    add(out, {"bool frame_linear() { return u.alpha_reference.y > 0.5; }\n"
              "vec4 stage_texel(int stage, vec4 t)\n{\n"
              "    return (int(u.alpha_reference.z) & (1 << stage)) != 0 ? vec4(display_from_linear(t.rgb), t.a) : t;\n}"});
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
    } else if (key.water != 0U) {
        add(out, {kSurfaceVaryingsIn}); // (NOT ORIGINAL - rt_water: the water's inputs alone)
    }
    if ((key.light & kLightFront) != 0U) {
        // (NOT ORIGINAL - round FL: the front layer's light - before the water's, which reads it)
        static_assert(kLightInputsSet == 1 && kLightFrontDepthBinding == 8 && kLightFrontBinding == 9 &&
                          kLightFrontSunBinding == 10 && kLightFrontReflectBinding == 11,
                      "kLightFrontGlsl names them");
        add(out, {"#define LIGHT_FRONT"});
        add(out, {kLightFrontGlsl});
    }
    if (key.water != 0U) {
        add(out, {water_declarations_glsl(key.water == kWaterMoving)}); // (NOT ORIGINAL - rt_water, water_shader.h)
    }
    if (key.trails != 0U) { // (NOT ORIGINAL - the surface trails: their inputs alone without the surface's)
        static_assert(kSnowUniformsBinding == 17 && kSnowSkyBinding == 18, "kSnowBlock names them");
        static_assert(kPaintUniformsBinding == 19 && kPaintCoatBinding == 20 && kPaintPlaceBinding == 21, "kPaintBlock names them");
        if (key.surface == 0 && key.water == 0U) {
            add(out, {kSurfaceVaryingsIn});
        }
        const bool wet = (key.trails & kTrailWet) != 0U;
        if (wet) { // (NOT ORIGINAL - [Graphics] rain; before the snow's: its precip_reached is the snow's too)
            add(out, {wet_block()});
        }
        if ((key.trails & kTrailSnow) != 0U) {
            add(out, {kSnowDeclarations});
            if ((key.trails & kTrailSnowTraced) != 0U) {
                // (round W: the precipitation occlusion map - the rain's binding, one map; with the wet ground's block its
                // precip_reached already declared over the same map)
                add(out, {"#define SNOW_TRACED"});
                if (!wet) {
                    add(out, {"layout(set = 0, binding = 23) uniform usampler2D snow_map;\n#define PRECIP_BLOCK snow\n"
                              "#define PRECIP_MAP snow_map\n#define PRECIP_SIZE ",
                              num(precip::kMapSize)});
                    add(out, {precip::reached_glsl()});
                }
            }
            if ((key.trails & kTrailSnowTint) != 0U && (key.surface & kSurfaceCelStream) == 0U) {
                add(out, {kCelVaryingsIn}); // (the relit world's stream: its tint)
            }
            add(out, {kSnowBlock});
        }
        if ((key.trails & kTrailPaint) != 0U) {
            add(out, {kPaintBlock});
        }
    }
    if ((key.light & (kLightInputs | kLightShare)) != 0U) {
        static_assert(kLightInputsSet == 1 && kLightVisibilityBinding == 0 && kLightLampsBinding == 1 &&
                          kLightLampsOnBinding == 2 && kLightDepthBinding == 3 && kLightDecalsBinding == 4 &&
                          kLightSpecularBinding == 6 && kLightLayerBinding == 7,
                      "kLightInputsGlsl names them");
        if (key.surface != 0) { // (round DB: a surface lit per pixel - its eye position and face - lies on the prepass's)
            add(out, {"#define LIGHT_LAYERS\n#define LIGHT_LAYER_LIFT ", num(kLayerLift)});
        }
        if (layer_write(key)) {
            add(out, {"#define LIGHT_LAYER_WRITE"}); // (round DECAL: the decals' inputs written, not read)
        }
        add(out, {kLightInputsGlsl}); // (NOT ORIGINAL - round V: the surface's light inputs)
    }
    if (layer_write(key)) {
        // (NOT ORIGINAL - round DECAL) a layer laid over the surface under it: its material (surface_finish) and the
        // second attachment, the layer's roughness and F0 (vulkan_surface_light.h SurfaceInputs::decals / layer)
        add(out, {"BrdfMaterial layer_write_m;\nlayout(location = 1) out vec4 o_layer;"});
    }
    if (key.surface != 0 || (key.light & (kLightInputs | kLightShare)) != 0U) {
        add(out, {kLightHelpersGlsl});
    }
    if ((key.light & kLightGuides) != 0U && (key.light & (kLightInputs | kLightShare)) != 0U) {
        static_assert(kLightGuidesBinding == 5, "kLightGuidesGlsl names it");
        add(out, {kLightGuidesGlsl}); // (NOT ORIGINAL - round V: Ray Reconstruction's guides)
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

// (NOT ORIGINAL - round BIAS) On a surface lit per pixel, a stage sampled along the reflection vector (the game's
// environment-mapped pass: a painted reflection) adds no colour to the albedo - its texel reads black, its alpha kept for
// the blends. The surface's reflection is the reflections pass's (its traced light on the specular albedo); lit as an
// albedo, the painted one was a second reflection under the lamps - BO's bank windows a glass pane whose environment
// map (a night city with lit squares) the lamps lit as a white wall. The rays' surfaces take the same rule
// (vulkan_rt_materials.h material_colour: the first pass black when it is the environment pass, environment layers
// none).
// Which stages those are is the draw's (u.alpha_reference.w: a bit per stage - vulkan_draws.cpp set_uniforms, from the
// vertex half's coordinates; the fragment key has no room for them).
void environment_texel_black(std::string& out, const FragmentKey& key, std::size_t stage, const std::string& texel,
                             std::string_view indent)
{
    if ((key.surface & kSurfaceLighting) == 0U) {
        return;
    }
    add(out, {std::string(indent), "if ((int(u.alpha_reference.w) & ", num(1U << stage), ") != 0) ", texel,
              ".rgb = vec3(0.0);"});
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
            add(out, {"        vec4 tex = stage_texel(", num(stage), ", texture(t", num(stage), "_map, ", coord_of(key, stage), "));"});
            environment_texel_black(out, key, stage, "tex", "        ");
            if (stage == 0) { // (NOT ORIGINAL - the surface trails: stage 0's texel covered)
                trail_albedo(out, key, "tex.rgb", "        ");
            } else if ((key.trails & kTrailSnow) != 0U) {
                // (NOT ORIGINAL - the snow: a later stage's texture - a detail layer over the ground, a lawn's grass, the
                // road's grit - under the snow too: its texel toward what leaves the colour as it is, for its op)
                const char* neutral = nullptr;
                switch (ops.colour_op) {
                case StageOp::kModulate:
                    neutral = "1.0";
                    break;
                case StageOp::kModulate2x:
                case StageOp::kAddSigned:
                case StageOp::kAddSigned2x:
                    neutral = "0.5";
                    break;
                case StageOp::kModulate4x:
                    neutral = "0.25";
                    break;
                case StageOp::kAdd:
                case StageOp::kSubtract:
                case StageOp::kAddSmooth:
                    neutral = "0.0";
                    break;
                default:
                    break;
                }
                if (neutral != nullptr) {
                    add(out, {"        tex.rgb = mix(tex.rgb, vec3(", neutral, "), snow_amount);"});
                }
            }
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

// (NOT ORIGINAL - round ALBEDO) Whether the draw's colour is an albedo lit per pixel (surface_finish runs on it): its pass
// colours are then the material's own (the albedo: texel x the material's colour, the game's 0.5 the texture as it is).
bool albedo_lit_per_pixel(const FragmentKey& key)
{
    return (key.surface & kSurfaceLighting) != 0U && (key.surface & (kSurfaceCel | kSurfaceCelHull)) == 0U;
}

void ps_common(std::string& out, const FragmentKey& key, uint32_t textures)
{
    add(out, {"    vec4 v0 = ", key.surface != 0 ? "surface_v0" : "v_diffuse",
              ";\n    vec4 v1 = v_specular;\n    vec4 r0 = vec4(0.0);\n    vec4 r1 = vec4(0.0);"});
    if (key.unlit != 0) {
        // The unlit twin (render_pc/view_mode.cpp): each whole v0 operand is c7 = 0.5 there. The game's shaders read v0
        // whole only into colour (rgb) results and its alpha as v0.a, which the twin leaves.
        add(out, {"    v0.rgb = vec3(0.5);"});
    }
    // (NOT ORIGINAL - round ALBEDO: one albedo definition) the pass colours: on a surface lit per pixel the material's
    // own - its albedo is never the game's colour on the geometry, which is the original's light (surface_finish takes
    // it as light where the light is the original's own) -; elsewhere x that colour, as the original scaled them
    const bool own_colours = albedo_lit_per_pixel(key);
    for (uint32_t index = 0; index < kStageCount; ++index) {
        if (own_colours) {
            add(out, {"    vec4 c", num(index), " = clamp(u.constant[", num(index), "], -1.0, 1.0);"});
        } else {
            add(out, {"    vec4 c", num(index), " = clamp(vec4(u.constant[", num(index), "].rgb * pass_tint_of(", num(index),
                      "), u.constant[", num(index), "].a), -1.0, 1.0);"});
        }
    }
    add(out, {"    vec4 c4 = clamp(u.constant[4], -1.0, 1.0);"});
    const bool relief = (key.detail & kDetailRelief) != 0U && (key.surface & kSurfaceMap) != 0U; // (added by surface relief)
    for (uint32_t index = 0; index < textures; ++index) {
        if (index == 0 && relief) { // (added by surface relief: stage 0 where the view ray meets the relief)
            add(out, {"    vec4 t0 = stage_texel(0, textureGrad(t0_map, relief_uv, relief_dx, relief_dy));"});
        } else {
            add(out, {"    vec4 t", num(index), " = stage_texel(", num(index), ", texture(t", num(index), "_map, ",
                      coord_of(key, index), "));"});
        }
        environment_texel_black(out, key, index, "t" + num(index), "    ");
        if ((key.surface & kSurfaceCel) != 0U && (key.surface & kSurfaceCelHull) == 0U) {
            // (added by cel shading - rt_cel_flatten) the colour toward a blurrier mip, its alpha (cut-outs) as it was
            add(out, {"    t", num(index), ".rgb = mix(t", num(index), ".rgb, texture(t", num(index), "_map, ",
                      coord_of(key, index), ", c.flatten.x).rgb, c.flatten.y);"});
        }
        if (index == 0 && (key.detail & kDetailMaterial) != 0U) { // (NOT ORIGINAL - rt_detail: the albedo's luminance)
            add(out, {"    t0.rgb *= mix(1.0, detail_m.a * 2.0, clamp(detail_w, 0.0, 1.0));"});
        }
        if (index == 0 && key.program != PixelProgram::kMaterial) { // (NOT ORIGINAL - the surface trails' cover)
            // (the pass colour c0 is the surface's colour too - x 2: 0.5 leaves the texel as it is - so the snow is set
            // over it: a lawn's green pass colour, a kerb's grey, don't tint the snow)
            trail_albedo(out, key, "t0.rgb", "    ", "max(c0.rgb * 2.0, vec3(0.04))");
        }
    }
}

// The fixed shaders (one pass).
void ps_fixed(std::string& out, const FragmentKey& trail_key)
{
    const PixelProgram program = trail_key.program;
    const uint8_t trails = trail_key.trails;
    switch (program) {
    case PixelProgram::kTextureVertexX4: // tex t0 / mul r0.rgb,t0,c0 / +mul_x2 r0.a,v0.a,t0.a / mul_x4 r0.rgb,v0,r0
        add(out, {"    r0.rgb = t0.rgb * c0.rgb;\n    r0.a = 2.0 * (v0.a * t0.a);\n    r0.rgb = 4.0 * (v0.rgb * r0.rgb);"});
        break;
    case PixelProgram::kVertexFactor: // mul_x2 r0.rgb,v0,c0 / +mov r0.a,v0.a
        if (trails != 0U) {
            // (NOT ORIGINAL - the surface trails on an untextured draw - the road's asphalt: its albedo is the pass
            // colour, covered as a texel is)
            add(out, {"    vec3 vf = c0.rgb;"});
            trail_albedo(out, trail_key, "vf", "    ");
            add(out, {"    r0.rgb = 2.0 * (v0.rgb * vf);\n    r0.a = v0.a;"});
        } else {
            add(out, {"    r0.rgb = 2.0 * (v0.rgb * c0.rgb);\n    r0.a = v0.a;"});
        }
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
    trail_albedo(out, key, "r0.rgb", "    "); // (NOT ORIGINAL - the surface trails: the passes' albedo covered before the light)
    add(out, {"    r0.rgb = 2.0 * (r0.rgb * v0.rgb);"}); // mul_x2 r0.rgb, r0, v0
}

void fragment_program(std::string& out, const FragmentKey& key, uint32_t sampled)
{
    if (key.trails != 0U) {
        trail_main(out, key); // (NOT ORIGINAL - the surface trails: before any branch on a pixel's values)
    }
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
        ps_fixed(out, key);
    }
    add(out, {"    vec4 colour = r0;"});
    if ((key.surface & kSurfaceLighting) != 0U && (key.surface & (kSurfaceCel | kSurfaceCelHull)) == 0U) {
        surface_finish(out, key); // (round M: the light model)
    }
    if ((key.surface & kSurfaceCelHull) != 0U) {
        add(out, {"    colour.rgb = cel_ink(clamp(colour.rgb, 0.0, 1.0));"}); // (added by cel shading: the outline)
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
    // (round F: in a linear frame the fog's colour is decoded as the fragment's is - fragment_linear_output - and the
    // two mixed as light)
    add(out, {"    vec3 fog_rgb = frame_linear() ? linear_from_display(u.fog_colour.rgb) : u.fog_colour.rgb;"});
    add(out, {"    colour.rgb = mix(fog_rgb, colour.rgb, clamp(fog_factor, 0.0, 1.0));"});
}

// (NOT ORIGINAL - round F, a linear, scene-referred frame: Backend::linear_phase) In a world view drawn into a float
// target the fragment's colour - the original's stages or pixel shader, the game's display-referred textures, vertex
// colours and constants, the relit stream's light - is decoded to linear light here, before the fog and the blend, so
// the frame holds light and every blend (alpha, additive glows, a multiplying decal) composes light. The game's
// combine is a product for its lit surfaces (texel x light x 2), and the display's power law is multiplicative, so this
// equals decoding each input; its additive stages and pixel-shader sums stay the original's material arithmetic.
// u.alpha_reference.y 0 (the 2D, an 8-bit target, Direct3D 9's parity): the colour as the original's.
void fragment_linear_output(std::string& out)
{
    add(out, {"    if (frame_linear()) {\n        colour.rgb = linear_from_display(colour.rgb);\n    }"});
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
    if (key.lighting && !fvf_pre_transformed(key.fvf)) {
        out += rt::kDisplayTransferGlsl; // (NOT ORIGINAL - round O: the one light encoded once - vertex_lighting)
    } else if ((key.surface & kVertexRelitColour) != 0U) {
        out += rt::kDisplayTransferGlsl; // (NOT ORIGINAL - round L: the store's light encoded as the colour word)
    }
    add(out, {"void main()\n{"});
    vertex_position(out, key);
    vertex_normal(out, key);
    vertex_lighting(out, key);
    if ((key.surface & ~kVertexRelitColour) != 0) { // (the relit colour alone hands nothing on)
        add(out, {"    v_surface_normal = normal;\n    v_surface_eye = eye.xyz;"}); // (NOT ORIGINAL - rt_pbr)
    }
    if ((key.surface & kVertexCelStream) != 0U) { // (added by cel shading: the stream handed on)
        // (round L: the light words decoded and x the frame's pre-exposure - the one place the draws apply it)
        // (round BAKEALB: the tint word decoded once - its bytes x rt_relit.h kRelitTintPerByte; the sun's share in alpha)
        add(out, {"    v_cel_light = vec4(stored_light(a_cel0) * u.light_store.x, a_cel_flags.r);\n    v_cel_tint = vec4(a_cel1.rgb * ",
                  tint_per_unorm(), ", a_cel1.a);\n"
                  "    v_cel_residual = vec4(stored_light(a_cel2) * u.light_store.x, a_cel_flags.a > 0.75 ? 1.0 : 0.0);\n"
                  "    v_cel_emit = vec4(stored_light(a_cel_emit) * u.light_store.x, a_cel_flags.b);"});
    }
    if ((key.surface & kVertexCelHull) != 0U) {
        // (added by cel shading) the outline's hull: each vertex pushed out along its normal as seen on the screen,
        // c.outline.x pixels whatever its distance (the normal's direction in pixels, then back to clip space)
        add(out, {"    {\n        vec4 hull_n = u.projection * vec4(normal, 0.0);\n"
                  "        vec2 hull_px = vec2(u.pixel.x > 0.0 ? hull_n.x / u.pixel.x : 0.0, u.pixel.y > 0.0 ? -hull_n.y / "
                  "u.pixel.y : 0.0);\n"
                  "        float hull_len = length(hull_px);\n"
                  "        if (hull_len > 1.0e-6) {\n"
                  "            gl_Position.xy += (hull_px / hull_len) * c.outline.x * 2.0 * u.pixel.xy * gl_Position.w;\n"
                  "        }\n    }"});
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
    // (added by cel shading: the sun's visibility per pixel - ray queries are GLSL 4.60's)
    const bool traced = (key.surface & kSurfaceCelShadow) != 0U; // (round W: the snow's reach is the map's, no ray query)
    std::string out = std::string(traced ? "#version 460\n" : "#version 450\n") + "// generated: " + describe(key) + "\n";
    if (traced) {
        out += "#extension GL_EXT_ray_query : require\n";
    }
    fragment_declarations(out, key, sampled);
    add(out, {"void main()\n{"});
    if ((key.light & (kLightInputs | kLightShare)) != 0U) {
        add(out, {"    light_find();"}); // (NOT ORIGINAL - round V / DB: before any branch - its derivatives)
        if (layer_write(key)) {
            add(out, {"    layer_write_m = brdf_material(vec3(0.0), vec3(0.04), 1.0);"}); // (round DECAL)
        }
        if ((key.light & kLightFront) != 0U) {
            add(out, {"    light_front_find(light_at);"}); // (round FL: the front layer, where the inputs' surface isn't)
        }
    }
    fragment_program(out, key, sampled);
    // (round M: a surface lit per pixel leaves the program as light - its alpha alone clamped)
    add(out, {surface_light_out(key) ? "    colour.a = clamp(colour.a, 0.0, 1.0);" : "    colour = clamp(colour, 0.0, 1.0);"});
    const bool water = key.water != 0U; // (NOT ORIGINAL - rt_water, water_shader.h)
    if (water) {
        add(out, {"    float water_alpha = colour.a;\n    colour = water_shade(colour, t0, ", coord_of(key, 0),
                  ", water_alpha);"});
    }
    if (!surface_light_out(key)) {
        fragment_linear_output(out);
    }
    if ((key.light & kLightShare) != 0U && key.surface == 0 && !water) {
        // (NOT ORIGINAL - round V: a colour lit already - its light not separated - takes the surface's light inputs as
        // shares of it, and the lamps on its traced albedo; kLightInputsGlsl)
        // (round DB: such a colour has no albedo apart from its light - the decals' layer laid over it as over a white
        // surface: x (a + rgb), the factor the layer makes of an albedo of 1)
        add(out, {"    if (frame_linear()) {\n        vec4 share_decals = light_decals_now();\n"
                  "        vec3 share_layer = share_decals.a + share_decals.rgb;\n"
                  "        colour.rgb = colour.rgb * share_layer * light_share(light_in_now()) + light_lamps_on_now() * share_layer;\n"
                  "    }"});
        if ((key.light & kLightGuides) != 0U) {
            add(out, {"    guide_albedo = colour.rgb;"}); // (its light not separated: the colour lit, as before round V)
        }
    }
    fragment_fog(out, key);
    fragment_alpha_test(out, key);
    if ((key.light & kLightGuides) != 0U && (key.light & (kLightInputs | kLightShare)) != 0U) {
        add(out, {"    light_write_guides();"}); // (round V: once kept)
    }
    if ((key.light & kLightDepthOnly) != 0U && (key.light & kLightDepthHalf) != 0U) {
        // (NOT ORIGINAL - round V: a blended draw's prepass keeps only its mostly opaque part - its translucent edges
        // don't hide what was drawn behind them; vulkan_surface_light.h)
        add(out, {"    if (colour.a < 0.5) discard;"});
    }
    if (layer_write(key)) {
        // (NOT ORIGINAL - round DECAL: laid where it lies on the surface the inputs were made for - its material over the
        // decals' inputs by its alpha, DBuffer's blend: albedo' = albedo x (1 - a) + its albedo x a; roughness and F0
        // the same - vulkan_surface_light.h; nothing elsewhere: it is drawn there as before)
        add(out, {"    if (!light_at) discard;\n"
                  "    float layer_a = clamp(colour.a, 0.0, 1.0);\n"
                  "    o_colour = vec4(min(layer_write_m.albedo, vec3(1.0)) * layer_a, 1.0 - layer_a);\n"
                  "    o_layer = vec4(clamp(layer_write_m.roughness, 0.0, 1.0) * layer_a, "
                  "clamp(dot(layer_write_m.f0, vec3(1.0 / 3.0)), 0.0, 1.0) * layer_a, 0.0, 1.0 - layer_a);\n}"});
        return out;
    }
    if (layer_laid(key)) {
        // (NOT ORIGINAL - round DECAL: where it lies on the surface the inputs were made for it was laid into that
        // surface's material - kLightLayerWrite - and is drawn as that surface; not a second time over it)
        add(out, {"    if (light_at) discard;"});
    }
    if (water) {
        add(out, {"    colour.a = water_alpha;"});
    }
    if (key.motion != 0U) {
        // the move to where the point was the frame before, in 0 - 1 of the viewport (Direct3D's y up: down on screen);
        // a point behind the camera then: none
        add(out, {"    vec2 now = v_motion_now.xy / v_motion_now.w;"});
        add(out, {"    vec2 before = v_motion_before.w > 0.0 ? v_motion_before.xy / v_motion_before.w : now;"});
        add(out, {"    o_motion = vec4((before - now) * vec2(0.5, -0.5), colour.a, 1.0);"}); // (z: a blended draw's share)
    }
    // (NOT ORIGINAL - round RX: pre-exposed light past the 16-bit target's range written as its largest finite value -
    // UE5's scene colour; rt_daylight.h kFrameHalfMost)
    add(out, {"    o_colour = vec4(min(colour.rgb, vec3(65504.0)), colour.a);\n}"});
    return out;
}

} // namespace render::vulkan
