// NOT ORIGINAL - the level editor's UI backend (ours). See level_editor_ui_backend.h: the only file that includes
// Direct3D or an ImGui backend header.

#include "game/front/level_editor_ui_backend.h"

#include <d3d9.h>

#include <SDL2/SDL.h>

#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_sdl2.h"
#include "game/sys/sdl_window.h"
#include "hook/game.h"
#include "hook/log.h"
#include "imgui.h"

namespace Front::LevelEditor::UiBackend {

namespace {

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }

bool g_ready;
bool g_failedLogged;

} // namespace

bool Init()
{
    if (g_ready)
        return true;
    SDL_Window* window = Sys::SdlWindow();
    IDirect3DDevice9* device = Device();
    if (!window || !device) {
        if (!g_failedLogged)
            Log("level editor UI: no %s yet", window ? "device" : "window");
        g_failedLogged = true;
        return false;
    }
    if (!ImGui_ImplSDL2_InitForD3D(window)) {
        Log("level editor UI: ImGui's SDL2 backend refused the window");
        return false;
    }
    // (no controllers: the game has them; the UI is mouse and keyboard)
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);
    if (!ImGui_ImplDX9_Init(device)) {
        ImGui_ImplSDL2_Shutdown();
        Log("level editor UI: ImGui's Direct3D 9 backend refused the device");
        return false;
    }
    g_ready = true;
    Log("level editor UI: Dear ImGui %s on SDL2 and Direct3D 9", ImGui::GetVersion());
    return true;
}

bool Ready() { return g_ready; }

bool ProcessEvent(const SDL_Event* event) { return g_ready && ImGui_ImplSDL2_ProcessEvent(event); }

void NewFrame()
{
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplSDL2_NewFrame();
}

void Render()
{
    ImDrawData* data = ImGui::GetDrawData();
    if (!g_ready || !data || !data->Valid)
        return;
    // Window pixels -> back buffer pixels (the same unless the window shows a back buffer of another size).
    int backWidth = 0, backHeight = 0;
    BackBufferSize(&backWidth, &backHeight);
    if (backWidth <= 0 || backHeight <= 0 || data->DisplaySize.x <= 0.0f || data->DisplaySize.y <= 0.0f)
        return;
    float sx = static_cast<float>(backWidth) / data->DisplaySize.x, sy = static_cast<float>(backHeight) / data->DisplaySize.y;
    if (sx != 1.0f || sy != 1.0f) {
        for (ImDrawList* list : data->CmdLists) {
            for (ImDrawVert& vertex : list->VtxBuffer) {
                vertex.pos.x *= sx;
                vertex.pos.y *= sy;
            }
            for (ImDrawCmd& command : list->CmdBuffer)
                command.ClipRect = ImVec4(command.ClipRect.x * sx, command.ClipRect.y * sy, command.ClipRect.z * sx,
                                          command.ClipRect.w * sy);
        }
        data->DisplaySize = ImVec2(static_cast<float>(backWidth), static_cast<float>(backHeight));
    }
    // (the backend saves the device's whole state in a state block and puts it back after: the renderer's cached
    // state stays true)
    ImGui_ImplDX9_RenderDrawData(data);
}

void InvalidateDeviceObjects()
{
    if (g_ready)
        ImGui_ImplDX9_InvalidateDeviceObjects();
}

void WindowSize(int* width, int* height)
{
    *width = *height = 0;
    if (SDL_Window* window = Sys::SdlWindow())
        SDL_GetWindowSize(window, width, height);
}

void BackBufferSize(int* width, int* height)
{
    *width = GameVariable<int>(0x00786D6C);
    *height = GameVariable<int>(0x00786D70);
}

uint64_t TextureId(void* rendererTexture)
{
    // (the Direct3D 9 backend takes an IDirect3DTexture9*)
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(static_cast<IDirect3DTexture9*>(rendererTexture)));
}

} // namespace Front::LevelEditor::UiBackend
