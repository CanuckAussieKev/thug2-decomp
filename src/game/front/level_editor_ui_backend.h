#pragma once

#include <cstdint>

union SDL_Event;

// NOT ORIGINAL - the level editor's UI layer, its backend (ours; see level_editor_ui.h): the one file that ties Dear
// ImGui to the platform and the renderer - ImGui's official SDL2 platform backend (the game's window and input are
// SDL2's: game/sys/sdl_window.h, game/sio/sdl_input.cpp) and its Direct3D 9 renderer backend (the game's device,
// 0x007D69E0). Renderer-independence rules (the user's requirement - OpenGL or Vulkan later only swap this file):
//   - only level_editor_ui_backend.cpp includes Direct3D or an ImGui backend header;
//   - every gizmo and line is drawn through ImGui draw lists (level_editor_ui.cpp), projected to the screen;
//   - a texture handed to ImGui goes through TextureId.
// ImGui's coordinates are the window's pixels (what SDL's mouse events are in); Render scales its draw data to the
// back buffer when the two differ.
namespace Front::LevelEditor::UiBackend {

// The backends started on the game's window and device (once; false - logged - while either is missing or a
// backend refuses). An ImGui context must be current.
bool Init();
bool Ready();
// An SDL event for ImGui (true: ImGui used it).
bool ProcessEvent(const SDL_Event* event);
// The frame's start (before ImGui::NewFrame): the platform backend (window size, time, cursor) and the renderer
// backend (its device objects made again if a reset dropped them).
void NewFrame();
// ImGui's draw data (after ImGui::Render) drawn on the device, scaled from the window's pixels to the back buffer's.
void Render();
// Before the device is reset (Mlp::ResetDeviceIfLost: alt-tab, a lost device): ImGui's default-pool objects dropped;
// NewFrame makes them again. Also after the fonts change (the font texture is made again).
void InvalidateDeviceObjects();
// The window's size in pixels (ImGui's display size) and the back buffer's.
void WindowSize(int* width, int* height);
void BackBufferSize(int* width, int* height);
// The one place a renderer texture becomes an ImGui texture id.
uint64_t TextureId(void* rendererTexture);

// An off-screen picture (the Add palette's preview, level_editor_preview.h): a square colour target of `size` pixels
// with its own depth surface, made on first use (again when the size changes) and freed by FreeOffscreen and with the
// device objects (InvalidateDeviceObjects: before a reset). BeginOffscreen makes it the device's target (the target,
// depth surface, viewport and the world / view / projection transforms saved first), cleared to `argb`; the game's
// renderer then draws into it; EndOffscreen puts back what was saved. False (nothing switched): no device or the
// target couldn't be made (logged once).
bool BeginOffscreen(int size, uint32_t argb);
void EndOffscreen();
// The picture's ImGui texture id (0: none made).
uint64_t OffscreenTexture();
void FreeOffscreen();

} // namespace Front::LevelEditor::UiBackend
