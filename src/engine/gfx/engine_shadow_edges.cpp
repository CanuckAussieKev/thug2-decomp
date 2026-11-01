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
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"

namespace render {

std::array<ShadowVolume*, 16> g_shadow_volumes;
lists::KeyMap g_shadow_edge_table{lists::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(8) at start-up)

namespace {

// An edge (0x50 bytes): its two ends as each of the two faces sharing it sees them - [0] / [2] the first face's
// ends, [1] / [3] the second's (set when it's found) - each with the face's normal.
struct EdgeCorner {
    uint16_t vertex;
    uint16_t pad; // (stack garbage in the original's records: zeroed here)
    maths::Vec4 normal;
};
struct ShadowSilhouetteEdge {
    std::array<EdgeCorner, 4> corners;
};
static_assert(sizeof(ShadowSilhouetteEdge) == 0x50, "layout must match the original");

ShadowVolume** shadow_volumes() { return g_shadow_volumes.data(); }
lists::KeyMap& edge_table() { return g_shadow_edge_table; }

// Into the first free slot of the volumes drawn (none: not drawn).
void add_shadow_volume(ShadowVolume* volume)
{
    for (int i = 0; i < 16; ++i) {
        if (!shadow_volumes()[i]) {
            shadow_volumes()[i] = volume;
            return;
        }
    }
}

// The meshes of the model's collision scene: +0x88 their count, +0x8C the array (the volumes' key).
int get_mesh_count(const uint8_t* asset_data) { return *raw::field_at<const int>(asset_data, 0x88); }

bool get_skipped(const TriMeshSource& mesh) { return (mesh.face_flags(0) & 0x40) != 0; }

// The edge from `from` to `to` of a face with `normal`: an existing edge the other way round, or the same way, gets
// its second face's corners; otherwise a new edge is added (past the end of the array if the estimate was short,
// as the original).
void add_edge(ShadowEdges* edges, int& count, uint16_t from, uint16_t to, const maths::Vec4& normal)
{
    auto* all = raw::as<ShadowSilhouetteEdge>(edges->edges);
    int i = 0;
    for (; i < count; ++i) {
        ShadowSilhouetteEdge& edge = all[i];
        if (edge.corners[0].vertex == from && edge.corners[2].vertex == to) {
            edge.corners[1].vertex = from;
            edge.corners[3].vertex = to;
        } else if (edge.corners[2].vertex == from && edge.corners[0].vertex == to) {
            edge.corners[3].vertex = from;
            edge.corners[1].vertex = to;
        } else {
            continue;
        }
        edge.corners[1].normal = normal;
        edge.corners[3].normal = normal;
        break;
    }
    if (i != count)
        return;
    ShadowSilhouetteEdge& edge = all[count++];
    for (int k = 0; k < 4; ++k) {
        edge.corners[k].vertex = k < 2 ? from : to;
        edge.corners[k].pad = 0;
        edge.corners[k].normal = normal;
    }
}

} // namespace

// 0x004A22F0
void ShadowVolume::change_mesh(RenderModel*, uint8_t* asset_data)
{
    if (asset_data)
        model_key_ = *raw::field_at<uint32_t>(asset_data, 0x8C);
    edges_ = find_shadow_edges(model_key_);
    if (edges_) {
        ++edges_->users;
        add_shadow_volume(this);
        return;
    }
    edges_ = static_cast<ShadowEdges*>(memory::alloc_zeroed(0x10)); // (not checked, as the original)
    auto* meshes = cell::pointer<TriMeshSource>(model_key_);
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
        edges_->edges = static_cast<uint8_t*>(memory::alloc(size));
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
        const uint32_t key = model_key_;
        edge_table().store_entry(key, edges_);
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

namespace render {

// 0x00640CD0 (a static initialiser): the shadow edges' table, 256 buckets.
void construct_shadow_edge_table() { new (&g_shadow_edge_table) lists::KeyMap(8); }

} // namespace render
