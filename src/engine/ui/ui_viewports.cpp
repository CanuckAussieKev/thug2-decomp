// NOT ORIGINAL - Dear ImGui's platform windows for our UI layers (ui_viewports.h).

#include "engine/ui/ui_viewports.h"

#include <SDL3/SDL.h>

#include "imgui.h"
#include "platform/run_log.h"
#include "platform/sdl_window.h"

namespace frontend::ui_backend::viewports {

namespace {

// The platform backend's own calls that take or give a window's place (wrapped below; the same for every context).
struct PlatformCalls {
    ImVec2 (*get_position)(ImGuiViewport*) = nullptr;
    void (*set_position)(ImGuiViewport*, ImVec2) = nullptr;
    void (*create)(ImGuiViewport*) = nullptr;
};
PlatformCalls& platform_calls()
{
    static PlatformCalls calls;
    return calls;
}

// The game window's place on the desktop when last seen (a frame's start).
ImVec2& last_origin()
{
    static ImVec2 origin;
    return origin;
}

// The game window's top-left corner on the desktop: where ImGui's space starts.
ImVec2 origin()
{
    int x = 0;
    int y = 0;
    if (SDL_Window* window = sys::sdl_window()) {
        SDL_GetWindowPosition(window, &x, &y);
    }
    return {static_cast<float>(x), static_cast<float>(y)};
}

ImVec2 get_position(ImGuiViewport* viewport)
{
    if (platform_calls().get_position == nullptr) {
        return {};
    }
    const ImVec2 on_desktop = platform_calls().get_position(viewport);
    const ImVec2 from = origin();
    return {on_desktop.x - from.x, on_desktop.y - from.y};
}

void set_position(ImGuiViewport* viewport, ImVec2 position)
{
    if (platform_calls().set_position == nullptr) {
        return;
    }
    const ImVec2 from = origin();
    platform_calls().set_position(viewport, ImVec2(position.x + from.x, position.y + from.y));
}

void create(ImGuiViewport* viewport)
{
    if (platform_calls().create == nullptr) {
        return;
    }
    // (the backend places the new window at the viewport's position)
    const ImVec2 kept = viewport->Pos;
    const ImVec2 from = origin();
    viewport->Pos = ImVec2(kept.x + from.x, kept.y + from.y);
    platform_calls().create(viewport);
    viewport->Pos = kept;
}

} // namespace

bool on()
{
    return ImGui::GetCurrentContext() != nullptr && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
}

bool enable()
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (on()) {
        return true;
    }
    constexpr ImGuiBackendFlags kBoth = ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    if ((io.BackendFlags & kBoth) != kBoth || platform.Platform_GetWindowPos == nullptr ||
        platform.Platform_SetWindowPos == nullptr || platform.Platform_CreateWindow == nullptr) {
        get_log("level editor UI: panels stay in the game's window (the renderer draws no windows of their own)");
        return false;
    }
    if (platform.Platform_GetWindowPos != get_position) {
        platform_calls() = {.get_position = platform.Platform_GetWindowPos,
                            .set_position = platform.Platform_SetWindowPos,
                            .create = platform.Platform_CreateWindow};
        platform.Platform_GetWindowPos = get_position;
        platform.Platform_SetWindowPos = set_position;
        platform.Platform_CreateWindow = create;
    }
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoTaskBarIcon = true; // (a panel's window is the editor's, not an application of its own)
    last_origin() = origin();
    get_log("level editor UI: panels may be dragged out of the window");
    return true;
}

const SDL_Event* for_platform(const SDL_Event* event, SDL_Event* copy)
{
    if (!on() || event == nullptr || event->type != SDL_EVENT_MOUSE_MOTION) {
        return event;
    }
    // (the backend adds the window's place on the desktop: the game window's taken off first)
    const ImVec2 from = origin();
    *copy = *event;
    copy->motion.x -= from.x;
    copy->motion.y -= from.y;
    return copy;
}

void after_frame_start()
{
    if (!on()) {
        return;
    }
    const ImVec2 from = origin();
    // the mouse while one of the game's windows has the keyboard: the backend gives the desktop's position (the
    // platform-windows way), here the game window's - queued after its own, so this one counts
    SDL_Window* window = sys::sdl_window();
    if (SDL_GetKeyboardFocus() != nullptr && window != nullptr && !SDL_GetWindowRelativeMouseMode(window)) {
        float x = 0.0F;
        float y = 0.0F;
        SDL_GetGlobalMouseState(&x, &y);
        ImGui::GetIO().AddMousePosEvent(x - from.x, y - from.y);
    }
    // the monitors (the backend lists them again at every frame's start)
    for (ImGuiPlatformMonitor& monitor : ImGui::GetPlatformIO().Monitors) {
        monitor.MainPos = ImVec2(monitor.MainPos.x - from.x, monitor.MainPos.y - from.y);
        monitor.WorkPos = ImVec2(monitor.WorkPos.x - from.x, monitor.WorkPos.y - from.y);
    }
    // the game's window moved: the other windows stayed where they were on the desktop, so their places in its space
    // changed - read again
    if (from.x != last_origin().x || from.y != last_origin().y) {
        last_origin() = from;
        for (ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports) {
            if (viewport != ImGui::GetMainViewport() && viewport->PlatformWindowCreated) {
                viewport->PlatformRequestMove = true;
            }
        }
    }
}

void render_windows()
{
    if (!on()) {
        return;
    }
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
}

} // namespace frontend::ui_backend::viewports
