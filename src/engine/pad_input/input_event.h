#pragma once

#include <cstdint>

#include "base/core/api.h"

union SDL_Event;

// NOT ORIGINAL - an input event as the handlers we added see it (the level editor, the new front end, the mouse menus,
// the screenshot key): the input source that has the window's events (SDL's, engine/pad_input/sdl_input.h) makes one
// for each event it takes from the window and hands it to every handler added here, in the order they were added,
// after its own handling - where the game's SDL event handlers stood. A call per event, never per key or frame.
namespace input {

// The kinds the handlers tell apart (everything else: kOther).
enum class EventType : uint8_t {
    kOther,
    kKeyDown,
    kKeyUp,
    kTextInput,
    kTextEditing,
    kMouseMotion,
    kMouseButtonDown,
    kMouseButtonUp,
    kMouseWheel,
    kPadButtonDown,
    kPadButtonUp,
    kPadAxis,
};

// The mouse's buttons (`Event::button`).
constexpr uint8_t kMouseLeft = 1;
constexpr uint8_t kMouseMiddle = 2;
constexpr uint8_t kMouseRight = 3;
// The modifier keys held (`Event::modifiers`): either Shift, Ctrl, Alt.
constexpr uint16_t kModifierShift = 0x0003;
constexpr uint16_t kModifierCtrl = 0x00C0;
constexpr uint16_t kModifierAlt = 0x0300;

struct Event {
    EventType type = EventType::kOther;
    // A key (kKeyDown / kKeyUp): the key - SDL's scan code, the USB keyboard usage, as in the keyboard's state
    // (input::keys) -, whether it is the key's auto-repeat, the modifier keys held.
    int key = 0;
    bool repeat = false;
    uint16_t modifiers = 0;
    // The mouse: where it is in the window's pixels (motion and buttons), how far it moved (motion), its button (1-3),
    // the wheel's turn (positive away from the user, as reported: `wheel_flipped` when the system reverses it) and the
    // window it is over.
    int x = 0;
    int y = 0;
    int dx = 0;
    int dy = 0;
    uint8_t button = 0;
    int wheel = 0;
    bool wheel_flipped = false;
    uint32_t window = 0;
    // A game controller: its button (SDL's game controller numbering) or its axis and the axis's value
    // (-32768..32767; the triggers 0..32767).
    uint8_t pad_button = 0;
    uint8_t pad_axis = 0;
    int16_t pad_value = 0;
    // The source's own event, only while the event is being handed round (never kept): what Dear ImGui's platform
    // backend reads - text typed and edited, the keys, the mouse and the wheel, the window's -
    // render::UiBackend::process_event. Nothing else reads it.
    const SDL_Event* native = nullptr;

    // The event to keep for later (a queue): without the source's own.
    [[nodiscard]] Event kept() const
    {
        Event copy = *this;
        copy.native = nullptr;
        return copy;
    }
};

using EventHandler = void (*)(const Event& event);

// A handler added after the others (start-up: ext::prepare_input; the level editor's simulation, later).
THUG2_API void add_event_handler(EventHandler handler);
// The source that has the window's events: an event handed to every handler, in order.
void deliver(const Event& event);

} // namespace input
