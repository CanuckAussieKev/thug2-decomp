// NOT ORIGINAL - the level editor's UI (ours). See level_editor_ui.h. Only ImGui's own API here: the platform and the
// renderer are level_editor_ui_backend.cpp's.

#include "game/front/level_editor_ui.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_scale.h"
#include "game/front/level_editor_ui_backend.h"
#include "game/front/level_editor_ui_fonts.h"
#include "game/front/level_editor_ui_layout.h"
#include "game/front/level_editor_view.h"
#include "hook/log.h"
#include "imgui.h"

namespace Front::LevelEditor::Ui {

namespace {

bool g_created;  // (the ImGui context, made once)
bool g_running;  // (between Start and Stop)
float g_scale;   // (the fonts' and the style's scale; 0: not built)
ImFont* g_ui;
ImFont* g_small;
ImFont* g_mono;
ImFont* g_heading;

// The routing (level_editor_ui_layout.h): the last frame's layout and ImGui's wishes, the mouse, and which mouse
// buttons a press gave to the UI / to the editor (their release goes the same way).
UiLayout::Layout g_layout;
bool g_wantMouse, g_wantKeys;
float g_mouseX = -1.0f, g_mouseY = -1.0f;
uint32_t g_uiButtons, g_editorButtons;

std::vector<Request> g_requests;
bool g_offerClosed;
char g_outlinerSearch[96];
char g_browserSearch[96];
int g_browserShownIndex = -1;
// The preview's entry: the list's row under the mouse (-1: none) and whether the keys moved the highlight since the
// mouse last moved onto a row (then the highlighted one shows); the entry asked for last.
int g_browserHovered = -1;
bool g_browserKeysWon;
int g_previewAsked = -1;

void Ask(Request::Kind kind, int value = 0, uint32_t id = 0) { g_requests.push_back({kind, value, id}); }

// ARGB (the editor's colours) to ImGui's packed colour.
ImU32 Colour(uint32_t argb)
{
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

// --- fonts and style ---

// The characters the editor shows: Latin-1, dashes and bullets, arrows and triangles.
const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2013, 0x2026, 0x2190, 0x2193, 0x25B2, 0x25C0, 0};

ImFont* AddFont(UiFonts::CompressedFont font, float size)
{
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    std::snprintf(config.Name, sizeof(config.Name), "%.0fpx", size);
    return ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(font.data, font.size, size, &config, kRanges);
}

void ApplyStyle(float scale)
{
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 5.0f);
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 12.0f;
    // A dark neutral with the orange accent of the plan's mockup.
    ImVec4* c = style.Colors;
    const ImVec4 accent(0.82f, 0.34f, 0.11f, 1.0f), accentSoft(0.82f, 0.34f, 0.11f, 0.55f);
    c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.115f, 0.125f, 0.94f);
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.125f, 0.135f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.25f, 0.26f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.19f, 0.2f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.25f, 0.27f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.29f, 0.31f, 1.0f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.095f, 0.1f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.2f, 0.21f, 0.225f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.29f, 0.31f, 1.0f);
    c[ImGuiCol_ButtonActive] = accentSoft;
    c[ImGuiCol_Header] = ImVec4(0.82f, 0.34f, 0.11f, 0.35f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.82f, 0.34f, 0.11f, 0.45f);
    c[ImGuiCol_HeaderActive] = accentSoft;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Separator] = c[ImGuiCol_Border];
    c[ImGuiCol_Text] = ImVec4(0.91f, 0.9f, 0.88f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.6f, 0.61f, 0.63f, 1.0f);
    c[ImGuiCol_NavCursor] = accent;
    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
}

// The fonts and the style for the window's size (made again when its scale changes: the font texture with them).
void UpdateScale()
{
    int width = 0, height = 0;
    UiBackend::WindowSize(&width, &height);
    float scale = UiLayout::ScaleFor(width, height);
    if (scale == g_scale)
        return;
    g_scale = scale;
    UiLayout::FontSizes sizes = UiLayout::FontSizesFor(scale);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->Clear();
    g_ui = AddFont(UiFonts::Inter(), sizes.ui);
    g_small = AddFont(UiFonts::Inter(), sizes.small);
    g_mono = AddFont(UiFonts::Mono(), sizes.mono);
    g_heading = AddFont(UiFonts::InterSemiBold(), sizes.heading);
    atlas->Build();
    UiBackend::InvalidateDeviceObjects(); // (the font texture is made again)
    ApplyStyle(scale);
    Log("level editor UI: scale %.2f for a %dx%d window (Inter %.0f px, JetBrains Mono %.0f px)", scale, width,
        height, sizes.ui, sizes.mono);
}

// --- small pieces ---

void Mono(const char* format, ...)
{
    ImGui::PushFont(g_mono);
    va_list args;
    va_start(args, format);
    ImGui::TextV(format, args);
    va_end(args);
    ImGui::PopFont();
}

void Muted(const char* text) { ImGui::TextDisabled("%s", text); }

// A key in a key cap, then what it does.
void KeyHint(const char* key, const char* what)
{
    ImGui::PushFont(g_mono);
    ImVec2 size = ImGui::CalcTextSize(key);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float padX = 4.0f * g_scale, padY = 1.0f * g_scale;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y - padY),
                                              ImVec2(pos.x + size.x + 2.0f * padX, pos.y + size.y + padY),
                                              IM_COL32(60, 63, 68, 255), 3.0f * g_scale);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + padX, pos.y));
    ImGui::TextUnformatted(key);
    ImGui::PopFont();
    ImGui::SameLine(0.0f, padX + 4.0f * g_scale);
    ImGui::PushFont(g_small);
    ImGui::TextUnformatted(what);
    ImGui::PopFont();
}

bool Matches(const std::string& text, const char* filter)
{
    if (!filter[0])
        return true;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    std::string a = text, b = filter;
    std::transform(a.begin(), a.end(), a.begin(), lower);
    std::transform(b.begin(), b.end(), b.begin(), lower);
    return a.find(b) != std::string::npos;
}

constexpr ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;

void PlaceWindow(const UiLayout::Rect& rect)
{
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
    ImGui::SetNextWindowSize(ImVec2(rect.width, rect.height));
}

void PanelTitle(const char* title)
{
    ImGui::PushFont(g_heading);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Separator();
}

// --- the view options (level_editor_view.h) ---

// The toolbar's Collision (C) and, while pieces are hidden in the editor, Show all.
void ViewToolbar(const Model& model)
{
    ImGui::SameLine(0.0f, 10.0f * g_scale);
    bool collision = model.view.collision;
    if (ImGui::Checkbox("Collision (C)", &collision))
        Ask(Request::kCollision, collision ? 1 : 0);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shows the level's collision near the camera, coloured by surface (the legend: bottom left);\n"
                          "invisible walls in magenta - Shift+click one to see what it is");
    if (model.view.hidden > 0) {
        ImGui::SameLine();
        char label[48];
        std::snprintf(label, sizeof(label), "Show all (%d hidden)", model.view.hidden);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.30f, 0.62f, 0.85f));
        if (ImGui::SmallButton(label))
            Ask(Request::kViewShowAll);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Alt+H: everything hidden in the editor (H, I) shown again - hiding there is only while "
                              "editing:\nnever saved, never in play");
    }
}

// The collision's legend, over the bottom left of the view while it's shown.
void CollisionLegend(const Model& model)
{
    if (!model.view.collision)
        return;
    const UiLayout::Rect& view = g_layout.viewport;
    ImGui::SetNextWindowPos(ImVec2(view.x + 8.0f * g_scale, view.y + view.height - 8.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::Begin("##collisionLegend", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PushFont(g_small);
    ImGui::TextUnformatted("Collision (C)");
    for (int i = 0; i < static_cast<int>(View::Surface::kCount); ++i) {
        auto surface = static_cast<View::Surface>(i);
        ImVec2 at = ImGui::GetCursorScreenPos();
        float side = ImGui::GetTextLineHeight();
        uint32_t argb = View::SurfaceColour(surface) | 0xFF000000;
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + side, at.y + side), Colour(argb), 2.0f);
        ImGui::Dummy(ImVec2(side, side));
        ImGui::SameLine();
        ImGui::TextUnformatted(View::SurfaceName(surface));
    }
    ImGui::TextDisabled("%d faces in %d pieces (%d invisible walls)%s", model.view.triangles, model.view.sectors,
                        model.view.walls, model.view.capped ? " - the nearest only" : "");
    ImGui::TextDisabled("Not coloured: 0x80 camera passes, 0x2000 not taggable");
    ImGui::PopFont();
    ImGui::End();
}

// The Inspector's part: an invisible wall selected (Shift+click), and what's hidden in the editor.
void ViewSection(const Model& model)
{
    if (model.view.wall) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PushFont(g_heading);
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.9f, 1.0f), "%s", model.view.wallName.c_str());
        ImGui::PopFont();
        Muted("invisible wall");
        ImGui::SameLine();
        Mono("0x%08X", model.view.wall);
        ImGui::PushFont(g_mono);
        ImGui::TextWrapped("%s", model.view.wallInfo.c_str());
        ImGui::PopFont();
        ImGui::TextWrapped("Collision without a drawn piece can't be moved or hidden yet: the editor's moves, undo and "
                           "layouts go by a piece's geometry.");
        if (ImGui::Button("Close##wall"))
            Ask(Request::kClearWall);
    }
    if (model.view.canHide || model.view.hidden > 0) {
        ImGui::Spacing();
        ImGui::Separator();
        Muted("While editing only (never saved, never in play)");
        ImGui::BeginDisabled(!model.view.canHide);
        if (ImGui::Button("Hide (H)"))
            Ask(Request::kViewHide);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hides the selection from the view and from clicks while editing");
        ImGui::SameLine();
        if (ImGui::Button("Isolate (I)"))
            Ask(Request::kViewIsolate);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hides everything else (scenery and objects) while editing");
        ImGui::EndDisabled();
        if (model.view.hidden > 0) {
            ImGui::SameLine();
            if (ImGui::Button("Show all (Alt+H)"))
                Ask(Request::kViewShowAll);
            ImGui::TextDisabled("%d hidden in the editor", model.view.hidden);
        }
    }
}

// --- the panels ---

void Toolbar(const Model& model)
{
    PlaceWindow(g_layout.toolbar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * g_scale, 5.0f * g_scale));
    ImGui::Begin("##toolbar", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PushFont(g_heading);
    ImGui::TextColored(ImVec4(0.94f, 0.48f, 0.24f, 1.0f), "LEVEL EDITOR");
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 16.0f * g_scale);
    // The mode (what the mouse and keys do now).
    const char* modes[] = {"Fly", "Select", "Move", "Add"};
    int mode = std::strcmp(model.mode, "ADD") == 0 ? 3 : std::strncmp(model.mode, "MOVING", 6) == 0 ? 2
             : std::strcmp(model.mode, "SELECTED") == 0 ? 1 : 0;
    for (int i = 0; i < 4; ++i) {
        bool on = i == mode;
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.34f, 0.11f, 0.85f));
        ImGui::SmallButton(modes[i]);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The mode follows what you do: fly (nothing selected), select (a click), move (drag), "
                              "add (B)");
        if (on)
            ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 2.0f * g_scale);
    }
    ImGui::SameLine(0.0f, 14.0f * g_scale);
    bool snap = model.snapAlways;
    if (ImGui::Checkbox("Snap 25", &snap))
        Ask(Request::kSnapAlways, snap ? 1 : 0);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Moves snap to a 25-unit grid (Ctrl while moving does the same)");
    // What a click in the view may take (level_editor_picking.h; [editor] pick= in the ini).
    ImGui::SameLine(0.0f, 12.0f * g_scale);
    ImGui::TextDisabled("Pick:");
    static const struct {
        const char* label;
        uint32_t bit;
        const char* tip;
    } kPick[] = {{"Nodes", Picking::kNodes, "Clicks take nodes' gizmos (spawns, crowns, lights...)"},
                 {"Objects", Picking::kObjects, "Clicks take objects (level objects, peds, cars)"},
                 {"Scenery", Picking::kScenery, "Clicks take scenery pieces (off: only the Outliner and the browser "
                                                "select them)"}};
    for (const auto& pick : kPick) {
        ImGui::SameLine(0.0f, 4.0f * g_scale);
        bool on = (model.pickFilter & pick.bit) != 0;
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.34f, 0.11f, 0.85f));
        if (ImGui::SmallButton(pick.label))
            Ask(Request::kPickFilter, 0, model.pickFilter ^ pick.bit);
        if (on)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", pick.tip);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(model.selection.objects == 0 && model.selection.sectors == 0 && !model.nodes.movable);
    if (ImGui::SmallButton("Ground (G)"))
        Ask(Request::kDrop);
    ImGui::EndDisabled();
    ViewToolbar(model);
    ImGui::SameLine(0.0f, 14.0f * g_scale);
    // Which level: always its edited copy (level_editor_layout.h) - never the level as shipped.
    ImGui::TextColored(ImVec4(0.55f, 0.84f, 1.0f, 1.0f), "%s (edited copy)", model.levelTitle.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("You are editing the edited copy of %s: a level of its own - \"%s (EDITED)\" in the level\n"
                          "select (offline only). The level as shipped, and every network game, stay as they are.\n"
                          "Saved layout: %s. Scene %s.",
                          model.levelTitle.c_str(), model.levelTitle.c_str(),
                          model.editedSaved ? "yes" : "not yet (Ctrl+S makes it)", model.level.c_str());
    // Right: undo / redo / save, reset, help, add.
    float right = (ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x);
    float width =
        ImGui::CalcTextSize("Undo  Redo  Save layout  Reset level  Help (F1)  Add (B)  Place (N)").x + 98.0f * g_scale;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f * g_scale, right - width));
    ImGui::BeginDisabled(!model.canUndo);
    if (ImGui::SmallButton("Undo"))
        Ask(Request::kUndo);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!model.canRedo);
    if (ImGui::SmallButton("Redo"))
        Ask(Request::kRedo);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (model.unsaved)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.34f, 0.11f, 0.85f));
    if (ImGui::SmallButton("Save layout"))
        Ask(Request::kSave);
    if (model.unsaved)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ctrl+S: the edited copy's layout - the level select's (EDITED) entry loads it");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset level"))
        Ask(Request::kResetLevel);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ctrl+Shift+Delete: everything back as the level has it, its saved layout deleted (asks first)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Help (F1)"))
        Ask(Request::kToggleHelp);
    ImGui::SameLine();
    if (ImGui::SmallButton("Add (B)"))
        Ask(Request::kToggleBrowser);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copies of the level's scenery and objects");
    ImGui::SameLine();
    if (model.place.open)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.34f, 0.11f, 0.85f));
    if (ImGui::SmallButton("Place (N)"))
        Ask(Request::kPlaceToggle);
    if (model.place.open)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("New nodes: spawns, crowns, CTF flags, lights, sounds, proximity nodes");
    ImGui::End();
}

// The Outliner's rows: each node's label (made again when the node arrays change) and the families' node lists.
struct OutlinerCache {
    uint32_t generation = 0;
    std::vector<std::string> labels;
    std::vector<int> byFamily[Nodes::kFamilies];
};
OutlinerCache g_cache;
int g_lastSelected = -1;

void UpdateCache()
{
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    if (g_cache.generation == Nodes::Generation() && g_cache.labels.size() == nodes.size())
        return;
    g_cache.generation = Nodes::Generation();
    g_cache.labels.clear();
    for (std::vector<int>& list : g_cache.byFamily)
        list.clear();
    for (size_t i = 0; i < nodes.size(); ++i) {
        g_cache.labels.push_back(nodes[i].name ? Nodes::Label(nodes[i].name) : "(no name)");
        g_cache.byFamily[static_cast<int>(nodes[i].family)].push_back(static_cast<int>(i));
    }
}

// What a row shows after the name: a spawn's types, a crown's index, a flag's colour, a rail's links.
std::string RowTagOf(const Nodes::Node& node);
std::string RowTag(const Nodes::Node& node)
{
    std::string tag = RowTagOf(node);
    if (node.hidden)
        return tag.empty() ? "hidden" : tag + " hidden";
    if (node.added)
        return tag.empty() ? "added" : tag + " added";
    return tag;
}
std::string RowTagOf(const Nodes::Node& node)
{
    char text[48] = "";
    switch (node.family) {
    case Nodes::Family::Spawn: {
        std::string tags;
        auto add = [&](uint32_t bit, const char* tag) {
            if (node.restartTypes & bit)
                tags += tags.empty() ? tag : std::string(" ") + tag;
        };
        add(Nodes::kPlayer1, "P1");
        add(Nodes::kPlayer2, "P2");
        add(Nodes::kMultiplayer, "MP");
        add(Nodes::kHorse, "H");
        add(Nodes::kCtf, "CTF");
        add(Nodes::kTeam, "Team");
        add(Nodes::kKing, "KotH");
        return tags;
    }
    case Nodes::Family::Crown:
        if (node.order < 0)
            return "";
        std::snprintf(text, sizeof(text), "#%d", node.order);
        return text;
    case Nodes::Family::Rail:
    case Nodes::Family::Path:
    case Nodes::Family::Ladder:
        if (node.links.empty())
            return "end";
        return node.links.size() == 1 ? "" : std::to_string(node.links.size()) + " links";
    default:
        return "";
    }
}

void FamilyChips(const Model& model)
{
    ImGui::PushFont(g_small);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f * g_scale, 2.0f * g_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * g_scale, 4.0f * g_scale));
    const int* counts = Nodes::Counts();
    float right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    for (int f = 0; f < Nodes::kFamilies; ++f) {
        auto family = static_cast<Nodes::Family>(f);
        if (!counts[f])
            continue;
        bool on = (model.nodes.families & Nodes::Bit(family)) != 0;
        const char* name = Nodes::FamilyName(family);
        float width = ImGui::CalcTextSize(name).x + 2.0f * ImGui::GetStyle().FramePadding.x;
        if (ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x + 1.0f) {
            ImGui::SameLine();
            if (ImGui::GetCursorPosX() + width > right)
                ImGui::NewLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.82f, 0.34f, 0.11f, 0.75f) : ImVec4(0.2f, 0.21f, 0.225f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.6f, 0.61f, 0.63f, 1.0f));
        if (ImGui::SmallButton(name))
            Ask(Request::kFamilies, 0, model.nodes.families ^ Nodes::Bit(family));
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d %s - click to %s them (kept in thug2-decomp.ini)", counts[f], name, on ? "hide" : "show");
    }
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
}

void Outliner(const Model& model)
{
    if (!g_layout.outlinerShown)
        return;
    UpdateCache();
    PlaceWindow(g_layout.outliner);
    ImGui::Begin("Outliner", nullptr, kPanel);
    PanelTitle("Outliner");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "Search nodes\xE2\x80\xA6", g_outlinerSearch, sizeof(g_outlinerSearch));
    FamilyChips(model);
    ImGui::Separator();
    ImGui::BeginChild("##tree");
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    if (nodes.empty())
        Muted("No nodes (no level loaded).");
    int selected = model.nodes.selected;
    bool follow = selected != g_lastSelected && selected >= 0; // (picked in the view: shown here)
    g_lastSelected = selected;
    static std::vector<int> shown;
    for (int f = 0; f < Nodes::kFamilies; ++f) {
        auto family = static_cast<Nodes::Family>(f);
        const std::vector<int>& members = g_cache.byFamily[f];
        if (members.empty() || !(model.nodes.families & Nodes::Bit(family)))
            continue;
        shown.clear();
        for (int i : members) {
            if (Matches(g_cache.labels[i], g_outlinerSearch))
                shown.push_back(i);
        }
        if (shown.empty())
            continue;
        bool holdsSelected = follow && selected < static_cast<int>(nodes.size()) && nodes[selected].family == family;
        if (holdsSelected || g_outlinerSearch[0])
            ImGui::SetNextItemOpen(true, holdsSelected ? ImGuiCond_Always : ImGuiCond_Once);
        char header[64];
        std::snprintf(header, sizeof(header), "%s###family%d", Nodes::FamilyName(family), f);
        bool open = ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_SpanAvailWidth);
        ImGui::SameLine();
        ImGui::PushFont(g_mono);
        ImGui::TextDisabled("%d", static_cast<int>(shown.size()));
        ImGui::PopFont();
        if (!open)
            continue;
        ImGui::PushFont(g_mono);
        // (rows the clipper skips aren't laid out; the selected one is brought into view once)
        int scrollTo = -1;
        if (holdsSelected) {
            auto it = std::find(shown.begin(), shown.end(), selected);
            if (it != shown.end())
                scrollTo = static_cast<int>(it - shown.begin());
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        if (scrollTo >= 0)
            clipper.IncludeItemByIndex(scrollTo);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                int i = shown[row];
                const Nodes::Node& node = nodes[i];
                ImGui::PushID(i);
                bool isSelected = i == selected;
                // (the gizmo's colour as a small swatch)
                ImVec2 pos = ImGui::GetCursorScreenPos();
                float h = ImGui::GetTextLineHeight();
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + h * 0.3f),
                                                          ImVec2(pos.x + h * 0.4f, pos.y + h * 0.7f),
                                                          Colour(node.colour), 2.0f);
                ImGui::SetCursorScreenPos(ImVec2(pos.x + h * 0.6f, pos.y));
                if (node.hidden)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                bool clicked = ImGui::Selectable(g_cache.labels[i].c_str(), isSelected,
                                                 ImGuiSelectableFlags_AllowDoubleClick |
                                                     ImGuiSelectableFlags_SpanAllColumns);
                if (node.hidden)
                    ImGui::PopStyleColor();
                if (clicked) {
                    Ask(Request::kSelectNode, i);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        Ask(Request::kGoToNode, i);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click selects; double-click takes the camera there");
                std::string tag = RowTag(node);
                if (!tag.empty()) {
                    float width = ImGui::CalcTextSize(tag.c_str()).x;
                    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ScrollbarSize - width));
                    ImGui::TextColored(ImVec4(0.6f, 0.61f, 0.63f, 1.0f), "%s", tag.c_str());
                }
                if (row == scrollTo)
                    ImGui::SetScrollHereY(0.4f);
                ImGui::PopID();
            }
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }
    ImGui::EndChild();
    ImGui::End();
}

// --- the Inspector ---

// A row of the Inspector's table: a label, then whatever follows.
void FieldLabel(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    Muted(label);
    ImGui::TableNextColumn();
}

void Badge(const char* text, ImU32 colour)
{
    ImVec2 size = ImGui::CalcTextSize(text);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float pad = 4.0f * g_scale;
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x + 2.0f * pad, pos.y + size.y),
                                              colour, 3.0f * g_scale);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad, pos.y));
    ImGui::TextUnformatted(text);
    ImGui::SameLine(0.0f, pad + 4.0f * g_scale);
}

struct FieldsCache {
    int node = -1;
    uint32_t generation = 0;
    std::vector<Nodes::Field> fields;
};
FieldsCache g_fields;

void NodeSection(const Model& model, int index)
{
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    if (index < 0 || index >= static_cast<int>(nodes.size()))
        return;
    const Nodes::Node& node = nodes[index];
    if (model.nodes.nodeOnly) {
        ImGui::PushFont(g_heading);
        ImGui::TextWrapped("%s", node.name ? Nodes::Label(node.name).c_str() : "(no name)");
        ImGui::PopFont();
    } else {
        ImGui::Spacing();
        ImGui::SeparatorText("Its node");
    }
    std::string cls = node.cls ? Nodes::Label(node.cls) : "(no class)";
    if (node.type)
        cls += " \xC2\xB7 " + Nodes::Label(node.type);
    Muted(cls.c_str());
    ImGui::PushFont(g_mono);
    ImGui::TextDisabled("node %d in %s", node.index, Nodes::Label(Nodes::ArrayName(node)).c_str());
    ImGui::PopFont();
    if (node.added) {
        ImGui::PushFont(g_small);
        Badge("Added by the editor", IM_COL32(46, 122, 67, 200));
        ImGui::NewLine();
        ImGui::PopFont();
    }
    if (node.hidden) {
        ImGui::PushFont(g_small);
        Badge("Hidden", IM_COL32(110, 110, 110, 220));
        ImGui::NewLine();
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s",
                           "Hidden, not removed: the game skips it (its class is the editor's marker). Home shows it "
                           "again.");
        ImGui::PopFont();
    }
    // Online: spawns, crowns and flags in a level shared with THUGPRO.
    if (Nodes::Online(node.family) && model.nodes.levelShared) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.37f, 0.6f, 0.35f));
        ImGui::BeginChild("##online", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::PushFont(g_heading);
        ImGui::TextColored(ImVec4(0.56f, 0.71f, 0.92f, 1.0f), "Online");
        ImGui::PopFont();
        ImGui::TextWrapped("This level is ported from %s and shared with THUGPRO. Its %s decide where players start "
                           "and play in net games: moving one changes that against THUGPRO's players (the THUGPRO "
                           "register, tools/compat/thugpro). Layouts aren't used in net games - a layout baked into "
                           "the level's files would be.",
                           model.nodes.sharedGame, Nodes::FamilyName(node.family));
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    bool editable = model.nodes.nodeOnly && model.nodes.movable;
    if (ImGui::BeginTable("##node", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
        if (editable) {
            // Typed or dragged to scrub (Ctrl+click or a double-click types a number); one undo step a field.
            float pos[3] = {model.nodes.pos.x, model.nodes.pos.y, model.nodes.pos.z};
            float heading = NodeEdit::HeadingDegrees(model.nodes.angles);
            ImGui::PushFont(g_mono);
            FieldLabel("Position");
            ImGui::SetNextItemWidth(-FLT_MIN);
            bool moved = ImGui::DragFloat3("##pos", pos, 1.0f, 0.0f, 0.0f, "%.1f");
            bool done = ImGui::IsItemDeactivated();
            bool turned = false;
            if (!model.nodes.rail) {
                FieldLabel("Heading");
                ImGui::SetNextItemWidth(-FLT_MIN);
                turned = ImGui::DragFloat("##heading", &heading, 0.5f, 0.0f, 0.0f, "%.1f\xC2\xB0");
                done = done || ImGui::IsItemDeactivated();
            }
            ImGui::PopFont();
            if (model.nodes.rail) {
                // A rail point: alone, or its whole rail (the points linked to it) by as much (L).
                FieldLabel("Moves");
                bool whole = model.nodes.wholeRail;
                char label[64];
                std::snprintf(label, sizeof(label), "Whole %s (L) - %d point%s", model.nodes.chainNoun,
                              model.nodes.railPoints, model.nodes.railPoints == 1 ? "" : "s");
                if (ImGui::Checkbox(label, &whole))
                    Ask(Request::kWholeRail, whole ? 1 : 0);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("On: dragging, the arrows, nudging, G and the Position field move every point of\n"
                                      "this rail by as much. Off: this point only (the segments to it follow).");
            }
            if (moved || turned) {
                V3 angles = turned ? NodeEdit::WithHeading(model.nodes.angles, heading) : model.nodes.angles;
                Request request = {Request::kNodeSet};
                request.a[0] = pos[0];
                request.a[1] = pos[1];
                request.a[2] = pos[2];
                request.b[0] = angles.x;
                request.b[1] = angles.y;
                request.b[2] = angles.z;
                g_requests.push_back(request);
            }
            if (done)
                Ask(Request::kNodeSetDone);
            FieldLabel("Angles");
            Mono("%.3f, %.3f, %.3f", model.nodes.angles.x, model.nodes.angles.y, model.nodes.angles.z);
        } else if (node.hasPos) {
            FieldLabel("Position");
            Mono("x %10.1f\ny %10.1f\nz %10.1f", node.pos.x, node.pos.y, node.pos.z);
            FieldLabel("Angles");
            Mono("%.3f, %.3f, %.3f", node.angles.x, node.angles.y, node.angles.z);
            if (Nodes::ShapeOf(node.family) == Nodes::Shape::Point) {
                FieldLabel("Heading");
                Mono("%.1f\xC2\xB0", std::atan2(node.forward.x, node.forward.z) * 57.29578f);
            }
        }
        if (node.family == Nodes::Family::Spawn) {
            FieldLabel("Types");
            static const struct {
                uint32_t bit;
                const char* name;
            } kTypes[] = {{Nodes::kPlayer1, "Player1"}, {Nodes::kPlayer2, "Player2"}, {Nodes::kMultiplayer, "Multiplayer"},
                          {Nodes::kHorse, "Horse"},     {Nodes::kCtf, "CTF"},         {Nodes::kTeam, "Team"},
                          {Nodes::kKing, "KingOfTheHill"}, {Nodes::kGeneric, "Generic"}};
            ImGui::PushFont(g_small);
            bool any = false;
            for (const auto& t : kTypes) {
                if (node.restartTypes & t.bit) {
                    Badge(t.name, Colour((node.colour & 0x00FFFFFF) | 0x80000000));
                    any = true;
                }
            }
            if (!any)
                Muted("(none)");
            else
                ImGui::NewLine();
            ImGui::PopFont();
            FieldLabel("Order");
            if (node.hidden)
                Mono("not counted while hidden");
            else
                Mono("%d of %d restarts", node.order + 1, Nodes::Walked(Nodes::Family::Spawn));
        }
        if (node.family == Nodes::Family::Crown) {
            FieldLabel("Crown");
            if (node.hidden)
                Mono("not counted while hidden\n(%d crowns count)", Nodes::Walked(Nodes::Family::Crown));
            else
                Mono("#%d of %d (the order the network sends;\nmoving it keeps it)", node.order,
                     Nodes::Walked(Nodes::Family::Crown));
            if (model.nodes.nodeOnly && !node.hidden) {
                ImGui::BeginDisabled(!model.nodes.crownUp);
                if (ImGui::SmallButton("Move up"))
                    Ask(Request::kCrownMove, -1);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(node.added ? "Earlier among the crowns you added (the level's own come first)"
                                                 : "The level's own crowns keep the level's order: only crowns you "
                                                   "added can be reordered");
                ImGui::SameLine();
                ImGui::BeginDisabled(!model.nodes.crownDown);
                if (ImGui::SmallButton("Move down"))
                    Ask(Request::kCrownMove, 1);
                ImGui::EndDisabled();
            }
        }
        if (node.family == Nodes::Family::Light) {
            FieldLabel("Colour");
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(Colour(node.colour));
            ImGui::ColorButton("##colour", c, ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f * g_scale, 14.0f * g_scale));
            ImGui::SameLine();
            Mono("%u %u %u", (node.colour >> 16) & 0xFF, (node.colour >> 8) & 0xFF, node.colour & 0xFF);
            FieldLabel("Radius");
            Mono("%.0f inner, %.0f outer", node.innerRadius, node.radius);
        }
        if (node.family == Nodes::Family::Trigger && node.radius > 0.0f) {
            FieldLabel("Radius");
            Mono("%.0f", node.radius);
        }
        if (node.terrain) {
            FieldLabel("Terrain");
            Mono("%s", Nodes::Label(node.terrain).c_str());
        }
        if (!node.links.empty()) {
            FieldLabel("Links");
            ImGui::PushFont(g_mono);
            for (int link : node.links) {
                ImGui::PushID(link);
                if (ImGui::SmallButton(Nodes::Label(nodes[link].name).c_str()))
                    Ask(Request::kSelectNode, link);
                ImGui::PopID();
            }
            ImGui::PopFont();
        }
        ImGui::EndTable();
    }
    if (editable) {
        ImGui::PushFont(g_small);
        ImGui::TextDisabled("Shows in the game:");
        ImGui::TextWrapped("%s", model.nodes.whenApplied);
        if (model.nodes.moved)
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "Moved from where the level has it (Home puts it back).");
        ImGui::PopFont();
        if (ImGui::Button("Ground (G)"))
            Ask(Request::kDrop);
        ImGui::SameLine();
        ImGui::BeginDisabled(!model.nodes.moved && !model.nodes.hidden);
        if (ImGui::Button(model.nodes.hidden ? "Show again (Home)" : "Home"))
            Ask(Request::kReset);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (model.nodes.added || (model.nodes.canHide && !model.nodes.hidden)) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.14f, 0.13f, 1.0f));
            if (ImGui::Button(model.nodes.added ? "Delete (Del)" : "Hide (Del)"))
                Ask(Request::kDeleteNode);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(model.nodes.added ? "Removed (Ctrl+Z brings it back)"
                                                    : "A level's node is hidden, not removed (Home brings it back)");
            ImGui::SameLine();
        } else if (!model.nodes.added && !model.nodes.hidden && !model.nodes.whyNoHide.empty()) {
            ImGui::BeginDisabled(true);
            ImGui::Button("Hide (Del)");
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", model.nodes.whyNoHide.c_str());
            ImGui::SameLine();
        }
    }
    // Phase 4: a rail's / path's operations, a rail's terrain, a ped's / car's path (level_editor_chains_ui.cpp).
    if ((model.nodes.nodeOnly || model.chain.object) && model.chain.show) {
        ImGui::NewLine();
        ChainSection(model.chain, &g_requests);
    }
    if (ImGui::Button("Go to"))
        Ask(Request::kGoToNode, index);
    // Every field of its structure.
    if (g_fields.node != index || g_fields.generation != Nodes::Generation()) {
        g_fields.node = index;
        g_fields.generation = Nodes::Generation();
        g_fields.fields = Nodes::Fields(node);
    }
    ImGui::Spacing();
    char header[48];
    std::snprintf(header, sizeof(header), "All fields (%d)###fields", static_cast<int>(g_fields.fields.size()));
    if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("##fields", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::PushFont(g_mono);
            for (const Nodes::Field& field : g_fields.fields) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (field.included)
                    ImGui::TextDisabled("%s", field.name.c_str());
                else
                    ImGui::TextUnformatted(field.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", field.value.c_str());
            }
            ImGui::PopFont();
            ImGui::EndTable();
        }
        ImGui::PushFont(g_small);
        Muted("Greyed: from its class's shared structure (ncomp_...).");
        ImGui::PopFont();
    }
}

// The locks (level_editor_picking.h): each with Unlock.
void LockList(const Model& model)
{
    if (model.locks.empty())
        return;
    ImGui::Spacing();
    ImGui::Separator();
    char title[48];
    std::snprintf(title, sizeof(title), "Locked (%d) - clicks pass these###locks", static_cast<int>(model.locks.size()));
    if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
        return;
    for (size_t i = 0; i < model.locks.size(); ++i) {
        const Model::LockView& lock = model.locks[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("Unlock"))
            Ask(Request::kUnlock, static_cast<int>(lock.kind), lock.id);
        ImGui::SameLine();
        ImGui::PushFont(g_mono);
        ImGui::TextUnformatted(lock.label.c_str());
        ImGui::PopFont();
        ImGui::PopID();
    }
}

// The scenery selection's parts (its pieces, shadows marked, and the rail points carried along): each with x (removed
// from the selection, as Ctrl+click) and Keep only this.
void MemberList(const std::vector<Model::Selection::Member>& members)
{
    if (members.empty())
        return;
    ImGui::Spacing();
    Muted("Parts (x removes one, Ctrl+click in the view too)");
    float rows = static_cast<float>(std::min<size_t>(members.size(), 10));
    ImGui::BeginChild("##members", ImVec2(0.0f, rows * ImGui::GetFrameHeightWithSpacing() + 4.0f * g_scale), false);
    for (size_t i = 0; i < members.size(); ++i) {
        const Model::Selection::Member& member = members[i];
        ImGui::PushID(static_cast<int>(i));
        int rail = member.rail ? 1 : 0;
        if (ImGui::SmallButton("x"))
            Ask(Request::kMemberRemove, rail, member.id);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(member.rail ? "No longer carried along with the selection"
                                          : "Removed from the selection (stays where it is)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Keep only this"))
            Ask(Request::kMemberKeep, rail, member.id);
        ImGui::SameLine();
        ImGui::PushFont(g_mono);
        ImGui::TextUnformatted(member.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        if (member.rail)
            Muted("rail point");
        else if (member.shadow)
            Muted(("shadow  " + member.size).c_str());
        else
            Muted(member.size.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void Inspector(const Model& model)
{
    if (!g_layout.inspectorShown)
        return;
    PlaceWindow(g_layout.inspector);
    ImGui::Begin("Inspector", nullptr, kPanel);
    PanelTitle("Inspector");
    const Model::Selection& s = model.selection;
    if (model.nodes.nodeOnly) {
        NodeSection(model, model.nodes.selected);
    } else if (s.objects == 0 && s.sectors == 0) {
        ImGui::TextWrapped("Nothing selected - click something in the view (a gizmo, an object, scenery), or pick it "
                           "in the Outliner.");
    } else {
        ImGui::PushFont(g_heading);
        ImGui::TextWrapped("%s", s.name.c_str());
        ImGui::PopFont();
        Muted(s.kind.c_str());
        if (s.checksum) {
            ImGui::SameLine();
            Mono("0x%08X", s.checksum);
        }
        ImGui::Spacing();
        if (ImGui::BeginTable("##transform", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            Muted("Position");
            ImGui::TableNextColumn();
            Mono("x %10.1f\ny %10.1f\nz %10.1f", s.pos.x, s.pos.y, s.pos.z);
            if (s.hasHeading) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                Muted("Heading");
                ImGui::TableNextColumn();
                Mono("%.1f\xC2\xB0", s.headingDegrees);
            }
            if (s.scalable) {
                // Scenery's uniform scale (level_editor_scale.h): dragged to scrub, Ctrl+click to type; one undo step.
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                Muted("Scale");
                ImGui::TableNextColumn();
                float scale = s.scale;
                ImGui::PushFont(g_mono);
                ImGui::SetNextItemWidth(110.0f * g_scale);
                if (ImGui::DragFloat("##scale", &scale, 0.005f, Scale::kMin, Scale::kMax, "%.3fx",
                                     ImGuiSliderFlags_AlwaysClamp)) {
                    Request request = {Request::kScaleSet};
                    request.a[0] = scale;
                    g_requests.push_back(request);
                }
                if (ImGui::IsItemDeactivated())
                    Ask(Request::kScaleDone);
                ImGui::PopFont();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Uniform scale, %.2fx to %.0fx, about the middle of its bottom ([ / ]: 10%%, "
                                      "Shift: 1%%; Home: back to 1x)",
                                      Scale::kMin, Scale::kMax);
            }
            if (s.objects > 1 || s.sectors > 1) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                Muted("Selected");
                ImGui::TableNextColumn();
                Mono("%d", s.objects > 0 ? s.objects : s.sectors);
            }
            ImGui::EndTable();
        }
        MemberList(s.members);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", model.selected.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Ground (G)"))
            Ask(Request::kDrop);
        ImGui::SameLine();
        if (ImGui::Button("Deselect (Esc)"))
            Ask(Request::kDeselect);
        if (model.selectionLockable) {
            ImGui::SameLine();
            if (ImGui::Button(model.selectionLocked ? "Unlock (L)" : "Lock (L)"))
                Ask(Request::kToggleLock);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("A locked piece or object is passed by every click in the view (the Outliner still "
                                  "selects it); kept in the layout");
        }
        if (model.nodes.selected >= 0)
            NodeSection(model, model.nodes.selected);
    }
    ImGui::Spacing();
    ImGui::Separator();
    Muted("Under the cursor");
    const std::vector<Nodes::Node>& all = Nodes::All();
    if (model.nodes.hovered >= 0 && model.nodes.hovered < static_cast<int>(all.size())) {
        const Nodes::Node& node = all[model.nodes.hovered];
        ImGui::TextWrapped("%s (%s)", Nodes::Label(node.name).c_str(), Nodes::Label(node.cls).c_str());
    } else {
        ImGui::TextWrapped("%s", model.hover.c_str());
    }
    ViewSection(model);
    LockList(model);
    ImGui::End();
}

void StatusBar(const Model& model)
{
    PlaceWindow(g_layout.status);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * g_scale, 4.0f * g_scale));
    ImGui::Begin("##status", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PushFont(g_small);
    if (!model.message.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.5f, 1.0f), "%s", model.message.c_str());
    } else {
        for (size_t i = 0; i < model.keys.size(); ++i) {
            if (i)
                ImGui::SameLine(0.0f, 12.0f * g_scale);
            KeyHint(model.keys[i].first.c_str(), model.keys[i].second.c_str());
        }
    }
    ImGui::PopFont();
    // Right: the changes and the camera.
    char right[160];
    std::snprintf(right, sizeof(right), "%s%d changes  \xC2\xB7  %d moved  \xC2\xB7  (%.0f, %.0f, %.0f)  speed %.0f",
                  model.unsaved ? "unsaved  \xC2\xB7  " : "", model.changes, model.moved, model.camera.x,
                  model.camera.y, model.camera.z, model.speed);
    ImGui::PushFont(g_mono);
    float width = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f * g_scale, (ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x) - width));
    ImGui::TextDisabled("%s", right);
    ImGui::PopFont();
    ImGui::End();
}

void Help(const Model& model)
{
    if (!model.help)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760.0f * g_scale, io.DisplaySize.y * 0.85f));
    bool open = true;
    ImGui::Begin("Level editor - keys (F1 or Esc closes this)", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
    struct Row {
        const char* section;
        const char* keys;
        const char* what;
    };
    static const Row kRows[] = {
        {"Flying", "W A S D", "move"},
        {"", "Q / E (Space)", "down / up"},
        {"", "Shift", "faster"},
        {"", "Right mouse", "look (orbits the selection)"},
        {"", "Wheel", "forward / back (with a selection: nearer / further)"},
        {"", "- / =", "flying speed"},
        {"Selecting", "Click", "an object or scenery (scenery takes its group); floors, ceilings, walls you're "
                               "inside and see-through decals are passed - a click on only those deselects"},
        {"", "Click again", "(same spot) the next thing under the cursor - n of m in the status bar"},
        {"", "Shift+click", "adds / removes"},
        {"", "Alt+click, middle click", "one piece of scenery - any piece, floors and decals too (hold Alt to see it); "
                                       "on a selected piece: takes it out"},
        {"", "Ctrl+click", "on a part of the scenery selection: removes just that piece (a shadow too) - or, on its "
                           "gizmo, a rail point carried along; the Inspector lists the parts (x, Keep only this)"},
        {"", "Shadows", "a scenery group takes its shadows (the see-through decals on the floor under it: by name, "
                        "else by place) - they move, turn, scale and go back with it"},
        {"", "L", "locks the selection: clicks pass it (the Inspector lists the locks: Unlock); with nothing selected, "
                     "unlocks all"},
        {"", "Pick: (toolbar)", "what clicks may take: nodes, objects, scenery"},
        {"", "Esc, Backspace", "deselects"},
        {"", "A gizmo, an Outliner row", "a node: spawns, crowns, flags, lights, sounds, triggers, rails and other "
                                         "point nodes move as objects do; rail, ladder and path points too (particles don't yet)"},
        {"", "L", "with a rail, ladder or path point selected: it moves with its whole chain / by itself (the "
                  "Inspector's Whole rail / ladder / path)"},
        {"Chains", "N > Rail / Path", "draw a new rail or path: each click a point (snapped to an edge of what the "
                                     "cursor hits - Alt: not; near a free end: linked onto it), Enter or a right click "
                                     "finishes, Esc stops"},
        {"", "Insert", "(a rail / path point) a point halfway to the next"},
        {"", "Delete", "(a rail / path point) deleted, the points either side linked (a level's own: hidden)"},
        {"", "End", "(an end point) draw on from it"},
        {"", "J / U", "(a point) link this end to the next end clicked / unlink it from its next point"},
        {"", "Inspector", "a rail's terrain (whole rail); a ped's / car's path: Assign, then click a waypoint"},
        {"Moving", "Drag", "moves it (Shift: up / down; wheel: push / pull; Ctrl or Snap 25: snap to 25)"},
        {"", "Drag an arrow", "along that axis only"},
        {"", "W A S D / Q E", "nudge it"},
        {"", "Z / X", "turn (objects and nodes 15\xC2\xB0, scenery a quarter)"},
        {"", "[ / ]", "scale scenery down / up 10% (Shift: 1%) about its bottom, 0.25x to 4x - or the Inspector's Scale"},
        {"", "G", "drops it onto the ground"},
        {"", "Inspector", "a node's position and heading: drag to scrub, Ctrl+click to type"},
        {"Editing", "Ctrl+D", "duplicates"},
        {"", "Delete", "hides scenery (deletes a copy)"},
        {"", "Home", "puts it back where the level had it"},
        {"", "Ctrl+Z / Ctrl+Y", "undo / redo"},
        {"", "Ctrl+Shift+Delete", "Reset level: everything as the level has it, its saved layout deleted (asks; "
                                  "Ctrl+Z undoes it this session)"},
        {"Adding", "B", "the level's pieces and objects: Up / Down choose, Left / Right switch, F shows, Enter "
                        "places a copy"},
        {"Saving", "Ctrl+S", "the edited copy's layout - the level select's \"<LEVEL> (EDITED)\" loads it (offline "
                             "only); the level as shipped never changes"},
        {"", "Ctrl+R / Ctrl+Shift+R", "restore / discard an older autosave"},
        {"Leaving", "Esc", "first cancels a drag, closes a window or deselects; then leaves"},
        {"View", "C", "shows the collision near the camera, coloured by surface (the legend); invisible walls magenta"},
        {"", "Shift+click", "with the collision shown: an invisible wall - what it is (the Inspector)"},
        {"", "H", "hides the selection while editing (from the view and from clicks; never saved, never in play)"},
        {"", "I or /", "isolates the selection: everything else hidden while editing"},
        {"", "Alt+H", "shows everything hidden in the editor again"},
        {"", "O", "occlusion culling: off while editing (the level's occluders are placed for the skater's view and "
                   "hide distant things from the flying camera); O turns it on as in play"},
        {"Camera", "P", "writes where the camera is to the log (a menu backdrop view)"},
        {"Panels", "Mouse", "over a panel the UI takes the mouse; the keys stay the editor's unless you type"},
    };
    // (the columns' widths set, not fitted: a long key list would otherwise squeeze the explanations to a letter a
    // line - the keys wrap within theirs)
    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("##section", ImGuiTableColumnFlags_WidthFixed, 90.0f * g_scale);
        ImGui::TableSetupColumn("##keys", ImGuiTableColumnFlags_WidthFixed, 190.0f * g_scale);
        ImGui::TableSetupColumn("##what", ImGuiTableColumnFlags_WidthStretch);
        for (const Row& row : kRows) {
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
    ImGui::End();
    if (!open)
        Ask(Request::kToggleHelp);
}

// The browser's preview (level_editor_preview.h): the picture - a drag in it orbits, the wheel zooms - and what the
// entry is.
void BrowserPreview(const Model& model)
{
    const Model::PreviewView& preview = model.preview;
    float width = ImGui::GetContentRegionAvail().x;
    float side = std::min(width, 210.0f * g_scale);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - side) * 0.5f);
    ImGui::InvisibleButton("##preview", ImVec2(side, side));
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (preview.texture)
        draw->AddImage(static_cast<ImTextureID>(preview.texture), a, b);
    else
        draw->AddRectFilled(a, b, IM_COL32(42, 44, 48, 255));
    draw->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_Border));
    if (!preview.texture) {
        const char* text = preview.index < 0 ? "(nothing highlighted)" : "(no preview)";
        ImVec2 size = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2((a.x + b.x - size.x) * 0.5f, (a.y + b.y - size.y) * 0.5f),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    }
    ImGuiIO& io = ImGui::GetIO();
    float dx = 0.0f, dy = 0.0f, wheel = 0.0f;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        dx = io.MouseDelta.x;
        dy = io.MouseDelta.y;
    }
    if (ImGui::IsItemHovered())
        wheel = io.MouseWheel;
    if (dx != 0.0f || dy != 0.0f || wheel != 0.0f) {
        Request request{Request::kPreviewOrbit};
        request.a[0] = dx;
        request.a[1] = dy;
        request.a[2] = wheel;
        g_requests.push_back(request);
    }
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive() && preview.texture)
        ImGui::SetTooltip("Drag to turn it, the wheel to zoom");
    if (!preview.name.empty()) {
        ImGui::PushFont(g_mono);
        ImGui::TextWrapped("%s", preview.name.c_str());
        ImGui::PopFont();
        ImGui::PushFont(g_small);
        if (!preview.size.empty())
            Muted(preview.size.c_str());
        if (!preview.type.empty())
            Muted(preview.type.c_str());
        ImGui::PopFont();
    }
    ImGui::Separator();
}

void Browser(const Model& model)
{
    if (!model.browser || !model.browserItems)
        return;
    ImGui::SetNextWindowPos(ImVec2(g_layout.viewport.x + g_layout.viewport.width - 8.0f * g_scale,
                                   g_layout.viewport.y + 8.0f * g_scale),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(360.0f * g_scale, std::min(760.0f * g_scale, g_layout.viewport.height - 16.0f * g_scale)));
    bool open = true;
    char title[64];
    std::snprintf(title, sizeof(title), "Add  (%d)###browser", static_cast<int>(model.browserItems->size()));
    ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    BrowserPreview(model);
    for (int tab = 0; tab < 2; ++tab) {
        if (tab)
            ImGui::SameLine();
        bool on = model.browserTab == tab;
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.34f, 0.11f, 0.85f));
        if (ImGui::Button(tab == 0 ? "Scenery" : "Objects") && !on)
            Ask(Request::kBrowserTab, tab);
        if (on)
            ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter\xE2\x80\xA6", g_browserSearch, sizeof(g_browserSearch));
    float buttons = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    ImGui::BeginChild("##items", ImVec2(0.0f, -buttons), ImGuiChildFlags_Borders);
    ImGui::PushFont(g_mono);
    const std::vector<BrowserItem>& items = *model.browserItems;
    bool follow = model.browserIndex != g_browserShownIndex; // (the keys moved it: keep it in view)
    g_browserShownIndex = model.browserIndex;
    if (follow)
        g_browserKeysWon = true;
    int hovered = -1;
    if (items.empty())
        Muted(model.browserTab == 0 ? "(no pieces that can be moved)" : "(no objects that can be copied)");
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (!Matches(items[i].label, g_browserSearch))
            continue;
        ImGui::PushID(i);
        bool selected = i == model.browserIndex;
        if (ImGui::Selectable(items[i].label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            Ask(Request::kBrowserIndex, i);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Ask(Request::kBrowserPlace);
        }
        if (ImGui::IsItemHovered())
            hovered = i;
        if (selected && follow)
            ImGui::SetScrollHereY(0.5f);
        ImGui::PopID();
    }
    ImGui::PopFont();
    ImGui::EndChild();
    // The preview follows the mouse over the list, or the keys when they moved the highlight since.
    if (hovered != g_browserHovered) {
        g_browserHovered = hovered;
        if (hovered >= 0)
            g_browserKeysWon = false;
    }
    int preview = g_browserHovered >= 0 && !g_browserKeysWon ? g_browserHovered : -1;
    if (preview != g_previewAsked) {
        g_previewAsked = preview;
        Ask(Request::kBrowserPreview, preview);
    }
    ImGui::BeginDisabled(items.empty());
    if (ImGui::Button("Place (Enter)"))
        Ask(Request::kBrowserPlace);
    ImGui::SameLine();
    if (ImGui::Button("Show me (F)"))
        Ask(Request::kBrowserFocus);
    ImGui::EndDisabled();
    ImGui::PushFont(g_small);
    Muted("Enter places a copy where the cursor points (over a panel: where the view looks).");
    ImGui::PopFont();
    ImGui::End();
    if (!open)
        Ask(Request::kToggleBrowser);
}

void AutosaveOffer(const Model& model)
{
    if (!model.offer || g_offerClosed)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, g_layout.viewport.y + 24.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::Begin("Unsaved changes from a previous session", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextUnformatted("An autosave newer than this level's saved layout was found.");
    if (ImGui::Button("Restore it (Ctrl+R)"))
        Ask(Request::kRestoreAutosave);
    ImGui::SameLine();
    if (ImGui::Button("Discard it (Ctrl+Shift+R)"))
        Ask(Request::kDiscardAutosave);
    ImGui::SameLine();
    if (ImGui::Button("Later"))
        g_offerClosed = true;
    ImGui::End();
}

// A spawn, crown or flag moved in a level shared with THUGPRO: said once a run (the edit is made).
void OnlineWarning(const Model& model)
{
    if (!model.nodes.onlineWarning)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, g_layout.viewport.y + 24.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(460.0f * g_scale, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.18f, 0.37f, 0.6f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.18f, 0.37f, 0.6f, 1.0f));
    ImGui::Begin("Online: a level shared with THUGPRO", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor(2);
    ImGui::TextWrapped("This level is ported from %s and THUGPRO's players have it too. Moving, adding, hiding or "
                       "reordering its spawns, crowns or flags changes where players start and where crowns and flags "
                       "are (and the crowns' order), against theirs.",
                       model.nodes.sharedGame);
    ImGui::Spacing();
    ImGui::TextWrapped("Your layout isn't used in net games, so playing online is unaffected - but a layout baked into "
                       "the level's files would be. The edit is made; Ctrl+Z puts it back.");
    ImGui::Spacing();
    if (ImGui::Button("Understood"))
        Ask(Request::kOnlineWarningSeen);
    ImGui::End();
}

// The Place palette (N): what can be added. A click on an entry arms it - the next click in the view places it there
// (Shift+click: keep placing); its button places one where the view looks.
char g_placeSearch[64];
void Place(const Model& model)
{
    if (!model.place.open)
        return;
    ImGui::SetNextWindowPos(ImVec2(g_layout.viewport.x + 8.0f * g_scale, g_layout.viewport.y + 8.0f * g_scale),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300.0f * g_scale, 0.0f));
    bool open = true;
    ImGui::Begin("Place: what to add###place", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##placesearch", "spawn\xE2\x80\xA6", g_placeSearch, sizeof(g_placeSearch));
    const std::vector<Model::PlaceView::Entry>& entries = model.place.entries;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const Model::PlaceView::Entry& e = entries[i];
        if (!Matches(e.label, g_placeSearch))
            continue;
        ImGui::PushID(i);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float h = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + h * 0.25f), ImVec2(pos.x + h * 0.5f, pos.y + h * 0.75f),
                                                  Colour(e.colour), 2.0f);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + h * 0.8f, pos.y));
        bool can = e.why.empty();
        ImGui::BeginDisabled(!can);
        if (ImGui::Selectable(e.label.c_str(), model.place.armed == i, ImGuiSelectableFlags_AllowDoubleClick)) {
            Ask(Request::kPlaceArm, model.place.armed == i ? -1 : i);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Ask(Request::kPlaceHere, i);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", can ? "Click, then click in the view to place it (double-click: where the view "
                                          "looks)"
                                        : e.why.c_str());
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::PushFont(g_small);
    if (model.place.armed >= 0 && model.place.armed < static_cast<int>(entries.size()))
        ImGui::TextWrapped("Click in the view to place: %s (Shift+click: more). Esc cancels.",
                           entries[model.place.armed].label.c_str());
    else
        Muted("Pick one, then click in the view. It's dropped to the ground and faces the way the camera looks.");
    ImGui::PopFont();
    ImGui::End();
    if (!open)
        Ask(Request::kPlaceToggle);
}

// Reset level: asked first.
void ConfirmReset(const Model& model)
{
    if (!model.confirmReset)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, g_layout.viewport.y + 60.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(460.0f * g_scale, 0.0f));
    ImGui::Begin("Reset the level?###confirmreset", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextWrapped("Everything in %s goes back as the level has it: moved scenery, objects, rails and nodes go "
                       "back, hidden things are shown, copies and added nodes are removed.",
                       model.levelTitle.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("Its saved layout and autosave are deleted - the edited copy leaves the level select. Ctrl+Z "
                       "brings the changes back while the editor stays in this level, but the files only come back "
                       "when you save again (Ctrl+S).");
    ImGui::Spacing();
    if (ImGui::Button("Cancel"))
        Ask(Request::kResetCancelled);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.14f, 0.13f, 1.0f));
    if (ImGui::Button("Reset level"))
        Ask(Request::kResetConfirmed);
    ImGui::PopStyleColor();
    ImGui::End();
}

// Deleting a level's node: hidden, not removed - asked first.
void ConfirmHide(const Model& model)
{
    if (!model.confirm.open)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, g_layout.viewport.y + 60.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(420.0f * g_scale, 0.0f));
    ImGui::Begin("Delete an original node?###confirmhide", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::PushFont(g_mono);
    ImGui::TextUnformatted(model.confirm.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextWrapped("is part of %s.", model.level.c_str());
    if (model.confirm.crown)
        ImGui::TextWrapped("It will be hidden, not removed, so the crowns after it keep their order - each moves up one "
                           "number while it's hidden (the game counts only the crowns it finds). You can bring it back "
                           "with Home.");
    else
        ImGui::TextWrapped("It will be hidden, not removed: the level's nodes keep their indexes and the game skips it. "
                           "You can bring it back with Home.");
    ImGui::Spacing();
    if (ImGui::Button("Cancel"))
        Ask(Request::kHideCancelled);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.14f, 0.13f, 1.0f));
    std::string hide = "Hide " + model.confirm.family;
    if (ImGui::Button(hide.c_str()))
        Ask(Request::kHideConfirmed);
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace

bool& Enabled()
{
    static bool on = ConfigInt("features", "editor_ui", 1) != 0;
    return on;
}

bool Running() { return g_running; }

int PreviewPixels()
{
    float scale = g_scale > 0.0f ? g_scale : 1.0f;
    return std::clamp(static_cast<int>(256.0f * scale + 0.5f), 256, 512);
}

bool Start()
{
    if (g_running)
        return true;
    if (!Enabled())
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
    if (!UiBackend::Init()) {
        Log("level editor UI: not started - the old HUD instead");
        return false;
    }
    g_running = true;
    g_scale = 0.0f; // (the fonts made for the window's size on the first frame)
    g_requests.clear();
    g_offerClosed = false;
    g_uiButtons = g_editorButtons = 0;
    g_wantMouse = g_wantKeys = false;
    g_browserShownIndex = -1;
    g_browserHovered = g_previewAsked = -1;
    g_browserKeysWon = false;
    return true;
}

void Stop()
{
    g_running = false;
    g_uiButtons = g_editorButtons = 0;
    g_wantMouse = g_wantKeys = false;
}

void DeviceLost() { UiBackend::InvalidateDeviceObjects(); }

bool RouteEvent(const SDL_Event* event, bool editorCapture)
{
    if (!g_running)
        return true;
    auto route = [&](float x, float y) {
        g_mouseX = x;
        g_mouseY = y;
        return UiLayout::Decide({g_wantMouse, g_wantKeys, UiLayout::OnPanel(g_layout, x, y),
                                 editorCapture || g_editorButtons != 0});
    };
    switch (event->type) {
    case SDL_MOUSEMOTION: {
        UiLayout::Route r = route(static_cast<float>(event->motion.x), static_cast<float>(event->motion.y));
        if (r.mouseToUi || g_uiButtons)
            UiBackend::ProcessEvent(event);
        return true; // (the editor keeps the position and the look's movement; MouseToEditor says if it may pick)
    }
    case SDL_MOUSEBUTTONDOWN: {
        uint32_t bit = 1u << (event->button.button & 31);
        UiLayout::Route r = route(static_cast<float>(event->button.x), static_cast<float>(event->button.y));
        if (!r.mouseToEditor) {
            g_uiButtons |= bit;
            UiBackend::ProcessEvent(event);
            return false;
        }
        g_editorButtons |= bit;
        if (r.mouseToUi)
            UiBackend::ProcessEvent(event); // (ImGui loses its focus and hover; the editor has the click)
        return true;
    }
    case SDL_MOUSEBUTTONUP: {
        uint32_t bit = 1u << (event->button.button & 31);
        if (g_uiButtons & bit) {
            g_uiButtons &= ~bit;
            UiBackend::ProcessEvent(event);
            return false;
        }
        g_editorButtons &= ~bit;
        UiBackend::ProcessEvent(event); // (ImGui's button state stays true)
        return true;
    }
    case SDL_MOUSEWHEEL: {
        UiLayout::Route r = route(g_mouseX, g_mouseY);
        if (!r.mouseToEditor) {
            UiBackend::ProcessEvent(event);
            return false;
        }
        return true;
    }
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    case SDL_TEXTINPUT:
    case SDL_TEXTEDITING:
        UiBackend::ProcessEvent(event);
        return !g_wantKeys;
    default:
        UiBackend::ProcessEvent(event);
        return true;
    }
}

bool MouseToEditor()
{
    if (!g_running)
        return true;
    return UiLayout::Decide({g_wantMouse, g_wantKeys, UiLayout::OnPanel(g_layout, g_mouseX, g_mouseY),
                             g_editorButtons != 0})
        .mouseToEditor;
}

bool KeysToEditor() { return !g_running || !g_wantKeys; }

std::vector<Request> TakeRequests()
{
    std::vector<Request> requests;
    requests.swap(g_requests);
    return requests;
}

void Frame(const Model& model, const Overlay& overlay)
{
    if (!g_running)
        return;
    UpdateScale();
    UiBackend::NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    g_layout = UiLayout::Compute(io.DisplaySize.x, io.DisplaySize.y, g_scale, true, true);
    // The lines and gizmos under the panels (back buffer pixels to the window's).
    int backWidth = 0, backHeight = 0;
    UiBackend::BackBufferSize(&backWidth, &backHeight);
    float sx = backWidth > 0 ? io.DisplaySize.x / static_cast<float>(backWidth) : 1.0f;
    float sy = backHeight > 0 ? io.DisplaySize.y / static_cast<float>(backHeight) : 1.0f;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    for (const Overlay::Triangle& t : overlay.under)
        background->AddTriangleFilled(ImVec2(t.ax * sx, t.ay * sy), ImVec2(t.bx * sx, t.by * sy),
                                      ImVec2(t.cx * sx, t.cy * sy), Colour(t.argb));
    for (const Line& line : overlay.lines)
        background->AddLine(ImVec2(line.ax * sx, line.ay * sy), ImVec2(line.bx * sx, line.by * sy), Colour(line.argb),
                            line.thickness * g_scale);
    for (const Overlay::Triangle& t : overlay.triangles)
        background->AddTriangleFilled(ImVec2(t.ax * sx, t.ay * sy), ImVec2(t.bx * sx, t.by * sy),
                                      ImVec2(t.cx * sx, t.cy * sy), Colour(t.argb));
    for (const Overlay::Circle& c : overlay.circles) {
        if (c.filled)
            background->AddCircleFilled(ImVec2(c.x * sx, c.y * sy), c.radius * sx, Colour(c.argb));
        else
            background->AddCircle(ImVec2(c.x * sx, c.y * sy), c.radius * sx, Colour(c.argb), 0, c.thickness * g_scale);
    }
    if (g_mono) {
        for (const Overlay::Text& t : overlay.texts) {
            ImVec2 at(t.x * sx, t.y * sy);
            ImVec2 size = g_mono->CalcTextSizeA(g_mono->FontSize, 1e9f, 0.0f, t.text.c_str());
            background->AddRectFilled(ImVec2(at.x - 3.0f, at.y - 1.0f), ImVec2(at.x + size.x + 3.0f, at.y + size.y + 1.0f),
                                      IM_COL32(16, 17, 19, 170), 3.0f);
            background->AddText(g_mono, g_mono->FontSize, at, Colour(t.argb), t.text.c_str());
        }
    }
    ImGui::PushFont(g_ui);
    Toolbar(model);
    Outliner(model);
    Inspector(model);
    StatusBar(model);
    Browser(model);
    Help(model);
    AutosaveOffer(model);
    Place(model);
    ConfirmHide(model);
    ConfirmReset(model);
    CollisionLegend(model);
    OnlineWarning(model);
    ImGui::PopFont();
    ImGui::Render();
    UiBackend::Render();
    g_wantMouse = io.WantCaptureMouse;
    g_wantKeys = io.WantCaptureKeyboard;
}

} // namespace Front::LevelEditor::Ui
