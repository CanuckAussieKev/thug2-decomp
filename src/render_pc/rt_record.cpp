// NOT ORIGINAL - ray tracing's frame as data: see rt_record.h.

#include "render_pc/rt_record.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/core/checked_index.h"
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
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/surface_material.h"
#include "engine/memory/arena_alloc.h"
#include "render_pc/bitmap_texture.h"
#include "render_pc/geom.h"
#include "render_pc/mesh_list.h"
#include "render_pc/pc_scene.h"
#include "render_pc/sub_mesh.h"
#include "render_pc/vertex_buffers.h"

namespace render::rt_record {

namespace {

constexpr uint32_t kMeshShown = 0x40;           // a mesh's flags: drawn (the scenes' passes test it)
constexpr uint32_t kMeshHidden = 0x210;         // ... and not with these
constexpr uint32_t kVertexBufferShare = 4;      // a vertex buffer's flags: a share of an arena's buffer
constexpr uint32_t kPassTransparent = 0x40;     // a material's first pass: drawn with the transparent meshes
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
constexpr int kBitsPerByte = 8;
constexpr uint32_t kByteMask = 0xFF;
constexpr std::size_t kMaxSceneLights = 31;     // update_all_lights' limit
constexpr uint32_t kFvfTexCountMask = 0xF00;    // a vertex format's count of texture coordinate sets
constexpr uint32_t kPassEnvironment = 0x8;      // a material pass's flags: environment mapped (pc_material.cpp)
constexpr uint32_t kPassCount = 4;              // a material's passes, at most
constexpr uint32_t kBlendFixedFirst = 2;        // the blend modes taking the pass's fixed alpha: 2, 4, 6, 8, 10
constexpr uint32_t kBlendFixedLast = 0xA;

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
    std::unordered_map<const MaterialRef*, MaterialEntry> materials;
    std::unordered_set<const VertexBufferData*> written; // since the last record
    uint64_t frame = 0;
    bool recorded = false; // record_frame ran this frame (begin_frame not yet)
    bool begun = false;    // begin_frame called this frame
    bool world_passes = false;
    int colour_writes = 0;
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
           a.colour_format == b.colour_format;
}

bool same_material(const rt::Material& a, const rt::Material& b)
{
    return a.texture == b.texture && a.uv_set == b.uv_set && a.blend == b.blend && a.alpha_ref == b.alpha_ref &&
           a.colour == b.colour && a.flags == b.flags && a.address_u == b.address_u && a.address_v == b.address_v &&
           a.filter == b.filter && a.reflectivity == b.reflectivity && a.environment_pass == b.environment_pass;
}

// The vertex buffer a mesh was last drawn from (MeshRef::render: the current one, then the next is current).
VertexBuffer* last_drawn_buffer(const MeshRef* mesh)
{
    const uint32_t count = mesh->vertex_buffer_count();
    const uint32_t current = mesh->current_vertex_buffer_;
    const uint32_t index = count > 1 ? (current + count - 1) % count : current;
    return mesh->vertex_store(index);
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
        // (the texture coordinates and the colour at the mesh's offsets - 0: none, as texture_splats.cpp reads them -
        // where its vertex format has them)
        .uv_offset = mesh->uv_offset(),
        .uv_format = mesh->uv_offset() != 0 && (mesh->fvf() & kFvfTexCountMask) != 0U ? rt::kUvFloat2
                                                                                        : rt::kAttributeNone,
        .colour_offset = mesh->color_offset(),
        .colour_format = mesh->color_offset() != 0 && (mesh->fvf() & kFvfDiffuse) != 0U ? rt::kColourA8R8G8B8
                                                                                         : rt::kAttributeNone,
    };
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

// How strongly a material mirrors its surroundings (R5's reflections): its first environment-mapped pass with a
// texture (the game's shiny surfaces) - the brightest channel of the pass's colour (doubled, as drawn), times its fixed
// alpha where the pass's blend mode takes it - or none.
void reflectivity_of(const MaterialRef* material, rt::Material* out)
{
    const uint32_t passes = std::min(material->pass_count(), kPassCount);
    for (uint32_t pass = 0; pass < passes; ++pass) {
        if ((core::at(material->pass_flags(), pass) & kPassEnvironment) == 0U ||
            core::at(material->textures(), pass) == nullptr) {
            continue;
        }
        const std::array<float, 4>& colour = core::at(material->pass_color(), pass);
        float strength = std::max({core::at(colour, 0), core::at(colour, 1), core::at(colour, 2)}) * kDoubled;
        const uint32_t mode = core::at(material->blend_modes(), pass) & static_cast<uint32_t>(kBlendModeMask);
        if (mode >= kBlendFixedFirst && mode <= kBlendFixedLast && (mode % 2) == 0) {
            strength *= core::at(colour, 3);
        }
        out->reflectivity = std::isfinite(strength) ? std::clamp(strength, 0.0F, 1.0F) : 0.0F;
        out->environment_pass = pass;
        return;
    }
}

// A material as a hit shader reads it: its first pass.
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
    } else if (material->alpha_ref() != 0U) {
        out.flags |= rt::kAlphaTested;
    }
    reflectivity_of(material, &out);
    return out;
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

// One instance of a mesh with a world transform (a Direct3D matrix).
void add_instance(const MeshRef* mesh, uint32_t kind, const maths::Mat4& world, rt::RayTracing& rt)
{
    const rt::MeshId id = mesh_id(mesh, rt);
    if (id == 0) {
        return;
    }
    const rt::MaterialId material = material_id(mesh->material(), rt);
    uint32_t flags = kind;
    const uint32_t surface = material != 0 ? state().materials.at(mesh->material()).material.flags : 0U;
    if ((surface & rt::kTransparent) != 0U) {
        flags |= rt::kTransparent;
    } else if ((surface & rt::kAlphaTested) != 0U) {
        flags |= rt::kAlphaTested | rt::kCastsShadow;
    } else {
        flags |= rt::kOpaque | rt::kCastsShadow;
    }
    if (mesh->per_vertex_data() != nullptr) {
        flags |= rt::kSkinned;
    }
    rt::frame_record().instances.push_back(
        {.mesh = id, .material = material, .flags = flags, .object_to_world = rt::object_to_world_rows(world.floats())});
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
        for (const MeshRef* mesh : meshes->mesh_run()) {
            if (mesh != nullptr && mesh_shown(mesh)) {
                add_instance(mesh, rt::kWorld, identity, rt);
            }
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
            add_instance(mesh, rt::kObject, world, rt);
        }
        return;
    }
    const bool palette = instance->bones() != nullptr && bones_on_device <= 0 && bone_count > 0;
    for (const MeshRef* mesh : scene->mesh_run()) {
        if (mesh == nullptr || !mesh_shown(mesh)) {
            continue;
        }
        if (mesh->unknown98 != kNoBone && palette && mesh->unknown98 >= 0 && mesh->unknown98 < bone_count) {
            const std::span<const maths::Mat4> bones(instance->bones(), static_cast<std::size_t>(bone_count));
            maths::Mat4 world;
            maths::matrix_multiply(&world, &core::at(bones, static_cast<std::size_t>(mesh->unknown98)), &transform);
            add_instance(mesh, rt::kObject, world, rt);
        } else {
            add_instance(mesh, rt::kObject, transform, rt);
        }
    }
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
        if (light == nullptr || !(light->intensity() > 0.0F) || light->owner() == 0U) {
            continue;
        }
        lights.push_back({.kind = rt::Light::Kind::kPoint,
                          .colour = colour_of(light->colour(), light->intensity() * kPerByte),
                          .position = {core::at(light->pos(), 0), core::at(light->pos(), 1), core::at(light->pos(), 2)},
                          .radius = light->radius()});
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

bool on() { return backend() != nullptr; }

void record_frame()
{
    rt::RayTracing* rt = backend();
    if (rt == nullptr) {
        return;
    }
    State& st = state();
    ++st.frame;
    st.recorded = true;
    st.begun = false;
    rt::FrameRecord& record = rt::frame_record();
    record.frame = st.frame;
    record.instances.clear();
    record.lights.clear();
    record.cameras.clear();
    record_world(*rt);
    for (const Singleton* instance = first_instance(); instance != nullptr; instance = instance->next()) {
        if (instance->active() && instance->viewports() != 0U) {
            record_object(instance, *rt);
        }
    }
    record_lights();
    st.written.clear();
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
    if (!st.begun) {
        st.begun = true;
        rt->begin_frame(rt::frame_record());
    }
}

void set_world_passes(bool inside) { state().world_passes = inside; }

void mesh_drawn(const MeshRef* mesh)
{
    rt::RayTracing* rt = backend();
    if (rt == nullptr || !state().world_passes || !rt->wants_draws()) {
        return;
    }
    const auto found = state().meshes.find(mesh);
    GpuMatrix world{};
    gpu().get_transform(GpuTransform::kWorld, world.data());
    rt->mesh_drawn(found != state().meshes.end() ? found->second.id : 0, rt::object_to_world_rows(world));
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
}

void material_released(const MaterialRef* material)
{
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
    if (backend() == nullptr || state().colour_writes > 0) {
        return;
    }
    state().written.insert(buffer);
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
}

ColourWrite::ColourWrite() { ++state().colour_writes; }
ColourWrite::~ColourWrite() { --state().colour_writes; }

} // namespace render::rt_record
