#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>

#include <gsl/assert>

#include "engine/core/raw_view.h"

// NOT ORIGINAL - a vertex buffer's bytes (the copy kept in memory, or a mesh file's vertex block) read and written
// by attribute: `stride` bytes per vertex, each attribute a run of floats at its offset in the vertex (the position
// at 0, then whatever the mesh's vertex format puts where). The game above the boundary reads positions, normals and
// texture coordinates through these instead of indexing the bytes itself. Unaligned and alias-free (memcpy). A view
// holds the vertices' bytes as a span (`count` vertices of `stride` bytes): every access is checked against it - an
// index past the vertices ends the game with a report instead of reading or writing past the buffer.
namespace gpu {

namespace detail {
// The `size` bytes at `offset` in vertex `index` of `bytes`, checked.
template <typename Byte>
std::span<Byte> vertex_bytes(std::span<Byte> bytes, size_t stride, size_t index, size_t offset, size_t size)
{
    const size_t start = (index * stride) + offset;
    Expects(start <= bytes.size() && size <= bytes.size() - start);
    return bytes.subspan(start, size);
}
} // namespace detail

class VertexView {
public:
    VertexView(std::span<const uint8_t> bytes, size_t stride) : bytes_(bytes), stride_(stride) {}
    // `count` vertices of `stride` bytes from `bytes`.
    VertexView(const uint8_t* bytes, size_t stride, size_t count) : bytes_(bytes, stride * count), stride_(stride) {}

    // How many vertices the view holds.
    [[nodiscard]] size_t count() const { return stride_ ? bytes_.size() / stride_ : 0; }

    // The `N` floats at `offset` in vertex `index` (a position: float3 at offset 0).
    template <size_t N>
    [[nodiscard]] std::array<float, N> floats(size_t index, size_t offset = 0) const
    {
        std::array<float, N> out{};
        std::memcpy(out.data(), at(index, offset, sizeof(out)), sizeof(out));
        return out;
    }
    [[nodiscard]] std::array<float, 3> float3(size_t index, size_t offset = 0) const { return floats<3>(index, offset); }
    [[nodiscard]] std::array<float, 2> float2(size_t index, size_t offset = 0) const { return floats<2>(index, offset); }
    // A position-like float3 as a record of the caller's with x, y, z (and w: `w`).
    template <typename Point>
    [[nodiscard]] Point point(size_t index, size_t offset = 0, float w = 1.0f) const
    {
        const std::array<float, 3> p = float3(index, offset);
        return {std::get<0>(p), std::get<1>(p), std::get<2>(p), w};
    }
    // The 32-bit word at `offset` in vertex `index` (a packed colour).
    [[nodiscard]] uint32_t word(size_t index, size_t offset) const { return value<uint32_t>(index, offset); }
    // Vertex `index`'s bytes (`stride` of them).
    [[nodiscard]] std::span<const uint8_t> vertex_bytes(size_t index) const
    {
        return detail::vertex_bytes(bytes_, stride_, index, 0, stride_);
    }
    // The value of type T (a plain number or record) at `offset` in vertex `index`.
    template <typename T>
    [[nodiscard]] T value(size_t index, size_t offset) const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        T out{};
        std::memcpy(&out, at(index, offset, sizeof(out)), sizeof(out));
        return out;
    }

private:
    [[nodiscard]] const uint8_t* at(size_t index, size_t offset, size_t size) const
    {
        return detail::vertex_bytes(bytes_, stride_, index, offset, size).data();
    }

    std::span<const uint8_t> bytes_;
    size_t stride_;
};

class VertexWriter {
public:
    VertexWriter(std::span<uint8_t> bytes, size_t stride) : bytes_(bytes), stride_(stride) {}
    // `count` vertices of `stride` bytes at `bytes`.
    VertexWriter(uint8_t* bytes, size_t stride, size_t count) : bytes_(bytes, stride * count), stride_(stride) {}

    [[nodiscard]] size_t count() const { return stride_ ? bytes_.size() / stride_ : 0; }

    template <size_t N>
    void set_floats(size_t index, size_t offset, const std::array<float, N>& values) const
    {
        std::memcpy(at(index, offset, sizeof(values)), values.data(), sizeof(values));
    }
    void set_float3(size_t index, size_t offset, float x, float y, float z) const
    {
        set_floats<3>(index, offset, {x, y, z});
    }
    void set_word(size_t index, size_t offset, uint32_t value) const { set_value(index, offset, value); }
    // A record's x, y, z as the float3 at `offset`.
    template <typename Point>
    void set_point(size_t index, size_t offset, const Point& p) const
    {
        set_float3(index, offset, p.x, p.y, p.z);
    }
    // `value` (a plain number or record) at `offset` in vertex `index`.
    template <typename T>
    void set_value(size_t index, size_t offset, const T& value) const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        std::memcpy(at(index, offset, sizeof(value)), &value, sizeof(value));
    }
    // `size` bytes from `source` to `offset` in vertex `index`.
    void set_bytes(size_t index, size_t offset, const void* source, size_t size) const
    {
        std::memcpy(at(index, offset, size), source, size);
    }
    [[nodiscard]] VertexView view() const { return {bytes_, stride_}; }

private:
    [[nodiscard]] uint8_t* at(size_t index, size_t offset, size_t size) const
    {
        return detail::vertex_bytes(bytes_, stride_, index, offset, size).data();
    }

    std::span<uint8_t> bytes_;
    size_t stride_;
};

// A vertex buffer of one drawn layout V (a record the card reads: the PC primitives' VertexColour...), its vertex
// `index` in place; and the position (three floats at the start) of vertex `index` in a buffer of any layout - both
// checked against the buffer's bytes.
template <typename V>
V& record_at(std::span<uint8_t> vertices, size_t index)
{
    return *raw::as<V>(detail::vertex_bytes(vertices, sizeof(V), index, 0, sizeof(V)).data());
}
inline std::span<float, 3> position_at(std::span<uint8_t> vertices, size_t stride, size_t index)
{
    return std::span<float, 3>(raw::as<float>(detail::vertex_bytes(vertices, stride, index, 0, 3 * sizeof(float)).data()), 3);
}

} // namespace gpu
