// NOT ORIGINAL - the level editor's transform tools on the toolbar (ours; see level_editor_transform.h). Dear ImGui only
// (the renderer-independence rules: level_editor_ui_backend.h); the settings are plain data the editor reads next frame.

#include <array>
#include <cstddef>

#include "editor/level_editor_transform.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"

namespace frontend::level_editor::xform {

namespace {

constexpr std::size_t kAxisLabelSize = 16; // a field's label text

} // namespace

void options_menu_items()
{
    Settings& settings = at_hand();
    if (ImGui::BeginMenu("Pivot")) {
        static const std::array<const char*, 3> kTips = {{"Objects: first object; scenery: its pivot; nodes: their position",
                                             "Centre of the selection's bounds", "Each object about its own position"}};
        int i = 0;
        for (const Pivot pivot : {Pivot::kBase, Pivot::kCentre, Pivot::kOwn}) {
            if (ImGui::MenuItem(pivot_name(pivot), nullptr, settings.pivot == pivot))
                settings.pivot = pivot;
            if (ImGui::IsItemHovered())
                gui::tooltip("%s", core::at(kTips, i));
            ++i;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Rotation Snap")) {
        for (const float step : angle_steps()) {
            std::array<char, kAxisLabelSize> label{};
            text::format_to(label, "%g\xC2\xB0", step);
            if (ImGui::MenuItem(label.data(), nullptr, angle_step() == step))
                set_angle_step(step);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Snap To")) {
        static const std::array<const char*, 4> kKeys = {{"Ctrl", "Ctrl+Shift", "Alt+Shift", "Ctrl+Alt"}};
        int i = 0;
        for (const SnapTarget target :
             {SnapTarget::kGrid, SnapTarget::kWorldGrid, SnapTarget::kSurface, SnapTarget::kVertex}) {
            if (ImGui::MenuItem(target_name(target), core::at(kKeys, i), settings.target == target))
                settings.target = target;
            ++i;
        }
        ImGui::EndMenu();
    }
    ImGui::MenuItem("Align to Slope", nullptr, &settings.align_to_slope_);
    if (ImGui::IsItemHovered())
        gui::tooltip("Tilt objects to the surface when surface snapping");
    const bool always = fly_always();
    if (ImGui::MenuItem("WASD Always Flies", nullptr, always))
        set_fly_always(!always);
    if (ImGui::IsItemHovered())
        gui::tooltip("Fly without holding the right mouse button");
}
} // namespace frontend::level_editor::xform
