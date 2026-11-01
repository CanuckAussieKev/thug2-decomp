#pragma once

#include <concepts>
#include <type_traits>
#include <cstddef>
#include <utility>

#include <gsl/util>

// NOT ORIGINAL - indexing checked against the extent: core::at(container, index) is gsl::at (an index outside the
// array, std::array, std::vector or std::span ends the game with a report instead of reading or writing past it),
// taking any integer or enumerator index - the game indexes with ints, unsigned counts, bytes and plain enums - and
// converting it to GSL's signed index explicitly (an unsigned index too large for it reads as negative and fails the same check).
namespace core {

template <typename Container, typename Index>
    requires std::integral<Index> || std::is_enum_v<Index>
constexpr decltype(auto) at(Container&& container, Index index)
{
    return gsl::at(std::forward<Container>(container), static_cast<gsl::index>(index));
}

} // namespace core
