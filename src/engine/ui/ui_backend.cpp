// NOT ORIGINAL - our ImGui UI layers' backend (ours). See ui_backend.h: the editor's and the new front
// end's way to the installed render::UiBackend (engine/gfx/ui_backend.h), which holds ImGui's platform and renderer
// backends; here only what is the UI's own - whether a context is started, the log, the draw data scaled to the back
// buffer.

#include "engine/ui/ui_backend.h"

#include <cstdint>
#include <string>

#include <SDL3/SDL.h>

#include "base/core/packed_colour.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/ui_backend.h"
#include "engine/ui/ui_viewports.h"
#include "imgui.h"
#include "platform/run_log.h"
#include "platform/sdl_window.h"

namespace frontend::ui_backend {

namespace {

// Whether the backends are started for the current ImGui context: each context (the editor's, the new front end's -
// frontend/new_frontend.h) has its own backend data, which the backends keep in its IO.
bool context_ready()
{
    return (ImGui::GetCurrentContext() != nullptr) && ImGui::GetIO().BackendRendererUserData != nullptr;
}
bool& failed_logged()
{
    static bool failed_logged{};
    return failed_logged;
}

} // namespace

bool prepare_all()
{
    if (context_ready()) {
        return true;
    }
    SDL_Window* window = sys::sdl_window();
    const render::UiStart started = window ? render::ui_backend().start(window) : render::UiStart::kNoDevice;
    if (!window || started == render::UiStart::kNoDevice) {
        if (!failed_logged()) {
            get_log("level editor UI: no %s yet", window ? "device" : "window");
        }
        failed_logged() = true;
        return false;
    }
    if (started == render::UiStart::kWindowRefused) {
        get_log("level editor UI: ImGui's SDL3 backend refused the window");
        return false;
    }
    if (started == render::UiStart::kDeviceRefused) {
        get_log("level editor UI: ImGui's renderer backend (%s) refused the device", render::ui_backend().describe());
        return false;
    }
    get_log("level editor UI: Dear ImGui %s on %s", ImGui::GetVersion(), render::ui_backend().describe());
    return true;
}

bool get_ready() { return context_ready(); }

bool process_event(const SDL_Event* event)
{
    if (!context_ready()) {
        return false;
    }
    SDL_Event copy{}; // (with platform windows: the mouse in the game window's space - ui_viewports.h)
    return render::ui_backend().process_event(viewports::for_platform(event, &copy));
}

void new_frame()
{
    render::ui_backend().new_frame();
    viewports::after_frame_start();
}

bool enable_viewports() { return context_ready() && viewports::enable(); }

namespace {

// The main viewport's draw data drawn, scaled from the window's pixels to the back buffer's.
void draw_main(ImDrawData* data)
{
    if (!data || !data->Valid) {
        return;
    }
    // Window pixels -> back buffer pixels (the same unless the window shows a back buffer of another size).
    int back_width = 0;
    int back_height = 0;
    back_buffer_size({.width = &back_width, .height = &back_height});
    if (back_width <= 0 || back_height <= 0 || data->DisplaySize.x <= 0.0F || data->DisplaySize.y <= 0.0F) {
        return;
    }
    const float sx = static_cast<float>(back_width) / data->DisplaySize.x;
    const float sy = static_cast<float>(back_height) / data->DisplaySize.y;
    if (sx != 1.0F || sy != 1.0F) {
        for (ImDrawList* list : data->CmdLists) {
            for (ImDrawVert& vertex : list->VtxBuffer) {
                vertex.pos.x *= sx;
                vertex.pos.y *= sy;
            }
            for (ImDrawCmd& command : list->CmdBuffer) {
                command.ClipRect = ImVec4(command.ClipRect.x * sx, command.ClipRect.y * sy, command.ClipRect.z * sx,
                                          command.ClipRect.w * sy);
            }
        }
        data->DisplaySize = ImVec2(static_cast<float>(back_width), static_cast<float>(back_height));
    }
    render::ui_backend().render_draw_data(data);
}

} // namespace

void render()
{
    if (!context_ready()) {
        return;
    }
    draw_main(ImGui::GetDrawData());
    viewports::render_windows(); // (the panels dragged out of the window, each in its own - ui_viewports.h)
}

void invalidate_device_objects()
{
    if (context_ready()) {
        render::ui_backend().invalidate_device_objects();
    }
    // (the picture is in the default pool: it must go before a reset; made again when next drawn)
    free_offscreen();
    free_atlas(); // (the thumbnails' atlas too: its cells drawn again - level_editor_thumbs.h)
}

void invalidate_context_objects()
{
    if (context_ready()) {
        render::ui_backend().invalidate_device_objects();
    }
}

uint64_t create_texture(int width, int height, PixelLayout format, const void* data, RowLayout row_layout)
{
    return render::ui_backend().create_texture(width, height, format, data, row_layout);
}

void release_texture(uint64_t id) { render::ui_backend().release_texture(id); }

void window_size(int* width, int* height)
{
    *width = *height = 0;
    if (SDL_Window* window = sys::sdl_window()) {
        SDL_GetWindowSize(window, width, height);
    }
}

std::string window_title()
{
    SDL_Window* window = sys::sdl_window();
    const char* title = window ? SDL_GetWindowTitle(window) : nullptr;
    return title ? title : "";
}

void set_window_title(const std::string& title)
{
    if (SDL_Window* window = sys::sdl_window()) {
        SDL_SetWindowTitle(window, title.c_str());
    }
}

void back_buffer_size(SizeOut size)
{
    auto* const width = size.width;
    auto* const height = size.height;
    *width = render::renderer().back_buffer_width();
    *height = render::renderer().back_buffer_height();
}

bool begin_offscreen(int size, colour::Packed argb_colour) { return render::ui_backend().begin_offscreen(size, argb_colour); }

void end_offscreen() { render::ui_backend().end_offscreen(); }

uint64_t offscreen_texture() { return render::ui_backend().offscreen_texture(); }

void free_offscreen() { render::ui_backend().free_offscreen(); }

bool begin_atlas_cell(int atlas_size, int x, int y, int size, colour::Packed argb_colour)
{
    return render::ui_backend().begin_atlas_cell(atlas_size, x, y, size, argb_colour);
}

void end_atlas_cell() { render::ui_backend().end_atlas_cell(); }

uint64_t atlas_texture() { return render::ui_backend().atlas_texture(); }

uint32_t atlas_generation() { return render::ui_backend().atlas_generation(); }

void free_atlas() { render::ui_backend().free_atlas(); }

} // namespace frontend::ui_backend
