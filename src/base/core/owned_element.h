#pragma once

#include <gsl/pointers>

namespace memory {

// NOT ORIGINAL - an object a container owns (a list's or a table's element, a pool's record) as the owner the
// container's own code frees or hands on: the container's slots are plain pointers - links, template arguments,
// void* values - which can't carry gsl::owner; the call marks where the container gives the ownership up.
template <typename T>
gsl::owner<T*> owned_element(T* element) noexcept
{
    return element;
}

} // namespace memory
