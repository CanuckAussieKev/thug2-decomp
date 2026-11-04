// The shadow volume's silhouette edges (ShadowVolume::change_mesh, 0x004A22F0): see engine_shadows.h.

#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <utility>
#include <vector>
#include <gsl/assert>

#include "engine/core/dbg.h"
#include "engine/render/coll_obj.h"
#include "engine/core/enum_ops.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/gfx/engine_shadows.h"
#include "engine/lists/object_set.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/engine_mesh.h"
#include "engine/gfx/effects_state.h"

namespace render {


namespace {

// Into the first free slot of the volumes drawn (none: not drawn).
void add_shadow_volume(ShadowVolume* volume)
{
    for (int i = 0; i < kShadowVolumeSlotLimit; ++i) {
        if (!core::at(effects_state().shadow_volumes(), i)) {
            core::at(effects_state().shadow_volumes(), i) = volume;
            return;
        }
    }
}

// The meshes of the model's collision scene: their count (the array is the volumes' key).
int get_mesh_count(const SubMesh* asset_data)
{
    Expects(asset_data != nullptr);
    return asset_data->collision_count();
}

// (the flag 0x40 - a trigger face - is also what keeps a face from casting a shadow edge)
bool get_skipped(const TriMeshSource& mesh) { return core::any(mesh.face_flags(0) & render::FaceFlag::kTrigger); }

// The edges of a model as they are found: the array made from Euler's estimate, and - when the estimate was short -
// the edges the original wrote on past the end of that allocation (over the heap after it, and read back from there
// while it went on counting). They are kept here instead, so the count comes out the same and the heap is left alone.
class EdgeStore {
public:
    explicit EdgeStore(std::span<ShadowSilhouetteEdge> room) : room_(room) {}

    ShadowSilhouetteEdge& at(int index)
    {
        const auto slot = static_cast<size_t>(index);
        return slot < room_.size() ? core::at(room_, slot) : core::at(beyond_, slot - room_.size());
    }
    // The slot for the edge number `index`.
    ShadowSilhouetteEdge& add(int index)
    {
        const auto slot = static_cast<size_t>(index);
        if (slot < room_.size()) {
            return core::at(room_, slot);
        }
        if (beyond_.size() <= slot - room_.size()) {
            beyond_.resize(slot - room_.size() + 1);
        }
        return core::at(beyond_, slot - room_.size());
    }

private:
    std::span<ShadowSilhouetteEdge> room_;
    std::vector<ShadowSilhouetteEdge> beyond_;
};

// The edge from `from` to `to` of a face with `normal`: an existing edge the other way round, or the same way, gets
// its second face's corners; otherwise a new edge is added (past the end of the array if the estimate was short).
void add_edge(EdgeStore& store, int& count, uint16_t from, uint16_t to, const maths::Vec4& normal)
{
    int index = 0;
    for (; index < count; ++index) {
        ShadowSilhouetteEdge& edge = store.at(index);
        if (core::at(edge.corners, 0).vertex == from && core::at(edge.corners, 2).vertex == to) {
            core::at(edge.corners, 1).vertex = from;
            core::at(edge.corners, 3).vertex = to;
        } else if (core::at(edge.corners, 2).vertex == from && core::at(edge.corners, 0).vertex == to) {
            core::at(edge.corners, 3).vertex = from;
            core::at(edge.corners, 1).vertex = to;
        } else {
            continue;
        }
        core::at(edge.corners, 1).normal = normal;
        core::at(edge.corners, 3).normal = normal;
        break;
    }
    if (index != count) {
        return;
    }
    ShadowSilhouetteEdge& edge = store.add(count++);
    for (int k = 0; k < 4; ++k) {
        core::at(edge.corners, k).vertex = k < 2 ? from : to;
        core::at(edge.corners, k).pad = 0;
        core::at(edge.corners, k).normal = normal;
    }
}

} // namespace

// 0x004A22F0
void ShadowVolume::change_mesh(RenderModel* /*unused*/, const SubMesh* asset_data)
{
    if (asset_data) {
        model_key_ = asset_data->hit_shape();
    }
    edges_ = find_shadow_edges(model_key_);
    if (edges_) {
        ++edges_->users;
        add_shadow_volume(this);
        return;
    }
    edges_ = static_cast<ShadowEdges*>(memory::alloc_zeroed(sizeof(ShadowEdges))); // (not checked, as the original)
    const TriMeshSource* meshes = model_key_;
    // (with a key but no asset data, read through null - as the original)
    int faces = 0;
    int vertices = 0;
    if (model_key_) {
        const std::span<const TriMeshSource> mesh_run(meshes, static_cast<size_t>(get_mesh_count(asset_data)));
        for (const TriMeshSource& mesh : mesh_run) {
            if (!get_skipped(mesh)) {
                faces += mesh.tri_count();
                vertices += mesh.num_vertices();
            }
        }
    }
    const int estimate = vertices + faces - 2; // (Euler's formula for a closed convex mesh)
    if (estimate >= 1) {
        const uint32_t size = static_cast<uint32_t>(estimate) * sizeof(ShadowSilhouetteEdge);
        edges_->edges = static_cast<ShadowSilhouetteEdge*>(memory::alloc(size));
        std::memset(edges_->edges, 0, size);
    }
    int count = 0;
    EdgeStore store(std::span<ShadowSilhouetteEdge>(edges_->edges, estimate >= 1 ? static_cast<size_t>(estimate) : 0));
    if (model_key_) {
        const std::span<const TriMeshSource> mesh_run(meshes, static_cast<size_t>(get_mesh_count(asset_data)));
        for (int mesh_index = 0; mesh_index < get_mesh_count(asset_data); ++mesh_index) {
            const TriMeshSource& mesh = core::at(mesh_run, mesh_index);
            if (get_skipped(mesh)) {
                continue;
            }
            count = 0; // (each mesh starts the edges again: only the last one's are kept)
            for (int face = 0; std::cmp_less(face, mesh.tri_count()); ++face) {
                const auto vertex_a = static_cast<uint16_t>(mesh.face_vertex(face, 0));
                const auto vertex_b = static_cast<uint16_t>(mesh.face_vertex(face, 1));
                const auto vertex_c = static_cast<uint16_t>(mesh.face_vertex(face, 2));
                const maths::Vec4 normal = mesh.get_triangle_normal(face);
                edges_->last_mesh_ = mesh_index;
                add_edge(store, count, vertex_a, vertex_b, normal);
                add_edge(store, count, vertex_b, vertex_c, normal);
                add_edge(store, count, vertex_c, vertex_a, normal);
            }
        }
    }
    if (count <= estimate && count > 0) {
        edges_->count = count;
        ++edges_->users;
        edges_->key = model_key_;
        effects_state().shadow_edge_table().insert(edges_);
        add_shadow_volume(this);
        return;
    }
    debug::printf("Breaking Euler's formula: %df + %dv - 2 != %d!  Make sure the model is convex.", faces, vertices,
                  count);
    if (edges_->edges) {
        memory::Hub::dealloc(edges_->edges);
        edges_->edges = nullptr;
    }
    memory::Hub::dealloc(edges_);
    edges_ = nullptr;
}

} // namespace render

