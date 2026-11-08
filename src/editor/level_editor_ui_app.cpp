// NOT ORIGINAL - the level editor as an application window (ours). See level_editor_ui_app.h. Only ImGui's own API (its
// docking and settings parts from imgui_internal.h), the engine's window title and game view here.

#include "editor/level_editor_ui_app.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor_content.h"
#include "editor/level_editor_split.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ui_layout.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/game_view.h"
#include "engine/sys/editor_mode.h"
#include "engine/ui/ui_backend.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "platform/run_log.h"

namespace frontend::level_editor::ui::app {

namespace {

constexpr const char* kLayoutFile = "thug2-decomp-editor.ini";
constexpr const char* kSettingsType = "LevelEditor"; // ([LevelEditor][Layout] in the layout file)
constexpr const char* kDockName = "LevelEditorDock";
constexpr const char* kSceneWindow = "Scene";
constexpr const char* kOutlinerWindow = "Outliner";
constexpr const char* kInspectorWindow = "Inspector";
constexpr const char* kContentWindow = "Content Browser"; // (level_editor_content.h)
// The default layout's side panels (at scale 1: level_editor_ui_layout.h's widths) and the bottom strip, as parts of
// the dock space; never more than these parts of it.
constexpr float kOutlinerWidth = 280.0F;
constexpr float kInspectorWidth = 320.0F;
constexpr float kMostSideFraction = 0.3F;
constexpr float kBottomFraction = 0.28F;
constexpr uint32_t kDecimalBase = 10;
constexpr uint32_t kHexBase = 16;
constexpr int kHexLetterValue = 10; // (a hex digit a's value)

// What the application's parts keep (app_state()).
struct AppState {
    // (the layout file's: the Window menu's)
    bool outliner_open = true;
    bool inspector_open = true;
    bool maximise_on_play = false;
    ImGuiID bottom = 0; // the bottom strip's dock node (0: none)
    bool rebuild = false; // (Reset Layout: the default built again on the next frame)
    // The Scene panel's last frame.
    ui_layout::Rect2 rect{};
    bool hovered = false;
    bool focused = false;
    bool focus_next = false;
    // The picture the frame being drawn uses (asked for in the frame before) and where it is in the back buffer.
    render::game_view::Request pending{};
    bool have_pending = false;
    float origin_x = 0.0F;
    float origin_y = 0.0F;
    // The window's title.
    std::string own_title;
    std::string title_level;
    bool title_set = false;
    // Playing in the panel (play_frame).
    bool playing = false;
    // (the layout file's: the editor mode's recent levels, the latest first)
    std::vector<RecentLevel> recent;
};

constexpr std::size_t kMostRecent = 8;

AppState& app_state()
{
    static core::NoDestroy<AppState> state;
    return state.get();
}

// --- the layout file ---------------------------------------------------------------------------------------------

std::string layout_path() { return game_folder_path(kLayoutFile); }

// A digit's value (`hex`: a hexadecimal one), or -1.
int digit_value(char character, bool hex)
{
    const auto unsigned_character = static_cast<unsigned char>(character);
    if (std::isdigit(unsigned_character) != 0) {
        return character - '0';
    }
    if (hex && std::isxdigit(unsigned_character) != 0) {
        return std::tolower(unsigned_character) - 'a' + kHexLetterValue;
    }
    return -1;
}

// A "Key=value" line's value as a number (base 10, or 16 after 0x); false when it isn't one.
bool read_number(std::string_view line, std::string_view key, uint32_t* value)
{
    if (!line.starts_with(key) || !line.substr(key.size()).starts_with('=')) {
        return false;
    }
    std::string_view digits = line.substr(key.size() + 1);
    uint32_t base = kDecimalBase;
    if (digits.starts_with("0x") || digits.starts_with("0X")) {
        digits.remove_prefix(2);
        base = kHexBase;
    }
    if (digits.empty()) {
        return false;
    }
    uint32_t number = 0;
    for (const char character : digits) {
        const int digit = digit_value(character, base == kHexBase);
        if (digit < 0) {
            return false;
        }
        number = (number * base) + static_cast<uint32_t>(digit);
    }
    *value = number;
    return true;
}

void* settings_open(ImGuiContext* /*context*/, ImGuiSettingsHandler* /*handler*/, const char* name)
{
    return std::strcmp(name, "Layout") == 0 ? static_cast<void*>(&app_state()) : nullptr;
}

void settings_line(ImGuiContext* /*context*/, ImGuiSettingsHandler* /*handler*/, void* entry, const char* line)
{
    auto* state = static_cast<AppState*>(entry);
    uint32_t value = 0;
    const std::string_view text(line);
    if (read_number(text, "Outliner", &value)) {
        state->outliner_open = value != 0U;
    } else if (read_number(text, "Inspector", &value)) {
        state->inspector_open = value != 0U;
    } else if (read_number(text, "MaximiseOnPlay", &value)) {
        state->maximise_on_play = value != 0U;
    } else if (read_number(text, "Bottom", &value)) {
        state->bottom = value;
    } else if (text.starts_with("Recent=")) {
        // (Recent=<load script, hex>,<1: the edited copy>)
        const std::string_view recent_text = text.substr(std::string_view("Recent=").size());
        const std::size_t comma = recent_text.find(',');
        uint32_t load = 0;
        uint32_t edited = 0;
        if (comma != std::string_view::npos &&
            read_number(std::string("L=").append(recent_text.substr(0, comma)), "L", &load) &&
            read_number(std::string("E=").append(recent_text.substr(comma + 1)), "E", &edited) && load != 0U &&
            state->recent.size() < kMostRecent) {
            state->recent.push_back({.load = load, .edited = edited != 0U});
        }
    }
}

void settings_write(ImGuiContext* /*context*/, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer)
{
    const AppState& state = app_state();
    std::string entry = text::format("[%s][Layout]\nOutliner=%d\nInspector=%d\nMaximiseOnPlay=%d\nBottom=0x%08X\n",
                                     handler->TypeName, state.outliner_open ? 1 : 0,
                                     state.inspector_open ? 1 : 0, state.maximise_on_play ? 1 : 0, state.bottom);
    for (const RecentLevel& level : state.recent) {
        text::append_format(entry, "Recent=0x%08X,%d\n", level.load, level.edited ? 1 : 0);
    }
    entry += "\n";
    buffer->append(entry.c_str());
}

// --- the dock space ----------------------------------------------------------------------------------------------

ImGuiID dock_id() { return ImHashStr(kDockName); }

// The default layout in the dock space: the Outliner left, the Inspector right, the Scene in the middle over an empty
// bottom strip (the log-type windows dock there - dock_next_in_bottom).
void build_default_layout(ImGuiID dock, ImVec2 size, float scale)
{
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, size);
    const float width = std::max(1.0F, size.x);
    const float left_part = std::min(kMostSideFraction, kOutlinerWidth * scale / width);
    // (the Inspector's part of what is left of the width once the Outliner has its own)
    const float right_part = std::min(kMostSideFraction, kInspectorWidth * scale / std::max(1.0F, width * (1.0F - left_part)));
    ImGuiID centre = dock;
    const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, left_part, nullptr, &centre);
    const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, right_part, nullptr, &centre);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, kBottomFraction, nullptr, &centre);
    ImGui::DockBuilderDockWindow(kSceneWindow, centre);
    ImGui::DockBuilderDockWindow(kOutlinerWindow, left);
    ImGui::DockBuilderDockWindow(kInspectorWindow, right);
    ImGui::DockBuilderDockWindow(kContentWindow, bottom); // (as Unreal / Unity: the assets along the bottom)
    ImGui::DockBuilderFinish(dock);
    app_state().bottom = bottom;
    ImGui::MarkIniSettingsDirty();
    get_log("level editor UI: the default layout (%.0fx%.0f)", size.x, size.y);
}

// --- the window ----------------------------------------------------------------------------------------------------

// The window's pixels per back-buffer pixel (the window may show a back buffer of another size).
ImVec2 window_per_back_pixel()
{
    int back_width = 0;
    int back_height = 0;
    ui_backend::back_buffer_size({.width = &back_width, .height = &back_height});
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    return {back_width > 0 ? display.x / static_cast<float>(back_width) : 1.0F,
            back_height > 0 ? display.y / static_cast<float>(back_height) : 1.0F};
}

ImU32 overlay_colour(uint32_t argb)
{
    return IM_COL32((argb >> 16U) & 0xFFU, (argb >> 8U) & 0xFFU, argb & 0xFFU, (argb >> 24U) & 0xFFU);
}

constexpr float kTextBackPadding = 3.0F; // the padding behind an overlay text
constexpr float kTextBackRounding = 3.0F;
constexpr ImU32 kTextBackColour = IM_COL32(16, 17, 19, 170);

// What's drawn over the view (back-buffer pixels, scaled to the window's) into the panel's draw list.
void draw_overlay(ImDrawList* list, const Overlay& overlay, SceneOverlayFonts fonts)
{
    const ImVec2 to_window = window_per_back_pixel();
    const float sx = to_window.x;
    const float sy = to_window.y;
    for (const Overlay::Triangle& triangle : overlay.under) {
        list->AddTriangleFilled(ImVec2(triangle.ax * sx, triangle.ay * sy), ImVec2(triangle.bx * sx, triangle.by * sy),
                                ImVec2(triangle.cx * sx, triangle.cy * sy), overlay_colour(triangle.argb));
    }
    for (const Seg& line : overlay.lines) {
        list->AddLine(ImVec2(line.ax * sx, line.ay * sy), ImVec2(line.bx * sx, line.by * sy), overlay_colour(line.argb),
                      line.thickness * fonts.scale);
    }
    for (const Overlay::Triangle& triangle : overlay.triangles) {
        list->AddTriangleFilled(ImVec2(triangle.ax * sx, triangle.ay * sy), ImVec2(triangle.bx * sx, triangle.by * sy),
                                ImVec2(triangle.cx * sx, triangle.cy * sy), overlay_colour(triangle.argb));
    }
    for (const Overlay::Circle& circle : overlay.circles) {
        if (circle.filled) {
            list->AddCircleFilled(ImVec2(circle.x * sx, circle.y * sy), circle.radius * sx, overlay_colour(circle.argb));
        } else {
            list->AddCircle(ImVec2(circle.x * sx, circle.y * sy), circle.radius * sx, overlay_colour(circle.argb), 0,
                            circle.thickness * fonts.scale);
        }
    }
    if (!fonts.mono) {
        return;
    }
    for (const Overlay::OverlayText& text : overlay.texts) {
        const ImVec2 at(text.x * sx, text.y * sy);
        const ImVec2 size = fonts.mono->CalcTextSizeA(fonts.mono->FontSize, 1e9F, 0.0F, text.text.c_str());
        list->AddRectFilled(ImVec2(at.x - kTextBackPadding, at.y - 1.0F),
                            ImVec2(at.x + size.x + kTextBackPadding, at.y + size.y + 1.0F), kTextBackColour,
                            kTextBackRounding);
        list->AddText(fonts.mono, fonts.mono->FontSize, at, overlay_colour(text.argb), text.text.c_str());
    }
}

} // namespace

void prepare_context()
{
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDockingWithShift = false;
    ImGuiSettingsHandler handler;
    handler.TypeName = kSettingsType;
    handler.TypeHash = ImHashStr(kSettingsType);
    handler.ReadOpenFn = settings_open;
    handler.ReadLineFn = settings_line;
    handler.WriteAllFn = settings_write;
    ImGui::AddSettingsHandler(&handler);
    std::string text;
    if (fs::read_text(layout_path(), &text) && !text.empty()) {
        ImGui::LoadIniSettingsFromMemory(text.data(), text.size());
        get_log("level editor UI: the layout from %s", kLayoutFile);
    }
}

void save_layout(bool now)
{
    ImGuiIO& io = ImGui::GetIO();
    if (!now && !io.WantSaveIniSettings) {
        return;
    }
    io.WantSaveIniSettings = false;
    size_t size = 0;
    const char* data = ImGui::SaveIniSettingsToMemory(&size);
    if (!fs::write_text(layout_path(), std::string_view(data, size)).written) {
        get_log("level editor UI: the layout couldn't be written to %s", kLayoutFile);
    }
}

bool* panel_open(Panel panel)
{
    return panel == Panel::kOutliner ? &app_state().outliner_open : &app_state().inspector_open;
}

void window_menu_panels()
{
    AppState& state = app_state();
    const bool outliner = ImGui::MenuItem("Outliner", nullptr, &state.outliner_open);
    const bool inspector = ImGui::MenuItem("Inspector", nullptr, &state.inspector_open);
    // (the Content Browser keeps its own switch: level_editor_content.h)
    if (ImGui::MenuItem("Content Browser", "Ctrl+Shift+B", content::open_flag())) {
        content::note_toggled();
    }
    if (outliner || inspector) {
        ImGui::MarkIniSettingsDirty();
    }
}

void window_menu_layout()
{
    if (ImGui::MenuItem("Reset Layout")) {
        AppState& state = app_state();
        state.rebuild = true;
        state.outliner_open = state.inspector_open = true;
    }
}

void window_menu()
{
    window_menu_panels();
    ImGui::Separator();
    window_menu_layout();
}

void play_menu()
{
    if (ImGui::MenuItem("Maximise on Play", nullptr, &app_state().maximise_on_play)) {
        ImGui::MarkIniSettingsDirty();
    }
}

void dock_space(float scale)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dock = dock_id();
    AppState& state = app_state();
    if (state.rebuild || ImGui::DockBuilderGetNode(dock) == nullptr) {
        state.rebuild = false;
        build_default_layout(dock, viewport->WorkSize, scale);
    }
    ImGui::DockSpaceOverViewport(dock, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
}

bool place_floating(const char* name, bool bottom)
{
    if (const ImGuiWindow* window = ImGui::FindWindowByName(name)) {
        return window->DockId == 0;
    }
    const ImGuiID strip = app_state().bottom;
    if (!bottom || strip == 0 || ImGui::DockBuilderGetNode(strip) == nullptr) {
        return true;
    }
    ImGui::SetNextWindowDockID(strip, ImGuiCond_Always);
    return false;
}

void scene_panel(const Overlay* overlay, SceneOverlayFonts fonts)
{
    AppState& state = app_state();
    state.rect = {};
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    // (floating, it stays in the game's window: the editor's mouse - picking, flying - is that window's; the other
    // panels may be dragged out into windows of their own - ui_backend::enable_viewports)
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    const bool shown = ImGui::Begin(kSceneWindow, nullptr,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    if (!shown) {
        state.hovered = state.focused = false;
        state.have_pending = false;
        ImGui::End();
        return;
    }
    if (state.focus_next) {
        state.focus_next = false;
        ImGui::SetWindowFocus();
    }
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 room = ImGui::GetContentRegionAvail();
    state.rect = {.x = at.x, .y = at.y, .width = std::max(0.0F, room.x), .height = std::max(0.0F, room.y)};
    ImDrawList* list = ImGui::GetWindowDrawList();
    // The frame just drawn, where it was asked to be shown (the overlay's projections are of that view).
    const uint64_t texture = render::game_view::texture();
    const render::game_view::Size drawn = render::game_view::drawn_size();
    const ImVec2 to_window = window_per_back_pixel();
    if (texture != 0U && state.have_pending) {
        const ImVec2 from(state.pending.shown.x, state.pending.shown.y);
        const ImVec2 to(from.x + (static_cast<float>(drawn.width) * to_window.x),
                        from.y + (static_cast<float>(drawn.height) * to_window.y));
        list->AddImage(static_cast<ImTextureID>(texture), from, to);
    }
    // (each view's overlay in it, the views' corner menus - level_editor_split.h; none in play)
    bool on_controls = false;
    if (overlay) {
        on_controls = split_view::draw_panel({.list = list,
                                              .rect = state.rect,
                                              .active_overlay = overlay,
                                              .draw_overlay = [list, fonts](const Overlay& view_overlay) {
                                                  draw_overlay(list, view_overlay, fonts);
                                              },
                                              .scale = fonts.scale});
    }
    state.hovered = ImGui::IsWindowHovered() && !on_controls &&
                    !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    state.focused = ImGui::IsWindowFocused();
    // The next frame's picture: the panel's size in the back buffer's pixels.
    const float back_x = to_window.x > 0.0F ? 1.0F / to_window.x : 1.0F;
    const float back_y = to_window.y > 0.0F ? 1.0F / to_window.y : 1.0F;
    const render::game_view::Request request{
        .width = static_cast<int>(std::lround(state.rect.width * back_x)),
        .height = static_cast<int>(std::lround(state.rect.height * back_y)),
        .shown = {.x = state.rect.x, .y = state.rect.y, .width = state.rect.width, .height = state.rect.height}};
    render::game_view::request(request);
    state.pending = request;
    state.have_pending = true;
    state.origin_x = state.rect.x * back_x;
    state.origin_y = state.rect.y * back_y;
    ImGui::End();
}

ui_layout::Rect2 scene_rect() { return app_state().rect; }

bool scene_hovered() { return app_state().hovered; }

bool scene_focused() { return app_state().focused; }

bool picture_origin(PictureOrigin* origin)
{
    const AppState& state = app_state();
    if (!render::game_view::on() || !state.have_pending) {
        return false;
    }
    *origin = {.x = state.origin_x, .y = state.origin_y};
    return true;
}

const std::vector<RecentLevel>& recent_levels() { return app_state().recent; }

void note_recent(RecentLevel level)
{
    std::vector<RecentLevel>& recent = app_state().recent;
    std::erase_if(recent, [level](const RecentLevel& known) { return known.load == level.load && known.edited == level.edited; });
    recent.insert(recent.begin(), level);
    if (recent.size() > kMostRecent) {
        recent.resize(kMostRecent);
    }
    ImGui::MarkIniSettingsDirty();
}

void set_window_title(const std::string& level)
{
    AppState& state = app_state();
    if (level.empty()) {
        if (state.title_set) {
            ui_backend::set_window_title(state.own_title);
            state.title_set = false;
            state.title_level.clear();
        }
        return;
    }
    if (!state.title_set) {
        state.own_title = ui_backend::window_title();
        state.title_set = true;
    } else if (state.title_level == level) {
        return;
    }
    state.title_level = level;
    // (the editor mode's window: "THUG2 Editor - <level>" - engine/sys/editor_mode.h)
    if (sys::editor_mode::on()) {
        ui_backend::set_window_title(state.own_title + " - " + level);
        return;
    }
    ui_backend::set_window_title(level + " - Level Editor" + (state.own_title.empty() ? "" : " - " + state.own_title));
}

bool maximised_on_play() { return app_state().maximise_on_play; }

bool note_playing(bool playing)
{
    AppState& state = app_state();
    const bool started = playing && !state.playing;
    state.playing = playing;
    if (started) {
        state.focus_next = true; // (the game's keys follow the Scene panel's focus: it has it as play starts)
    }
    return started;
}

bool blocks_game_input()
{
    const AppState& state = app_state();
    return state.playing && !state.focused;
}

} // namespace frontend::level_editor::ui::app
