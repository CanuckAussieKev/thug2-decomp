// NOT ORIGINAL - the demos' tag on screen (demo_overlay.h).

#include "devtools/demo_overlay.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

#include "base/core/no_destroy.h"
#include "engine/ui/ui_backend.h"
#include "engine/ui/ui_fonts.h"
#include "platform/run_log.h"
#include "platform/sdl_window.h"

namespace devtools::demo_overlay {

namespace {

constexpr float kTagPixels = 20.0F;  // (at 720 lines)
constexpr float kLinePixels = 14.0F; // (at 720 lines)
constexpr float kBaseHeight = 720.0F;
constexpr float kMargin = 16.0F;
constexpr float kPadding = 8.0F;
constexpr float kRounding = 4.0F;
constexpr float kDotRadius = 0.28F; // (of the tag's height)
constexpr float kHalf = 0.5F;
constexpr ImU32 kBackground = IM_COL32(0, 0, 0, 150);
constexpr ImU32 kText = IM_COL32(255, 255, 255, 235);
constexpr ImU32 kDim = IM_COL32(220, 220, 220, 200);
constexpr ImU32 kRed = IM_COL32(230, 40, 40, 255);
constexpr ImU32 kDivider = IM_COL32(255, 255, 255, 220);
constexpr float kDividerWidth = 2.0F; // (at 720 lines)

struct State {
    ImGuiContext* context = nullptr;
    bool failed = false;
    ImFont* tag = nullptr;
    ImFont* line = nullptr;
    float scale = 1.0F;
};

State& state()
{
    static core::NoDestroy<State> value;
    return value.get();
}

bool begin_context(ImGuiContext** previous)
{
    State& overlay = state();
    *previous = ImGui::GetCurrentContext();
    if (overlay.context == nullptr) {
        if (overlay.failed) {
            return false;
        }
        overlay.context = ImGui::CreateContext();
        ImGui::SetCurrentContext(overlay.context);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoMouse;
        int height = 0;
        if (SDL_Window* window = sys::sdl_window()) {
            SDL_GetWindowSize(window, nullptr, &height);
        }
        overlay.scale = std::max(1.0F, static_cast<float>(height) / kBaseHeight);
        const frontend::ui_fonts::CompressedFont bold = frontend::ui_fonts::inter_semi_bold();
        const frontend::ui_fonts::CompressedFont regular = frontend::ui_fonts::inter();
        overlay.tag = io.Fonts->AddFontFromMemoryCompressedTTF(bold.data, bold.size, kTagPixels * overlay.scale);
        overlay.line = io.Fonts->AddFontFromMemoryCompressedTTF(regular.data, regular.size, kLinePixels * overlay.scale);
        io.Fonts->Build();
        if (!frontend::ui_backend::prepare_all()) {
            ImGui::DestroyContext(overlay.context);
            overlay.context = nullptr;
            overlay.failed = true;
            ImGui::SetCurrentContext(*previous);
            get_log("demo: ImGui couldn't start on the game's window / device - no tag on screen");
            return false;
        }
    }
    ImGui::SetCurrentContext(overlay.context);
    return true;
}

} // namespace

void draw(const std::string& tag, const std::string& line, bool dot)
{
    if (tag.empty()) {
        return;
    }
    State& overlay = state();
    ImGuiContext* previous = nullptr;
    if (!begin_context(&previous)) {
        return;
    }
    frontend::ui_backend::new_frame();
    ImGui::NewFrame();
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const float margin = kMargin * overlay.scale;
    const float padding = kPadding * overlay.scale;
    const ImVec2 tag_size = overlay.tag->CalcTextSizeA(overlay.tag->FontSize, FLT_MAX, 0.0F, tag.c_str());
    const ImVec2 line_size =
        line.empty() ? ImVec2(0.0F, 0.0F) : overlay.line->CalcTextSizeA(overlay.line->FontSize, FLT_MAX, 0.0F, line.c_str());
    const float dot_width = dot ? (tag_size.y * (kDotRadius * 2.0F)) + padding : 0.0F;
    const float width = std::max(tag_size.x + dot_width, line_size.x) + (padding * 2.0F);
    const float height = tag_size.y + (line.empty() ? 0.0F : line_size.y) + (padding * 2.0F);
    const ImVec2 corner(io.DisplaySize.x - margin - width, margin);
    list->AddRectFilled(corner, ImVec2(corner.x + width, corner.y + height), kBackground, kRounding * overlay.scale);
    float x = corner.x + padding;
    if (dot) {
        const float radius = tag_size.y * kDotRadius;
        list->AddCircleFilled(ImVec2(x + radius, corner.y + padding + (tag_size.y * kHalf)), radius, kRed);
        x += dot_width;
    }
    list->AddText(overlay.tag, overlay.tag->FontSize, ImVec2(x, corner.y + padding), kText, tag.c_str());
    if (!line.empty()) {
        list->AddText(overlay.line, overlay.line->FontSize, ImVec2(corner.x + padding, corner.y + padding + tag_size.y), kDim,
                      line.c_str());
    }
    ImGui::Render();
    frontend::ui_backend::render();
    ImGui::SetCurrentContext(previous);
}

void draw_split(float x, const std::string& left, const std::string& right, bool labels)
{
    State& overlay = state();
    ImGuiContext* previous = nullptr;
    if (!begin_context(&previous)) {
        return;
    }
    frontend::ui_backend::new_frame();
    ImGui::NewFrame();
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const float line_x = std::round(io.DisplaySize.x * std::clamp(x, 0.0F, 1.0F));
    list->AddLine(ImVec2(line_x, 0.0F), ImVec2(line_x, io.DisplaySize.y), kDivider, kDividerWidth * overlay.scale);
    if (labels) {
        const float margin = kMargin * overlay.scale;
        const float padding = kPadding * overlay.scale;
        const auto label = [&](const std::string& text, bool on_left) {
            if (text.empty()) {
                return;
            }
            const ImVec2 size = overlay.line->CalcTextSizeA(overlay.line->FontSize, FLT_MAX, 0.0F, text.c_str());
            const float width = size.x + padding + padding;
            const float left_edge = on_left ? line_x - margin - width : line_x + margin;
            const ImVec2 corner(left_edge, margin);
            list->AddRectFilled(corner, ImVec2(corner.x + width, corner.y + size.y + padding + padding), kBackground,
                                kRounding * overlay.scale);
            list->AddText(overlay.line, overlay.line->FontSize, ImVec2(corner.x + padding, corner.y + padding), kText,
                          text.c_str());
        };
        label(left, true);
        label(right, false);
    }
    ImGui::Render();
    frontend::ui_backend::render();
    ImGui::SetCurrentContext(previous);
}

void device_lost()
{
    if (state().context == nullptr) {
        return;
    }
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(state().context);
    frontend::ui_backend::invalidate_context_objects();
    ImGui::SetCurrentContext(previous);
}

} // namespace devtools::demo_overlay
