// NOT ORIGINAL - the level editor's Outliner panel: the node list by family, its rows, marks and chips (ours). See
// level_editor_ui.h.

#include "base/core/enum_ops.h"
#include "editor/level_editor_ui.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <array>
#include <vector>
#include "editor/level_editor_notes.h"
#include "base/core/format.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_ux3.h"
#include "imgui.h"
#include "base/core/checked_index.h"
#include "editor/level_editor_gui_text.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor_ui_internal.h"

namespace frontend::level_editor::ui {
// What the source file's anonymous namespace held that its pieces share (level_editor_ui_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor::ui

namespace frontend::level_editor::ui {
namespace {

// NOT ORIGINAL grouping - the Outliner's filter: its search text and "Changed only" (were in the UI's record; only this
// piece uses them). Made on first use, never destroyed (outliner_filter()).
struct OutlinerFilter {
    std::array<char, kSearchTextSize> outliner_search_{};
    bool changed_only_{};
};

OutlinerFilter& outliner_filter()
{
    static core::NoDestroy<OutlinerFilter> filter;
    return filter.get();
}

constexpr std::size_t kRowTagSize = 48; // an Outliner row's tag
constexpr std::size_t kFamilyHeaderSize = 64; // "<family>###family<n>"

constexpr float kChipPaddingX = 5.0F; // the family chips
constexpr float kChipPaddingY = 2.0F;
constexpr float kChipSpacing = 4.0F;
constexpr float kChipOnAlpha = 0.75F; // the accent's alpha on a chip that is on
constexpr float kRowSwatchTop = 0.3F; // an Outliner row's colour swatch, as fractions of the line height
constexpr float kRowSwatchBottom = 0.7F;
constexpr float kRowSwatchWidth = 0.4F;
constexpr float kRowSwatchTextOffset = 0.6F; // where the row's text starts

constexpr float kMarksGap = 6.0F;          // before an Outliner row's mark letters
constexpr float kScrollRowFraction = 0.4F; // the selected row scrolls to this fraction of the list's height

constexpr ImVec4 kChipOffColour{0.2F, 0.21F, 0.225F, 1.0F};
constexpr ImVec4 kMutedTextColour{0.6F, 0.61F, 0.63F, 1.0F};

// The Outliner's rows: each node's label (made again when the node arrays change) and the families' node lists.
struct OutlinerCache {
    uint32_t generation = 0;
    std::vector<std::string> labels;
    std::array<std::vector<int>, nodes::kFamilies> by_family_;
};
// NOT ORIGINAL grouping - the UI's cache and the last selection (were globals). Made on first use, never destroyed
// (ui_cache()).
struct UiCache {
    OutlinerCache cache_{};
    int last_selected_ = -1;
};

UiCache& ui_cache()
{
    static core::NoDestroy<UiCache> state;
    return state.get();
}

void update_cache()
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (ui_cache().cache_.generation == nodes::get_generation() && ui_cache().cache_.labels.size() == nodes.size()) {
        return;
    }
    ui_cache().cache_.generation = nodes::get_generation();
    ui_cache().cache_.labels.clear();
    for (std::vector<int>& list : ui_cache().cache_.by_family_) {
        list.clear();
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        ui_cache().cache_.labels.push_back((core::at(nodes, i).name != 0U) ? nodes::caption(core::at(nodes, i).name)
                                                                           : "(no name)");
        core::at(ui_cache().cache_.by_family_, static_cast<int>(core::at(nodes, i).family)).push_back(static_cast<int>(i));
    }
}

// What a row shows after the name: a spawn's types, a crown's index, a flag's colour, a rail's links.
std::string row_tag_of(const nodes::Elem& node);
std::string row_tag(const nodes::Elem& node)
{
    std::string tag = row_tag_of(node);
    if (node.hidden) {
        return tag.empty() ? "hidden" : tag + " hidden";
    }
    if (node.added) {
        return tag.empty() ? "added" : tag + " added";
    }
    return tag;
}
std::string row_tag_of(const nodes::Elem& node)
{
    std::array<char, kRowTagSize> text = {""};
    switch (node.family) {
    case nodes::Family::kSpawn: {
        std::string tags;
        auto add = [&](nodes::SpawnPointKind bit, const char* tag) {
            if (core::any(node.restart_types_ & bit)) {
                tags += tags.empty() ? tag : std::string(" ") + tag;
            }
        };
        add(nodes::SpawnPointKind::kPlayer1, "P1");
        add(nodes::SpawnPointKind::kPlayer2, "P2");
        add(nodes::SpawnPointKind::kMultiplayer, "MP");
        add(nodes::SpawnPointKind::kHorse, "H");
        add(nodes::SpawnPointKind::kCtf, "CTF");
        add(nodes::SpawnPointKind::kTeam, "Team");
        add(nodes::SpawnPointKind::kKing, "KotH");
        return tags;
    }
    case nodes::Family::kCrown:
        if (node.order < 0) {
            return "";
        }
        text::format_to(text, "#%d", node.order);
        return text.data();
    case nodes::Family::kRail:
    case nodes::Family::kRouteWay:
    case nodes::Family::kLadder:
        if (node.links.empty()) {
            return "end";
        }
        return node.links.size() == 1 ? "" : std::to_string(node.links.size()) + " links";
    default:
        return "";
    }
}

void family_chips(const ShapeMesh& model)
{
    ImGui::PushFont(imgui_setup().small_);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kChipPaddingX * imgui_setup().scale_, kChipPaddingY * imgui_setup().scale_));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kChipSpacing * imgui_setup().scale_, kChipSpacing * imgui_setup().scale_));
    const auto& counts = nodes::get_counts();
    const float right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    for (int family_index = 0; family_index < nodes::kFamilies; ++family_index) {
        auto family = static_cast<nodes::Family>(family_index);
        if (core::at(counts, family_index) == 0) {
            continue;
        }
        const bool on = (model.nodes.families & nodes::get_bit(family)) != 0;
        const char* name = nodes::family_name(family);
        const float width = ImGui::CalcTextSize(name).x + (2.0F * ImGui::GetStyle().FramePadding.x);
        if (ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x + 1.0F) {
            ImGui::SameLine();
            if (ImGui::GetCursorPosX() + width > right) {
                ImGui::NewLine();
            }
        }
        ImGui::PushStyleColor(ImGuiCol_Button, on ? get_accent(kChipOnAlpha) : kChipOffColour);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1.0F, 1.0F, 1.0F, 1.0F) : kMutedTextColour);
        if (ImGui::SmallButton(name)) {
            get_ask(Request::Kind::kFamilies, 0, model.nodes.families ^ nodes::get_bit(family));
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            gui::tooltip("%d %s - click to %s", core::at(counts, family_index), name, on ? "hide" : "show");
        }
    }
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
}

// A row's end: its tag and its markers (what the editor has done to it); where the row's toggles end.
float row_marks(const ShapeMesh& model, const nodes::Elem& node)
{
    const std::string tag = row_tag(node);
    // (the markers - what the editor has done to it - at the row's end, the tag before them)
    auto found = model.marks.find(node.name);
    const uint8_t marks = found == model.marks.end() ? 0 : found->second;
    // (the Overlays menu's Outliner markers off: none shown - Changed only still filters by them)
    const std::string letters = overlays::get_shown(overlays::kOutlinerMarks) ? mark_letters(marks) : std::string();
    const float row_right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x -
                            ImGui::GetStyle().ScrollbarSize - ux3::toggles_width();
    const float marks_width =
        letters.empty() ? 0.0F : ImGui::CalcTextSize(letters.c_str()).x + (6.0F * imgui_setup().scale_);
    if (!tag.empty()) {
        const float width = ImGui::CalcTextSize(tag.c_str()).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), row_right - marks_width - width));
        gui::text_colored(kMutedTextColour, "%s", tag.c_str());
    }
    if (!letters.empty()) {
        ImGui::SameLine(
            std::max(ImGui::GetCursorPosX(), row_right - marks_width + (kMarksGap * imgui_setup().scale_)));
        bool first = true;
        for (int bit = 0; bit < kMaxMarkers; ++bit) {
            if ((marks & (1U << bit)) == 0U) {
                continue;
            }
            if (!first) {
                ImGui::SameLine(0.0F, 0.0F);
            }
            first = false;
            gui::text_colored(ImGui::ColorConvertU32ToFloat4(get_colour(mark_colour(bit))), "%c", mark_letter(bit));
            if (ImGui::IsItemHovered()) {
                gui::tooltip("%s", mark_name(bit));
            }
        }
    }
    return row_right;
}

// One row of the Outliner: the node's swatch, its name (and the user's label), its menu and tooltip; its tag,
// markers and toggles at the end.
void outliner_row(const ShapeMesh& model, const nodes::Elem& node, int index, bool is_selected)
{
    // (the gizmo's colour as a small swatch)
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(pos.x, pos.y + (height * kRowSwatchTop)),
        ImVec2(pos.x + (height * kRowSwatchWidth), pos.y + (height * kRowSwatchBottom)), get_colour(node.colour),
        kSwatchRounding);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + (height * kRowSwatchTextOffset), pos.y));
    if (node.hidden) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    }
    // (the user's label beside the name - level_editor_organize.h)
    const std::string user_label = ux3::row_label(node.name);
    const std::string row_text = user_label.empty()
                                     ? core::at(ui_cache().cache_.labels, index)
                                     : core::at(ui_cache().cache_.labels, index) + "  \xE2\x80\x94 " + user_label;
    const bool clicked = ImGui::Selectable(row_text.c_str(), is_selected,
                                           ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns |
                                               ImGuiSelectableFlags_AllowOverlap); // (the row's toggles)
    ux3::row_menu(node); // (right-click: label, groups, eye / lock / selectable)
    if (node.hidden) {
        ImGui::PopStyleColor();
    }
    if (clicked) {
        get_ask(Request::Kind::kSelectNode, index);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            get_ask(Request::Kind::kGoToNode, index);
        }
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Click to select, double-click to focus");
    }
    const float row_right = row_marks(model, node);
    ux3::row_toggles(node, row_right); // (the eye, the lock, selectable - level_editor_organize.h)
}

// One family's branch of the Outliner: its rows that pass the search and the Changed Only filter, opened for the
// node picked in the view (`follow`) and scrolled to it.
void outliner_family(const ShapeMesh& model, int family_index, bool follow)
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    const int selected = model.nodes.selected;
    static std::vector<int> shown;
    auto family = static_cast<nodes::Family>(family_index);
    const std::vector<int>& members = core::at(ui_cache().cache_.by_family_, family_index);
    if (members.empty() || ((model.nodes.families & nodes::get_bit(family)) == 0U)) {
        return;
    }
    shown.clear();
    for (const int member : members) {
        auto found = model.marks.find(core::at(nodes, member).name);
        if (ux3::row_passes(core::at(nodes, member), core::at(ui_cache().cache_.labels, member),
                            outliner_filter().outliner_search_.data()) &&
            mark_shown(found == model.marks.end() ? 0 : found->second, outliner_filter().changed_only_)) {
            shown.push_back(member);
        }
    }
    if (shown.empty()) {
        return;
    }
    const bool holds_selected =
        follow && std::cmp_less(selected, nodes.size()) && core::at(nodes, selected).family == family;
    if (holds_selected || (core::at(outliner_filter().outliner_search_, 0) != '\0') ||
        outliner_filter().changed_only_) {
        ImGui::SetNextItemOpen(true, holds_selected ? ImGuiCond_Always : ImGuiCond_Once);
    }
    std::array<char, kFamilyHeaderSize> header{};
    text::format_to(header, "%s###family%d", nodes::family_name(family), family_index);
    const bool open = ImGui::TreeNodeEx(header.data(), ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::SameLine();
    ImGui::PushFont(imgui_setup().mono_);
    gui::text_disabled("%d", static_cast<int>(shown.size()));
    ImGui::PopFont();
    if (!open) {
        return;
    }
    ImGui::PushFont(imgui_setup().mono_);
    // (rows the clipper skips aren't laid out; the selected one is brought into view once)
    int scroll_to = -1;
    if (holds_selected) {
        auto it = std::ranges::find(shown, selected);
        if (it != shown.end()) {
            scroll_to = static_cast<int>(it - shown.begin());
        }
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown.size()));
    if (scroll_to >= 0) {
        clipper.IncludeItemByIndex(scroll_to);
    }
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int index = core::at(shown, row);
            const nodes::Elem& node = core::at(nodes, index);
            ImGui::PushID(index);
            const bool is_selected = index == selected;
            outliner_row(model, node, index, is_selected);
            if (row == scroll_to) {
                ImGui::SetScrollHereY(kScrollRowFraction);
            }
            ImGui::PopID();
        }
    }
    ImGui::PopFont();
    ImGui::TreePop();
}

} // namespace
namespace internal {

void get_outliner(const ShapeMesh& model)
{
    if (!input_routing().layout_.outliner_shown_) {
        return;
    }
    update_cache();
    place_window(input_routing().layout_.outliner);
    ImGui::Begin("Outliner", nullptr, kPanel);
    panel_title("Outliner");
    ImGui::SetNextItemWidth(-1.0F);
    // (Select similar on a node asks for its family here - level_editor_ux3.h)
    if (const std::string asked = ux3::take_outliner_search(); !asked.empty()) {
        text::format_to(outliner_filter().outliner_search_, "%s", asked.c_str());
    }
    ImGui::InputTextWithHint("##search", "Search\xE2\x80\xA6", outliner_filter().outliner_search_.data(), sizeof(outliner_filter().outliner_search_));
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Filter by name. Prefixes: rail: spawn: object: group:<name> label: locked: hidden:");
    }
    // (only the rows the editor has changed - those with markers)
    ImGui::Checkbox("Changed Only", &outliner_filter().changed_only_);
    if (ImGui::IsItemHovered()) {
        std::string legend = "Show changed items only. Markers:";
        for (int bit = 0; bit < kMaxMarkers; ++bit) {
            legend += std::string("\n") + mark_letter(bit) + "  " + mark_name(bit);
        }
        gui::tooltip("%s", legend.c_str());
    }
    if (!model.marks.empty()) {
        ImGui::SameLine();
        gui::text_disabled("%d changed", static_cast<int>(model.marks.size()));
    }
    family_chips(model);
    ImGui::Separator();
    ImGui::BeginChild("##tree");
    ux3::bunches(); // (the user's groups - level_editor_organize.h)
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (nodes.empty()) {
        get_muted("No level loaded");
    }
    const int selected = model.nodes.selected;
    const bool follow = selected != ui_cache().last_selected_ && selected >= 0; // (picked in the view: shown here)
    ui_cache().last_selected_ = selected;
    for (int family_index = 0; family_index < nodes::kFamilies; ++family_index) {
        outliner_family(model, family_index, follow);
    }
    notes::outliner_section(imgui_setup().mono_, outliner_filter().outliner_search_.data()); // (UX5: note pins - level_editor_notes.h)
    ImGui::EndChild();
    ImGui::End();
}

} // namespace internal
} // namespace frontend::level_editor::ui
