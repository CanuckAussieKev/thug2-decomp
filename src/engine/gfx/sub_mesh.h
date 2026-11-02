#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "engine/maths/vec4.h"
#include "gpu/vertex_buffers.h"
#include <gsl/pointers>

namespace scene_format {
class MemoryStream;
class FileStream;
} // namespace scene_format

// A scene mesh (Nx engine, PC): what its vertex and index buffers hold and the operations on it that live here -
// moving and turning it in place, copying it, making it a billboard. Most of its code (drawing, the buffers
// themselves) is elsewhere and not rewritten yet.
namespace render {


struct VertexArena; // (pc_scene.h)
struct Singleton;   // (geom.h)
struct SceneRef;    // (mesh_list.h)
struct MaterialRef; // (surface_material.h)


// A billboard (0x24 bytes): how it turns (0 / 1 towards the camera about any axis, 2 about `axis`), its pivot.
struct Billboard {
    uint32_t type;
    std::array<float, 4> pivot;
    std::array<float, 4> axis;
};

struct MeshRef {
    uint32_t flags{0}; // 1: shares another mesh's buffers; 0x10: a billboard
    std::array<float, 3> centre{}; // the bounding sphere
    float radius{};
    MaterialRef* material{};
    uint8_t stride;                 // bytes per vertex
    uint8_t current_vertex_buffer_{0}; // +0x19
    uint8_t vertex_buffer_count_{1};   // +0x1A
    uint8_t viewports;              // +0x1B: a bit per viewport it shows in
    uint8_t color_offset_{0};       // +0x1C: where in a vertex its colour is (0: none)
    uint8_t normal_offset_{0};      // +0x1D: its normal
    uint8_t uv_offset_{0};          // +0x1E: its first texture coordinates
    uint8_t bone_{0};               // +0x1F: the bone it follows (0xFF: none)
    uint16_t load_index_{};         // +0x20
    uint16_t vertex_count_{};       // +0x22
    std::array<uint16_t, 8> index_counts_{};    // +0x24
    std::array<uint8_t, 4> unknown34{};
    uint32_t fvf; // +0x38: its vertex format (D3DFVF; 0x10 the normals: lit)
    uint32_t second_fvf_{};           // +0x3C: the vertex format of the mesh's second type (written, never read)
    std::array<uint8_t, 4> unknown40_{};
    void* pixel_shader_;              // +0x44
    void* unknown48;                  // +0x48 (from the heap)
    Billboard* billboard;             // +0x4C
    uint32_t primitive_type_;         // +0x50 (5 when constructed)
    std::array<IdxStore*, 8> index_buffers_{};    // +0x54
    uint32_t buffer_usage_{};         // +0x74: the vertex buffers' usage and flags
    uint32_t buffer_flags_{};         // +0x78
    float depth{};                    // +0x7C (the render's)
    std::array<float, 3> color{};                 // +0x80: its colour (doubled when used: 0.5 leaves the passes as they are)
    uint8_t* per_vertex_bytes_;       // +0x8C: a byte per vertex
    float* transform;                 // +0x90: 16 floats, made on first use
    uint8_t* per_vertex_data_;        // +0x94: 0x4C bytes per vertex
    int unknown98;                    // +0x98: its one bone (-1: none)
    int unknown9c_;                   // +0x9C: the frame (0x00786AC4) it was last skinned in
    const Singleton* unknowna0_;      // +0xA0: and for which instance (0x007825B8)
    uint32_t unknowna4_{}, unknowna8_{}; // +0xA4: texture coordinate sets; +0xA8: bones a vertex (1-3)
    uint32_t position_count_;         // +0xAC
    float* positions;                 // +0xB0: three floats each
    uint32_t triangle_count_;         // +0xB4
    uint16_t* triangles;              // +0xB8: three indices each
    std::array<VertexBuffer*, 3> extra_buffers_{}; // +0xBC: destroyed with it (each beside its vertex buffer)
    VertexBuffer* last_buffer_;       // +0xC8: destroyed with it
    std::array<VertexBuffer*, 3> vertex_buffers_{}; // +0xCC

    // NOT ORIGINAL - the transform's 16 floats and the positions (three each) as runs.
    [[nodiscard]] std::span<float, 16> transform_floats() const { return std::span<float, 16>(transform, 16); }
    [[nodiscard]] std::span<float> position_floats() const { return {positions, static_cast<size_t>(position_count_) * 3}; }

    // 0x004D4BE0: construct - no buffers, one vertex buffer to come, shown (0x40) in every viewport, no bone,
    // primitive type 5.
    MeshRef();
    // 0x004D69E0: the destructor's body - off the billboards (0x10), its transform freed, and unless it shares
    // another mesh's buffers (1) its index and vertex buffers destroyed (the device's stream let go of if it is one),
    // its per-vertex data and billboard freed; its positions and triangles freed; all of it cleared.
    ~MeshRef();
    MeshRef(const MeshRef&) = delete;
    MeshRef& operator=(const MeshRef&) = delete;
    // 0x004D4CA0: each vertex's colour from the material's colour animations (its per-vertex byte: 1-based; 0 none),
    // written into the current vertex buffer, unlocked after. Unlock's result (0 without per-vertex bytes).
    long apply_vertex_colors();
    // 0x004D4D30: its position (its transform's last row; 0, 0, 0, 1 without one).
    void fetch_place(float* out) const;
    // 0x004D57D0: its first strip's long runs of degenerate triangles (more than 5) cut to the four or five a join
    // needs, and the strip written to the card.
    void clean_strip();
    // 0x004D5900: the bounding sphere (the two stack arguments after are unused).
    void change_bound_sphere(const float* centre, float radius);
    // 0x004D5C30: its first vertex buffer made (a share of `arena`'s, whose size grows by it, 32-byte aligned) and
    // filled with `bytes` of `vertices`.
    void create_vertex_buffer(const void* vertices, uint32_t bytes, VertexArena* arena);
    // 0x004D4D70 / 0x004D4E50 / 0x004D5020: its vertices (per_vertex_data_) skinned on the CPU with one, two or three
    // bones each (the bone matrices 0x007825B0) into `out` (a stride apart): position, normal, colour, texture sets.
    void skin_one_bone(uint8_t* out);
    void skin_two_bones(uint8_t* out);
    void skin_three_bones(uint8_t* out);
    // 0x004D5290: drawn - unless drawing is off (0x007D1EC0) or it is past the clip distance: distance fog, its bone's
    // matrix (+0x98), its colours, vertex format and material (shaders, or pass by pass), skinned once a frame per
    // instance, its next vertex buffer, its level of detail by distance (+0x48), counted (0x00786EBC / C0).
    void render();
    // 0x004D5CC0 / 0x004D6100: loaded from a scene in memory / read from a file (scene_format.h): its eight index
    // buffers, its vertex buffers (their positions moved by the skin scaling where flagged), vertex formats, the
    // offsets in a vertex, per-vertex colour bytes (dynamic, no arena share), levels of detail, its material from
    // `scene` and its pixel shader; the first vertex buffer made (or, without a format, unpacked for skinning).
    // `dynamic` (the file's only): its buffers dynamic. False (nothing made, the stream failed) if the record is
    // damaged.
    bool read_from_ram_block(scene_format::MemoryStream& in, uint32_t material_checksum, SceneRef* scene,
                             VertexArena* arena);
    bool ingest_from_file(scene_format::FileStream& in, uint32_t material_checksum, SceneRef* scene, VertexArena* arena,
                          bool dynamic);
    // 0x004D5940: the packed skinned vertices unpacked into per_vertex_data_ (normals and weights in 11/11/10 bits,
    // bones / 3), the bones a vertex counted, its format, stride and a vertex buffer without a copy made for them.
    void set_up_skinning(const uint8_t* packed, uint32_t bytes, VertexArena* arena);
    // 0x004D6530: its collision data made once: the positions from its first vertex buffer, the non-degenerate
    // triangles of its first strip.
    void build_collision();
    // 0x004D3FC0 / 0x004D3FD0: a vertex buffer (0-2).
    VertexBuffer* fetch_vertex_store(int index);
    void set_vertex_buffer(int index, VertexBuffer* buffer);
    // 0x004D3E50: its first vertex buffer moved into the arena (a share at the next free offset, 32-byte aligned;
    // the arena's buffer gains a reference) unless it has a buffer of its own already.
    void move_into_arena(VertexArena* arena);
    // 0x004D3D10: its pixel shader set with the material's pass colours (0x00786ECC, 16 bytes a pass) times its own
    // colour (+0x80, doubled) - or the instance's (0x00786F20, /128) while one is on (0x00786F1C) - for the passes
    // without the top bit of their flags; the constants put back after.
    void set_pixel_shader_colored();
    // 0x004D6D40: `count` more vertex buffers, copies of the first (which is unlocked after).
    void add_vertex_buffers(int count);
    // 0x004D6E00: move it so its transform's position is `position` (vertices, positions and sphere moved by the
    // difference).
    void move_to(const maths::Vec4* position);
    // 0x004D6FB0 (Ghidra starts it at 0x004D6FBA): turn the vertices `turns` quarter turns about the transform's
    // position (about y; 1-3 only) - and the sphere's centre once per vertex buffer.
    void turn_y(int turns);
    // 0x004D71F0: a copy - sharing the buffers (flag 1) or with copies of them. Returns the copy.
    MeshRef* replicate(bool share);
    // 0x004D7580: make it a billboard of `type` (1: facing the camera; 2: about `axis`, or as 1 when the axis is
    // nearly vertical) about `pivot`: its quad's vertices turned into the billboard's own axes.
    void make_billboard(int type, const maths::Vec4* pivot, const maths::Vec4* axis);
};
static_assert(offsetof(MeshRef, material) == 0x14 && offsetof(MeshRef, vertex_buffer_count_) == 0x1A &&
                  offsetof(MeshRef, index_counts_) == 0x24 && offsetof(MeshRef, billboard) == 0x4C &&
                  offsetof(MeshRef, index_buffers_) == 0x54 && offsetof(MeshRef, depth) == 0x7C &&
                  offsetof(MeshRef, per_vertex_bytes_) == 0x8C && offsetof(MeshRef, per_vertex_data_) == 0x94 &&
                  offsetof(MeshRef, unknowna4_) == 0xA4 && offsetof(MeshRef, position_count_) == 0xAC &&
                  offsetof(MeshRef, triangles) == 0xB8 && offsetof(MeshRef, vertex_buffers_) == 0xCC &&
                  offsetof(MeshRef, pixel_shader_) == 0x44 && offsetof(MeshRef, buffer_flags_) == 0x78 &&
                  offsetof(MeshRef, extra_buffers_) == 0xBC && sizeof(MeshRef) == 0xD8,
              "layout must match the original");

// 0x004D5760: a new vertex buffer (from the C runtime's heap; null if none).
gsl::owner<VertexBuffer*> new_vertex_buffer(uint32_t bytes, uint32_t usage, uint32_t flags);
// 0x004D67F0 / 0x004D68C0: before / after a device reset - every vertex buffer's, scene arena's and index buffer's
// card buffer let go of (the vertex buffers keep the old pointers, to find the shares by), then made again and
// filled from the copies (the shares moved to their arena's new buffer).
void release_buffers();
void recreate_buffers();

} // namespace render
