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

void ToolbarMenus(const Ui::Model& model, float scale, std::vector<Ui::Request>* requests)
{
    // View: the mode (Alt+1..4).
    ImGui::SameLine(0.0f, 10.0f * scale);
    char label[64];
    std::snprintf(label, sizeof(label), "View: %s###viewmode", ViewModeName(ViewMode()));
    if (ViewMode() != 0)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.45f, 0.70f, 0.85f));
    if (ImGui::SmallButton(label))
        ImGui::OpenPopup("##viewmodes");
    if (ViewMode() != 0)
        ImGui::PopStyleColor();
    Tip("How the level is drawn (Alt+1 Lit, Alt+2 Unlit, Alt+3 Wireframe, Alt+4 Collision only) - while editing only");
    if (ImGui::BeginPopup("##viewmodes")) {
        static const char* const kTips[kViewModes] = {
            "As the game draws it",
            "The textures without their light: the level's baked lighting and the models' lights left out",
            "The 3D drawn as lines (the game's own render mode)",
            "Only the collision view: the level's scenery and sky not drawn",
        };
        for (int mode = 0; mode < kViewModes; ++mode) {
            char item[64], keys[16];
            std::snprintf(item, sizeof(item), "%s", ViewModeName(mode));
            std::snprintf(keys, sizeof(keys), "Alt+%d", mode + 1);
            if (ImGui::MenuItem(item, keys, ViewMode() == mode))
                SetViewMode(mode);
            Tip(kTips[mode]);
        }
        ImGui::EndPopup();
    }

    // Overlays: every overlay's checkbox.
    ImGui::SameLine(0.0f, 6.0f * scale);
    if (ImGui::SmallButton("Overlays"))
        ImGui::OpenPopup("##overlays");
    Tip("What's drawn over the view - gizmos, collision, triggers, labels, arcs, the ruler... (remembered)");
    if (ImGui::BeginPopup("##overlays")) {
        ImGui::TextDisabled("Over the view");
        bool collision = model.view.collision;
        if (ImGui::Checkbox("Collision##ov", &collision)) {
            Ui::Request r{Ui::Request::kCollision};
            r.value = collision ? 1 : 0;
            requests->push_back(r);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("C");
        Tip("The level's collision near the camera, coloured by surface; invisible walls magenta");
        bool triggers = Triggers::Shown();
        if (ImGui::Checkbox("Trigger volumes##ov", &triggers))
            Triggers::SetShown(triggers);
        ImGui::SameLine();
        ImGui::TextDisabled("T");
        Tip("Kill zones, teleports, gaps, scripts - and the Triggers window");
        for (const BitInfo& info : Bits()) {
            bool on = Shown(info.bit);
            char item[64];
            std::snprintf(item, sizeof(item), "%s##ov%u", info.label, info.bit);
            if (ImGui::Checkbox(item, &on))
                Set(info.bit, on);
            if (info.key[0]) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", info.key);
            }
            Tip(info.tip);
        }
        ImGui::Separator();
        ImGui::TextDisabled("Gizmos (the Outliner's chips)");
        uint32_t families = model.nodes.families;
        for (int f = 0; f < Nodes::kFamilies; ++f) {
            auto family = static_cast<Nodes::Family>(f);
            if (Nodes::ShapeOf(family) == Nodes::Shape::None)
                continue; // (scenery: its own geometry shows it)
            bool on = (families & Nodes::Bit(family)) != 0;
            char item[48];
            std::snprintf(item, sizeof(item), "%s##fam%d", Nodes::FamilyName(family), f);
            if (ImGui::Checkbox(item, &on)) {
                Ui::Request r{Ui::Request::kFamilies};
                r.id = on ? families | Nodes::Bit(family) : families & ~Nodes::Bit(family);
                requests->push_back(r);
            }
        }
        if (ImGui::SmallButton("All gizmos")) {
            Ui::Request r{Ui::Request::kFamilies};
            r.id = Nodes::kAllFamilies;
            requests->push_back(r);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("No gizmos")) {
            Ui::Request r{Ui::Request::kFamilies};
            r.id = 0;
            requests->push_back(r);
        }
        ImGui::EndPopup();
    }
    // Faces mode (K) stays a checkbox of its own: it's a mode of the clicks, not an overlay.
    ImGui::SameLine(0.0f, 6.0f * scale);
    bool faces = Faces::Picking();
    if (ImGui::Checkbox("Faces (K)", &faces))
        Faces::SetPicking(faces);
    Tip("Faces mode: a click takes a collision face (Shift+click adds / drops one); the Inspector sets or clears their "
        "flags\n(skatable, wallride, vert, not collidable, trigger, camera passes, not taggable) - live, saved, Home "
        "puts them back");
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
