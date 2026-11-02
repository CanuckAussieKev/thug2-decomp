// NOT ORIGINAL - the level editor's UX9 tools: the glue and the Ground tool's form (ours). See level_editor_ux9.h.

#include "editor/level_editor_ux9.h"

#include <SDL2/SDL_scancode.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
#include <array>

#include "editor/level_editor_actions.h"
#include "editor/level_editor_gap_wizard.h"
#include "editor/level_editor_ground.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_skate_view.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"

namespace frontend::level_editor::ux9 {

namespace {

int g_gap_action = -1, g_skate_action = -1, g_ground_action = -1;

// --- the Ground tool ---

enum class Stage : uint8_t { kDisengaged, kFirst, kSecond, kForm };
Stage g_stage = Stage::kDisengaged;
bool g_editing; // (the form edits a piece that is there - g_draft.name)
ground::Spec g_draft;
float g_grid = 50.0f;
bool g_have_hover;
V3 g_hover, g_first;
uint32_t g_floor; // (the piece the first corner was clicked on: the material by default)
std::string g_form_message;
std::vector<std::function<void()>> g_queue;

V3 get_snapped(V3 p) { return {ground::snap_to(p.x, g_grid), p.y, ground::snap_to(p.z, g_grid)}; }

// The collision ray: the point and the piece hit (0: none / a movable).
bool get_cast(V3 from, V3 to, V3* hit, uint32_t* sector)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {from.x, from.y, from.z, 1.0f};
    const maths::Vec4 end = {to.x, to.y, to.z, 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    const auto& p = ray->hit_.point;
    *hit = {p[0], p[1], p[2]};
    *sector = ray->hit_.object ? ray->hit_.object_checksum_ : 0;
    return true;
}

// The ground's height under a point (cast down from 300 above; `fallback` when nothing).
float get_ground_at(V3 p, float fallback)
{
    V3 hit{};
    uint32_t sector = 0;
    if (get_cast(p + V3{0.0f, 300.0f, 0.0f}, p - V3{0.0f, 3000.0f, 0.0f}, &hit, &sector) && !ground::is_piece(sector))
        return hit.y;
    return fallback;
}

void change_stage(Stage stage)
{
    g_stage = stage;
    g_have_hover = false;
    if (stage == Stage::kFirst)
        show_message("Ground Tool: click the first corner", 6.0);
}

void open_form(const ground::Spec& spec, bool editing)
{
    g_draft = spec;
    g_editing = editing;
    g_form_message.clear();
    g_stage = Stage::kForm;
}

void get_apply()
{
    std::string why;
    ground::Spec spec = g_draft;
    if (!g_editing && spec.name.empty())
        spec.name = ground::fresh_name(ground::get_all());
    if (!ground::place_in(spec, &why)) {
        g_form_message = "Not made: " + why;
        show_message(g_form_message, 8.0);
        return;
    }
    if (const ground::Spec* made = ground::get_find(spec.name))
        g_draft = *made;
    g_editing = true;
    g_form_message = "Applied";
    show_message(std::string(spec.lower ? "Lowered" : "Raised") + " ground: " + spec.name, 5.0);
}

const char* ramp_name(ground::Ramp ramp)
{
    return ramp == ground::Ramp::kKicker ? "Kicker" : ramp == ground::Ramp::kQuarter ? "Quarter pipe" : "None (wall)";
}

void ground_form(float view_x, float view_y, float view_width, float scale)
{
    if (g_stage != Stage::kForm)
        return;
    ImGui::SetNextWindowPos(ImVec2(view_x + view_width - 12.0f * scale, view_y + 60.0f * scale), ImGuiCond_Appearing,
                            ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(340.0f * scale, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    const std::string title =
        (g_editing ? "Ground piece " + g_draft.name : std::string("Raise / lower ground")) + "###groundForm";
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open)
            g_queue.emplace_back([] { change_stage(Stage::kDisengaged); });
        return;
    }
    ground::Spec& d = g_draft;
    int mode = d.lower ? 1 : 0;
    ImGui::RadioButton("Raise", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Lower", &mode, 1);
    d.lower = mode == 1;
    ImGui::DragFloat(d.lower ? "Depth" : "Height", &d.height, 1.0f, ground::kMinHeight, ground::kCeilingY, "%.0f");
    std::array<float, 4> rect = {{d.x0, d.z0, d.x1, d.z1}};
    if (ImGui::DragFloat4("x0 z0 x1 z1", rect.data(), 1.0f, 0.0f, 0.0f, "%.0f")) {
        d.x0 = core::at(rect, 0), d.z0 = core::at(rect, 1), d.x1 = core::at(rect, 2), d.z1 = core::at(rect, 3);
    }
    ImGui::DragFloat("Ground at", &d.base, 1.0f, 0.0f, 0.0f, "%.1f");
    static const std::array<const char*, 4> kSideNames = {{"-X side", "+X side", "-Z side", "+Z side"}};
    for (int side = 0; side < 4; ++side) {
        ImGui::PushID(side);
        if (ImGui::BeginCombo(core::at(kSideNames, side), ramp_name(core::at(d.ramps, side)))) {
            for (int r = 0; r < 3; ++r) {
                auto ramp = static_cast<ground::Ramp>(r);
                if (ImGui::Selectable(ramp_name(ramp), core::at(d.ramps, side) == ramp))
                    core::at(d.ramps, side) = ramp;
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    ImGui::DragFloat("Ramp length", &d.length, 1.0f, 0.0f, ground::kMaxLength, d.length > 0.0f ? "%.0f" : "auto");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("0: automatic");
    ImGui::DragFloat("Grid", &g_grid, 1.0f, 0.0f, 500.0f, g_grid > 0.0f ? "%.0f" : "off");
    ImGui::TextDisabled("Material: %s", d.material ? object_edit::caption(d.material).c_str() : "(automatic)");
    if (ImGui::Button("Use Selected Material"))
        g_queue.emplace_back([] {
            const std::vector<uint32_t>& selection = scenery::get_selection();
            std::string why;
            ground::Spec spec = g_draft;
            if (selection.empty())
                g_form_message = "Select a scenery piece to copy its material";
            else if (const char* no = ground::why_not_donor(core::at(selection, 0)))
                g_form_message = std::string("Can't use this material: ") + no;
            else if (ground::choose_material(core::at(selection, 0), &spec, &why))
                g_draft.material = spec.material, g_draft.uv = spec.uv, g_draft.colour = spec.colour,
                g_draft.terrain = spec.terrain, g_form_message.clear();
        });
    if (d.lower)
        ImGui::TextDisabled("Pits need floor pieces inside the rectangle");
    if (!g_form_message.empty())
        ImGui::TextWrapped("%s", g_form_message.c_str());
    ImGui::Separator();
    if (ImGui::Button(g_editing ? "Apply" : "Make"))
        g_queue.emplace_back([] { get_apply(); });
    if (g_editing) {
        ImGui::SameLine();
        if (ImGui::Button("Remove"))
            g_queue.emplace_back([] {
                ground::drop_out(g_draft.name);
                show_message("Removed ground piece " + g_draft.name);
                change_stage(Stage::kDisengaged);
            });
    }
    ImGui::SameLine();
    if (ImGui::Button("New Rectangle"))
        g_queue.emplace_back([] { change_stage(Stage::kFirst); });
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        g_queue.emplace_back([] { change_stage(Stage::kDisengaged); });
    // (the pieces of this level)
    if (!ground::get_all().empty() && ImGui::CollapsingHeader("Pieces in this level")) {
        for (const ground::Spec& s : ground::get_all()) {
            std::array<char, 160> row{};
            text::format_to(row, "%s  %s %.0f x %.0f, %.0f", s.name.c_str(), s.lower ? "pit" : "platform", s.x1 - s.x0,
                            s.z1 - s.z0, s.height);
            if (ImGui::Selectable(row.data(), g_editing && s.name == d.name)) {
                const std::string name = s.name;
                g_queue.emplace_back([name] {
                    if (const ground::Spec* found = ground::get_find(name)) {
                        open_form(*found, true);
                        go_to_point({(found->x0 + found->x1) * 0.5f, found->base, (found->z0 + found->z1) * 0.5f},
                                    std::max(found->x1 - found->x0, found->z1 - found->z0) * 1.5f + 300.0f);
                    }
                });
            }
        }
    }
    ImGui::End();
    if (!open)
        g_queue.emplace_back([] { change_stage(Stage::kDisengaged); });
}

void ground_hover(V3 origin, V3 unit_dir)
{
    g_have_hover = false;
    if (g_stage != Stage::kFirst && g_stage != Stage::kSecond)
        return;
    V3 hit{};
    uint32_t sector = 0;
    if (get_cast(origin, origin + unit_dir * 20000.0f, &hit, &sector)) {
        g_hover = get_snapped(hit);
        g_have_hover = true;
    }
}

bool ground_click(V3 origin, V3 unit_dir)
{
    if (g_stage != Stage::kFirst && g_stage != Stage::kSecond)
        return false;
    V3 hit{};
    uint32_t sector = 0;
    if (!get_cast(origin, origin + unit_dir * 20000.0f, &hit, &sector)) {
        show_message("Ground Tool: click on the ground");
        return true;
    }
    if (g_stage == Stage::kFirst && ground::is_piece(sector)) {
        for (const ground::Spec& s : ground::get_all()) {
            if (hit.x >= s.x0 - 1000.0f && hit.x <= s.x1 + 1000.0f && hit.z >= s.z0 - 1000.0f &&
                hit.z <= s.z1 + 1000.0f) {
                open_form(s, true);
                return true;
            }
        }
    }
    const V3 corner = get_snapped(hit);
    if (g_stage == Stage::kFirst) {
        g_first = corner;
        g_floor = ground::is_piece(sector) ? 0 : sector;
        g_stage = Stage::kSecond;
        show_message("Ground Tool: click the opposite corner", 6.0);
        return true;
    }
    ground::Spec spec;
    spec.x0 = g_first.x, spec.z0 = g_first.z, spec.x1 = corner.x, spec.z1 = corner.z;
    ground::normalise(&spec);
    const V3 centre = {(spec.x0 + spec.x1) * 0.5f, std::max(g_first.y, corner.y), (spec.z0 + spec.z1) * 0.5f};
    spec.base = get_ground_at(centre, g_first.y);
    std::string why;
    if (!ground::choose_material(g_floor, &spec, &why)) {
        show_message("Ground Tool: " + why, 8.0);
        change_stage(Stage::kDisengaged);
        return true;
    }
    open_form(spec, false);
    return true;
}

} // namespace

void prepare_all()
{
    static bool done;
    if (done)
        return;
    done = true;
    auto make = [](const char* id, const char* name, const char* category, int scancode, const char* tip) {
        actions::ActStep a;
        a.id = id;
        a.name = name;
        a.category = category;
        a.tip = tip;
        a.kind = actions::Kind::kPress;
        a.defaults = a.keys = {actions::Chord{scancode, actions::kAlt}};
        return actions::enrol(a);
    };
    g_gap_action =
        make("gap_wizard", "Gap Wizard", "Triggers", SDL_SCANCODE_J, "Create a gap from a start and end point");
    g_skate_action = make("skateability", "Skateability", "View", SDL_SCANCODE_K, "Colour surfaces by skateability");
    actions::set_checked(g_skate_action, [] { return skate_view::get_shown() ? 1 : 0; });
    g_ground_action =
        make("ground_tool", "Ground Tool", "Edit", SDL_SCANCODE_U, "Raise or lower a rectangle of ground");
    layout::Extension e;
    e.keywords = {"ground"};
    e.capture = [] { return ground::capture_records(); };
    e.apply = [](const std::vector<std::string>& lines) { ground::apply_records(lines); };
    e.reset = [] { ground::reset_all(); };
    layout::add_extension(e);
}

void kickoff()
{
    ground::kickoff();
    gap_wizard::set_armed(false);
    g_stage = Stage::kDisengaged;
}

void halt()
{
    gap_wizard::set_armed(false);
    g_stage = Stage::kDisengaged;
    skate_view::halt();
    ground::halt();
    g_queue.clear();
}

void tick_frame()
{
    if (g_gap_action >= 0 && actions::squeezed(g_gap_action)) {
        const bool on = !gap_wizard::get_armed();
        g_stage = Stage::kDisengaged;
        gap_wizard::set_armed(on);
        if (!on)
            show_message("Gap Wizard off");
    }
    if (g_skate_action >= 0 && actions::squeezed(g_skate_action)) {
        skate_view::set_shown(!skate_view::get_shown());
        show_message(skate_view::get_shown() ? "Skateability on" : "Skateability off");
    }
    if (g_ground_action >= 0 && actions::squeezed(g_ground_action)) {
        gap_wizard::set_armed(false);
        if (g_stage == Stage::kDisengaged)
            change_stage(Stage::kFirst);
        else {
            change_stage(Stage::kDisengaged);
            show_message("Ground Tool off");
        }
    }
    gap_wizard::tick_frame();
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
}

bool occupied() { return gap_wizard::get_armed() || g_stage != Stage::kDisengaged; }

int picking_tool()
{
    if (gap_wizard::get_picking())
        return 1;
    return g_stage == Stage::kFirst || g_stage == Stage::kSecond ? 2 : 0;
}

bool get_cancel()
{
    if (gap_wizard::get_cancel())
        return true;
    if (g_stage == Stage::kDisengaged)
        return false;
    change_stage(Stage::kDisengaged);
    show_message("Ground Tool off");
    return true;
}

void get_hover(V3 origin, V3 unit_dir, float x, float y, bool have_ray)
{
    if (!have_ray) {
        g_have_hover = false;
        return;
    }
    gap_wizard::get_hover(origin, unit_dir, x, y);
    ground_hover(origin, unit_dir);
}

bool get_click(V3 origin, V3 unit_dir, float x, float y)
{
    return gap_wizard::get_click(origin, unit_dir, x, y) || ground_click(origin, unit_dir);
}

void add_overlay(const overlays::Painter& painter)
{
    skate_view::add_overlay(painter);
    gap_wizard::add_overlay(painter);
    constexpr uint32_t kRect = 0xFFFFE070, kPreview = 0xE0FFE070;
    if (g_stage == Stage::kSecond) {
        V3 b = g_have_hover ? g_hover : g_first;
        float y = g_first.y + 2.0f;
        const std::array<V3, 4> c = {{{g_first.x, y, g_first.z}, {b.x, y, g_first.z}, {b.x, y, b.z}, {g_first.x, y, b.z}}};
        for (int i = 0; i < 4; ++i)
            (void)painter.get_seg(core::at(c, i), core::at(c, (i + 1) % 4), kRect, 2.0f);
        std::array<char, 64> text{};
        text::format_to(text, "%.0f x %.0f", std::fabs(b.x - g_first.x), std::fabs(b.z - g_first.z));
        painter.draw_label(b, kRect, text.data());
    } else if (g_stage == Stage::kFirst && g_have_hover) {
        painter.perpendicular(g_hover, 25.0f, kRect, 2.0f);
    }
    if (g_stage == Stage::kForm)
        ground::add_preview(painter, g_draft, kPreview);
}

void get_windows(float view_x, float view_y, float view_width, float view_height, float scale)
{
    gap_wizard::pane(view_x, view_y, view_width, view_height, scale);
    ground_form(view_x, view_y, view_width, scale);
    skate_view::get_legend(view_x, view_y, view_width, view_height, scale);
}

std::string describe()
{
    std::string text;
    if (!ground::get_all().empty())
        text += std::to_string(ground::get_all().size()) + " ground pieces";
    if (skate_view::get_shown())
        text += std::string(text.empty() ? "" : ", ") + "skateability overlay on";
    if (gap_wizard::get_armed())
        text += std::string(text.empty() ? "" : ", ") + "gap wizard armed";
    return text;
}

} // namespace frontend::level_editor::ux9
