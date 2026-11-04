// The SDL event pump.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/sdl_events.h"

#include <memory>
#include <vector>

#include <SDL2/SDL.h>

#include "base/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"
#include <gsl/pointers>

namespace os_layer {

namespace {

std::unique_ptr<std::vector<EventHandler>>& handlers()
{
    static core::NoDestroy<std::unique_ptr<std::vector<EventHandler>>> handlers; // (made by init_events, never freed)
    return handlers.get();
}

void handle_quit(SDL_Event* event)
{
    if (event->type == SDL_QUIT) {
        render::engine_parts().set_quit_requested(true);
    }
}

} // namespace

void init_events()
{
    handlers() = std::make_unique<std::vector<EventHandler>>();
    handlers()->reserve(4);
    register_event_handler(handle_quit);
}

void register_event_handler(EventHandler handler) { handlers()->push_back(handler); }

void pump_sdl_events()
{
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        for (EventHandler handler : *handlers()) {
            handler(&event);
        }
    }
}

// 0x005F56E0. Originally from PartyMod Fixes: the SDL event pump (which also sees the quit request; PartyMod jumped from here). The
// original pumped the game window's (0x007D6A28) Win32 messages:
//     MSG msg;
//     while (PeekMessageA(&msg, window, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
void process_window_events() { os_layer::pump_sdl_events(); }

} // namespace os_layer
