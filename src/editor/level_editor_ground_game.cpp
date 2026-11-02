// NOT ORIGINAL - the level editor's raised / lowered ground: the game side (ours). See level_editor_ground.h.

#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <utility>
#include <array>

#include "engine/gfx/engine_geom.h"
#include "engine/core/checksums.h"
#include "engine/core/mem.h"
#include "editor/level_editor_ground.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scenery.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/sector.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "platform/run_log.h"
#include "platform/crt.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/core/down_cast.h"
#include "gpu/vertex_view.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace frontend::level_editor::ground {

namespace {

render::PcGeom* geom_of(render::WorldCell* sector) { return static_cast<render::PcGeom*>(sector->geom_handle_); }
gsl::owner<render::HitMesh*> coll_of(render::WorldCell* sector) { return sector->hit_body_; }
render::MeshRef** meshes_of(render::PcGeom* geom) { return geom->meshes; }
uint32_t mesh_count_of(render::PcGeom* geom) { return geom->mesh_count_; }
uint32_t vertex_format(const render::MeshRef* mesh) { return mesh->fvf; }

render::WorldCell* lookup_sector(render::WorldScene* scene, uint32_t checksum)
{
    return scene ? static_cast<render::WorldCell*>(scene->look_up_sector(checksum)) : nullptr;
}
// WorldCell::change_live (0x0049E9B0) / is_operative (0x0049E9E0): drawn and collided with, or neither.
void change_live(render::WorldCell* sector, bool on) { sector->change_live(on); }
bool is_operative(render::WorldCell* sector) { return sector->is_operative(); }

render::StaticCollider* static_of(render::WorldCell* sector)
{
    gsl::owner<render::HitMesh*> coll = coll_of(sector);
    if (!render::is_static_collider(coll))
        return nullptr;
    return core::down_cast<render::StaticCollider>(coll);
}

// The buffers as MeshRef::Replicate (sub_mesh.cpp) makes them: a vertex buffer (0x004D5760: its memory copy and the card's) and
// an index buffer (the C runtime's malloc 0x00624031, constructor 0x004D4270).
gsl::owner<render::VertexBuffer*> new_vertex_buffer(uint32_t size) { return render::new_vertex_buffer(size, 0, 0); }
void upload_vertices(render::VertexBuffer* buffer)
{
    buffer->lock_offset_ = 0;
    buffer->lock_size_ = buffer->size;
    buffer->lock_flags_ = 0;
    buffer->get_unlock();
}
gsl::owner<render::IdxStore*> new_index_buffer(uint32_t bytes)
{
    gsl::owner<void*> memory = crt::alloc_block(0x18);
    if (!memory)
        return nullptr;
    return memory::construct_in<render::IdxStore>(memory, bytes, 0, 0x65);
}
void upload_indices(render::IdxStore* buffer)
{
    auto* d3d = static_cast<IDirect3DIndexBuffer9*>(buffer->buffer);
    void* locked = nullptr;
    if (!d3d || FAILED(d3d->Lock(0, 0, &locked, 0)))
        return;
    std::memcpy(locked, buffer->indices, buffer->size);
    d3d->Unlock();
}

// --- state ---

std::vector<Spec> g_specs;
struct Made {
    uint32_t sector = 0; // the sector made for it (Checksums(name + "_" + material) - a new donor: a new copy)
    Spec built;
};
std::map<std::string, Made>& made_state()
{
    static std::map<std::string, Made> value; // (the pieces in place)
    return value;
}
std::set<uint32_t>& pool()
{
    static std::set<uint32_t> value; // (every sector made this level - kept, hidden, when its piece goes)
    return value;
}
std::map<uint32_t, int>& tile_hides()
{
    static std::map<uint32_t, int> value; // (tiles hidden by pits, and by how many)
    return value;
}
render::WorldScene* g_scene;
uint32_t g_active_scene_key;

uint32_t sector_key_from_spec(const Spec& spec)
{
    std::array<char, 96> text{};
    std::snprintf(text.data(), sizeof(text), "%s_%08X", spec.name.c_str(), spec.material);
    return checksums::hash_from_text(text.data());
}

// A new level (another scene): what the editor knew of the last one forgotten (its sectors are gone).
void note_level()
{
    render::WorldScene* scene = scenery::main_scene();
    const uint32_t name = scenery::world_scene_key(scene);
    if (scene == g_scene && name == g_active_scene_key)
        return;
    g_scene = scene;
    g_active_scene_key = name;
    g_specs.clear();
    made_state().clear();
    pool().clear();
    tile_hides().clear();
}

void hide_tiles(const std::vector<uint32_t>& tiles, bool hide)
{
    for (const uint32_t tile : tiles) {
        render::WorldCell* sector = scenery::sector_of(tile);
        if (!sector)
            continue;
        int& count = tile_hides()[tile];
        if (hide) {
            if (count++ == 0)
                change_live(sector, false);
            // NOLINTNEXTLINE(bugprone-inc-dec-in-conditions): counted down and tested in one step, on purpose
        } else if (count > 0 && --count == 0) {
            change_live(sector, true);
        }
    }
}

struct GroundStep : ModuleStep {
    std::vector<Spec> specs;
    explicit GroundStep(std::vector<Spec> s) : specs(std::move(s)) {}
    void get_restore() override
    {
        std::string message;
        apply_to_each(specs, &message);
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override { return std::make_shared<GroundStep>(g_specs); }
};

// The generated piece written into a copy of its donor (see the header). False and why.
bool emit_out(render::WorldCell* sector, const Spec& spec, const Mesh& mesh, std::string* why)
{
    render::PcGeom* geom = geom_of(sector);
    if (!render::is_pc_geom(geom) || mesh_count_of(geom) == 0) {
        *why = "the copy has no drawn geometry";
        return false;
    }
    render::MeshRef* const* meshes = meshes_of(geom);
    render::MeshRef* first = meshes[0];
    Layout layout;
    if (!first || !layout_for(vertex_format(first), first->stride, &layout) || first->vertex_buffer_count_ < 1 ||
        first->vertex_buffer_count_ > 3) {
        *why = "the copy's vertex format can't be written";
        return false;
    }
    render::StaticCollider* coll = static_of(sector);
    if (!coll || !coll->geometry_ || !(coll->flag_bits_ & render::HitMesh::kOwnsGeometry)) {
        *why = "the copy has no collision of its own";
        return false;
    }
    std::vector<uint16_t> strip = get_strip(mesh.faces, true);
    if (mesh.faces.empty() || mesh.faces.size() > static_cast<size_t>(kMaxFaces) || mesh.vertices.size() > 0xFFFF ||
        strip.size() > 0xFFFF) {
        *why = "the piece has too many faces (at most 2048)";
        return false;
    }
    auto count = static_cast<uint32_t>(mesh.vertices.size());
    // The render: new vertex buffers (each of the mesh's) and index buffers (each level of detail it draws).
    std::vector<uint8_t> data(static_cast<size_t>(count) * first->stride);
    write_vertices(mesh, layout, first->stride, spec.colour, data.data());
    for (int b = 0; std::cmp_less(b, first->vertex_buffer_count_); ++b) {
        gsl::owner<render::VertexBuffer*> buffer = new_vertex_buffer(static_cast<uint32_t>(data.size())); // (the mesh's)
        if (!buffer || !buffer->data) {
            *why = "no vertex buffer";
            return false;
        }
        std::memcpy(buffer->data, data.data(), data.size());
        upload_vertices(buffer);
        core::at(first->vertex_buffers_, b) = buffer;
    }
    first->vertex_count_ = static_cast<uint16_t>(count);
    for (int k = 0; k < 8; ++k) {
        if (k > 0 && !core::at(first->index_counts_, k))
            continue;
        gsl::owner<render::IdxStore*> buffer = new_index_buffer(static_cast<uint32_t>(strip.size() * 2));
        if (!buffer || !buffer->indices) {
            *why = "no index buffer";
            return false;
        }
        std::memcpy(buffer->indices, strip.data(), strip.size() * 2);
        upload_indices(buffer);
        core::at(first->index_buffers_, k) = buffer;
        core::at(first->index_counts_, k) = static_cast<uint16_t>(strip.size());
    }
    if (first->per_vertex_bytes_) {
        first->per_vertex_bytes_ = static_cast<uint8_t*>(memory::alloc(count));
        if (first->per_vertex_bytes_)
            std::memset(first->per_vertex_bytes_, 0, count);
    }
    first->position_count_ = 0; // (the copied lists aren't the piece's)
    first->triangle_count_ = 0;
    const V3 centre = (mesh.min + mesh.max) * 0.5f;
    const V3 half = mesh.max - centre;
    core::at(first->centre, 0) = centre.x, core::at(first->centre, 1) = centre.y, core::at(first->centre, 2) = centre.z;
    first->radius = std::sqrt(get_dot(half, half));
    // The other meshes: nothing drawn (their strips all one index - degenerate).
    for (uint32_t i = 1; i < mesh_count_of(geom); ++i) {
        render::MeshRef* other = meshes[i];
        if (!other)
            continue;
        for (int k = 0; k < 8; ++k) {
            render::IdxStore* buffer = core::at(other->index_buffers_, k);
            if (!core::at(other->index_counts_, k) || !buffer || !buffer->indices)
                continue;
            if (std::any_of(buffer->indices, buffer->indices + core::at(other->index_counts_, k),
                            [](uint16_t v) { return v != 0; })) {
                std::memset(buffer->indices, 0, core::at(other->index_counts_, k) * 2u);
                upload_indices(buffer);
            }
        }
        core::at(other->centre, 0) = centre.x, core::at(other->centre, 1) = centre.y, core::at(other->centre, 2) = centre.z;
        other->radius = 1.0f;
    }
    geom->bounds.min.x = mesh.min.x, geom->bounds.min.y = mesh.min.y, geom->bounds.min.z = mesh.min.z;
    geom->bounds.max.x = mesh.max.x, geom->bounds.max.y = mesh.max.y, geom->bounds.max.z = mesh.max.z;

    // The collision: new arrays (memory::alloc - the game frees them with memory::Dealloc, TriMeshSource::destruct), floats,
    // 10-byte faces, one leaf listing every face.
    render::TriMeshSource* tri = coll->geometry_;
    auto faces = static_cast<uint32_t>(mesh.faces.size());
    auto* vertices = static_cast<float*>(memory::alloc(count * 12));
    auto* face_data = static_cast<uint8_t*>(memory::alloc(faces * 10));
    render::HitTreeNode* leaf = render::new_tree_nodes(1);
    auto* leaf_faces = static_cast<uint16_t*>(memory::alloc(faces * 2));
    uint8_t* bytes = tri->vertex_bytes_ ? static_cast<uint8_t*>(memory::alloc(count)) : nullptr;
    if (!vertices || !face_data || !leaf || !leaf_faces || (tri->vertex_bytes_ && !bytes)) {
        *why = "out of memory for the collision";
        return false;
    }
    for (uint32_t v = 0; v < count; ++v)
        std::memcpy(vertices + 3 * v, &core::at(mesh.vertices, v).pos, 12);
    for (uint32_t f = 0; f < faces; ++f) {
        const Face& face = core::at(mesh.faces, f);
        std::array<uint16_t, 5> words = {{face.flags, spec.terrain, face.a, face.b, face.c}};
        std::memcpy(face_data + 10 * f, words.data(), 10);
        leaf_faces[f] = static_cast<uint16_t>(f);
    }
    if (bytes)
        std::memset(bytes, 0x80, count);
    if (tri->vertices_)
        memory::dealloc(tri->vertices_);
    if (tri->faces_)
        memory::dealloc(tri->faces_);
    if (tri->vertex_bytes_)
        memory::dealloc(tri->vertex_bytes_);
    tri->delete_bsp();
    tri->vertices_ = vertices;
    tri->faces_ = face_data;
    tri->vertex_bytes_ = bytes;
    tri->num_vertices_ = static_cast<uint16_t>(count);
    tri->tri_count_ = static_cast<uint16_t>(faces);
    tri->small_faces_ = false;
    tri->compressed_ = false;
    tri->bounds_.min.x = mesh.min.x, tri->bounds_.min.y = mesh.min.y, tri->bounds_.min.z = mesh.min.z;
    tri->bounds_.max.x = mesh.max.x, tri->bounds_.max.y = mesh.max.y, tri->bounds_.max.z = mesh.max.z;
    tri->unknown3c_ = 0; // (no face skipped by its distance)
    // (new HitTreeNode[1]: one leaf of every face)
    leaf->make_leaf(static_cast<uint16_t>(faces), leaf_faces);
    tri->bsp_ = leaf;
    tri->set_up_all_faces();
    // (the face-rejection bytes: one a face, on the bottom-up heap as StaticCollider::Replicate keeps them)
    if (coll->octets_)
        memory::free_malloc(coll->octets_);
    memory::enter_heap_scope(memory::bottom_up_heap());
    coll->octets_ = static_cast<uint8_t*>(memory::alloc_block(faces));
    memory::leave_heap_scope();
    if (coll->octets_)
        std::memset(coll->octets_, 0xFF, faces);
    return true;
}

// The piece put in place: its sector made (or one made before reused) and written, shown; its pit's tiles hidden.
bool get_make(const Spec& spec, std::string* why)
{
    render::WorldScene* scene = scenery::main_scene();
    if (!scene) {
        *why = "no level";
        return false;
    }
    const Mesh mesh = get_build(spec);
    if (mesh.faces.empty()) {
        *why = "the piece has no faces";
        return false;
    }
    uint32_t name = sector_key_from_spec(spec);
    render::WorldCell* sector = lookup_sector(scene, name);
    if (sector && !pool().count(name)) {
        *why = "a sector of that name is the level's";
        return false;
    }
    if (!sector) {
        if (const char* no = why_not_donor(spec.material)) {
            *why = std::string("its material's piece can't be copied: ") + no;
            return false;
        }
        const uint32_t made = scene->clone_sector_named(spec.material, scene, 0, true, name);
        sector = made ? lookup_sector(scene, made) : nullptr;
        if (!sector) {
            *why = "the material's piece wasn't copied";
            return false;
        }
        pool().insert(made);
        name = made;
    }
    if (!emit_out(sector, spec, mesh, why)) {
        change_live(sector, false);
        return false;
    }
    change_live(sector, true);
    scenery::note_collision_moved(name);
    scenery::finalise();
    // (a piece already there under this name: its old sector hidden when it was another, its tiles shown again)
    auto it = made_state().find(spec.name);
    if (it != made_state().end()) {
        if (it->second.sector != name)
            if (render::WorldCell* old = lookup_sector(scene, it->second.sector))
                change_live(old, false);
        hide_tiles(it->second.built.hides, false);
    }
    hide_tiles(spec.hides, true);
    made_state()[spec.name] = {name, spec};
    get_log("level editor: ground piece %s (%s, %.0f x %.0f, %.0f %s, %u faces) in place - material %s",
            spec.name.c_str(), spec.lower ? "pit" : "platform", spec.x1 - spec.x0, spec.z1 - spec.z0, spec.height,
            spec.lower ? "deep" : "high", static_cast<unsigned>(mesh.faces.size()),
            object_edit::caption(spec.material).c_str());
    return true;
}

void unmake(const std::string& piece_name)
{
    auto it = made_state().find(piece_name);
    if (it == made_state().end())
        return;
    if (render::WorldCell* sector = lookup_sector(scenery::main_scene(), it->second.sector))
        change_live(sector, false);
    hide_tiles(it->second.built.hides, false);
    made_state().erase(it);
}

} // namespace

const std::vector<Spec>& get_all() { return g_specs; }

bool is_piece(uint32_t sector) { return pool().count(sector) != 0; }

const Spec* get_find(const std::string& name)
{
    for (const Spec& s : g_specs) {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

void kickoff() { note_level(); }
void halt() {}

const char* why_not_donor(uint32_t checksum)
{
    render::WorldScene* scene = scenery::main_scene();
    render::WorldCell* sector = lookup_sector(scene, checksum);
    if (!sector)
        return "not a piece of the level's main scene";
    if (pool().count(checksum))
        return "a generated piece";
    render::PcGeom* geom = geom_of(sector);
    if (!render::is_pc_geom(geom))
        return "nothing drawn";
    if (geom->instance)
        return "an instance";
    const uint32_t meshes = mesh_count_of(geom);
    if (!meshes_of(geom) || meshes == 0 || meshes > 64)
        return "no meshes (or too many)";
    const render::MeshRef* first = meshes_of(geom)[0];
    Layout layout;
    if (!first || first->billboard || (first->flags & 0x11))
        return "a billboard or shared mesh";
    if (!layout_for(vertex_format(first), first->stride, &layout) || layout.tex_count_ < 1)
        return "its vertex format (no plain positions and texture coordinates)";
    if (first->vertex_buffer_count_ < 1 || first->vertex_buffer_count_ > 3 || !core::at(first->vertex_buffers_, 0) ||
        !core::at(first->vertex_buffers_, 0)->data)
        return "its vertex buffers";
    if (first->per_vertex_data_)
        return "per-vertex data the editor can't make";
    const render::MaterialRef* material = first->material;
    if (!material || picking::material_see_through(core::at(material->pass_flags_, 0), core::at(material->blend_modes_, 0)))
        return "a see-through material";
    const render::StaticCollider* coll = static_of(sector);
    if (!coll || !coll->geometry_ || !coll->geometry_->tri_count_)
        return "no collision";
    return nullptr;
}

bool choose_material(uint32_t wanted, Spec* spec, std::string* why)
{
    note_level();
    uint32_t chosen = wanted && !why_not_donor(wanted) ? wanted : 0;
    if (!chosen) {
        // (the nearest piece that can lend its material - a floor-like one, its top near the ground, first)
        struct Walk {
            const Spec* spec{};
            uint32_t best = 0, best_floor_ = 0;
            float best_distance_ = 1e30f, best_floor_distance_ = 1e30f;
        } walk = {spec};
        scenery::for_each_level_sector(
            [](void* context, uint32_t checksum, render::WorldCell* sector) {
                auto* w = static_cast<Walk*>(context);
                if (!is_operative(sector) || why_not_donor(checksum))
                    return;
                const render::TriMeshSource* tri = static_of(sector)->geometry_;
                const V3 c = {(w->spec->x0 + w->spec->x1) * 0.5f, w->spec->base, (w->spec->z0 + w->spec->z1) * 0.5f};
                const V3 lo = {tri->bounds_.min.x, tri->bounds_.min.y, tri->bounds_.min.z};
                const V3 hi = {tri->bounds_.max.x, tri->bounds_.max.y, tri->bounds_.max.z};
                const V3 nearest = {std::clamp(c.x, lo.x, hi.x), std::clamp(c.y, lo.y, hi.y),
                                    std::clamp(c.z, lo.z, hi.z)};
                const V3 d = nearest - c;
                const float distance = std::sqrt(get_dot(d, d));
                if (distance < w->best_distance_)
                    w->best = checksum, w->best_distance_ = distance;
                if (std::fabs(hi.y - c.y) < 30.0f && distance < w->best_floor_distance_)
                    w->best_floor_ = checksum, w->best_floor_distance_ = distance;
            },
            &walk);
        chosen = walk.best_floor_ ? walk.best_floor_ : walk.best;
    }
    if (!chosen) {
        *why = wanted ? std::string("that piece can't lend its material (") + why_not_donor(wanted) +
                            ") and no other piece of the level can"
                      : "no piece of the level can lend its material";
        return false;
    }
    spec->material = chosen;
    // What it looks like: its first mesh's average colour, how many units a texture repeat spans, its commonest terrain.
    render::WorldCell* sector = lookup_sector(scenery::main_scene(), chosen);
    const render::MeshRef* mesh = meshes_of(geom_of(sector))[0];
    Layout layout;
    layout_for(vertex_format(mesh), mesh->stride, &layout);
    const uint8_t* data = core::at(mesh->vertex_buffers_, 0)->data;
    if (layout.diffuse >= 0 && mesh->vertex_count_) {
        std::array<double, 4> sum = {};
        for (uint32_t v = 0; v < mesh->vertex_count_; ++v) {
            uint32_t c = 0;
            std::memcpy(&c, data + v * mesh->stride + layout.diffuse, 4);
            for (int k = 0; k < 4; ++k)
                core::at(sum, k) += (c >> (8 * k)) & 0xFF;
        }
        uint32_t colour = 0;
        for (int k = 0; k < 4; ++k)
            // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
            colour |= static_cast<uint32_t>(core::at(sum, k) / mesh->vertex_count_ + 0.5) << (8 * k);
        spec->colour = colour;
    }
    const render::IdxStore* indices = core::at(mesh->index_buffers_, 0);
    if (indices && indices->indices && core::at(mesh->index_counts_, 0) >= 3) {
        double world = 0.0, texture = 0.0;
        for (uint32_t i = 0; i + 1 < core::at(mesh->index_counts_, 0); ++i) {
            const uint16_t a = indices->indices[i];
            const uint16_t b = indices->indices[i + 1];
            if (a == b || a >= mesh->vertex_count_ || b >= mesh->vertex_count_)
                continue;
            const auto pa = gpu::VertexView(data, mesh->stride).float3(a);
            const auto pb = gpu::VertexView(data, mesh->stride).float3(b);
            const auto ta = gpu::VertexView(data, mesh->stride).float2(a, layout.tex);
            const auto tb = gpu::VertexView(data, mesh->stride).float2(b, layout.tex);
            const double dw = std::sqrt(static_cast<double>(core::at(pa, 0) - core::at(pb, 0)) * (core::at(pa, 0) - core::at(pb, 0)) +
                                        static_cast<double>(core::at(pa, 1) - core::at(pb, 1)) * (core::at(pa, 1) - core::at(pb, 1)) +
                                        static_cast<double>(core::at(pa, 2) - core::at(pb, 2)) * (core::at(pa, 2) - core::at(pb, 2)));
            const double dt = std::sqrt(static_cast<double>(core::at(ta, 0) - core::at(tb, 0)) * (core::at(ta, 0) - core::at(tb, 0)) +
                                        static_cast<double>(core::at(ta, 1) - core::at(tb, 1)) * (core::at(ta, 1) - core::at(tb, 1)));
            if (dt > 1e-4 && std::isfinite(dt) && std::isfinite(dw)) {
                world += dw;
                texture += dt;
            }
        }
        if (texture > 1e-4)
            spec->uv = std::clamp(static_cast<float>(world / texture), 16.0f, 4096.0f);
    }
    const render::TriMeshSource* tri = static_of(sector)->geometry_;
    std::map<uint16_t, int> terrains;
    for (int f = 0; std::cmp_less(f, tri->tri_count_) && tri->faces_; ++f)
        ++terrains[tri->face_terrain(f)];
    int most = -1;
    for (const auto& t : terrains) {
        if (t.second > most)
            most = t.second, spec->terrain = t.first;
    }
    return true;
}

bool find_pit_tiles(Spec* spec, std::string* why)
{
    note_level();
    spec->hides.clear();
    struct Walk {
        Spec* spec;
        std::vector<uint32_t> tiles;
        std::string refused;
    } walk = {spec, {}, {}};
    scenery::for_each_level_sector(
        [](void* context, uint32_t checksum, render::WorldCell* sector) {
            auto* w = static_cast<Walk*>(context);
            // (a tile a pit hides now still counts - the pit being edited)
            const bool mine = tile_hides().count(checksum) && tile_hides()[checksum] > 0;
            if (!w->refused.empty() || pool().count(checksum) || (!is_operative(sector) && !mine))
                return;
            const render::StaticCollider* coll = static_of(sector);
            const render::TriMeshSource* tri = coll ? coll->geometry_ : nullptr;
            if (!tri || !tri->faces_ || !tri->vertices_ || ((coll->flag_bits_ & 4) && !mine))
                return;
            const Spec& s = *w->spec;
            if (tri->bounds_.max.x < s.x0 || tri->bounds_.min.x > s.x1 || tri->bounds_.max.z < s.z0 ||
                tri->bounds_.min.z > s.z1 || tri->bounds_.max.y < s.base - 40.0f ||
                tri->bounds_.min.y > s.base + 40.0f)
                return;
            bool ground = false;
            const int faces = std::min<int>(tri->tri_count_, 60000);
            for (int f = 0; f < faces && !ground; ++f) {
                const uint16_t flags =
                    tri->face_flags(f);
                if (flags & 0x10)
                    continue;
                std::array<V3, 3> c{};
                bool ok = true;
                for (int k = 0; k < 3 && ok; ++k) {
                    const uint32_t index = tri->face_vertex(f, k);
                    ok = index < tri->num_vertices_;
                    if (ok) {
                        const maths::Vec4 p = tri->read_stored_vertex(static_cast<int>(index));
                        core::at(c, k) = {p.x, p.y, p.z};
                    }
                }
                ground = ok && ground_face_under(c, s, 30.0f);
            }
            if (!ground)
                return;
            const V3 lo = {tri->bounds_.min.x, tri->bounds_.min.y, tri->bounds_.min.z};
            const V3 hi = {tri->bounds_.max.x, tri->bounds_.max.y, tri->bounds_.max.z};
            if (tile_inside(lo, hi, s, 25.0f)) {
                w->tiles.push_back(checksum);
                return;
            }
            std::array<char, 320> text{};
            std::snprintf(
                text.data(), sizeof(text),
                "the floor here is %s%s, %.0f x %.0f - bigger than the rectangle. THUG2's floors can't be cut: "
                "make the rectangle cover that whole piece, or raise around it instead",
                object_edit::caption(checksum).c_str(), geom_of(sector) ? "" : " (collision only)", hi.x - lo.x,
                hi.z - lo.z);
            w->refused = text.data();
        },
        &walk);
    if (!walk.refused.empty()) {
        *why = walk.refused;
        return false;
    }
    if (walk.tiles.empty()) {
        *why = "no floor piece was found at the ground's height under the rectangle";
        return false;
    }
    spec->hides = walk.tiles;
    return true;
}

bool place_in(const Spec& in, std::string* why)
{
    note_level();
    Spec spec = in;
    if (!normalise(&spec)) {
        *why = "a value isn't a number";
        return false;
    }
    if (spec.lower && !find_pit_tiles(&spec, why))
        return false;
    if (!spec.lower)
        spec.hides.clear();
    const std::vector<Spec> before = g_specs;
    if (!get_make(spec, why))
        return false;
    push_module_undo_labelled(std::make_shared<GroundStep>(before), spec.lower ? "Lower ground" : "Raise ground");
    bool found = false;
    for (Spec& s : g_specs) {
        if (s.name == spec.name) {
            s = spec;
            found = true;
        }
    }
    if (!found)
        g_specs.push_back(spec);
    return true;
}

void drop_out(const std::string& name)
{
    note_level();
    if (!get_find(name))
        return;
    push_module_undo_labelled(std::make_shared<GroundStep>(g_specs), "Remove ground piece");
    unmake(name);
    std::erase_if(g_specs, [&](const Spec& s) { return s.name == name; });
}

int apply_to_each(const std::vector<Spec>& specs, std::string* message)
{
    note_level();
    // (gone: taken out; changed or new: made; the same: left)
    std::vector<std::string> gone;
    for (const auto& made : made_state()) {
        if (std::ranges::none_of(specs, [&](const Spec& s) { return s.name == made.first; }))
            gone.push_back(made.first);
    }
    for (const std::string& name : gone)
        unmake(name);
    std::vector<Spec> kept;
    int placed = 0;
    for (const Spec& spec : specs) {
        auto it = made_state().find(spec.name);
        std::string why;
        if ((it != made_state().end() && same_spec(it->second.built, spec)) || get_make(spec, &why)) {
            ++placed;
        } else {
            get_log("level editor: ground piece %s not made: %s", spec.name.c_str(), why.c_str());
            if (message && message->empty())
                *message = "ground piece " + spec.name + " not made: " + why;
        }
        kept.push_back(spec); // (kept even when not made: its record isn't lost)
    }
    g_specs = kept;
    return placed;
}

std::vector<std::string> capture_records()
{
    std::vector<std::string> lines;
    lines.reserve(g_specs.size());
    for (const Spec& s : g_specs)
        lines.push_back("ground " + link_record(s, object_edit::caption(s.material).c_str()));
    return lines;
}

void apply_records(const std::vector<std::string>& lines)
{
    std::vector<Spec> specs;
    for (const std::string& line : lines) {
        Spec s;
        const std::string body = line.size() > 7 ? line.substr(7) : std::string();
        if (line.starts_with("ground ") && read_record(body, &s))
            specs.push_back(s);
        else
            get_log("level editor: a layout's ground record isn't readable - skipped: %s", line.c_str());
    }
    std::string message;
    const int placed = apply_to_each(specs, &message);
    if (!specs.empty())
        get_log("level editor: the layout's %d of %u ground pieces put in place", placed,
                static_cast<unsigned>(specs.size()));
}

void reset_all()
{
    // (a new level: nothing of the old one touched - its sectors are gone)
    render::WorldScene* scene = scenery::main_scene();
    if (scene == g_scene && scenery::world_scene_key(scene) == g_active_scene_key) {
        std::vector<std::string> names;
        names.reserve(made_state().size());
        for (const auto& made : made_state())
            names.push_back(made.first);
        for (const std::string& name : names)
            unmake(name);
    }
    g_specs.clear();
    made_state().clear();
    tile_hides().clear();
    if (scene != g_scene || scenery::world_scene_key(scene) != g_active_scene_key)
        note_level();
}

void add_preview(const overlays::Painter& painter, const Spec& spec, uint32_t argb)
{
    Mesh mesh = get_build(spec);
    const size_t faces = std::min<size_t>(mesh.faces.size(), 3000);
    for (size_t f = 0; f < faces; ++f) {
        const Face& face = core::at(mesh.faces, f);
        const V3 a = core::at(mesh.vertices, face.a).pos;
        const V3 b = core::at(mesh.vertices, face.b).pos;
        const V3 c = core::at(mesh.vertices, face.c).pos;
        (void)painter.get_seg(a, b, argb);
        (void)painter.get_seg(b, c, argb);
        (void)painter.get_seg(c, a, argb);
    }
}

} // namespace frontend::level_editor::ground
