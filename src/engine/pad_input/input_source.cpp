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

bool devices_made()
{
    for (Source* source : sources()) {
        if (source->devices_made()) {
            return true;
        }
    }
    return false;
}

void start()
{
    for (Source* source : sources()) {
        source->start();
    }
}

void read_frame()
{
    for (Source* source : sources()) {
        source->read_frame();
    }
}

bool has_pad(int port)
{
    for (Source* source : sources()) {
        if (source->has_pad(port)) {
            return true;
        }
    }
    return false;
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

void set_pad_mode(int mode)
{
    for (Source* source : sources()) {
        source->set_pad_mode(mode);
    }
}

void set_default_keys(DefaultKeys keys)
{
    for (Source* source : sources()) {
        source->set_default_keys(keys);
    }
}

void read_pad(int port, sio::PadSnapshot* pad)
{
    for (Source* source : sources()) {
        if (source->read_pad(port, pad)) {
            return;
        }
    }
}

KeyState keys()
{
    for (Source* source : sources()) {
        const KeyState state = source->keys();
        if (!state.empty()) {
            return state;
        }
    }
    return {};
}

bool mouse(MouseState* out)
{
    for (Source* source : sources()) {
        if (source->mouse(out)) {
            return true;
        }
    }
    return false;
}

bool controller(ControllerState* out)
{
    for (Source* source : sources()) {
        if (source->controller(out)) {
            return true;
        }
    }
    return false;
}

void set_relative_mouse(bool on)
{
    for (Source* source : sources()) {
        if (source->set_relative_mouse(on)) {
            return;
        }
    }
}

int show_cursor(int toggle)
{
    for (Source* source : sources()) {
        int shown = 0;
        if (source->show_cursor(toggle, &shown)) {
            return shown;
        }
    }
    return -1;
}

bool window_size(uint32_t window, int* width, int* height)
{
    for (Source* source : sources()) {
        if (source->window_size(window, width, height)) {
            return true;
        }
    }
    return false;
}

bool key_down(int code)
{
    for (Source* source : sources()) {
        bool down = false;
        if (source->key_down(code, &down)) {
            return down;
        }
    }
    return false;
}

} // namespace input
