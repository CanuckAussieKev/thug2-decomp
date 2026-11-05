#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

#include <gsl/pointers>

// NOT ORIGINAL - a subsystem's state held for the whole run and never destroyed: the storage behind a state accessor.
//
//     SoundState& sound_state()
//     {
//         static core::NoDestroy<SoundState> state;
//         return state.get();
//     }
//
// - Made on first use (a function-local static): no static-initialisation order between files, and the state starts
//   as the original's .bss / .data did - value-initialised (zero) unless its members say otherwise - before anything
//   reads it.
// - Never destroyed: the game ends by ExitProcess (sys::fast_exit) or std::quick_exit (crt::exit_fatal), and nothing of
//   it may be torn down under the game's other threads even if a path ever reached exit(). What the original tore down
//   (a level unload, a module's shut-down) is the state's own reset, called where the original did it.
// - Its address never changes once made: a state's members may be registered elsewhere (callbacks, lists).
// - A trivially destructible state is held as a plain member with a constexpr constructor: a constant-initialised
//   static, so its accessor has no guard at all. Any other state is constructed in place in the static storage (the
//   guard is the function-local static's own).
// - Static storage, like the globals it replaces: nothing comes from the game's heaps.
namespace core {

template <typename State, bool = std::is_trivially_destructible_v<State>>
class NoDestroy;

// A state with nothing to destroy: held directly.
template <typename State>
class NoDestroy<State, true> {
public:
    template <typename... Args>
    constexpr explicit NoDestroy(Args&&... args) : value_{std::forward<Args>(args)...}
    {
    }
    NoDestroy(const NoDestroy&) = delete;
    NoDestroy& operator=(const NoDestroy&) = delete;
    ~NoDestroy() = default;

    [[nodiscard]] constexpr State& get() noexcept { return value_; }
    [[nodiscard]] constexpr const State& get() const noexcept { return value_; }

private:
    State value_;
};

// A state whose destructor must never run: constructed in place in static storage, never ended.
template <typename State>
class NoDestroy<State, false> {
public:
    template <typename... Args>
    explicit NoDestroy(Args&&... args) : value_(*make(storage_, std::forward<Args>(args)...))
    {
    }
    NoDestroy(const NoDestroy&) = delete;
    NoDestroy& operator=(const NoDestroy&) = delete;
    ~NoDestroy() = default;

    [[nodiscard]] State& get() noexcept { return value_.get(); }
    [[nodiscard]] const State& get() const noexcept { return value_.get(); }

private:
    using Storage = std::array<std::byte, sizeof(State)>;

    template <typename... Args>
    static gsl::owner<State*> make(Storage& storage, Args&&... args)
    {
        // Value-initialised: zero where the state's members don't say otherwise.
        return ::new (static_cast<void*>(storage.data())) State(std::forward<Args>(args)...);
    }

    alignas(State) Storage storage_{};
    // The state made in storage_ (never ended: the point of the class).
    std::reference_wrapper<State> value_;
};

} // namespace core
