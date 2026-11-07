#pragma once

union SDL_Event;

// NOT ORIGINAL - Dear ImGui's platform windows for our UI layers (ours; ui_backend.h: enable_viewports): a panel dragged
// out of the game's window becomes a window of its own on the desktop (ImGuiConfigFlags_ViewportsEnable - the level
// editor's context only, on a renderer that draws such windows: Vulkan's, gpu_vulkan/vulkan_ui_windows.cpp).
//
// The coordinates: with platform windows ImGui works in desktop coordinates, but the UI layers (the editor's projections,
// its panels' places, its mouse) work in the game window's pixels. So here the whole desktop is moved by the game
// window's position: ImGui's space is the game window's, its origin at the window's top-left corner, as without platform
// windows - the platform backend's window positions, the monitors and the mouse positions it reports are moved in, and the
// positions ImGui gives the platform moved out. When the game's window moves, the other windows' places are read again.
namespace frontend::ui_backend::viewports {

// Whether the current context has platform windows on (ImGuiConfigFlags_ViewportsEnable).
bool on();
// The current context's platform windows turned on, when its platform and renderer backends both draw them (the
// translation above installed in its platform calls); false - nothing changed - when they don't.
bool enable();
// The event for the platform backend (ImGui_ImplSDL3_ProcessEvent): a mouse movement's position moved into the game
// window's space (in `copy`), the others as they are.
const SDL_Event* for_platform(const SDL_Event* event, SDL_Event* copy);
// After the platform backend's frame start (ImGui_ImplSDL3_NewFrame): the mouse position it read from the desktop, the
// monitors and - when the game's window moved - the other windows' places, in the game window's space.
void after_frame_start();
// After the main viewport's draw data is drawn: the platform windows made, moved, shown or closed, and drawn.
void render_windows();

} // namespace frontend::ui_backend::viewports
