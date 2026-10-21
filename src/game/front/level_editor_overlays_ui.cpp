// NOT ORIGINAL - the level editor's View and Overlays menus on the toolbar (ours). See level_editor_overlays.h. Only
// ImGui's own API here.

#include <cstdio>

#include "game/front/level_editor_faces.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_triggers.h"
#include "imgui.h"

namespace Front::LevelEditor::Overlays {

namespace {

void Tip(const char* text)
{
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

} // namespace

void ViewModeItems()
{
    static const char* const kTips[kViewModes] = {
        "As in play",
        "Textures without lighting",
        "Edges only",
        "Collision only",
    };
    for (int mode = 0; mode < kViewModes; ++mode) {
        char keys[16];
        std::snprintf(keys, sizeof(keys), "Alt+%d", mode + 1);
        if (ImGui::MenuItem(ViewModeName(mode), keys, ViewMode() == mode))
            SetViewMode(mode);
        Tip(kTips[mode]);
    }
}

void OverlayItems(const Ui::Model& model, std::vector<Ui::Request>* requests)
{
    if (ImGui::MenuItem("Collision", "C", model.view.collision)) {
        Ui::Request r{Ui::Request::kCollision};
        r.value = model.view.collision ? 0 : 1;
        requests->push_back(r);
    }
    Tip("Collision near the camera, coloured by surface");
    if (ImGui::MenuItem("Trigger Volumes", "T", Triggers::Shown()))
        Triggers::SetShown(!Triggers::Shown());
    Tip("Kill zones, teleports, gaps and script triggers");
    for (const BitInfo& info : Bits()) {
        char item[64];
        std::snprintf(item, sizeof(item), "%s##ov%u", info.label, info.bit);
        if (ImGui::MenuItem(item, info.key[0] ? info.key : nullptr, Shown(info.bit)))
            Set(info.bit, !Shown(info.bit));
        Tip(info.tip);
    }
    if (ImGui::BeginMenu("Gizmos")) {
        uint32_t families = model.nodes.families;
        for (int f = 0; f < Nodes::kFamilies; ++f) {
            auto family = static_cast<Nodes::Family>(f);
            if (Nodes::ShapeOf(family) == Nodes::Shape::None)
                continue; // (scenery: its own geometry shows it)
            bool on = (families & Nodes::Bit(family)) != 0;
            char item[48];
            std::snprintf(item, sizeof(item), "%s##fam%d", Nodes::FamilyName(family), f);
            if (ImGui::MenuItem(item, nullptr, on)) {
                Ui::Request r{Ui::Request::kFamilies};
                r.id = on ? families & ~Nodes::Bit(family) : families | Nodes::Bit(family);
                requests->push_back(r);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Show All")) {
            Ui::Request r{Ui::Request::kFamilies};
            r.id = Nodes::kAllFamilies;
            requests->push_back(r);
        }
        if (ImGui::MenuItem("Hide All")) {
            Ui::Request r{Ui::Request::kFamilies};
            r.id = 0;
            requests->push_back(r);
        }
        ImGui::EndMenu();
    }
}

const std::vector<HelpRow>& HelpRows()
{
    static const std::vector<HelpRow> rows = {
        {"View", "Alt+1 / 2 / 3 / 4", "Lit / Unlit / Wireframe / Collision only"},
        {"", "Y", "Jump arcs from the selected ramp"},
        {"", "M, Shift+M", "Ruler; clear measurement (Alt: no snap)"},
        {"", "Hover a rail", "Grind preview"},
        {"Add Piece", "Grid / List", "Thumbnail or list view; star to add to Favourites"},
    };
    return rows;
}

} // namespace Front::LevelEditor::Overlays
