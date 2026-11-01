// NOT ORIGINAL - the level editor's View and Overlays menus on the toolbar (ours). See level_editor_overlays.h. Only
// ImGui's own API here.

#include <cstdio>

#include "editor/level_editor_faces.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_triggers.h"
#include "imgui.h"

namespace frontend::level_editor::overlays {

namespace {

void get_tip(const char* text)
{
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

} // namespace

void view_mode_items()
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
        if (ImGui::MenuItem(view_mode_name(mode), keys, view_mode() == mode))
            change_view_mode(mode);
        get_tip(kTips[mode]);
    }
}

void overlay_items(const ui::ShapeMesh& model, std::vector<ui::Request>* requests)
{
    if (ImGui::MenuItem("Collision", "C", model.view.collision)) {
        ui::Request r{ui::Request::Kind::kCollision};
        r.value = model.view.collision ? 0 : 1;
        requests->push_back(r);
    }
    get_tip("Collision near the camera, coloured by surface");
    if (ImGui::MenuItem("Trigger Volumes", "T", triggers::get_shown()))
        triggers::set_shown(!triggers::get_shown());
    get_tip("Kill zones, teleports, gaps and script triggers");
    for (const BitInfo& info : get_bits()) {
        char item[64];
        std::snprintf(item, sizeof(item), "%s##ov%u", info.label, info.bit);
        if (ImGui::MenuItem(item, info.key[0] ? info.key : nullptr, get_shown(info.bit)))
            assign_value(info.bit, !get_shown(info.bit));
        get_tip(info.tip);
    }
    if (ImGui::BeginMenu("Gizmos")) {
        const uint32_t families = model.nodes.families;
        for (int f = 0; f < nodes::kFamilies; ++f) {
            auto family = static_cast<nodes::Family>(f);
            if (nodes::shape_of(family) == nodes::Shape::kNone)
                continue; // (scenery: its own geometry shows it)
            const bool on = (families & nodes::get_bit(family)) != 0;
            char item[48];
            std::snprintf(item, sizeof(item), "%s##fam%d", nodes::family_name(family), f);
            if (ImGui::MenuItem(item, nullptr, on)) {
                ui::Request r{ui::Request::Kind::kFamilies};
                r.id = on ? families & ~nodes::get_bit(family) : families | nodes::get_bit(family);
                requests->push_back(r);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Show All")) {
            ui::Request r{ui::Request::Kind::kFamilies};
            r.id = nodes::kAllFamilies;
            requests->push_back(r);
        }
        if (ImGui::MenuItem("Hide All")) {
            ui::Request r{ui::Request::Kind::kFamilies};
            r.id = 0;
            requests->push_back(r);
        }
        ImGui::EndMenu();
    }
}

const std::vector<HelpRow>& help_rows()
{
    static const std::vector<HelpRow> kRows = {
        {"View", "Alt+1 / 2 / 3 / 4", "Lit / Unlit / Wireframe / Collision only"},
        {"", "Y", "Jump arcs from the selected ramp"},
        {"", "M, Shift+M", "Ruler; clear measurement (Alt: no snap)"},
        {"", "Hover a rail", "Grind preview"},
        {"Add Piece", "Grid / List", "Thumbnail or list view; star to add to Favourites"},
    };
    return kRows;
}

} // namespace frontend::level_editor::overlays
