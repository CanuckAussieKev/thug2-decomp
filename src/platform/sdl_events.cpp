// The SDL event pump.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/sdl_events.h"

#include <vector>

#include <SDL2/SDL.h>

#include "engine/gfx/movie.h"

namespace os_layer {

namespace {

std::vector<EventHandler>* s_handlers = nullptr; // (made by init_events, never freed)

bool& quit_requested() { return render::g_quit_requested; }

void handle_quit(SDL_Event* event)
{
    if (event->type == SDL_QUIT)
        quit_requested() = true;
}

} // namespace

void init_events()
{
    s_handlers = new std::vector<EventHandler>();
    s_handlers->reserve(4);
    register_event_handler(handle_quit);
}

void register_event_handler(EventHandler handler) { s_handlers->push_back(handler); }

void pump_sdl_events()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        for (EventHandler handler : *s_handlers)
            handler(&event);
    }
}

} // namespace os_layer
