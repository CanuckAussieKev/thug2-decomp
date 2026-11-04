#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "base/maths/mat4.h"
#include "engine/gfx/gpu_handles.h"

// NOT ORIGINAL split - the PC geometry's records as data: a vertex buffer's and an index buffer's (their sizes, the
// graphics buffers' handles and the copies of their bytes in memory), a mesh's (its formats, counts, bounding sphere,
// colour, buffers, collision lists) and the first part of an instance's (its flags, transform, bones and colour) - the
// original's layouts (static_asserts below), with the accessors other code reads and sets them through. The graphics
// backend's classes derive from them and add what they do (render_d3d9/vertex_buffers.h: VertexBuffer and IdxStore;
// sub_mesh.h: MeshRef; geom.h: Singleton); the level editor reads and edits a sector's meshes through these records
// alone, and asks the backend for the rest (render::Device, "what our level editor asks": a geom's meshes, new buffers,
// their upload). Nothing here calls the backend.
namespace render {

struct MaterialRef; // (surface_material.h)

constexpr std::size_t kIndexBufferSlots = 8;
constexpr std::size_t kTransformFloats = 16;

// A vertex buffer's record (0x24 bytes; the backend's VertexBuffer, constructor 0x004D3FF0): the size, the graphics
// buffer - its own, or a share of an arena's (flag 4) -, a copy of the vertices in memory (none with flag 1) written to
// the card when unlocked, and the lock's state.
struct VertexBufferData {
    // The fields other code reads and sets.
    [[nodiscard]] uint32_t size() const { return size_; }
    [[nodiscard]] GpuVertexBuffer* buffer() const { return buffer_; }
    void set_buffer(GpuVertexBuffer* value) { buffer_ = value; }
    [[nodiscard]] uint8_t* data() const { return data_; }
    void set_lock_offset(uint32_t value) { lock_offset_ = value; }
    void set_lock_size(uint32_t value) { lock_size_ = value; }
    void set_lock_flags(uint32_t value) { lock_flags_ = value; }
    [[nodiscard]] uint32_t arena_offset() const { return arena_offset_; }
    void set_arena_offset(uint32_t value) { arena_offset_ = value; }

protected:
    VertexBufferData() = default;

public:
    // The record (the backend's class reads and sets it as its own; its constructor sets the fields).
    uint32_t size_{};
    uint32_t usage{};
    GpuVertexBuffer* buffer_{nullptr};                  // +0x08
    uint8_t* data_{};                                     // +0x0C
    uint32_t lock_offset_{}, lock_size_{}, lock_flags_{}; // +0x10 (lockFlags -1: not locked)
    uint32_t flags{};                                     // +0x1C: 1 no copy in memory; 4 a share of an arena's buffer
    uint32_t arena_offset_{};                             // +0x20: where in the arena's buffer

private:
    // The original's layout (checked inside the class).
    static void check_layout()
    {
        constexpr size_t kDataOffset = 0xC;
        constexpr size_t kLockFlagsOffset = 0x18;
        constexpr size_t kBufferSize = 0x24;
        static_assert(offsetof(VertexBufferData, data_) == kDataOffset &&
                          offsetof(VertexBufferData, lock_flags_) == kLockFlagsOffset &&
                          sizeof(VertexBufferData) == kBufferSize,
                      "layout must match the original");
    }
};

// An index buffer's record (0x18 bytes; the backend's IdxStore, constructor 0x004D4270): the size, the graphics buffer
// and a copy of the indices in memory, written to the card when unlocked.
struct IndexBufferData {
    // The fields other code reads and sets.
    [[nodiscard]] uint32_t size() const { return size_; }
    [[nodiscard]] uint32_t usage() const { return usage_; }
    [[nodiscard]] uint32_t format() const { return format_; }
    GpuIndexBuffer*& buffer() { return buffer_; }
    [[nodiscard]] GpuIndexBuffer* buffer() const { return buffer_; }
    [[nodiscard]] uint16_t* indices() const { return indices_; }
    // NOT ORIGINAL - the copy kept in memory as a run of its indices (size is in bytes).
    [[nodiscard]] std::span<uint16_t> index_run() const { return {indices_, size_ / 2}; }

protected:
    // The size in bytes, the usage, the index format and the copy in memory (the constructor's first steps).
    struct Made {
        uint32_t bytes{};
        uint32_t usage{};
        uint32_t format{};
        uint16_t* indices{};
    };
    explicit IndexBufferData(Made made)
        : size_(made.bytes), usage_(made.usage), format_(made.format), indices_(made.indices)
    {
    }

public:
    // The record (the backend's class reads and sets it as its own; its constructor sets the fields).
    uint32_t size_;
    uint32_t usage_;
    uint32_t format_;
    GpuIndexBuffer* buffer_{nullptr};
    uint16_t* indices_;
    uint32_t unknown14{}; // (cleared when destroyed)

private:
    // The original's layout (checked inside the class).
    static void check_layout()
    {
        constexpr size_t kIndicesOffset = 0x10;
        constexpr size_t kStoreSize = 0x18;
        static_assert(offsetof(IndexBufferData, indices_) == kIndicesOffset && sizeof(IndexBufferData) == kStoreSize,
                      "layout must match the original");
    }
};

// A billboard (0x24 bytes): how it turns (0 / 1 towards the camera about any axis, 2 about `axis`), its pivot.
struct Billboard {
    uint32_t type;
    std::array<float, 4> pivot;
    std::array<float, 4> axis;
};

// The first part of an instance (0x54 of its 0x64 bytes; the backend's Singleton, constructor 0x004D1030): its flags,
// transform, bone matrices and colour.
struct InstanceData {
    // The fields other code reads and sets.
    uint32_t& flags() { return flags_; }
    [[nodiscard]] uint32_t flags() const { return flags_; }
    maths::Mat4& transform() { return transform_; }
    [[nodiscard]] const maths::Mat4& transform() const { return transform_; }
    void set_transform(const maths::Mat4& value) { transform_ = value; }
    [[nodiscard]] maths::Mat4* bones() const { return bones_; }
    void set_bones(maths::Mat4* value) { bones_ = value; }
    void set_bone_count(int value) { bone_count_ = value; }
    [[nodiscard]] bool active() const { return active_; }
    void set_active(bool value) { active_ = value; }
    std::array<uint8_t, 4>& color() { return color_; }
    [[nodiscard]] const std::array<uint8_t, 4>& color() const { return color_; }

protected:
    // Its transform (a copy), its bone matrices (not copied) and their count.
    struct Made {
        const maths::Mat4* transform{};
        maths::Mat4* bones{};
        int bone_count{};
    };
    explicit InstanceData(Made made) : transform_(*made.transform), bones_(made.bones), bone_count_(made.bone_count) {}

public:
    // The record (the backend's class reads and sets it as its own; its constructor sets the fields).
    uint32_t flags_{0};               // 1: owns its mesh list and bones; 2: lit (every mesh asks for it; set by the
                                      // constructor); 4: coloured
    maths::Mat4 transform_{};         // +0x04
    maths::Mat4* bones_;              // +0x44
    int bone_count_;                  // +0x48
    bool active_{true};               // +0x4C
    std::array<uint8_t, 4> color_{};  // +0x4D: RGBA (0x80 each: unchanged)
    std::array<uint8_t, 3> pad51{};

private:
    // The original's layout (checked inside the class).
    static void check_layout()
    {
        constexpr size_t kBonesOffset = 0x44;
        constexpr size_t kColorOffset = 0x4D;
        constexpr size_t kBytes = 0x54;
        static_assert(offsetof(InstanceData, bones_) == kBonesOffset && offsetof(InstanceData, color_) == kColorOffset &&
                          sizeof(InstanceData) == kBytes,
                      "layout must match the original");
    }
};

constexpr std::size_t kMeshRefMaterialOffset = 0x14;
constexpr std::size_t kMeshRefVertexBufferCountOffset = 0x1A;
constexpr std::size_t kMeshRefIndexCountsOffset = 0x24;
constexpr std::size_t kMeshRefBillboardOffset = 0x4C;
constexpr std::size_t kMeshRefIndexBuffersOffset = 0x54;
constexpr std::size_t kMeshRefDepthOffset = 0x7C;
constexpr std::size_t kMeshRefPerVertexBytesOffset = 0x8C;
constexpr std::size_t kMeshRefPerVertexDataOffset = 0x94;
constexpr std::size_t kMeshRefUnknowna4Offset = 0xA4;
constexpr std::size_t kMeshRefPositionCountOffset = 0xAC;
constexpr std::size_t kMeshRefTrianglesOffset = 0xB8;
constexpr std::size_t kMeshRefVertexBuffersOffset = 0xCC;
constexpr std::size_t kMeshRefPixelShaderOffset = 0x44;
constexpr std::size_t kMeshRefBufferFlagsOffset = 0x78;
constexpr std::size_t kMeshRefExtraBuffersOffset = 0xBC;
constexpr std::size_t kMeshRefBytes = 0xD8;

// A scene mesh's record (0xD8 bytes; the backend's MeshRef, constructor 0x004D4BE0): what its vertex and index
// buffers hold, its formats and the offsets in a vertex, its bounding sphere, colour, levels of detail, per-vertex data
// and collision lists.
struct MeshData {
    // The fields other code reads and sets.
    uint32_t& flags() { return flags_; }
    [[nodiscard]] uint32_t flags() const { return flags_; }
    std::array<float, 3>& centre() { return centre_; }
    [[nodiscard]] const std::array<float, 3>& centre() const { return centre_; }
    [[nodiscard]] float radius() const { return radius_; }
    void set_radius(float value) { radius_ = value; }
    [[nodiscard]] MaterialRef* material() const { return material_; }
    void set_material(MaterialRef* value) { material_ = value; }
    [[nodiscard]] uint8_t stride() const { return stride_; }
    void set_stride(uint8_t value) { stride_ = value; }
    [[nodiscard]] uint8_t vertex_buffer_count() const { return vertex_buffer_count_; }
    void set_vertex_buffer_count(uint8_t value) { vertex_buffer_count_ = value; }
    [[nodiscard]] uint8_t viewports() const { return viewports_; }
    void set_viewports(uint8_t value) { viewports_ = value; }
    [[nodiscard]] uint8_t color_offset() const { return color_offset_; }
    void set_color_offset(uint8_t value) { color_offset_ = value; }
    [[nodiscard]] uint8_t normal_offset() const { return normal_offset_; }
    void set_normal_offset(uint8_t value) { normal_offset_ = value; }
    [[nodiscard]] uint8_t uv_offset() const { return uv_offset_; }
    void set_uv_offset(uint8_t value) { uv_offset_ = value; }
    [[nodiscard]] uint8_t bone() const { return bone_; }
    void set_bone(uint8_t value) { bone_ = value; }
    uint16_t& load_index() { return load_index_; }
    [[nodiscard]] uint16_t load_index() const { return load_index_; }
    void set_load_index(uint16_t value) { load_index_ = value; }
    [[nodiscard]] uint16_t vertex_count() const { return vertex_count_; }
    void set_vertex_count(uint16_t value) { vertex_count_ = value; }
    std::array<uint16_t, kIndexBufferSlots>& index_counts() { return index_counts_; }
    [[nodiscard]] const std::array<uint16_t, kIndexBufferSlots>& index_counts() const { return index_counts_; }
    uint32_t& fvf() { return fvf_; }
    [[nodiscard]] uint32_t fvf() const { return fvf_; }
    void set_fvf(uint32_t value) { fvf_ = value; }
    void set_second_fvf(uint32_t value) { second_fvf_ = value; }
    void*& pixel_shader() { return pixel_shader_; }
    [[nodiscard]] void* pixel_shader() const { return pixel_shader_; }
    [[nodiscard]] void* unknown48() const { return unknown48_; }
    void set_unknown48(void* value) { unknown48_ = value; }
    [[nodiscard]] Billboard* billboard() const { return billboard_; }
    [[nodiscard]] uint32_t primitive_type() const { return primitive_type_; }
    std::array<IndexBufferData*, kIndexBufferSlots>& index_buffers() { return index_buffers_; }
    [[nodiscard]] const std::array<IndexBufferData*, kIndexBufferSlots>& index_buffers() const { return index_buffers_; }
    void set_buffer_usage(uint32_t value) { buffer_usage_ = value; }
    uint32_t& buffer_flags() { return buffer_flags_; }
    [[nodiscard]] uint32_t buffer_flags() const { return buffer_flags_; }
    [[nodiscard]] float depth() const { return depth_; }
    void set_depth(float value) { depth_ = value; }
    std::array<float, 3>& color() { return color_; }
    [[nodiscard]] const std::array<float, 3>& color() const { return color_; }
    [[nodiscard]] uint8_t* per_vertex_bytes() const { return per_vertex_bytes_; }
    void set_per_vertex_bytes(uint8_t* value) { per_vertex_bytes_ = value; }
    [[nodiscard]] uint8_t* per_vertex_data() const { return per_vertex_data_; }
    [[nodiscard]] uint32_t unknowna4() const { return unknowna4_; }
    [[nodiscard]] uint32_t position_count() const { return position_count_; }
    void set_position_count(uint32_t value) { position_count_ = value; }
    [[nodiscard]] float* positions() const { return positions_; }
    [[nodiscard]] uint32_t triangle_count() const { return triangle_count_; }
    void set_triangle_count(uint32_t value) { triangle_count_ = value; }
    [[nodiscard]] uint16_t* triangles() const { return triangles_; }
    std::array<VertexBufferData*, 3>& vertex_buffers() { return vertex_buffers_; }
    [[nodiscard]] const std::array<VertexBufferData*, 3>& vertex_buffers() const { return vertex_buffers_; }

    // NOT ORIGINAL - the transform's 16 floats and the positions (three each) as runs.
    [[nodiscard]] std::span<float, kTransformFloats> transform_floats() const
    {
        return std::span<float, kTransformFloats>(transform, kTransformFloats);
    }
    [[nodiscard]] std::span<float> position_floats() const
    {
        return {positions_, static_cast<size_t>(position_count_) * 3};
    }

protected:
    MeshData() = default;

public:
    // The record (the backend's class reads and sets it as its own; its constructor sets the fields).
    uint32_t flags_{0};             // 1: shares another mesh's buffers; 0x10: a billboard
    std::array<float, 3> centre_{}; // the bounding sphere
    float radius_{};
    MaterialRef* material_{};
    uint8_t stride_{};                   // bytes per vertex
    uint8_t current_vertex_buffer_{0}; // +0x19
    uint8_t vertex_buffer_count_{1};   // +0x1A
    uint8_t viewports_{};                // +0x1B: a bit per viewport it shows in
    uint8_t color_offset_{0};          // +0x1C: where in a vertex its colour is (0: none)
    uint8_t normal_offset_{0};         // +0x1D: its normal
    uint8_t uv_offset_{0};             // +0x1E: its first texture coordinates
    uint8_t bone_{0};                  // +0x1F: the bone it follows (0xFF: none)
    uint16_t load_index_{};            // +0x20
    uint16_t vertex_count_{};          // +0x22
    std::array<uint16_t, kIndexBufferSlots> index_counts_{}; // +0x24
    std::array<uint8_t, 4> unknown34{};
    uint32_t fvf_{};          // +0x38: its vertex format (D3DFVF; 0x10 the normals: lit)
    uint32_t second_fvf_{}; // +0x3C: the vertex format of the mesh's second type (written, never read)
    std::array<uint8_t, 4> unknown40_{};
    void* pixel_shader_{};     // +0x44
    void* unknown48_{};        // +0x48 (from the heap)
    Billboard* billboard_{};   // +0x4C
    uint32_t primitive_type_{}; // +0x50 (5 when constructed)
    std::array<IndexBufferData*, kIndexBufferSlots> index_buffers_{}; // +0x54
    uint32_t buffer_usage_{}; // +0x74: the vertex buffers' usage and flags
    uint32_t buffer_flags_{}; // +0x78
    float depth_{};           // +0x7C (the render's)
    std::array<float, 3> color_{}; // +0x80: its colour (doubled when used: 0.5 leaves the passes as they are)
    uint8_t* per_vertex_bytes_{}; // +0x8C: a byte per vertex
    float* transform{};           // +0x90: 16 floats, made on first use
    uint8_t* per_vertex_data_{};  // +0x94: 0x4C bytes per vertex
    int unknown98{};              // +0x98: its one bone (-1: none)
    int unknown9c_{};             // +0x9C: the frame (0x00786AC4) it was last skinned in
    const InstanceData* unknowna0_{}; // +0xA0: and for which instance (0x007825B8)
    uint32_t unknowna4_{}, unknowna8_{}; // +0xA4: texture coordinate sets; +0xA8: bones a vertex (1-3)
    uint32_t position_count_{}; // +0xAC
    float* positions_{};        // +0xB0: three floats each
    uint32_t triangle_count_{}; // +0xB4
    uint16_t* triangles_{};     // +0xB8: three indices each
    std::array<VertexBufferData*, 3> extra_buffers_{};  // +0xBC: destroyed with it (each beside its vertex buffer)
    VertexBufferData* last_buffer_{};                    // +0xC8: destroyed with it
    std::array<VertexBufferData*, 3> vertex_buffers_{}; // +0xCC

private:
    // The original's layout (checked inside the class).
    static void check_layout()
    {
        static_assert(offsetof(MeshData, material_) == kMeshRefMaterialOffset &&
                          offsetof(MeshData, vertex_buffer_count_) == kMeshRefVertexBufferCountOffset &&
                          offsetof(MeshData, index_counts_) == kMeshRefIndexCountsOffset &&
                          offsetof(MeshData, billboard_) == kMeshRefBillboardOffset &&
                          offsetof(MeshData, index_buffers_) == kMeshRefIndexBuffersOffset &&
                          offsetof(MeshData, depth_) == kMeshRefDepthOffset &&
                          offsetof(MeshData, per_vertex_bytes_) == kMeshRefPerVertexBytesOffset &&
                          offsetof(MeshData, per_vertex_data_) == kMeshRefPerVertexDataOffset &&
                          offsetof(MeshData, unknowna4_) == kMeshRefUnknowna4Offset &&
                          offsetof(MeshData, position_count_) == kMeshRefPositionCountOffset &&
                          offsetof(MeshData, triangles_) == kMeshRefTrianglesOffset &&
                          offsetof(MeshData, vertex_buffers_) == kMeshRefVertexBuffersOffset &&
                          offsetof(MeshData, pixel_shader_) == kMeshRefPixelShaderOffset &&
                          offsetof(MeshData, buffer_flags_) == kMeshRefBufferFlagsOffset &&
                          offsetof(MeshData, extra_buffers_) == kMeshRefExtraBuffersOffset &&
                          sizeof(MeshData) == kMeshRefBytes,
                      "layout must match the original");
    }
};

} // namespace render
