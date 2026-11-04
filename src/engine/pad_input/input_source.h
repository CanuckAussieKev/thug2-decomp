#pragma once

#include <cstdint>
#include <span>

// NOT ORIGINAL - the input seam: where the game's input comes from. A source is one kind of device the game reads -
// SDL's game controllers and keyboard (engine/pad_input/sdl_input.h), the test-automation channel's pressed buttons
// (devtools/automation.h) - and app/ adds them at start-up (app/main.cpp), in the order the original's input read
// them: that order is the call order at every point below, and where several sources give a port's buttons the
// later ones add to what the earlier ones gave.
//
// The grain is a call per source per start-up, frame or port poll - never per key: a source fills a port's control
// bytes in one call, as the original's pad read did.
namespace input {

// A port's poll (sio::InputPort::work_on, once a frame for each port): the port, its control bytes (sio's PS2 format,
// engine/pad_input/device.h: a set bit is a button down until the poll inverts them at the end), whether an extension
// has the keyboard and the controllers this frame (ext::game_input_blocked: the game reads none of them) - and, set by
// a source that read a device for the port, whether one is connected.
struct PortPoll {
    int port = 0;
    std::span<uint8_t> data;
    bool blocked = false;
    bool connected = false;
};

class Source {
public:
    Source() = default;
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
    Source(Source&&) = delete;
    Source& operator=(Source&&) = delete;

    // --- start-up (os_layer::init_input, once the window is made) ---
    // Its devices, settings and event handler.
    virtual void start() {}

    // --- a port's poll ---
    // First, before the control bytes are cleared: what the source does before any port reads (SDL: the pending
    // events handled).
    virtual void begin_poll(int /*port*/) {}
    // Its buttons, sticks and pressures for the port, added to the control bytes.
    virtual void poll_port(PortPoll& /*poll*/) {}
    // A port's two motors (0-65535, `high` and `low` frequency): true if the source has the port's motors.
    virtual bool rumble(int /*port*/, uint16_t /*high*/, uint16_t /*low*/) { return false; }

protected:
    ~Source() = default;
};

// app/'s start-up: a source added after the others (before anything below is called).
void add_source(Source& source);

// The dispatch: each calls the sources in their order.
void start();
void begin_poll(int port);
void poll_port(PortPoll& poll);
void rumble(int port, uint16_t high, uint16_t low);

} // namespace input
