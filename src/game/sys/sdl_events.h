#pragma once

// Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt

union SDL_Event;

// The SDL event pump that replaces the game window's Win32 message pump (the window is SDL's; see
// sdl_window.h). Each event goes to every registered handler, in the order they were registered: the quit
// request (InitEvents), the window's focus (Sys::CreateSdlWindow), then input (Sio::InitSdlInput).
namespace Platform {

using EventHandler = void (*)(SDL_Event* event);

// The handler list, with the first handler: SDL_QUIT asks the game to quit (0x007D6A2C).
void InitEvents();
void RegisterEventHandler(EventHandler handler);
// Hand every pending event to the handlers (Platform::ProcessWindowEvents, and Sio's device processing).
void PumpSdlEvents();

} // namespace Platform
