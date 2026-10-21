// NOT ORIGINAL - the level editor's transform tools on the toolbar (ours; see level_editor_transform.h). Dear ImGui only
// (the renderer-independence rules: level_editor_ui_backend.h); the settings are plain data the editor reads next frame.

#include <cstdio>

#include "game/front/level_editor_transform.h"
#include "imgui.h"

namespace Front::LevelEditor::Transform {

void OptionsMenuItems()
{
    Settings& settings = Current();
    if (ImGui::BeginMenu("Pivot")) {
        static const char* const kTips[3] = {"Objects: first object; scenery: its pivot; nodes: their position",
                                             "Center of the selection's bounds", "Each object about its own position"};
        int i = 0;
        for (Pivot pivot : {Pivot::Base, Pivot::Centre, Pivot::Own}) {
            if (ImGui::MenuItem(PivotName(pivot), nullptr, settings.pivot == pivot))
                settings.pivot = pivot;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", kTips[i]);
            ++i;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Rotation Snap")) {
        for (float step : AngleSteps()) {
            char label[16];
            std::snprintf(label, sizeof(label), "%g\xC2\xB0", step);
            if (ImGui::MenuItem(label, nullptr, AngleStep() == step))
                SetAngleStep(step);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Snap To")) {
        static const char* const kKeys[4] = {"Ctrl", "Ctrl+Shift", "Alt+Shift", "Ctrl+Alt"};
        int i = 0;
        for (SnapTarget target : {SnapTarget::Grid, SnapTarget::WorldGrid, SnapTarget::Surface, SnapTarget::Vertex}) {
            if (ImGui::MenuItem(TargetName(target), kKeys[i], settings.target == target))
                settings.target = target;
            ++i;
        }
        ImGui::EndMenu();
    }
    ImGui::MenuItem("Align to Slope", nullptr, &settings.alignToSlope);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Tilt objects to the surface when surface snapping");
    bool always = FlyAlways();
    if (ImGui::MenuItem("WASD Always Flies", nullptr, always))
        SetFlyAlways(!always);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fly without holding the right mouse button");
}
} // namespace Front::LevelEditor::Transform
