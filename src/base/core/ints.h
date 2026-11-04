#pragma once

#include <concepts>
#include <type_traits>

// NOT ORIGINAL - the sign conversions the original's mixed-sign comparisons make, written out: an int compared with a
// 32-bit count as unsigned (a negative int is then a huge count), or a count compared as an int (a count past
// 0x7FFFFFFF is then negative). The same bits, the other signedness of the same width - what static_cast does between
// the two - so a comparison written with them gives the original's answer for every value. (std::cmp_less and its
// kin compare the values themselves: used where that is the same answer.)
namespace ints {

template <std::integral Value>
constexpr std::make_signed_t<Value> as_signed(Value value) noexcept
{
    return static_cast<std::make_signed_t<Value>>(value);
}

template <std::integral Value>
constexpr std::make_unsigned_t<Value> as_unsigned(Value value) noexcept
{
    return static_cast<std::make_unsigned_t<Value>>(value);
}

} // namespace ints
