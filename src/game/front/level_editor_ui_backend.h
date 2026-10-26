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

// Several ImGui contexts may use these backends (the editor's and the new front end's - game/front/new_frontend.h):
// Init / Ready / ProcessEvent / NewFrame / Render act on the CURRENT context (each keeps its own backend data).
// InvalidateContextObjects drops only the current context's device objects (its font texture), not the editor's
// pictures below.
void InvalidateContextObjects();

// A picture for ImGui made from texels in memory (the new front end's loading screens): `rows` rows of `rowBytes`
// bytes - 32-bit BGRA texels (A8R8G8B8), or rows of 4x4 DXT1 blocks. A managed texture (it survives a device reset);
// 0 when it can't be made. ReleaseTexture frees it.
enum class TextureFormat { Bgra32, Dxt1 };
uint64_t CreateTexture(int width, int height, TextureFormat format, const void* data, int rowBytes, int rows);
void ReleaseTexture(uint64_t id);

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

// The Add palette's thumbnails (level_editor_thumbs.h): one square atlas of `atlasSize` pixels with its own depth
// surface, made on first use and kept (freed by FreeAtlas and with the device objects, as the picture above).
// BeginAtlasCell makes it the device's target with the viewport on one cell (x, y, size in pixels) - the target, depth
// surface, viewport and transforms saved first as BeginOffscreen does - and clears that cell to `argb` (colour and
// depth); EndAtlasCell puts back what was saved. AtlasGeneration changes whenever the atlas is freed (its cells are
// then empty: what they held is drawn again).
bool BeginAtlasCell(int atlasSize, int x, int y, int size, uint32_t argb);
void EndAtlasCell();
uint64_t AtlasTexture();
uint32_t AtlasGeneration();
void FreeAtlas();

} // namespace Front::LevelEditor::UiBackend
