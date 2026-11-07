#pragma once

// NOT ORIGINAL - the editor mode's loading dialog (engine/gfx/loading_dialog.h), drawn in place of the loading screen:
// the editor's page with the box in its middle, by a timer of its own (every 33 ms, on winmm's timer thread, as the
// loading bar's) while the game loads on the main thread. Its fonts (Inter, at the editor's sizes) and their texture
// are made when it is shown, its draw list kept between frames (no ImGui context; nothing allocated once it has grown).
namespace render::loading_dialog_panel {

// The loading screen shown (render_pc/loading_screen.cpp, with its lock held): the dialog drawn at once and then by
// its timer.
void show();
// The loading screen gone (its lock held): the timer stopped.
void hide();
// Whether the dialog stands in for the loading screen now (the loading bar's and the console's timers draw nothing).
bool up();

} // namespace render::loading_dialog_panel
