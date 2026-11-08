// NOT ORIGINAL - the level editor's status bar, help, browser, place panel and its confirmations (ours). See
// level_editor_ui.h.

#include "editor/level_editor_arrange.h"
#include "editor/level_editor_ui.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <array>
#include <vector>
#include "editor/level_editor.h"
#include "editor/level_editor_camera.h"
#include "base/core/format.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_thumbs.h"
#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_ui_strip.h"
#include "editor/level_editor_ui_theme.h"
#include "editor/level_editor_ux3.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "base/core/checked_index.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_ui_internal.h"
#include "editor/level_editor_content.h"

namespace frontend::level_editor::ui {
// What the source file's anonymous namespace held that its pieces share (level_editor_ui_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor::ui

namespace frontend::level_editor::ui {
namespace {

constexpr float kHalf = 0.5F; // half

constexpr std::size_t kStatusRightSize = 224; // the status bar's right-hand text
constexpr std::size_t kBrowserTitleSize = 64;
constexpr std::size_t kPlaceSearchSize = 64;

constexpr float kPlaceSwatchTop = 0.25F;     // the same for a Place row
constexpr float kPlaceSwatchBottom = 0.75F;
constexpr float kPlaceTextOffset = 0.8F;

constexpr float kHelpWidth = 760.0F;
constexpr float kHelpHeightFraction = 0.85F; // of the display's height
constexpr float kBrowserWidth = 360.0F;
constexpr float kBrowserMaxHeight = 760.0F;
constexpr float kBrowserBottomMargin = 16.0F;
constexpr float kBrowserTabAlpha = 0.85F; // the accent's alpha on the selected tab
constexpr float kBannerTop = 24.0F; // the autosave offer and the online warning hang this far below the viewport's top
constexpr float kBannerWidth = 460.0F;
constexpr float kPlaceWidth = 300.0F;
constexpr float kConfirmTop = 60.0F; // the reset and hide dialogs
constexpr float kConfirmResetWidth = 460.0F;
constexpr float kConfirmHideWidth = 420.0F;

constexpr ImVec2 kCentrePivot{0.5F, 0.5F};
constexpr ImVec2 kTopCentrePivot{0.5F, 0.0F};

constexpr ImVec4 kBannerTitleColour{0.18F, 0.37F, 0.6F, 1.0F};

} // namespace
namespace internal {

void status_bar(const ShapeMesh& model)
{
    // (a bar across the bottom of the window, under the dock space)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kBarPaddingX * imgui_setup().scale_, kBarPaddingY * imgui_setup().scale_));
    const bool shown = ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down,
                                                   input_routing().layout_.status.height, kPanel | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (!shown) {
        ImGui::End();
        return;
    }
    input_routing().layout_.status = {.x = ImGui::GetWindowPos().x,
                                      .y = ImGui::GetWindowPos().y,
                                      .width = ImGui::GetWindowSize().x,
                                      .height = ImGui::GetWindowSize().y};
    // Right: unsaved, the changes, the camera (elided before the hints are crowded out).
    std::array<char, kStatusRightSize> right{};
    const std::string view = view_cam::get_standing(); // (an ortho view, orbiting - level_editor_camera.h)
    text::format_to(right, "%s%d changes  \xC2\xB7  %d moved  \xC2\xB7  %.0f, %.0f, %.0f  \xC2\xB7  speed %.0f%s%s",
                    model.unsaved ? "Unsaved  \xC2\xB7  " : "", model.changes, model.moved, model.camera.x,
                    model.camera.y, model.camera.z, model.speed, view.empty() ? "" : "  \xC2\xB7  ", view.c_str());
    const ui_layout::Rect2& bar = input_routing().layout_.status;
    const float pad_x = 8.0F * imgui_setup().scale_;
    ImGui::PushFont(imgui_setup().small_);
    const float y = bar.y + std::max(0.0F, (bar.height - ImGui::GetTextLineHeight()) * 0.5F);
    ImGui::PopFont();
    // (UX8: key caps and mouse / controller glyphs - level_editor_ux8.h)
    ui_strip::status_row(model.message, model.keys, right.data(), bar.x + pad_x, y,
                         bar.width - (ui_layout::kBothSides * pad_x), ux8_fonts());
    ImGui::End();
}
void get_help(const ShapeMesh& model)
{
    if (!model.help) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * kHalf, io.DisplaySize.y * kHalf), ImGuiCond_Always,
                            kCentrePivot);
    ImGui::SetNextWindowSize(ImVec2(kHelpWidth * imgui_setup().scale_, io.DisplaySize.y * kHelpHeightFraction));
    bool open = true;
    ImGui::Begin("Keyboard Shortcuts", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoDocking);
    // (the first-run tips again, the preferences - level_editor_prefs.h)
    if (ImGui::SmallButton("Show Tips")) {
        prefs::show_tips();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Preferences")) {
        prefs::unseal() = true;
    }
    // (every action from the registry, with its keys as bound now - level_editor_actions.h; the details below)
    ux3::help_from_registry();
    if (ImGui::CollapsingHeader("Reference###helpdetails")) {
        struct Row {
            const char* section;
            const char* keys;
            const char* what;
        };
        static const auto kRows = std::to_array<Row>({
            {.section = "Camera", .keys = "RMB + W A S D / Q E", .what = "Fly"},
            {.section = "", .keys = "RMB + Wheel", .what = "Fly speed"},
            {.section = "", .keys = "Alt+drag", .what = "Orbit"},
            {.section = "", .keys = "Wheel", .what = "Dolly; while dragging: push / pull"},
            {.section = "", .keys = "Double-click", .what = "Go to surface"},
            {.section = "", .keys = "Numpad 7 / 1 / 3 / 5", .what = "Top / front / right view, perspective / ortho (Ctrl: opposite)"},
            {.section = "Select", .keys = "Click", .what = "Select; click again to cycle"},
            {.section = "", .keys = "Shift+click", .what = "Add / remove"},
            {.section = "", .keys = "Alt+click", .what = "Single scenery piece"},
            {.section = "", .keys = "Ctrl+click", .what = "Remove part from selection"},
            {.section = "", .keys = "Drag from empty space", .what = "Box select (right: inside, left: crossing)"},
            {.section = "", .keys = "Alt+Wheel", .what = "Cycle items under cursor"},
            {.section = "Transform", .keys = "Drag", .what = "Move (Shift: vertical)"},
            {.section = "", .keys = "Drag an arrow", .what = "Move along axis"},
            {.section = "", .keys = "Ctrl / Ctrl+Shift", .what = "Snap to grid / world grid"},
            {.section = "", .keys = "Alt+Shift / Ctrl+Alt", .what = "Snap to surface / vertex"},
            {.section = "", .keys = "X / Y / Z, digits, Enter", .what = "Constrain axis, type value, apply"},
            {.section = "", .keys = "Alt (while moving)", .what = "Don't carry attached objects"},
            {.section = "", .keys = "Esc", .what = "Cancel"},
            {.section = "Rails and Paths", .keys = "Click / Enter / RMB", .what = "Add point / finish / finish"},
            {.section = "", .keys = "Alt+click", .what = "Add point without edge snapping"},
            {.section = "Collision", .keys = "Shift+click", .what = "Select invisible wall (collision shown)"},
            {.section = "Panels", .keys = "Mouse over a panel", .what = "Panel input; keys stay with the editor unless typing"},
        });
        // (the columns' widths set, not fitted: a long key list would otherwise squeeze the explanations to a letter a
        // line - the keys wrap within theirs)
        if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("##section", ImGuiTableColumnFlags_WidthFixed, ui_layout::kHelpNameColumnWidth * imgui_setup().scale_);
            ImGui::TableSetupColumn("##keys", ImGuiTableColumnFlags_WidthFixed, ui_layout::kHelpKeysColumnWidth * imgui_setup().scale_);
            ImGui::TableSetupColumn("##what", ImGuiTableColumnFlags_WidthStretch);
            // (the view menus' and their modules' rows after the editor's own: level_editor_overlays.h)
            std::vector<Row> rows(std::begin(kRows), std::end(kRows));
            for (const overlays::HelpRow& extra : overlays::help_rows()) {
                rows.push_back({.section = extra.section, .keys = extra.keys, .what = extra.what});
            }
            for (const Row& row : rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (*row.section != '\0') {
                    ImGui::PushFont(imgui_setup().heading_);
                    ImGui::TextUnformatted(row.section);
                    ImGui::PopFont();
                }
                ImGui::TableNextColumn();
                ImGui::PushFont(imgui_setup().mono_);
                ImGui::PushTextWrapPos(0.0F);
                ImGui::PushStyleColor(ImGuiCol_Text, ui_theme::kWarningColour);
                ImGui::TextUnformatted(row.keys);
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
                ImGui::PopFont();
                ImGui::TableNextColumn();
                gui::text_wrapped("%s", row.what);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
    if (!open) {
        get_ask(Request::Kind::kToggleHelp);
    }
}

} // namespace internal
namespace {

// The browser's preview (level_editor_preview.h): the picture - a drag in it orbits, the wheel zooms - and what the
// entry is.
void browser_preview(const ShapeMesh& model)
{
    const ShapeMesh::PreviewView& preview = model.preview;
    const float width = ImGui::GetContentRegionAvail().x;
    const float side = std::min(width, 210.0F * imgui_setup().scale_);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ((width - side) * kHalf));
    ImGui::InvisibleButton("##preview", ImVec2(side, side));
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const ImVec2 point_a = ImGui::GetItemRectMin();
    const ImVec2 point_b = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (preview.texture != 0U) {
        draw->AddImage(static_cast<ImTextureID>(preview.texture), point_a, point_b);
    } else {
        draw->AddRectFilled(point_a, point_b, IM_COL32(42, 44, 48, 255));
    }
    draw->AddRect(point_a, point_b, ImGui::GetColorU32(ImGuiCol_Border));
    if (preview.texture == 0U) {
        const char* text = preview.index < 0 ? "(no selection)" : "(no preview)";
        const ImVec2 size = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2((point_a.x + point_b.x - size.x) * kHalf, (point_a.y + point_b.y - size.y) * kHalf),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    }
    const ImGuiIO& io = ImGui::GetIO();
    float dx = 0.0F;
    float dy = 0.0F;
    float wheel = 0.0F;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)) {
        dx = io.MouseDelta.x;
        dy = io.MouseDelta.y;
    }
    if (ImGui::IsItemHovered()) {
        wheel = io.MouseWheel;
    }
    if (dx != 0.0F || dy != 0.0F || wheel != 0.0F) {
        Request request{.kind = Request::Kind::kPreviewOrbit};
        core::at(request.a, 0) = dx;
        core::at(request.a, 1) = dy;
        core::at(request.a, 2) = wheel;
        input_routing().requests_.push_back(request);
    }
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive() && (preview.texture != 0U)) {
        gui::tooltip("Drag to rotate, wheel to zoom");
    }
    if (!preview.name.empty()) {
        ImGui::PushFont(imgui_setup().mono_);
        gui::text_wrapped("%s", preview.name.c_str());
        ImGui::PopFont();
        ImGui::PushFont(imgui_setup().small_);
        if (!preview.size.empty()) {
            get_muted(preview.size.c_str());
        }
        if (!preview.type.empty()) {
            get_muted(preview.type.c_str());
        }
        ImGui::PopFont();
    }
    ImGui::Separator();
}

} // namespace
namespace internal {

void get_browser(const ShapeMesh& model)
{
    // (the Content Browser shows it as its This Level folder - level_editor_content.h)
    if (!model.browser || !model.browser_items_ || content::hosts_this_level()) {
        return;
    }
    ImGui::SetNextWindowPos(
        ImVec2(input_routing().layout_.viewport.x + input_routing().layout_.viewport.width -
                   (kPanelInset * imgui_setup().scale_),
               input_routing().layout_.viewport.y + (kPanelInset * imgui_setup().scale_)),
        ImGuiCond_Always, ImVec2(1.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(kBrowserWidth * imgui_setup().scale_,
                                    std::min(kBrowserMaxHeight * imgui_setup().scale_,
                                             input_routing().layout_.viewport.height -
                                                 (kBrowserBottomMargin * imgui_setup().scale_))));
    bool open = true;
    std::array<char, kBrowserTitleSize> title{};
    text::format_to(title, "Add Piece (%d)###browser", static_cast<int>(model.browser_items_->size()));
    ImGui::Begin(title.data(), &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking);
    browser_body(model);
    ImGui::End();
    if (!open) {
        get_ask(Request::Kind::kToggleBrowser);
    }
}

void browser_body(const ShapeMesh& model)
{
    if (!model.browser || !model.browser_items_) {
        get_muted("Opening the level's pieces\xE2\x80\xA6");
        return;
    }
    browser_preview(model);
    for (int tab = 0; tab < 2; ++tab) {
        if (tab != 0) {
            ImGui::SameLine();
        }
        const bool on = model.browser_tab_ == tab;
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, get_accent(kBrowserTabAlpha));
        }
        if (ImGui::Button(tab == 0 ? "Scenery" : "Objects") && !on) {
            get_ask(Request::Kind::kBrowserTab, tab);
        }
        if (on) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##filter", "Filter\xE2\x80\xA6", panel_state().browser_search_.data(), sizeof(panel_state().browser_search_));
    const float buttons = ImGui::GetFrameHeightWithSpacing() * 3.0F; // (UX7: a row of the arrange tools')
    const std::vector<BrowserItem>& items = *model.browser_items_;
    const bool follow = model.browser_index_ != panel_state().browser_shown_index_; // (the keys moved it: keep it in view)
    panel_state().browser_shown_index_ = model.browser_index_;
    if (follow) {
        panel_state().browser_keys_won_ = true;
    }
    // The entries: a grid of thumbnails or the list, All / Recent / Favourites (level_editor_thumbs.h).
    const int hovered =
        thumbs::items_panel(model, panel_state().browser_search_.data(), follow, -buttons, imgui_setup().mono_, imgui_setup().small_, imgui_setup().scale_, &input_routing().requests_);
    // The preview follows the mouse over the list, or the keys when they moved the highlight since.
    if (hovered != panel_state().browser_hovered_) {
        panel_state().browser_hovered_ = hovered;
        if (hovered >= 0) {
            panel_state().browser_keys_won_ = false;
        }
    }
    const int preview = panel_state().browser_hovered_ >= 0 && !panel_state().browser_keys_won_ ? panel_state().browser_hovered_ : -1;
    if (preview != panel_state().preview_asked_) {
        panel_state().preview_asked_ = preview;
        get_ask(Request::Kind::kBrowserPreview, preview);
    }
    ImGui::BeginDisabled(items.empty());
    if (ImGui::Button("Place")) {
        get_ask(Request::Kind::kBrowserPlace);
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus")) {
        get_ask(Request::Kind::kBrowserFocus);
    }
    arrange::browser_extras(imgui_setup().scale_); // (UX7: Replace selection, Click places - level_editor_arrange.h)
    ImGui::EndDisabled();
    ImGui::PushFont(imgui_setup().small_);
    get_muted("Enter: place at the cursor");
    ImGui::PopFont();
}

void autosave_offer(const ShapeMesh& model)
{
    if (!model.offer || panel_state().offer_closed_) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * kHalf, input_routing().layout_.viewport.y +
                                                                 (kBannerTop * imgui_setup().scale_)),
                            ImGuiCond_Always, kTopCentrePivot);
    ImGui::Begin("Recover Autosave", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextUnformatted("An autosave is newer than the saved layout.");
    if (ImGui::Button("Restore")) {
        get_ask(Request::Kind::kRestoreAutosave);
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Restore Autosave  (Ctrl+R)");
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
        get_ask(Request::Kind::kDiscardAutosave);
    }
    ImGui::SameLine();
    if (ImGui::Button("Later")) {
        panel_state().offer_closed_ = true;
    }
    ImGui::End();
}

// A spawn, crown or flag moved in a level shared with THUGPRO: said once a run (the edit is made).
void online_warning(const ShapeMesh& model)
{
    if (!model.nodes.online_warning_) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * kHalf, input_routing().layout_.viewport.y +
                                                                 (kBannerTop * imgui_setup().scale_)),
                            ImGuiCond_Always, kTopCentrePivot);
    ImGui::SetNextWindowSize(ImVec2(kBannerWidth * imgui_setup().scale_, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kBannerTitleColour);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kBannerTitleColour);
    ImGui::Begin("Shared Level", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor(2);
    gui::text_wrapped("This level (ported from %s) is shared with THUGPRO. Changing spawns, crowns or flags makes "
                       "it differ from other players' copies. Layouts are not used online.",
                       model.nodes.shared_game_);
    ImGui::Spacing();
    if (ImGui::Button("OK")) {
        get_ask(Request::Kind::kOnlineWarningSeen);
    }
    ImGui::End();
}

} // namespace internal
namespace {

// The Place palette (N): what can be added. A click on an entry arms it - the next click in the view places it there
// (Shift+click: keep placing); its button places one where the view looks.
std::array<char, kPlaceSearchSize>& place_search()
{
    static std::array<char, kPlaceSearchSize> place_search{};
    return place_search;
}

} // namespace
namespace internal {

void get_place(const ShapeMesh& model)
{
    if (!model.place.open) {
        return;
    }
    ImGui::SetNextWindowPos(
        ImVec2(input_routing().layout_.viewport.x + (kPanelInset * imgui_setup().scale_),
               input_routing().layout_.viewport.y + (kPanelInset * imgui_setup().scale_)),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPlaceWidth * imgui_setup().scale_, 0.0F));
    bool open = true;
    ImGui::Begin("Place Node###place", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoDocking);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##placesearch", "Search\xE2\x80\xA6", place_search().data(), sizeof(place_search()));
    const std::vector<ShapeMesh::PlaceView::TableRow>& entries = model.place.entries;
    // (a list that scrolls: the level's peds, cars, sets, particles and sounds make it long - level_editor_entities.h)
    const float list_height = std::min(
        (static_cast<float>(entries.size()) * ImGui::GetTextLineHeightWithSpacing()) + 8.0F,
        std::max(160.0F * imgui_setup().scale_, input_routing().layout_.viewport.height * 0.6F));
    ImGui::BeginChild("##placelist", ImVec2(0.0F, list_height));
    for (int i = 0; std::cmp_less(i, entries.size()); ++i) {
        const ShapeMesh::PlaceView::TableRow& table_row = core::at(entries, i);
        if (!get_matches(table_row.label, place_search().data())) {
            continue;
        }
        ImGui::PushID(i);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(pos.x, pos.y + (height * kPlaceSwatchTop)),
            ImVec2(pos.x + (height * kHalf), pos.y + (height * kPlaceSwatchBottom)), get_colour(table_row.colour),
            kSwatchRounding);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + (height * kPlaceTextOffset), pos.y));
        const bool can = table_row.why.empty();
        ImGui::BeginDisabled(!can);
        if (ImGui::Selectable(table_row.label.c_str(), model.place.armed == i, ImGuiSelectableFlags_AllowDoubleClick)) {
            get_ask(Request::Kind::kPlaceArm, model.place.armed == i ? -1 : i);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                get_ask(Request::Kind::kPlaceHere, i);
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            gui::tooltip("%s", can ? "Click, then click in the view. Double-click: place at view center"
                                   : table_row.why.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::PushFont(imgui_setup().small_);
    if (model.place.armed >= 0 && std::cmp_less(model.place.armed, entries.size())) {
        gui::text_wrapped("Click to place %s. Shift+click: place more. Esc: cancel.",
                           core::at(entries, model.place.armed).label.c_str());
    } else {
        get_muted("Choose an item, then click in the view.");
    }
    ImGui::PopFont();
    ImGui::End();
    if (!open) {
        get_ask(Request::Kind::kPlaceToggle);
    }
}

// Reset level: asked first.
void confirm_reset(const ShapeMesh& model)
{
    if (!model.confirm_reset_) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * kHalf, input_routing().layout_.viewport.y +
                                                                 (kConfirmTop * imgui_setup().scale_)),
                            ImGuiCond_Always, kTopCentrePivot);
    ImGui::SetNextWindowSize(ImVec2(kConfirmResetWidth * imgui_setup().scale_, 0.0F));
    ImGui::Begin("Reset Level###confirmreset", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    gui::text_wrapped("Revert all changes to %s and delete its saved layout and autosave?",
                       model.level_title_.c_str());
    gui::text_disabled("Undo restores the changes until you leave the level.");
    ImGui::Spacing();
    if (ImGui::Button("Cancel")) {
        get_ask(Request::Kind::kResetCancelled);
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, kDangerButtonColour);
    if (ImGui::Button("Reset level")) {
        get_ask(Request::Kind::kResetConfirmed);
    }
    ImGui::PopStyleColor();
    ImGui::End();
}

// Deleting a level's node: hidden, not removed - asked first.
void confirm_hide(const ShapeMesh& model)
{
    if (!model.confirm.open) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * kHalf, input_routing().layout_.viewport.y +
                                                                 (kConfirmTop * imgui_setup().scale_)),
                            ImGuiCond_Always, kTopCentrePivot);
    ImGui::SetNextWindowSize(ImVec2(kConfirmHideWidth * imgui_setup().scale_, 0.0F));
    ImGui::Begin("Hide Original Node###confirmhide", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::PushFont(imgui_setup().mono_);
    ImGui::TextUnformatted(model.confirm.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    gui::text_wrapped("belongs to %s.", model.level.c_str());
    gui::text_wrapped("%s", model.confirm.crown ? "Original nodes are hidden, not deleted. Later crowns move up one."
                                                 : "Original nodes are hidden, not deleted. Revert shows it again.");
    ImGui::Spacing();
    if (ImGui::Button("Cancel")) {
        get_ask(Request::Kind::kHideCancelled);
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, kDangerButtonColour);
    const std::string hide = "Hide " + model.confirm.family;
    if (ImGui::Button(hide.c_str())) {
        get_ask(Request::Kind::kHideConfirmed);
    }
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace internal
} // namespace frontend::level_editor::ui
