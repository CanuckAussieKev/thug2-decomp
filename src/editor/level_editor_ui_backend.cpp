// NOT ORIGINAL - the level editor's UI backend (ours). See level_editor_ui_backend.h: the only file that includes
// Direct3D or an ImGui backend header.

#include "editor/level_editor_ui_backend.h"

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <SDL2/SDL.h>
#include <array>

#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_sdl2.h"
#include "platform/sdl_window.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/gfx/device.h"
#include "engine/gfx/draw_frame.h"
#include "engine/util/com_ptr.h"
#include "platform/platform_d3d.h"
#include "engine/core/checked_index.h"

namespace frontend::level_editor::ui_backend {

namespace {

IDirect3DDevice9* input_port() { return render::g_d3d_device; }

// Whether the backends are started for the current ImGui context: each context (the editor's, the new front end's -
// frontend/new_frontend.h) has its own backend data, which the backends keep in its IO.
bool context_ready() { return ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData != nullptr; }
bool g_failed_logged;

// The off-screen picture: its colour target (X8R8G8B8: what the game's blending leaves in alpha doesn't show through
// ImGui's), its depth surface (the format of the game's own; no multisampling: the target has none), and what
// begin_offscreen switched from.
struct Offscreen {
    IDirect3DTexture9* texture = nullptr;
    IDirect3DSurface9* depth = nullptr;
    int size = 0;
    bool stencil = false; // (the depth format has one: cleared with it)
    bool failed_logged_ = false;
    // (between Begin and End)
    bool active = false;
    IDirect3DSurface9* saved_target_ = nullptr;
    IDirect3DSurface9* saved_depth_ = nullptr;
    D3DVIEWPORT9 saved_viewport_ = {};
    std::array<D3DMATRIX, 3> saved_transforms_ = {};
};
Offscreen g_offscreen;
constexpr std::array<D3DTRANSFORMSTATETYPE, 3> kSavedTransforms = {{D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION}};

void release_offscreen()
{
    if (g_offscreen.texture)
        g_offscreen.texture->Release();
    if (g_offscreen.depth)
        g_offscreen.depth->Release();
    g_offscreen.texture = nullptr;
    g_offscreen.depth = nullptr;
    g_offscreen.size = 0;
}

bool make_offscreen(IDirect3DDevice9* device, int size)
{
    if (g_offscreen.texture && g_offscreen.size == size)
        return true;
    release_offscreen();
    D3DFORMAT depth_format = D3DFMT_D24S8;
    IDirect3DSurface9* game_depth = nullptr;
    if (SUCCEEDED(device->GetDepthStencilSurface(&game_depth)) && game_depth) {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(game_depth->GetDesc(&desc)))
            depth_format = desc.Format;
        game_depth->Release();
    }
    HRESULT made = device->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT,
                                         &g_offscreen.texture, nullptr);
    if (SUCCEEDED(made))
        made = device->CreateDepthStencilSurface(size, size, depth_format, D3DMULTISAMPLE_NONE, 0, TRUE,
                                                 &g_offscreen.depth, nullptr);
    if (FAILED(made)) {
        if (!g_offscreen.failed_logged_)
            get_log("level editor UI: no off-screen target of %d pixels (%08X)", size, static_cast<unsigned>(made));
        g_offscreen.failed_logged_ = true;
        release_offscreen();
        return false;
    }
    g_offscreen.size = size;
    g_offscreen.stencil = depth_format == D3DFMT_D24S8 || depth_format == D3DFMT_D24X4S4 ||
                          depth_format == D3DFMT_D15S1 || depth_format == D3DFMT_D24FS8;
    return true;
}

} // namespace

bool prepare_all()
{
    if (context_ready())
        return true;
    SDL_Window* window = sys::sdl_window();
    IDirect3DDevice9* device = input_port();
    if (!window || !device) {
        if (!g_failed_logged)
            get_log("level editor UI: no %s yet", window ? "device" : "window");
        g_failed_logged = true;
        return false;
    }
    if (!ImGui_ImplSDL2_InitForD3D(window)) {
        get_log("level editor UI: ImGui's SDL2 backend refused the window");
        return false;
    }
    // (no controllers: the game has them; the UI is mouse and keyboard)
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);
    if (!ImGui_ImplDX9_Init(device)) {
        ImGui_ImplSDL2_Shutdown();
        get_log("level editor UI: ImGui's Direct3D 9 backend refused the device");
        return false;
    }
    get_log("level editor UI: Dear ImGui %s on SDL2 and Direct3D 9", ImGui::GetVersion());
    return true;
}

bool get_ready() { return context_ready(); }

bool process_event(const SDL_Event* event) { return context_ready() && ImGui_ImplSDL2_ProcessEvent(event); }

void new_frame()
{
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplSDL2_NewFrame();
}

void render()
{
    ImDrawData* data = ImGui::GetDrawData();
    if (!context_ready() || !data || !data->Valid)
        return;
    // Window pixels -> back buffer pixels (the same unless the window shows a back buffer of another size).
    int back_width = 0, back_height = 0;
    back_buffer_size(&back_width, &back_height);
    if (back_width <= 0 || back_height <= 0 || data->DisplaySize.x <= 0.0f || data->DisplaySize.y <= 0.0f)
        return;
    const float sx = static_cast<float>(back_width) / data->DisplaySize.x;
    const float sy = static_cast<float>(back_height) / data->DisplaySize.y;
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
        data->DisplaySize = ImVec2(static_cast<float>(back_width), static_cast<float>(back_height));
    }
    // (the backend saves the device's whole state in a state block and puts it back after: the renderer's cached
    // state stays true)
    ImGui_ImplDX9_RenderDrawData(data);
}

void invalidate_device_objects()
{
    if (context_ready())
        ImGui_ImplDX9_InvalidateDeviceObjects();
    // (the picture is in the default pool: it must go before a reset; made again when next drawn)
    free_offscreen();
    free_atlas(); // (the thumbnails' atlas too: its cells drawn again - level_editor_thumbs.h)
}

void invalidate_context_objects()
{
    if (context_ready())
        ImGui_ImplDX9_InvalidateDeviceObjects();
}

uint64_t create_texture(int width, int height, PixelLayout format, const void* data, int row_bytes, int rows)
{
    IDirect3DDevice9* device = input_port();
    if (!device || width <= 0 || height <= 0 || !data)
        return 0;
    util::ComPtr<IDirect3DTexture9> texture;
    // (managed: Direct3D keeps a copy, so a device reset doesn't lose it)
    const D3DFORMAT d3d_format = format == PixelLayout::kDxt1 ? D3DFMT_DXT1 : D3DFMT_A8R8G8B8;
    if (FAILED(device->CreateTexture(width, height, 1, 0, d3d_format, D3DPOOL_MANAGED, texture.put(), nullptr)) ||
        !texture)
        return 0;
    D3DLOCKED_RECT locked;
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        return 0;
    const auto rows_in_bytes = static_cast<size_t>(rows);
    const auto source_pitch = static_cast<size_t>(row_bytes);
    const auto target_pitch = static_cast<size_t>(locked.Pitch);
    const std::span<const uint8_t> from(static_cast<const uint8_t*>(data), rows_in_bytes * source_pitch);
    const std::span<uint8_t> to(static_cast<uint8_t*>(locked.pBits), rows_in_bytes * target_pitch);
    for (size_t row = 0; row < rows_in_bytes; ++row)
        std::memcpy(to.subspan(row * target_pitch).data(), from.subspan(row * source_pitch).data(), source_pitch);
    texture->UnlockRect(0);
    return get_texture_id(texture.detach()); // (released by release_texture)
}

void release_texture(uint64_t id)
{
    if (id)
        os_layer::imgui_texture_of(id)->Release();
}

void window_size(int* width, int* height)
{
    *width = *height = 0;
    if (SDL_Window* window = sys::sdl_window())
        SDL_GetWindowSize(window, width, height);
}

void back_buffer_size(int* width, int* height)
{
    *width = render::g_back_buffer_width;
    *height = render::g_back_buffer_height;
}

uint64_t get_texture_id(void* renderer_texture)
{
    // (the Direct3D 9 backend takes an IDirect3DTexture9*)
    return os_layer::imgui_texture_id(static_cast<IDirect3DTexture9*>(renderer_texture));
}

bool begin_offscreen(int size, uint32_t argb)
{
    IDirect3DDevice9* device = input_port();
    if (!device || g_offscreen.active || size <= 0 || !make_offscreen(device, size))
        return false;
    IDirect3DSurface9* surface = nullptr;
    if (FAILED(g_offscreen.texture->GetSurfaceLevel(0, &surface)))
        return false;
    Offscreen& o = g_offscreen;
    device->GetRenderTarget(0, &o.saved_target_);
    device->GetDepthStencilSurface(&o.saved_depth_);
    device->GetViewport(&o.saved_viewport_);
    for (int i = 0; i < 3; ++i)
        device->GetTransform(core::at(kSavedTransforms, i), &core::at(o.saved_transforms_, i));
    device->SetRenderTarget(0, surface);
    surface->Release();
    device->SetDepthStencilSurface(o.depth);
    const D3DVIEWPORT9 viewport = {0, 0, static_cast<DWORD>(size), static_cast<DWORD>(size), 0.0f, 1.0f};
    device->SetViewport(&viewport);
    device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | (o.stencil ? D3DCLEAR_STENCIL : 0), argb, 1.0f, 0);
    o.active = true;
    return true;
}

void end_offscreen()
{
    Offscreen& o = g_offscreen;
    IDirect3DDevice9* device = input_port();
    if (!o.active || !device)
        return;
    device->SetRenderTarget(0, o.saved_target_);
    device->SetDepthStencilSurface(o.saved_depth_);
    device->SetViewport(&o.saved_viewport_); // (after the target: setting one resets the viewport)
    for (int i = 0; i < 3; ++i)
        device->SetTransform(core::at(kSavedTransforms, i), &core::at(o.saved_transforms_, i));
    if (o.saved_target_)
        o.saved_target_->Release();
    if (o.saved_depth_)
        o.saved_depth_->Release();
    o.saved_target_ = o.saved_depth_ = nullptr;
    o.active = false;
}

uint64_t offscreen_texture() { return g_offscreen.texture ? get_texture_id(g_offscreen.texture) : 0; }

void free_offscreen()
{
    end_offscreen();
    release_offscreen();
}

// --- the thumbnails' atlas ----------------------------------------------------------------------------------------

namespace {

struct Atlas {
    IDirect3DTexture9* texture = nullptr;
    IDirect3DSurface9* depth = nullptr;
    int size = 0;
    bool stencil = false;
    bool failed_logged_ = false;
    uint32_t generation = 1;
    bool active = false;
    IDirect3DSurface9* saved_target_ = nullptr;
    IDirect3DSurface9* saved_depth_ = nullptr;
    D3DVIEWPORT9 saved_viewport_ = {};
    std::array<D3DMATRIX, 3> saved_transforms_ = {};
};
Atlas g_atlas;

void release_atlas()
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

bool make_atlas(IDirect3DDevice9* device, int size)
{
    if (g_atlas.texture && g_atlas.size == size)
        return true;
    release_atlas();
    D3DFORMAT depth_format = D3DFMT_D24S8;
    IDirect3DSurface9* game_depth = nullptr;
    if (SUCCEEDED(device->GetDepthStencilSurface(&game_depth)) && game_depth) {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(game_depth->GetDesc(&desc)))
            depth_format = desc.Format;
        game_depth->Release();
    }
    HRESULT made = device->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT,
                                         &g_atlas.texture, nullptr);
    if (SUCCEEDED(made))
        made = device->CreateDepthStencilSurface(size, size, depth_format, D3DMULTISAMPLE_NONE, 0, TRUE, &g_atlas.depth,
                                                 nullptr);
    if (FAILED(made)) {
        if (!g_atlas.failed_logged_)
            get_log("level editor UI: no thumbnail atlas of %d pixels (%08X)", size, static_cast<unsigned>(made));
        g_atlas.failed_logged_ = true;
        release_atlas();
        return false;
    }
    g_atlas.size = size;
    g_atlas.stencil = depth_format == D3DFMT_D24S8 || depth_format == D3DFMT_D24X4S4 || depth_format == D3DFMT_D15S1 ||
                      depth_format == D3DFMT_D24FS8;
    // (a new atlas: every cell cleared once, so an unfilled one isn't garbage)
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(g_atlas.texture->GetSurfaceLevel(0, &surface))) {
        device->ColorFill(surface, nullptr, D3DCOLOR_XRGB(42, 44, 48));
        surface->Release();
    }
    return true;
}

} // namespace

bool begin_atlas_cell(int atlas_size, int x, int y, int size, uint32_t argb)
{
    IDirect3DDevice9* device = input_port();
    if (!device || g_atlas.active || g_offscreen.active || atlas_size <= 0 || size <= 0 || x < 0 || y < 0 ||
        x + size > atlas_size || y + size > atlas_size || !make_atlas(device, atlas_size))
        return false;
    IDirect3DSurface9* surface = nullptr;
    if (FAILED(g_atlas.texture->GetSurfaceLevel(0, &surface)))
        return false;
    Atlas& a = g_atlas;
    device->GetRenderTarget(0, &a.saved_target_);
    device->GetDepthStencilSurface(&a.saved_depth_);
    device->GetViewport(&a.saved_viewport_);
    for (int i = 0; i < 3; ++i)
        device->GetTransform(core::at(kSavedTransforms, i), &core::at(a.saved_transforms_, i));
    device->SetRenderTarget(0, surface);
    surface->Release();
    device->SetDepthStencilSurface(a.depth);
    const D3DVIEWPORT9 viewport = {
        static_cast<DWORD>(x), static_cast<DWORD>(y), static_cast<DWORD>(size), static_cast<DWORD>(size), 0.0f, 1.0f};
    device->SetViewport(&viewport);
    // (no rectangles: the viewport - the cell - is cleared)
    device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | (a.stencil ? D3DCLEAR_STENCIL : 0), argb, 1.0f, 0);
    a.active = true;
    return true;
}

void end_atlas_cell()
{
    Atlas& a = g_atlas;
    IDirect3DDevice9* device = input_port();
    if (!a.active || !device)
        return;
    device->SetRenderTarget(0, a.saved_target_);
    device->SetDepthStencilSurface(a.saved_depth_);
    device->SetViewport(&a.saved_viewport_);
    for (int i = 0; i < 3; ++i)
        device->SetTransform(core::at(kSavedTransforms, i), &core::at(a.saved_transforms_, i));
    if (a.saved_target_)
        a.saved_target_->Release();
    if (a.saved_depth_)
        a.saved_depth_->Release();
    a.saved_target_ = a.saved_depth_ = nullptr;
    a.active = false;
}

uint64_t atlas_texture() { return g_atlas.texture ? get_texture_id(g_atlas.texture) : 0; }

uint32_t atlas_generation() { return g_atlas.generation; }

void free_atlas()
{
    end_atlas_cell();
    release_atlas();
}

} // namespace frontend::level_editor::ui_backend
