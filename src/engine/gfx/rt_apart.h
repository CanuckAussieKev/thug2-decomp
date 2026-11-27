#pragma once

// NOT ORIGINAL - (round PG: path tracing's stage 3, its last gap) the frame's triangles of what the game draws on the
// processor each frame for its passes apart - the particles, the level's billboards (render_pc/rt_apart.h captures them
// as they are drawn) - as ray-tracing meshes: a per-frame bottom-level structure for the shadow and secondary rays, as
// shipping path-traced games make their particles' (RTX Remix's captured particle draws, Cyberpunk 2077 RT Overdrive's
// alpha-tested particle BLAS). API-neutral, like the poses (rt_poses.h).
//
// One mesh per batch (a material and a vertex layout, the caller's key): its triangles, three vertices each, copied into
// a vertex buffer of its own made through the Gpu (dynamic; grown by doubling, never shrunk while the batch lives), the
// vertices past this frame's made inactive (a position not a number: no triangle there - the structure's build leaves
// them out), registered once with rt::Geometry::rebuilt - built anew each frame (a refit would keep the tree of the
// triangles that have gone). A batch not given for kKeepRounds commits in a row is released (its mesh removed, its buffer
// released) - one that only blinks out of view for a while (a particle system the view culls) keeps its buffer and its
// structure's memory: no allocation churn.

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "engine/gfx/rt_scene.h"

namespace render {

class Gpu;

namespace rt {

class ApartStore {
public:
    // A batch: `layout`'s vertex stride and attribute offsets / formats (rt::Geometry's - the rest is the store's), its
    // triangles' vertices (vertex_count x stride bytes, three vertices a triangle).
    struct Batch {
        const void* key = nullptr;
        uint32_t layout_key = 0; // (another layout under the same key: another batch)
        Geometry layout;
        std::span<const uint8_t> vertices;
    };
    // This frame's meshes, one per batch in order (0: none - no triangles, or its buffer couldn't be made or locked).
    void commit(std::span<const Batch> batches, Gpu& gpu, RayTracing& rt, std::vector<MeshId>* meshes);
    // Every batch released (a device reset; `rt` null - nothing to remove from).
    void release_all(Gpu& gpu, RayTracing* rt);
    [[nodiscard]] std::size_t size() const { return slots_.size(); }
    // (the log's) batches made and released, ever
    [[nodiscard]] uint64_t made() const { return made_; }
    [[nodiscard]] uint64_t released() const { return released_; }
    // A batch's mesh: whether `mesh` is one of the store's (for tools that describe a hit).
    [[nodiscard]] bool owns(MeshId mesh) const;

    static constexpr uint32_t kLeastVertices = 192; // a batch's buffer at least this many vertices (64 triangles)
    static constexpr uint64_t kKeepRounds = 120;    // a batch not given this many commits in a row released (2 s)

private:
    struct Key {
        const void* key = nullptr;
        uint32_t layout_key = 0;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };
    struct Slot {
        MeshId id = 0;
        GpuVertexBuffer* buffer = nullptr;
        uint32_t capacity = 0; // vertices
        Geometry geometry;
        uint64_t round = 0;
    };
    static void release(Slot& slot, Gpu& gpu, RayTracing* rt);

    std::unordered_map<Key, Slot, KeyHash> slots_;
    uint64_t round_ = 0;
    uint64_t made_ = 0;
    uint64_t released_ = 0;
};

// (round MIST) What the last commit (of any store) held: an FNV-1a hash of its batches' vertex bytes in order and their
// triangles - the particles and billboards as the frame drew them. The measuring session logs it (devtools/console.cpp
// `state`, `measure on`): two runs of a plan froze the same particles when it is the same. Hashed only once asked for
// (keep_apart_frames - the console's first `state` / `measure`): no cost in play.
struct ApartFrame {
    uint64_t hash = 0;
    uint64_t triangles = 0;
    uint64_t batches = 0;
    bool kept = false; // (false: not hashed - keep_apart_frames not asked for when it was committed)
};
void keep_apart_frames();
[[nodiscard]] ApartFrame last_apart_frame();

// The vertices a batch's buffer holds for `count` triangles' vertices: the least power of two from kLeastVertices.
[[nodiscard]] uint32_t apart_capacity(uint32_t vertices);

} // namespace rt
} // namespace render
