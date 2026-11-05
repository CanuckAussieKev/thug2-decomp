#pragma once

#include <concepts>
#include <type_traits>

// NOT ORIGINAL - helpers for the scoped enums (`enum class X : <type>`) that name the values of one closed thing.
//
// - core::to_underlying(e): the enumerator's number, where the value goes out as bytes (a file, the wire, a table
//   index) - std::to_underlying of C++23.
// - Flag enums: a set of bits declared `enum class Flags : uint16_t { kA = 1u << 0, ... }` and switched to flag
//   operators by declaring, next to it, `constexpr bool enable_flag_operators(Flags) { return true; }` (found by
//   argument-dependent lookup; it is never called). Such an enum then has |, &, ^, ~ and |=, &=, ^=, and
//   core::any(flags) for "any bit set". Values the original stores that the enum doesn't list stay representable
//   (an enum class holds any value of its type).
namespace core {

template <typename E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr std::underlying_type_t<E> to_underlying(E value) noexcept
{
    return static_cast<std::underlying_type_t<E>>(value);
}

template <typename E>
concept FlagEnum = std::is_enum_v<E> && requires(E value) {
    { enable_flag_operators(value) } -> std::same_as<bool>;
};

// Whether any bit of `flags` is set (`any(flags & Flags::kA)`: whether kA is).
template <FlagEnum E>
[[nodiscard]] constexpr bool any(E flags) noexcept
{
    return to_underlying(flags) != 0;
}

// Whether every bit of `wanted` is set in `flags`.
template <FlagEnum E>
[[nodiscard]] constexpr bool all_of(E flags, E wanted) noexcept
{
    return (to_underlying(flags) & to_underlying(wanted)) == to_underlying(wanted);
}

// A flag enum from its raw bits (a field the boundary layer read, an integer the original kept).
template <FlagEnum E>
[[nodiscard]] constexpr E flags_from(std::underlying_type_t<E> bits) noexcept
{
    return static_cast<E>(bits);
}

} // namespace core

// The operators are in the global namespace so that unqualified use finds them from every namespace (no namespace of
// ours declares a | or & of its own); the constraint limits them to the enums that ask for them.
template <core::FlagEnum E>
[[nodiscard]] constexpr E operator|(E a, E b) noexcept
{
    return static_cast<E>(core::to_underlying(a) | core::to_underlying(b));
}

template <core::FlagEnum E>
[[nodiscard]] constexpr E operator&(E a, E b) noexcept
{
    return static_cast<E>(core::to_underlying(a) & core::to_underlying(b));
}

template <core::FlagEnum E>
[[nodiscard]] constexpr E operator^(E a, E b) noexcept
{
    return static_cast<E>(core::to_underlying(a) ^ core::to_underlying(b));
}

template <core::FlagEnum E>
[[nodiscard]] constexpr E operator~(E a) noexcept
{
    return static_cast<E>(static_cast<std::underlying_type_t<E>>(~core::to_underlying(a)));
}

template <core::FlagEnum E>
constexpr E& operator|=(E& a, E b) noexcept
{
    return a = a | b;
}

template <core::FlagEnum E>
constexpr E& operator&=(E& a, E b) noexcept
{
    return a = a & b;
}

template <core::FlagEnum E>
constexpr E& operator^=(E& a, E b) noexcept
{
    return a = a ^ b;
}
