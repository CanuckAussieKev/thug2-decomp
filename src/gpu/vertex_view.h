#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// NOT ORIGINAL - a vertex buffer's bytes (the copy kept in memory, or a mesh file's vertex block) read and written
// by attribute: `stride` bytes per vertex, each attribute a run of floats at its offset in the vertex (the position
// at 0, then whatever the mesh's vertex format puts where). The game above the boundary reads positions, normals and
// texture coordinates through these instead of indexing the bytes itself. Unaligned and alias-free (memcpy).
namespace gpu {

class VertexView {
public:
    VertexView(const uint8_t* bytes, size_t stride) : bytes_(bytes), stride_(stride) {}

    // The `N` floats at `offset` in vertex `index` (a position: float3 at offset 0).
    template <size_t N>
    [[nodiscard]] std::array<float, N> floats(size_t index, size_t offset = 0) const
    {
        std::array<float, N> out{};
        std::memcpy(out.data(), at(index, offset), sizeof(out));
        return out;
    }
    [[nodiscard]] std::array<float, 3> float3(size_t index, size_t offset = 0) const { return floats<3>(index, offset); }
    [[nodiscard]] std::array<float, 2> float2(size_t index, size_t offset = 0) const { return floats<2>(index, offset); }
    // The 32-bit word at `offset` in vertex `index` (a packed colour).
    [[nodiscard]] uint32_t word(size_t index, size_t offset) const
    {
        uint32_t out = 0;
        std::memcpy(&out, at(index, offset), sizeof(out));
        return out;
    }

private:
    [[nodiscard]] const uint8_t* at(size_t index, size_t offset) const { return bytes_ + (index * stride_) + offset; }

    const uint8_t* bytes_;
    size_t stride_;
};

class VertexWriter {
public:
    VertexWriter(uint8_t* bytes, size_t stride) : bytes_(bytes), stride_(stride) {}

    template <size_t N>
    void set_floats(size_t index, size_t offset, const std::array<float, N>& values) const
    {
        std::memcpy(at(index, offset), values.data(), sizeof(values));
    }
    void set_float3(size_t index, size_t offset, float x, float y, float z) const
    {
        set_floats<3>(index, offset, {x, y, z});
    }
    void set_word(size_t index, size_t offset, uint32_t value) const
    {
        std::memcpy(at(index, offset), &value, sizeof(value));
    }
    [[nodiscard]] VertexView view() const { return {bytes_, stride_}; }

private:
    [[nodiscard]] uint8_t* at(size_t index, size_t offset) const { return bytes_ + (index * stride_) + offset; }

    uint8_t* bytes_;
    size_t stride_;
};

} // namespace gpu
