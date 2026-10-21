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
    Tip("Collision near the camera, colored by surface");
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
        {"View modes", "Alt+1 / 2 / 3 / 4", "Lit / Unlit (textures without their light) / Wireframe / Collision only "
                                            "(the toolbar's View menu; back to Lit when the editor closes)"},
        {"", "Overlays (toolbar)", "one menu with a checkbox for every overlay - collision (C), trigger volumes (T), "
                                   "labels, feature map, grind preview, jump arcs (Y), ruler (M), grid, carried objects, "
                                   "the collision legend, the Outliner's markers and each gizmo family - remembered"},
        {"Jump arcs", "Y", "with scenery selected (a kicker, a quarter pipe, any ramp): its lips and the flight off "
                           "them at three speeds - push (green), top (yellow), max (red) - with a full ollie; where "
                           "each lands (a cross, its distance and drop), or how high a vert lip sends you"},
        {"Grind preview", "Hover a rail", "its whole rail: direction (chevrons), points, segments, terrain, speed-ups, "
                                          "length, and the rail ends near its ends (transfers) - the selected rail's "
                                          "too"},
        {"Ruler", "M", "the ruler: click two points (on collision, snapped to a corner within 12 px - Alt: no snap); "
                       "the distance, the horizontal gap and the height difference stay shown; a third click starts "
                       "again; M again leaves the tool (the measurement stays)"},
        {"", "Shift+M", "clears the measurement"},
        {"Add palette", "B, then Grid", "thumbnails of the pieces / objects (made a few a frame); the list as before "
                                        "with List; the filter searches names"},
        {"", "Star, Recent", "a star on a thumbnail or row keeps it in Favourites; Recent: the last 20 placed - both "
                             "remembered per level"},
    };
    return rows;
}

} // namespace Front::LevelEditor::Overlays
