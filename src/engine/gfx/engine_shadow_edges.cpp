// The shadow volume's silhouette edges (ShadowVolume::change_mesh, 0x004A22F0): see engine_shadows.h.

#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#include <array>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_shadows.h"
#include "engine/lists/key_map.h"
#include "engine/render/coll_tri.h"
#include "engine/sys/startup_init.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/engine_mesh.h"

namespace render {

std::array<ShadowVolume*, 16> g_shadow_volumes;
lists::ObjectSet<ShadowEdges> g_shadow_edge_table;

namespace {


std::array<ShadowVolume*, 16>& shadow_volumes() { return g_shadow_volumes; }
lists::ObjectSet<ShadowEdges>& edge_table() { return g_shadow_edge_table; }

// Into the first free slot of the volumes drawn (none: not drawn).
void add_shadow_volume(ShadowVolume* volume)
{
    for (int i = 0; i < 16; ++i) {
        if (!core::at(shadow_volumes(), i)) {
            core::at(shadow_volumes(), i) = volume;
            return;
        }
    }
}

// The meshes of the model's collision scene: their count (the array is the volumes' key).
int get_mesh_count(const SubMesh* asset_data) { return asset_data->collision_count_; }

bool get_skipped(const TriMeshSource& mesh) { return (mesh.face_flags(0) & 0x40) != 0; }

// The edge from `from` to `to` of a face with `normal`: an existing edge the other way round, or the same way, gets
// its second face's corners; otherwise a new edge is added (past the end of the array if the estimate was short,
// as the original).
void add_edge(ShadowEdges* edges, int& count, uint16_t from, uint16_t to, const maths::Vec4& normal)
{
    ShadowSilhouetteEdge* all = edges->edges;
    int i = 0;
    for (; i < count; ++i) {
        ShadowSilhouetteEdge& edge = all[i];
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
    if (i != count)
        return;
    ShadowSilhouetteEdge& edge = all[count++];
    for (int k = 0; k < 4; ++k) {
        core::at(edge.corners, k).vertex = k < 2 ? from : to;
        core::at(edge.corners, k).pad = 0;
        core::at(edge.corners, k).normal = normal;
    }
}

} // namespace

// 0x004A22F0
void ShadowVolume::change_mesh(RenderModel*, const SubMesh* asset_data)
{
    if (asset_data)
        model_key_ = asset_data->hit_shape_;
    edges_ = find_shadow_edges(model_key_);
    if (edges_) {
        ++edges_->users;
        add_shadow_volume(this);
        return;
    }
    edges_ = static_cast<ShadowEdges*>(memory::alloc_zeroed(sizeof(ShadowEdges))); // (not checked, as the original)
    const TriMeshSource* meshes = model_key_;
    // (with a key but no asset data, read through null - as the original)
    int faces = 0, vertices = 0;
    if (model_key_) {
        for (int i = 0; i < get_mesh_count(asset_data); ++i) {
            if (!get_skipped(meshes[i])) {
                faces += meshes[i].tri_count_;
                vertices += meshes[i].num_vertices_;
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
    if (model_key_) {
        for (int m = 0; m < get_mesh_count(asset_data); ++m) {
            const TriMeshSource& mesh = meshes[m];
            if (get_skipped(mesh))
                continue;
            count = 0; // (each mesh starts the edges again: only the last one's are kept)
            for (int f = 0; std::cmp_less(f, mesh.tri_count_); ++f) {
                const auto a = static_cast<uint16_t>(mesh.face_vertex(f, 0));
                const auto b = static_cast<uint16_t>(mesh.face_vertex(f, 1));
                const auto c = static_cast<uint16_t>(mesh.face_vertex(f, 2));
                const maths::Vec4 normal = mesh.get_triangle_normal(f);
                edges_->last_mesh_ = m;
                add_edge(edges_, count, a, b, normal);
                add_edge(edges_, count, b, c, normal);
                add_edge(edges_, count, c, a, normal);
            }
        }
    }
    if (count <= estimate && count > 0) {
        edges_->count = count;
        ++edges_->users;
        edges_->key = model_key_;
        edge_table().insert(edges_);
        add_shadow_volume(this);
        return;
    }
    debug::printf("Breaking Euler's formula: %df + %dv - 2 != %d!  Make sure the model is convex.", faces, vertices,
                  count);
    if (edges_->edges) {
        memory::Hub::get_pull()->dealloc(edges_->edges);
        edges_->edges = nullptr;
    }
    memory::Hub::get_pull()->dealloc(edges_);
    edges_ = nullptr;
}

} // namespace render

