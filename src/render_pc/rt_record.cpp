// NOT ORIGINAL - ray tracing's frame as data: see rt_record.h.

#include "render_pc/rt_record.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ratio>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
#include "engine/gfx/rt_sun.h"
#include "engine/gfx/surface_material.h"
#include "engine/memory/arena_alloc.h"
#include "platform/run_log.h"
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
// The baked sun's fit (engine/gfx/rt_sun.h)
constexpr uint32_t kFvfNormal = 0x10;           // a vertex format's normal (three floats after the position)
constexpr uint32_t kFvfPointSize = 0x20;        // ... its point size (a float)
constexpr uint32_t kFvfDiffuse = 0x40;          // ... its diffuse colour (B, G, R, A bytes) - the baked lighting
constexpr uint32_t kPositionBytes = 12;
constexpr uint32_t kBlendWeightBytes = 4;
constexpr uint32_t kNormalBytes = 12;
constexpr uint32_t kColourBytes = 4;
constexpr uint32_t kStableFrames = 60;          // the world unchanged this many frames: fitted
constexpr float kLumaRed = 0.299F;              // a colour's brightness (Rec. 601 luma)
constexpr float kLumaGreen = 0.587F;
constexpr float kLumaBlue = 0.114F;
constexpr float kHalfAgreed = 0.5F;             // (the log's count of cells where the bake disagrees)
constexpr std::size_t kFitTriangles = 40000;       // triangles looked at for the fit (the upward ones sampled)
constexpr std::size_t kAgreementTriangles = 400000; // ... and for where the bake agrees (one ray a point) ...
constexpr uint32_t kAgreementPoints = 6;            // ... points in each
constexpr float kMinUp = 0.7F;                  // the samples' triangles face up (or down) at least this much
constexpr uint64_t kFnvBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;
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
    // the baked sun's fit: the last frame's world meshes, their signature (their ids), how long it has stood
    std::vector<const MeshRef*> world;
    uint64_t world_signature = 0;
    uint32_t stable_frames = 0;
    uint64_t fitted_signature = 0;
    uint64_t fits = 0;
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
                state().world.push_back(mesh);
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

// --- the baked sun (engine/gfx/rt_sun.h) ------------------------------------------------------------------------------

// Where a vertex's diffuse colour is in a vertex of this format, or false: none (or a position not of three floats).
bool colour_offset(uint32_t fvf, uint32_t* offset)
{
    const uint32_t position = fvf & kPositionMask;
    if (position < kPositionXyz || position > kPositionXyzB5 || (position % 2) != 0 || (fvf & kFvfDiffuse) == 0U) {
        return false;
    }
    uint32_t at = kPositionBytes + (kBlendWeightBytes * ((position - kPositionXyz) / 2));
    at += (fvf & kFvfNormal) != 0U ? kNormalBytes : 0U;
    at += (fvf & kFvfPointSize) != 0U ? static_cast<uint32_t>(sizeof(float)) : 0U;
    *offset = at;
    return true;
}

// A mesh's triangles as the sampler walks them, read from the copies in memory: their count, a triangle's corners, a
// vertex's position and its baked colour's brightness.
class MeshTriangles {
public:
    MeshTriangles(const rt::Geometry& geometry, const VertexBufferData& source, uint32_t colour)
        : geometry_(&geometry),
          vertices_(source.data(), source.size()),
          colour_(colour),
          index_bytes_(geometry.indices_32bit ? sizeof(uint32_t) : sizeof(uint16_t))
    {
    }

    [[nodiscard]] uint32_t count() const
    {
        if (geometry_->strip) {
            return geometry_->index_count >= 3 ? geometry_->index_count - 2 : 0U;
        }
        return geometry_->index_count / 3;
    }
    // The triangle's three vertex indices, or false (degenerate, or outside the vertices).
    bool corners(uint32_t triangle, std::array<uint32_t, 3>* out) const
    {
        const uint32_t first = geometry_->strip ? triangle : triangle * 3;
        *out = {index(first), index(first + 1), index(first + 2)};
        const uint32_t limit =
            std::min(geometry_->vertex_count, static_cast<uint32_t>(vertices_.size() / geometry_->vertex_stride));
        const auto [one, two, three] = *out;
        return one != two && two != three && one != three && one < limit && two < limit && three < limit;
    }
    [[nodiscard]] rt::Vec3 position(uint32_t vertex) const
    {
        rt::Vec3 out{};
        std::memcpy(out.data(), vertices_.subspan(std::size_t{vertex} * geometry_->vertex_stride, sizeof(out)).data(),
                    sizeof(out));
        return out;
    }
    // (the colour's B, G, R bytes as luma)
    [[nodiscard]] float brightness(uint32_t vertex) const
    {
        std::array<uint8_t, kColourBytes> bgra{};
        std::memcpy(bgra.data(),
                    vertices_.subspan((std::size_t{vertex} * geometry_->vertex_stride) + colour_, bgra.size()).data(),
                    bgra.size());
        return (kLumaBlue * static_cast<float>(std::get<0>(bgra))) + (kLumaGreen * static_cast<float>(std::get<1>(bgra))) +
               (kLumaRed * static_cast<float>(std::get<2>(bgra)));
    }

private:
    [[nodiscard]] uint32_t index(uint32_t at) const
    {
        const std::span<const uint8_t> indices(static_cast<const uint8_t*>(geometry_->cpu_indices),
                                               (std::size_t{geometry_->index_offset} + geometry_->index_count) *
                                                   index_bytes_);
        const std::size_t byte = (std::size_t{geometry_->index_offset} + at) * index_bytes_;
        if (index_bytes_ == sizeof(uint32_t)) {
            uint32_t value = 0;
            std::memcpy(&value, indices.subspan(byte, sizeof(value)).data(), sizeof(value));
            return value;
        }
        uint16_t value = 0;
        std::memcpy(&value, indices.subspan(byte, sizeof(value)).data(), sizeof(value));
        return value;
    }

    const rt::Geometry* geometry_;
    std::span<const uint8_t> vertices_;
    uint32_t colour_; // the colour's offset in a vertex
    std::size_t index_bytes_;
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

// The meshes the samples come from: the world's with a copy of their vertices and indices in memory and a diffuse
// colour.
std::vector<MeshTriangles> sampled_meshes()
{
    State& st = state();
    std::vector<MeshTriangles> meshes;
    for (const MeshRef* mesh : st.world) {
        const auto found = st.meshes.find(mesh);
        if (found == st.meshes.end() || found->second.source == nullptr) {
            continue;
        }
        const rt::Geometry& geometry = found->second.geometry;
        const VertexBufferData& source = *found->second.source;
        uint32_t colour = 0;
        if (source.data() == nullptr || geometry.cpu_indices == nullptr || geometry.dynamic ||
            geometry.vertex_stride == 0 || !colour_offset(mesh->fvf(), &colour) ||
            colour + kColourBytes > geometry.vertex_stride) {
            continue;
        }
        meshes.emplace_back(geometry, source, colour);
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
};
std::vector<rt::BakeSample> bake_samples(SampleCount count)
{
    const std::vector<MeshTriangles> meshes = sampled_meshes();
    std::size_t total = 0;
    for (const MeshTriangles& mesh : meshes) {
        total += mesh.count();
    }
    std::vector<rt::BakeSample> samples;
    const std::size_t step = std::max<std::size_t>(1, total / std::max<std::size_t>(1, count.triangles));
    std::size_t next = 0;
    std::size_t walked = 0;
    for (const MeshTriangles& mesh : meshes) {
        const uint32_t triangles = mesh.count();
        for (; next < walked + triangles; next += step) {
            std::array<uint32_t, 3> corner{};
            UpTriangle up;
            if (!mesh.corners(static_cast<uint32_t>(next - walked), &corner) || !up_triangle(mesh, corner, &up)) {
                continue;
            }
            for (uint32_t k = 0; k < count.points; ++k) {
                samples.push_back(point_of(mesh, corner, up, weights_of(k)));
            }
        }
        walked += triangles;
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

// The fit made and logged: the sun, and with one found where the bake agrees.
void make_fit(rt::RayTracing& rt)
{
    const auto start = std::chrono::steady_clock::now();
    rt::SunFit fit = rt::fit_baked_sun(bake_samples({.triangles = kFitTriangles, .points = 1}), rt);
    fit.sun.scene = ++state().fits;
    if (fit.sun.found) {
        const std::vector<rt::BakeSample> dense =
            bake_samples({.triangles = kAgreementTriangles, .points = kAgreementPoints});
        rt::trace_agreement(dense, rt, &fit.sun);
        const auto disagree = std::ranges::count_if(fit.sun.agreement, [](float value) { return value < kHalfAgreed; });
        get_log("rt: the baked sun's agreement: %zu samples, %ux%u cells of %.0f, %td of them where the bake lights "
                "what the sun can't reach (no traced shadow there)",
                dense.size(), fit.sun.columns, fit.sun.rows, static_cast<double>(fit.sun.cell), disagree);
    }
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

// The last frame's world (its TLAS built): once it has stood still kStableFrames frames and isn't the one fitted, the
// baked sun fitted to it; a world that changed loses its fit. Only when the fit is wanted (the shadow pass on).
void fit_sun(rt::RayTracing& rt)
{
    State& st = state();
    uint64_t signature = kFnvBasis;
    for (const MeshRef* mesh : st.world) {
        const auto found = st.meshes.find(mesh);
        signature = (signature ^ (found != st.meshes.end() ? found->second.id : 0U)) * kFnvPrime;
    }
    const bool empty = st.world.empty();
    st.stable_frames = signature == st.world_signature ? st.stable_frames + 1 : 0U;
    st.world_signature = signature;
    if (signature != st.fitted_signature && rt::baked_sun().scene != 0) {
        rt::set_baked_sun({}); // (another world: the old fit no longer holds)
    }
    if (rt::baked_sun_wanted() && !empty && st.stable_frames >= kStableFrames && signature != st.fitted_signature) {
        st.fitted_signature = signature;
        make_fit(rt); // (the samples read the world's meshes as the last frame recorded them: st.world)
    }
    st.world.clear();
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
    fit_sun(*rt);
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
