// NOT ORIGINAL - the level editor's UI (ours). See level_editor_ui.h. Only ImGui's own API here: the platform and the
// renderer are level_editor_ui_backend.cpp's.

#include "editor/level_editor_actions.h"
#include "editor/level_editor_adjust.h"
#include "editor/level_editor_arrange.h"
#include "editor/level_editor_ui.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "editor/level_editor_entities.h"
#include "editor/level_editor_budget.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_goals.h"
#include "editor/level_editor_hints.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_notes.h"
#include "editor/level_editor_ux9.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_thumbs.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_ui_backend.h"
#include "editor/level_editor_ui_fonts.h"
#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_ui_strip.h"
#include "editor/level_editor_ui_theme.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
#include "editor/level_editor_view.h"
#include "platform/run_log.h"
#include "imgui.h"

namespace frontend::level_editor::ui {

namespace {

bool g_created; // (the ImGui context, made once)
bool g_running; // (between Start and Stop)
float g_scale;  // (the fonts' and the style's scale; 0: not built)
ImFont* g_ui;
ImFont* g_small;
ImFont* g_mono;
ImFont* g_heading;
bool g_scale_locked = true; // (the Inspector's Scale: Uniform - one field changes all three in proportion)

// The routing (level_editor_ui_layout.h): the last frame's layout and ImGui's wishes, the mouse, and which mouse
// buttons a press gave to the UI / to the editor (their release goes the same way).
ui_layout::Layout g_layout;
bool g_want_mouse, g_want_keys;
float g_mouse_x = -1.0f, g_mouse_y = -1.0f;
uint32_t g_ui_buttons, g_editor_buttons;

std::vector<Request> g_requests;
bool g_offer_closed;
char g_outliner_search[96];
bool g_changed_only; // (the Outliner's "Changed only")
char g_browser_search[96];
int g_browser_shown_index = -1;
// The preview's entry: the list's row under the mouse (-1: none) and whether the keys moved the highlight since the
// mouse last moved onto a row (then the highlighted one shows); the entry asked for last.
int g_browser_hovered = -1;
bool g_browser_keys_won;
int g_preview_asked = -1;

void get_ask(Request::Kind kind, int value = 0, uint32_t id = 0) { g_requests.push_back({kind, value, id}); }

// The accent colour ([editor] accent - level_editor_prefs.h) at `alpha`.
ImVec4 get_accent(float alpha)
{
    float rgb[3];
    prefs::get_accent_rgb(prefs::get_pull().accent, rgb);
    return {rgb[0], rgb[1], rgb[2], alpha};
}
int g_style_generation = -1; // (the preferences' scale / accent the style was built for)

// ARGB (the editor's colours) to ImGui's packed colour.
ImU32 get_colour(uint32_t argb)
{
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

// --- fonts and style ---

// The fonts and the style for the window's size (made again when its scale changes: the font texture with them).
void update_scale()
{
    int width = 0, height = 0;
    ui_backend::window_size(&width, &height);
    float scale = ui_layout::scale_for(width, height);
    if (const int pct = prefs::get_pull().ui_scale_pct_;
        pct > 0) // ([editor] ui_scale_pct: the automatic one overridden)
        scale = static_cast<float>(pct) / 100.0f;
    if (scale == g_scale && g_style_generation == prefs::style_generation())
        return;
    g_scale = scale;
    g_style_generation = prefs::style_generation();
    const ui_layout::FontSizes sizes = ui_layout::font_sizes_for(scale);
    const ui_theme::Fonts fonts = ui_theme::build_fonts(scale);
    g_ui = fonts.ui;
    g_small = fonts.small;
    g_mono = fonts.mono;
    g_heading = fonts.heading;
    ui_backend::invalidate_device_objects(); // (the font texture is made again)
    float rgb[3];
    prefs::get_accent_rgb(prefs::get_pull().accent, rgb);
    ui_theme::apply_style(scale, rgb);
    get_log("level editor UI: scale %.2f for a %dx%d window (Inter %.0f px, JetBrains Mono %.0f px)", scale, width,
            height, sizes.ui, sizes.mono);
}

// --- small pieces ---

template <typename... Args>
void get_mono(const char* format, Args... args)
{
    ImGui::PushFont(g_mono);
    ImGui::Text(format, args...);
    ImGui::PopFont();
}

void get_muted(const char* text) { ImGui::TextDisabled("%s", text); }

// A key in a key cap, then what it does.
void key_hint(const char* key, const char* what)
{
    ImGui::PushFont(g_mono);
    const ImVec2 size = ImGui::CalcTextSize(key);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float pad_x = 4.0f * g_scale;
    const float pad_y = 1.0f * g_scale;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y - pad_y),
                                              ImVec2(pos.x + size.x + 2.0f * pad_x, pos.y + size.y + pad_y),
                                              IM_COL32(60, 63, 68, 255), 3.0f * g_scale);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad_x, pos.y));
    ImGui::TextUnformatted(key);
    ImGui::PopFont();
    ImGui::SameLine(0.0f, pad_x + 4.0f * g_scale);
    ImGui::PushFont(g_small);
    ImGui::TextUnformatted(what);
    ImGui::PopFont();
}

// (UX8's drawing: the UI's fonts and scale)
ux8::Fonts ux8_fonts()
{
    ux8::Fonts fonts;
    fonts.ui = g_ui;
    fonts.small = g_small;
    fonts.mono = g_mono;
    fonts.scale = g_scale;
    return fonts;
}

bool get_matches(const std::string& text, const char* filter)
{
    if (!filter[0])
        return true;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    std::string a = text, b = filter;
    std::ranges::transform(a, a.begin(), lower);
    std::ranges::transform(b, b.begin(), lower);
    return a.find(b) != std::string::npos;
}

constexpr ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;

void place_window(const ui_layout::Rect2& rect)
{
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
    ImGui::SetNextWindowSize(ImVec2(rect.width, rect.height));
}

void panel_title(const char* title)
{
    ImGui::PushFont(g_heading);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Separator();
}

// --- the view options (level_editor_view.h) ---

// The collision's legend, over the bottom left of the view while it's shown.
void collision_legend(const ShapeMesh& model)
{
    if (!model.view.collision ||
        !overlays::get_shown(overlays::kLegend)) // (the Overlays menu: level_editor_overlays.h)
        return;
    const ui_layout::Rect2& view = g_layout.viewport;
    ImGui::SetNextWindowPos(ImVec2(view.x + 8.0f * g_scale, view.y + view.height - 8.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::Begin("##collisionLegend", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PushFont(g_small);
    ImGui::TextUnformatted("Collision");
    ImGui::SameLine();
    if (ImGui::SmallButton("Hide##collisionLegend"))
        get_ask(Request::Kind::kCollision); // (value 0: off)
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide Collision  (C)");
    for (int i = 0; i < static_cast<int>(view::Surface::kCount); ++i) {
        auto surface = static_cast<view::Surface>(i);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float side = ImGui::GetTextLineHeight();
        const uint32_t argb = view::surface_colour(surface) | 0xFF000000;
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + side, at.y + side), get_colour(argb), 2.0f);
        ImGui::Dummy(ImVec2(side, side));
        ImGui::SameLine();
        ImGui::TextUnformatted(view::surface_name(surface));
    }
    ImGui::TextDisabled("%d faces, %d pieces, %d invisible walls%s", model.view.triangles, model.view.sectors,
                        model.view.walls, model.view.capped ? " (nearest)" : "");
    if (model.view.off > 0)
        ImGui::TextDisabled("Faded / dashed: collision off (%d)", model.view.off);
    // (a moved piece's shared collision - level_editor_shared_coll.h - and the level's triggers by kind)
    auto swatch_line = [](uint32_t argb, const char* text) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float side = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRect(at, ImVec2(at.x + side, at.y + side), get_colour(argb | 0xFF000000u), 2.0f,
                                            0, 2.0f);
        ImGui::Dummy(ImVec2(side, side));
        ImGui::SameLine();
        ImGui::TextUnformatted(text);
    };
    swatch_line(view::kCompanionEdge, "Moved collision");
    swatch_line(view::kTakenEdge, "Original collision position (off)");
    swatch_line(view::kHoverWall, "Wall under cursor (Shift)");
    if (model.view.companions > 0 || model.view.taken > 0)
        ImGui::TextDisabled("%d moved pieces, %d faces moved", model.view.companions, model.view.taken);
    if (ImGui::TreeNode("Triggers")) {
        for (int k = 0; k < triggers::kKinds; ++k) {
            auto kind = static_cast<triggers::Kind>(k);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float side = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + side, at.y + side),
                                                      get_colour(triggers::kind_colour(kind) | 0xFF000000u), 2.0f);
            ImGui::Dummy(ImVec2(side, side));
            ImGui::SameLine();
            ImGui::TextUnformatted(triggers::kind_name(kind));
        }
        ImGui::TextDisabled("%d trigger faces", model.view.triggers);
        ImGui::TreePop();
    }
    ImGui::TextDisabled("Not shown: camera pass-through, not taggable");
    ImGui::PopFont();
    ImGui::End();
}

// The Inspector's part: an invisible wall selected (Shift+click), and what's hidden in the editor.
void view_section(const ShapeMesh& model)
{
    if (model.view.wall) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PushFont(g_heading);
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.9f, 1.0f), "%s", model.view.wall_name_.c_str());
        ImGui::PopFont();
        get_muted("Invisible wall");
        ImGui::SameLine();
        get_mono("0x%08X", model.view.wall);
        ImGui::PushFont(g_mono);
        ImGui::TextWrapped("%s", model.view.wall_info_.c_str());
        ImGui::PopFont();
        // (level_editor_walls.h: the game's own switch, WorldCell::change_live; moved and turned as scenery)
        if (model.view.wall_off_)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "%s", model.view.wall_state_.c_str());
        else
            ImGui::TextWrapped("%s", model.view.wall_state_.c_str());
        if (model.view.wall_off_) {
            if (ImGui::Button("Restore Collision"))
                get_ask(Request::Kind::kWallCollision, 1);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Turn collision back on  (Del)");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.22f, 0.30f, 0.85f));
            if (ImGui::Button("Disable Collision"))
                get_ask(Request::Kind::kWallCollision, 0);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Turn this wall's collision off  (Del)");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!model.view.wall_moved_ && !model.view.wall_off_);
        if (ImGui::Button("Revert##wall"))
            get_ask(Request::Kind::kReset);
        ImGui::EndDisabled();
        if (ImGui::Button("Drop to Ground##wall"))
            get_ask(Request::Kind::kDrop);
        ImGui::SameLine();
        if (ImGui::Button("Close##wall"))
            get_ask(Request::Kind::kClearWall);
    }
    if (model.view.scenery_collision_ >= 0 && !model.view.wall) {
        // A drawn piece's collision switched off by itself (it stays drawn; the same switch, level_editor_walls.h).
        ImGui::Spacing();
        ImGui::Separator();
        bool on = model.view.scenery_collision_ == 1;
        if (ImGui::Checkbox(model.view.scenery_collision_ == 2 ? "Collision (mixed)" : "Collision", &on))
            get_ask(Request::Kind::kSceneryCollision, on ? 1 : 0);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off: visible but not solid");
    }
    if (model.view.can_hide_ || model.view.hidden > 0) {
        ImGui::Spacing();
        ImGui::Separator();
        get_muted("Editor Visibility");
        ImGui::BeginDisabled(!model.view.can_hide_);
        if (ImGui::Button("Hide"))
            get_ask(Request::Kind::kViewHide);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hide Selection  (H)");
        ImGui::SameLine();
        if (ImGui::Button("Isolate"))
            get_ask(Request::Kind::kViewIsolate);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Isolate Selection  (I)");
        ImGui::EndDisabled();
        if (model.view.hidden > 0) {
            ImGui::SameLine();
            if (ImGui::Button("Show All"))
                get_ask(Request::Kind::kViewShowAll);
            ImGui::TextDisabled("%d hidden", model.view.hidden);
        }
    }
    faces::inspector_section(g_mono, g_small); // (map3: the collision faces selected - level_editor_faces.h)
}

// --- the panels ---

// The menu bar over the tool strip (level_editor_ui_strip.h).
void get_toolbar(const ShapeMesh& model)
{
    place_window(g_layout.menu);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##menubar", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(2);
    if (ImGui::BeginMenuBar()) {
        menu_bar(model, ux8_fonts(), &g_requests);
        ImGui::EndMenuBar();
    }
    ImGui::End();

    place_window(g_layout.strip);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * g_scale, 4.0f * g_scale));
    ImGui::Begin("##toolstrip", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * g_scale, 3.0f * g_scale));
    ui_strip::State state;
    state.selection = std::strcmp(model.mode, "FLYING") != 0 && std::strcmp(model.mode, "ADD") != 0;
    xform::Settings& transform = xform::at_hand();
    state.tool = static_cast<int>(transform.mode);
    state.local = transform.space == xform::Space::kLocal;
    state.snap = model.snap_always_;
    state.grid = xform::get_grid();
    state.grids = xform::grid_sizes();
    state.pick_filter_ = model.pick_filter_;
    state.level = model.level_title_;
    state.saved = model.edited_saved_;
    state.unsaved = model.unsaved;
    state.budget = budget::strip_meter(&state.budget_fill_, &state.budget_argb_);
    int infos = 0;
    checks::shown_counts(&state.errors, &state.warnings, &infos);
    state.hidden = model.view.hidden;
    state.simulating = play_test::get_simulating();
    state.key_of_ = [](const char* id) {
        const int index = actions::get_find(id);
        return index >= 0 ? hints::leading_key_name(actions::get_keys_text(index)) : std::string();
    };
    const ui_layout::Rect2& strip = g_layout.strip;
    const float pad_x = 8.0f * g_scale;
    const float pad_y = std::max(0.0f, (strip.height - ImGui::GetFrameHeight()) * 0.5f);
    ui_strip::Output out;
    ui_strip::blit(state, strip.x + pad_x, strip.y + pad_y, strip.width - 2.0f * pad_x, g_scale, &out);
    ImGui::PopStyleVar();
    if (out.budget_hovered_)
        budget::tooltip();
    if (out.level_hovered_)
        ImGui::SetTooltip(
            "Editing a copy of %s. The original level is unchanged.\nLayout: %s", model.level_title_.c_str(),
            model.edited_saved_ ? (model.unsaved ? "saved, with unsaved changes" : "saved") : "not saved yet");
    for (const ui_strip::Occurrence& e : out.events) {
        switch (e.kind) {
        case ui_strip::Occurrence::Kind::kDeselect:
            get_ask(Request::Kind::kDeselect);
            break;
        case ui_strip::Occurrence::Kind::kTool:
            transform.mode = static_cast<xform::Mode>(static_cast<int>(e.value));
            break;
        case ui_strip::Occurrence::Kind::kSpace:
            transform.space = transform.space == xform::Space::kWorld ? xform::Space::kLocal : xform::Space::kWorld;
            break;
        case ui_strip::Occurrence::Kind::kSnap:
            get_ask(Request::Kind::kSnapAlways, e.value != 0.0f ? 1 : 0);
            break;
        case ui_strip::Occurrence::Kind::kGrid:
            xform::set_grid(e.value);
            break;
        case ui_strip::Occurrence::Kind::kPick:
            get_ask(Request::Kind::kPickFilter, 0, static_cast<uint32_t>(e.value));
            break;
        case ui_strip::Occurrence::Kind::kProblems:
            checks::toggle_window();
            break;
        case ui_strip::Occurrence::Kind::kBudget:
            checks::open_window(checks::kBudgetTab);
            break;
        case ui_strip::Occurrence::Kind::kShowAll:
            get_ask(Request::Kind::kViewShowAll);
            break;
        case ui_strip::Occurrence::Kind::kSimulate:
            actions::get_run(actions::kSimulate);
            break;
        }
    }
    ImGui::End();
}
// The Outliner's rows: each node's label (made again when the node arrays change) and the families' node lists.
struct OutlinerCache {
    uint32_t generation = 0;
    std::vector<std::string> labels;
    std::vector<int> by_family_[nodes::kFamilies];
};
OutlinerCache g_cache;
int g_last_selected = -1;

void update_cache()
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (g_cache.generation == nodes::get_generation() && g_cache.labels.size() == nodes.size())
        return;
    g_cache.generation = nodes::get_generation();
    g_cache.labels.clear();
    for (std::vector<int>& list : g_cache.by_family_)
        list.clear();
    for (size_t i = 0; i < nodes.size(); ++i) {
        g_cache.labels.push_back(nodes[i].name ? nodes::caption(nodes[i].name) : "(no name)");
        g_cache.by_family_[static_cast<int>(nodes[i].family)].push_back(static_cast<int>(i));
    }
}

// What a row shows after the name: a spawn's types, a crown's index, a flag's colour, a rail's links.
std::string row_tag_of(const nodes::Elem& node);
std::string row_tag(const nodes::Elem& node)
{
    std::string tag = row_tag_of(node);
    if (node.hidden)
        return tag.empty() ? "hidden" : tag + " hidden";
    if (node.added)
        return tag.empty() ? "added" : tag + " added";
    return tag;
}
std::string row_tag_of(const nodes::Elem& node)
{
    char text[48] = "";
    switch (node.family) {
    case nodes::Family::kSpawn: {
        std::string tags;
        auto add = [&](uint32_t bit, const char* tag) {
            if (node.restart_types_ & bit)
                tags += tags.empty() ? tag : std::string(" ") + tag;
        };
        add(nodes::kPlayer1, "P1");
        add(nodes::kPlayer2, "P2");
        add(nodes::kMultiplayer, "MP");
        add(nodes::kHorse, "H");
        add(nodes::kCtf, "CTF");
        add(nodes::kTeam, "Team");
        add(nodes::kKing, "KotH");
        return tags;
    }
    case nodes::Family::kCrown:
        if (node.order < 0)
            return "";
        std::snprintf(text, sizeof(text), "#%d", node.order);
        return text;
    case nodes::Family::kRail:
    case nodes::Family::kRouteWay:
    case nodes::Family::kLadder:
        if (node.links.empty())
            return "end";
        return node.links.size() == 1 ? "" : std::to_string(node.links.size()) + " links";
    default:
        return "";
    }
}

void family_chips(const ShapeMesh& model)
{
    ImGui::PushFont(g_small);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f * g_scale, 2.0f * g_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * g_scale, 4.0f * g_scale));
    const int* counts = nodes::get_counts();
    const float right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    for (int f = 0; f < nodes::kFamilies; ++f) {
        auto family = static_cast<nodes::Family>(f);
        if (!counts[f])
            continue;
        const bool on = (model.nodes.families & nodes::get_bit(family)) != 0;
        const char* name = nodes::family_name(family);
        const float width = ImGui::CalcTextSize(name).x + 2.0f * ImGui::GetStyle().FramePadding.x;
        if (ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x + 1.0f) {
            ImGui::SameLine();
            if (ImGui::GetCursorPosX() + width > right)
                ImGui::NewLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Button, on ? get_accent(0.75f) : ImVec4(0.2f, 0.21f, 0.225f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.6f, 0.61f, 0.63f, 1.0f));
        if (ImGui::SmallButton(name))
            get_ask(Request::Kind::kFamilies, 0, model.nodes.families ^ nodes::get_bit(family));
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d %s - click to %s", counts[f], name, on ? "hide" : "show");
    }
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
}

void get_outliner(const ShapeMesh& model)
{
    if (!g_layout.outliner_shown_)
        return;
    update_cache();
    place_window(g_layout.outliner);
    ImGui::Begin("Outliner", nullptr, kPanel);
    panel_title("Outliner");
    ImGui::SetNextItemWidth(-1.0f);
    // (Select similar on a node asks for its family here - level_editor_ux3.h)
    if (const std::string asked = ux3::take_outliner_search(); !asked.empty())
        std::snprintf(g_outliner_search, sizeof(g_outliner_search), "%s", asked.c_str());
    ImGui::InputTextWithHint("##search", "Search\xE2\x80\xA6", g_outliner_search, sizeof(g_outliner_search));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Filter by name. Prefixes: rail: spawn: object: group:<name> label: locked: hidden:");
    // (only the rows the editor has changed - those with markers)
    ImGui::Checkbox("Changed Only", &g_changed_only);
    if (ImGui::IsItemHovered()) {
        std::string legend = "Show changed items only. Markers:";
        for (int bit = 0; bit < kMaxMarkers; ++bit)
            legend += std::string("\n") + mark_letter(bit) + "  " + mark_name(bit);
        ImGui::SetTooltip("%s", legend.c_str());
    }
    if (!model.marks.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d changed", static_cast<int>(model.marks.size()));
    }
    family_chips(model);
    ImGui::Separator();
    ImGui::BeginChild("##tree");
    ux3::bunches(); // (the user's groups - level_editor_organize.h)
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (nodes.empty())
        get_muted("No level loaded");
    const int selected = model.nodes.selected;
    const bool follow = selected != g_last_selected && selected >= 0; // (picked in the view: shown here)
    g_last_selected = selected;
    static std::vector<int> shown;
    for (int f = 0; f < nodes::kFamilies; ++f) {
        auto family = static_cast<nodes::Family>(f);
        const std::vector<int>& members = g_cache.by_family_[f];
        if (members.empty() || !(model.nodes.families & nodes::get_bit(family)))
            continue;
        shown.clear();
        for (const int i : members) {
            auto found = model.marks.find(nodes[i].name);
            if (ux3::row_passes(nodes[i], g_cache.labels[i], g_outliner_search) &&
                mark_shown(found == model.marks.end() ? 0 : found->second, g_changed_only))
                shown.push_back(i);
        }
        if (shown.empty())
            continue;
        const bool holds_selected = follow && std::cmp_less(selected, nodes.size()) && nodes[selected].family == family;
        if (holds_selected || g_outliner_search[0] || g_changed_only)
            ImGui::SetNextItemOpen(true, holds_selected ? ImGuiCond_Always : ImGuiCond_Once);
        char header[64];
        std::snprintf(header, sizeof(header), "%s###family%d", nodes::family_name(family), f);
        const bool open = ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_SpanAvailWidth);
        ImGui::SameLine();
        ImGui::PushFont(g_mono);
        ImGui::TextDisabled("%d", static_cast<int>(shown.size()));
        ImGui::PopFont();
        if (!open)
            continue;
        ImGui::PushFont(g_mono);
        // (rows the clipper skips aren't laid out; the selected one is brought into view once)
        int scroll_to = -1;
        if (holds_selected) {
            auto it = std::ranges::find(shown, selected);
            if (it != shown.end())
                scroll_to = static_cast<int>(it - shown.begin());
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        if (scroll_to >= 0)
            clipper.IncludeItemByIndex(scroll_to);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const int i = shown[row];
                const nodes::Elem& node = nodes[i];
                ImGui::PushID(i);
                const bool is_selected = i == selected;
                // (the gizmo's colour as a small swatch)
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float h = ImGui::GetTextLineHeight();
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + h * 0.3f),
                                                          ImVec2(pos.x + h * 0.4f, pos.y + h * 0.7f),
                                                          get_colour(node.colour), 2.0f);
                ImGui::SetCursorScreenPos(ImVec2(pos.x + h * 0.6f, pos.y));
                if (node.hidden)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                // (the user's label beside the name - level_editor_organize.h)
                const std::string user_label = ux3::row_label(node.name);
                const std::string row_text =
                    user_label.empty() ? g_cache.labels[i] : g_cache.labels[i] + "  \xE2\x80\x94 " + user_label;
                const bool clicked =
                    ImGui::Selectable(row_text.c_str(), is_selected,
                                      ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowOverlap); // (the row's toggles)
                ux3::row_menu(node); // (right-click: label, groups, eye / lock / selectable)
                if (node.hidden)
                    ImGui::PopStyleColor();
                if (clicked) {
                    get_ask(Request::Kind::kSelectNode, i);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        get_ask(Request::Kind::kGoToNode, i);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click to select, double-click to focus");
                const std::string tag = row_tag(node);
                // (the markers - what the editor has done to it - at the row's end, the tag before them)
                auto found = model.marks.find(node.name);
                const uint8_t marks = found == model.marks.end() ? 0 : found->second;
                // (the Overlays menu's Outliner markers off: none shown - Changed only still filters by them)
                const std::string letters =
                    overlays::get_shown(overlays::kOutlinerMarks) ? mark_letters(marks) : std::string();
                const float row_right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x -
                                        ImGui::GetStyle().ScrollbarSize - ux3::toggles_width();
                const float marks_width =
                    letters.empty() ? 0.0f : ImGui::CalcTextSize(letters.c_str()).x + 6.0f * g_scale;
                if (!tag.empty()) {
                    const float width = ImGui::CalcTextSize(tag.c_str()).x;
                    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), row_right - marks_width - width));
                    ImGui::TextColored(ImVec4(0.6f, 0.61f, 0.63f, 1.0f), "%s", tag.c_str());
                }
                if (!letters.empty()) {
                    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), row_right - marks_width + 6.0f * g_scale));
                    bool first = true;
                    for (int bit = 0; bit < kMaxMarkers; ++bit) {
                        if (!(marks & (1u << bit)))
                            continue;
                        if (!first)
                            ImGui::SameLine(0.0f, 0.0f);
                        first = false;
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(get_colour(mark_colour(bit))), "%c",
                                           mark_letter(bit));
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", mark_name(bit));
                    }
                }
                ux3::row_toggles(node, row_right); // (the eye, the lock, selectable - level_editor_organize.h)
                if (row == scroll_to)
                    ImGui::SetScrollHereY(0.4f);
                ImGui::PopID();
            }
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }
    notes::outliner_section(g_mono, g_outliner_search); // (UX5: note pins - level_editor_notes.h)
    ImGui::EndChild();
    ImGui::End();
}

// --- the Inspector ---

// A row of the Inspector's table: a label, then whatever follows.
void field_label(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    get_muted(label);
    ImGui::TableNextColumn();
}

void badge(const char* text, ImU32 colour)
{
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float pad = 4.0f * g_scale;
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x + 2.0f * pad, pos.y + size.y), colour,
                                              3.0f * g_scale);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad, pos.y));
    ImGui::TextUnformatted(text);
    ImGui::SameLine(0.0f, pad + 4.0f * g_scale);
}

void node_section(const ShapeMesh& model, int index)
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (index < 0 || std::cmp_greater_equal(index, nodes.size()))
        return;
    const nodes::Elem& node = nodes[index];
    if (model.nodes.node_only_) {
        ImGui::PushFont(g_heading);
        ImGui::TextWrapped("%s", node.name ? nodes::caption(node.name).c_str() : "(no name)");
        ImGui::PopFont();
    } else {
        ImGui::Spacing();
        ImGui::SeparatorText("Node");
    }
    std::string cls = node.cls ? nodes::caption(node.cls) : "(no class)";
    if (node.type)
        cls += " \xC2\xB7 " + nodes::caption(node.type);
    get_muted(cls.c_str());
    ImGui::PushFont(g_mono);
    ImGui::TextDisabled("node %d in %s", node.index, nodes::caption(nodes::get_array_name(node)).c_str());
    ImGui::PopFont();
    if (node.added) {
        ImGui::PushFont(g_small);
        badge("Added", IM_COL32(46, 122, 67, 200));
        ImGui::NewLine();
        ImGui::PopFont();
    }
    if (node.hidden) {
        ImGui::PushFont(g_small);
        badge("Hidden", IM_COL32(110, 110, 110, 220));
        ImGui::NewLine();
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s", "Hidden in game. Revert to show.");
        ImGui::PopFont();
    }
    // Online: spawns, crowns and flags in a level shared with THUGPRO.
    if (nodes::get_online(node.family) && model.nodes.level_shared_) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.37f, 0.6f, 0.35f));
        ImGui::BeginChild("##online", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::PushFont(g_heading);
        ImGui::TextColored(ImVec4(0.56f, 0.71f, 0.92f, 1.0f), "Online");
        ImGui::PopFont();
        ImGui::TextWrapped("Shared with THUGPRO (ported from %s). %s affect online play.", model.nodes.shared_game_,
                           nodes::family_name(node.family));
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    const bool editable = model.nodes.node_only_ && model.nodes.movable;
    if (ImGui::BeginTable("##node", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
        if (editable) {
            // Typed or dragged to scrub (Ctrl+click or a double-click types a number); one undo step a field.
            float pos[3] = {model.nodes.pos.x, model.nodes.pos.y, model.nodes.pos.z};
            float heading = node_edit::get_heading_degrees(model.nodes.angles);
            ImGui::PushFont(g_mono);
            field_label("Position");
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool moved = ImGui::DragFloat3("##pos", pos, 1.0f, 0.0f, 0.0f, "%.1f");
            bool done = ImGui::IsItemDeactivated();
            bool turned = false;
            if (!model.nodes.rail) {
                field_label("Heading");
                ImGui::SetNextItemWidth(-FLT_MIN);
                turned = ImGui::DragFloat("##heading", &heading, 0.5f, 0.0f, 0.0f, "%.1f\xC2\xB0");
                done = done || ImGui::IsItemDeactivated();
            }
            ImGui::PopFont();
            if (model.nodes.rail) {
                // A rail point: alone, or its whole rail (the points linked to it) by as much (L).
                field_label("Moves");
                bool whole = model.nodes.entire_rail_;
                char label[64];
                std::snprintf(label, sizeof(label), "Whole %s (%d point%s)", model.nodes.chain_noun_,
                              model.nodes.rail_points_, model.nodes.rail_points_ == 1 ? "" : "s");
                if (ImGui::Checkbox(label, &whole))
                    get_ask(Request::Kind::kReqEntireRail, whole ? 1 : 0);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Move all points together  (L)");
            }
            if (moved || turned) {
                const V3 angles = turned ? node_edit::with_heading(model.nodes.angles, heading) : model.nodes.angles;
                Request request = {Request::Kind::kNodeSet};
                request.a[0] = pos[0];
                request.a[1] = pos[1];
                request.a[2] = pos[2];
                request.b[0] = angles.x;
                request.b[1] = angles.y;
                request.b[2] = angles.z;
                g_requests.push_back(request);
            }
            if (done)
                get_ask(Request::Kind::kNodeSetDone);
            field_label("Angles");
            get_mono("%.3f, %.3f, %.3f", model.nodes.angles.x, model.nodes.angles.y, model.nodes.angles.z);
        } else if (node.has_pos_) {
            field_label("Position");
            get_mono("x %10.1f\ny %10.1f\nz %10.1f", node.pos.x, node.pos.y, node.pos.z);
            field_label("Angles");
            get_mono("%.3f, %.3f, %.3f", node.angles.x, node.angles.y, node.angles.z);
            if (nodes::shape_of(node.family) == nodes::Shape::kPoint) {
                field_label("Heading");
                get_mono("%.1f\xC2\xB0", std::atan2(node.forward.x, node.forward.z) * 57.29578f);
            }
        }
        if (node.family == nodes::Family::kSpawn) {
            field_label("Types");
            static const struct {
                uint32_t bit;
                const char* name;
            } kTypes[] = {
                {nodes::kPlayer1, "Player1"},    {nodes::kPlayer2, "Player2"}, {nodes::kMultiplayer, "Multiplayer"},
                {nodes::kHorse, "Horse"},        {nodes::kCtf, "CTF"},         {nodes::kTeam, "Team"},
                {nodes::kKing, "KingOfTheHill"}, {nodes::kGeneric, "Generic"}};
            ImGui::PushFont(g_small);
            bool any = false;
            for (const auto& t : kTypes) {
                if (node.restart_types_ & t.bit) {
                    badge(t.name, get_colour((node.colour & 0x00FFFFFF) | 0x80000000));
                    any = true;
                }
            }
            if (!any)
                get_muted("(none)");
            else
                ImGui::NewLine();
            ImGui::PopFont();
            field_label("Order");
            if (node.hidden)
                get_mono("hidden");
            else
                get_mono("%d of %d restarts", node.order + 1, nodes::walked(nodes::Family::kSpawn));
        }
        if (node.family == nodes::Family::kCrown) {
            field_label("Crown");
            if (node.hidden)
                get_mono("hidden (%d active)", nodes::walked(nodes::Family::kCrown));
            else
                get_mono("%d of %d", node.order, nodes::walked(nodes::Family::kCrown));
            if (model.nodes.node_only_ && !node.hidden) {
                ImGui::BeginDisabled(!model.nodes.crown_up_);
                if (ImGui::SmallButton("Move Up"))
                    get_ask(Request::Kind::kCrownMove, -1);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(node.added ? "Move earlier in crown order"
                                                 : "Only added crowns can be reordered");
                ImGui::SameLine();
                ImGui::BeginDisabled(!model.nodes.crown_down_);
                if (ImGui::SmallButton("Move Down"))
                    get_ask(Request::Kind::kCrownMove, 1);
                ImGui::EndDisabled();
            }
        }
        if (node.family == nodes::Family::kLight) {
            field_label("Colour");
            const ImVec4 c = ImGui::ColorConvertU32ToFloat4(get_colour(node.colour));
            ImGui::ColorButton("##colour", c, ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f * g_scale, 14.0f * g_scale));
            ImGui::SameLine();
            get_mono("%u %u %u", (node.colour >> 16) & 0xFF, (node.colour >> 8) & 0xFF, node.colour & 0xFF);
            field_label("Radius");
            get_mono("%.0f inner, %.0f outer", node.inner_radius_, node.radius);
        }
        if (node.family == nodes::Family::kFireOff && node.radius > 0.0f) {
            field_label("Radius");
            get_mono("%.0f", node.radius);
        }
        if (node.terrain) {
            field_label("Terrain");
            get_mono("%s", nodes::caption(node.terrain).c_str());
        }
        if (!node.links.empty()) {
            field_label("Links");
            ImGui::PushFont(g_mono);
            for (const int link : node.links) {
                ImGui::PushID(link);
                if (ImGui::SmallButton(nodes::caption(nodes[link].name).c_str()))
                    get_ask(Request::Kind::kSelectNode, link);
                ImGui::PopID();
            }
            ImGui::PopFont();
        }
        ImGui::EndTable();
    }
    if (editable) {
        ImGui::PushFont(g_small);
        ImGui::TextDisabled("Applies: %s", model.nodes.when_applied_);
        if (model.nodes.moved) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "Modified");
        }
        ImGui::PopFont();
        if (ImGui::Button("Drop to Ground"))
            get_ask(Request::Kind::kDrop);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drop to Ground  (G)");
        ImGui::SameLine();
        ImGui::BeginDisabled(!model.nodes.moved && !model.nodes.hidden);
        if (ImGui::Button(model.nodes.hidden ? "Show" : "Revert"))
            get_ask(Request::Kind::kReset);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (model.nodes.added || (model.nodes.can_hide_ && !model.nodes.hidden)) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.14f, 0.13f, 1.0f));
            if (ImGui::Button(model.nodes.added ? "Delete" : "Hide"))
                get_ask(Request::Kind::kRemoveNodeAction);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(model.nodes.added ? "Delete  (Del)" : "Hide original node  (Del)");
            ImGui::SameLine();
        } else if (!model.nodes.added && !model.nodes.hidden && !model.nodes.why_no_hide_.empty()) {
            ImGui::BeginDisabled(true);
            ImGui::Button("Hide");
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", model.nodes.why_no_hide_.c_str());
            ImGui::SameLine();
        }
    }
    // Phase 4: a rail's / path's operations, a rail's terrain, a ped's / car's path (level_editor_chains_ui.cpp).
    if ((model.nodes.node_only_ || model.chain.object) && model.chain.show) {
        ImGui::NewLine();
        chain_section(model.chain, &g_requests);
    }
    play_test::restart_section(index); // (a restart's Play from here - level_editor_playtest.h)
    spawn_section(index, &g_requests); // (a HORSE spot - the feature map: level_editor_spots_ui.cpp)
    if (ImGui::Button("Focus"))
        get_ask(Request::Kind::kGoToNode, index);
    // Every field of its structure, typed and editable (phase 6: level_editor_fields_ui.cpp).
    fields_section(index, g_mono, g_small, &g_requests);
}

// The locks (level_editor_picking.h): each with Unlock.
void lock_list(const ShapeMesh& model)
{
    if (model.locks.empty())
        return;
    ImGui::Spacing();
    ImGui::Separator();
    char title[48];
    std::snprintf(title, sizeof(title), "Locked (%d)###locks", static_cast<int>(model.locks.size()));
    if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
        return;
    for (size_t i = 0; i < model.locks.size(); ++i) {
        const ShapeMesh::LockView& lock = model.locks[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("Unlock"))
            get_ask(Request::Kind::kUnlock, static_cast<int>(lock.kind), lock.id);
        ImGui::SameLine();
        ImGui::PushFont(g_mono);
        ImGui::TextUnformatted(lock.label.c_str());
        ImGui::PopFont();
        ImGui::PopID();
    }
}

// The scenery selection's parts (its pieces, shadows marked, and the rail points carried along): each with x (removed
// from the selection, as Ctrl+click) and Keep only this.
void member_list(const std::vector<ShapeMesh::Selection::Member>& members)
{
    if (members.empty())
        return;
    ImGui::Spacing();
    get_muted("Parts");
    const float rows = static_cast<float>(std::min<size_t>(members.size(), 10));
    ImGui::BeginChild("##members", ImVec2(0.0f, rows * ImGui::GetFrameHeightWithSpacing() + 4.0f * g_scale), false);
    for (size_t i = 0; i < members.size(); ++i) {
        const ShapeMesh::Selection::Member& member = members[i];
        ImGui::PushID(static_cast<int>(i));
        const int rail = member.rail ? 1 : 0;
        if (ImGui::SmallButton("x"))
            get_ask(Request::Kind::kMemberRemove, rail, member.id);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Remove from selection");
        ImGui::SameLine();
        if (ImGui::SmallButton("Only"))
            get_ask(Request::Kind::kMemberKeep, rail, member.id);
        ImGui::SameLine();
        ImGui::PushFont(g_mono);
        ImGui::TextUnformatted(member.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        if (member.rail)
            get_muted("rail point");
        else if (member.shadow)
            get_muted(("shadow  " + member.size).c_str());
        else
            get_muted(member.size.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void get_inspector(const ShapeMesh& model)
{
    if (!g_layout.inspector_shown_)
        return;
    place_window(g_layout.inspector);
    ImGui::Begin("Inspector", nullptr, kPanel);
    panel_title("Inspector");
    const ShapeMesh::Selection& s = model.selection;
    if (model.nodes.node_only_) {
        node_section(model, model.nodes.selected);
        triggers::inspector_section(model.nodes.selected); // (a trigger volume's settings - level_editor_triggers.h)
        entities::inspector_section(model.nodes.selected, 0, g_mono, &g_requests); // (sound, particles, ghosts)
        // (map3: a camera node's pitch, target, preview - level_editor_cameras.h)
        if (model.nodes.selected >= 0 && std::cmp_less(model.nodes.selected, nodes::get_all().size()))
            cameras::inspector_section(nodes::get_all()[model.nodes.selected].name, g_mono, g_small);
        if (model.nodes.selected >= 0 && std::cmp_less(model.nodes.selected, nodes::get_all().size()))
            goal_edit::inspector_section(nodes::get_all()[model.nodes.selected].name, g_mono,
                                         g_small); // (a goal's point)
    } else if (s.objects == 0 && s.sectors == 0 && !model.view.wall) {
        ImGui::TextDisabled("Nothing selected");
    } else if (s.objects > 0 || s.sectors > 0) {
        ImGui::PushFont(g_heading);
        ImGui::TextWrapped("%s", s.name.c_str());
        ImGui::PopFont();
        get_muted(s.kind.c_str());
        if (s.checksum) {
            ImGui::SameLine();
            get_mono("0x%08X", s.checksum);
        }
        ImGui::Spacing();
        if (ImGui::BeginTable("##transform", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            get_muted("Position");
            ImGui::TableNextColumn();
            get_mono("x %10.1f\ny %10.1f\nz %10.1f", s.pos.x, s.pos.y, s.pos.z);
            if (s.has_heading_) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                get_muted("Heading");
                ImGui::TableNextColumn();
                get_mono("%.1f\xC2\xB0", s.heading_degrees_);
            }
            if (s.scalable) {
                // The scale along its own axes (level_editor_scale.h): X / Y / Z dragged to scrub, Ctrl+click to type;
                // one undo step a field. Locked (the default): all three keep their proportions.
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                get_muted("Scale");
                ImGui::TableNextColumn();
                const float values[3] = {s.scale.x, s.scale.y, s.scale.z};
                static const char* const kIds[3] = {"##scalex", "##scaley", "##scalez"};
                static const char* const kFormats[3] = {"x %.3f", "y %.3f", "z %.3f"};
                ImGui::PushFont(g_mono);
                for (int axis = 0; axis < 3; ++axis) {
                    ImGui::SetNextItemWidth(110.0f * g_scale);
                    float value = values[axis];
                    if (ImGui::DragFloat(kIds[axis], &value, 0.005f, scale_tool::kMin, scale_tool::kMax, kFormats[axis],
                                         ImGuiSliderFlags_AlwaysClamp)) {
                        Request request = {Request::Kind::kScaleSet};
                        for (int a = 0; a < 3; ++a)
                            request.a[a] = values[a];
                        if (g_scale_locked && values[axis] > 0.0f) {
                            // (every axis by the same ratio, clamped so none leaves 0.25..4)
                            float ratio = value / values[axis];
                            for (const float limit : values)
                                ratio = std::clamp(ratio, scale_tool::kMin / limit, scale_tool::kMax / limit);
                            for (int a = 0; a < 3; ++a)
                                request.a[a] = values[a] * ratio;
                        } else {
                            request.a[axis] = value;
                        }
                        g_requests.push_back(request);
                    }
                    if (ImGui::IsItemDeactivated())
                        get_ask(Request::Kind::kScaleDone);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Local %c scale (%.2f to %.0f)", "XYZ"[axis], scale_tool::kMin,
                                          scale_tool::kMax);
                }
                ImGui::PopFont();
                ImGui::Checkbox("Uniform##scalelock", &g_scale_locked);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Scale all axes together");
            }
            if (s.objects > 1 || s.sectors > 1) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                get_muted("Selected");
                ImGui::TableNextColumn();
                get_mono("%d", s.objects > 0 ? s.objects : s.sectors);
            }
            ImGui::EndTable();
        }
        member_list(s.members);
        // (map3: the selected scenery's trick-object cluster - level_editor_clusters.h)
        if (s.sectors > 0 && s.objects == 0)
            clusters::inspector_section(scenery::get_selection(), g_mono, g_small);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", model.selected.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Drop to Ground"))
            get_ask(Request::Kind::kDrop);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drop to Ground  (G)");
        ImGui::SameLine();
        if (ImGui::Button("Deselect"))
            get_ask(Request::Kind::kDeselect);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Deselect  (Esc)");
        if (model.selection_lockable_) {
            ImGui::SameLine();
            if (ImGui::Button(model.selection_locked_ ? "Unlock" : "Lock"))
                get_ask(Request::Kind::kToggleLock);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Prevent selection in the view  (L)");
        }
        if (s.objects == 0 && s.sectors == 1)
            triggers::scenery_section(s.checksum); // (breaks when hit - the feature map: level_editor_triggers_ui.cpp)
        // (one object: its model, physics, path speed and preview - level_editor_entities.h)
        if (s.objects == 1)
            entities::inspector_section(-1, s.checksum, g_mono, &g_requests);
        if (model.nodes.selected >= 0)
            node_section(model, model.nodes.selected);
    }
    ux3::inspector_section(model); // (the clipboard, the shared fields of a multi-selection - level_editor_ux3.h)
    ImGui::Spacing();
    ImGui::Separator();
    get_muted("Under Cursor");
    const std::vector<nodes::Elem>& all = nodes::get_all();
    if (model.nodes.hovered >= 0 && std::cmp_less(model.nodes.hovered, all.size())) {
        const nodes::Elem& node = all[model.nodes.hovered];
        ImGui::TextWrapped("%s (%s)", nodes::caption(node.name).c_str(), nodes::caption(node.cls).c_str());
    } else {
        ImGui::TextWrapped("%s", model.hover.c_str());
    }
    view_section(model);
    entities::view_options(); // (Show uncreated objects)
    lock_list(model);
    cameras::list_section(g_mono, g_small);   // (map3: the camera nodes - level_editor_cameras.h)
    goal_edit::list_section(g_mono, g_small); // (Create-a-Goal goals of the edited copy - level_editor_goals.h)
    level_section(model.level_props_, g_mono, g_small, &g_requests); // (phase 6: level_editor_fields_ui.cpp)
    ImGui::End();
}

void status_bar(const ShapeMesh& model)
{
    place_window(g_layout.status);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * g_scale, 4.0f * g_scale));
    ImGui::Begin("##status", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    // Right: unsaved, the changes, the camera (elided before the hints are crowded out).
    char right[224];
    const std::string view = view_cam::get_standing(); // (an ortho view, orbiting - level_editor_camera.h)
    std::snprintf(right, sizeof(right),
                  "%s%d changes  \xC2\xB7  %d moved  \xC2\xB7  %.0f, %.0f, %.0f  \xC2\xB7  speed %.0f%s%s",
                  model.unsaved ? "Unsaved  \xC2\xB7  " : "", model.changes, model.moved, model.camera.x,
                  model.camera.y, model.camera.z, model.speed, view.empty() ? "" : "  \xC2\xB7  ", view.c_str());
    const ui_layout::Rect2& bar = g_layout.status;
    const float pad_x = 8.0f * g_scale;
    ImGui::PushFont(g_small);
    const float y = bar.y + std::max(0.0f, (bar.height - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::PopFont();
    // (UX8: key caps and mouse / controller glyphs - level_editor_ux8.h)
    ui_strip::status_row(model.message, model.keys, right, bar.x + pad_x, y, bar.width - 2.0f * pad_x, ux8_fonts());
    ImGui::End();
}
void get_help(const ShapeMesh& model)
{
    if (!model.help)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760.0f * g_scale, io.DisplaySize.y * 0.85f));
    bool open = true;
    ImGui::Begin("Keyboard Shortcuts", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
    // (the first-run tips again, the preferences - level_editor_prefs.h)
    if (ImGui::SmallButton("Show Tips"))
        prefs::show_tips();
    ImGui::SameLine();
    if (ImGui::SmallButton("Preferences"))
        prefs::unseal() = true;
    // (every action from the registry, with its keys as bound now - level_editor_actions.h; the details below)
    ux3::help_from_registry();
    if (ImGui::CollapsingHeader("Reference###helpdetails")) {
        struct Row {
            const char* section;
            const char* keys;
            const char* what;
        };
        static const Row kRows[] = {
            {"Camera", "RMB + W A S D / Q E", "Fly"},
            {"", "RMB + Wheel", "Fly speed"},
            {"", "Alt+drag", "Orbit"},
            {"", "Wheel", "Dolly; while dragging: push / pull"},
            {"", "Double-click", "Go to surface"},
            {"", "Numpad 7 / 1 / 3 / 5", "Top / front / right view, perspective / ortho (Ctrl: opposite)"},
            {"Select", "Click", "Select; click again to cycle"},
            {"", "Shift+click", "Add / remove"},
            {"", "Alt+click", "Single scenery piece"},
            {"", "Ctrl+click", "Remove part from selection"},
            {"", "Drag from empty space", "Box select (right: inside, left: crossing)"},
            {"", "Alt+Wheel", "Cycle items under cursor"},
            {"Transform", "Drag", "Move (Shift: vertical)"},
            {"", "Drag an arrow", "Move along axis"},
            {"", "Ctrl / Ctrl+Shift", "Snap to grid / world grid"},
            {"", "Alt+Shift / Ctrl+Alt", "Snap to surface / vertex"},
            {"", "X / Y / Z, digits, Enter", "Constrain axis, type value, apply"},
            {"", "Alt (while moving)", "Don't carry attached objects"},
            {"", "Esc", "Cancel"},
            {"Rails and Paths", "Click / Enter / RMB", "Add point / finish / finish"},
            {"", "Alt+click", "Add point without edge snapping"},
            {"Collision", "Shift+click", "Select invisible wall (collision shown)"},
            {"Panels", "Mouse over a panel", "Panel input; keys stay with the editor unless typing"},
        };
        // (the columns' widths set, not fitted: a long key list would otherwise squeeze the explanations to a letter a
        // line - the keys wrap within theirs)
        if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("##section", ImGuiTableColumnFlags_WidthFixed, 90.0f * g_scale);
            ImGui::TableSetupColumn("##keys", ImGuiTableColumnFlags_WidthFixed, 190.0f * g_scale);
            ImGui::TableSetupColumn("##what", ImGuiTableColumnFlags_WidthStretch);
            // (the view menus' and their modules' rows after the editor's own: level_editor_overlays.h)
            std::vector<Row> rows(std::begin(kRows), std::end(kRows));
            for (const overlays::HelpRow& extra : overlays::help_rows())
                rows.push_back({extra.section, extra.keys, extra.what});
            for (const Row& row : rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (row.section[0]) {
                    ImGui::PushFont(g_heading);
                    ImGui::TextUnformatted(row.section);
                    ImGui::PopFont();
                }
                ImGui::TableNextColumn();
                ImGui::PushFont(g_mono);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.45f, 1.0f));
                ImGui::TextUnformatted(row.keys);
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
                ImGui::PopFont();
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", row.what);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
    if (!open)
        get_ask(Request::Kind::kToggleHelp);
}

// The browser's preview (level_editor_preview.h): the picture - a drag in it orbits, the wheel zooms - and what the
// entry is.
void browser_preview(const ShapeMesh& model)
{
    const ShapeMesh::PreviewView& preview = model.preview;
    const float width = ImGui::GetContentRegionAvail().x;
    const float side = std::min(width, 210.0f * g_scale);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - side) * 0.5f);
    ImGui::InvisibleButton("##preview", ImVec2(side, side));
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (preview.texture)
        draw->AddImage(static_cast<ImTextureID>(preview.texture), a, b);
    else
        draw->AddRectFilled(a, b, IM_COL32(42, 44, 48, 255));
    draw->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_Border));
    if (!preview.texture) {
        const char* text = preview.index < 0 ? "(no selection)" : "(no preview)";
        const ImVec2 size = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2((a.x + b.x - size.x) * 0.5f, (a.y + b.y - size.y) * 0.5f),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    }
    const ImGuiIO& io = ImGui::GetIO();
    float dx = 0.0f, dy = 0.0f, wheel = 0.0f;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        dx = io.MouseDelta.x;
        dy = io.MouseDelta.y;
    }
    if (ImGui::IsItemHovered())
        wheel = io.MouseWheel;
    if (dx != 0.0f || dy != 0.0f || wheel != 0.0f) {
        Request request{Request::Kind::kPreviewOrbit};
        request.a[0] = dx;
        request.a[1] = dy;
        request.a[2] = wheel;
        g_requests.push_back(request);
    }
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive() && preview.texture)
        ImGui::SetTooltip("Drag to rotate, wheel to zoom");
    if (!preview.name.empty()) {
        ImGui::PushFont(g_mono);
        ImGui::TextWrapped("%s", preview.name.c_str());
        ImGui::PopFont();
        ImGui::PushFont(g_small);
        if (!preview.size.empty())
            get_muted(preview.size.c_str());
        if (!preview.type.empty())
            get_muted(preview.type.c_str());
        ImGui::PopFont();
    }
    ImGui::Separator();
}

void get_browser(const ShapeMesh& model)
{
    if (!model.browser || !model.browser_items_)
        return;
    ImGui::SetNextWindowPos(
        ImVec2(g_layout.viewport.x + g_layout.viewport.width - 8.0f * g_scale, g_layout.viewport.y + 8.0f * g_scale),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(
        ImVec2(360.0f * g_scale, std::min(760.0f * g_scale, g_layout.viewport.height - 16.0f * g_scale)));
    bool open = true;
    char title[64];
    std::snprintf(title, sizeof(title), "Add Piece (%d)###browser", static_cast<int>(model.browser_items_->size()));
    ImGui::Begin(title, &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    browser_preview(model);
    for (int tab = 0; tab < 2; ++tab) {
        if (tab)
            ImGui::SameLine();
        const bool on = model.browser_tab_ == tab;
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, get_accent(0.85f));
        if (ImGui::Button(tab == 0 ? "Scenery" : "Objects") && !on)
            get_ask(Request::Kind::kBrowserTab, tab);
        if (on)
            ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter\xE2\x80\xA6", g_browser_search, sizeof(g_browser_search));
    const float buttons = ImGui::GetFrameHeightWithSpacing() * 3.0f; // (UX7: a row of the arrange tools')
    const std::vector<BrowserItem>& items = *model.browser_items_;
    const bool follow = model.browser_index_ != g_browser_shown_index; // (the keys moved it: keep it in view)
    g_browser_shown_index = model.browser_index_;
    if (follow)
        g_browser_keys_won = true;
    // The entries: a grid of thumbnails or the list, All / Recent / Favourites (level_editor_thumbs.h).
    const int hovered =
        thumbs::items_panel(model, g_browser_search, follow, -buttons, g_mono, g_small, g_scale, &g_requests);
    // The preview follows the mouse over the list, or the keys when they moved the highlight since.
    if (hovered != g_browser_hovered) {
        g_browser_hovered = hovered;
        if (hovered >= 0)
            g_browser_keys_won = false;
    }
    const int preview = g_browser_hovered >= 0 && !g_browser_keys_won ? g_browser_hovered : -1;
    if (preview != g_preview_asked) {
        g_preview_asked = preview;
        get_ask(Request::Kind::kBrowserPreview, preview);
    }
    ImGui::BeginDisabled(items.empty());
    if (ImGui::Button("Place"))
        get_ask(Request::Kind::kBrowserPlace);
    ImGui::SameLine();
    if (ImGui::Button("Focus"))
        get_ask(Request::Kind::kBrowserFocus);
    arrange::browser_extras(g_scale); // (UX7: Replace selection, Click places - level_editor_arrange.h)
    ImGui::EndDisabled();
    ImGui::PushFont(g_small);
    get_muted("Enter: place at the cursor");
    ImGui::PopFont();
    ImGui::End();
    if (!open)
        get_ask(Request::Kind::kToggleBrowser);
}

void autosave_offer(const ShapeMesh& model)
{
    if (!model.offer || g_offer_closed)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, g_layout.viewport.y + 24.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::Begin("Recover Autosave", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextUnformatted("An autosave is newer than the saved layout.");
    if (ImGui::Button("Restore"))
        get_ask(Request::Kind::kRestoreAutosave);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Restore Autosave  (Ctrl+R)");
    ImGui::SameLine();
    if (ImGui::Button("Discard"))
        get_ask(Request::Kind::kDiscardAutosave);
    ImGui::SameLine();
    if (ImGui::Button("Later"))
        g_offer_closed = true;
    ImGui::End();
}

// A spawn, crown or flag moved in a level shared with THUGPRO: said once a run (the edit is made).
void online_warning(const ShapeMesh& model)
{
    if (!model.nodes.online_warning_)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, g_layout.viewport.y + 24.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(460.0f * g_scale, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.18f, 0.37f, 0.6f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.18f, 0.37f, 0.6f, 1.0f));
    ImGui::Begin("Shared Level", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor(2);
    ImGui::TextWrapped("This level (ported from %s) is shared with THUGPRO. Changing spawns, crowns or flags makes "
                       "it differ from other players' copies. Layouts are not used online.",
                       model.nodes.shared_game_);
    ImGui::Spacing();
    if (ImGui::Button("OK"))
        get_ask(Request::Kind::kOnlineWarningSeen);
    ImGui::End();
}

// The Place palette (N): what can be added. A click on an entry arms it - the next click in the view places it there
// (Shift+click: keep placing); its button places one where the view looks.
char g_place_search[64];
void get_place(const ShapeMesh& model)
{
    if (!model.place.open)
        return;
    ImGui::SetNextWindowPos(ImVec2(g_layout.viewport.x + 8.0f * g_scale, g_layout.viewport.y + 8.0f * g_scale),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300.0f * g_scale, 0.0f));
    bool open = true;
    ImGui::Begin("Place Node###place", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##placesearch", "Search\xE2\x80\xA6", g_place_search, sizeof(g_place_search));
    const std::vector<ShapeMesh::PlaceView::TableRow>& entries = model.place.entries;
    // (a list that scrolls: the level's peds, cars, sets, particles and sounds make it long - level_editor_entities.h)
    const float list_height =
        std::min(static_cast<float>(entries.size()) * ImGui::GetTextLineHeightWithSpacing() + 8.0f,
                 std::max(160.0f * g_scale, g_layout.viewport.height * 0.6f));
    ImGui::BeginChild("##placelist", ImVec2(0.0f, list_height));
    for (int i = 0; std::cmp_less(i, entries.size()); ++i) {
        const ShapeMesh::PlaceView::TableRow& e = entries[i];
        if (!get_matches(e.label, g_place_search))
            continue;
        ImGui::PushID(i);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(pos.x, pos.y + h * 0.25f), ImVec2(pos.x + h * 0.5f, pos.y + h * 0.75f), get_colour(e.colour), 2.0f);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + h * 0.8f, pos.y));
        const bool can = e.why.empty();
        ImGui::BeginDisabled(!can);
        if (ImGui::Selectable(e.label.c_str(), model.place.armed == i, ImGuiSelectableFlags_AllowDoubleClick)) {
            get_ask(Request::Kind::kPlaceArm, model.place.armed == i ? -1 : i);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                get_ask(Request::Kind::kPlaceHere, i);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", can ? "Click, then click in the view. Double-click: place at view center"
                                        : e.why.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::PushFont(g_small);
    if (model.place.armed >= 0 && std::cmp_less(model.place.armed, entries.size()))
        ImGui::TextWrapped("Click to place %s. Shift+click: place more. Esc: cancel.",
                           entries[model.place.armed].label.c_str());
    else
        get_muted("Choose an item, then click in the view.");
    ImGui::PopFont();
    ImGui::End();
    if (!open)
        get_ask(Request::Kind::kPlaceToggle);
}

// Reset level: asked first.
void confirm_reset(const ShapeMesh& model)
{
    if (!model.confirm_reset_)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, g_layout.viewport.y + 60.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(460.0f * g_scale, 0.0f));
    ImGui::Begin("Reset Level###confirmreset", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextWrapped("Revert all changes to %s and delete its saved layout and autosave?",
                       model.level_title_.c_str());
    ImGui::TextDisabled("Undo restores the changes until you leave the level.");
    ImGui::Spacing();
    if (ImGui::Button("Cancel"))
        get_ask(Request::Kind::kResetCancelled);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.14f, 0.13f, 1.0f));
    if (ImGui::Button("Reset level"))
        get_ask(Request::Kind::kResetConfirmed);
    ImGui::PopStyleColor();
    ImGui::End();
}

// Deleting a level's node: hidden, not removed - asked first.
void confirm_hide(const ShapeMesh& model)
{
    if (!model.confirm.open)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, g_layout.viewport.y + 60.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(420.0f * g_scale, 0.0f));
    ImGui::Begin("Hide Original Node###confirmhide", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::PushFont(g_mono);
    ImGui::TextUnformatted(model.confirm.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextWrapped("belongs to %s.", model.level.c_str());
    ImGui::TextWrapped("%s", model.confirm.crown ? "Original nodes are hidden, not deleted. Later crowns move up one."
                                                 : "Original nodes are hidden, not deleted. Revert shows it again.");
    ImGui::Spacing();
    if (ImGui::Button("Cancel"))
        get_ask(Request::Kind::kHideCancelled);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.14f, 0.13f, 1.0f));
    const std::string hide = "Hide " + model.confirm.family;
    if (ImGui::Button(hide.c_str()))
        get_ask(Request::Kind::kHideConfirmed);
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "editor_ui", 1) != 0;
    return on;
}

bool underway() { return g_running; }

int preview_pixels()
{
    const float scale = g_scale > 0.0f ? g_scale : 1.0f;
    // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
    return std::clamp(static_cast<int>(256.0f * scale + 0.5f), 256, 512);
}

bool kickoff()
{
    if (g_running)
        return true;
    if (!is_switched_on())
        return false;
    if (!g_created) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // (nothing written to the game folder)
        io.LogFilename = nullptr;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        g_created = true;
    }
    if (!ui_backend::prepare_all()) {
        get_log("level editor UI: not started - the old HUD instead");
        return false;
    }
    g_running = true;
    g_scale = 0.0f; // (the fonts made for the window's size on the first frame)
    g_requests.clear();
    g_offer_closed = false;
    g_ui_buttons = g_editor_buttons = 0;
    g_want_mouse = g_want_keys = false;
    g_browser_shown_index = -1;
    g_browser_hovered = g_preview_asked = -1;
    g_browser_keys_won = false;
    return true;
}

void halt()
{
    g_running = false;
    g_ui_buttons = g_editor_buttons = 0;
    g_want_mouse = g_want_keys = false;
}

void device_lost() { ui_backend::invalidate_device_objects(); }

bool route_event(const SDL_Event* event, bool editor_capture)
{
    if (!g_running)
        return true;
    auto route = [&](float x, float y) {
        g_mouse_x = x;
        g_mouse_y = y;
        return ui_layout::decide(
            {g_want_mouse, g_want_keys, ui_layout::on_panel(g_layout, x, y), editor_capture || g_editor_buttons != 0});
    };
    switch (event->type) {
    case SDL_MOUSEMOTION: {
        const ui_layout::Route r = route(static_cast<float>(event->motion.x), static_cast<float>(event->motion.y));
        if (r.mouse_to_ui_ || g_ui_buttons)
            ui_backend::process_event(event);
        return true; // (the editor keeps the position and the look's movement; mouse_to_editor says if it may pick)
    }
    case SDL_MOUSEBUTTONDOWN: {
        const uint32_t bit = 1u << (event->button.button & 31);
        const ui_layout::Route r = route(static_cast<float>(event->button.x), static_cast<float>(event->button.y));
        if (!r.mouse_to_editor_) {
            g_ui_buttons |= bit;
            ui_backend::process_event(event);
            return false;
        }
        g_editor_buttons |= bit;
        if (r.mouse_to_ui_)
            ui_backend::process_event(event); // (ImGui loses its focus and hover; the editor has the click)
        return true;
    }
    case SDL_MOUSEBUTTONUP: {
        const uint32_t bit = 1u << (event->button.button & 31);
        if (g_ui_buttons & bit) {
            g_ui_buttons &= ~bit;
            ui_backend::process_event(event);
            return false;
        }
        g_editor_buttons &= ~bit;
        ui_backend::process_event(event); // (ImGui's button state stays true)
        return true;
    }
    case SDL_MOUSEWHEEL: {
        const ui_layout::Route r = route(g_mouse_x, g_mouse_y);
        if (!r.mouse_to_editor_) {
            ui_backend::process_event(event);
            return false;
        }
        return true;
    }
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    case SDL_TEXTINPUT:
    case SDL_TEXTEDITING:
        ui_backend::process_event(event);
        return !g_want_keys;
    default:
        ui_backend::process_event(event);
        return true;
    }
}

bool mouse_to_editor()
{
    if (!g_running)
        return true;
    return ui_layout::decide(
               {g_want_mouse, g_want_keys, ui_layout::on_panel(g_layout, g_mouse_x, g_mouse_y), g_editor_buttons != 0})
        .mouse_to_editor_;
}

bool keys_to_editor() { return !g_running || !g_want_keys; }

void push_request(const Request& request) { g_requests.push_back(request); }

std::vector<Request> take_requests()
{
    std::vector<Request> requests;
    requests.swap(g_requests);
    return requests;
}

void tick_cycle(const ShapeMesh& model, const Overlay& overlay)
{
    if (!g_running)
        return;
    update_scale();
    ui_backend::new_frame();
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();
    g_layout = ui_layout::compute(io.DisplaySize.x, io.DisplaySize.y, g_scale, true, true);
    // The lines and gizmos under the panels (back buffer pixels to the window's).
    int back_width = 0, back_height = 0;
    ui_backend::back_buffer_size(&back_width, &back_height);
    const float sx = back_width > 0 ? io.DisplaySize.x / static_cast<float>(back_width) : 1.0f;
    const float sy = back_height > 0 ? io.DisplaySize.y / static_cast<float>(back_height) : 1.0f;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    for (const Overlay::Triangle& t : overlay.under)
        background->AddTriangleFilled(ImVec2(t.ax * sx, t.ay * sy), ImVec2(t.bx * sx, t.by * sy),
                                      ImVec2(t.cx * sx, t.cy * sy), get_colour(t.argb));
    for (const Seg& line : overlay.lines)
        background->AddLine(ImVec2(line.ax * sx, line.ay * sy), ImVec2(line.bx * sx, line.by * sy),
                            get_colour(line.argb), line.thickness * g_scale);
    for (const Overlay::Triangle& t : overlay.triangles)
        background->AddTriangleFilled(ImVec2(t.ax * sx, t.ay * sy), ImVec2(t.bx * sx, t.by * sy),
                                      ImVec2(t.cx * sx, t.cy * sy), get_colour(t.argb));
    for (const Overlay::Circle& c : overlay.circles) {
        if (c.filled)
            background->AddCircleFilled(ImVec2(c.x * sx, c.y * sy), c.radius * sx, get_colour(c.argb));
        else
            background->AddCircle(ImVec2(c.x * sx, c.y * sy), c.radius * sx, get_colour(c.argb), 0,
                                  c.thickness * g_scale);
    }
    if (g_mono) {
        for (const Overlay::OverlayText& t : overlay.texts) {
            const ImVec2 at(t.x * sx, t.y * sy);
            const ImVec2 size = g_mono->CalcTextSizeA(g_mono->FontSize, 1e9f, 0.0f, t.text.c_str());
            background->AddRectFilled(ImVec2(at.x - 3.0f, at.y - 1.0f),
                                      ImVec2(at.x + size.x + 3.0f, at.y + size.y + 1.0f), IM_COL32(16, 17, 19, 170),
                                      3.0f);
            background->AddText(g_mono, g_mono->FontSize, at, get_colour(t.argb), t.text.c_str());
        }
    }
    ImGui::PushFont(g_ui);
    {
        ux3::Fonts fonts;
        fonts.ui = g_ui;
        fonts.small = g_small;
        fonts.mono = g_mono;
        fonts.heading = g_heading;
        fonts.scale = g_scale;
        ux3::set_fonts(fonts);
        ux3::Settings settings;
        settings.snap_always_ = model.snap_always_;
        settings.carry = model.carry;
        settings.collision = model.view.collision;
        settings.pick_filter_ = model.pick_filter_;
        settings.can_undo_ = model.can_undo_;
        settings.can_redo_ = model.can_redo_;
        ux3::note_settings(settings);
    }
    get_toolbar(model);
    get_outliner(model);
    get_inspector(model);
    status_bar(model);
    get_browser(model);
    get_help(model);
    autosave_offer(model);
    get_place(model);
    confirm_hide(model);
    confirm_reset(model);
    collision_legend(model);
    online_warning(model);
    prefs::pane(g_scale, g_heading, g_small); // (level_editor_prefs.h)
    prefs::tips_window(g_scale, g_heading, g_mono);
    play_test::pane(g_scale, g_heading, g_small); // (level_editor_playtest.h)
    // (UX7: the Arrange window, the adjust-last panel - level_editor_arrange.h, level_editor_adjust.h)
    arrange::pane(g_scale);
    adjust::get_panel(g_layout.viewport.x, g_layout.viewport.y + g_layout.viewport.height, g_scale);
    triggers::pane(g_layout.viewport.x, g_layout.viewport.y, g_layout.viewport.width, g_layout.viewport.height, g_scale,
                   model.nodes.selected);
    // (UX5: the Problems window, the selected note's window)
    checks::pane(g_layout.viewport.x, g_layout.viewport.y, g_layout.viewport.width, g_layout.viewport.height, g_scale,
                 g_mono);
    notes::pane(g_layout.viewport.x, g_layout.viewport.y, g_layout.viewport.width, g_layout.viewport.height, g_scale);
    // (UX9: the gap wizard's and the ground tool's forms, the skateability legend - level_editor_ux9.h)
    ux9::get_windows(g_layout.viewport.x, g_layout.viewport.y, g_layout.viewport.width, g_layout.viewport.height,
                     g_scale);
    ux3::get_windows(model); // (the search, history, changes, keys, snapshots - level_editor_ux3.h)
    ux8::get_windows(g_layout.viewport.x, g_layout.viewport.y, g_layout.viewport.width, g_layout.viewport.height,
                     ux8_fonts()); // (UX8: the pie, the crosshair - level_editor_ux8.h)
    ux3::remember_sections();      // (the Inspector's sections folded as last time)
    ImGui::PopFont();
    ImGui::Render();
    ui_backend::render();
    g_want_mouse = io.WantCaptureMouse;
    g_want_keys = io.WantCaptureKeyboard;
}

} // namespace frontend::level_editor::ui
