#pragma once

#include <cstdint>

// The loading screen (Nx engine, PC): its picture (a texture loaded by name, the language's version in "fr/" or
// "gr/"), shown full screen; the bar's timer that draws it while the game loads (loading_bar.h); taking it down.
// Everything runs under the loading screen's lock (0x007052AC): the timer draws on winmm's thread.
namespace Nx {

// 0x004B6CD0: the picture over the whole back buffer (its texture's rows upside down, as the original), linear
// filtering (the minification filter put back after), no lighting or blending.
void __cdecl DrawLoadingScreenPicture();
// 0x004B6F10: the bar's timer stopped, the screen down, fog back as it was (0x00787338).
void __cdecl HideLoadingScreen();
// 0x004B6F70: the loading screen's second picture (0x007052B0, a sprite) deleted, fog back as it was.
void __cdecl FreeLoadingScreenSprite();
// 0x004B7400: the bar to fill in 0.6 x `seconds` (0.03 a tick), its colours, and its timer started (every 29 ms).
void __cdecl StartLoadingBar(float seconds);
// 0x004B78B0: the loading screen up, with fog off: just marked (`keep`), or black (`black`), or the picture `name` -
// loaded again each time (the old one deleted when the name changes, as the original; the new one replaces it in
// any case), drawn and presented. A picture that won't load leaves the screen down.
void __cdecl ShowLoadingScreen(const char* name, bool keep, bool black);

// Not original: the picture up is a level select picture ("loadscrn_sel_..." - [features]
// level_select_loading_screens): drawn cover-fitted, with a plate behind the bar (loading_bar.cpp).
bool SelectPictureUp();

extern uint8_t* g_loadingScreenPicture;

} // namespace Nx
