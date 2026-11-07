// NOT ORIGINAL - the level editor's Inspector panel: the selected node's fields, spawn and crown rows, the scale and
// transform fields, the lock and member lists (ours). See level_editor_ui.h.

#include "base/core/enum_ops.h"
#include "editor/level_editor_ui.h"
#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <array>
#include <vector>
#include "editor/level_editor.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_goals.h"
#include "base/core/format.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_ui_theme.h"
#include "editor/level_editor_ux3.h"
#include "imgui.h"
#include "base/core/checked_index.h"
#include "editor/level_editor_gui_text.h"
#include "base/core/checksums.h"
#include "editor/level_editor_ui_app.h"
#include "editor/level_editor_ui_internal.h"
#include "base/maths/trig.h"

namespace frontend::level_editor::ui {
// What the source file's anonymous namespace held that its pieces share (level_editor_ui_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor::ui

namespace frontend::level_editor::ui {
namespace {

// The Inspector's Scale: Uniform - one field changes all three in proportion (was in the UI's record; only this piece
// uses it).
bool& scale_locked()
{
    static bool locked = true;
    return locked;
}

constexpr std::size_t kFieldLabelSize = 64; // an Inspector field's label
constexpr std::size_t kLockTitleSize = 48;

constexpr std::uint32_t kHalfAlpha = 0x80000000; // half-transparent
constexpr std::uint32_t kColourRgbMask = 0x00FFFFFF;
constexpr float kDegreesPerRadian = 57.29578F;

constexpr float kBadgePadding = 4.0F;      // a badge's padding, rounding and the gap after it
constexpr float kBadgeRounding = 3.0F;
constexpr float kBadgeGap = 4.0F;
constexpr float kHeadingDragSpeed = 0.5F;  // degrees per pixel of the heading drag
constexpr float kColourButtonSize = 14.0F; // the Inspector's colour swatch button
constexpr float kMemberListPadding = 4.0F; // under the parts list
constexpr float kScaleFieldWidth = 110.0F; // one axis of the scale fields
constexpr float kScaleDragSpeed = 0.005F;

constexpr ImVec4 kOnlineBackgroundColour{0.18F, 0.37F, 0.6F, 0.35F};
constexpr ImVec4 kOnlineTextColour{0.56F, 0.71F, 0.92F, 1.0F};

// --- the Inspector ---

// A row of the Inspector's table: a label, then whatever follows.
void field_label(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    get_muted(label);
    ImGui::TableNextColumn();
}

void badge(const char* text, ImU32 colour)
{
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float pad = kBadgePadding * imgui_setup().scale_;
    ImGui::GetWindowDrawList()->AddRectFilled(pos,
                                              ImVec2(pos.x + size.x + (ui_layout::kBothSides * pad), pos.y + size.y),
                                              colour, kBadgeRounding * imgui_setup().scale_);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad, pos.y));
    ImGui::TextUnformatted(text);
    ImGui::SameLine(0.0F, pad + (kBadgeGap * imgui_setup().scale_));
}

// The node's heading: its name (or a separator), class and type, its array, Added / Hidden; online, a
// note for the families a level shared with THUGPRO plays online.
void node_heading(const ShapeMesh& model, const nodes::Elem& node)
{
    if (model.nodes.node_only_) {
        ImGui::PushFont(imgui_setup().heading_);
        gui::text_wrapped("%s", (node.name != 0U) ? nodes::caption(node.name).c_str() : "(no name)");
        ImGui::PopFont();
    } else {
        ImGui::Spacing();
        ImGui::SeparatorText("Node");
    }
    std::string cls = (node.cls != 0U) ? nodes::caption(node.cls) : "(no class)";
    if (node.type != 0U) {
        cls += " \xC2\xB7 " + nodes::caption(node.type);
    }
    get_muted(cls.c_str());
    ImGui::PushFont(imgui_setup().mono_);
    gui::text_disabled("node %d in %s", node.index, nodes::caption(nodes::get_array_name(node)).c_str());
    ImGui::PopFont();
    if (node.added) {
        ImGui::PushFont(imgui_setup().small_);
        badge("Added", IM_COL32(46, 122, 67, 200));
        ImGui::NewLine();
        ImGui::PopFont();
    }
    if (node.hidden) {
        ImGui::PushFont(imgui_setup().small_);
        badge("Hidden", IM_COL32(110, 110, 110, 220));
        ImGui::NewLine();
        gui::text_colored(ui_theme::kWarningColour, "%s", "Hidden in game. Revert to show.");
        ImGui::PopFont();
    }
    // Online: spawns, crowns and flags in a level shared with THUGPRO.
    if (nodes::get_online(node.family) && model.nodes.level_shared_) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kOnlineBackgroundColour);
        ImGui::BeginChild("##online", ImVec2(0.0F, 0.0F), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::PushFont(imgui_setup().heading_);
        gui::text_colored(kOnlineTextColour, "Online");
        ImGui::PopFont();
        gui::text_wrapped("Shared with THUGPRO (ported from %s). %s affect online play.", model.nodes.shared_game_,
                           nodes::family_name(node.family));
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
}

// The selected node's place: typed or dragged to scrub (Ctrl+click or a double-click types a number);
// one undo step a field.
void node_place_fields(const ShapeMesh& model)
{
    // Typed or dragged to scrub (Ctrl+click or a double-click types a number); one undo step a field.
    std::array<float, 3> pos = {{model.nodes.pos.x, model.nodes.pos.y, model.nodes.pos.z}};
    float heading = node_edit::get_heading_degrees(model.nodes.angles);
    ImGui::PushFont(imgui_setup().mono_);
    field_label("Position");
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool moved = ImGui::DragFloat3("##pos", pos.data(), 1.0F, 0.0F, 0.0F, "%.1f");
    bool done = ImGui::IsItemDeactivated();
    bool turned = false;
    if (!model.nodes.rail) {
        field_label("Heading");
        ImGui::SetNextItemWidth(-FLT_MIN);
        turned = ImGui::DragFloat("##heading", &heading, kHeadingDragSpeed, 0.0F, 0.0F, "%.1f\xC2\xB0");
        done = done || ImGui::IsItemDeactivated();
    }
    ImGui::PopFont();
    if (model.nodes.rail) {
        // A rail point: alone, or its whole rail (the points linked to it) by as much (L).
        field_label("Moves");
        bool whole = model.nodes.entire_rail_;
        std::array<char, kFieldLabelSize> label{};
        text::format_to(label, "Whole %s (%d point%s)", model.nodes.chain_noun_, model.nodes.rail_points_,
                        model.nodes.rail_points_ == 1 ? "" : "s");
        if (ImGui::Checkbox(label.data(), &whole)) {
            get_ask(Request::Kind::kReqEntireRail, whole ? 1 : 0);
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Move all points together  (L)");
        }
    }
    if (moved || turned) {
        const V3 angles = turned ? node_edit::with_heading(model.nodes.angles, heading) : model.nodes.angles;
        Request request = {.kind = Request::Kind::kNodeSet};
        core::at(request.a, 0) = core::at(pos, 0);
        core::at(request.a, 1) = core::at(pos, 1);
        core::at(request.a, 2) = core::at(pos, 2);
        core::at(request.b, 0) = angles.x;
        core::at(request.b, 1) = angles.y;
        core::at(request.b, 2) = angles.z;
        input_routing().requests_.push_back(request);
    }
    if (done) {
        get_ask(Request::Kind::kNodeSetDone);
    }
    field_label("Angles");
    get_mono("%.3f, %.3f, %.3f", model.nodes.angles.x, model.nodes.angles.y, model.nodes.angles.z);
}

// A restart's types and its order.
void spawn_rows(const nodes::Elem& node)
{
    field_label("Types");
    struct TypeName {
        nodes::SpawnPointKind bit;
        const char* name;
    };
    static constexpr auto kTypes =
        std::to_array<TypeName>({{.bit = nodes::SpawnPointKind::kPlayer1, .name = "Player1"},
                                 {.bit = nodes::SpawnPointKind::kPlayer2, .name = "Player2"},
                                 {.bit = nodes::SpawnPointKind::kMultiplayer, .name = "Multiplayer"},
                                 {.bit = nodes::SpawnPointKind::kHorse, .name = "Horse"},
                                 {.bit = nodes::SpawnPointKind::kCtf, .name = "CTF"},
                                 {.bit = nodes::SpawnPointKind::kTeam, .name = "Team"},
                                 {.bit = nodes::SpawnPointKind::kKing, .name = "KingOfTheHill"},
                                 {.bit = nodes::SpawnPointKind::kGeneric, .name = "Generic"}});
    ImGui::PushFont(imgui_setup().small_);
    bool any = false;
    for (const auto& type : kTypes) {
        if (core::any(node.restart_types_ & type.bit)) {
            badge(type.name, get_colour((node.colour & kColourRgbMask) | kHalfAlpha));
            any = true;
        }
    }
    if (!any) {
        get_muted("(none)");
    } else {
        ImGui::NewLine();
    }
    ImGui::PopFont();
    field_label("Order");
    if (node.hidden) {
        get_mono("hidden");
    } else {
        get_mono("%d of %d restarts", node.order + 1, nodes::walked(nodes::Family::kSpawn));
    }
}

// A crown's order (an added one moved up or down).
void crown_rows(const ShapeMesh& model, const nodes::Elem& node)
{
    field_label("Crown");
    if (node.hidden) {
        get_mono("hidden (%d active)", nodes::walked(nodes::Family::kCrown));
    } else {
        get_mono("%d of %d", node.order, nodes::walked(nodes::Family::kCrown));
    }
    if (model.nodes.node_only_ && !node.hidden) {
        ImGui::BeginDisabled(!model.nodes.crown_up_);
        if (ImGui::SmallButton("Move Up")) {
            get_ask(Request::Kind::kCrownMove, -1);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            gui::tooltip("%s", node.added ? "Move earlier in crown order" : "Only added crowns can be reordered");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!model.nodes.crown_down_);
        if (ImGui::SmallButton("Move Down")) {
            get_ask(Request::Kind::kCrownMove, 1);
        }
        ImGui::EndDisabled();
    }
}

// The selected node's buttons: Drop to Ground, Revert / Show, Delete / Hide.
void node_buttons(const ShapeMesh& model)
{
    ImGui::PushFont(imgui_setup().small_);
    gui::text_disabled("Applies: %s", model.nodes.when_applied_);
    if (model.nodes.moved) {
        ImGui::SameLine();
        gui::text_colored(ui_theme::kWarningColour, "Modified");
    }
    ImGui::PopFont();
    if (ImGui::Button("Drop to Ground")) {
        get_ask(Request::Kind::kDrop);
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Drop to Ground  (G)");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!model.nodes.moved && !model.nodes.hidden);
    if (ImGui::Button(model.nodes.hidden ? "Show" : "Revert")) {
        get_ask(Request::Kind::kReset);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (model.nodes.added || (model.nodes.can_hide_ && !model.nodes.hidden)) {
        ImGui::PushStyleColor(ImGuiCol_Button, kDangerButtonColour);
        if (ImGui::Button(model.nodes.added ? "Delete" : "Hide")) {
            get_ask(Request::Kind::kRemoveNodeAction);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            gui::tooltip("%s", model.nodes.added ? "Delete  (Del)" : "Hide original node  (Del)");
        }
        ImGui::SameLine();
    } else if (!model.nodes.added && !model.nodes.hidden && !model.nodes.why_no_hide_.empty()) {
        ImGui::BeginDisabled(true);
        ImGui::Button("Hide");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            gui::tooltip("%s", model.nodes.why_no_hide_.c_str());
        }
        ImGui::SameLine();
    }
}

void node_section(const ShapeMesh& model, int index)
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (index < 0 || std::cmp_greater_equal(index, nodes.size())) {
        return;
    }
    const nodes::Elem& node = core::at(nodes, index);
    node_heading(model, node);
    ImGui::Spacing();
    const bool editable = model.nodes.node_only_ && model.nodes.movable;
    if (ImGui::BeginTable("##node", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
        if (editable) {
            node_place_fields(model);
        } else if (node.has_pos_) {
            field_label("Position");
            get_mono("x %10.1f\ny %10.1f\nz %10.1f", node.pos.x, node.pos.y, node.pos.z);
            field_label("Angles");
            get_mono("%.3f, %.3f, %.3f", node.angles.x, node.angles.y, node.angles.z);
            if (nodes::shape_of(node.family) == nodes::Shape::kPoint) {
                field_label("Heading");
                get_mono("%.1f\xC2\xB0", maths::runtime32::atan2(node.forward.x, node.forward.z) * kDegreesPerRadian);
            }
        }
        if (node.family == nodes::Family::kSpawn) {
            spawn_rows(node);
        }
        if (node.family == nodes::Family::kCrown) {
            crown_rows(model, node);
        }
        if (node.family == nodes::Family::kLight) {
            field_label("Colour");
            const ImVec4 corner = ImGui::ColorConvertU32ToFloat4(get_colour(node.colour));
            ImGui::ColorButton("##colour", corner, ImGuiColorEditFlags_NoTooltip,
                               ImVec2(kColourButtonSize * imgui_setup().scale_,
                                      kColourButtonSize * imgui_setup().scale_));
            ImGui::SameLine();
            get_mono("%u %u %u", (node.colour >> kRedShift) & kChannelMask, (node.colour >> kGreenShift) & kChannelMask, node.colour & kChannelMask);
            field_label("Radius");
            get_mono("%.0f inner, %.0f outer", node.inner_radius_, node.radius);
        }
        if (node.family == nodes::Family::kFireOff && node.radius > 0.0F) {
            field_label("Radius");
            get_mono("%.0f", node.radius);
        }
        if (node.terrain != 0U) {
            field_label("Terrain");
            get_mono("%s", nodes::caption(node.terrain).c_str());
        }
        if (!node.links.empty()) {
            field_label("Links");
            ImGui::PushFont(imgui_setup().mono_);
            for (const int link : node.links) {
                ImGui::PushID(link);
                if (ImGui::SmallButton(nodes::caption(core::at(nodes, link).name).c_str())) {
                    get_ask(Request::Kind::kSelectNode, link);
                }
                ImGui::PopID();
            }
            ImGui::PopFont();
        }
        ImGui::EndTable();
    }
    if (editable) {
        node_buttons(model);
    }
    // Phase 4: a rail's / path's operations, a rail's terrain, a ped's / car's path (level_editor_chains_ui.cpp).
    if ((model.nodes.node_only_ || model.chain.object) && model.chain.show) {
        ImGui::NewLine();
        chain_section(model.chain, &input_routing().requests_);
    }
    play_test::restart_section(index); // (a restart's Play from here - level_editor_playtest.h)
    spawn_section(index, &input_routing().requests_); // (a HORSE spot - the feature map: level_editor_spots_ui.cpp)
    if (ImGui::Button("Focus")) {
        get_ask(Request::Kind::kGoToNode, index);
    }
    // Every field of its structure, typed and editable (phase 6: level_editor_fields_ui.cpp).
    fields_section(index, imgui_setup().mono_, imgui_setup().small_, &input_routing().requests_);
}

// The locks (level_editor_picking.h): each with Unlock.
void lock_list(const ShapeMesh& model)
{
    if (model.locks.empty()) {
        return;
    }
    ImGui::Spacing();
    ImGui::Separator();
    std::array<char, kLockTitleSize> title{};
    text::format_to(title, "Locked (%d)###locks", static_cast<int>(model.locks.size()));
    if (!ImGui::CollapsingHeader(title.data(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    for (size_t i = 0; i < model.locks.size(); ++i) {
        const ShapeMesh::LockView& lock = core::at(model.locks, i);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("Unlock")) {
            get_ask(Request::Kind::kUnlock, static_cast<int>(lock.kind), lock.id);
        }
        ImGui::SameLine();
        ImGui::PushFont(imgui_setup().mono_);
        ImGui::TextUnformatted(lock.label.c_str());
        ImGui::PopFont();
        ImGui::PopID();
    }
}

// The scenery selection's parts (its pieces, shadows marked, and the rail points carried along): each with x (removed
// from the selection, as Ctrl+click) and Keep only this.
void member_list(const std::vector<ShapeMesh::Selection::Member>& members)
{
    if (members.empty()) {
        return;
    }
    ImGui::Spacing();
    get_muted("Parts");
    const float rows = static_cast<float>(std::min<size_t>(members.size(), 10));
    ImGui::BeginChild("##members",
                      ImVec2(0.0F, (rows * ImGui::GetFrameHeightWithSpacing()) +
                                       (kMemberListPadding * imgui_setup().scale_)),
                      ImGuiChildFlags_None);
    for (size_t i = 0; i < members.size(); ++i) {
        const ShapeMesh::Selection::Member& member = core::at(members, i);
        ImGui::PushID(static_cast<int>(i));
        const int rail = member.rail ? 1 : 0;
        if (ImGui::SmallButton("x")) {
            get_ask(Request::Kind::kMemberRemove, rail, member.id);
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Remove from selection");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Only")) {
            get_ask(Request::Kind::kMemberKeep, rail, member.id);
        }
        ImGui::SameLine();
        ImGui::PushFont(imgui_setup().mono_);
        ImGui::TextUnformatted(member.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        if (member.rail) {
            get_muted("rail point");
        } else if (member.shadow) {
            get_muted(("shadow  " + member.size).c_str());
        } else {
            get_muted(member.size.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void scale_fields(const ShapeMesh::Selection& selection)
{
    // The scale along its own axes (level_editor_scale.h): X / Y / Z dragged to scrub, Ctrl+click to type;
    // one undo step a field. Locked (the default): all three keep their proportions.
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    get_muted("Scale");
    ImGui::TableNextColumn();
    const std::array<float, 3> values = {{selection.scale.x, selection.scale.y, selection.scale.z}};
    static const std::array<const char*, 3> kIds = {{"##scalex", "##scaley", "##scalez"}};
    static const std::array<const char*, 3> kFormats = {{"x %.3f", "y %.3f", "z %.3f"}};
    ImGui::PushFont(imgui_setup().mono_);
    for (int axis = 0; axis < 3; ++axis) {
        ImGui::SetNextItemWidth(kScaleFieldWidth * imgui_setup().scale_);
        float value = core::at(values, axis);
        if (ImGui::DragFloat(core::at(kIds, axis), &value, kScaleDragSpeed, scale_tool::kMin, scale_tool::kMax,
                             core::at(kFormats, axis), ImGuiSliderFlags_AlwaysClamp)) {
            Request request = {.kind = Request::Kind::kScaleSet};
            for (int component = 0; component < 3; ++component) {
                core::at(request.a, component) = core::at(values, component);
            }
            if (scale_locked() && core::at(values, axis) > 0.0F) {
                // (every axis by the same ratio, clamped so none leaves 0.25..4)
                float ratio = value / core::at(values, axis);
                for (const float limit : values) {
                    ratio = std::clamp(ratio, scale_tool::kMin / limit, scale_tool::kMax / limit);
                }
                for (int component = 0; component < 3; ++component) {
                    core::at(request.a, component) = core::at(values, component) * ratio;
                }
            } else {
                core::at(request.a, axis) = value;
            }
            input_routing().requests_.push_back(request);
        }
        if (ImGui::IsItemDeactivated()) {
            get_ask(Request::Kind::kScaleDone);
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Local %c scale (%.2f to %.0f)", core::at(kAxisLetters, axis), scale_tool::kMin,
                         scale_tool::kMax);
        }
    }
    ImGui::PopFont();
    ImGui::Checkbox("Uniform##scalelock", &scale_locked());
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Scale all axes together");
    }
}

// The node selected: its section, a trigger's, an object's, a camera's, a goal point's.
void inspect_node(const ShapeMesh& model)
{
    node_section(model, model.nodes.selected);
    triggers::inspector_section(model.nodes.selected); // (a trigger volume's settings - level_editor_triggers.h)
    entities::inspector_section(model.nodes.selected, checksums::Name{0}, imgui_setup().mono_,
                                &input_routing().requests_); // (sound, particles, ghosts)
    // (map3: a camera node's pitch, target, preview - level_editor_cameras.h)
    if (model.nodes.selected >= 0 && std::cmp_less(model.nodes.selected, nodes::get_all().size())) {
        cameras::inspector_section(core::at(nodes::get_all(), model.nodes.selected).name, imgui_setup().mono_,
                                   imgui_setup().small_);
    }
    if (model.nodes.selected >= 0 && std::cmp_less(model.nodes.selected, nodes::get_all().size())) {
        goal_edit::inspector_section(core::at(nodes::get_all(), model.nodes.selected).name,
                                     imgui_setup().mono_,
                                     imgui_setup().small_); // (a goal's point)
    }
}

// The selection's place, heading, scale and count.
void selection_transform(const ShapeMesh::Selection& selection)
{
    if (ImGui::BeginTable("##transform", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        get_muted("Position");
        ImGui::TableNextColumn();
        get_mono("x %10.1f\ny %10.1f\nz %10.1f", selection.pos.x, selection.pos.y, selection.pos.z);
        if (selection.has_heading_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            get_muted("Heading");
            ImGui::TableNextColumn();
            get_mono("%.1f\xC2\xB0", selection.heading_degrees_);
        }
        if (selection.scalable) {
            scale_fields(selection);
        }
        if (selection.objects > 1 || selection.sectors > 1) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            get_muted("Selected");
            ImGui::TableNextColumn();
            get_mono("%d", selection.objects > 0 ? selection.objects : selection.sectors);
        }
        ImGui::EndTable();
    }
}

// The selection's buttons: Drop to Ground, Deselect, Lock / Unlock.
void selection_buttons(const ShapeMesh& model)
{
    if (ImGui::Button("Drop to Ground")) {
        get_ask(Request::Kind::kDrop);
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Drop to Ground  (G)");
    }
    ImGui::SameLine();
    if (ImGui::Button("Deselect")) {
        get_ask(Request::Kind::kDeselect);
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Deselect  (Esc)");
    }
    if (model.selection_lockable_) {
        ImGui::SameLine();
        if (ImGui::Button(model.selection_locked_ ? "Unlock" : "Lock")) {
            get_ask(Request::Kind::kToggleLock);
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Prevent selection in the view  (L)");
        }
    }
}

} // namespace
namespace internal {

void get_inspector(const ShapeMesh& model)
{
    bool* open = app::panel_open(app::Panel::kInspector);
    if (!*open) {
        return;
    }
    // (docked: level_editor_ui_app.h)
    if (!ImGui::Begin("Inspector", open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    const ShapeMesh::Selection& selection = model.selection;
    if (model.nodes.node_only_) {
        inspect_node(model);
    } else if (selection.objects == 0 && selection.sectors == 0 && (model.view.wall == 0U)) {
        gui::text_disabled("Nothing selected");
    } else if (selection.objects > 0 || selection.sectors > 0) {
        ImGui::PushFont(imgui_setup().heading_);
        gui::text_wrapped("%s", selection.name.c_str());
        ImGui::PopFont();
        get_muted(selection.kind.c_str());
        if (selection.checksum != 0U) {
            ImGui::SameLine();
            get_mono("0x%08X", selection.checksum);
        }
        ImGui::Spacing();
        selection_transform(selection);
        member_list(selection.members);
        // (map3: the selected scenery's trick-object cluster - level_editor_clusters.h)
        if (selection.sectors > 0 && selection.objects == 0) {
            clusters::inspector_section(scenery::get_selection(), imgui_setup().mono_, imgui_setup().small_);
        }
        ImGui::Spacing();
        gui::text_wrapped("%s", model.selected.c_str());
        ImGui::Spacing();
        selection_buttons(model);
        if (selection.objects == 0 && selection.sectors == 1) {
            triggers::scenery_section(
                selection.checksum); // (breaks when hit - the feature map: level_editor_triggers_ui.cpp)
        }
        // (one object: its model, physics, path speed and preview - level_editor_entities.h)
        if (selection.objects == 1) {
            entities::inspector_section(-1, checksums::Name{selection.checksum}, imgui_setup().mono_,
                                        &input_routing().requests_);
        }
        if (model.nodes.selected >= 0) {
            node_section(model, model.nodes.selected);
        }
    }
    ux3::inspector_section(model); // (the clipboard, the shared fields of a multi-selection - level_editor_ux3.h)
    ImGui::Spacing();
    ImGui::Separator();
    get_muted("Under Cursor");
    const std::vector<nodes::Elem>& all = nodes::get_all();
    if (model.nodes.hovered >= 0 && std::cmp_less(model.nodes.hovered, all.size())) {
        const nodes::Elem& node = core::at(all, model.nodes.hovered);
        gui::text_wrapped("%s (%s)", nodes::caption(node.name).c_str(), nodes::caption(node.cls).c_str());
    } else {
        gui::text_wrapped("%s", model.hover.c_str());
    }
    view_section(model);
    entities::view_options(); // (Show uncreated objects)
    lock_list(model);
    cameras::list_section(imgui_setup().mono_, imgui_setup().small_);   // (map3: the camera nodes - level_editor_cameras.h)
    goal_edit::list_section(imgui_setup().mono_, imgui_setup().small_); // (Create-a-Goal goals of the edited copy - level_editor_goals.h)
    level_section(model.level_props_, imgui_setup().mono_, imgui_setup().small_, &input_routing().requests_); // (phase 6: level_editor_fields_ui.cpp)
    ImGui::End();
}

} // namespace internal
} // namespace frontend::level_editor::ui
