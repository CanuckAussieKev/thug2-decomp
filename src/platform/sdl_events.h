#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

union SDL_Event;

// The SDL event pump that replaces the game window's Win32 message pump (the window is SDL's; see
// sdl_window.h). Each event goes to every registered handler, in the order they were registered: the quit
// request (sys::init_pc_setup), the window's focus (sys::create_sdl_window), then input (the SDL input source, engine/pad_input/sdl_input.h).
namespace os_layer {

using EventHandler = void (*)(SDL_Event* event);

// The handler list, empty (the start-up registers the first handler: SDL_EVENT_QUIT asks the game to quit, 0x007D6A2C -
// sys::init_pc_setup).
void init_events();
void register_event_handler(EventHandler handler);
// Hand every pending event to the handlers (os_layer::process_window_events, and Sio's device processing).
void pump_sdl_events();

// 0x005F56E0: handle the window's pending messages (Originally from PartyMod Fixes: the SDL event pump, os_layer::pump_sdl_events).
void process_window_events();

} // namespace os_layer
