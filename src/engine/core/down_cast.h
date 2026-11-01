#pragma once

#include <cassert>
#include <type_traits>

// NOT ORIGINAL - the checked downcast. The game's class hierarchies dispatch on their own type tags (an object's type,
// a component's type checksum, a screen element's type, a message's kind): code that has checked the tag - or that
// asked a manager for an object of a type - knows the derived class and converts the base pointer to it. That
// conversion is spelled down_cast<Derived>(base): in Debug builds it asserts that the object really is a Derived
// (dynamic_cast on polymorphic classes; a class may also say how its tag tells, with a static
// `is_instance(const Base&)`), in Release it is the plain static_cast the original's code did. A wrong tag stops a
// Debug build at the cast instead of reading the object as the wrong class.
namespace core {

namespace down_cast_detail {

template <typename Derived, typename Base>
concept TagChecked = requires(const Base& base) {
    { Derived::is_instance(base) } -> std::convertible_to<bool>;
};

template <typename Derived, typename Base>
bool is_a(const Base& base)
{
    if constexpr (TagChecked<Derived, Base>) {
        return Derived::is_instance(base);
    } else if constexpr (std::is_polymorphic_v<Base>) {
        return dynamic_cast<const Derived*>(&base) != nullptr;
    } else {
        return true;
    }
}

} // namespace down_cast_detail

// `base` (null allowed) as the Derived it is. Derived may be const-qualified; a const Base needs a const Derived.
template <typename Derived, typename Base>
Derived* down_cast(Base* base) noexcept
{
    using Plain = std::remove_cv_t<Derived>;
    static_assert(std::is_base_of_v<std::remove_cv_t<Base>, Plain>, "a downcast: Derived derives from Base");
    static_assert(!std::is_const_v<Base> || std::is_const_v<Derived>, "a downcast keeps const");
    assert(base == nullptr || down_cast_detail::is_a<Plain>(*base));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): the one downcast, checked above in Debug
    return static_cast<Derived*>(base);
}

// The same for a reference.
template <typename Derived, typename Base>
Derived& down_cast(Base& base) noexcept
{
    return *down_cast<Derived>(&base);
}

} // namespace core
