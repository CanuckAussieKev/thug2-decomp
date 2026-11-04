#pragma once

#include <cstdint>
#include <span>

namespace sio {
struct PadSnapshot;
} // namespace sio

// NOT ORIGINAL - the input seam: where the game's input comes from. A source is one kind of device the game reads -
// the original's DirectInput keyboard, joysticks and pad read (input_dinput/dinput_source.h), SDL's game controllers
// and keyboard (engine/pad_input/sdl_input.h), the test-automation channel's pressed buttons (devtools/automation.h) -
// and app/ adds them at start-up (app/main.cpp), in the order the original's input read them: that order is the call
// order at every point below, and where several sources give a port's buttons the later ones add to what the earlier
// ones gave. A point that answers a question asks the sources in turn until one does.
//
// The grain is a call per source per start-up, frame or port poll - never per key: a source fills a port's control
// bytes in one call, as the original's pad read did.
namespace input {

// What Enter and Escape stand for in the original pad read's default keys: a pressure button 0-7, or the button
// 0x10 / 0x20 (os_layer::set_default_enter_control / set_default_escape_control).
struct DefaultKeys {
    int enter = 0;
    int escape = 1;
};

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
    // Whether its devices are made already (init_input's first test: then it does nothing more).
    virtual bool devices_made() { return false; }
    // Its devices, settings and event handler.
    virtual void start() {}

    // --- once a frame (the main loop's first step, sio::read_keyboard: 0x005BDD60) ---
    // Its devices' state for the frame.
    virtual void read_frame() {}

    // --- a port ---
    // Whether it has a pad for the port (read for this frame: the original's joystick read), asked by the port's
    // state steps (sio::InputPort::acquire / detect / get_settle, the original's pad read).
    virtual bool has_pad(int /*port*/) { return false; }

    // --- a port's poll (sio::InputPort::work_on) ---
    // First, before the control bytes are cleared: what the source does before any port reads (SDL: the pending
    // events handled).
    virtual void begin_poll(int /*port*/) {}
    // Its buttons, sticks and pressures for the port, added to the control bytes.
    virtual void poll_port(PortPoll& /*poll*/) {}
    // A port's two motors (0-65535, `high` and `low` frequency): true if the source has the port's motors.
    virtual bool rumble(int /*port*/, uint16_t /*high*/, uint16_t /*low*/) { return false; }

    // --- the original's pad read (sio::InputPort::pull_data, which PartyMod's poll replaced: not called) ---
    // The keys it reads (0-3: os_layer::set_pad_mode's modes) and the buttons Enter and Escape stand for in the
    // default keys.
    virtual void set_pad_mode(int /*mode*/) {}
    virtual void set_default_keys(DefaultKeys /*keys*/) {}
    // The port's pad state (true if the source has one; `pad` cleared and filled).
    virtual bool read_pad(int /*port*/, sio::PadSnapshot* /*pad*/) { return false; }
    // Whether a key (a DirectInput key code) is down (true if the source has a keyboard state for it).
    virtual bool key_down(int /*code*/, bool* /*down*/) { return false; }

protected:
    ~Source() = default;
};

// app/'s start-up: a source added after the others (before anything below is called).
void add_source(Source& source);

// The dispatch: each calls the sources in their order.
bool devices_made();
void start();
void read_frame();
bool has_pad(int port);
void begin_poll(int port);
void poll_port(PortPoll& poll);
void rumble(int port, uint16_t high, uint16_t low);
void set_pad_mode(int mode);
void set_default_keys(DefaultKeys keys);
// (`pad` as the first source that has one left it; untouched if none has)
void read_pad(int port, sio::PadSnapshot* pad);
bool key_down(int code);

} // namespace input
