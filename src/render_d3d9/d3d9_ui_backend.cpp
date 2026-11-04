// NOT ORIGINAL - the Direct3D 9 UI backend (d3d9_ui_backend.h; the interface: engine/gfx/ui_backend.h). Until stage 11
// round 16 this was the level editor's own backend file (editor/level_editor_ui_backend.cpp), which now asks for it
// through render::ui_backend().

#include "render_d3d9/d3d9_ui_backend.h"

#include <d3d9.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_sdl2.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/core/packed_colour.h"
#include "base/util/com_ptr.h"
#include "engine/gfx/ui_backend.h"
#include "imgui.h"
#include "platform/run_log.h"
#include "render_d3d9/d3d9_state.h"
#include "render_d3d9/imgui_texture_id.h"

namespace render::d3d9 {

namespace {

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
Offscreen& offscreen()
{
    static core::NoDestroy<Offscreen> offscreen;
    return offscreen.get();
}
constexpr std::array<D3DTRANSFORMSTATETYPE, 3> kSavedTransforms = {{D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION}};

uint64_t get_texture_id(IDirect3DTexture9* texture)
{
    // (the Direct3D 9 backend takes an IDirect3DTexture9*)
    return os_layer::imgui_texture_id(texture);
}

void release_offscreen()
{
    if (offscreen().texture) {
        offscreen().texture->Release();
    }
    if (offscreen().depth) {
        offscreen().depth->Release();
    }
    offscreen().texture = nullptr;
    offscreen().depth = nullptr;
    offscreen().size = 0;
}

bool make_offscreen(IDirect3DDevice9* device, int size)
{
    if (offscreen().texture && offscreen().size == size) {
        return true;
    }
    release_offscreen();
    D3DFORMAT depth_format = D3DFMT_D24S8;
    IDirect3DSurface9* game_depth = nullptr;
    if (SUCCEEDED(device->GetDepthStencilSurface(&game_depth)) && game_depth) {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(game_depth->GetDesc(&desc))) {
            depth_format = desc.Format;
        }
        game_depth->Release();
    }
    HRESULT made = device->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT,
                                         &offscreen().texture, nullptr);
    if (SUCCEEDED(made)) {
        made = device->CreateDepthStencilSurface(size, size, depth_format, D3DMULTISAMPLE_NONE, 0, TRUE,
                                                 &offscreen().depth, nullptr);
    }
    if (FAILED(made)) {
        if (!offscreen().failed_logged_) {
            get_log("level editor UI: no off-screen target of %d pixels (%08X)", size, static_cast<unsigned>(made));
        }
        offscreen().failed_logged_ = true;
        release_offscreen();
        return false;
    }
    offscreen().size = size;
    offscreen().stencil = depth_format == D3DFMT_D24S8 || depth_format == D3DFMT_D24X4S4 ||
                          depth_format == D3DFMT_D15S1 || depth_format == D3DFMT_D24FS8;
    return true;
}

// --- the thumbnails' atlas ----------------------------------------------------------------------------------------

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
Atlas& stored_atlas()
{
    static core::NoDestroy<Atlas> atlas;
    return atlas.get();
}

void release_atlas()
{
    if (stored_atlas().texture || stored_atlas().depth) {
        ++stored_atlas().generation;
    }
    if (stored_atlas().texture) {
        stored_atlas().texture->Release();
    }
    if (stored_atlas().depth) {
        stored_atlas().depth->Release();
    }
    stored_atlas().texture = nullptr;
    stored_atlas().depth = nullptr;
    stored_atlas().size = 0;
}

bool make_atlas(IDirect3DDevice9* device, int size)
{
    if (stored_atlas().texture && stored_atlas().size == size) {
        return true;
    }
    release_atlas();
    D3DFORMAT depth_format = D3DFMT_D24S8;
    IDirect3DSurface9* game_depth = nullptr;
    if (SUCCEEDED(device->GetDepthStencilSurface(&game_depth)) && game_depth) {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(game_depth->GetDesc(&desc))) {
            depth_format = desc.Format;
        }
        game_depth->Release();
    }
    HRESULT made = device->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT,
                                         &stored_atlas().texture, nullptr);
    if (SUCCEEDED(made)) {
        made = device->CreateDepthStencilSurface(size, size, depth_format, D3DMULTISAMPLE_NONE, 0, TRUE,
                                                 &stored_atlas().depth, nullptr);
    }
    if (FAILED(made)) {
        if (!stored_atlas().failed_logged_) {
            get_log("level editor UI: no thumbnail atlas of %d pixels (%08X)", size, static_cast<unsigned>(made));
        }
        stored_atlas().failed_logged_ = true;
        release_atlas();
        return false;
    }
    stored_atlas().size = size;
    stored_atlas().stencil = depth_format == D3DFMT_D24S8 || depth_format == D3DFMT_D24X4S4 ||
                             depth_format == D3DFMT_D15S1 || depth_format == D3DFMT_D24FS8;
    // (a new atlas: every cell cleared once, so an unfilled one isn't garbage)
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(stored_atlas().texture->GetSurfaceLevel(0, &surface))) {
        device->ColorFill(surface, nullptr, D3DCOLOR_XRGB(42, 44, 48));
        surface->Release();
    }
    return true;
}

class D3d9UiBackend final : public UiBackend {
public:
    UiStart start(SDL_Window* window) override
    {
        IDirect3DDevice9* device = state().d3d_device();
        if (!device) {
            return UiStart::kNoDevice;
        }
        if (!ImGui_ImplSDL2_InitForD3D(window)) {
            return UiStart::kWindowRefused;
        }
        // (no controllers: the game has them; the UI is mouse and keyboard)
        ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);
        if (!ImGui_ImplDX9_Init(device)) {
            ImGui_ImplSDL2_Shutdown();
            return UiStart::kDeviceRefused;
        }
        return UiStart::kStarted;
    }

    [[nodiscard]] const char* describe() const override { return "SDL2 and Direct3D 9"; }

    bool process_event(const SDL_Event* event) override { return ImGui_ImplSDL2_ProcessEvent(event); }

    void new_frame() override
    {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplSDL2_NewFrame();
    }

    // (the backend saves the device's whole state in a state block and puts it back after: the renderer's cached
    // state stays true)
    void render_draw_data(ImDrawData* data) override { ImGui_ImplDX9_RenderDrawData(data); }

    void invalidate_device_objects() override { ImGui_ImplDX9_InvalidateDeviceObjects(); }

    UiTextureId create_texture(int width, int height, UiPixelLayout format, const void* data,
                               UiRowLayout row_layout) override
    {
        const auto row_bytes = row_layout.row_bytes_;
        const auto rows = row_layout.rows;
        IDirect3DDevice9* device = state().d3d_device();
        if (!device || width <= 0 || height <= 0 || !data) {
            return 0;
        }
        util::ComPtr<IDirect3DTexture9> texture;
        // (managed: Direct3D keeps a copy, so a device reset doesn't lose it)
        const D3DFORMAT d3d_format = format == UiPixelLayout::kDxt1 ? D3DFMT_DXT1 : D3DFMT_A8R8G8B8;
        if (FAILED(device->CreateTexture(width, height, 1, 0, d3d_format, D3DPOOL_MANAGED, texture.put(), nullptr)) ||
            !texture) {
            return 0;
        }
        D3DLOCKED_RECT locked;
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
            return 0;
        }
        const auto rows_in_bytes = static_cast<size_t>(rows);
        const auto source_pitch = static_cast<size_t>(row_bytes);
        const auto target_pitch = static_cast<size_t>(locked.Pitch);
        const std::span<const uint8_t> from(static_cast<const uint8_t*>(data), rows_in_bytes * source_pitch);
        const std::span<uint8_t> to(static_cast<uint8_t*>(locked.pBits), rows_in_bytes * target_pitch);
        for (size_t row = 0; row < rows_in_bytes; ++row) {
            std::memcpy(to.subspan(row * target_pitch).data(), from.subspan(row * source_pitch).data(), source_pitch);
        }
        texture->UnlockRect(0);
        return get_texture_id(texture.detach()); // (released by release_texture)
    }

    void release_texture(UiTextureId id) override
    {
        if (id != 0U) {
            os_layer::imgui_texture_of(id)->Release();
        }
    }

    bool begin_offscreen(int size, colour::Packed argb_colour) override
    {
        const uint32_t argb = argb_colour.value();
        IDirect3DDevice9* device = state().d3d_device();
        if (!device || offscreen().active || size <= 0 || !make_offscreen(device, size)) {
            return false;
        }
        IDirect3DSurface9* surface = nullptr;
        if (FAILED(offscreen().texture->GetSurfaceLevel(0, &surface))) {
            return false;
        }
        Offscreen& offscreen_state = offscreen();
        device->GetRenderTarget(0, &offscreen_state.saved_target_);
        device->GetDepthStencilSurface(&offscreen_state.saved_depth_);
        device->GetViewport(&offscreen_state.saved_viewport_);
        for (int i = 0; i < 3; ++i) {
            device->GetTransform(core::at(kSavedTransforms, i), &core::at(offscreen_state.saved_transforms_, i));
        }
        device->SetRenderTarget(0, surface);
        surface->Release();
        device->SetDepthStencilSurface(offscreen_state.depth);
        const D3DVIEWPORT9 viewport = {.X = 0,
                                       .Y = 0,
                                       .Width = static_cast<DWORD>(size),
                                       .Height = static_cast<DWORD>(size),
                                       .MinZ = 0.0F,
                                       .MaxZ = 1.0F};
        device->SetViewport(&viewport);
        device->Clear(0, nullptr,
                      D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | (offscreen_state.stencil ? D3DCLEAR_STENCIL : 0), argb,
                      1.0F, 0);
        offscreen_state.active = true;
        return true;
    }

    void end_offscreen() override
    {
        Offscreen& offscreen_state = offscreen();
        IDirect3DDevice9* device = state().d3d_device();
        if (!offscreen_state.active || !device) {
            return;
        }
        device->SetRenderTarget(0, offscreen_state.saved_target_);
        device->SetDepthStencilSurface(offscreen_state.saved_depth_);
        device->SetViewport(&offscreen_state.saved_viewport_); // (after the target: setting one resets the viewport)
        for (int i = 0; i < 3; ++i) {
            device->SetTransform(core::at(kSavedTransforms, i), &core::at(offscreen_state.saved_transforms_, i));
        }
        if (offscreen_state.saved_target_) {
            offscreen_state.saved_target_->Release();
        }
        if (offscreen_state.saved_depth_) {
            offscreen_state.saved_depth_->Release();
        }
        offscreen_state.saved_target_ = offscreen_state.saved_depth_ = nullptr;
        offscreen_state.active = false;
    }

    [[nodiscard]] UiTextureId offscreen_texture() override
    {
        return offscreen().texture ? get_texture_id(offscreen().texture) : 0;
    }

    void free_offscreen() override
    {
        end_offscreen();
        release_offscreen();
    }

    bool begin_atlas_cell(int atlas_size, int x, int y, int size, colour::Packed argb_colour) override
    {
        const uint32_t argb = argb_colour.value();
        IDirect3DDevice9* device = state().d3d_device();
        if (!device || stored_atlas().active || offscreen().active || atlas_size <= 0 || size <= 0 || x < 0 || y < 0 ||
            x + size > atlas_size || y + size > atlas_size || !make_atlas(device, atlas_size)) {
            return false;
        }
        IDirect3DSurface9* surface = nullptr;
        if (FAILED(stored_atlas().texture->GetSurfaceLevel(0, &surface))) {
            return false;
        }
        Atlas& atlas = stored_atlas();
        device->GetRenderTarget(0, &atlas.saved_target_);
        device->GetDepthStencilSurface(&atlas.saved_depth_);
        device->GetViewport(&atlas.saved_viewport_);
        for (int i = 0; i < 3; ++i) {
            device->GetTransform(core::at(kSavedTransforms, i), &core::at(atlas.saved_transforms_, i));
        }
        device->SetRenderTarget(0, surface);
        surface->Release();
        device->SetDepthStencilSurface(atlas.depth);
        const D3DVIEWPORT9 viewport = {.X = static_cast<DWORD>(x),
                                       .Y = static_cast<DWORD>(y),
                                       .Width = static_cast<DWORD>(size),
                                       .Height = static_cast<DWORD>(size),
                                       .MinZ = 0.0F,
                                       .MaxZ = 1.0F};
        device->SetViewport(&viewport);
        // (no rectangles: the viewport - the cell - is cleared)
        device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | (atlas.stencil ? D3DCLEAR_STENCIL : 0), argb,
                      1.0F, 0);
        atlas.active = true;
        return true;
    }

    void end_atlas_cell() override
    {
        Atlas& atlas = stored_atlas();
        IDirect3DDevice9* device = state().d3d_device();
        if (!atlas.active || !device) {
            return;
        }
        device->SetRenderTarget(0, atlas.saved_target_);
        device->SetDepthStencilSurface(atlas.saved_depth_);
        device->SetViewport(&atlas.saved_viewport_);
        for (int i = 0; i < 3; ++i) {
            device->SetTransform(core::at(kSavedTransforms, i), &core::at(atlas.saved_transforms_, i));
        }
        if (atlas.saved_target_) {
            atlas.saved_target_->Release();
        }
        if (atlas.saved_depth_) {
            atlas.saved_depth_->Release();
        }
        atlas.saved_target_ = atlas.saved_depth_ = nullptr;
        atlas.active = false;
    }

    [[nodiscard]] UiTextureId atlas_texture() override
    {
        return stored_atlas().texture ? get_texture_id(stored_atlas().texture) : 0;
    }

    [[nodiscard]] uint32_t atlas_generation() override { return stored_atlas().generation; }

    void free_atlas() override
    {
        end_atlas_cell();
        release_atlas();
    }
};

} // namespace

UiBackend& ui_backend()
{
    static core::NoDestroy<D3d9UiBackend> made;
    return made.get();
}

} // namespace render::d3d9
