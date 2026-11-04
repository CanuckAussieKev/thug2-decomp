#pragma once

#include <algorithm>
#include <cstddef>
#include <span>

#include <gsl/assert>

// NOT ORIGINAL - bytes (or elements) copied from one run into another, the destination's room checked first: a run too
// long for it ends the game with a report instead of writing past it, as core::at does for one element.
namespace core {

template <typename T, std::size_t FromExtent, std::size_t ToExtent>
void copy_into(std::span<const T, FromExtent> from, std::span<T, ToExtent> to)
{
    Expects(to.size() >= from.size());
    std::ranges::copy(from, to.begin());
}

} // namespace core
