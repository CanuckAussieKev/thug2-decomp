// NOT ORIGINAL - the level editor's arrange tools and adjust-last panel on the UI (ours; see level_editor_arrange.h and
// level_editor_adjust.h). Dear ImGui only (the renderer-independence rules: level_editor_ui_backend.h); what the
// buttons ask for is queued and done in the editor's next Update (Arrange::tick_frame runs the queue).

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <array>

#include "editor/level_editor_adjust.h"
#include "editor/level_editor_arrange.h"
#include "editor/level_editor_links.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor {

namespace {

constexpr float kPanelWidth = 330.0F; // the Arrange panel (before the UI scale)
constexpr float kPanelPosX = 420.0F;  // its first position
constexpr float kPanelPosY = 120.0F;
constexpr float kGroupSpacing = 14.0F; // the gap between two groups of buttons on a row
constexpr std::size_t kLabelTextSize = 32;
constexpr float kTipOffset = 8.0F; // the preview tip sits this far from the cursor
constexpr float kTipWidth = 280.0F;
constexpr std::size_t kTitleTextSize = 64;
constexpr float kOffsetLimit = 100000.0F; // the offset and position fields' range (plus and minus)
constexpr float kQuarterDragSpeed = 3.0F; // degrees a drag moves the angle fields with quarter turns on ...
constexpr float kFreeDragSpeed = 0.5F;    // ... and with free angles
constexpr float kSpinLimit = 3600.0F;     // the Angle field's range (plus and minus)
constexpr float kTurnLimit = 360.0F;      // the Turn fields' range (plus and minus)
constexpr float kFactorDragSpeed = 0.01F;
constexpr float kFactorMin = 0.05F;
constexpr float kFactorMax = 20.0F;
constexpr float kCountDragSpeed = 0.2F;

} // namespace

} // namespace frontend::level_editor

namespace frontend::level_editor::arrange {

namespace {


std::vector<std::function<void()>>& stored_queue()
{
    static core::NoDestroy<std::vector<std::function<void()>>> queue;
    return queue.get();
}
void get_later(std::function<void()> action) { stored_queue().push_back(std::move(action)); }

void get_tip(const char* text)
{
    if (ImGui::IsItemHovered())
        gui::tooltip("%s", text);
}

} // namespace

// (called from Update: what the window asked for)
void run_queued()
{
    std::vector<std::function<void()>> queue;
    queue.swap(stored_queue());
    for (auto& action : queue)
        action();
}

void pane(float ui_scale)
{
    Settings& s = at_hand();
    if (!s.window)
        return;
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth * ui_scale, 0.0F), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(kPanelPosX * ui_scale, kPanelPosY * ui_scale), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Arrange###arrange", &s.window,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Align and Distribute");
    static const auto kAxes = std::to_array<const char*>({"X", "Y", "Z"});
    for (int axis = 0; axis < 3; ++axis) {
        if (axis)
            ImGui::SameLine();
        ImGui::RadioButton(core::at(kAxes, axis), &s.axis, axis);
    }
    ImGui::SameLine(0.0F, kGroupSpacing * ui_scale);
    ImGui::Checkbox("Local", &s.local);
    get_tip("Use the first item's axes");
    int reference = static_cast<int>(s.reference);
    ImGui::RadioButton("Selection Bounds", &reference, 0);
    ImGui::SameLine();
    ImGui::RadioButton("First Selected", &reference, 1);
    s.reference = static_cast<Reference>(reference);
    static const auto kEdges = std::to_array<const char*>({"Min", "Centre", "Max"});
    for (int edge = 0; edge < 3; ++edge) {
        if (edge)
            ImGui::SameLine();
        std::array<char, kLabelTextSize> label{};
        text::format_to(label, "Align %s", core::at(kEdges, edge));
        if (ImGui::Button(label.data())) {
            const int axis = s.axis;
            get_later([axis, edge] { justify(axis, static_cast<Edge>(edge), at_hand().reference, at_hand().local); });
        }
    }
    if (ImGui::Button("Distribute")) {
        const int axis = s.axis;
        get_later([axis] { distribute(axis, at_hand().gaps, at_hand().local); });
    }
    get_tip("Space 3 or more evenly; ends stay");
    ImGui::SameLine();
    ImGui::Checkbox("Equal gaps", &s.gaps);
    get_tip("Equal gaps instead of equal centre spacing");

    ImGui::SeparatorText("Replace");
    if (ImGui::Button("Replace Selection"))
        get_later([] { get_replace(); });
    get_tip("Replace with the item highlighted in Add Piece (B)");

    ImGui::SeparatorText("Linked Copies");
    if (ImGui::Button("Duplicate Linked"))
        get_later([] { links::linked_duplicate(); });
    ImGui::SameLine();
    if (ImGui::Button("Unlink"))
        get_later([] { links::unlink_selection(); });
    const std::string linked = links::describe();
    gui::text_disabled("%s", linked.empty() ? "(not linked)" : linked.c_str());

    ImGui::SeparatorText("Rail Along Edge");
    bool picking = edge_picking();
    if (ImGui::Checkbox("Pick Edge", &picking))
        get_later([picking] { set_edge_picking(picking); });
    get_tip("Click a top edge of the selected scenery to add a rail  (Alt+R)");

    ImGui::SeparatorText("Options");
    ImGui::Checkbox("Edge Snapping", &s.edge_snap_);
    get_tip("Snap scenery flush to neighbours");
    ImGui::Checkbox("Drag Measurements", &s.measure);
    ImGui::Checkbox("Placement Preview", &s.ghost);
    ImGui::Checkbox("Click to Place", &s.click_places_);
    ImGui::Checkbox("Align to Slope", &s.ghost_slope_);
    ImGui::End();
}

void browser_extras(float ui_scale)
{
    (void)ui_scale;
    if (ImGui::SmallButton("Replace Selection"))
        get_later([] { get_replace(); });
    get_tip("Replace the selection with this item");
    ImGui::SameLine();
    ImGui::Checkbox("Click to Place", &at_hand().click_places_);
    get_tip("Click in the view to place (Shift: more)");
}

} // namespace frontend::level_editor::arrange

namespace frontend::level_editor::adjust {

void get_panel(float x, float y, float ui_scale)
{
    const ActStep& last = final_one();
    if (!get_shown() || last.kind == Kind::kNone)
        return;
    static ActStep edit;
    static bool editing;
    if (!editing)
        edit = last;
    const bool adjustable = get_adjustable();
    ImGui::SetNextWindowPos(ImVec2(x + (kTipOffset * ui_scale), y - (kTipOffset * ui_scale)), ImGuiCond_Always,
                            ImVec2(0.0F, 1.0F));
    ImGui::SetNextWindowSize(ImVec2(kTipWidth * ui_scale, 0.0F));
    std::array<char, kTitleTextSize> title{};
    text::format_to(title, "Adjust: %s (F4)###adjust_last", get_title(last.kind));
    if (!ImGui::Begin(title.data(), nullptr,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        editing = false;
        return;
    }
    ImGui::BeginDisabled(!adjustable);
    bool live = false;
    bool done = false;
    auto track = [&](bool changed) {
        live = live || changed;
        done = done || ImGui::IsItemDeactivatedAfterEdit();
    };
    const float speed = 1.0F;
    switch (edit.kind) {
    case Kind::kShiftBy:
        track(ImGui::DragFloat3("Offset", &edit.offset.x, speed, -kOffsetLimit, kOffsetLimit, "%.1f"));
        track(ImGui::Checkbox("On the grid", &edit.grid));
        break;
    case Kind::kTurn:
        track(ImGui::DragFloat("Angle", &edit.degrees, edit.quarters ? kQuarterDragSpeed : kFreeDragSpeed, -kSpinLimit, kSpinLimit,
                               edit.quarters ? "%.0f (quarters)" : "%.1f"));
        break;
    case Kind::kScale:
        if (edit.uniform)
            track(ImGui::DragFloat("Factor", &edit.factor.x, kFactorDragSpeed, kFactorMin, kFactorMax, "%.3f"));
        else
            track(ImGui::DragFloat3("Factor", &edit.factor.x, kFactorDragSpeed, kFactorMin, kFactorMax, "%.3f"));
        break;
    case Kind::kDuplicate:
        // (copies are made only when a field is let go: dozens of copies each frame would be slow)
        ImGui::DragInt("Copies", &edit.count, kCountDragSpeed, 1, kMaxCopies);
        done = done || ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat3("Offset", &edit.offset.x, 1.0F, -kOffsetLimit, kOffsetLimit, "%.1f");
        done = done || ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Turn", &edit.degrees, edit.quarters ? kQuarterDragSpeed : kFreeDragSpeed, -kTurnLimit, kTurnLimit,
                         edit.quarters ? "%.0f (quarters)" : "%.1f");
        done = done || ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("On the grid", &edit.grid))
            done = true;
        break;
    case Kind::kJustify:
    case Kind::kDistribute: {
        static const auto kAxes = std::to_array<const char*>({"X", "Y", "Z"});
        for (int axis = 0; axis < 3; ++axis) {
            if (axis)
                ImGui::SameLine();
            track(ImGui::RadioButton(core::at(kAxes, axis), &edit.axis, axis));
        }
        if (edit.kind == Kind::kJustify) {
            static const auto kEdges = std::to_array<const char*>({"Min", "Centre", "Max"});
            for (int e = 0; e < 3; ++e) {
                if (e)
                    ImGui::SameLine();
                track(ImGui::RadioButton(core::at(kEdges, e), &edit.edge, e));
            }
            track(ImGui::RadioButton("Bounds", &edit.reference, 0));
            ImGui::SameLine();
            track(ImGui::RadioButton("First", &edit.reference, 1));
        } else {
            track(ImGui::Checkbox("Equal gaps", &edit.gaps));
        }
        track(ImGui::Checkbox("Local", &edit.local));
        break;
    }
    case Kind::kPlace:
        track(ImGui::DragFloat3("Position", &edit.pos.x, 1.0F, -kOffsetLimit, kOffsetLimit, "%.1f"));
        track(ImGui::DragFloat("Turn", &edit.degrees, edit.quarters ? kQuarterDragSpeed : kFreeDragSpeed, -kTurnLimit, kTurnLimit,
                               edit.quarters ? "%.0f (quarters)" : "%.1f"));
        break;
    case Kind::kNone:
        break;
    }
    ImGui::EndDisabled();
    editing = ImGui::IsAnyItemActive() && ImGui::IsWindowFocused();
    if (!adjustable)
        gui::text_disabled("Only the last operation can be adjusted");
    else if (edit.kind == Kind::kDuplicate)
        gui::text_disabled("Release to apply");
    ImGui::End();
    if (adjustable && (live || done))
        get_request(edit, done);
}

} // namespace frontend::level_editor::adjust
