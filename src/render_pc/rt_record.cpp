// NOT ORIGINAL - ray tracing's frame as data: see rt_record.h.

#include "render_pc/rt_record.h"
#include "platform/frame_profile.h"
#include "render_pc/mesh_smoothing.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <ratio>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/maths/mat4.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/light_sets.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/render_camera.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/rt_checks.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_light_census.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/rt_apart.h"
#include "engine/gfx/rt_poses.h"
#include "engine/gfx/scatter.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"
#include "engine/gfx/rt_surface_class.h"
#include "engine/gfx/rt_vertex_attributes.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/light_cards.h"
#include "engine/gfx/texel_decode.h"
#include "engine/memory/arena_alloc.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "render_pc/bitmap_texture.h"
#include "render_pc/geom.h"
#include "render_pc/mesh_list.h"
#include "render_pc/mesh_pass.h"
#include "render_pc/pc_scene.h"
#include "render_pc/relit_bake.h"
#include "render_pc/rt_apart.h"
#include "render_pc/rt_relit_world.h"
#include "render_pc/scatter_world.h"
#include "render_pc/sub_mesh.h"
#include "render_pc/vertex_buffers.h"

namespace render::rt_record {

namespace {

constexpr uint32_t kMeshShown = 0x40;           // a mesh's flags: drawn (the scenes' passes test it)
constexpr uint32_t kMeshHidden = 0x210;         // ... and not with these
constexpr uint32_t kVertexBufferShare = 4;      // a vertex buffer's flags: a share of an arena's buffer
constexpr uint32_t kPassTransparent = 0x40;     // a material's first pass: drawn with the transparent meshes
constexpr uint32_t kPassUvWibble = 0x1;         // a pass's flags: its texture matrix from its UV wibble (pc_material.cpp)
constexpr uint32_t kPassTextureAnim = 0x800;    // ... its texture from the material's texture animation
constexpr uint32_t kPrimitiveList = 4;          // a mesh's primitive type: a triangle list
constexpr uint32_t kPrimitiveStrip = 5;         // ... a strip
constexpr uint32_t kFormatIndex32 = 102;        // an index buffer's format: 32-bit indices
constexpr uint32_t kPositionMask = 0x400E;      // a vertex format's position bits
constexpr uint32_t kPositionXyz = 0x2;          // three floats (and up to five blend weights after: 0x6-0xE)
constexpr uint32_t kPositionXyzB5 = 0xE;
constexpr int kNoBone = -1;                     // a mesh's one bone: none
constexpr int kMaxDrawnBones = 0x3A;            // Singleton::render draws this many bones at most
constexpr uint8_t kNoBoneByte = 0xFF;           // a mesh's bone byte: none
constexpr float kDoubled = 2.0F;                // the passes' colours: 0.5 leaves the texture as it is
constexpr uint32_t kLowHalf = 0xFFFF;           // a pass's address modes: u, then v in the high half
constexpr int kHalfBits = 16;
constexpr float kPerByteLight = 1.0F / 128.0F;  // a scene light's byte (times its intensity): 128 is 1
constexpr float kPerByte = 1.0F / 255.0F;       // a dynamic light's
constexpr uint32_t kByteMax = 255;              // (round PT-B) a colour byte at its most
constexpr int kBitsPerByte = 8;
constexpr uint32_t kByteMask = 0xFF;
// The baked sun's fit (engine/gfx/rt_sun.h)
constexpr uint32_t kColourBytes = 4;
constexpr float kLumaRed = 0.299F;              // a colour's brightness (Rec. 601 luma)
constexpr float kLumaGreen = 0.587F;
constexpr float kLumaBlue = 0.114F;
constexpr float kThirdOf = 1.0F / 3.0F;                 // (bake_samples: a triangle's centre)
constexpr uint64_t kSampleHashBasis = 0xCBF29CE484222325ULL; // (bake_samples: FNV-1a's, over the centre's units)
constexpr uint64_t kSampleHashPrime = 0x100000001B3ULL;
constexpr unsigned kSampleHashFold = 29U;
constexpr std::size_t kFitTriangles = 40000;       // triangles looked at for the fit (the upward ones sampled)
constexpr float kMinUp = 0.7F;                  // the samples' triangles face up (or down) at least this much
constexpr uint64_t kFnvBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;
constexpr uint32_t kFnvPrime32 = 16777619U;     // (round PG: a draw's layout key)
constexpr std::size_t kMaxSceneLights = 31;     // update_all_lights' limit
constexpr uint32_t kPassEnvironment = 0x8;      // a material pass's flags: environment mapped (pc_material.cpp)
constexpr uint32_t kPassCount = 4;              // a material's passes, at most
constexpr uint32_t kPassTextureAlpha = 0x1000;  // a pass's flags: its alpha from the texture only (pixel_shaders.cpp)
constexpr uint32_t kBlendAdd = 1;               // the blend modes (engine/gfx/mesh_formats.h has the rest): add
constexpr uint32_t kBlendFixedAdd = 2;          // ... add by the fixed alpha
// (round P10 - objects relit) an object drawn with its own colours is placed (relit where it stands) once its transform
// has stayed the same this many records in a row (half a second at 60 frames): an object the game moves every few
// frames isn't placed and let go of in turn (Boston: one alternated every frame - its emitters scanned again each time)
constexpr uint64_t kObjectStillFrames = 30;

struct MeshEntry {
    rt::MeshId id = 0;
    rt::Geometry geometry;
    const VertexBufferData* source = nullptr; // the vertex buffer the geometry is in
    uint64_t frame = 0;                       // the frame it was last recorded in
};

struct MaterialEntry {
    rt::MaterialId id = 0;
    rt::Material material;
    uint64_t frame = 0;
};

struct State {
    std::unordered_map<const MeshRef*, MeshEntry> meshes;
    // (changed by the first frame) a new world's work - its structures settled, the baked sun fitted - done in its first
    // frame: this frame's world meshes, the loaded scenes' key (scene_key), the key last settled and the one last fitted
    std::vector<const MeshRef*> world;
    uint64_t scene_key = 0;
    uint64_t settled_key = 0;
    uint64_t fitted_key = 0;
    uint64_t fits = 0;
    // (the offline relit data) the baked sun the level's data holds (rt_relit_world::new_world), published as the fit
    rt::BakedSun level_sun;
    uint64_t emitters_key = 0; // (added by the night lighting) the loaded scenes' key the emitters were found for
    uint64_t emitter_sets = 0;
    std::size_t game_lights = 0;    // (round SL, reported) the level's light nodes' lights last frame
    std::size_t game_lights_on = 0; // ... and how many of them were on
    // the first frame's time (from its record to the next frame's), logged at the next record_frame
    std::chrono::steady_clock::time_point first_frame_start;
    double first_frame_work_ms = 0.0;
    bool first_frame_timed = false;
    std::unordered_map<const MaterialRef*, MaterialEntry> materials;
    // (round PERF4) each texture's texels' median luminance (lamp_texel_code_of; negative: unreadable), decoded once a
    // level - every mesh with a lamp drawn in a texture decoded its texture again when the lamps first switched on (BO
    // at dusk: a 1 - 2 s stall of the main loop)
    std::unordered_map<const void*, float> lamp_medians;
    std::unordered_set<const VertexBufferData*> written; // since the last record
    // (round PT-B) each vertex buffer's least vertex alpha (least_vertex_alpha), read when first asked after the buffer
    // was last written (its colours too)
    std::unordered_map<const VertexBufferData*, float> vertex_alpha;
    // (path tracing, stage 3 - engine/gfx/rt_poses.h) the meshes skinned on the processor: each instance's pose of the
    // frame, skinned here with its bones (the draw's code and palette) - the meshes' own buffers hold only the last
    // draw's pose of the frame before, and are never registered
    rt::PoseStore poses;
    // (round PG) what is drawn apart (rt_apart.h): the particles' and billboards' triangles of the frame before, as
    // per-frame meshes; and the clutter's items as instances of a mesh per part (scatter.h "the clutter in the rays")
    rt::ApartStore apart;
    struct ClutterRays {
        GpuVertexBuffer* buffer = nullptr; // every part's triangles (scatter::MeshVertex), one after another
        std::array<rt::MeshId, scatter::kRayParts> meshes{};
        std::array<std::array<rt::MaterialId, scatter::kRayShades>, scatter::kRayParts> materials{};
        bool tried = false; // (made once a device; not again after a failure)
        std::vector<uint32_t> items; // (the frame's, reused)
        std::array<std::vector<uint32_t>, std::size_t{scatter::kRayParts} * scatter::kRayShades> runs; // (by part, shade)
        // each item's transform, made once a scatter (the items never move)
        std::shared_ptr<const scatter::Scatter> placed_for;
        std::vector<std::array<std::array<float, 4>, 3>> placed;
    } clutter;
    std::array<float, 3> camera{}; // the first viewport's camera, last drawn (the clutter's range is measured from it)
    bool camera_known = false;
    uint64_t frame = 0;
    bool recorded = false; // record_frame ran this frame (begin_frame not yet)
    bool begun = false;    // begin_frame called this frame
    bool world_passes = false;
    int colour_writes = 0;
    int colour_writes_told = 0; // (of them, the game's own colours the relit mode is told of: ColourWrite::Relit::kTell)
    bool relit_idle = true; // the relit mode off and told so
    // (the objects' shadows) each lit instance's lights as its last draw sent them (object_lit), and the record's frame
    // then
    struct Lit {
        rt::ObjectLight light;
        uint64_t frame = 0;
    };
    std::unordered_map<const Singleton*, Lit> object_lights;
    // (round P10 - objects relit) the frame's meshes of objects drawn with their own baked colours (instance_drawn_lit
    // false; not skinned, not drawn a bone at a time): each with its instance and world transform - and each instance's
    // transform with the frame it last changed in (an object standing still since the record before is placed)
    struct Placement {
        const Singleton* instance = nullptr;
        const MeshRef* mesh = nullptr;
        maths::Mat4 world;
    };
    std::vector<Placement> placed;
    struct Still {
        maths::Mat4 transform;
        uint64_t changed = 0;
        uint64_t seen = 0;
    };
    std::unordered_map<const Singleton*, Still> still;
    // (the census - object_census) this frame's instances by how they are drawn
    struct ObjectCounts {
        std::size_t lit = 0;      // lit by the game's lights: vertex colours its paint
        std::size_t own = 0;      // drawn with its own colours ...
        std::size_t skinned = 0;  // ... of them skinned or drawn a bone at a time (animated)
        std::size_t moving = 0;   // ... moved since the record before
        std::size_t lit_meshes = 0;
        std::size_t own_meshes = 0;
    } counts;
};

State& state()
{
    static core::NoDestroy<State> made;
    return made.get();
}

rt::RayTracing* backend() { return rt::ray_tracing(); }

bool same_geometry(const rt::Geometry& a, const rt::Geometry& b)
{
    return a.vertices == b.vertices && a.vertex_offset == b.vertex_offset && a.vertex_stride == b.vertex_stride &&
           a.vertex_count == b.vertex_count && a.position_offset == b.position_offset && a.indices == b.indices &&
           a.index_offset == b.index_offset && a.index_count == b.index_count && a.indices_32bit == b.indices_32bit &&
           a.strip == b.strip && a.dynamic == b.dynamic && a.vertex_buffer_bytes == b.vertex_buffer_bytes &&
           a.index_buffer_bytes == b.index_buffer_bytes && a.cpu_indices == b.cpu_indices &&
           a.uv_offset == b.uv_offset && a.uv_format == b.uv_format && a.colour_offset == b.colour_offset &&
           a.colour_format == b.colour_format && a.uv_sets == b.uv_sets && a.normal_offset == b.normal_offset &&
           a.normal_format == b.normal_format;
}

bool same_material(const rt::Material& a, const rt::Material& b)
{
    return a.texture == b.texture && a.uv_set == b.uv_set && a.blend == b.blend && a.alpha_ref == b.alpha_ref &&
           a.colour == b.colour && a.flags == b.flags && a.address_u == b.address_u && a.address_v == b.address_v &&
           a.filter == b.filter && a.reflectivity == b.reflectivity && a.environment_pass == b.environment_pass &&
           a.reflection_class == b.reflection_class && a.reflection_mask == b.reflection_mask &&
           a.mask_passes == b.mask_passes && a.layers == b.layers && a.least_texel_alpha == b.least_texel_alpha;
}

// The vertex buffer a mesh was last drawn from (MeshRef::render: the current one, then the next is current).
VertexBuffer* last_drawn_buffer(const MeshRef* mesh)
{
    const uint32_t count = mesh->vertex_buffer_count();
    const uint32_t current = mesh->current_vertex_buffer_;
    const uint32_t index = count > 1 ? (current + count - 1) % count : current;
    return mesh->vertex_store(index);
}

// (added for the skinned meshes' attributes) A mesh's own normal / colour / uv offsets against where its vertex format
// puts them (rt_vertex_attributes.h: the format is what the buffer is drawn with, and what the geometry table reads by),
// logged once for each format, stride and offsets that disagree. The skater's skinned meshes always do - their
// offsets are the scene file's packed vertex's -; any other mesh would be a surprise worth knowing about.
// A traced attribute's offset as the log shows it (-1: none).
int shown(uint32_t format, uint32_t offset) { return format != rt::kAttributeNone ? static_cast<int>(offset) : -1; }

void report_offsets(const MeshRef* mesh, const rt::Geometry& geometry)
{
    const auto own = [](uint8_t offset, uint32_t format, uint32_t derived) {
        return format != rt::kAttributeNone ? uint32_t{offset} != derived : offset != 0U;
    };
    if (!own(mesh->normal_offset(), geometry.normal_format, geometry.normal_offset) &&
        !own(mesh->color_offset(), geometry.colour_format, geometry.colour_offset) &&
        !own(mesh->uv_offset(), geometry.uv_format, geometry.uv_offset)) {
        return;
    }
    const uint64_t key = (uint64_t{mesh->fvf()} << 32U) | (uint64_t{mesh->stride()} << 24U) |
                         (uint64_t{mesh->normal_offset()} << 16U) | (uint64_t{mesh->color_offset()} << 8U) |
                         uint64_t{mesh->uv_offset()};
    static core::NoDestroy<std::unordered_set<uint64_t>> logged;
    if (!logged.get().insert(key).second) {
        return;
    }
    get_log("rt: a mesh's own attribute offsets (normal %u colour %u uv %u) aren't its vertex format's %X, stride %u "
            "(normal %d colour %d uv %d, -1: none) - the format's used%s",
            unsigned{mesh->normal_offset()}, unsigned{mesh->color_offset()}, unsigned{mesh->uv_offset()}, mesh->fvf(),
            unsigned{mesh->stride()}, shown(geometry.normal_format, geometry.normal_offset),
            shown(geometry.colour_format, geometry.colour_offset), shown(geometry.uv_format, geometry.uv_offset),
            mesh->per_vertex_data() != nullptr ? " (skinned: the offsets are its packed vertex's, as expected)" : "");
}

// A mesh's triangles where the card has them (its first level of detail), or false: nothing to trace (no indices, no
// buffer on the card, not a position of three floats, neither a strip nor a list).
bool geometry_of(const MeshRef* mesh, rt::Geometry* out, const VertexBufferData** source)
{
    const uint32_t index_count = core::at(mesh->index_counts(), 0);
    const IdxStore* ib = mesh->index_store(0);
    const uint32_t primitive = mesh->primitive_type();
    const uint32_t position = mesh->fvf() & kPositionMask;
    const bool xyz = position >= kPositionXyz && position <= kPositionXyzB5 && (position % 2) == 0;
    if (index_count == 0 || ib == nullptr || ib->buffer() == nullptr || !xyz ||
        (primitive != kPrimitiveList && primitive != kPrimitiveStrip)) {
        return false;
    }
    const VertexBuffer* vb = last_drawn_buffer(mesh);
    if (vb == nullptr || vb->buffer() == nullptr) {
        return false;
    }
    *out = {
        .vertices = vb->buffer(),
        .vertex_offset = vb->arena_offset(),
        .vertex_stride = mesh->stride(),
        .vertex_count = mesh->vertex_count(),
        .position_offset = 0,
        .indices = ib->buffer(),
        .index_offset = 0,
        .index_count = index_count,
        .indices_32bit = ib->format() == kFormatIndex32,
        .strip = primitive == kPrimitiveStrip,
        .dynamic = mesh->per_vertex_data() != nullptr,
        .vertex_buffer_bytes = ((vb->flags & kVertexBufferShare) != 0U ? vb->arena_offset() : 0U) + vb->size(),
        .index_buffer_bytes = ib->size(),
        .cpu_indices = ib->indices(),
    };
    // (every attribute where the vertex format the buffer is drawn with puts it - not the mesh's own offsets, which for
    // a skinned mesh are its packed vertex's: rt_vertex_attributes.h)
    if (!rt::set_vertex_attributes(mesh->fvf(), out)) {
        return false;
    }
    *source = vb;
    return true;
}

// The mesh's id - registered, given again or reported changed as needed - or 0 (nothing to trace).
rt::MeshId mesh_id(const MeshRef* mesh, rt::RayTracing& rt)
{
    State& st = state();
    rt::Geometry geometry;
    const VertexBufferData* source = nullptr;
    const auto found = st.meshes.find(mesh);
    if (!geometry_of(mesh, &geometry, &source)) {
        if (found != st.meshes.end()) {
            rt.remove_mesh(found->second.id);
            st.meshes.erase(found);
        }
        return 0;
    }
    if (found == st.meshes.end()) {
        const MeshEntry entry{.id = rt::next_mesh_id(), .geometry = geometry, .source = source, .frame = st.frame};
        rt.add_mesh(entry.id, geometry);
        st.meshes.emplace(mesh, entry);
        report_offsets(mesh, geometry);
        return entry.id;
    }
    MeshEntry& entry = found->second;
    if (entry.frame == st.frame) { // (an instance of it recorded already this frame)
        return entry.id;
    }
    entry.frame = st.frame;
    if (!same_geometry(entry.geometry, geometry)) {
        entry.geometry = geometry;
        entry.source = source;
        rt.update_mesh(entry.id, geometry);
    } else if (st.written.contains(source)) {
        rt.mesh_changed(entry.id);
    }
    return entry.id;
}

// A material pass's address mode (the game's numbering) as the sampler's (gpu_value::kAddress*); an unknown one
// leaves the sampler as it was: wrap, the default.
uint32_t sampler_address(uint32_t mode)
{
    switch (mode) {
    case 1:
        return gpu_value::kAddressClamp;
    case 2:
        return renderer().border_addressing_supported() ? gpu_value::kAddressBorder : gpu_value::kAddressClamp;
    default:
        return gpu_value::kAddressWrap;
    }
}

// How much of a material's environment-mapped pass `env` the game shows at a pixel, as its generated pixel shader
// composes the passes (render_pc/pixel_shaders.cpp generated_pixel_shader: the first pass in r0, each later one
// blended into r0 by its mode; a pass's alpha is its texel's alpha - the pass's flag 0x1000 - or the vertex alpha x the
// texel's, doubled). The environment colour E enters r0 by the pass's own mode and is then scaled by every later pass's:
//   the environment pass's own mode (after the first pass): 1 add, 5 blend (by E's alpha, sampled along the reflection:
//     taken as 1), 2 / 6 their fixed alpha; 12 / 13 lerped in by the pass before's alpha (a, 1 - a: a mask pass, and
//     reflection_mask - the glass's own colour put back); the others draw no environment colour (3 / 4 subtract it,
//     7 - 10 multiply or brighten by its alpha alone, 0 and 11 do nothing to r0): not reflective;
//   each later pass: 5 lerps r0 toward it by its alpha (E x 1 - a: a mask pass - the wall over the windows), 6 by its
//     fixed alpha (x 1 - c.a), 7 / 8 multiply r0 by its alpha (x a: a mask pass) / its fixed alpha, 12 / 13 lerp by the
//     pass before's alpha (x 1 - a / x a: a mask pass, unless the pass before is the environment pass itself - its
//     alpha along the reflection, taken as 1); the rest (adds, subtracts, brightens) leave E as it is.
// The constant factors go into the reflectivity, the per-pixel ones into mask_passes (rt::Material::MaskPass). False
// when the pass draws no environment colour.
struct EnvironmentShare {
    float factor = 1.0F;
    std::array<rt::Material::MaskPass, rt::Material::kMaskPasses> masks{};
    uint32_t mask_count = 0;
    bool lerped = false;  // blend modes 12 / 13: lerped in by the pass before's alpha (reflection_mask)
    bool covered = false; // a later pass blended over it by its texture's alpha (the windows in a wall's texture)
};

// A mask pass for `pass` of the material, `kind` kMaskClear / kMaskOpaque.
rt::Material::MaskPass mask_pass(const MaterialRef* material, uint32_t pass, uint32_t kind)
{
    const TextureImage* texture = core::at(material->textures(), pass);
    const uint32_t address = core::at(material->address_modes(), pass);
    return {.texture = texture != nullptr ? texture->texture : nullptr,
            .kind = kind,
            .uv_set = pass,
            .address_u = sampler_address(address & kLowHalf),
            .address_v = sampler_address(address >> kHalfBits),
            .texture_alpha = (core::at(material->pass_flags(), pass) & kPassTextureAlpha) != 0U};
}

void add_mask(EnvironmentShare* share, const rt::Material::MaskPass& mask)
{
    for (uint32_t i = 0; i < share->mask_count; ++i) {
        if (core::at(share->masks, i) == mask) {
            return; // (the same factor twice: a / 1 - a of one pass is 0 or 1 at full-strength texels either way)
        }
    }
    if (share->mask_count < rt::Material::kMaskPasses) {
        core::at(share->masks, share->mask_count++) = mask;
    }
}

uint32_t blend_mode(const MaterialRef* material, uint32_t pass)
{
    return core::at(material->blend_modes(), pass) & static_cast<uint32_t>(kBlendModeMask);
}

bool environment_share(const MaterialRef* material, uint32_t env, EnvironmentShare* share)
{
    const uint32_t passes = std::min(material->pass_count(), kPassCount);
    const auto fixed_alpha = [&](uint32_t pass) {
        return std::clamp(core::at(core::at(material->pass_color(), pass), 3), 0.0F, 1.0F);
    };
    if (env > 0) {
        switch (blend_mode(material, env)) {
        case kBlendAdd:
        case static_cast<uint32_t>(kBlendModeAlpha):
            break;
        case kBlendFixedAdd:
        case static_cast<uint32_t>(kBlendModeFixBlend):
            share->factor *= fixed_alpha(env);
            break;
        case static_cast<uint32_t>(kBlendModeLerpPrevious):
            add_mask(share, mask_pass(material, env - 1, rt::kMaskOpaque));
            share->lerped = true;
            break;
        case static_cast<uint32_t>(kBlendModeLerpFromPrevious):
            add_mask(share, mask_pass(material, env - 1, rt::kMaskClear));
            share->lerped = true;
            break;
        default:
            return false;
        }
    }
    for (uint32_t pass = env + 1; pass < passes; ++pass) {
        switch (blend_mode(material, pass)) {
        case static_cast<uint32_t>(kBlendModeAlpha):
            add_mask(share, mask_pass(material, pass, rt::kMaskClear));
            share->covered = true;
            break;
        case static_cast<uint32_t>(kBlendModeFixBlend):
            share->factor *= 1.0F - fixed_alpha(pass);
            break;
        case static_cast<uint32_t>(kBlendModeModulate):
            add_mask(share, mask_pass(material, pass, rt::kMaskOpaque));
            break;
        case static_cast<uint32_t>(kBlendModeFixModulate):
            share->factor *= fixed_alpha(pass);
            break;
        case static_cast<uint32_t>(kBlendModeLerpPrevious):
            if (pass - 1 != env) {
                add_mask(share, mask_pass(material, pass - 1, rt::kMaskClear));
                share->covered = true;
            }
            break;
        case static_cast<uint32_t>(kBlendModeLerpFromPrevious):
            if (pass - 1 != env) {
                add_mask(share, mask_pass(material, pass - 1, rt::kMaskOpaque));
            }
            break;
        default:
            break;
        }
    }
    return share->factor > 0.0F;
}

// What kind of reflective surface a material is, from how the game draws it (its environment-mapped pass `env`) - read
// off the levels' own materials ([debug] rt_reflection_materials=1 logs them; AU, BA, BO):
// - water: a pass with a UV wibble or a texture animation (pass flags 0x1 / 0x800, or the material's texture
//   animation) - the game's moving water textures;
// - glass: the environment pass lerped in by the pass before's alpha (blend modes 12 / 13 - the reflection painted into
//   the clear parts of a texture: AU's and BA's windows in their wall textures, 8A6663D9 on the user's glass building),
//   or under a later pass blended over it by its alpha (a wall texture whose clear parts are its windows, the
//   environment map drawn first: BO's buildings), or a first pass that is seen through (the transparent flag 0x40, or
//   an alpha / fixed blend: panes over a frame, and the single-pass sheens drawn over windows);
// - shiny: the rest - an opaque surface with a reflection added or blended over it (metal, paint, polished floors).
void classify_reflective(const MaterialRef* material, const EnvironmentShare& share, rt::Material* out)
{
    const uint32_t passes = std::min(material->pass_count(), kPassCount);
    bool moving = material->texture_anim() != nullptr;
    for (uint32_t pass = 0; pass < passes; ++pass) {
        moving = moving || (core::at(material->pass_flags(), pass) & (kPassUvWibble | kPassTextureAnim)) != 0U ||
                 core::at(material->uv_wibble(), pass) != nullptr;
    }
    const uint32_t env_mode = blend_mode(material, out->environment_pass);
    const uint32_t first_mode = blend_mode(material, 0);
    const bool see_through = (core::at(material->pass_flags(), 0) & kPassTransparent) != 0U ||
                             first_mode == static_cast<uint32_t>(kBlendModeAlpha) ||
                             first_mode == static_cast<uint32_t>(kBlendModeFixBlend);
    if (moving) {
        out->reflection_class = rt::kReflectWater;
    } else if (share.lerped || share.covered || see_through) {
        out->reflection_class = rt::kReflectGlass;
    } else {
        out->reflection_class = rt::kReflectShiny;
    }
    if (share.lerped) {
        out->reflection_mask = env_mode == static_cast<uint32_t>(kBlendModeLerpFromPrevious) ? rt::kReflectWhereBaseClear
                                                                                            : rt::kReflectWhereBaseOpaque;
    }
    out->mask_passes = share.masks;
}

// How strongly a material mirrors its surroundings (R5's reflections): its first environment-mapped pass with a
// texture (the game's shiny surfaces) - the brightest channel of the pass's colour (doubled, as drawn), times the
// constant share of it the game shows (environment_share) - and where (mask_passes); or none.
void reflectivity_of(const MaterialRef* material, rt::Material* out)
{
    const uint32_t passes = std::min(material->pass_count(), kPassCount);
    for (uint32_t pass = 0; pass < passes; ++pass) {
        if ((core::at(material->pass_flags(), pass) & kPassEnvironment) == 0U ||
            core::at(material->textures(), pass) == nullptr) {
            continue;
        }
        EnvironmentShare share;
        if (!environment_share(material, pass, &share)) {
            return; // (the pass draws no environment colour: nothing mirrors)
        }
        const std::array<float, 4>& colour = core::at(material->pass_color(), pass);
        const float strength = std::max({core::at(colour, 0), core::at(colour, 1), core::at(colour, 2)}) * kDoubled *
                               share.factor;
        out->reflectivity = std::isfinite(strength) ? std::clamp(strength, 0.0F, 1.0F) : 0.0F;
        if (out->reflectivity <= 0.0F) {
            out->reflectivity = 0.0F;
            return;
        }
        out->environment_pass = pass;
        classify_reflective(material, share, out);
        return;
    }
}

// A pass environment-mapped with a texture.
bool has_environment_pass(const MaterialRef* material)
{
    const uint32_t passes = std::min(material->pass_count(), kPassCount);
    for (uint32_t pass = 0; pass < passes; ++pass) {
        if ((core::at(material->pass_flags(), pass) & kPassEnvironment) != 0U &&
            core::at(material->textures(), pass) != nullptr) {
            return true;
        }
    }
    return false;
}

// (added for the cut-out shadows) A material drawn blended whose texels the game also cuts out by an alpha test: its
// first pass alpha-blended (blend mode 5) with an alpha reference above the lowest (the game's 1 of 255 passes every
// texel drawn) - a cannon's spoked wheels, leaves, railings drawn with the transparent meshes. Not water or other moving
// textures (a UV wibble or a texture animation: they cast nothing) or anything environment mapped (glass).
// Such a surface blocks the rays (the shadows) where its texel's alpha is at least kCutoutAlpha (or its reference, if
// higher): its solid parts, not its soft edges or a pane's tint.
constexpr float kCutoutAlpha = 0.5F;
bool cutout_material(const MaterialRef* material)
{
    if (material->alpha_ref() <= 1U || blend_mode(material, 0) != static_cast<uint32_t>(kBlendModeAlpha) ||
        material->texture_anim() != nullptr || has_environment_pass(material)) {
        return false;
    }
    const uint32_t passes = std::min(material->pass_count(), kPassCount);
    for (uint32_t pass = 0; pass < passes; ++pass) {
        if ((core::at(material->pass_flags(), pass) & (kPassUvWibble | kPassTextureAnim)) != 0U ||
            core::at(material->uv_wibble(), pass) != nullptr) {
            return false;
        }
    }
    return true;
}

// (round BIAS) The later passes composed over the first into the surface's colour (rt::Material::layers - the game's
// generated pixel shader's blends, pixel_shaders.cpp): each pass's texture, mode, coordinates, alpha source and colour;
// the environment-mapped passes black (their colour is the reflection). Modes that compose no colour (0, 11 - the
// specular's vertex alpha, and the rest) aren't kept.
void layers_of(const MaterialRef* material, rt::Material* out)
{
    const uint32_t passes = std::min(material->pass_count(), kPassCount);
    for (uint32_t pass = 1; pass < passes && pass <= rt::Material::kLayerPasses; ++pass) {
        const uint32_t mode = blend_mode(material, pass);
        const bool composes = (mode >= 1U && mode <= static_cast<uint32_t>(kBlendModeFixBrighten)) ||
                              mode == static_cast<uint32_t>(kBlendModeLerpPrevious) ||
                              mode == static_cast<uint32_t>(kBlendModeLerpFromPrevious);
        if (!composes) {
            continue;
        }
        // (an environment-mapped pass is a painted reflection: composed as black - its alpha kept for the blends -, as
        // the frame's shading takes it: ff_shaders.cpp environment_texel_black)
        const float paint = (core::at(material->pass_flags(), pass) & kPassEnvironment) != 0U ? 0.0F : kDoubled;
        const TextureImage* texture = core::at(material->textures(), pass);
        const uint32_t address = core::at(material->address_modes(), pass);
        const std::array<float, 4>& colour = core::at(material->pass_color(), pass);
        core::at(out->layers, pass - 1) = {
            .texture = texture != nullptr ? texture->texture : nullptr,
            .mode = mode,
            .uv_set = pass,
            .address_u = sampler_address(address & kLowHalf),
            .address_v = sampler_address(address >> kHalfBits),
            .texture_alpha = (core::at(material->pass_flags(), pass) & kPassTextureAlpha) != 0U,
            .colour = {core::at(colour, 0) * paint, core::at(colour, 1) * paint, core::at(colour, 2) * paint,
                       std::clamp(core::at(colour, 3), 0.0F, 1.0F)}};
    }
}

// A material as a hit shader reads it: its first pass, and (round BIAS) the later ones composed over it.
rt::Material material_of(const MaterialRef* material)
{
    rt::Material out;
    const TextureImage* texture = core::at(material->textures(), 0);
    out.texture = texture != nullptr ? texture->texture : nullptr;
    out.uv_set = 0;
    out.blend = core::at(material->blend_modes(), 0);
    out.alpha_ref = static_cast<float>(material->alpha_ref()) * kPerByte;
    const std::array<float, 4>& colour = core::at(material->pass_color(), 0);
    // (doubled: the passes' 0.5 leaves the texture as it is; the fourth value is the pass's fixed alpha, as stored)
    out.colour = {core::at(colour, 0) * kDoubled, core::at(colour, 1) * kDoubled, core::at(colour, 2) * kDoubled,
                  core::at(colour, 3)};
    // (the pass's address modes, u in the low half and v in the high, as render_state.cpp sends them: 0 wrap, 1 clamp,
    // 2 border where the device has it - else clamp; the filter linear, as the game's minification filter is)
    const uint32_t address = core::at(material->address_modes(), 0);
    out.address_u = sampler_address(address & kLowHalf);
    out.address_v = sampler_address(address >> kHalfBits);
    out.filter = gpu_value::kFilterLinear;
    if ((core::at(material->pass_flags(), 0) & kPassTransparent) != 0U) {
        out.flags |= rt::kTransparent;
        if (cutout_material(material)) {
            // (added for the cut-out shadows) blended, but its texels cut out by its alpha test: the ray-traced effects
            // take it by its alpha, as an alpha-tested surface - where it is mostly solid
            out.flags |= rt::kAlphaTested;
            out.alpha_ref = std::max(out.alpha_ref, kCutoutAlpha);
            // (its alpha the texel's: an alpha blend has no use for the pass's fixed alpha, which the alpha test
            // would multiply in - often 0)
            std::get<3>(out.colour) = 1.0F;
        }
    } else if (material->alpha_ref() != 0U) {
        out.flags |= rt::kAlphaTested;
    }
    if ((core::at(material->pass_flags(), 0) & kPassTextureAlpha) != 0U) {
        out.flags |= rt::kTextureAlpha; // (path tracing, stage 3: a blended surface's coverage - its texel's alpha alone)
    }
    // (round PT-B - rt_surface_class.h) a blended surface's texels' least alpha (untextured: 1; not known: 0)
    if ((out.flags & rt::kTransparent) != 0U) {
        const rt::RayTracing* rt = rt::ray_tracing();
        const rt::TexelAlpha texels = out.texture != nullptr && rt != nullptr ? rt->texture_alpha(out.texture) : rt::TexelAlpha{};
        out.least_texel_alpha = out.texture == nullptr ? 1.0F : texels.known ? static_cast<float>(texels.least) * kPerByte : 0.0F;
    }
    reflectivity_of(material, &out);
    layers_of(material, &out);
    return out;
}

// [debug] rt_reflection_materials=1: each environment-mapped material's passes logged once, when first registered (how
// R5b's surfaces were classified - water, glass, other - and where they reflect: their mask passes; "not reflective":
// its environment pass draws no environment colour).
void log_reflective_material(const MaterialRef* material, const rt::Material& out, rt::MaterialId id)
{
    static const bool kWanted = build_info::fetch_flag("debug", "rt_reflection_materials", false);
    if (!kWanted) {
        return;
    }
    std::string text = text::format("rt: %s material %u = %08X (name %08X): reflectivity %.2f, class %u, mask %u, "
                                    "sort %u, two-sided %u, alpha ref %u, texture anim %s",
                                    out.reflectivity > 0.0F ? "reflective" : "not reflective", static_cast<unsigned>(id),
                                    material->checksum(), material->name_hash(), static_cast<double>(out.reflectivity),
                                    static_cast<unsigned>(out.reflection_class), static_cast<unsigned>(out.reflection_mask), unsigned{material->sort_flag()},
                                    unsigned{material->two_sided()}, unsigned{material->alpha_ref()},
                                    material->texture_anim() != nullptr ? "yes" : "no");
    text += ", masks";
    for (const rt::Material::MaskPass& mask : out.mask_passes) {
        if (mask.kind != rt::kMaskNone) {
            text += text::format(" pass %u %s%s", mask.uv_set, mask.kind == rt::kMaskClear ? "clear" : "opaque",
                                 mask.texture_alpha ? " (texture alpha)" : "");
        }
    }
    const uint32_t count = std::min(material->pass_count(), kPassCount);
    for (uint32_t pass = 0; pass < count; ++pass) {
        const TextureImage* texture = core::at(material->textures(), pass);
        const std::array<float, 4>& colour = core::at(material->pass_color(), pass);
        text += text::format(" | pass %u flags %04X blend %u colour %.2f %.2f %.2f %.2f tex %08X %ux%u%s", pass,
                             core::at(material->pass_flags(), pass), core::at(material->blend_modes(), pass),
                             static_cast<double>(core::at(colour, 0)), static_cast<double>(core::at(colour, 1)),
                             static_cast<double>(core::at(colour, 2)), static_cast<double>(core::at(colour, 3)),
                             texture != nullptr ? texture->checksum : 0U,
                             texture != nullptr ? unsigned{texture->width} : 0U,
                             texture != nullptr ? unsigned{texture->height} : 0U,
                             core::at(material->uv_wibble(), pass) != nullptr ? " wibble" : "");
    }
    get_log("%s", text.c_str());
}

// [debug] rt_seen_through_materials=1: each material ray tracing takes as seen through or alpha tested logged once, when
// first registered - which cast shadows by their alpha (rt::kAlphaTested) and which none (rt::kTransparent).
void log_seen_through_material(const MaterialRef* material, const rt::Material& out, rt::MaterialId id)
{
    static const bool kWanted = build_info::fetch_flag("debug", "rt_seen_through_materials", false);
    if (!kWanted || (out.flags & (rt::kTransparent | rt::kAlphaTested)) == 0U) {
        return;
    }
    const TextureImage* texture = core::at(material->textures(), 0);
    get_log("rt: material %u = %08X: %s, alpha ref %u, passes %u, first pass blend %u flags %04X colour alpha %.2f, "
            "tex %08X",
            static_cast<unsigned>(id), material->checksum(),
            (out.flags & rt::kTransparent) != 0U ? "seen through" : "alpha tested", unsigned{material->alpha_ref()},
            material->pass_count(), core::at(material->blend_modes(), 0), core::at(material->pass_flags(), 0),
            static_cast<double>(core::at(core::at(material->pass_color(), 0), 3)),
            texture != nullptr ? texture->checksum : 0U);
}

// The material's id - given (again, when its first pass changed) once a frame - or 0 for none.
rt::MaterialId material_id(const MaterialRef* material, rt::RayTracing& rt)
{
    if (material == nullptr) {
        return 0;
    }
    State& st = state();
    auto [found, made] = st.materials.try_emplace(material);
    MaterialEntry& entry = found->second;
    if (made) {
        entry.id = rt::next_material_id();
    } else if (entry.frame == st.frame) {
        return entry.id;
    }
    entry.frame = st.frame;
    const rt::Material now = material_of(material);
    if (made && (now.reflectivity > 0.0F || has_environment_pass(material))) {
        log_reflective_material(material, now, entry.id);
    }
    if (made) {
        log_seen_through_material(material, now, entry.id);
    }
    if (made || !same_material(entry.material, now)) {
        entry.material = now;
        rt.set_material(entry.id, now);
    }
    return entry.id;
}

// A mesh drawn shown, not hidden.
bool mesh_shown(const MeshRef* mesh)
{
    return (mesh->flags() & kMeshShown) != 0U && (mesh->flags() & kMeshHidden) == 0U;
}

// (path tracing, stage 3) The mesh's pose for `owner` (the instance drawing it with `palette`): skinned now, as its
// draw will skin it this frame - 0 when it can't be (no layout, a bone past the palette, no buffer).
rt::MeshId pose_id(const MeshRef* mesh, const Singleton* owner, std::span<const maths::Mat4> palette,
                   rt::RayTracing& rt, rt::Geometry* layout)
{
    const VertexBufferData* source = nullptr;
    if (owner == nullptr || palette.empty() || !geometry_of(mesh, layout, &source)) {
        return 0;
    }
    bool skinned = true;
    State& st = state();
    const rt::MeshId id = st.poses.pose(
        owner, mesh, *layout, st.frame,
        [&](std::span<uint8_t> out) {
            skinned = mesh->skin_for_rays(owner, palette, out);
            return skinned;
        },
        gpu(), rt);
    if (id != 0) {
        // (the record's poses key: the pose's mesh and the bones it was skinned with)
        uint64_t& key = rt::frame_record().poses_key;
        key = (key ^ id) * kFnvPrime;
        for (const maths::Mat4& bone : palette) {
            for (const float value : bone.floats()) {
                key = (key ^ std::bit_cast<uint32_t>(value)) * kFnvPrime;
            }
        }
    }
    if (!skinned) {
        static bool told = false;
        if (!told) {
            told = true;
            get_log("rt: a skinned mesh (%u vertices) could not be posed for the rays: a bone past its instance's %zu",
                    mesh->vertex_count(), palette.size());
        }
    }
    return id;
}

// [debug] rt_objects=1: each object's mesh logged once, when first recorded - how ray tracing sees the skater's, peds'
// and vehicles' parts (which cast, which are alpha tested or seen through, skinned, how many vertex buffers they cycle).
void log_object_mesh(const MeshRef* mesh, const rt::Geometry* pose, rt::MeshId id, uint32_t flags,
                     rt::MaterialId material)
{
    static const bool kWanted = build_info::fetch_flag("debug", "rt_objects", false);
    if (!kWanted || (flags & rt::kObject) == 0U) {
        return;
    }
    static core::NoDestroy<std::unordered_map<rt::MeshId, uint32_t>> logged; // (again when its flags change)
    const auto [found, made] = logged.get().try_emplace(id, flags);
    if (!made && found->second == flags) {
        return;
    }
    found->second = flags;
    const rt::Geometry& geometry = pose != nullptr ? *pose : state().meshes.at(mesh).geometry; // (a pose's: its layout)
    const MaterialRef* surface = mesh->material();
    get_log("rt: object mesh %u: flags %04X, material %u (%08X, alpha ref %u, first pass blend %u flags %04X), "
            "%u vertices, %u vertex buffers, bone %d, skinned %s, format %X, stride %u, its offsets normal %u colour %u "
            "uv %u, traced at normal %d colour %d uv %d (%u sets; -1: none)",
            static_cast<unsigned>(id), flags, static_cast<unsigned>(material),
            surface != nullptr ? surface->checksum() : 0U, surface != nullptr ? unsigned{surface->alpha_ref()} : 0U,
            surface != nullptr ? core::at(surface->blend_modes(), 0) : 0U,
            surface != nullptr ? core::at(surface->pass_flags(), 0) : 0U, mesh->vertex_count(),
            unsigned{mesh->vertex_buffer_count()}, mesh->bone() == kNoBoneByte ? kNoBone : int{mesh->bone()},
            mesh->per_vertex_data() != nullptr ? "yes" : "no", mesh->fvf(), unsigned{mesh->stride()},
            unsigned{mesh->normal_offset()}, unsigned{mesh->color_offset()}, unsigned{mesh->uv_offset()},
            shown(geometry.normal_format, geometry.normal_offset), shown(geometry.colour_format, geometry.colour_offset),
            shown(geometry.uv_format, geometry.uv_offset), geometry.uv_sets);
}

// The instance whose bones a skinned mesh is drawn with, and those bones (path tracing, stage 3: its pose).
struct Skin {
    const Singleton* owner = nullptr;
    std::span<const maths::Mat4> palette;
};

// (round PT-B) The least vertex alpha (0 - 1, the colour word's alpha / 255) of the buffer the mesh was last drawn from;
// 1 for a mesh without vertex colours (Direct3D's diffuse then is white) or no buffer.
float least_vertex_alpha(const MeshRef* mesh)
{
    const VertexBuffer* buffer = last_drawn_buffer(mesh);
    if (buffer == nullptr) {
        return 1.0F;
    }
    State& st = state();
    const auto found = st.vertex_alpha.find(buffer);
    if (found != st.vertex_alpha.end()) {
        return found->second;
    }
    rt::Geometry layout;
    const uint32_t stride = mesh->stride();
    float least = 1.0F;
    if (rt::set_vertex_attributes(mesh->fvf(), &layout) && layout.colour_format == rt::kColourA8R8G8B8 && stride > 0 &&
        layout.colour_offset + sizeof(uint32_t) <= stride && buffer->data() != nullptr) {
        const uint32_t count = std::min<uint32_t>(mesh->vertex_count(), static_cast<uint32_t>(buffer->size() / stride));
        const std::span<const uint8_t> bytes(buffer->data(), std::size_t{count} * stride);
        uint32_t lowest = kByteMax;
        for (uint32_t vi = 0; vi < count; ++vi) {
            // (the colour word's bytes B, G, R, A)
            lowest = std::min<uint32_t>(lowest, core::at(bytes, (std::size_t{vi} * stride) + layout.colour_offset + 3));
        }
        least = static_cast<float>(lowest) * kPerByte;
    }
    st.vertex_alpha.emplace(buffer, least);
    return least;
}

// (round PT-B) How every ray meets the mesh drawn with its material (rt_surface_class.h): the material's own data and,
// where it counts, the mesh's vertex alpha.
rt::SurfaceClass surface_class_of(const MeshRef* mesh, const rt::Material& material)
{
    return rt::surface_class(material, rt::vertex_alpha_counts(material) ? least_vertex_alpha(mesh) : 1.0F);
}

// One instance of a mesh with a world transform (a Direct3D matrix); `light`: its object's lights in the record
// (rt::Instance::light); `skin`: the drawing instance and its bones, for a mesh skinned on the processor (its pose).
void add_instance(const MeshRef* mesh, uint32_t kind, const maths::Mat4& world, rt::RayTracing& rt,
                  uint32_t light = rt::kNoObjectLight, const Skin& skin = {})
{
    // (path tracing, stage 3: a mesh skinned on the processor traced in its instance's pose of this frame - one pose
    // per instance; without an instance's bones, not traced)
    const bool skinned = mesh->per_vertex_data() != nullptr;
    rt::Geometry layout;
    const rt::MeshId id =
        skinned ? pose_id(mesh, skin.owner, skin.palette, rt, &layout) : mesh_id(mesh, rt);
    if (id == 0) {
        return;
    }
    const rt::MaterialId material = material_id(mesh->material(), rt);
    uint32_t flags = kind;
    // (round PT-B) its flags from its class - the one classification every ray takes (rt_surface_class.h)
    if (material != 0) {
        const rt::Material& surface = state().materials.at(mesh->material()).material;
        flags |= rt::surface_instance_flags(surface_class_of(mesh, surface), surface);
    } else {
        flags |= rt::kOpaque | rt::kCastsShadow;
    }
    if (skinned) {
        flags |= rt::kSkinned;
    }
    log_object_mesh(mesh, skinned ? &layout : nullptr, id, flags, material);
    rt::frame_record().instances.push_back({.mesh = id,
                                            .material = material,
                                            .flags = flags,
                                            .object_to_world = rt::object_to_world_rows(world.floats()),
                                            .light = light});
}

// The loaded scenes' meshes but the sky's (draw_viewport's world passes; the world transform is the identity).
void record_world(rt::RayTracing& rt)
{
    maths::Mat4 identity;
    identity.reset_to_identity();
    const memory::ListNode* node = engine_parts().scene_list().next();
    while (node != nullptr && !node->is_head()) {
        const auto* scene = static_cast<const PcScene*>(node->data());
        node = node->next();
        const SceneRef* meshes = scene != nullptr ? scene->meshes() : nullptr;
        if (meshes == nullptr || scene->is_sky() || meshes->unknown40() != 0U) {
            continue;
        }
        uint64_t count = 0;
        // (the relit bake - relit_bake.h: every mesh of the level's world, shown or hidden, is taken apart - its data
        // has them all, whatever a script shows later; the rays meet the meshes the level shows once it has settled, as
        // the game draws it - a hidden mesh is registered, not an instance)
        const bool every = relit_bake::requested();
        for (const MeshRef* mesh : meshes->mesh_run()) {
            ++count;
            if (mesh == nullptr) {
                continue;
            }
            // (round PG) a billboard - a card the processor turns toward each camera as it draws it: its buffer holds the
            // last draw's vertices - is in the rays as drawn, through the draws apart (rt_apart.h), not as a world mesh
            if (mesh->billboard() != nullptr) {
                continue;
            }
            if (mesh_shown(mesh)) {
                add_instance(mesh, rt::kWorld, identity, rt);
                state().world.push_back(mesh);
            } else if (every && mesh_id(mesh, rt) != 0) {
                state().world.push_back(mesh);
            }
        }
        // (the loaded scenes' key: which they are and how many meshes each has - not which are shown, so a sector
        // turned on or off keeps the level's fit)
        for (const uint64_t part : {static_cast<uint64_t>(std::bit_cast<uintptr_t>(scene)),
                                    static_cast<uint64_t>(std::bit_cast<uintptr_t>(meshes)), count}) {
            state().scene_key = (state().scene_key ^ part) * kFnvPrime;
        }
    }
}

// One instance's meshes as Singleton::render draws them: a draw per bone (the meshes of that bone, the bone times its
// transform) when its scene's bones are drawn so (+0x78), else its transform - a mesh with one bone of the instance's
// bones before it.
void record_object(const Singleton* instance, rt::RayTracing& rt)
{
    const SceneRef* scene = instance->scene();
    if (scene == nullptr) {
        return;
    }
    const maths::Mat4& transform = instance->transform();
    // (round P10 - objects relit) how the game draws it: lit by its lights (its vertex colours paint), or with its own
    // baked colours - placed (object_placements) when it isn't animated and has stood still since the record before
    const bool own_colours = !instance_drawn_lit(instance);
    bool still = false;
    {
        State& st_now = state();
        auto [kept, made] = st_now.still.try_emplace(instance);
        if (made || !(kept->second.transform == transform)) {
            kept->second.transform = transform;
            kept->second.changed = st_now.frame;
        }
        if (!made && kept->second.seen + 1 != st_now.frame) {
            kept->second.changed = st_now.frame; // (not drawn in between: counted again from now)
        }
        still = !made && kept->second.changed + kObjectStillFrames <= st_now.frame;
        kept->second.seen = st_now.frame;
        const bool animated = instance->bones() != nullptr && instance->bone_count_ > 0;
        st_now.counts.lit += own_colours ? 0 : 1;
        st_now.counts.own += own_colours ? 1 : 0;
        st_now.counts.skinned += own_colours && animated ? 1 : 0;
        st_now.counts.moving += own_colours && !animated && !still ? 1 : 0;
        still = still && own_colours && !animated;
    }
    const auto place = [instance, own_colours, still](const MeshRef* mesh, const maths::Mat4& world) {
        State& st_now = state();
        (own_colours ? st_now.counts.own_meshes : st_now.counts.lit_meshes) += 1;
        if (still) {
            st_now.placed.push_back({.instance = instance, .mesh = mesh, .world = world});
        }
    };
    // (the objects' shadows) the lights its last draw was lit by - drawn since the record before (object_lit)
    uint32_t light = rt::kNoObjectLight;
    const State& st = state();
    if (const auto lit = st.object_lights.find(instance);
        lit != st.object_lights.end() && lit->second.frame + 1 >= st.frame) {
        rt::FrameRecord& record = rt::frame_record();
        light = static_cast<uint32_t>(record.object_lights.size());
        record.object_lights.push_back(lit->second.light);
    }
    const auto bones_on_device = static_cast<int>(scene->unknown78());
    const int bone_count = instance->bone_count_;
    if (bones_on_device != 0 && instance->bones() != nullptr && bone_count > 0) {
        const std::span<const maths::Mat4> bones(instance->bones(), static_cast<std::size_t>(bone_count));
        for (const MeshRef* mesh : scene->mesh_run()) {
            const int bone = mesh == nullptr || mesh->bone() == kNoBoneByte ? kNoBone : int{mesh->bone()};
            if (mesh == nullptr || (mesh->flags() & kMeshHidden) != 0U || bone < 0 || bone >= bone_count ||
                bone >= kMaxDrawnBones) {
                continue;
            }
            maths::Mat4 world = transform;
            maths::matrix_multiply(&world, &core::at(bones, static_cast<std::size_t>(bone)), &world);
            add_instance(mesh, rt::kObject, world, rt, light);
        }
        return;
    }
    const bool palette = instance->bones() != nullptr && bones_on_device <= 0 && bone_count > 0;
    // (the bones a skinned mesh of it is drawn with - Singleton::render's palette: its own)
    const Skin skin{.owner = instance,
                    .palette = palette ? std::span<const maths::Mat4>(instance->bones(), static_cast<std::size_t>(bone_count))
                                       : std::span<const maths::Mat4>{}};
    for (const MeshRef* mesh : scene->mesh_run()) {
        if (mesh == nullptr || !mesh_shown(mesh)) {
            continue;
        }
        if (mesh->unknown98 != kNoBone && palette && mesh->unknown98 >= 0 && mesh->unknown98 < bone_count) {
            maths::Mat4 world;
            maths::matrix_multiply(&world, &core::at(skin.palette, static_cast<std::size_t>(mesh->unknown98)),
                                   &transform);
            add_instance(mesh, rt::kObject, world, rt, light, skin);
        } else {
            add_instance(mesh, rt::kObject, transform, rt, light, skin);
            place(mesh, transform);
        }
    }
}

// --- the baked sun (engine/gfx/rt_sun.h) ------------------------------------------------------------------------------

// A mesh's triangles as the sampler walks them, read from the copies in memory: their count, a triangle's corners, a
// vertex's position and its baked colour's brightness.
class MeshTriangles {
public:
    // (`as_drawn`, added by the scene's sun: the colours as the buffer holds them now - the relit ones -, not the bake's)
    MeshTriangles(const rt::Geometry& geometry, const VertexBufferData& source, uint32_t colour, bool as_drawn)
        : geometry_(geometry),
          vertices_(source.data(), source.size()),
          colour_(colour),
          index_bytes_(geometry.indices_32bit ? sizeof(uint32_t) : sizeof(uint16_t)),
          originals_(as_drawn ? std::span<const uint32_t>{} : rt_relit_world::original_colours(&source))
    {
    }

    [[nodiscard]] uint32_t count() const
    {
        if (geometry_.strip) {
            return geometry_.index_count >= 3 ? geometry_.index_count - 2 : 0U;
        }
        return geometry_.index_count / 3;
    }
    // The triangle's three vertex indices, or false (degenerate, or outside the vertices).
    bool corners(uint32_t triangle, std::array<uint32_t, 3>* out) const
    {
        const uint32_t first = geometry_.strip ? triangle : triangle * 3;
        *out = {index(first), index(first + 1), index(first + 2)};
        const uint32_t limit =
            std::min(geometry_.vertex_count, static_cast<uint32_t>(vertices_.size() / geometry_.vertex_stride));
        const auto [one, two, three] = *out;
        return one != two && two != three && one != three && one < limit && two < limit && three < limit;
    }
    [[nodiscard]] rt::Vec3 position(uint32_t vertex) const
    {
        rt::Vec3 out{};
        std::memcpy(out.data(), vertices_.subspan(std::size_t{vertex} * geometry_.vertex_stride, sizeof(out)).data(),
                    sizeof(out));
        if (placed_) { // (round P10: an object's mesh where it stands - Direct3D's rows, p' = (x, y, z, 1) x M)
            const rt::Vec3 was = out;
            for (std::size_t c = 0; c < 3; ++c) {
                core::at(out, c) = (std::get<0>(was) * core::at(to_world_, c)) +
                                   (std::get<1>(was) * core::at(to_world_, 4 + c)) +
                                   (std::get<2>(was) * core::at(to_world_, 8 + c)) + core::at(to_world_, 12 + c);
            }
        }
        return out;
    }
    // (round P10 - objects relit) its positions through an object's transform (the emitters of an object drawn with its
    // own colours, where it stands)
    void place(const std::array<float, 16>& to_world)
    {
        to_world_ = to_world;
        placed_ = true;
    }
    // (the colour's B, G, R bytes as luma)
    [[nodiscard]] float brightness(uint32_t vertex) const
    {
        std::array<uint8_t, kColourBytes> bgra{};
        if (vertex < originals_.size()) { // (the relit mode wrote its own colours: the bake's read)
            std::memcpy(bgra.data(), &core::at(originals_, vertex), bgra.size());
        } else {
            std::memcpy(bgra.data(),
                        vertices_.subspan((std::size_t{vertex} * geometry_.vertex_stride) + colour_, bgra.size()).data(),
                        bgra.size());
        }
        return (kLumaBlue * static_cast<float>(std::get<0>(bgra))) + (kLumaGreen * static_cast<float>(std::get<1>(bgra))) +
               (kLumaRed * static_cast<float>(std::get<2>(bgra)));
    }
    // (added by the night lighting) The baked colour as red, green, blue (128 the game's white: 1); white when the mesh
    // has no colours (kNoColour).
    [[nodiscard]] std::array<float, 3> colour(uint32_t vertex) const
    {
        if (colour_ == kNoColour) {
            return {1.0F, 1.0F, 1.0F};
        }
        std::array<uint8_t, kColourBytes> bgra{};
        if (vertex < originals_.size()) {
            std::memcpy(bgra.data(), &core::at(originals_, vertex), bgra.size());
        } else {
            std::memcpy(bgra.data(),
                        vertices_.subspan((std::size_t{vertex} * geometry_.vertex_stride) + colour_, bgra.size()).data(),
                        bgra.size());
        }
        return {static_cast<float>(std::get<2>(bgra)) * kPerByteLight, static_cast<float>(std::get<1>(bgra)) * kPerByteLight,
                static_cast<float>(std::get<0>(bgra)) * kPerByteLight};
    }
    static constexpr uint32_t kNoColour = 0xFFFFFFFFU;
    // (round light cards) The baked colour's alpha, doubled as the passes double it (128: 1); 1 without colours.
    [[nodiscard]] float alpha(uint32_t vertex) const
    {
        if (colour_ == kNoColour) {
            return 1.0F;
        }
        std::array<uint8_t, kColourBytes> bgra{};
        if (vertex < originals_.size()) {
            std::memcpy(bgra.data(), &core::at(originals_, vertex), bgra.size());
        } else {
            std::memcpy(bgra.data(),
                        vertices_.subspan((std::size_t{vertex} * geometry_.vertex_stride) + colour_, bgra.size()).data(),
                        bgra.size());
        }
        return static_cast<float>(std::get<3>(bgra)) * kPerByteLight;
    }
    // (lighting round A3) The vertex's first texture coordinates, or false (none).
    bool uv(uint32_t vertex, std::array<float, 2>* out) const
    {
        if (geometry_.uv_format != rt::kUvFloat2) {
            return false;
        }
        const std::size_t at = (std::size_t{vertex} * geometry_.vertex_stride) + geometry_.uv_offset;
        if (at + sizeof(*out) > vertices_.size()) {
            return false;
        }
        std::memcpy(out->data(), vertices_.subspan(at, sizeof(*out)).data(), sizeof(*out));
        return std::isfinite(std::get<0>(*out)) && std::isfinite(std::get<1>(*out));
    }

private:
    std::array<float, 16> to_world_{};
    bool placed_ = false;
    [[nodiscard]] uint32_t index(uint32_t at) const
    {
        const std::span<const uint8_t> indices(static_cast<const uint8_t*>(geometry_.cpu_indices),
                                               (std::size_t{geometry_.index_offset} + geometry_.index_count) *
                                                   index_bytes_);
        const std::size_t byte = (std::size_t{geometry_.index_offset} + at) * index_bytes_;
        if (index_bytes_ == sizeof(uint32_t)) {
            uint32_t value = 0;
            std::memcpy(&value, indices.subspan(byte, sizeof(value)).data(), sizeof(value));
            return value;
        }
        uint16_t value = 0;
        std::memcpy(&value, indices.subspan(byte, sizeof(value)).data(), sizeof(value));
        return value;
    }

    rt::Geometry geometry_; // (a copy: the sampled triangles may be other than the drawn ones - sampled_meshes)
    std::span<const uint8_t> vertices_;
    uint32_t colour_; // the colour's offset in a vertex
    std::size_t index_bytes_;
    std::span<const uint32_t> originals_; // (the bake's colours where the relit mode has written others; else empty)
};

// A triangle's corners and its normal turned up; false: no area, or not facing up (or down - turned up) enough.
struct UpTriangle {
    std::array<rt::Vec3, 3> corners{};
    rt::Vec3 normal{};
};
bool up_triangle(const MeshTriangles& mesh, const std::array<uint32_t, 3>& corner, UpTriangle* out)
{
    const auto [first, second, third] = corner;
    const std::array<rt::Vec3, 3> at{mesh.position(first), mesh.position(second), mesh.position(third)};
    const rt::Vec3 normal = rt::cross(rt::minus(std::get<1>(at), std::get<0>(at)), rt::minus(std::get<2>(at), std::get<0>(at)));
    const float length = std::sqrt(rt::dot(normal, normal));
    if (!(length > 0.0F) || !std::isfinite(length)) {
        return false;
    }
    const rt::Vec3 up = rt::scaled(normal, rt::y_of(normal) < 0.0F ? -1.0F / length : 1.0F / length);
    if (rt::y_of(up) < kMinUp) {
        return false;
    }
    *out = {.corners = at, .normal = up};
    return true;
}

// A world mesh's triangles as the samplers read them (its copies in memory), or false: none there, a dynamic mesh, or
// no diffuse colour (unless `colourless`, added by the night lighting: then MeshTriangles::kNoColour - white).
bool mesh_triangles(const MeshRef* mesh, bool as_drawn, bool colourless, std::vector<MeshTriangles>* out)
{
    State& st = state();
    {
        const auto found = st.meshes.find(mesh);
        if (found == st.meshes.end() || found->second.source == nullptr) {
            return false;
        }
        const rt::Geometry& geometry = found->second.geometry;
        const VertexBufferData& source = *found->second.source;
        const bool coloured = geometry.colour_format == rt::kColourA8R8G8B8;
        // (the colour where the vertex format puts it - rt_vertex_attributes.h -, within the vertex)
        if (source.data() == nullptr || geometry.cpu_indices == nullptr || geometry.dynamic ||
            geometry.vertex_stride == 0 || (!coloured && !colourless)) {
            return false;
        }
        const uint32_t colour = coloured ? geometry.colour_offset : MeshTriangles::kNoColour;
        // Not original - a mesh smoothed at load (mesh_smoothing.h) sampled on its original triangles (its original
        // vertices are still first), so the sun and the bake's agreement are found as without smoothing.
        rt::Geometry sampled = geometry;
        const std::span<const uint16_t> original = mesh_smoothing::original_strip(core::at(mesh->index_buffers(), 0));
        if (!original.empty()) {
            sampled.cpu_indices = original.data();
            sampled.index_offset = 0;
            sampled.index_count = static_cast<uint32_t>(original.size());
            sampled.indices_32bit = false;
            sampled.strip = true;
        }
        out->emplace_back(sampled, source, colour, as_drawn);
    }
    return true;
}

// The meshes the samples come from: the world's with a copy of their vertices and indices in memory and a diffuse
// colour.
std::vector<MeshTriangles> sampled_meshes(bool as_drawn)
{
    std::vector<MeshTriangles> meshes;
    for (const MeshRef* mesh : state().world) {
        mesh_triangles(mesh, as_drawn, false, &meshes);
    }
    return meshes;
}

// A point of a triangle by its weights for the second and third corners (the first's the rest), and the brightness
// there blended from the corners'.
rt::BakeSample point_of(const MeshTriangles& mesh, const std::array<uint32_t, 3>& corner, const UpTriangle& up,
                        std::array<float, 2> weights)
{
    const auto [second, third] = weights;
    const float first = 1.0F - second - third;
    const auto& [one, two, three] = up.corners;
    return {.position = rt::plus(rt::plus(rt::scaled(one, first), rt::scaled(two, second)), rt::scaled(three, third)),
            .normal = up.normal,
            .brightness = (mesh.brightness(std::get<0>(corner)) * first) + (mesh.brightness(std::get<1>(corner)) * second) +
                          (mesh.brightness(std::get<2>(corner)) * third)};
}

// Where the k-th point in a triangle lies (the second and third corners' weights): its centre, then a low-discrepancy
// sequence (R2) folded into the triangle.
std::array<float, 2> weights_of(uint32_t point)
{
    constexpr double kThird = 1.0 / 3.0;
    constexpr double kR2First = 0.7548776662466927; // (the R2 sequence's steps)
    constexpr double kR2Second = 0.5698402909980532;
    constexpr double kStart = 0.5;
    if (point == 0) {
        return {static_cast<float>(kThird), static_cast<float>(kThird)};
    }
    double second = std::fmod(kStart + (kR2First * point), 1.0);
    double third = std::fmod(kStart + (kR2Second * point), 1.0);
    if (second + third > 1.0) {
        second = 1.0 - second;
        third = 1.0 - third;
    }
    return {static_cast<float>(second), static_cast<float>(third)};
}

// Samples of the world's upward-facing triangles with their baked colours: each triangle counted the same (the play
// areas' detailed ground weighs more than the scenery's few large faces - the bake's shadows are where the detail is),
// every n-th of them for about `count.triangles` triangles, `count.points` points in each (weights_of). The same world
// gives the same samples.
struct SampleCount {
    std::size_t triangles = 0;
    uint32_t points = 1;
    bool as_drawn = false; // (added by the scene's sun) the relit colours, not the bake's
};
std::vector<rt::BakeSample> bake_samples(SampleCount count)
{
    const std::vector<MeshTriangles> meshes = sampled_meshes(count.as_drawn);
    std::size_t total = 0;
    for (const MeshTriangles& mesh : meshes) {
        total += mesh.count();
    }
    std::vector<rt::BakeSample> samples;
    // (changed by the light energy audit: the same triangles whatever else the frame drew) A triangle is taken by where
    // it is - a hash of its centre, to a unit - one in `step` of them, `step` a power of two: a sector more or less in
    // the first frame (a script's mesh, the evening's lamps) changes only its own samples. Before, every step-th
    // triangle in the world's order: one mesh more moved every sample after it, and the baked sun moved by degrees
    // between two loads of a level (sch: heading -43 / -45, elevation 38 / 35) - enough to tip relit's fit.
    std::size_t step = 1;
    while (step * std::max<std::size_t>(1, count.triangles) < total) {
        step <<= 1U;
    }
    uint32_t group = 0; // (lighting round A: each sample's mesh - rt_sun.h BakeSample::group)
    for (const MeshTriangles& mesh : meshes) {
        ++group;
        const uint32_t triangles = mesh.count();
        for (uint32_t index = 0; index < triangles; ++index) {
            std::array<uint32_t, 3> corner{};
            if (!mesh.corners(index, &corner)) {
                continue;
            }
            if (step > 1) {
                const auto [first, second, third] = corner;
                const rt::Vec3 centre = rt::scaled(
                    rt::plus(rt::plus(mesh.position(first), mesh.position(second)), mesh.position(third)), kThirdOf);
                uint64_t hash = kSampleHashBasis;
                for (const float axis : {rt::x_of(centre), rt::y_of(centre), rt::z_of(centre)}) {
                    hash = (hash ^ static_cast<uint64_t>(static_cast<int64_t>(std::lround(axis)))) * kSampleHashPrime;
                }
                hash ^= hash >> kSampleHashFold;
                if ((hash & (step - 1)) != 0) {
                    continue;
                }
            }
            UpTriangle up;
            if (!up_triangle(mesh, corner, &up)) {
                continue;
            }
            for (uint32_t k = 0; k < count.points; ++k) {
                samples.push_back(point_of(mesh, corner, up, weights_of(k)));
                samples.back().group = group;
            }
        }
    }
    return samples;
}

// A direction's heading and elevation in degrees (for the log).
std::array<double, 2> heading_elevation(const rt::Vec3& toward)
{
    constexpr double kDegrees = 180.0 / 3.14159265358979323846;
    return {std::atan2(static_cast<double>(rt::x_of(toward)), static_cast<double>(rt::z_of(toward))) * kDegrees,
            std::asin(std::clamp(static_cast<double>(rt::y_of(toward)), -1.0, 1.0)) * kDegrees};
}

// The fit made and logged: the sun.
void make_fit(rt::RayTracing& rt)
{
    const auto start = std::chrono::steady_clock::now();
    rt::SunFit fit = rt::fit_baked_sun(bake_samples({.triangles = kFitTriangles, .points = 1}), rt);
    fit.sun.scene = ++state().fits;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto [heading, elevation] = heading_elevation(fit.sun.toward);
    get_log("rt: the baked sun: %s toward %.3f %.3f %.3f (heading %.0f, elevation %.0f), confidence %.2f - %zu samples, "
            "%zu rays, %.0f ms",
            fit.sun.found ? "found" : "none (the bake shows no sun: no sun shadows here)",
            static_cast<double>(rt::x_of(fit.sun.toward)), static_cast<double>(rt::y_of(fit.sun.toward)),
            static_cast<double>(rt::z_of(fit.sun.toward)), heading, elevation,
            static_cast<double>(fit.sun.confidence), fit.samples, fit.rays, ms);
    for (const std::array<float, 4>& other : fit.runners_up) {
        const auto [other_heading, other_elevation] =
            heading_elevation({std::get<0>(other), std::get<1>(other), std::get<2>(other)});
        get_log("rt: the baked sun's runners-up: heading %.0f, elevation %.0f, score %.2f", other_heading,
                other_elevation, static_cast<double>(std::get<3>(other)));
    }
    rt::set_baked_sun(std::move(fit.sun));
}

// The world meshes recorded this frame as rt_relit_world takes them (their RayTracing ids, geometry and buffers).
bool glow_card(const MaterialRef* material);

std::vector<rt_relit_world::WorldMesh> world_meshes()
{
    State& st = state();
    std::vector<rt_relit_world::WorldMesh> world;
    world.reserve(st.world.size());
    for (const MeshRef* mesh : st.world) {
        const auto found = st.meshes.find(mesh);
        if (found != st.meshes.end()) {
            const MaterialRef* material = mesh->material();
            const uint32_t first = material != nullptr ? blend_mode(material, 0) : 0U;
            // (round light cards) the instance flags add_instance gives it: blended and not cut out - never in the lamps
            // pass's camera ray (vulkan_rt_scene.cpp instance_of: kMaskNoShadow)
            // (round PT-B: by its class - a blended surface drawn opaque is one of them)
            const auto surface = material != nullptr ? st.materials.find(material) : st.materials.end();
            const rt::SurfaceClass kind =
                surface != st.materials.end() ? surface_class_of(mesh, surface->second.material) : rt::SurfaceClass::kOpaque;
            const bool lit_per_pixel = kind == rt::SurfaceClass::kOpaque || kind == rt::SurfaceClass::kCutOut ||
                                       kind == rt::SurfaceClass::kBlendedOpaque;
            world.push_back({.mesh = mesh,
                             .id = found->second.id,
                             .geometry = &found->second.geometry,
                             .source = found->second.source,
                             .blended = first != 0U && first != kBlendAdd && first != kBlendFixedAdd,
                             .glow = material != nullptr && glow_card(material),
                             .lit_per_pixel = lit_per_pixel});
        }
    }
    // (round P10 - objects relit) the objects drawn with their own colours that stand still, each mesh where it stands -
    // taken apart and relit as the world's meshes are (rt_relit_world.h WorldMesh::object). A mesh placed more than once
    // (one model's mesh drawn by two instances, or twice by one) has one set of colours and buffer for several places:
    // its light can't be one store - not placed (object_census counts them)
    std::unordered_map<const MeshRef*, uint32_t> placings;
    for (const State::Placement& placed : st.placed) {
        placings[placed.mesh] += 1;
    }
    for (const State::Placement& placed : st.placed) {
        const auto found = st.meshes.find(placed.mesh);
        if (found == st.meshes.end() || placings[placed.mesh] != 1) {
            continue;
        }
        const MaterialRef* material = placed.mesh->material();
        const uint32_t first = material != nullptr ? blend_mode(material, 0) : 0U;
        const auto surface = material != nullptr ? st.materials.find(material) : st.materials.end();
        const rt::SurfaceClass kind = surface != st.materials.end() ? surface_class_of(placed.mesh, surface->second.material)
                                                                    : rt::SurfaceClass::kOpaque;
        rt_relit_world::WorldMesh mesh{.mesh = placed.mesh,
                                       .id = found->second.id,
                                       .geometry = &found->second.geometry,
                                       .source = found->second.source,
                                       .blended = first != 0U && first != kBlendAdd && first != kBlendFixedAdd,
                                       .glow = material != nullptr && glow_card(material),
                                       .lit_per_pixel = kind == rt::SurfaceClass::kOpaque ||
                                                        kind == rt::SurfaceClass::kCutOut ||
                                                        kind == rt::SurfaceClass::kBlendedOpaque,
                                       .object = placed.instance};
        std::ranges::copy(placed.world.floats(), mesh.to_world.begin());
        world.push_back(mesh);
    }
    return world;
}

// (round P10) Which objects are placed now (world_meshes' objects): their meshes and transforms hashed.
uint64_t placed_key()
{
    // (an object placed or let go of changes it - kObjectStillFrames keeps one that moves now and then from flickering)
    uint64_t key = kFnvBasis;
    for (const State::Placement& placed : state().placed) {
        key = (key ^ static_cast<uint64_t>(std::bit_cast<uintptr_t>(placed.mesh))) * kFnvPrime;
        for (const float value : placed.world.floats()) {
            key = (key ^ std::bit_cast<uint32_t>(value)) * kFnvPrime;
        }
    }
    return key;
}

std::string object_census_detail()
{
    const State& st = state();
    // (each placed object: where, its meshes, vertices, materials and its colours' brightness - the bytes' luma, 128 white)
    std::string detail;
    std::unordered_map<const Singleton*, std::vector<const MeshRef*>> by_instance;
    for (const State::Placement& placed : st.placed) {
        by_instance[placed.instance].push_back(placed.mesh);
    }
    for (const auto& [instance, meshes] : by_instance) {
        const maths::Vec4& at = core::at(instance->transform().row, 3);
        detail += text::format("\n  object at %.0f %.0f %.0f:", static_cast<double>(at.x), static_cast<double>(at.y),
                               static_cast<double>(at.z));
        for (const MeshRef* mesh : meshes) {
            std::vector<MeshTriangles> read;
            float least = 255.0F;
            float most = 0.0F;
            double sum = 0.0;
            uint32_t n = 0;
            if (mesh_triangles(mesh, false, false, &read)) {
                const auto found = st.meshes.find(mesh);
                const uint32_t count = found != st.meshes.end() ? found->second.geometry.vertex_count : 0U;
                for (uint32_t v = 0; v < count; ++v) {
                    const float y = read.front().brightness(v);
                    least = std::min(least, y);
                    most = std::max(most, y);
                    sum += y;
                    ++n;
                }
            }
            const MaterialRef* material = mesh->material();
            detail += text::format(" [%08X %u vertices, luma %.0f / %.0f / %.0f]", material != nullptr ? material->checksum() : 0U,
                                   mesh->vertex_count(), static_cast<double>(n > 0 ? least : 0.0F),
                                   n > 0 ? sum / n : 0.0, static_cast<double>(most));
        }
    }
    return detail;
}

std::string object_census_text()
{
    const State& st = state();
    std::unordered_map<const MeshRef*, uint32_t> placings;
    std::unordered_set<const Singleton*> instances;
    for (const State::Placement& placed : st.placed) {
        placings[placed.mesh] += 1;
        instances.insert(placed.instance);
    }
    std::size_t shared = 0;
    std::size_t once = 0;
    std::size_t vertices = 0;
    for (const auto& [mesh, count] : placings) {
        (count == 1 ? once : shared) += 1;
        vertices += count == 1 ? mesh->vertex_count() : 0U;
    }
    return text::format("%zu instances lit by the game's lights (their vertex colours paint; %zu meshes), %zu drawn with "
                        "their own colours (%zu meshes) - %zu of them animated (skinned or a bone at a time), %zu moved "
                        "since the frame before, %zu standing still with %zu meshes: %zu placed once (%zu vertices - "
                        "relit), %zu drawn at more than one place (one buffer for several: not relit)",
                        st.counts.lit, st.counts.lit_meshes, st.counts.own, st.counts.own_meshes, st.counts.skinned,
                        st.counts.moving, instances.size(), placings.size(), once, vertices, shared);
}

// (changed by the first frame - docs/design/vulkan.md "The first frame after a load") A new world - the loaded scenes'
// key not the one settled: the level's first frame, after a load, the editor's load - gets its work done in that frame,
// after its begin_frame and before any view is drawn (the frame is shown over the loading screen's last picture): the
// structures the frame built run and are compacted (RayTracing::settle), and the baked sun is fitted to them when it
// is wanted - so the first frame the player sees has its shadows, its bounce light and its fog's sun. A fit wanted
// later (the settings changed) is made then; a world with nothing in it keeps nothing.
void new_world_work(rt::RayTracing& rt)
{
    State& st = state();
    if (st.world.empty()) {
        return;
    }
    const bool settle = st.scene_key != st.settled_key;
    // (lighting round A: the baked sun is always the level's data's - published in every new world)
    const bool fit = st.scene_key != st.fitted_key;
    if (settle && st.scene_key != st.fitted_key && rt::baked_sun().scene != 0) {
        rt::set_baked_sun({}); // (another world: the old fit no longer holds)
    }
    if (!settle && !fit) {
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    double settled_ms = 0.0;
    if (settle) {
        st.settled_key = st.scene_key;
        rt.settle(rt::frame_record());
        settled_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        // (the offline relit data: the level's data read and its meshes matched, what the level is lit by decided -
        // whether the relit mode is on or not: the settings model's level facts - and its baked sun)
        if (!relit_bake::requested()) {
            st.level_sun = rt_relit_world::new_world(world_meshes());
        }
    }
    if (fit) {
        st.fitted_key = st.scene_key;
        // (the offline relit data: the baked sun is the level's data's - the fit runs only in the relit bake, which
        // makes that data; the samples read the world's meshes as this frame recorded them: st.world)
        if (relit_bake::requested()) {
            make_fit(rt);
        } else {
            rt::set_baked_sun(st.level_sun);
        }
    }
    st.first_frame_work_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    st.first_frame_timed = true;
    get_log("rt: a new world's first frame: %zu world meshes, %zu instances - the structures built and compacted in "
            "%.0f ms%s, %.0f ms in all before it is drawn",
            st.world.size(), rt::frame_record().instances.size(), settled_ms,
            fit ? ", the baked sun fitted" : " (no baked sun wanted)", st.first_frame_work_ms);
}

// (added by the relit mode, rt_relit_world.h - placed by the merge after the first frame) The relit mode's work each
// frame, after the new world's: its analysis once the world's structures are settled and its sun fitted, then the
// writes of the relit colours (or the bake's back when it is turned off).
void relit_work(rt::RayTracing& rt)
{
    State& st = state();
    rt::note_census_frame(); // (the light energy audit's census: the frames its counts are over)
    if (relit_bake::requested()) {
        return; // (the relit bake: the buffers keep the bake - relit_bake.h)
    }
    if (!rt::relit_settings().on && st.relit_idle) {
        // (round LS2) nothing relit - no store of the world's light to write toward a lighting's cut: the world shows a
        // cut's light at once (rt_daylight.h note_cut_shown - as rt_relit_world::update says on its ways out). It said
        // nothing here before: a cut stayed "settling" its whole kCutMostSeconds with relit off
        rt::note_cut_shown(rt::day_light().cut);
        return;
    }
    const std::vector<rt_relit_world::WorldMesh> world = world_meshes();
    const bool ready = !st.world.empty() && st.settled_key == st.scene_key &&
                       st.fitted_key == st.scene_key;
    rt_relit_world::update(world, ready, rt);
    st.relit_idle = !rt::relit_settings().on;
}

// (added by the night lighting - engine/gfx/rt_lights.h) The world's emissive surfaces turned into lights, once a world
// while rt_lights wants them: its additive meshes (glows), and in the relit bake its candidates - triangles far brighter
// than the level (self-lit) or whose texture shows a lamp (textured; round A3). The bake's colours are read (not the
// relit mode's).
constexpr std::size_t kLumaSamples = 200000; // triangles looked at for the level's median brightness
constexpr uint32_t kBlendNone = 0;           // a first pass drawn over what is there (no blend)

bool additive_first_pass(const MaterialRef* material)
{
    const uint32_t mode = blend_mode(material, 0);
    return mode == kBlendAdd || mode == kBlendFixedAdd;
}

// Moving textures (a UV wibble, a texture animation: water, caustics, scrolling effects) - not lamps.
bool moving_material(const MaterialRef* material)
{
    if (material->texture_anim() != nullptr) {
        return true;
    }
    const uint32_t passes = std::min(material->pass_count(), kPassCount);
    for (uint32_t pass = 0; pass < passes; ++pass) {
        if ((core::at(material->pass_flags(), pass) & (kPassUvWibble | kPassTextureAnim)) != 0U ||
            core::at(material->uv_wibble(), pass) != nullptr) {
            return true;
        }
    }
    return false;
}

// (lighting round A3 - the original's light cards) A world mesh drawn additively, its texture still (not water or a
// moving effect): the halos round lamps, light cones, pools and starbursts the artists drew where light is - the lamps'
// evidence (glows), never light of their own: the relit level hides them (rt_relit_world.h WorldMesh::glow).
bool glow_card(const MaterialRef* material) { return rt::light_card(blend_mode(material, 0), moving_material(material)); }

// The level's median triangle brightness (the bake's luma, 128 its white): every n-th triangle of the coloured meshes.
float median_luma()
{
    const std::vector<MeshTriangles> meshes = sampled_meshes(false);
    std::size_t total = 0;
    for (const MeshTriangles& mesh : meshes) {
        total += mesh.count();
    }
    const std::size_t step = std::max<std::size_t>(1, total / kLumaSamples);
    std::vector<float> lumas;
    std::size_t next = 0;
    std::size_t walked = 0;
    for (const MeshTriangles& mesh : meshes) {
        for (; next < walked + mesh.count(); next += step) {
            std::array<uint32_t, 3> corner{};
            if (mesh.corners(static_cast<uint32_t>(next - walked), &corner)) {
                const auto [one, two, three] = corner;
                lumas.push_back((mesh.brightness(one) + mesh.brightness(two) + mesh.brightness(three)) / 3.0F);
            }
        }
        walked += mesh.count();
    }
    if (lumas.empty()) {
        return 0.0F;
    }
    const auto middle = lumas.begin() + static_cast<std::ptrdiff_t>(lumas.size() / 2);
    std::ranges::nth_element(lumas, middle);
    return *middle;
}

// (lighting round A2) The world's winding: +1 when its triangles' (b - a) x (c - a) points out of their front, -1 when
// into it - the sign that turns most of its upward-facing area up (floors outnumber ceilings: rt_relit.h relit_facing's
// rule, over the same meshes the samples read).
float world_facing()
{
    constexpr float kUpNormal = 0.7F;
    double signed_area = 0.0;
    for (const MeshTriangles& mesh : sampled_meshes(false)) {
        for (uint32_t t = 0; t < mesh.count(); ++t) {
            std::array<uint32_t, 3> corner{};
            if (!mesh.corners(t, &corner)) {
                continue;
            }
            const auto [one, two, three] = corner;
            const rt::Vec3 a = mesh.position(one);
            const rt::Vec3 normal = rt::cross(rt::minus(mesh.position(two), a), rt::minus(mesh.position(three), a));
            const float length = std::sqrt(rt::dot(normal, normal));
            if (length > 0.0F && std::abs(rt::y_of(normal)) >= kUpNormal * length) {
                signed_area += rt::y_of(normal) > 0.0F ? length : -length;
            }
        }
    }
    return signed_area < 0.0 ? -1.0F : 1.0F;
}

// A decoded texel's luminance, linear (its bytes display-referred: rt_display.h).
float texel_luma(const std::array<uint8_t, 4>& texel)
{
    constexpr float kByte = 1.0F / 255.0F;
    return (0.2126F * rt::linear_from_display(static_cast<float>(std::get<0>(texel)) * kByte)) +
           (0.7152F * rt::linear_from_display(static_cast<float>(std::get<1>(texel)) * kByte)) +
           (0.0722F * rt::linear_from_display(static_cast<float>(std::get<2>(texel)) * kByte));
}

// (lighting round A3) A material's first texture decoded (texel_decode.h) with its texels' median luminance (linear):
// what the textured candidates are told by. Empty when it can't be read.
struct TextureLook {
    texel::Image image;
    float median = 0.0F;
    uint32_t format = 0;     // (reported) its format ...
    float brightest = 0.0F;  // ... and its brightest texel's luminance
    std::size_t lamp_texels = 0; // ... and its texels that show a lamp (rt_lights.h lamp_texel)
    // (round light cards) its texels' mean colour, display-referred (1 white), each by its alpha - what a surface's vertex
    // colour multiplies (rt::LightCards::surface_colours)
    rt::Rgbf mean{1.0F, 1.0F, 1.0F};
};
const TextureLook& texture_look(const MaterialRef* material, std::unordered_map<const void*, TextureLook>* cache)
{
    static const TextureLook kNone;
    const TextureImage* texture = core::at(material->textures(), 0);
    if (texture == nullptr || texture->texture == nullptr) {
        return kNone;
    }
    const auto [found, made] = cache->try_emplace(texture->texture);
    TextureLook& look = found->second;
    if (!made) {
        return look;
    }
    look.format = decode_first_level(texture->texture, &look.image);
    if (!look.image.empty()) {
        std::vector<float> lumas;
        lumas.reserve(look.image.texels.size());
        for (const std::array<uint8_t, 4>& texel : look.image.texels) {
            lumas.push_back(texel_luma(texel));
        }
        const auto middle = lumas.begin() + static_cast<std::ptrdiff_t>(lumas.size() / 2);
        std::ranges::nth_element(lumas, middle);
        look.median = *middle;
        constexpr float kByte = 1.0F / 255.0F;
        std::array<double, 3> mean_sum{};
        double mean_weight = 0.0;
        for (const std::array<uint8_t, 4>& texel : look.image.texels) {
            const double weight = static_cast<double>(std::get<3>(texel)) * kByte;
            for (std::size_t k = 0; k < 3; ++k) {
                core::at(mean_sum, k) += static_cast<double>(core::at(texel, k)) * kByte * weight;
            }
            mean_weight += weight;
        }
        if (mean_weight > 0.0) {
            for (std::size_t k = 0; k < 3; ++k) {
                core::at(look.mean, k) = static_cast<float>(core::at(mean_sum, k) / mean_weight);
            }
        }
        for (const std::array<uint8_t, 4>& texel : look.image.texels) {
            look.brightest = std::max(look.brightest, texel_luma(texel));
            const std::array<float, 3> linear{rt::linear_from_display(static_cast<float>(std::get<0>(texel)) * kByte),
                                              rt::linear_from_display(static_cast<float>(std::get<1>(texel)) * kByte),
                                              rt::linear_from_display(static_cast<float>(std::get<2>(texel)) * kByte)};
            look.lamp_texels += rt::lamp_texel(linear, look.median) ? 1 : 0;
        }
    }
    return look;
}

// (lighting round A3 - the ceiling lights drawn as textures) A triangle's samples (kTexelSamples over it) that show a
// lamp (rt_lights.h lamp_texel against the texture's own median): their mean texel colour (display-referred, 1 white)
// and their share of the triangle; false when none does.
constexpr uint32_t kTexelSide = 5; // samples along each edge: a triangle's 15 points
bool lamp_texels(const MeshTriangles& tris, const std::array<uint32_t, 3>& corner, const TextureLook& look,
                 std::array<float, 3>* colour, float* share, bool blended)
{
    std::array<std::array<float, 2>, 3> uvs{};
    for (std::size_t k = 0; k < 3; ++k) {
        if (!tris.uv(core::at(corner, k), &core::at(uvs, k))) {
            return false;
        }
    }
    std::array<float, 3> sum{};
    uint32_t lit = 0;
    uint32_t samples = 0;
    for (uint32_t i = 0; i < kTexelSide; ++i) {
        for (uint32_t j = 0; i + j < kTexelSide; ++j) {
            // (the points of a grid inside the triangle, off its edges)
            const float b = (static_cast<float>(i) + (1.0F / 3.0F)) / static_cast<float>(kTexelSide);
            const float c = (static_cast<float>(j) + (1.0F / 3.0F)) / static_cast<float>(kTexelSide);
            const float a = 1.0F - b - c;
            const float u = (a * std::get<0>(std::get<0>(uvs))) + (b * std::get<0>(std::get<1>(uvs))) + (c * std::get<0>(std::get<2>(uvs)));
            const float v = (a * std::get<1>(std::get<0>(uvs))) + (b * std::get<1>(std::get<1>(uvs))) + (c * std::get<1>(std::get<2>(uvs)));
            const std::array<uint8_t, 4> texel = look.image.at(u, v);
            ++samples;
            const std::array<float, 3> shown{static_cast<float>(std::get<0>(texel)) / 255.0F,
                                             static_cast<float>(std::get<1>(texel)) / 255.0F,
                                             static_cast<float>(std::get<2>(texel)) / 255.0F};
            const std::array<float, 3> linear{rt::linear_from_display(std::get<0>(shown)),
                                              rt::linear_from_display(std::get<1>(shown)),
                                              rt::linear_from_display(std::get<2>(shown))};
            // (round indoor lamps) on a blended surface only a texel drawn solid - its alpha at least kCutoutAlpha: a
            // fitting's tube drawn with soft edges (Training's troffers), not the see-through parts round it
            const bool solid = !blended || static_cast<float>(std::get<3>(texel)) / 255.0F >= kCutoutAlpha;
            if (solid && rt::lamp_texel(linear, look.median)) {
                ++lit;
                for (std::size_t k = 0; k < 3; ++k) {
                    core::at(sum, k) += core::at(shown, k);
                }
            }
        }
    }
    if (lit == 0) {
        return false;
    }
    *colour = rt::scaled(sum, 1.0F / static_cast<float>(lit));
    *share = static_cast<float>(lit) / static_cast<float>(samples);
    return true;
}

// (round L) A light card's luminous core: the share of a fine grid of points over the triangle (kCoreSide along each
// edge) whose texel's brightest channel (linear) is at least rt_lights.h kTexelBright - the lamp the card is drawn round,
// saturated in the art; the halo's falling edge is glare, not the lamp. 0 without the texture or its coordinates.
constexpr uint32_t kCoreSide = 16;
float card_core_share(const MeshTriangles& tris, const std::array<uint32_t, 3>& corner, const TextureLook& look)
{
    std::array<std::array<float, 2>, 3> uvs{};
    if (look.image.empty()) {
        return 0.0F;
    }
    for (std::size_t k = 0; k < 3; ++k) {
        if (!tris.uv(core::at(corner, k), &core::at(uvs, k))) {
            return 0.0F;
        }
    }
    uint32_t lit = 0;
    uint32_t samples = 0;
    for (uint32_t i = 0; i < kCoreSide; ++i) {
        for (uint32_t j = 0; i + j < kCoreSide; ++j) {
            const float b = (static_cast<float>(i) + (1.0F / 3.0F)) / static_cast<float>(kCoreSide);
            const float c = (static_cast<float>(j) + (1.0F / 3.0F)) / static_cast<float>(kCoreSide);
            const float a = 1.0F - b - c;
            const float u = (a * std::get<0>(std::get<0>(uvs))) + (b * std::get<0>(std::get<1>(uvs))) + (c * std::get<0>(std::get<2>(uvs)));
            const float v = (a * std::get<1>(std::get<0>(uvs))) + (b * std::get<1>(std::get<1>(uvs))) + (c * std::get<1>(std::get<2>(uvs)));
            const std::array<uint8_t, 4> texel = look.image.at(u, v);
            ++samples;
            const uint8_t most = std::max({std::get<0>(texel), std::get<1>(texel), std::get<2>(texel)});
            lit += rt::linear_from_display(static_cast<float>(most) / 255.0F) >= rt::kTexelBright ? 1 : 0;
        }
    }
    return samples > 0 ? static_cast<float>(lit) / static_cast<float>(samples) : 0.0F;
}

// (round FG) Whether a surface's lamp texels are told by its texels drawn solid alone: a blended surface that is no glow
// and isn't drawn opaque (round indoor lamps - a fitting drawn with a soft-edged texture).
bool lamp_texels_solid_only(const MeshRef* mesh, const MaterialRef* material, const rt::Material& made)
{
    if (additive_first_pass(material)) {
        return false;
    }
    const uint32_t mode = blend_mode(material, 0);
    const bool opaque = (made.flags & (rt::kTransparent | rt::kAlphaTested)) == 0U &&
                        (mode == kBlendNone || mode == static_cast<uint32_t>(kBlendModeVertexTexAlpha));
    // (round A3: a candidate's surface is drawn solid - not blended; an alpha test cuts its holes, not its light)
    // (round PT-B: a blended surface drawn opaque too - rt_surface_class.h)
    const bool solid = (made.flags & rt::kTransparent) == 0U || surface_class_of(mesh, made) == rt::SurfaceClass::kBlendedOpaque;
    return !(opaque || solid);
}

void emitter_work()
{
    State& st = state();
    // (round P10: in the bake, the objects placed now are part of what was scanned - found again when they change)
    const uint64_t key = relit_bake::requested() ? st.scene_key ^ placed_key() : st.scene_key;
    if (st.world.empty() || st.settled_key != st.scene_key || st.emitters_key == key) {
        return;
    }
    st.emitters_key = key;
    const auto start = std::chrono::steady_clock::now();
    rt::LevelEmitters found;
    found.serial = ++st.emitter_sets;
    // (lighting round A3 - no "dark level") the self-lit and textured candidates are the relit bake's: it alone can
    // tell a lamp from a bright surface (by the light the bake shows of it - rt_relit.h solve_relit_lamps); the game
    // shines the data's lamps, or without data the glows alone
    const bool bake = relit_bake::requested();
    found.median_luma = bake ? median_luma() : 0.0F;
    {
        // the level's own time's light (its open sky and its lamps without the clock - rt_lights.h lamp_on)
        const rt::DayLight own = relit_bake::level_own_day(rt::baked_sun());
        found.own_sky = (0.2126F * std::get<0>(own.sky_light)) + (0.7152F * std::get<1>(own.sky_light)) +
                        (0.0722F * std::get<2>(own.sky_light));
        found.own_lamps = own.lamps;
    }
    const float facing = world_facing();
    std::unordered_map<const void*, TextureLook> textures;
    std::size_t blended_read = 0; // (reported: the blended meshes looked at for lamp texels drawn solid, and those
                                  // with no copy to read)
    std::size_t skipped_unread = 0;
    std::vector<rt::EmitterTriangle> triangles;
    // (round light cards - the bake's) the light cards' triangles with their textures, and the surfaces' colours
    auto cards = std::make_shared<rt::LightCards>();
    std::unordered_map<const void*, uint32_t> card_images;
    // (round P10 - objects relit) in the bake, the objects drawn with their own colours where they stand too (world_meshes'
    // objects): a lamp globe's light is in its own colours - a candidate like the level's lamp heads
    struct Scanned {
        const MeshRef* mesh = nullptr;
        const std::array<float, 16>* to_world = nullptr;
        bool other_state = false; // (a light card hidden in the bake's lighting state)
    };
    std::vector<Scanned> scanned;
    for (const MeshRef* mesh : st.world) {
        if (mesh_shown(mesh)) { // (the relit bake's hidden meshes: not lit, no lamp - relit_bake.h)
            scanned.push_back({.mesh = mesh});
        } else if (bake && mesh->material() != nullptr) {
            // (round P10) a light card the level shows in another of its lighting states (its night's pools under the
            // street lamps - Boston's under its globes): hidden in the state the bake is made in, but the artists' mark
            // of a lamp all the same - its lamp found as a shown card's is (a glow, switched by its own daylight); its
            // light isn't in this state's colours, so it is no card laid on a surface.
            // (round SL) Any other mesh hidden in the bake's state too - another state's geometry (Boston's night: its
            // street lamps' lit glass, its lit windows, made by the time of day's scripts): its lamp texels and self-lit
            // colours are candidates the bake can't see lit, a lamp only as the head of one of the game's own lights
            // (rt_relit.h relit_adopt_game_lights)
            scanned.push_back({.mesh = mesh, .other_state = true});
        }
    }
    const std::vector<rt_relit_world::WorldMesh> placed_objects = bake ? world_meshes() : std::vector<rt_relit_world::WorldMesh>{};
    for (const rt_relit_world::WorldMesh& object : placed_objects) {
        if (object.object != nullptr) {
            scanned.push_back({.mesh = object.mesh, .to_world = &object.to_world});
        }
    }
    for (const Scanned& scan : scanned) {
        const MeshRef* mesh = scan.mesh;
        const MaterialRef* material = mesh->material();
        const auto entry = material != nullptr ? st.materials.find(material) : st.materials.end();
        if (entry == st.materials.end() || moving_material(material)) {
            continue;
        }
        const bool glow = additive_first_pass(material);
        found.glow_meshes += glow ? 1 : 0;
        // (round indoor lamps) a blended surface too, in the bake: its lamp texels drawn solid (lamp_texels) - a
        // fitting drawn with a soft-edged texture is a lamp's surface where its texels are opaque (Training's hanging
        // troffers: an alpha-blended mesh, all 617 of its blended meshes passed over before); never by its vertex
        // colours (a blended overlay's are often the white of no light at all - a light cone, a decal)
        const bool blended = lamp_texels_solid_only(mesh, material, entry->second.material);
        if (!glow && !bake) {
            continue;
        }
        blended_read += blended ? 1 : 0;
        const auto mesh_found = st.meshes.find(mesh); // (round relit-misses: the triangles' own mesh)
        std::vector<MeshTriangles> read;
        if (mesh_found == st.meshes.end() || !mesh_triangles(mesh, false, glow, &read)) {
            skipped_unread += 1;
            continue;
        }
        MeshTriangles& tris = read.front();
        if (scan.to_world != nullptr) {
            tris.place(*scan.to_world);
        }
        const std::array<float, 4>& pass = entry->second.material.colour; // (doubled: 1 leaves the texture as it is)
        // (round L: in the bake a light card's texture too - its saturated core is the lamp's luminous area)
        static const TextureLook kNoLook;
        const TextureLook& look = glow && !bake ? kNoLook : texture_look(material, &textures);
        uint32_t card_image = rt::kNoCardImage;
        if (bake && !glow && !look.image.empty()) {
            cards->surface_colours.emplace_back(mesh_found->second.id, look.mean);
        }
        found.other_state_cards += scan.other_state && glow ? 1 : 0;
        if (bake && glow && !look.image.empty()) {
            const void* handle = core::at(material->textures(), 0)->texture;
            const auto [image, added] = card_images.try_emplace(handle, static_cast<uint32_t>(cards->images.size()));
            if (added) {
                cards->images.push_back(std::make_shared<const texel::Image>(look.image));
            }
            card_image = image->second;
        }
        // (round light cards) a card's blend: added by the texel's alpha (blend 1) or by the pass's fixed alpha (blend 2)
        const bool card_texel_alpha = glow && blend_mode(material, 0) == kBlendAdd;
        const float card_factor =
            glow && !card_texel_alpha ? std::clamp(core::at(core::at(material->pass_color(), 0), 3), 0.0F, 1.0F) : 1.0F;
        for (uint32_t t = 0; t < tris.count(); ++t) {
            std::array<uint32_t, 3> corner{};
            if (!tris.corners(t, &corner)) {
                continue;
            }
            const auto [one, two, three] = corner;
            if (bake && glow && !scan.other_state) {
                rt::CardTriangle card{.corners = {tris.position(one), tris.position(two), tris.position(three)},
                                      .image = card_image,
                                      .texel_alpha = card_texel_alpha,
                                      .mesh = mesh_found->second.id};
                for (std::size_t k = 0; k < 3; ++k) {
                    const uint32_t vertex = core::at(corner, k);
                    std::array<float, 2> uv{};
                    if (tris.uv(vertex, &uv)) {
                        core::at(card.uvs, k) = uv;
                    }
                    const std::array<float, 3> colour = tris.colour(vertex);
                    for (std::size_t ch = 0; ch < 3; ++ch) {
                        core::at(core::at(card.colours, k), ch) = core::at(colour, ch) * core::at(pass, ch) * card_factor;
                    }
                    core::at(card.alphas, k) = tris.alpha(vertex);
                }
                cards->triangles.push_back(card);
            }
            rt::EmitterKind kind = rt::EmitterKind::kGlow;
            std::array<float, 3> texture_colour{1.0F, 1.0F, 1.0F};
            float lit_share = glow ? card_core_share(tris, corner, look) : 1.0F;
            if (!glow) {
                if (!blended &&
                    rt::self_lit_triangle(std::min({tris.brightness(one), tris.brightness(two), tris.brightness(three)}),
                                          found.median_luma)) {
                    kind = rt::EmitterKind::kSelfLit;
                } else {
                    // (round A3) a lamp drawn in the texture: not on a floor (facing up by the world's winding - a
                    // bright floor is paint or a baked pool)
                    const rt::Vec3 a = tris.position(one);
                    const rt::Vec3 n = rt::cross(rt::minus(tris.position(two), a), rt::minus(tris.position(three), a));
                    const float length = std::sqrt(rt::dot(n, n));
                    constexpr float kFloor = 0.7F;
                    float share = 0.0F;
                    if (look.image.empty() || !(length > 0.0F) || rt::y_of(n) * facing >= kFloor * length ||
                        !lamp_texels(tris, corner, look, &texture_colour, &share, blended)) {
                        continue;
                    }
                    kind = rt::EmitterKind::kTextured;
                    lit_share = share;
                }
            }
            rt::EmitterTriangle triangle{.corners = {tris.position(one), tris.position(two), tris.position(three)},
                                         .colour = {},
                                         .material = entry->second.id,
                                         .kind = kind,
                                         .lit_share = lit_share,
                                         .mesh = mesh_found->second.id,
                                         .vertices = corner,
                                         .other_state = scan.other_state && !glow};
            for (const uint32_t vertex : corner) {
                const std::array<float, 3> colour = tris.colour(vertex);
                for (std::size_t k = 0; k < 3; ++k) {
                    core::at(triangle.colour, k) +=
                        core::at(colour, k) * core::at(pass, k) * core::at(texture_colour, k) / 3.0F;
                }
            }
            (kind == rt::EmitterKind::kGlow       ? found.glow_triangles
             : kind == rt::EmitterKind::kSelfLit ? found.self_lit_triangles
                                                 : found.textured_triangles) += 1;
            triangles.push_back(triangle);
        }
    }
    if (bake) {
        // (reported) the textures the textured candidates were looked for in
        std::size_t decoded = 0;
        std::size_t with_lamps = 0;
        std::string unread;
        for (const auto& [handle, look] : textures) {
            if (look.image.empty()) {
                if (unread.size() < 80) {
                    unread += text::format(" %08x", look.format);
                }
                continue;
            }
            ++decoded;
            with_lamps += look.lamp_texels > 0 ? 1 : 0;
        }
        get_log("rt: lights: the bake's textured candidates - %zu textures read, %zu decoded (not:%s), %zu with lamp "
                "texels; %zu textured triangles (%zu blended meshes looked at for solid lamp texels; %zu meshes without a copy to read)",
                textures.size(), decoded, unread.empty() ? " none" : unread.c_str(), with_lamps,
                found.textured_triangles, blended_read, skipped_unread);
    }
    found.emitters = rt::cluster_emitters(triangles, &found.triangle_emitter, facing);
    // (lighting round A2 - one lamp model) a level relit from its data shines its data's lamps - their intensities
    // solved from the bake, in the global unit - not the brightness found here (the bake itself finds them)
    if (!relit_bake::requested()) {
        std::vector<rt::Emitter> solved;
        std::vector<uint8_t> surfaced;
        if (rt_relit_world::data_emitters(&solved, &surfaced)) {
            found.emitters = std::move(solved);
            found.surfaced = std::move(surfaced); // (path tracing: the lamps whose own surface emits)
            found.triangle_emitter.clear();
            found.solved = true; // (round O: only the solved lamps shine)
            // (round PX) who owns each shining lamp and how it switches: its own surface (sampled as emitting triangles)
            // or analytic; always on (no sky where it lights) or by a photocell, and the photocells' range
            std::size_t own_surface = 0;
            std::size_t always = 0;
            std::size_t photocell = 0;
            std::size_t by_game = 0;
            float lowest = 0.0F;
            float highest = 0.0F;
            for (std::size_t i = 0; i < found.emitters.size(); ++i) {
                own_surface += i < found.surfaced.size() && core::at(found.surfaced, i) != 0 ? 1 : 0;
                const float sky = core::at(found.emitters, i).switch_sky;
                if (core::at(found.emitters, i).game_light != 0U) {
                    ++by_game; // (round SL: switched by the game)
                } else if (sky >= rt::kLampAlwaysOn) {
                    ++always;
                } else if (sky > 0.0F) {
                    lowest = photocell == 0 ? sky : std::min(lowest, sky);
                    highest = std::max(highest, sky);
                    ++photocell;
                }
            }
            get_log("rt: lights: the relit data's %zu shining lamps - %zu with a surface of their own (emitting triangles), "
                    "%zu analytic; %zu the game's own lights (switched by the game), %zu always on, %zu by a photocell (on "
                    "under an open sky of %.4g - %.4g), %zu by the open sky's lamps",
                    found.emitters.size(), own_surface, found.emitters.size() - own_surface, by_game, always, photocell,
                    static_cast<double>(lowest), static_cast<double>(highest),
                    found.emitters.size() - by_game - always - photocell);
        }
    }
    found.triangles = std::move(triangles); // (lighting round A2: a path tracer's emissive triangles - the relit data's)
    if (bake) {
        found.cards = std::move(cards);
    }
    const auto glows = std::ranges::count_if(found.emitters,
                                             [](const rt::Emitter& e) { return e.kind == rt::EmitterKind::kGlow; });
    const auto points = std::ranges::count_if(rt::frame_record().lights, [](const rt::Light& light) {
        return light.kind == rt::Light::Kind::kPoint;
    });
    get_log("rt: lights: %zu emitters (%td glows from %zu additive triangles in %zu meshes - %zu of them cards of another "
            "lighting state; %td candidates from %zu "
            "self-lit and %zu textured triangles%s; the level's own open sky %.4f, its lamps %.2f) and %td point lights "
            "on now - %.0f ms",
            found.emitters.size(), glows, found.glow_triangles, found.glow_meshes, found.other_state_cards,
            static_cast<std::ptrdiff_t>(found.emitters.size()) - glows, found.self_lit_triangles,
            found.textured_triangles, bake ? " - the bake's" : " - looked for in the bake only",
            static_cast<double>(found.own_sky), static_cast<double>(found.own_lamps), points,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    rt::set_level_emitters(std::move(found));
}

// A colour's r, g, b bytes times a factor.
std::array<float, 3> colour_of(uint32_t rgba, float factor)
{
    std::array<float, 3> out{};
    for (std::size_t k = 0; k < 3; ++k) {
        core::at(out, k) = static_cast<float>((rgba >> (kBitsPerByte * k)) & kByteMask) * factor;
    }
    return out;
}

// The level's ambient colour and directional lights (the scene's: pc_lights.cpp sends them so - a direction negated:
// the way the light travels), and the dynamic point lights that are on (light_manager.h).
void record_lights()
{
    std::vector<rt::Light>& lights = rt::frame_record().lights;
    const LightingState& world = light_sets().world_lighting();
    lights.push_back({.kind = rt::Light::Kind::kAmbient,
                      .colour = colour_of(world.ambient, light_sets().ambient_intensity() * kPerByteLight)});
    const std::size_t count = std::min({static_cast<std::size_t>(light_sets().light_count()), world.directions.size(),
                                        world.colours.size(), light_sets().intensities().size(), kMaxSceneLights});
    for (std::size_t i = 0; i < count; ++i) {
        const maths::Vec4& towards = core::at(world.directions, i);
        const float length = std::sqrt((towards.x * towards.x) + (towards.y * towards.y) + (towards.z * towards.z));
        const float scale = length > 0.0F ? -1.0F / length : 0.0F;
        lights.push_back({.kind = rt::Light::Kind::kDirectional,
                          .colour = colour_of(core::at(world.colours, i),
                                              core::at(light_sets().intensities(), i) * kPerByteLight),
                          .direction = {towards.x * scale, towards.y * scale, towards.z * scale}});
    }
    const auto dynamic = static_cast<std::size_t>(std::max(0, light_sets().dynamic_light_count()));
    for (std::size_t i = 0; i < dynamic && i < light_sets().dynamic_lights().size(); ++i) {
        const DynamicLightBase* light = core::at(light_sets().dynamic_lights(), i);
        if (light != nullptr && light->owner() != 0U && light->unknown2c()) {
            // (round SL) a light node's, on or off: the game's own lights and how bright it has them now
            rt::frame_record().game_lights.push_back(
                {.owner = light->owner(),
                 .position = {core::at(light->pos(), 0), core::at(light->pos(), 1), core::at(light->pos(), 2)},
                 .colour = colour_of(light->colour(), kPerByte),
                 .radius = light->radius(),
                 .strength = std::max(light->intensity(), 0.0F)});
        }
        if (light == nullptr || !(light->intensity() > 0.0F) || light->owner() == 0U) {
            continue;
        }
        lights.push_back({.kind = rt::Light::Kind::kPoint,
                          .colour = colour_of(light->colour(), light->intensity() * kPerByte),
                          .position = {core::at(light->pos(), 0), core::at(light->pos(), 1), core::at(light->pos(), 2)},
                          .radius = light->radius()});
    }
    std::ranges::sort(rt::frame_record().game_lights,
                      [](const rt::GameLight& a, const rt::GameLight& b) { return a.owner < b.owner; });
    {
        // (round SL, reported) how many of the level's light nodes the game has on - logged when that changes (the time
        // of day's transition turning a state's lights on over its frames logs its last count once it settles)
        const std::vector<rt::GameLight>& game = rt::frame_record().game_lights;
        const auto on = static_cast<std::size_t>(
            std::ranges::count_if(game, [](const rt::GameLight& light) { return light.strength > 0.0F; }));
        State& st = state();
        if (on != st.game_lights_on || game.size() != st.game_lights) {
            st.game_lights_on = on;
            st.game_lights = game.size();
            get_log("rt: the game's own lights - %zu of the level's %zu light nodes on now", on, game.size());
        }
    }
}

std::array<std::array<float, 4>, 4> rows_of(const maths::Mat4& matrix)
{
    std::array<std::array<float, 4>, 4> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        const maths::Vec4& row = core::at(matrix.row, i);
        core::at(out, i) = {row.x, row.y, row.z, row.w};
    }
    return out;
}

} // namespace

bool mesh_geometry(const MeshRef* mesh, rt::Geometry* out, const VertexBufferData** source)
{
    return geometry_of(mesh, out, source);
}

uint32_t decode_first_level(GpuTexture* texture, texel::Image* out)
{
    *out = {};
    GpuLevelDesc desc;
    if (texture == nullptr || gpu().texture_level_desc(texture, 0, &desc) != gpu_value::kResultOk) {
        return 0;
    }
    GpuLockedRect locked;
    if (!texel::decodable(desc.format) ||
        gpu().lock_texture(texture, 0, &locked, nullptr, gpu_value::kLockReadOnly) != gpu_value::kResultOk) {
        return desc.format;
    }
    if (locked.bits != nullptr && locked.pitch > 0) {
        constexpr uint32_t kBlockSide = 4;
        const bool block = desc.format == gpu_value::kFormatDxt1 || desc.format == gpu_value::kFormatDxt3 ||
                           desc.format == gpu_value::kFormatDxt5;
        const uint32_t rows = block ? (desc.height + kBlockSide - 1) / kBlockSide : desc.height;
        const std::span<const uint8_t> bytes(static_cast<const uint8_t*>(locked.bits),
                                             static_cast<std::size_t>(rows) * static_cast<uint32_t>(locked.pitch));
        texel::decode(desc.format, desc.width, desc.height, bytes, static_cast<uint32_t>(locked.pitch), out);
    }
    gpu().unlock_texture(texture, 0);
    return desc.format;
}

// (round PG) The clutter's meshes - each part's triangles (scatter::ray_part_triangles) in one buffer of the Gpu's, a mesh
// each - and its materials (each part's colour at each shade, untextured), made the first time they are wanted. False:
// they couldn't be (logged once).
bool clutter_ready(rt::RayTracing& rt)
{
    State::ClutterRays& own = state().clutter;
    if (own.buffer != nullptr) {
        return true;
    }
    if (own.tried) {
        return false;
    }
    own.tried = true;
    const std::vector<scatter::MeshVertex> meshes = scatter::make_meshes();
    std::vector<scatter::MeshVertex> all;
    std::array<std::pair<uint32_t, uint32_t>, scatter::kRayParts> runs{}; // first vertex, vertices
    for (uint32_t part = 0; part < scatter::kRayParts; ++part) {
        const std::vector<scatter::MeshVertex> triangles = scatter::ray_part_triangles(meshes, part);
        core::at(runs, part) = {static_cast<uint32_t>(all.size()), static_cast<uint32_t>(triangles.size())};
        all.insert(all.end(), triangles.begin(), triangles.end());
    }
    const auto bytes = static_cast<uint32_t>(all.size() * sizeof(scatter::MeshVertex));
    void* data = nullptr;
    if (bytes == 0 ||
        gpu().create_vertex_buffer(bytes, gpu_value::kUsageWriteOnly, 0, gpu_value::kPoolDefault, &own.buffer) !=
            gpu_value::kResultOk ||
        own.buffer == nullptr) {
        own.buffer = nullptr;
        get_log("rt: the clutter's meshes couldn't be made - the clutter isn't in the rays");
        return false;
    }
    if (gpu().lock_vertex_buffer(own.buffer, 0, 0, &data, 0) != gpu_value::kResultOk || data == nullptr) {
        gpu().release_vertex_buffer(own.buffer);
        own.buffer = nullptr;
        get_log("rt: the clutter's meshes couldn't be written - the clutter isn't in the rays");
        return false;
    }
    std::memcpy(data, all.data(), bytes);
    gpu().unlock_vertex_buffer(own.buffer);
    constexpr uint32_t kNormalAt = offsetof(scatter::MeshVertex, normal);
    for (uint32_t part = 0; part < scatter::kRayParts; ++part) {
        const auto [first, count] = core::at(runs, part);
        rt::MeshId& id = core::at(own.meshes, part);
        id = 0;
        if (count == 0) {
            continue;
        }
        id = rt::next_mesh_id();
        rt.add_mesh(id, {.vertices = own.buffer,
                         .vertex_offset = first * static_cast<uint32_t>(sizeof(scatter::MeshVertex)),
                         .vertex_stride = sizeof(scatter::MeshVertex),
                         .vertex_count = count,
                         .position_offset = 0,
                         .vertex_buffer_bytes = bytes,
                         .normal_offset = kNormalAt,
                         .normal_format = rt::kNormalFloat3});
        for (uint32_t shade = 0; shade < scatter::kRayShades; ++shade) {
            rt::MaterialId& material = core::at(core::at(own.materials, part), shade);
            if (material != 0) {
                continue; // (the materials outlive a device reset - the table's own)
            }
            const scatter::Vec3 colour = scatter::ray_part_colour(part, shade);
            rt::Material surface;
            surface.colour = {core::at(colour, 0), core::at(colour, 1), core::at(colour, 2), 1.0F};
            material = rt::next_material_id();
            rt.set_material(material, surface);
        }
    }
    get_log("rt: the clutter's meshes made for the rays: %zu triangles in %u parts", all.size() / 3,
            static_cast<unsigned>(scatter::kRayParts));
    return true;
}

// (round PG) The clutter's items within its range of the first viewport's camera (the raster's: kClutterLodDistance x
// [Graphics] lod_distance_scale) - an instance of its part's mesh each (a can's body and label), drawn apart
// (rt::kDrawnApart: a light ray's and a secondary ray's occluder, never a camera ray's), opaque, casting.
void record_clutter(rt::RayTracing& rt)
{
    static build_info::LiveInt wanted{"debug", "rt_clutter", 1}; // (round CS: read again after the game writes the settings file - build_info::LiveInt)
    const bool kWanted = wanted.on();
    State& st = state();
    const std::shared_ptr<const scatter::Scatter> made = scatter_world::current();
    if (!kWanted || made == nullptr || made->items.empty() || !st.camera_known || !clutter_ready(rt)) {
        return;
    }
    const float range = graphics_options::get_scaled(scatter::kClutterLodDistance,
                                                     graphics_options::get_pull().lod_distance_scale_);
    if (st.clutter.placed_for != made) {
        st.clutter.placed_for = made;
        st.clutter.placed.resize(made->items.size());
        for (std::size_t i = 0; i < made->items.size(); ++i) {
            core::at(st.clutter.placed, i) = scatter::ray_item_transform(core::at(made->items, i));
        }
    }
    std::vector<uint32_t>& items = st.clutter.items;
    scatter::ray_items(*made, st.camera, range, &items);
    std::vector<rt::Instance>& out = rt::frame_record().instances;
    out.reserve(out.size() + (items.size() * 2));
    constexpr uint32_t kFlags = rt::kObject | rt::kDrawnApart | rt::kOpaque | rt::kCastsShadow;
    // in runs of one part and shade (one mesh, one material: the TLAS's writer takes a run without a lookup)
    for (std::vector<uint32_t>& run : st.clutter.runs) {
        run.clear();
    }
    for (const uint32_t index : items) {
        const scatter::Item& item = core::at(made->items, index);
        const auto kind = static_cast<scatter::ItemKind>(
            std::min<uint32_t>((item.packed >> scatter::kItemKindShift) & 0xFU, scatter::kItemKinds - 1));
        const uint32_t shade = scatter::ray_shade(item);
        for (const uint32_t part : scatter::ray_parts_of(kind)) {
            core::at(st.clutter.runs, (part * scatter::kRayShades) + shade).push_back(index);
        }
    }
    for (uint32_t part = 0; part < scatter::kRayParts; ++part) {
        const rt::MeshId mesh = core::at(st.clutter.meshes, part);
        for (uint32_t shade = 0; shade < scatter::kRayShades && mesh != 0; ++shade) {
            const rt::MaterialId material = core::at(core::at(st.clutter.materials, part), shade);
            for (const uint32_t index : core::at(st.clutter.runs, (part * scatter::kRayShades) + shade)) {
                out.push_back({.mesh = mesh,
                               .material = material,
                               .flags = kFlags,
                               .object_to_world = core::at(st.clutter.placed, index)});
            }
        }
    }
}

// (round PG) The particles' and billboards' draws of the frame before (rt_apart.h), a mesh per material and vertex
// layout (engine/gfx/rt_apart.h - built anew each frame), each an instance drawn apart with its material's class
// (rt_surface_class.h: a cut-out or solid one an occluder by its alpha, a blended one seen through by its coverage, an
// added one light that passes) - the vertex alpha the least of its vertices'.
void record_apart(rt::RayTracing& rt)
{
    State& st = state();
    std::vector<rt_apart::Draw> draws = rt_apart::take();
    struct Group {
        const MaterialRef* material = nullptr;
        uint32_t fvf = 0;
        uint32_t stride = 0;
        std::vector<uint8_t> vertices;
    };
    std::vector<Group> groups;
    for (rt_apart::Draw& draw : draws) {
        const auto found = std::ranges::find_if(groups, [&draw](const Group& g) {
            return g.material == draw.material && g.fvf == draw.fvf && g.stride == draw.stride;
        });
        if (found == groups.end()) {
            groups.push_back({.material = draw.material, .fvf = draw.fvf, .stride = draw.stride,
                              .vertices = std::move(draw.vertices)});
        } else {
            found->vertices.insert(found->vertices.end(), draw.vertices.begin(), draw.vertices.end());
        }
    }
    std::vector<rt::ApartStore::Batch> batches;
    std::vector<uint32_t> flags;
    std::vector<rt::MaterialId> materials;
    for (const Group& group : groups) {
        rt::Geometry layout;
        layout.vertex_stride = group.stride; // (first: the attributes that fit within it are taken)
        const bool laid_out = rt::set_vertex_attributes(group.fvf, &layout);
        const rt::MaterialId material = laid_out ? material_id(group.material, rt) : 0;
        if (material == 0) {
            static core::NoDestroy<std::unordered_set<uint64_t>> told; // (each format and stride once)
            if (told.get().insert((uint64_t{group.fvf} << 32U) | group.stride).second) {
                get_log("rt: drawn apart - a draw left out of the rays: vertex format %X, stride %u, %s", group.fvf,
                        group.stride, laid_out ? "no material" : "its format has no position the rays read");
            }
            continue;
        }
        const rt::Material& surface = st.materials.at(group.material).material;
        float least_alpha = 1.0F;
        if (layout.colour_format == rt::kColourA8R8G8B8 && layout.colour_offset + sizeof(uint32_t) <= group.stride) {
            uint32_t lowest = kByteMax;
            for (std::size_t at = layout.colour_offset + 3; at < group.vertices.size(); at += group.stride) {
                lowest = std::min<uint32_t>(lowest, core::at(group.vertices, at)); // (the colour word's A byte)
            }
            least_alpha = static_cast<float>(lowest) * kPerByte;
        }
        const rt::SurfaceClass surface_class =
            rt::surface_class(surface, rt::vertex_alpha_counts(surface) ? least_alpha : 1.0F);
        batches.push_back({.key = group.material,
                           .layout_key = (group.fvf * kFnvPrime32) ^ group.stride,
                           .layout = layout,
                           .vertices = group.vertices});
        flags.push_back(rt::kObject | rt::kDrawnApart | rt::surface_instance_flags(surface_class, surface));
        materials.push_back(material);
    }
    std::vector<rt::MeshId> meshes;
    st.apart.commit(batches, gpu(), rt, &meshes);
    // (what is in the rays, now and then: the draws kept, their triangles and meshes, and the clutter's instances)
    constexpr uint64_t kLogEvery = 600;
    if (st.frame % kLogEvery == 0 && (!draws.empty() || !st.clutter.items.empty())) {
        std::size_t triangles = 0;
        for (const rt::ApartStore::Batch& batch : batches) {
            triangles += batch.layout.vertex_stride > 0 ? batch.vertices.size() / batch.layout.vertex_stride / 3 : 0;
        }
        get_log("rt: drawn apart in the rays - %zu particle / billboard draws, %zu triangles in %zu meshes (%zu kept; %llu "
                "made, %llu released ever); %zu clutter items within the range",
                draws.size(), triangles, batches.size(), st.apart.size(),
                static_cast<unsigned long long>(st.apart.made()), static_cast<unsigned long long>(st.apart.released()),
                st.clutter.items.size());
    }
    maths::Mat4 identity;
    identity.reset_to_identity();
    const std::array<std::array<float, 4>, 3> rows = rt::object_to_world_rows(identity.floats());
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        if (core::at(meshes, i) != 0) {
            rt::frame_record().instances.push_back(
                {.mesh = core::at(meshes, i), .material = core::at(materials, i), .flags = core::at(flags, i),
                 .object_to_world = rows});
        }
    }
}

bool on() { return backend() != nullptr; }

void object_lit(const Singleton* instance, const rt::ObjectLight& light)
{
    if (backend() == nullptr) {
        return;
    }
    State& st = state();
    st.object_lights.insert_or_assign(instance, State::Lit{.light = light, .frame = st.frame});
}

void record_frame()
{
    rt::RayTracing* rt = backend();
    if (rt == nullptr) {
        return;
    }
    State& st = state();
    rt::set_screen_probe(&describe_screen_point); // (the console's `rt probe` - rt_relit.h screen_probe)
    rt::set_world_census(&world_census);          // (the console's `rt census` - rt_light_census.h)
    const auto now = std::chrono::steady_clock::now();
    if (st.first_frame_timed) {
        st.first_frame_timed = false;
        get_log("rt: the new world's first frame took %.0f ms (%.0f ms of it its structures and sun)",
                std::chrono::duration<double, std::milli>(now - st.first_frame_start).count(), st.first_frame_work_ms);
    }
    st.first_frame_start = now;
    st.world.clear();
    st.scene_key = kFnvBasis;
    ++st.frame;
    st.recorded = true;
    st.begun = false;
    rt::FrameRecord& record = rt::frame_record();
    record.frame = st.frame;
    record.instances.clear();
    record.lights.clear();
    record.game_lights.clear();
    record.cameras.clear();
    record.object_lights.clear();
    record.poses_key = kFnvBasis;
    // (the objects' shadows: an instance not drawn for a while forgotten - its address may be another's next)
    std::erase_if(st.object_lights, [&st](const auto& kept) { return kept.second.frame + 2 < st.frame; });
    // (round P10: an instance not recorded for a while forgotten - its address may be another's next)
    std::erase_if(st.still, [&st](const auto& kept) { return kept.second.seen + 2 < st.frame; });
    st.placed.clear();
    st.counts = {};
    record_world(*rt);
    for (const Singleton* instance = first_instance(); instance != nullptr; instance = instance->next()) {
        if (instance->active() && instance->viewports() != 0U) {
            record_object(instance, *rt);
        }
    }
    st.poses.end_frame(st.frame, gpu(), *rt); // (the poses of instances gone or not drawn released)
    { // (round PG: the clutter, the particles and the billboards - drawn apart)
        const frame_profile::Scope phase("rt_clutter");
        record_clutter(*rt);
    }
    {
        const frame_profile::Scope phase("rt_apart");
        record_apart(*rt);
    }
    record_lights();
    st.written.clear();
}

std::string object_census(bool detail) { return object_census_text() + (detail ? object_census_detail() : std::string()); }

std::string world_census()
{
    State& st = state();
    // (each bucket: meshes, vertices, and a few materials' checksums as examples), by bucket and the material's kind
    struct Bucket {
        std::size_t meshes = 0;
        std::size_t vertices = 0;
        std::vector<uint32_t> examples;
    };
    std::map<std::string, Bucket> buckets;
    const auto kind_of = [&st](const MeshRef* mesh) -> const char* {
        const MaterialRef* material = mesh->material();
        if (material == nullptr) {
            return "no material";
        }
        const uint32_t first = blend_mode(material, 0);
        if (first == kBlendAdd || first == kBlendFixedAdd) {
            return "additive";
        }
        const auto found = st.materials.find(material);
        if (found == st.materials.end()) {
            return "not registered";
        }
        // (round PT-B: its class for rays - rt_surface_class.h; a blended cut-out named so)
        const rt::SurfaceClass surface = surface_class_of(mesh, found->second.material);
        if (surface == rt::SurfaceClass::kCutOut && (found->second.material.flags & rt::kTransparent) != 0U) {
            return "blended cut-out";
        }
        return rt::surface_class_name(surface);
    };
    const auto add = [&buckets](const std::string& name, const MeshRef* mesh) {
        Bucket& bucket = buckets[name];
        ++bucket.meshes;
        bucket.vertices += mesh->vertex_count();
        const MaterialRef* material = mesh->material();
        const uint32_t checksum = material != nullptr ? material->checksum() : 0U;
        if (bucket.examples.size() < 4 && std::ranges::find(bucket.examples, checksum) == bucket.examples.end()) {
            bucket.examples.push_back(checksum);
        }
    };
    const memory::ListNode* node = engine_parts().scene_list().next();
    while (node != nullptr && !node->is_head()) {
        const auto* scene = static_cast<const PcScene*>(node->data());
        node = node->next();
        const SceneRef* meshes = scene != nullptr ? scene->meshes() : nullptr;
        if (meshes == nullptr) {
            continue;
        }
        for (const MeshRef* mesh : meshes->mesh_run()) {
            if (mesh == nullptr) {
                continue;
            }
            const std::string kind = kind_of(mesh);
            if (scene->is_sky()) {
                add("sky (lit by the sky's own pass) | " + kind, mesh);
                continue;
            }
            if (meshes->unknown40() != 0U) {
                add("scene not drawn as the world | " + kind, mesh);
                continue;
            }
            if (!mesh_shown(mesh)) {
                add("not shown now | " + kind, mesh);
                continue;
            }
            const auto found = st.meshes.find(mesh);
            if (found == st.meshes.end()) {
                add("not traced (no geometry the rays can read) | " + kind, mesh);
                continue;
            }
            const rt_relit_world::WorldMesh world{
                .mesh = mesh, .id = found->second.id, .geometry = &found->second.geometry, .source = found->second.source};
            add(rt_relit_world::census_bucket(world) + " | " + kind, mesh);
        }
    }
    std::string out = "rt census: world: " + rt_relit_world::census_status() + "\n";
    for (const auto& [name, bucket] : buckets) {
        out += text::format("rt census: world mesh %s: %zu meshes, %zu vertices; e.g. materials", name.c_str(),
                            bucket.meshes, bucket.vertices);
        for (const uint32_t checksum : bucket.examples) {
            out += text::format(" %08X", checksum);
        }
        out += "\n";
    }
    return out;
}

uint8_t lamp_texel_code_of(const MeshRef* mesh)
{
    const MaterialRef* material = mesh != nullptr ? mesh->material() : nullptr;
    if (material == nullptr) {
        return rt::lamp_texel_code(rt::kLampTexelMostFloor, false);
    }
    State& st = state();
    const auto entry = st.materials.find(material);
    const bool solid_only = entry != st.materials.end() && lamp_texels_solid_only(mesh, material, entry->second.material);
    const TextureImage* texture = core::at(material->textures(), 0);
    if (texture == nullptr || texture->texture == nullptr) {
        return rt::lamp_texel_code(rt::kLampTexelMostFloor, solid_only);
    }
    const auto [found, made] = st.lamp_medians.try_emplace(texture->texture, -1.0F);
    if (made) {
        std::unordered_map<const void*, TextureLook> one;
        const TextureLook& look = texture_look(material, &one);
        found->second = look.image.empty() ? -1.0F : look.median;
    }
    if (found->second < 0.0F) {
        return rt::lamp_texel_code(rt::kLampTexelMostFloor, solid_only);
    }
    return rt::lamp_texel_code(rt::kTexelOverTexture * found->second, solid_only);
}

std::string describe_screen_point(float across, float down)
{
    rt::RayTracing* rt = backend();
    const rt::FrameRecord& record = rt::frame_record();
    if (rt == nullptr || record.cameras.empty()) {
        return "rt probe: no ray tracing or no camera";
    }
    const rt::Camera& camera = record.cameras.front();
    // (D3D's row-vector view: its columns are the camera's right, up and forward in the world; the projection's
    // diagonal the screen's scale)
    const auto& view = camera.view;
    const auto& projection = camera.projection;
    const float x = ((2.0F * across) - 1.0F) / std::get<0>(std::get<0>(projection));
    const float y = (1.0F - (2.0F * down)) / std::get<1>(std::get<1>(projection));
    // (the view looks along +z when the projection's w takes +z - a left-handed one -, along -z otherwise)
    const float ahead = std::get<3>(std::get<2>(projection)) >= 0.0F ? 1.0F : -1.0F;
    std::array<float, 3> direction{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& row = core::at(view, i);
        core::at(direction, i) = (std::get<2>(row) * ahead) + (std::get<0>(row) * x) + (std::get<1>(row) * y);
    }
    // (round BIAS) the surface under what adds light over it (mask 0x40 - glows, cards, a window's lit panes drawn
    // additively: light passes them whole, the frame shades what is behind) - those reported below as seen in front
    constexpr uint32_t kAddsLightMask = 0x40;
    const rt::ProbeResult found = rt->probe({.origin = camera.position,
                                             .direction = direction,
                                             .max_distance = 100000.0F,
                                             .cull_mask = rt::kAllMasks & ~kAddsLightMask});
    if (!found.traced || !found.hit) {
        return text::format("rt probe %.3f %.3f: %s", static_cast<double>(across), static_cast<double>(down),
                            found.traced ? "miss" : "no scene");
    }
    const float length = std::sqrt(rt::dot(direction, direction));
    const rt::Vec3 point = {std::get<0>(camera.position) + (std::get<0>(direction) * found.distance / length),
                            std::get<1>(camera.position) + (std::get<1>(direction) * found.distance / length),
                            std::get<2>(camera.position) + (std::get<2>(direction) * found.distance / length)};
    const uint32_t flags = found.instance < record.instances.size() ? record.instances[found.instance].flags : 0U;
    std::string out = text::format("rt probe %.3f %.3f: hit %.1f at %.1f %.1f %.1f, instance %u flags %04X (%s), mesh %u, "
                                   "material %u",
                                   static_cast<double>(across), static_cast<double>(down),
                                   static_cast<double>(found.distance), static_cast<double>(std::get<0>(point)),
                                   static_cast<double>(std::get<1>(point)), static_cast<double>(std::get<2>(point)),
                                   found.instance, flags, (flags & rt::kObject) != 0U ? "object" : "world",
                                   static_cast<unsigned>(found.mesh), static_cast<unsigned>(found.material));
    State& st = state();
    for (const auto& [mesh, entry] : st.meshes) {
        if (entry.id == found.mesh) {
            out += text::format(", %u vertices, scene mesh flags %08X", mesh->vertex_count(), mesh->flags());
            // (round PT-B) its class for rays and what decided it
            const auto made = mesh->material() != nullptr ? st.materials.find(mesh->material()) : st.materials.end();
            if (made != st.materials.end()) {
                const rt::Material& surface = made->second.material;
                const rt::RayTracing* tracer = rt::ray_tracing();
                const rt::TexelAlpha texels =
                    tracer != nullptr && surface.texture != nullptr ? tracer->texture_alpha(surface.texture) : rt::TexelAlpha{};
                out += text::format("; for rays: %s (least drawn alpha %.3f: texels' least alpha %u, %u of %u below "
                                    "255, %u below 128%s; least vertex alpha %.3f%s)",
                                    rt::surface_class_name(surface_class_of(mesh, surface)),
                                    static_cast<double>(rt::least_drawn_alpha(surface, least_vertex_alpha(mesh))),
                                    texels.least, texels.below, texels.texels, texels.below_half,
                                    texels.known ? "" : ", not known", static_cast<double>(least_vertex_alpha(mesh)),
                                    rt::vertex_alpha_counts(surface) ? "" : ", not counted");
            }
            break;
        }
    }
    if (const auto* posed = static_cast<const MeshRef*>(st.poses.source_of(found.mesh)); posed != nullptr) {
        out += text::format(", an instance's pose of a skinned mesh (%u vertices, flags %08X)", posed->vertex_count(),
                            posed->flags());
    }
    for (const auto& [material, entry] : st.materials) {
        if (entry.id != found.material) {
            continue;
        }
        const rt::Material& made = entry.material;
        out += text::format("\n  material %08X: reflectivity %.2f class %u mask %u env pass %u", material->checksum(),
                            static_cast<double>(made.reflectivity), static_cast<unsigned>(made.reflection_class),
                            static_cast<unsigned>(made.reflection_mask), static_cast<unsigned>(made.environment_pass));
        const uint32_t count = std::min(material->pass_count(), kPassCount);
        for (uint32_t pass = 0; pass < count; ++pass) {
            const TextureImage* texture = core::at(material->textures(), pass);
            const std::array<float, 4>& colour = core::at(material->pass_color(), pass);
            out += text::format(" | pass %u flags %04X blend %u colour %.2f %.2f %.2f %.2f tex %08X", pass,
                                core::at(material->pass_flags(), pass), core::at(material->blend_modes(), pass),
                                static_cast<double>(core::at(colour, 0)), static_cast<double>(core::at(colour, 1)),
                                static_cast<double>(core::at(colour, 2)), static_cast<double>(core::at(colour, 3)),
                                texture != nullptr ? texture->checksum : 0U);
        }
        break;
    }
    constexpr float kNearby = 96.0F;
    out += "\n  " + rt_relit_world::describe_mesh(found.mesh);
    // (what is drawn over it, seen through: the transparent instances alone - masks 0x80 and 0x40, round PT-B's added
    // light - a decal, a glow, an overlay)
    constexpr uint32_t kSeenThroughMask = 0xC0;
    const rt::ProbeResult over = rt->probe(
        {.origin = camera.position, .direction = direction, .max_distance = 100000.0F, .cull_mask = kSeenThroughMask});
    if (over.traced && over.hit && over.distance <= found.distance + 1.0F) {
        out += text::format("\n  seen through in front (%.1f): mesh %u, material %u", static_cast<double>(over.distance),
                            static_cast<unsigned>(over.mesh), static_cast<unsigned>(over.material));
        for (const auto& [material, entry] : st.materials) {
            if (entry.id == over.material) {
                out += text::format(" %08X, pass 0 blend %u", material->checksum(), core::at(material->blend_modes(), 0));
                break;
            }
        }
        out += "\n  " + rt_relit_world::describe_mesh(over.mesh);
    }
    out += "\n  " + rt_relit_world::describe_near(point, kNearby);
    // (the direct light's diagnosis: the backend's shadows' light, its casters and the clouds' share here)
    const std::string light = rt->describe_light(point);
    get_log("%s", out.c_str());
    if (!light.empty()) {
        get_log("rt probe %.3f %.3f: %s", static_cast<double>(across), static_cast<double>(down), light.c_str());
        out += "\n  " + light;
    }
    return out;
}

void camera_chosen(int viewport)
{
    rt::RayTracing* rt = backend();
    State& st = state();
    if (rt == nullptr || !st.recorded) {
        return;
    }
    const Renderer& gfx = renderer();
    const RenderCamera& camera = gfx.render_camera();
    rt::frame_record().cameras.push_back({.viewport = static_cast<uint32_t>(viewport),
                                          .view = rows_of(gfx.view_matrix()),
                                          .projection = rows_of(gfx.projection_matrix()),
                                          .position = {core::at(camera.position, 0), core::at(camera.position, 1),
                                                       core::at(camera.position, 2)}});
    if (viewport == 0) { // (round PG: the clutter's range is measured from it at the next record)
        st.camera = {core::at(camera.position, 0), core::at(camera.position, 1), core::at(camera.position, 2)};
        st.camera_known = true;
    }
    if (!st.begun) {
        st.begun = true;
        const frame_profile::Scope phase("rt_begin"); // (where a frame's time goes - platform/frame_profile.h)
        rt->begin_frame(rt::frame_record());
        new_world_work(*rt); // (added by the first frame: a new world's structures and sun, before it is drawn)
        relit_work(*rt);
        emitter_work();      // (added by the night lighting)
        if (relit_bake::requested()) { // (the relit bake: the level's data made, then the game closed - relit_bake.h)
            State& st = state();
            relit_bake::step(world_meshes(),
                             !st.world.empty() && st.settled_key == st.scene_key && st.fitted_key == st.scene_key,
                             st.scene_key, *rt);
        }
        rt->prepare_frame();
    }
}

void set_world_passes(bool inside)
{
    state().world_passes = inside;
    rt::set_world_surfaces_drawing(inside); // (round V: the backend keeps these draws for after the visibility)
}

void mesh_drawn(const MeshRef* mesh)
{
    rt::RayTracing* rt = backend();
    if (rt == nullptr || !state().world_passes || !rt->wants_draws()) {
        return;
    }
    const auto found = state().meshes.find(mesh);
    GpuMatrix world{};
    gpu().get_transform(GpuTransform::kWorld, world.data());
    // (a skinned mesh: the drawing instance's pose)
    const rt::MeshId id = mesh->per_vertex_data() != nullptr
                              ? state().poses.find(mesh_pass().current_instance(), mesh)
                              : (found != state().meshes.end() ? found->second.id : 0);
    rt->mesh_drawn(id, rt::object_to_world_rows(world));
}

void mesh_released(const MeshRef* mesh)
{
    rt::RayTracing* rt = backend();
    if (rt == nullptr) {
        return;
    }
    State& st = state();
    const auto found = st.meshes.find(mesh);
    if (found != st.meshes.end()) {
        rt->remove_mesh(found->second.id);
        st.meshes.erase(found);
    }
    st.poses.release_source(mesh, gpu(), *rt);
    rt_relit_world::mesh_released(mesh);
}

void material_released(const MaterialRef* material)
{
    rt_apart::material_released(material); // (round PG: the draws kept with it)
    rt::RayTracing* rt = backend();
    if (rt == nullptr) {
        return;
    }
    State& st = state();
    const auto found = st.materials.find(material);
    if (found != st.materials.end()) {
        rt->remove_material(found->second.id);
        st.materials.erase(found);
    }
}

void vertex_buffer_written(const VertexBufferData* buffer)
{
    if (backend() == nullptr) {
        return;
    }
    State& st = state();
    st.vertex_alpha.erase(buffer); // (round PT-B: its least vertex alpha read again - its colours may have changed)
    if (st.colour_writes > 0) { // (colour-only: no BLAS built again - ColourWrite)
        if (st.colour_writes_told > 0) {
            rt_relit_world::buffer_written(buffer); // (the game's own colours over the relit ones)
        }
        return;
    }
    state().written.insert(buffer);
    rt_relit_world::buffer_written(buffer); // (the game's own write over the relit colours)
}

void vertex_buffer_released(const VertexBufferData* buffer)
{
    state().written.erase(buffer);
    state().vertex_alpha.erase(buffer);
    rt_relit_world::buffer_released(buffer);
}

void forget_meshes()
{
    rt::RayTracing* rt = backend();
    if (rt == nullptr) {
        return;
    }
    State& st = state();
    for (const auto& [mesh, entry] : st.meshes) {
        rt->remove_mesh(entry.id);
    }
    st.meshes.clear();
    st.written.clear();
    st.lamp_medians.clear(); // (round PERF4: the level's textures go with it)
    st.poses.release_all(gpu(), rt); // (the device's buffers go: the poses made again at the next record)
    st.apart.release_all(gpu(), rt);  // (round PG: the draws apart's meshes, and the clutter's - made again)
    if (st.clutter.buffer != nullptr) {
        for (rt::MeshId& id : st.clutter.meshes) {
            if (id != 0) {
                rt->remove_mesh(id);
            }
            id = 0;
        }
        gpu().release_vertex_buffer(st.clutter.buffer);
        st.clutter.buffer = nullptr;
    }
    st.clutter.tried = false;
}

ColourWrite::ColourWrite(Relit relit) : relit_(relit)
{
    ++state().colour_writes;
    state().colour_writes_told += relit_ == Relit::kTell ? 1 : 0;
}
ColourWrite::~ColourWrite()
{
    --state().colour_writes;
    state().colour_writes_told -= relit_ == Relit::kTell ? 1 : 0;
}

} // namespace render::rt_record
