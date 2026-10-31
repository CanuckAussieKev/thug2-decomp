// NOT ORIGINAL - the level editor's transform tools on the toolbar (ours; see level_editor_transform.h). Dear ImGui only
// (the renderer-independence rules: level_editor_ui_backend.h); the settings are plain data the editor reads next frame.

#include <cstdio>

#include "editor/level_editor_transform.h"
#include "imgui.h"

namespace frontend::level_editor::xform {

void options_menu_items()
{
    Settings& settings = at_hand();
    if (ImGui::BeginMenu("Pivot")) {
        static const char* const kTips[3] = {"Objects: first object; scenery: its pivot; nodes: their position",
                                             "Centre of the selection's bounds", "Each object about its own position"};
        int i = 0;
        for (Pivot pivot : {Pivot::kBase, Pivot::kCentre, Pivot::kOwn}) {
            if (ImGui::MenuItem(pivot_name(pivot), nullptr, settings.pivot == pivot))
                settings.pivot = pivot;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", kTips[i]);
            ++i;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Rotation Snap")) {
        for (float step : angle_steps()) {
            char label[16];
            std::snprintf(label, sizeof(label), "%g\xC2\xB0", step);
            if (ImGui::MenuItem(label, nullptr, angle_step() == step))
                set_angle_step(step);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Snap To")) {
        static const char* const kKeys[4] = {"Ctrl", "Ctrl+Shift", "Alt+Shift", "Ctrl+Alt"};
        int i = 0;
        for (SnapTarget target : {SnapTarget::kGrid, SnapTarget::kWorldGrid, SnapTarget::kSurface, SnapTarget::kVertex}) {
            if (ImGui::MenuItem(target_name(target), kKeys[i], settings.target == target))
                settings.target = target;
            ++i;
        }
        ImGui::EndMenu();
    }
    ImGui::MenuItem("Align to Slope", nullptr, &settings.align_to_slope_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Tilt objects to the surface when surface snapping");
    bool always = fly_always();
    if (ImGui::MenuItem("WASD Always Flies", nullptr, always))
        set_fly_always(!always);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fly without holding the right mouse button");
}
} // namespace MenuSystem::level_editor::xform
