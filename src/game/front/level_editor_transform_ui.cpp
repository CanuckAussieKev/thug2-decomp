// NOT ORIGINAL - the level editor's transform tools on the toolbar (ours; see level_editor_transform.h). Dear ImGui only
// (the renderer-independence rules: level_editor_ui_backend.h); the settings are plain data the editor reads next frame.

#include <cstdio>

#include "game/front/level_editor_transform.h"
#include "imgui.h"

namespace Front::LevelEditor::Transform {

namespace {

const ImVec4 kOn(0.82f, 0.34f, 0.11f, 0.85f);

bool ToggleButton(const char* label, bool on)
{
    if (on)
        ImGui::PushStyleColor(ImGuiCol_Button, kOn);
    bool pressed = ImGui::SmallButton(label);
    if (on)
        ImGui::PopStyleColor();
    return pressed;
}

std::string GridLabel(float grid)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%g", grid);
    return text;
}

} // namespace

void ToolbarTools(float uiScale)
{
    Settings& settings = Current();
    bool always = FlyAlways();
    static const struct {
        Mode mode;
        const char* label;
        const char* key;
    } kModes[] = {{Mode::Move, "Move", "W"}, {Mode::Rotate, "Rotate", "E"}, {Mode::Scale, "Scale", "R"}};
    for (const auto& m : kModes) {
        if (ToggleButton(m.label, settings.mode == m.mode))
            settings.mode = m.mode;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s: the gizmo's handles %s (%s%s; Space cycles the tools; Alt+G starts it without holding "
                              "the mouse)",
                              m.label,
                              m.mode == Mode::Move     ? "move it along an axis"
                              : m.mode == Mode::Rotate ? "turn it (the heading ring; scenery by quarter turns)"
                                                       : "scale it along an axis, or all three from the centre",
                              always ? "fly_always=1: no letter key" : m.key, always ? "" : " while the right button is up");
        ImGui::SameLine(0.0f, 2.0f * uiScale);
    }
    ImGui::SameLine(0.0f, 6.0f * uiScale);
    if (ImGui::SmallButton(settings.space == Space::World ? "World###space" : "Local###space"))
        settings.space = settings.space == Space::World ? Space::Local : Space::World;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("` switches: World - the gizmo along the world's axes; Local - along the selection's own (an "
                          "object's turn, a node's heading, scenery's quarter turns)");
}

void ToolbarSnap(float uiScale)
{
    Settings& settings = Current();
    ImGui::SameLine(0.0f, 4.0f * uiScale);
    ImGui::SetNextItemWidth(52.0f * uiScale);
    if (ImGui::BeginCombo("##grid", GridLabel(settings.grid).c_str())) {
        for (float grid : GridSizes()) {
            if (ImGui::Selectable(GridLabel(grid).c_str(), grid == settings.grid)) {
                settings.grid = grid;
                SaveSettings();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The grid's size in units (Ctrl while moving, or Snap)");
    ImGui::SameLine(0.0f, 4.0f * uiScale);
    if (ImGui::SmallButton("Tools..."))
        ImGui::OpenPopup("##transform_tools");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The pivot, the rotation snap, what Snap snaps to, Align to slope, the fly keys");
    if (ImGui::BeginPopup("##transform_tools")) {
        ImGui::TextDisabled("Pivot (Rotate / Scale)");
        for (Pivot pivot : {Pivot::Base, Pivot::Centre, Pivot::Own}) {
            if (ImGui::RadioButton(PivotName(pivot), settings.pivot == pivot))
                settings.pivot = pivot;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", pivot == Pivot::Base ? "As always: objects about the first one's position, scenery about its pivot\n"
                                                               "(Scale: the middle of its bottom), a node its place"
                                        : pivot == Pivot::Centre ? "About the middle of the selection's box"
                                                                 : "Each object about its own position (a scenery group keeps its pivot)");
            if (pivot != Pivot::Own)
                ImGui::SameLine();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Rotation snap (Ctrl while turning; Z / X)");
        for (size_t i = 0; i < AngleSteps().size(); ++i) {
            float step = AngleSteps()[i];
            char label[16];
            std::snprintf(label, sizeof(label), "%g\xC2\xB0", step);
            if (ImGui::RadioButton(label, settings.angleStep == step)) {
                settings.angleStep = step;
                SaveSettings();
            }
            if (i + 1 < AngleSteps().size())
                ImGui::SameLine();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Snap to (with Snap on)");
        for (SnapTarget target : {SnapTarget::Grid, SnapTarget::WorldGrid, SnapTarget::Surface, SnapTarget::Vertex}) {
            if (ImGui::RadioButton(TargetName(target), settings.target == target))
                settings.target = target;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", target == SnapTarget::Grid        ? "Whole grid steps from where it was (Ctrl)"
                                        : target == SnapTarget::WorldGrid ? "On the world's grid lines (Ctrl+Shift)"
                                        : target == SnapTarget::Surface   ? "Its base on the surface under the cursor (Alt+Shift)"
                                                                          : "Its nearest corner onto a vertex under the cursor (Ctrl+Alt)");
            if (target != SnapTarget::Vertex)
                ImGui::SameLine();
        }
        ImGui::Checkbox("Align objects to the slope (surface snapping)", &settings.alignToSlope);
        ImGui::Separator();
        bool always = FlyAlways();
        if (ImGui::Checkbox("W A S D always fly (as before; the tools on Space)", &always))
            SetFlyAlways(always);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("[editor] fly_always=1. Off (the default): W A S D / Q E fly while the right button is held;\n"
                              "without it W / E / R pick the tools and the arrow keys nudge the selection");
        ImGui::EndPopup();
    }
}

} // namespace Front::LevelEditor::Transform
