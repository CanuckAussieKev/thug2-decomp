#pragma once

#include <atomic>
#include <concepts>
#include <type_traits>
#include <cstddef>
#include <iterator>
#include <source_location>
#include <utility>

#include <gsl/util>

#include "base/core/api.h"
#include "base/core/compiler.h"

// NOT ORIGINAL - indexing checked against the extent: core::at(container, index) is gsl::at (an index outside the
// array, std::array, std::vector or std::span ends the game with a report instead of reading or writing past it),
// taking any integer or enumerator index - the game indexes with ints, unsigned counts, bytes and plain enums - and
// converting it to GSL's signed index explicitly (an unsigned index too large for it reads as negative and fails the same check).
//
// Play-testing aid, [debug] soft_bounds=1 (thug2-decomp.ini, default 0 - platform/crash.cpp installs the handler): an
// index outside the extent is reported (once per place, with the calls that led there) and the game carries on - a
// read gets a zeroed element, a write goes to a throwaway one (an array of enumerators still ends the game) - so one session collects every such place instead of
// ending at the first. Off, nothing changes: the game ends with a report as above.
namespace core {

// Called for an index outside the extent while soft_bounds is on: where, the index and the extent.
using OutOfRangeHandler = void (*)(const std::source_location& where, std::ptrdiff_t index, std::ptrdiff_t extent);

namespace detail {
// (out of line, checked_index.cpp: one handler for the exe and its plugins - an inline function's static would be a
// second one in each plugin)
THUG2_API std::atomic<OutOfRangeHandler>& out_of_range_slot();

// The throwaway element an out-of-range index gets under soft_bounds (zeroed again each time).
template <typename Element>
Element& stand_in_element()
{
    static thread_local Element element{};
    element = Element{};
    return element;
}

// An index of any integer or enumerator type as GSL's signed index (a byte widens through int first, by its value).
template <typename Index>
constexpr gsl::index to_index(Index index)
{
    if constexpr (sizeof(Index) < sizeof(int)) {
        return static_cast<gsl::index>(static_cast<int>(index));
    } else {
        return static_cast<gsl::index>(index);
    }
}
} // namespace detail

inline void set_out_of_range_handler(OutOfRangeHandler handler) { detail::out_of_range_slot().store(handler); }
[[nodiscard]] inline OutOfRangeHandler out_of_range_handler()
{
    return detail::out_of_range_slot().load(std::memory_order_relaxed);
}

namespace detail {
// An index outside the extent: reported and the stand-in element given under soft_bounds, else gsl::at's failure (the
// game ends). Out of line, never inlined: the check in `at` is then a compare and a branch, small enough for the
// compiler to inline `at` - and the little accessors built on it - into the hot loops (the matrices' elements, the
// draws' state).
template <typename Reference, typename Container>
THUG2_NOINLINE Reference out_of_range(Container&& container, gsl::index checked, const std::source_location& where)
{
    using Element = std::remove_cvref_t<Reference>;
    if (const OutOfRangeHandler handler = out_of_range_handler()) {
        handler(where, checked, static_cast<std::ptrdiff_t>(std::size(container)));
        return stand_in_element<Element>();
    }
    return gsl::at(std::forward<Container>(container), checked);
}
} // namespace detail

template <typename Container, typename Index>
    requires std::integral<Index> || std::is_enum_v<Index>
constexpr auto at(Container&& container, Index index, const std::source_location where = std::source_location::current())
    -> decltype(gsl::at(std::forward<Container>(container), gsl::index{}))
{
    const gsl::index checked = detail::to_index(index);
    using Reference = decltype(gsl::at(std::forward<Container>(container), gsl::index{}));
    using Element = std::remove_cvref_t<Reference>;
    if constexpr (!std::is_enum_v<Element> && std::is_default_constructible_v<Element> && std::is_move_assignable_v<Element>) {
        if (!std::is_constant_evaluated()) {
            const auto extent = static_cast<std::ptrdiff_t>(std::size(container));
            if (checked < 0 || checked >= extent) [[unlikely]] {
                return detail::out_of_range<Reference>(std::forward<Container>(container), checked, where);
            }
        }
    }
    return gsl::at(std::forward<Container>(container), checked);
}

} // namespace core
