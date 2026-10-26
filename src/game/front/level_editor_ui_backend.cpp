// NOT ORIGINAL - the level editor's UI backend (ours). See level_editor_ui_backend.h: the only file that includes
// Direct3D or an ImGui backend header.

#include "game/front/level_editor_ui_backend.h"

#include <d3d9.h>

#include <cstring>

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

// Whether the backends are started for the current ImGui context: each context (the editor's, the new front end's -
// game/front/new_frontend.h) has its own backend data, which the backends keep in its IO.
bool ContextReady() { return ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData != nullptr; }
bool g_failedLogged;

// The off-screen picture: its colour target (X8R8G8B8: what the game's blending leaves in alpha doesn't show through
// ImGui's), its depth surface (the format of the game's own; no multisampling: the target has none), and what
// BeginOffscreen switched from.
struct Offscreen {
    IDirect3DTexture9* texture = nullptr;
    IDirect3DSurface9* depth = nullptr;
    int size = 0;
    bool stencil = false; // (the depth format has one: cleared with it)
    bool failedLogged = false;
    // (between Begin and End)
    bool active = false;
    IDirect3DSurface9* savedTarget = nullptr;
    IDirect3DSurface9* savedDepth = nullptr;
    D3DVIEWPORT9 savedViewport = {};
    D3DMATRIX savedTransforms[3] = {};
};
Offscreen g_offscreen;
constexpr D3DTRANSFORMSTATETYPE kSavedTransforms[3] = {D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION};

void ReleaseOffscreen()
{
    if (g_offscreen.texture)
        g_offscreen.texture->Release();
    if (g_offscreen.depth)
        g_offscreen.depth->Release();
    g_offscreen.texture = nullptr;
    g_offscreen.depth = nullptr;
    g_offscreen.size = 0;
}

bool MakeOffscreen(IDirect3DDevice9* device, int size)
{
    if (g_offscreen.texture && g_offscreen.size == size)
        return true;
    ReleaseOffscreen();
    D3DFORMAT depthFormat = D3DFMT_D24S8;
    IDirect3DSurface9* gameDepth = nullptr;
    if (SUCCEEDED(device->GetDepthStencilSurface(&gameDepth)) && gameDepth) {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(gameDepth->GetDesc(&desc)))
            depthFormat = desc.Format;
        gameDepth->Release();
    }
    HRESULT made = device->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT,
                                         &g_offscreen.texture, nullptr);
    if (SUCCEEDED(made))
        made = device->CreateDepthStencilSurface(size, size, depthFormat, D3DMULTISAMPLE_NONE, 0, TRUE,
                                                 &g_offscreen.depth, nullptr);
    if (FAILED(made)) {
        if (!g_offscreen.failedLogged)
            Log("level editor UI: no off-screen target of %d pixels (%08X)", size, static_cast<unsigned>(made));
        g_offscreen.failedLogged = true;
        ReleaseOffscreen();
        return false;
    }
    g_offscreen.size = size;
    g_offscreen.stencil = depthFormat == D3DFMT_D24S8 || depthFormat == D3DFMT_D24X4S4 ||
                          depthFormat == D3DFMT_D15S1 || depthFormat == D3DFMT_D24FS8;
    return true;
}

} // namespace

bool Init()
{
    if (ContextReady())
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
    Log("level editor UI: Dear ImGui %s on SDL2 and Direct3D 9", ImGui::GetVersion());
    return true;
}

bool Ready() { return ContextReady(); }

bool ProcessEvent(const SDL_Event* event) { return ContextReady() && ImGui_ImplSDL2_ProcessEvent(event); }

void NewFrame()
{
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplSDL2_NewFrame();
}

void Render()
{
    ImDrawData* data = ImGui::GetDrawData();
    if (!ContextReady() || !data || !data->Valid)
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
    if (ContextReady())
        ImGui_ImplDX9_InvalidateDeviceObjects();
    // (the picture is in the default pool: it must go before a reset; made again when next drawn)
    FreeOffscreen();
    FreeAtlas(); // (the thumbnails' atlas too: its cells drawn again - level_editor_thumbs.h)
}

void InvalidateContextObjects()
{
    if (ContextReady())
        ImGui_ImplDX9_InvalidateDeviceObjects();
}

uint64_t CreateTexture(int width, int height, TextureFormat format, const void* data, int rowBytes, int rows)
{
    IDirect3DDevice9* device = Device();
    if (!device || width <= 0 || height <= 0 || !data)
        return 0;
    IDirect3DTexture9* texture = nullptr;
    // (managed: Direct3D keeps a copy, so a device reset doesn't lose it)
    D3DFORMAT d3dFormat = format == TextureFormat::Dxt1 ? D3DFMT_DXT1 : D3DFMT_A8R8G8B8;
    if (FAILED(device->CreateTexture(width, height, 1, 0, d3dFormat, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
        return 0;
    D3DLOCKED_RECT locked;
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
        texture->Release();
        return 0;
    }
    const uint8_t* from = static_cast<const uint8_t*>(data);
    uint8_t* to = static_cast<uint8_t*>(locked.pBits);
    for (int row = 0; row < rows; ++row)
        std::memcpy(to + row * locked.Pitch, from + row * rowBytes, static_cast<size_t>(rowBytes));
    texture->UnlockRect(0);
    return TextureId(texture);
}

void ReleaseTexture(uint64_t id)
{
    if (id)
        reinterpret_cast<IDirect3DTexture9*>(static_cast<uintptr_t>(id))->Release();
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

bool BeginOffscreen(int size, uint32_t argb)
{
    IDirect3DDevice9* device = Device();
    if (!device || g_offscreen.active || size <= 0 || !MakeOffscreen(device, size))
        return false;
    IDirect3DSurface9* surface = nullptr;
    if (FAILED(g_offscreen.texture->GetSurfaceLevel(0, &surface)))
        return false;
    Offscreen& o = g_offscreen;
    device->GetRenderTarget(0, &o.savedTarget);
    device->GetDepthStencilSurface(&o.savedDepth);
    device->GetViewport(&o.savedViewport);
    for (int i = 0; i < 3; ++i)
        device->GetTransform(kSavedTransforms[i], &o.savedTransforms[i]);
    device->SetRenderTarget(0, surface);
    surface->Release();
    device->SetDepthStencilSurface(o.depth);
    D3DVIEWPORT9 viewport = {0, 0, static_cast<DWORD>(size), static_cast<DWORD>(size), 0.0f, 1.0f};
    device->SetViewport(&viewport);
    device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | (o.stencil ? D3DCLEAR_STENCIL : 0), argb, 1.0f, 0);
    o.active = true;
    return true;
}

void EndOffscreen()
{
    Offscreen& o = g_offscreen;
    IDirect3DDevice9* device = Device();
    if (!o.active || !device)
        return;
    device->SetRenderTarget(0, o.savedTarget);
    device->SetDepthStencilSurface(o.savedDepth);
    device->SetViewport(&o.savedViewport); // (after the target: setting one resets the viewport)
    for (int i = 0; i < 3; ++i)
        device->SetTransform(kSavedTransforms[i], &o.savedTransforms[i]);
    if (o.savedTarget)
        o.savedTarget->Release();
    if (o.savedDepth)
        o.savedDepth->Release();
    o.savedTarget = o.savedDepth = nullptr;
    o.active = false;
}

uint64_t OffscreenTexture() { return g_offscreen.texture ? TextureId(g_offscreen.texture) : 0; }

void FreeOffscreen()
{
    EndOffscreen();
    ReleaseOffscreen();
}

// --- the thumbnails' atlas ----------------------------------------------------------------------------------------

namespace {

struct Atlas {
    IDirect3DTexture9* texture = nullptr;
    IDirect3DSurface9* depth = nullptr;
    int size = 0;
    bool stencil = false;
    bool failedLogged = false;
    uint32_t generation = 1;
    bool active = false;
    IDirect3DSurface9* savedTarget = nullptr;
    IDirect3DSurface9* savedDepth = nullptr;
    D3DVIEWPORT9 savedViewport = {};
    D3DMATRIX savedTransforms[3] = {};
};
Atlas g_atlas;

void ReleaseAtlas()
{
    if (g_atlas.texture || g_atlas.depth)
        ++g_atlas.generation;
    if (g_atlas.texture)
        g_atlas.texture->Release();
    if (g_atlas.depth)
        g_atlas.depth->Release();
    g_atlas.texture = nullptr;
    g_atlas.depth = nullptr;
    g_atlas.size = 0;
}

bool MakeAtlas(IDirect3DDevice9* device, int size)
{
    if (g_atlas.texture && g_atlas.size == size)
        return true;
    ReleaseAtlas();
    D3DFORMAT depthFormat = D3DFMT_D24S8;
    IDirect3DSurface9* gameDepth = nullptr;
    if (SUCCEEDED(device->GetDepthStencilSurface(&gameDepth)) && gameDepth) {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(gameDepth->GetDesc(&desc)))
            depthFormat = desc.Format;
        gameDepth->Release();
    }
    HRESULT made = device->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT,
                                         &g_atlas.texture, nullptr);
    if (SUCCEEDED(made))
        made = device->CreateDepthStencilSurface(size, size, depthFormat, D3DMULTISAMPLE_NONE, 0, TRUE, &g_atlas.depth,
                                                 nullptr);
    if (FAILED(made)) {
        if (!g_atlas.failedLogged)
            Log("level editor UI: no thumbnail atlas of %d pixels (%08X)", size, static_cast<unsigned>(made));
        g_atlas.failedLogged = true;
        ReleaseAtlas();
        return false;
    }
    g_atlas.size = size;
    g_atlas.stencil = depthFormat == D3DFMT_D24S8 || depthFormat == D3DFMT_D24X4S4 || depthFormat == D3DFMT_D15S1 ||
                      depthFormat == D3DFMT_D24FS8;
    // (a new atlas: every cell cleared once, so an unfilled one isn't garbage)
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(g_atlas.texture->GetSurfaceLevel(0, &surface))) {
        device->ColorFill(surface, nullptr, D3DCOLOR_XRGB(42, 44, 48));
        surface->Release();
    }
    return true;
}

} // namespace

bool BeginAtlasCell(int atlasSize, int x, int y, int size, uint32_t argb)
{
    IDirect3DDevice9* device = Device();
    if (!device || g_atlas.active || g_offscreen.active || atlasSize <= 0 || size <= 0 || x < 0 || y < 0 ||
        x + size > atlasSize || y + size > atlasSize || !MakeAtlas(device, atlasSize))
        return false;
    IDirect3DSurface9* surface = nullptr;
    if (FAILED(g_atlas.texture->GetSurfaceLevel(0, &surface)))
        return false;
    Atlas& a = g_atlas;
    device->GetRenderTarget(0, &a.savedTarget);
    device->GetDepthStencilSurface(&a.savedDepth);
    device->GetViewport(&a.savedViewport);
    for (int i = 0; i < 3; ++i)
        device->GetTransform(kSavedTransforms[i], &a.savedTransforms[i]);
    device->SetRenderTarget(0, surface);
    surface->Release();
    device->SetDepthStencilSurface(a.depth);
    D3DVIEWPORT9 viewport = {static_cast<DWORD>(x), static_cast<DWORD>(y), static_cast<DWORD>(size),
                             static_cast<DWORD>(size), 0.0f, 1.0f};
    device->SetViewport(&viewport);
    // (no rectangles: the viewport - the cell - is cleared)
    device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | (a.stencil ? D3DCLEAR_STENCIL : 0), argb, 1.0f, 0);
    a.active = true;
    return true;
}

void EndAtlasCell()
{
    Atlas& a = g_atlas;
    IDirect3DDevice9* device = Device();
    if (!a.active || !device)
        return;
    device->SetRenderTarget(0, a.savedTarget);
    device->SetDepthStencilSurface(a.savedDepth);
    device->SetViewport(&a.savedViewport);
    for (int i = 0; i < 3; ++i)
        device->SetTransform(kSavedTransforms[i], &a.savedTransforms[i]);
    if (a.savedTarget)
        a.savedTarget->Release();
    if (a.savedDepth)
        a.savedDepth->Release();
    a.savedTarget = a.savedDepth = nullptr;
    a.active = false;
}

uint64_t AtlasTexture() { return g_atlas.texture ? TextureId(g_atlas.texture) : 0; }

uint32_t AtlasGeneration() { return g_atlas.generation; }

void FreeAtlas()
{
    EndAtlasCell();
    ReleaseAtlas();
}

} // namespace Front::LevelEditor::UiBackend
