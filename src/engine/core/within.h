#pragma once

// A pointer that a reader returned through a const view of a buffer, as a pointer into the buffer itself (`at` must
// point into `buffer`'s array) - for readers that take and return const pointers (they only read) when the caller's
// buffer is its own to change, as strchr's C++ overloads do. No const_cast: the position is recomputed from `buffer`.
template <typename T>
constexpr T* mutable_within(T* buffer, const T* at)
{
    return buffer + (at - buffer);
}
