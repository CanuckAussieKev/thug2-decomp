// NOT ORIGINAL - the level editor's UI (ours). See level_editor_ui.h. Only ImGui's own API here: the platform and the
// renderer are level_editor_ui_backend.cpp's (render::UiBackend). This file holds the UI state,
// the shared widgets, the toolbar and sections, the event routing and the per-frame cycle; the pieces (ui_outliner / ui_inspector / ui_dialogs) are
// level_editor_ui_<name>.cpp, sharing level_editor_ui_internal.h.

#include "editor/level_editor_actions.h"
#include "editor/level_editor_adjust.h"
#include "editor/level_editor_arrange.h"
#include "editor/level_editor_ui.h"
#include "engine/pad_input/input_event.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <array>
#include <vector>
#include "editor/level_editor.h"
#include "editor/level_editor_budget.h"
#include "editor/level_editor_hints.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_notes.h"
#include "editor/level_editor_ux9.h"
#include "base/core/format.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_play.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_transform.h"
#include "engine/ui/ui_backend.h"
#include "editor/level_editor_shell.h"
#include "editor/level_editor_ui_app.h"
#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_ui_strip.h"
#include "editor/level_editor_ui_theme.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
#include "editor/level_editor_view.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "base/core/checked_index.h"
#include "editor/level_editor_gui_text.h"
#include "base/core/no_destroy.h"
#include "base/maths/maths.h"
#include "editor/level_editor_ui_internal.h"

namespace frontend::level_editor::ui {
// What the source file's anonymous namespace held that its pieces share (level_editor_ui_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor::ui

namespace frontend::level_editor::ui {
namespace {

constexpr float kKeyCapRounding = 3.0F; // the key hints' key caps
constexpr float kKeyHintGap = 4.0F;
constexpr float kLegendBackgroundAlpha = 0.82F; // the collision legend

constexpr float kSwatchOutlineThickness = 2.0F;

constexpr float kToolbarFramePaddingX = 6.0F;
constexpr float kToolbarFramePaddingY = 3.0F;

constexpr float kPreviewBasePixels = 256.0F; // the preview image's size at scale 1...
constexpr int kPreviewMinPixels = 256; // ...and its limits
constexpr int kPreviewMaxPixels = 512;

// Colours.
constexpr ImVec4 kWallNameColour{1.0F, 0.35F, 0.9F, 1.0F};
constexpr ImVec4 kWallStateColour{1.0F, 0.55F, 0.3F, 1.0F};

} // namespace
namespace internal {

ImguiSetup& imgui_setup()
{
    static core::NoDestroy<ImguiSetup> setup;
    return setup.get();
}

InputRouting& input_routing()
{
    static core::NoDestroy<InputRouting> routing;
    return routing.get();
}

PanelState& panel_state()
{
    static core::NoDestroy<PanelState> panels;
    return panels.get();
}

void get_ask(Request::Kind kind, int value, uint32_t id) { input_routing().requests_.push_back({.kind = kind, .value = value, .id = id}); }

// The accent colour ([editor] accent - level_editor_prefs.h) at `alpha`.
ImVec4 get_accent(float alpha)
{
    std::array<float, 3> rgb{};
    prefs::get_accent_rgb(prefs::get_pull().accent, rgb);
    return {core::at(rgb, 0), core::at(rgb, 1), core::at(rgb, 2), alpha};
}

// ARGB (the editor's colours) to ImGui's packed colour.
ImU32 get_colour(uint32_t argb)
{
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

} // namespace internal
namespace {

// --- fonts and style ---

// The fonts and the style for the window's size (made again when its scale changes: the font texture with them).
void update_scale()
{
    int width = 0;
    int height = 0;
    ui_backend::window_size(&width, &height);
    float scale = ui_layout::scale_for(width, height);
    if (const int pct = prefs::get_pull().ui_scale_pct_;
        pct > 0) { // ([editor] ui_scale_pct: the automatic one overridden)
        scale = static_cast<float>(pct) / 100.0F;
    }
    if (scale == imgui_setup().scale_ &&
        imgui_setup().style_generation_ == prefs::style_generation()) {
        return;
    }
    imgui_setup().scale_ = scale;
    imgui_setup().style_generation_ = prefs::style_generation();
    const ui_layout::FontSizes sizes = ui_layout::font_sizes_for(scale);
    const ui_theme::Fonts fonts = ui_theme::build_fonts(scale);
    imgui_setup().ui_ = fonts.ui;
    imgui_setup().small_ = fonts.small;
    imgui_setup().mono_ = fonts.mono;
    imgui_setup().heading_ = fonts.heading;
    ui_backend::invalidate_device_objects(); // (the font texture is made again)
    std::array<float, 3> rgb{};
    prefs::get_accent_rgb(prefs::get_pull().accent, rgb);
    ui_theme::apply_style(scale, rgb);
    get_log("level editor UI: scale %.2f for a %dx%d window (Inter %.0f px, JetBrains Mono %.0f px)", scale, width,
            height, sizes.ui, sizes.mono);
}

} // namespace
namespace internal {

void get_muted(const char* text) { gui::text_disabled("%s", text); }

} // namespace internal
namespace {

// A key in a key cap, then what it does.
void key_hint(const char* key, const char* what)
{
    ImGui::PushFont(imgui_setup().mono_);
    const ImVec2 size = ImGui::CalcTextSize(key);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float pad_x = 4.0F * imgui_setup().scale_;
    const float pad_y = 1.0F * imgui_setup().scale_;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(pos.x, pos.y - pad_y), ImVec2(pos.x + size.x + (ui_layout::kBothSides * pad_x), pos.y + size.y + pad_y),
        IM_COL32(60, 63, 68, 255), kKeyCapRounding * imgui_setup().scale_);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad_x, pos.y));
    ImGui::TextUnformatted(key);
    ImGui::PopFont();
    ImGui::SameLine(0.0F, pad_x + (kKeyHintGap * imgui_setup().scale_));
    ImGui::PushFont(imgui_setup().small_);
    ImGui::TextUnformatted(what);
    ImGui::PopFont();
}

} // namespace
namespace internal {

ux3::Fonts ux3_fonts()
{
    return {.ui = imgui_setup().ui_,
            .small = imgui_setup().small_,
            .mono = imgui_setup().mono_,
            .heading = imgui_setup().heading_,
            .scale = imgui_setup().scale_};
}

// (UX8's drawing: the UI's fonts and scale)
ux8::Fonts ux8_fonts()
{
    ux8::Fonts fonts;
    fonts.ui = imgui_setup().ui_;
    fonts.small = imgui_setup().small_;
    fonts.mono = imgui_setup().mono_;
    fonts.scale = imgui_setup().scale_;
    return fonts;
}

bool get_matches(const std::string& text, const char* filter)
{
    if (*filter == '\0') {
        return true;
    }
    auto lower = [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); };
    std::string haystack = text;
    std::string needle = filter;
    std::ranges::transform(haystack, haystack.begin(), lower);
    std::ranges::transform(needle, needle.begin(), lower);
    return haystack.find(needle) != std::string::npos;
}

void place_window(const ui_layout::Rect2& rect)
{
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
    ImGui::SetNextWindowSize(ImVec2(rect.width, rect.height));
}

void panel_title(const char* title)
{
    ImGui::PushFont(imgui_setup().heading_);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Separator();
}

} // namespace internal
namespace {

// --- the view options (level_editor_view.h) ---

// The collision's legend, over the bottom left of the view while it's shown.
void collision_legend(const ShapeMesh& model)
{
    if (!model.view.collision ||
        !overlays::get_shown(overlays::kLegend)) { // (the Overlays menu: level_editor_overlays.h)
        return;
    }
    const ui_layout::Rect2& view = input_routing().layout_.viewport;
    ImGui::SetNextWindowPos(ImVec2(view.x + (kPanelInset * imgui_setup().scale_),
                                   view.y + view.height - (kPanelInset * imgui_setup().scale_)),
                            ImGuiCond_Always, ImVec2(0.0F, 1.0F));
    ImGui::SetNextWindowBgAlpha(kLegendBackgroundAlpha);
    ImGui::Begin("##collisionLegend", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PushFont(imgui_setup().small_);
    ImGui::TextUnformatted("Collision");
    ImGui::SameLine();
    if (ImGui::SmallButton("Hide##collisionLegend")) {
        get_ask(Request::Kind::kCollision); // (value 0: off)
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Hide Collision  (C)");
    }
    for (int i = 0; i < static_cast<int>(view::Surface::kCount); ++i) {
        auto surface = static_cast<view::Surface>(i);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float side = ImGui::GetTextLineHeight();
        const uint32_t argb = view::surface_colour(surface) | 0xFF000000;
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + side, at.y + side), get_colour(argb), kSwatchRounding);
        ImGui::Dummy(ImVec2(side, side));
        ImGui::SameLine();
        ImGui::TextUnformatted(view::surface_name(surface));
    }
    gui::text_disabled("%d faces, %d pieces, %d invisible walls%s", model.view.triangles, model.view.sectors,
                        model.view.walls, model.view.capped ? " (nearest)" : "");
    if (model.view.off > 0) {
        gui::text_disabled("Faded / dashed: collision off (%d)", model.view.off);
    }
    // (a moved piece's shared collision - level_editor_shared_coll.h - and the level's triggers by kind)
    auto swatch_line = [](uint32_t argb, const char* text) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float side = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRect(at, ImVec2(at.x + side, at.y + side), get_colour(argb | kOpaqueAlpha), kSwatchRounding,
                                            0, kSwatchOutlineThickness);
        ImGui::Dummy(ImVec2(side, side));
        ImGui::SameLine();
        ImGui::TextUnformatted(text);
    };
    swatch_line(view::kCompanionEdge, "Moved collision");
    swatch_line(view::kTakenEdge, "Original collision position (off)");
    swatch_line(view::kHoverWall, "Wall under cursor (Shift)");
    if (model.view.companions > 0 || model.view.taken > 0) {
        gui::text_disabled("%d moved pieces, %d faces moved", model.view.companions, model.view.taken);
    }
    if (ImGui::TreeNode("Triggers")) {
        for (int k = 0; k < triggers::kKinds; ++k) {
            auto kind = static_cast<triggers::Kind>(k);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float side = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + side, at.y + side),
                                                      get_colour(triggers::kind_colour(kind) | kOpaqueAlpha), kSwatchRounding);
            ImGui::Dummy(ImVec2(side, side));
            ImGui::SameLine();
            ImGui::TextUnformatted(triggers::kind_name(kind));
        }
        gui::text_disabled("%d trigger faces", model.view.triggers);
        ImGui::TreePop();
    }
    gui::text_disabled("Not shown: camera pass-through, not taggable");
    ImGui::PopFont();
    ImGui::End();
}

namespace {

// The invisible wall selected (Shift+click): its name and state, its collision switched, reverted, dropped.
void wall_section(const ShapeMesh& model)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushFont(imgui_setup().heading_);
    gui::text_colored(kWallNameColour, "%s", model.view.wall_name_.c_str());
    ImGui::PopFont();
    get_muted("Invisible wall");
    ImGui::SameLine();
    get_mono("0x%08X", model.view.wall);
    ImGui::PushFont(imgui_setup().mono_);
    gui::text_wrapped("%s", model.view.wall_info_.c_str());
    ImGui::PopFont();
    // (level_editor_walls.h: the game's own switch, WorldCell::change_live; moved and turned as scenery)
    if (model.view.wall_off_) {
        gui::text_colored(kWallStateColour, "%s", model.view.wall_state_.c_str());
    } else {
        gui::text_wrapped("%s", model.view.wall_state_.c_str());
    }
    if (model.view.wall_off_) {
        if (ImGui::Button("Restore Collision")) {
            get_ask(Request::Kind::kWallCollision, 1);
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Turn collision back on  (Del)");
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ui_theme::kDangerButtonColour);
        if (ImGui::Button("Disable Collision")) {
            get_ask(Request::Kind::kWallCollision, 0);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Turn this wall's collision off  (Del)");
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!model.view.wall_moved_ && !model.view.wall_off_);
    if (ImGui::Button("Revert##wall")) {
        get_ask(Request::Kind::kReset);
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Drop to Ground##wall")) {
        get_ask(Request::Kind::kDrop);
    }
    ImGui::SameLine();
    if (ImGui::Button("Close##wall")) {
        get_ask(Request::Kind::kClearWall);
    }
}

} // namespace
} // namespace
namespace internal {

// The Inspector's part: an invisible wall selected (Shift+click), and what's hidden in the editor.
void view_section(const ShapeMesh& model)
{
    if (model.view.wall != 0U) {
        wall_section(model);
    }
    if (model.view.scenery_collision_ >= 0 && (model.view.wall == 0U)) {
        // A drawn piece's collision switched off by itself (it stays drawn; the same switch, level_editor_walls.h).
        ImGui::Spacing();
        ImGui::Separator();
        bool on = model.view.scenery_collision_ == 1;
        if (ImGui::Checkbox(model.view.scenery_collision_ == 2 ? "Collision (mixed)" : "Collision", &on)) {
            get_ask(Request::Kind::kSceneryCollision, on ? 1 : 0);
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Off: visible but not solid");
        }
    }
    if (model.view.can_hide_ || model.view.hidden > 0) {
        ImGui::Spacing();
        ImGui::Separator();
        get_muted("Editor Visibility");
        ImGui::BeginDisabled(!model.view.can_hide_);
        if (ImGui::Button("Hide")) {
            get_ask(Request::Kind::kViewHide);
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Hide Selection  (H)");
        }
        ImGui::SameLine();
        if (ImGui::Button("Isolate")) {
            get_ask(Request::Kind::kViewIsolate);
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Isolate Selection  (I)");
        }
        ImGui::EndDisabled();
        if (model.view.hidden > 0) {
            ImGui::SameLine();
            if (ImGui::Button("Show All")) {
                get_ask(Request::Kind::kViewShowAll);
            }
            gui::text_disabled("%d hidden", model.view.hidden);
        }
    }
    faces::inspector_section(imgui_setup().mono_, imgui_setup().small_); // (map3: the collision faces selected - level_editor_faces.h)
}

} // namespace internal
namespace {

// --- the panels ---

// The window's menu bar, then the tool strip across the window under it (level_editor_ui_strip.h).
void get_toolbar(const ShapeMesh& model)
{
    if (ImGui::BeginMainMenuBar()) {
        menu_bar(model, ux8_fonts(), &input_routing().requests_);
        ImGui::EndMainMenuBar();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kBarPaddingX * imgui_setup().scale_, kBarPaddingY * imgui_setup().scale_));
    const bool shown = ImGui::BeginViewportSideBar("##toolstrip", ImGui::GetMainViewport(), ImGuiDir_Up,
                                                   input_routing().layout_.strip.height, kPanel | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (!shown) {
        ImGui::End();
        return;
    }
    input_routing().layout_.strip = {.x = ImGui::GetWindowPos().x,
                                     .y = ImGui::GetWindowPos().y,
                                     .width = ImGui::GetWindowSize().x,
                                     .height = ImGui::GetWindowSize().y};
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kToolbarFramePaddingX * imgui_setup().scale_, kToolbarFramePaddingY * imgui_setup().scale_));
    ui_strip::State state;
    state.selection = std::strcmp(model.mode, "FLYING") != 0 && std::strcmp(model.mode, "ADD") != 0;
    xform::Settings& transform = xform::at_hand();
    state.tool = static_cast<int>(transform.mode);
    state.local = transform.space == xform::Space::kLocal;
    state.snap = model.snap_always_;
    state.grid = xform::get_grid();
    state.grids = xform::grid_sizes();
    state.pick_filter_ = model.pick_filter_;
    state.level = model.level_title_;
    state.saved = model.edited_saved_;
    state.unsaved = model.unsaved;
    state.budget = budget::strip_meter(&state.budget_fill_, &state.budget_argb_);
    int infos = 0;
    checks::shown_counts(&state.errors, &state.warnings, &infos);
    state.hidden = model.view.hidden;
    state.simulating = play_test::get_simulating();
    state.key_of_ = [](const char* id) {
        const int index = actions::get_find(id);
        return index >= 0 ? hints::leading_key_name(actions::get_keys_text(index)) : std::string();
    };
    const ui_layout::Rect2& strip = input_routing().layout_.strip;
    const float pad_x = 8.0F * imgui_setup().scale_;
    const float pad_y = std::max(0.0F, (strip.height - ImGui::GetFrameHeight()) * 0.5F);
    ui_strip::Output out;
    ui_strip::blit(state, {.x = strip.x + pad_x, .y = strip.y + pad_y}, strip.width - (ui_layout::kBothSides * pad_x),
                   imgui_setup().scale_, &out);
    ImGui::PopStyleVar();
    if (out.budget_hovered_) {
        budget::tooltip();
    }
    if (out.level_hovered_) {
        const char* layout = "not saved yet";
        if (model.edited_saved_) {
            layout = model.unsaved ? "saved, with unsaved changes" : "saved";
        }
        gui::tooltip("Editing a copy of %s. The original level is unchanged.\nLayout: %s", model.level_title_.c_str(),
                     layout);
    }
    for (const ui_strip::Occurrence& event : out.events) {
        switch (event.kind) {
        case ui_strip::Occurrence::Kind::kDeselect:
            get_ask(Request::Kind::kDeselect);
            break;
        case ui_strip::Occurrence::Kind::kTool:
            transform.mode = static_cast<xform::Mode>(static_cast<int>(event.value));
            break;
        case ui_strip::Occurrence::Kind::kSpace:
            transform.space = transform.space == xform::Space::kWorld ? xform::Space::kLocal : xform::Space::kWorld;
            break;
        case ui_strip::Occurrence::Kind::kSnap:
            get_ask(Request::Kind::kSnapAlways, event.value != 0.0F ? 1 : 0);
            break;
        case ui_strip::Occurrence::Kind::kGrid:
            xform::set_grid(event.value);
            break;
        case ui_strip::Occurrence::Kind::kPick:
            get_ask(Request::Kind::kPickFilter, 0, static_cast<uint32_t>(event.value));
            break;
        case ui_strip::Occurrence::Kind::kProblems:
            checks::toggle_window();
            break;
        case ui_strip::Occurrence::Kind::kBudget:
            checks::open_window(checks::kBudgetTab);
            break;
        case ui_strip::Occurrence::Kind::kShowAll:
            get_ask(Request::Kind::kViewShowAll);
            break;
        case ui_strip::Occurrence::Kind::kSimulate:
            actions::get_run(actions::kSimulate);
            break;
        }
    }
    ImGui::End();
}

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "editor_ui", 1) != 0;
    return on;
}

bool underway() { return imgui_setup().running_; }

int preview_pixels()
{
    const float scale = imgui_setup().scale_ > 0.0F ? imgui_setup().scale_ : 1.0F;
    return std::clamp(maths::round_non_negative<int>(kPreviewBasePixels * scale), kPreviewMinPixels, kPreviewMaxPixels);
}

namespace {

// The editor's ImGui context, made once.
void create_context()
{
    if (imgui_setup().created_) {
        return;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // (nothing written to the game folder)
    io.LogFilename = nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    app::prepare_context(); // (docking on, the layout read: thug2-decomp-editor.ini - level_editor_ui_app.h)
    imgui_setup().created_ = true;
}

} // namespace

bool kickoff()
{
    if (imgui_setup().running_) {
        return true;
    }
    if (!is_switched_on()) {
        return false;
    }
    create_context();
    if (!ui_backend::prepare_all()) {
        get_log("level editor UI: not started - the old HUD instead");
        return false;
    }
    imgui_setup().running_ = true;
    imgui_setup().scale_ = 0.0F; // (the fonts made for the window's size on the first frame)
    input_routing().requests_.clear();
    panel_state().offer_closed_ = false;
    input_routing().ui_buttons_ = input_routing().editor_buttons_ = 0;
    input_routing().want_mouse_ = input_routing().want_keys_ = false;
    panel_state().browser_shown_index_ = -1;
    panel_state().browser_hovered_ = panel_state().preview_asked_ = -1;
    panel_state().browser_keys_won_ = false;
    return true;
}

void halt()
{
    if (imgui_setup().running_) {
        app::save_layout(true);
    }
    imgui_setup().running_ = false;
    input_routing().ui_buttons_ = input_routing().editor_buttons_ = 0;
    input_routing().want_mouse_ = input_routing().want_keys_ = false;
}

void device_lost() { ui_backend::invalidate_device_objects(); }

namespace {

// Whether a point of the window is on the Scene panel and the panel has the mouse (nothing over it, no popup open):
// the editor's viewport (level_editor_ui_app.h).
bool over_scene(float x, float y) { return app::scene_hovered() && ui_layout::get_holds(app::scene_rect(), x, y); }

} // namespace

bool route_event(const input::Event& event, bool editor_capture)
{
    if (!imgui_setup().running_) {
        return true;
    }
    auto route = [&](float x, float y) {
        input_routing().mouse_x_ = x;
        input_routing().mouse_y_ = y;
        return ui_layout::decide(
            {.ui_wants_mouse_ = input_routing().want_mouse_, .ui_wants_keyboard_ = input_routing().want_keys_, .over_panel_ = !over_scene(x, y), .editor_capture_ = editor_capture || input_routing().editor_buttons_ != 0});
    };
    switch (event.type) {
    case input::EventType::kMouseMotion: {
        const ui_layout::Route mouse_route =
            route(static_cast<float>(event.x), static_cast<float>(event.y));
        if (mouse_route.mouse_to_ui_ || (input_routing().ui_buttons_ != 0U)) {
            ui_backend::process_event(event.native);
        }
        return true; // (the editor keeps the position and the look's movement; mouse_to_editor says if it may pick)
    }
    case input::EventType::kMouseButtonDown: {
        const uint32_t bit = 1U << (event.button & 31);
        const ui_layout::Route mouse_route =
            route(static_cast<float>(event.x), static_cast<float>(event.y));
        if (!mouse_route.mouse_to_editor_) {
            input_routing().ui_buttons_ |= bit;
            ui_backend::process_event(event.native);
            return false;
        }
        input_routing().editor_buttons_ |= bit;
        if (mouse_route.mouse_to_ui_) {
            ui_backend::process_event(event.native); // (ImGui loses its focus and hover; the editor has the click)
        }
        return true;
    }
    case input::EventType::kMouseButtonUp: {
        const uint32_t bit = 1U << (event.button & 31);
        if ((input_routing().ui_buttons_ & bit) != 0U) {
            input_routing().ui_buttons_ &= ~bit;
            ui_backend::process_event(event.native);
            return false;
        }
        input_routing().editor_buttons_ &= ~bit;
        ui_backend::process_event(event.native); // (ImGui's button state stays true)
        return true;
    }
    case input::EventType::kMouseWheel: {
        const ui_layout::Route mouse_route = route(input_routing().mouse_x_, input_routing().mouse_y_);
        if (!mouse_route.mouse_to_editor_) {
            ui_backend::process_event(event.native);
            return false;
        }
        return true;
    }
    case input::EventType::kKeyDown:
    case input::EventType::kKeyUp:
    case input::EventType::kTextInput:
    case input::EventType::kTextEditing:
        ui_backend::process_event(event.native);
        return !input_routing().want_keys_;
    default:
        ui_backend::process_event(event.native);
        return true;
    }
}

bool mouse_to_editor()
{
    if (!imgui_setup().running_) {
        return true;
    }
    return ui_layout::decide(
               {.ui_wants_mouse_ = input_routing().want_mouse_, .ui_wants_keyboard_ = input_routing().want_keys_, .over_panel_ = !over_scene(input_routing().mouse_x_, input_routing().mouse_y_), .editor_capture_ = input_routing().editor_buttons_ != 0})
        .mouse_to_editor_;
}

bool keys_to_editor() { return !imgui_setup().running_ || !input_routing().want_keys_; }

void push_request(const Request& request) { input_routing().requests_.push_back(request); }

std::vector<Request> take_requests()
{
    std::vector<Request> requests;
    requests.swap(input_routing().requests_);
    return requests;
}

void tick_cycle(const ShapeMesh& model, const Overlay& overlay)
{
    if (!imgui_setup().running_) {
        return;
    }
    update_scale();
    ui_backend::new_frame();
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();
    // (the bars' heights; the panels are docked - level_editor_ui_app.h)
    input_routing().layout_ = ui_layout::compute({.width = io.DisplaySize.x, .height = io.DisplaySize.y}, imgui_setup().scale_, true, true);
    app::note_playing(false);
    app::set_window_title(model.level_title_.empty() ? model.level : model.level_title_);
    ImGui::PushFont(imgui_setup().ui_);
    {
        ux3::Fonts fonts;
        fonts.ui = imgui_setup().ui_;
        fonts.small = imgui_setup().small_;
        fonts.mono = imgui_setup().mono_;
        fonts.heading = imgui_setup().heading_;
        fonts.scale = imgui_setup().scale_;
        ux3::set_fonts(fonts);
        ux3::Settings settings;
        settings.snap_always_ = model.snap_always_;
        settings.carry = model.carry;
        settings.collision = model.view.collision;
        settings.pick_filter_ = model.pick_filter_;
        settings.can_undo_ = model.can_undo_;
        settings.can_redo_ = model.can_redo_;
        ux3::note_settings(settings);
    }
    get_toolbar(model);
    status_bar(model);
    app::dock_space(imgui_setup().scale_);
    // The Scene panel: the game's frame with the lines and gizmos over it (back buffer pixels to the window's); the
    // floating tools place themselves over it.
    app::scene_panel(&overlay, {.mono = imgui_setup().mono_, .scale = imgui_setup().scale_});
    input_routing().layout_.viewport = app::scene_rect();

    get_outliner(model);
    get_inspector(model);
    get_browser(model);
    get_help(model);
    autosave_offer(model);
    get_place(model);
    confirm_hide(model);
    confirm_reset(model);
    collision_legend(model);
    online_warning(model);
    prefs::pane(imgui_setup().scale_, {.heading = imgui_setup().heading_, .small_font_ = imgui_setup().small_}); // (level_editor_prefs.h)
    prefs::tips_window(imgui_setup().scale_, imgui_setup().heading_, imgui_setup().mono_);
    play_test::pane(imgui_setup().scale_, {.heading = imgui_setup().heading_, .small_font_ = imgui_setup().small_}); // (level_editor_playtest.h)
    // (UX7: the Arrange window, the adjust-last panel - level_editor_arrange.h, level_editor_adjust.h)
    arrange::pane(imgui_setup().scale_);
    adjust::get_panel(input_routing().layout_.viewport.x, input_routing().layout_.viewport.y + input_routing().layout_.viewport.height, imgui_setup().scale_);
    triggers::pane({.x = input_routing().layout_.viewport.x, .y = input_routing().layout_.viewport.y, .width = input_routing().layout_.viewport.width, .height = input_routing().layout_.viewport.height}, {.scale = imgui_setup().scale_, .selected_node_ = model.nodes.selected});
    // (UX5: the Problems window, the selected note's window)
    checks::pane({.x = input_routing().layout_.viewport.x, .y = input_routing().layout_.viewport.y, .width = input_routing().layout_.viewport.width, .height = input_routing().layout_.viewport.height}, imgui_setup().scale_, imgui_setup().mono_);
    notes::pane({.x = input_routing().layout_.viewport.x, .y = input_routing().layout_.viewport.y, .width = input_routing().layout_.viewport.width, .height = input_routing().layout_.viewport.height}, imgui_setup().scale_);
    // (UX9: the gap wizard's and the ground tool's forms, the skateability legend - level_editor_ux9.h)
    ux9::get_windows(input_routing().layout_.viewport.x, input_routing().layout_.viewport.y, input_routing().layout_.viewport.width, input_routing().layout_.viewport.height,
                     imgui_setup().scale_);
    ux3::get_windows(model); // (the search, history, changes, keys, snapshots - level_editor_ux3.h)
    ux8::get_windows(input_routing().layout_.viewport.x, input_routing().layout_.viewport.y, input_routing().layout_.viewport.width, input_routing().layout_.viewport.height,
                     ux8_fonts()); // (UX8: the pie, the crosshair - level_editor_ux8.h)
    ux3::remember_sections();      // (the Inspector's sections folded as last time)
    shell::dialogs(ux3_fonts());   // (the editor mode's Open Level and unsaved-changes question - level_editor_shell.h)
    ImGui::PopFont();
    ImGui::Render();
    ui_backend::render();
    // (over the Scene panel the editor has the mouse, as it had the viewport - ImGui wants it over any window)
    input_routing().want_mouse_ = io.WantCaptureMouse && !app::scene_hovered();
    input_routing().want_keys_ = io.WantCaptureKeyboard;
    app::save_layout(false);
}

void play_frame(bool playing)
{
    const bool started = app::note_playing(playing);
    if (!playing) {
        if (imgui_setup().created_ && !imgui_setup().running_) {
            app::set_window_title({}); // (the window's own title back once the editor and its play are over)
        }
        return;
    }
    if (!imgui_setup().created_ || imgui_setup().running_ || !ui_backend::get_ready()) {
        return;
    }
    if (started) {
        get_log("level editor UI: playing in the Scene panel");
    }
    update_scale();
    ui_backend::new_frame();
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();
    input_routing().layout_ = ui_layout::compute({.width = io.DisplaySize.x, .height = io.DisplaySize.y}, imgui_setup().scale_, true, true);
    ImGui::PushFont(imgui_setup().ui_);
    if (ImGui::BeginMainMenuBar()) {
        // (the editor mode: File's Exit - level_editor_shell.h)
        if (shell::on() && ImGui::BeginMenu("File")) {
            shell::play_file_menu();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Play")) {
            if (ImGui::MenuItem("Stop Playing", "F5")) {
                perform::ask_return();
            }
            ImGui::Separator();
            app::play_menu();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            app::window_menu();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kBarPaddingX * imgui_setup().scale_, kBarPaddingY * imgui_setup().scale_));
    if (ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down,
                                   input_routing().layout_.status.height, kPanel | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::PushFont(imgui_setup().small_);
        ImGui::TextUnformatted("Playing");
        ImGui::SameLine();
        get_muted("F5  Back to the editor");
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    app::dock_space(imgui_setup().scale_);
    app::scene_panel(nullptr, {.mono = imgui_setup().mono_, .scale = imgui_setup().scale_});
    // (the other panels say so - or stand aside: Window > Maximise on Play)
    if (!app::maximised_on_play()) {
        for (const app::Panel panel : {app::Panel::kOutliner, app::Panel::kInspector}) {
            bool* open = app::panel_open(panel);
            if (!*open) {
                continue;
            }
            if (ImGui::Begin(panel == app::Panel::kOutliner ? "Outliner" : "Inspector", open, ImGuiWindowFlags_NoCollapse)) {
                get_muted("Playing - F5 to edit");
            }
            ImGui::End();
        }
    }
    shell::dialogs(ux3_fonts());
    ImGui::PopFont();
    ImGui::Render();
    ui_backend::render();
    app::save_layout(false);
}

bool shell_frame(ShellDraw draw)
{
    if (!is_switched_on() || imgui_setup().running_) {
        return false;
    }
    create_context();
    if (!ui_backend::get_ready() && !ui_backend::prepare_all()) {
        return false;
    }
    update_scale();
    ui_backend::new_frame();
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();
    input_routing().layout_ = ui_layout::compute({.width = io.DisplaySize.x, .height = io.DisplaySize.y}, imgui_setup().scale_, true, true);
    app::note_playing(false);
    ImGui::PushFont(imgui_setup().ui_);
    draw({.ui = imgui_setup().ui_,
          .small = imgui_setup().small_,
          .mono = imgui_setup().mono_,
          .heading = imgui_setup().heading_,
          .scale = imgui_setup().scale_});
    ImGui::PopFont();
    ImGui::Render();
    ui_backend::render();
    app::save_layout(false);
    return true;
}

void play_event(const input::Event& event)
{
    if (imgui_setup().created_ && !imgui_setup().running_ && ui_backend::get_ready()) {
        ui_backend::process_event(event.native);
    }
}

} // namespace frontend::level_editor::ui
