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
#include "editor/level_editor_entities.h"
#include "editor/level_editor_budget.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_goals.h"
#include "editor/level_editor_hints.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_notes.h"
#include "editor/level_editor_ux9.h"
#include "base/core/format.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_thumbs.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_ui_backend.h"
#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_ui_strip.h"
#include "editor/level_editor_ui_theme.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
#include "editor/level_editor_view.h"
#include "platform/run_log.h"
#include "imgui.h"
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
constexpr float kTextBackPadding = 3.0F; // the padding behind an overlay text
constexpr float kTextBackRounding = 3.0F;

// Colours.
constexpr ImVec4 kWallNameColour{1.0F, 0.35F, 0.9F, 1.0F};
constexpr ImVec4 kWallStateColour{1.0F, 0.55F, 0.3F, 1.0F};

} // namespace
namespace internal {

LevelEditorUiState& level_editor_ui_state()
{
    static core::NoDestroy<LevelEditorUiState> state;
    return state.get();
}

void get_ask(Request::Kind kind, int value, uint32_t id) { level_editor_ui_state().requests_.push_back({.kind = kind, .value = value, .id = id}); }

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
    if (scale == level_editor_ui_state().scale_ &&
        level_editor_ui_state().style_generation_ == prefs::style_generation()) {
        return;
    }
    level_editor_ui_state().scale_ = scale;
    level_editor_ui_state().style_generation_ = prefs::style_generation();
    const ui_layout::FontSizes sizes = ui_layout::font_sizes_for(scale);
    const ui_theme::Fonts fonts = ui_theme::build_fonts(scale);
    level_editor_ui_state().ui_ = fonts.ui;
    level_editor_ui_state().small_ = fonts.small;
    level_editor_ui_state().mono_ = fonts.mono;
    level_editor_ui_state().heading_ = fonts.heading;
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
    ImGui::PushFont(level_editor_ui_state().mono_);
    const ImVec2 size = ImGui::CalcTextSize(key);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float pad_x = 4.0F * level_editor_ui_state().scale_;
    const float pad_y = 1.0F * level_editor_ui_state().scale_;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(pos.x, pos.y - pad_y), ImVec2(pos.x + size.x + (ui_layout::kBothSides * pad_x), pos.y + size.y + pad_y),
        IM_COL32(60, 63, 68, 255), kKeyCapRounding * level_editor_ui_state().scale_);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad_x, pos.y));
    ImGui::TextUnformatted(key);
    ImGui::PopFont();
    ImGui::SameLine(0.0F, pad_x + (kKeyHintGap * level_editor_ui_state().scale_));
    ImGui::PushFont(level_editor_ui_state().small_);
    ImGui::TextUnformatted(what);
    ImGui::PopFont();
}

} // namespace
namespace internal {

// (UX8's drawing: the UI's fonts and scale)
ux8::Fonts ux8_fonts()
{
    ux8::Fonts fonts;
    fonts.ui = level_editor_ui_state().ui_;
    fonts.small = level_editor_ui_state().small_;
    fonts.mono = level_editor_ui_state().mono_;
    fonts.scale = level_editor_ui_state().scale_;
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
    ImGui::PushFont(level_editor_ui_state().heading_);
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
    const ui_layout::Rect2& view = level_editor_ui_state().layout_.viewport;
    ImGui::SetNextWindowPos(ImVec2(view.x + (kPanelInset * level_editor_ui_state().scale_),
                                   view.y + view.height - (kPanelInset * level_editor_ui_state().scale_)),
                            ImGuiCond_Always, ImVec2(0.0F, 1.0F));
    ImGui::SetNextWindowBgAlpha(kLegendBackgroundAlpha);
    ImGui::Begin("##collisionLegend", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PushFont(level_editor_ui_state().small_);
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
    ImGui::PushFont(level_editor_ui_state().heading_);
    gui::text_colored(kWallNameColour, "%s", model.view.wall_name_.c_str());
    ImGui::PopFont();
    get_muted("Invisible wall");
    ImGui::SameLine();
    get_mono("0x%08X", model.view.wall);
    ImGui::PushFont(level_editor_ui_state().mono_);
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
    faces::inspector_section(level_editor_ui_state().mono_, level_editor_ui_state().small_); // (map3: the collision faces selected - level_editor_faces.h)
}

} // namespace internal
namespace {

// --- the panels ---

// The menu bar over the tool strip (level_editor_ui_strip.h).
void get_toolbar(const ShapeMesh& model)
{
    place_window(level_editor_ui_state().layout_.menu);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::Begin("##menubar", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(2);
    if (ImGui::BeginMenuBar()) {
        menu_bar(model, ux8_fonts(), &level_editor_ui_state().requests_);
        ImGui::EndMenuBar();
    }
    ImGui::End();

    place_window(level_editor_ui_state().layout_.strip);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kBarPaddingX * level_editor_ui_state().scale_, kBarPaddingY * level_editor_ui_state().scale_));
    ImGui::Begin("##toolstrip", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kToolbarFramePaddingX * level_editor_ui_state().scale_, kToolbarFramePaddingY * level_editor_ui_state().scale_));
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
    const ui_layout::Rect2& strip = level_editor_ui_state().layout_.strip;
    const float pad_x = 8.0F * level_editor_ui_state().scale_;
    const float pad_y = std::max(0.0F, (strip.height - ImGui::GetFrameHeight()) * 0.5F);
    ui_strip::Output out;
    ui_strip::blit(state, {.x = strip.x + pad_x, .y = strip.y + pad_y}, strip.width - (ui_layout::kBothSides * pad_x),
                   level_editor_ui_state().scale_, &out);
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

bool underway() { return level_editor_ui_state().running_; }

int preview_pixels()
{
    const float scale = level_editor_ui_state().scale_ > 0.0F ? level_editor_ui_state().scale_ : 1.0F;
    return std::clamp(maths::round_non_negative<int>(kPreviewBasePixels * scale), kPreviewMinPixels, kPreviewMaxPixels);
}

bool kickoff()
{
    if (level_editor_ui_state().running_) {
        return true;
    }
    if (!is_switched_on()) {
        return false;
    }
    if (!level_editor_ui_state().created_) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // (nothing written to the game folder)
        io.LogFilename = nullptr;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        level_editor_ui_state().created_ = true;
    }
    if (!ui_backend::prepare_all()) {
        get_log("level editor UI: not started - the old HUD instead");
        return false;
    }
    level_editor_ui_state().running_ = true;
    level_editor_ui_state().scale_ = 0.0F; // (the fonts made for the window's size on the first frame)
    level_editor_ui_state().requests_.clear();
    level_editor_ui_state().offer_closed_ = false;
    level_editor_ui_state().ui_buttons_ = level_editor_ui_state().editor_buttons_ = 0;
    level_editor_ui_state().want_mouse_ = level_editor_ui_state().want_keys_ = false;
    level_editor_ui_state().browser_shown_index_ = -1;
    level_editor_ui_state().browser_hovered_ = level_editor_ui_state().preview_asked_ = -1;
    level_editor_ui_state().browser_keys_won_ = false;
    return true;
}

void halt()
{
    level_editor_ui_state().running_ = false;
    level_editor_ui_state().ui_buttons_ = level_editor_ui_state().editor_buttons_ = 0;
    level_editor_ui_state().want_mouse_ = level_editor_ui_state().want_keys_ = false;
}

void device_lost() { ui_backend::invalidate_device_objects(); }

bool route_event(const input::Event& event, bool editor_capture)
{
    if (!level_editor_ui_state().running_) {
        return true;
    }
    auto route = [&](float x, float y) {
        level_editor_ui_state().mouse_x_ = x;
        level_editor_ui_state().mouse_y_ = y;
        return ui_layout::decide(
            {.ui_wants_mouse_ = level_editor_ui_state().want_mouse_, .ui_wants_keyboard_ = level_editor_ui_state().want_keys_, .over_panel_ = ui_layout::on_panel(level_editor_ui_state().layout_, x, y), .editor_capture_ = editor_capture || level_editor_ui_state().editor_buttons_ != 0});
    };
    switch (event.type) {
    case input::EventType::kMouseMotion: {
        const ui_layout::Route mouse_route =
            route(static_cast<float>(event.x), static_cast<float>(event.y));
        if (mouse_route.mouse_to_ui_ || (level_editor_ui_state().ui_buttons_ != 0U)) {
            ui_backend::process_event(event.native);
        }
        return true; // (the editor keeps the position and the look's movement; mouse_to_editor says if it may pick)
    }
    case input::EventType::kMouseButtonDown: {
        const uint32_t bit = 1U << (event.button & 31);
        const ui_layout::Route mouse_route =
            route(static_cast<float>(event.x), static_cast<float>(event.y));
        if (!mouse_route.mouse_to_editor_) {
            level_editor_ui_state().ui_buttons_ |= bit;
            ui_backend::process_event(event.native);
            return false;
        }
        level_editor_ui_state().editor_buttons_ |= bit;
        if (mouse_route.mouse_to_ui_) {
            ui_backend::process_event(event.native); // (ImGui loses its focus and hover; the editor has the click)
        }
        return true;
    }
    case input::EventType::kMouseButtonUp: {
        const uint32_t bit = 1U << (event.button & 31);
        if ((level_editor_ui_state().ui_buttons_ & bit) != 0U) {
            level_editor_ui_state().ui_buttons_ &= ~bit;
            ui_backend::process_event(event.native);
            return false;
        }
        level_editor_ui_state().editor_buttons_ &= ~bit;
        ui_backend::process_event(event.native); // (ImGui's button state stays true)
        return true;
    }
    case input::EventType::kMouseWheel: {
        const ui_layout::Route mouse_route = route(level_editor_ui_state().mouse_x_, level_editor_ui_state().mouse_y_);
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
        return !level_editor_ui_state().want_keys_;
    default:
        ui_backend::process_event(event.native);
        return true;
    }
}

bool mouse_to_editor()
{
    if (!level_editor_ui_state().running_) {
        return true;
    }
    return ui_layout::decide(
               {.ui_wants_mouse_ = level_editor_ui_state().want_mouse_, .ui_wants_keyboard_ = level_editor_ui_state().want_keys_, .over_panel_ = ui_layout::on_panel(level_editor_ui_state().layout_, level_editor_ui_state().mouse_x_, level_editor_ui_state().mouse_y_), .editor_capture_ = level_editor_ui_state().editor_buttons_ != 0})
        .mouse_to_editor_;
}

bool keys_to_editor() { return !level_editor_ui_state().running_ || !level_editor_ui_state().want_keys_; }

void push_request(const Request& request) { level_editor_ui_state().requests_.push_back(request); }

std::vector<Request> take_requests()
{
    std::vector<Request> requests;
    requests.swap(level_editor_ui_state().requests_);
    return requests;
}

void tick_cycle(const ShapeMesh& model, const Overlay& overlay)
{
    if (!level_editor_ui_state().running_) {
        return;
    }
    update_scale();
    ui_backend::new_frame();
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();
    level_editor_ui_state().layout_ = ui_layout::compute({.width = io.DisplaySize.x, .height = io.DisplaySize.y}, level_editor_ui_state().scale_, true, true);
    // The lines and gizmos under the panels (back buffer pixels to the window's).
    int back_width = 0;
    int back_height = 0;
    ui_backend::back_buffer_size({.width = &back_width, .height = &back_height});
    const float sx = back_width > 0 ? io.DisplaySize.x / static_cast<float>(back_width) : 1.0F;
    const float sy = back_height > 0 ? io.DisplaySize.y / static_cast<float>(back_height) : 1.0F;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    for (const Overlay::Triangle& triangle : overlay.under) {
        background->AddTriangleFilled(ImVec2(triangle.ax * sx, triangle.ay * sy),
                                      ImVec2(triangle.bx * sx, triangle.by * sy),
                                      ImVec2(triangle.cx * sx, triangle.cy * sy), get_colour(triangle.argb));
    }
    for (const Seg& line : overlay.lines) {
        background->AddLine(ImVec2(line.ax * sx, line.ay * sy), ImVec2(line.bx * sx, line.by * sy),
                            get_colour(line.argb), line.thickness * level_editor_ui_state().scale_);
    }
    for (const Overlay::Triangle& triangle : overlay.triangles) {
        background->AddTriangleFilled(ImVec2(triangle.ax * sx, triangle.ay * sy),
                                      ImVec2(triangle.bx * sx, triangle.by * sy),
                                      ImVec2(triangle.cx * sx, triangle.cy * sy), get_colour(triangle.argb));
    }
    for (const Overlay::Circle& circle : overlay.circles) {
        if (circle.filled) {
            background->AddCircleFilled(ImVec2(circle.x * sx, circle.y * sy), circle.radius * sx,
                                        get_colour(circle.argb));
        } else {
            background->AddCircle(ImVec2(circle.x * sx, circle.y * sy), circle.radius * sx, get_colour(circle.argb), 0,
                                  circle.thickness * level_editor_ui_state().scale_);
        }
    }
    if (level_editor_ui_state().mono_) {
        for (const Overlay::OverlayText& text : overlay.texts) {
            const ImVec2 at(text.x * sx, text.y * sy);
            const ImVec2 size = level_editor_ui_state().mono_->CalcTextSizeA(level_editor_ui_state().mono_->FontSize,
                                                                             1e9F, 0.0F, text.text.c_str());
            background->AddRectFilled(ImVec2(at.x - kTextBackPadding, at.y - 1.0F),
                                      ImVec2(at.x + size.x + kTextBackPadding, at.y + size.y + 1.0F),
                                      IM_COL32(16, 17, 19, 170), kTextBackRounding);
            background->AddText(level_editor_ui_state().mono_, level_editor_ui_state().mono_->FontSize, at,
                                get_colour(text.argb), text.text.c_str());
        }
    }
    ImGui::PushFont(level_editor_ui_state().ui_);
    {
        ux3::Fonts fonts;
        fonts.ui = level_editor_ui_state().ui_;
        fonts.small = level_editor_ui_state().small_;
        fonts.mono = level_editor_ui_state().mono_;
        fonts.heading = level_editor_ui_state().heading_;
        fonts.scale = level_editor_ui_state().scale_;
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
    get_outliner(model);
    get_inspector(model);
    status_bar(model);
    get_browser(model);
    get_help(model);
    autosave_offer(model);
    get_place(model);
    confirm_hide(model);
    confirm_reset(model);
    collision_legend(model);
    online_warning(model);
    prefs::pane(level_editor_ui_state().scale_, {.heading = level_editor_ui_state().heading_, .small_font_ = level_editor_ui_state().small_}); // (level_editor_prefs.h)
    prefs::tips_window(level_editor_ui_state().scale_, level_editor_ui_state().heading_, level_editor_ui_state().mono_);
    play_test::pane(level_editor_ui_state().scale_, {.heading = level_editor_ui_state().heading_, .small_font_ = level_editor_ui_state().small_}); // (level_editor_playtest.h)
    // (UX7: the Arrange window, the adjust-last panel - level_editor_arrange.h, level_editor_adjust.h)
    arrange::pane(level_editor_ui_state().scale_);
    adjust::get_panel(level_editor_ui_state().layout_.viewport.x, level_editor_ui_state().layout_.viewport.y + level_editor_ui_state().layout_.viewport.height, level_editor_ui_state().scale_);
    triggers::pane({.x = level_editor_ui_state().layout_.viewport.x, .y = level_editor_ui_state().layout_.viewport.y, .width = level_editor_ui_state().layout_.viewport.width, .height = level_editor_ui_state().layout_.viewport.height}, {.scale = level_editor_ui_state().scale_, .selected_node_ = model.nodes.selected});
    // (UX5: the Problems window, the selected note's window)
    checks::pane({.x = level_editor_ui_state().layout_.viewport.x, .y = level_editor_ui_state().layout_.viewport.y, .width = level_editor_ui_state().layout_.viewport.width, .height = level_editor_ui_state().layout_.viewport.height}, level_editor_ui_state().scale_, level_editor_ui_state().mono_);
    notes::pane({.x = level_editor_ui_state().layout_.viewport.x, .y = level_editor_ui_state().layout_.viewport.y, .width = level_editor_ui_state().layout_.viewport.width, .height = level_editor_ui_state().layout_.viewport.height}, level_editor_ui_state().scale_);
    // (UX9: the gap wizard's and the ground tool's forms, the skateability legend - level_editor_ux9.h)
    ux9::get_windows(level_editor_ui_state().layout_.viewport.x, level_editor_ui_state().layout_.viewport.y, level_editor_ui_state().layout_.viewport.width, level_editor_ui_state().layout_.viewport.height,
                     level_editor_ui_state().scale_);
    ux3::get_windows(model); // (the search, history, changes, keys, snapshots - level_editor_ux3.h)
    ux8::get_windows(level_editor_ui_state().layout_.viewport.x, level_editor_ui_state().layout_.viewport.y, level_editor_ui_state().layout_.viewport.width, level_editor_ui_state().layout_.viewport.height,
                     ux8_fonts()); // (UX8: the pie, the crosshair - level_editor_ux8.h)
    ux3::remember_sections();      // (the Inspector's sections folded as last time)
    ImGui::PopFont();
    ImGui::Render();
    ui_backend::render();
    level_editor_ui_state().want_mouse_ = io.WantCaptureMouse;
    level_editor_ui_state().want_keys_ = io.WantCaptureKeyboard;
}

} // namespace frontend::level_editor::ui
