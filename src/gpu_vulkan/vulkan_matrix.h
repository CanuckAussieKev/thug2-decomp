#pragma once

#include <array>
#include <cstddef>
#include <utility>

#include "gpu_vulkan/ff_uniforms.h"

// NOT ORIGINAL - (round PERF2) the draws' 4 x 4 products with their elements named at compile time: every draw takes two
// or three (world x view, the motion's world x view x projection the frame before), three thousand draws a view at NO,
// and the products through checked indexing (core::at) were calls the compiler could not unroll. The same sums in the
// same order (0 + a0 b0 + a1 b1 + a2 b2 + a3 b3): the same floats as the loops they replace.
namespace render::vulkan {

namespace matrix_detail {
constexpr std::size_t kSide = 4;

template <std::size_t Row, std::size_t Column>
[[nodiscard]] inline float product_cell(const Float4x4& a, const Float4x4& b)
{
    float sum = 0.0F;
    sum += std::get<(Row * kSide) + 0>(a) * std::get<(0 * kSide) + Column>(b);
    sum += std::get<(Row * kSide) + 1>(a) * std::get<(1 * kSide) + Column>(b);
    sum += std::get<(Row * kSide) + 2>(a) * std::get<(2 * kSide) + Column>(b);
    sum += std::get<(Row * kSide) + 3>(a) * std::get<(3 * kSide) + Column>(b);
    return sum;
}

template <std::size_t... Cell>
[[nodiscard]] inline Float4x4 product(const Float4x4& a, const Float4x4& b, std::index_sequence<Cell...> /*cells*/)
{
    return Float4x4{product_cell<Cell / kSide, Cell % kSide>(a, b)...};
}
} // namespace matrix_detail

// a x b, Direct3D's row-major matrices (row vectors: v x a x b).
[[nodiscard]] inline Float4x4 matrix_product(const Float4x4& a, const Float4x4& b)
{
    return matrix_detail::product(a, b, std::make_index_sequence<kMatrixFloats>{});
}

} // namespace render::vulkan
