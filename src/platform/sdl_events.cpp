// The SDL event pump.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/sdl_events.h"

#include <vector>

#include <SDL2/SDL.h>

#include "engine/gfx/movie.h"

namespace OsLayer {

namespace {

std::vector<EventHandler>* s_handlers = nullptr; // (made by InitEvents, never freed)

bool& QuitRequested() { return Engine3D::g_quitRequested; }

void HandleQuit(SDL_Event* event)
{
    if (event->type == SDL_QUIT)
        QuitRequested() = true;
}

} // namespace

void InitEvents()
{
    s_handlers = new std::vector<EventHandler>();
    s_handlers->reserve(4);
    RegisterEventHandler(HandleQuit);
}

void RegisterEventHandler(EventHandler handler)
{
    s_handlers->push_back(handler);
}

void PumpSdlEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        for (EventHandler handler : *s_handlers)
            handler(&event);
    }
}

} // namespace Platform
