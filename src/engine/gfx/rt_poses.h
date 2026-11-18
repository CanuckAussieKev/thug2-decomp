#pragma once

// NOT ORIGINAL - path tracing, stage 3 (P2: everything drawn in the acceleration structure): the poses of meshes the
// game skins on the processor, one per instance that draws them, for the rays.
//
// The game skins such a mesh into the mesh's own vertex buffers as each instance draws it (render_pc's MeshRef::render),
// so the buffers hold the last instance's pose of the frame before: a mesh shared by two peds holds one of them, and
// the skater's is a frame old. The rays need each instance's pose of the frame they are traced in. A pose here is a
// vertex buffer of its own per (instance, mesh) - made through the Gpu, dynamic, written whole each frame by the
// caller's skinning (the same code and bones the draw uses) before the frame's TLAS is built - and a ray-tracing mesh
// of it (rt::RayTracing::add_mesh, dynamic: refitted each frame by mesh_changed, rebuilt by the backend when refits
// have worn it). The mesh's indices and vertex layout are the drawn mesh's.
//
// Lifetime: a pose not asked for in a frame (its instance gone, inactive or hidden) is released at that frame's
// end_frame; a source mesh destroyed releases its poses (release_source); a device reset all of them.

#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>

#include "engine/gfx/rt_scene.h"

namespace render {

class Gpu;

namespace rt {

class PoseStore {
public:
    // The writer of a pose: `out` is the pose's vertex buffer (vertex_count x stride bytes, its earlier contents
    // undefined) - every vertex's position (and whatever attributes the layout has) written; false: it couldn't be (the
    // pose is released - nothing traced for it).
    using Writer = std::function<bool(std::span<uint8_t> out)>;

    // The rays' mesh for `owner`'s draw of `source` this frame: its pose written by `write`, registered with `rt` on
    // first use with `layout` (the drawn mesh's geometry: its indices and the vertex layout - its own vertices are
    // replaced by the pose's buffer) and reported changed on every later frame. `frame` is the record's frame (one
    // write a frame: a second ask in the same frame returns the same mesh). 0: nothing to trace (no vertices, or the
    // buffer could not be made or locked).
    MeshId pose(const void* owner, const void* source, const Geometry& layout, uint64_t frame, const Writer& write,
                Gpu& gpu, RayTracing& rt);
    // The poses not asked for in `frame` released (their meshes removed from `rt`, their buffers released).
    void end_frame(uint64_t frame, Gpu& gpu, RayTracing& rt);
    // A source mesh destroyed: its poses released.
    void release_source(const void* source, Gpu& gpu, RayTracing& rt);
    // Every pose released (a device reset: the backend forgets its meshes; `rt` null - nothing to remove from).
    void release_all(Gpu& gpu, RayTracing* rt);
    // `owner`'s pose of `source` (0: none) - what a draw of it is matched with.
    [[nodiscard]] MeshId find(const void* owner, const void* source) const;
    // A pose's source mesh (null: `mesh` is no pose) - for tools that describe a hit.
    [[nodiscard]] const void* source_of(MeshId mesh) const;
    [[nodiscard]] std::size_t size() const { return poses_.size(); }

private:
    struct Key {
        const void* owner = nullptr;
        const void* source = nullptr;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };
    struct Pose {
        MeshId id = 0;
        GpuVertexBuffer* buffer = nullptr;
        uint32_t bytes = 0;
        Geometry geometry;
        uint64_t frame = 0; // last asked for
    };
    static void release(Pose& pose, Gpu& gpu, RayTracing* rt);

    std::unordered_map<Key, Pose, KeyHash> poses_;
};

// The bytes a pose of `layout` takes (its vertices at its stride, at least the positions of the last one).
[[nodiscard]] uint32_t pose_bytes(const Geometry& layout);

} // namespace rt
} // namespace render
