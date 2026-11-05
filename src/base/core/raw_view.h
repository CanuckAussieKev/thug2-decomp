#pragma once

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <span>
#include <type_traits>

// NOT ORIGINAL - the views of the original's in-memory layouts still left: each call is a place where the code reads
// an object through a type it wasn't declared with, or a field at a byte offset, as the original's code did. They are
// named here instead of being written as reinterpret_casts, so they can be found and counted, and each given its
// real type or member and removed in turn. (Bytes from outside the game - files, network messages - are
// read through the boundary layer's readers instead: src/formats, src/wire.) Nothing is converted: the same address,
// const kept.
namespace raw {

// Storage seen as a T: an object reached through a pointer of another type (a byte buffer, a pool slot, a class it
// doesn't derive from, a pointer of another layout with the same fields).
template <typename T, typename P>
auto as(P* storage) noexcept
{
    constexpr bool kConst = std::is_const_v<P> || std::is_const_v<T>;
    using Address = std::conditional_t<kConst, const void, void>;
    using Result = std::conditional_t<kConst, const T, T>;
    auto* const address = static_cast<Address*>(storage);
    return static_cast<Result*>(address);
}

// The object's bytes (its object representation - reading them is defined, as with std::as_bytes).
template <typename P>
auto bytes(P* object) noexcept
{
    return as<uint8_t>(object);
}

// The same bytes as characters (a text kept in a byte buffer, or a byte buffer handed to a text function).
template <typename P>
auto chars(P* object) noexcept
{
    return as<char>(object);
}

// The field of type T at byte `offset` in `object`.
template <typename T, typename P, typename Offset>
auto field_at(P* object, Offset offset) noexcept
{
    return as<T>(bytes(object) + offset);
}

// The object of type Outer that holds `member` `offset` bytes in (offsetof): an embedded member's owner.
template <typename Outer, typename P>
auto owner_of(P* member, size_t offset) noexcept
{
    return as<Outer>(bytes(member) - offset);
}

// The original's whole-object clear (memset of the object to zero bytes) of an object that isn't trivially copyable:
// every byte zeroed - a vtable pointer too, as there.
template <typename T>
void zero_object(T* object) noexcept
{
    std::ranges::fill(std::span<uint8_t>(bytes(object), sizeof(T)), uint8_t{0});
}

// The original's whole-object copy (memcpy of `from` over `to`) of an object that isn't trivially copyable: its
// bytes as they are, pointers shared.
template <typename T>
void copy_object(T* to, const T* from) noexcept
{
    std::ranges::copy(std::span<const uint8_t>(bytes(from), sizeof(T)), bytes(to));
}

} // namespace raw
