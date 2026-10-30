#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "hook/hook.h"

// Shadow checks: comparing a replacement against the original on the game's own calls.
//
//   REPLACE_SHADOW(0x00401B90, Crc::GenerateCRCFromString);
//
// works like REPLACE, but for the first N calls the game makes (thug2-decomp.ini, [selftests]
// shadow_calls=N, default 100, 0 = off) the original also runs, through its trampoline, with the same
// arguments, and the two results are compared. A mismatch is logged with the arguments:
//
//   SHADOW MISMATCH 0x00401B90 Crc::GenerateCRCFromString(0x0064A2C4): original 0x1A2B3C4D, ours 0x...
//
// Only for functions that are safe to run twice: the result depends only on the arguments and on memory
// the function doesn't change (lookups, math, string helpers). Never for anything that allocates, frees,
// does I/O or changes state. Calls made while another shadow check is running on the same thread (e.g. the
// original calling other shadowed functions) aren't checked again.
//
// Supported: free functions (cdecl) returning void, a scalar, a pointer or a reference, and non-virtual
// member functions (thiscall) returning the same. Floating-point results are compared with a small relative
// tolerance: our code uses SSE, the original x87, so the last bits can differ.

namespace Shadow {

int CallLimit(); // from thug2-decomp.ini, read once
void CountCheckedCall();
// Logs how many calls have been checked so far, how many mismatched, and which shadowed functions the game
// hasn't called yet.
void LogSummary();
bool& InsideCheck(); // per thread
void ReportMismatch(uint32_t address, const char* name, const char* arguments, const char* expected,
                    const char* actual, int calls_checked);

template <typename T>
void Describe(char* out, size_t size, const T& value)
{
    using V = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<V, bool>)
        std::snprintf(out, size, "%s", value ? "true" : "false");
    else if constexpr (std::is_floating_point_v<V>)
        std::snprintf(out, size, "%.9g", static_cast<double>(value));
    else if constexpr (std::is_integral_v<V> || std::is_enum_v<V>)
        std::snprintf(out, size, "0x%X", static_cast<unsigned>(value));
    else if constexpr (std::is_pointer_v<V>)
        std::snprintf(out, size, "%p", static_cast<const void*>(value));
    else
        std::snprintf(out, size, "<%u-byte value at %p>", static_cast<unsigned>(sizeof(V)),
                      static_cast<const void*>(&value));
}

template <typename T>
bool Same(const T& a, const T& b)
{
    using V = std::remove_cvref_t<T>;
    if constexpr (std::is_floating_point_v<V>) {
        if (a == b || (a != a && b != b))
            return true;
        V scale = (a < 0 ? -a : a) > (b < 0 ? -b : b) ? (a < 0 ? -a : a) : (b < 0 ? -b : b);
        V diff = a > b ? a - b : b - a;
        return diff <= V(1e-5) * (scale > V(1) ? scale : V(1));
    } else {
        return a == b;
    }
}

template <typename... Args>
void DescribeArguments(char* out, size_t size, const Args&... args)
{
    out[0] = '\0';
    size_t used = 0;
    auto add = [&](const auto& arg) {
        char one[64];
        Describe(one, sizeof(one), arg);
        int n = std::snprintf(out + used, size - used, "%s%s", used ? ", " : "", one);
        if (n > 0)
            used = used + static_cast<size_t>(n) < size ? used + static_cast<size_t>(n) : size - 1;
    };
    (add(args), ...);
}

// One per shadowed function: counts the calls checked. Each registers itself for the summary.
struct Counter {
    Counter(uint32_t address, const char* name);
    uint32_t address;
    const char* name;
    std::atomic<int> calls{0};
};

// Decides whether this call is checked, and marks the thread as inside a check.
class Scope {
public:
    explicit Scope(Counter& counter)
    {
        if (InsideCheck())
            return;
        int limit = CallLimit();
        if (limit <= 0 || counter.calls.load(std::memory_order_relaxed) >= limit)
            return;
        m_checking = counter.calls.fetch_add(1, std::memory_order_relaxed) < limit;
        if (m_checking) {
            InsideCheck() = true;
            CountCheckedCall();
        }
    }
    ~Scope()
    {
        if (m_checking)
            InsideCheck() = false;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    bool Checking() const { return m_checking; }

private:
    bool m_checking = false;
};

template <typename R>
constexpr bool kSupportedResult = std::is_void_v<R> || std::is_scalar_v<R> || std::is_reference_v<R>;

template <uint32_t Address, auto Function, const char* Name>
struct Check;

// Free functions.
template <uint32_t Address, typename R, typename... Args, R (*Function)(Args...), const char* Name>
struct Check<Address, Function, Name> {
    static_assert(kSupportedResult<R>, "shadow checks need a void, scalar, pointer or reference result");
    static inline Counter counter{Address, Name};

    static R __cdecl Call(Args... args)
    {
        Scope scope(counter);
        if (!scope.Checking())
            return Function(args...);
        auto original = reinterpret_cast<R(__cdecl*)(Args...)>(OriginalFunction(Address));
        if constexpr (std::is_void_v<R>) {
            original(args...);
            Function(args...);
        } else if constexpr (std::is_reference_v<R>) {
            R expected = original(args...);
            R actual = Function(args...);
            Compare(&expected, &actual, args...);
            return actual;
        } else {
            R expected = original(args...);
            R actual = Function(args...);
            Compare(expected, actual, args...);
            return actual;
        }
    }

    template <typename T>
    static void Compare(const T& expected, const T& actual, const Args&... args)
    {
        if (Same(expected, actual))
            return;
        char arguments[256], e[64], a[64];
        DescribeArguments(arguments, sizeof(arguments), args...);
        Describe(e, sizeof(e), expected);
        Describe(a, sizeof(a), actual);
        ReportMismatch(Address, Name, arguments, e, a, counter.calls.load());
    }
};

// Member functions (thiscall). The hook is a fastcall function with an unused second parameter: MSVC passes
// `this` in ECX and the rest on the stack, popped by the callee, exactly like thiscall.
template <uint32_t Address, typename C, typename R, typename... Args>
struct MemberCheck {
    static_assert(kSupportedResult<R>, "shadow checks need a void, scalar, pointer or reference result");

    template <typename Ours>
    static R Run(Counter& counter, Ours ours, const char* name, C* self, Args... args)
    {
        Scope scope(counter);
        if (!scope.Checking())
            return (self->*ours)(args...);
        auto original = reinterpret_cast<R(__fastcall*)(C*, void*, Args...)>(OriginalFunction(Address));
        if constexpr (std::is_void_v<R>) {
            original(self, nullptr, args...);
            (self->*ours)(args...);
        } else {
            R expected = original(self, nullptr, args...);
            R actual = (self->*ours)(args...);
            bool same;
            if constexpr (std::is_reference_v<R>)
                same = &expected == &actual;
            else
                same = Same(expected, actual);
            if (!same) {
                char arguments[256], e[64], a[64];
                DescribeArguments(arguments, sizeof(arguments), static_cast<const void*>(self), args...);
                if constexpr (std::is_reference_v<R>) {
                    Describe(e, sizeof(e), &expected);
                    Describe(a, sizeof(a), &actual);
                } else {
                    Describe(e, sizeof(e), expected);
                    Describe(a, sizeof(a), actual);
                }
                ReportMismatch(Address, name, arguments, e, a, counter.calls.load());
            }
            return actual;
        }
    }
};

template <uint32_t Address, typename C, typename R, typename... Args, R (C::*Function)(Args...), const char* Name>
struct Check<Address, Function, Name> {
    static inline Counter counter{Address, Name};
    static R __fastcall Call(C* self, void*, Args... args)
    {
        return MemberCheck<Address, C, R, Args...>::Run(counter, Function, Name, self, args...);
    }
};

template <uint32_t Address, typename C, typename R, typename... Args, R (C::*Function)(Args...) const,
          const char* Name>
struct Check<Address, Function, Name> {
    static inline Counter counter{Address, Name};
    static R __fastcall Call(const C* self, void*, Args... args)
    {
        return MemberCheck<Address, const C, R, Args...>::Run(counter, Function, Name, self, args...);
    }
};

} // namespace Shadow

// Like REPLACE, with shadow checks (see the top of this file). The function must be named directly
// (overloads via static_cast are fine).
#define REPLACE_SHADOW(address, ...)                                                                 \
    static constexpr char REPLACE_CONCAT(g_shadow_name_, __LINE__)[] = #__VA_ARGS__;                 \
    static ReplacementRegistrar REPLACE_CONCAT(g_replace_, __LINE__)(                                \
        address, CodeAddress(&Shadow::Check<address, __VA_ARGS__, REPLACE_CONCAT(g_shadow_name_, __LINE__)>::Call), \
        kCalleePop<decltype(__VA_ARGS__)>, #__VA_ARGS__ " [shadow]", __FILE__)
#ifdef THUG2_STANDALONE
#undef REPLACE_SHADOW
#define REPLACE_SHADOW(address, ...) static_assert(true)
#endif
