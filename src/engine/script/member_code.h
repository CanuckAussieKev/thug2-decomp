#pragma once

#include <cstdint>
#include <type_traits>

// The code in a script member function table's entry (MemberFunctionEntry::method_code_, filled at start-up by
// member_function_code.cpp): a plain function per member function that calls it on the object - so the objects'
// dispatchers call an ordinary function pointer, not the member function's code cast to a __thiscall pointer.
// (The original's entry holds the member function pointer itself.)
namespace script {

class Struct;
class Script;

// self: the object (already adjusted by the entry's this_adjustment_); callsign: the entry's name (for the member
// functions that take it). The member function's result as an int (a bool as 0 / 1).
using MemberCode = int (*)(void* self, uint32_t callsign, Struct* params, Script* script);

template <typename Method>
struct MemberClass;
template <typename Result, typename Class, typename... Args>
struct MemberClass<Result (Class::*)(Args...)> {
    using Type = Class;
};

template <auto Method>
int call_member_function(void* self, uint32_t callsign, Struct* params, Script* script)
{
    if constexpr (!std::is_member_function_pointer_v<decltype(Method)>) {
        // (a static member function: the entry's object isn't used)
        if constexpr (std::is_invocable_v<decltype(Method), uint32_t, Struct*, Script*>)
            return static_cast<int>(Method(callsign, params, script));
        else
            return static_cast<int>(Method(params, script));
    } else {
        using Class = MemberClass<decltype(Method)>::Type;
        auto* object = static_cast<Class*>(self);
        if constexpr (std::is_invocable_v<decltype(Method), Class*, uint32_t, Struct*, Script*>)
            return static_cast<int>((object->*Method)(callsign, params, script));
        else
            return static_cast<int>((object->*Method)(params, script));
    }
}

// The entry's code for `Method` (`bool / int Class::f(Struct*, Script*)`, or with the callsign first).
template <auto Method>
constexpr MemberCode member_code()
{
    return &call_member_function<Method>;
}

// A callback that runs a member function with no arguments on an object (the walk component's animation-end
// callback): one plain function per member function.
using ObjectCallback = void (*)(void* self);

template <auto Method>
void call_object_method(void* self)
{
    using Class = MemberClass<decltype(Method)>::Type;
    (static_cast<Class*>(self)->*Method)();
}

template <auto Method>
constexpr ObjectCallback object_callback()
{
    return &call_object_method<Method>;
}

} // namespace script
