// NOT ORIGINAL - the input sources' registry and dispatch (input_source.h).

#include "engine/pad_input/input_source.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"

namespace input {

namespace {

constexpr size_t kMaxSources = 4;

// NOT ORIGINAL grouping - the sources app/ added, in their order. Made on first use, never destroyed (registry()).
struct Registry {
    std::array<Source*, kMaxSources> sources_{};
    size_t count_{};
};

Registry& registry()
{
    static core::NoDestroy<Registry> state;
    return state.get();
}

std::span<Source* const> sources() { return std::span<Source* const>(registry().sources_).first(registry().count_); }

} // namespace

void add_source(Source& source)
{
    // (a fifth ends the game with core::at's report)
    core::at(registry().sources_, registry().count_++) = &source;
}

void start()
{
    for (Source* source : sources()) {
        source->start();
    }
}

void begin_poll(int port)
{
    for (Source* source : sources()) {
        source->begin_poll(port);
    }
}

void poll_port(PortPoll& poll)
{
    for (Source* source : sources()) {
        source->poll_port(poll);
    }
}

void rumble(int port, uint16_t high, uint16_t low)
{
    for (Source* source : sources()) {
        if (source->rumble(port, high, low)) {
            return;
        }
    }
}

} // namespace input
