// NOT ORIGINAL - the level editor's UI (ours). See level_editor_ui.h. Only ImGui's own API here: the platform and the
// renderer are level_editor_ui_backend.cpp's.

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_adjust.h"
#include "game/front/level_editor_arrange.h"
#include "game/front/level_editor_ui.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/front/level_editor_entities.h"
#include "game/front/level_editor_budget.h"
#include "game/front/level_editor_camera.h"
#include "game/front/level_editor_cameras.h"
#include "game/front/level_editor_goals.h"
#include "game/front/level_editor_hints.h"
#include "game/front/level_editor_checks.h"
#include "game/front/level_editor_notes.h"
#include "game/front/level_editor_ux9.h"
#include "game/front/level_editor_clusters.h"
#include "game/front/level_editor_faces.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_playtest.h"
#include "game/front/level_editor_prefs.h"
#include "game/front/level_editor_triggers.h"
#include "game/front/level_editor_scale.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_thumbs.h"
#include "game/front/level_editor_transform.h"
#include "game/front/level_editor_ui_backend.h"
#include "game/front/level_editor_ui_fonts.h"
#include "game/front/level_editor_ui_layout.h"
#include "game/front/level_editor_ui_strip.h"
#include "game/front/level_editor_ui_theme.h"
#include "game/front/level_editor_ux3.h"
#include "game/front/level_editor_ux8.h"
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
bool g_scaleLocked = true; // (the Inspector's Scale: Uniform - one field changes all three in proportion)

// The routing (level_editor_ui_layout.h): the last frame's layout and ImGui's wishes, the mouse, and which mouse
// buttons a press gave to the UI / to the editor (their release goes the same way).
UiLayout::Layout g_layout;
bool g_wantMouse, g_wantKeys;
float g_mouseX = -1.0f, g_mouseY = -1.0f;
uint32_t g_uiButtons, g_editorButtons;

std::vector<Request> g_requests;
bool g_offerClosed;
char g_outlinerSearch[96];
bool g_changedOnly; // (the Outliner's "Changed only")
char g_browserSearch[96];
int g_browserShownIndex = -1;
// The preview's entry: the list's row under the mouse (-1: none) and whether the keys moved the highlight since the
// mouse last moved onto a row (then the highlighted one shows); the entry asked for last.
int g_browserHovered = -1;
bool g_browserKeysWon;
int g_previewAsked = -1;

void Ask(Request::Kind kind, int value = 0, uint32_t id = 0) { g_requests.push_back({kind, value, id}); }

// The accent colour ([editor] accent - level_editor_prefs.h) at `alpha`.
ImVec4 Accent(float alpha)
{
    float rgb[3];
    Prefs::AccentRgb(Prefs::Get().accent, rgb);
    return ImVec4(rgb[0], rgb[1], rgb[2], alpha);
}
int g_styleGeneration = -1; // (the preferences' scale / accent the style was built for)

// ARGB (the editor's colours) to ImGui's packed colour.
ImU32 Colour(uint32_t argb)
{
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

// --- fonts and style ---

// The fonts and the style for the window's size (made again when its scale changes: the font texture with them).
void UpdateScale()
{
    int width = 0, height = 0;
    UiBackend::WindowSize(&width, &height);
    float scale = UiLayout::ScaleFor(width, height);
    if (int pct = Prefs::Get().uiScalePct; pct > 0) // ([editor] ui_scale_pct: the automatic one overridden)
        scale = static_cast<float>(pct) / 100.0f;
    if (scale == g_scale && g_styleGeneration == Prefs::StyleGeneration())
        return;
    g_scale = scale;
    g_styleGeneration = Prefs::StyleGeneration();
    UiLayout::FontSizes sizes = UiLayout::FontSizesFor(scale);
    UiTheme::Fonts fonts = UiTheme::BuildFonts(scale);
    g_ui = fonts.ui;
    g_small = fonts.small;
    g_mono = fonts.mono;
    g_heading = fonts.heading;
    UiBackend::InvalidateDeviceObjects(); // (the font texture is made again)
    float rgb[3];
    Prefs::AccentRgb(Prefs::Get().accent, rgb);
    UiTheme::ApplyStyle(scale, rgb);
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

// (UX8's drawing: the UI's fonts and scale)
Ux8::Fonts Ux8Fonts()
{
    Ux8::Fonts fonts;
    fonts.ui = g_ui;
    fonts.small = g_small;
    fonts.mono = g_mono;
    fonts.scale = g_scale;
    return fonts;
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

// The collision's legend, over the bottom left of the view while it's shown.
void CollisionLegend(const Model& model)
{
    if (!model.view.collision || !Overlays::Shown(Overlays::kLegend)) // (the Overlays menu: level_editor_overlays.h)
        return;
    const UiLayout::Rect& view = g_layout.viewport;
    ImGui::SetNextWindowPos(ImVec2(view.x + 8.0f * g_scale, view.y + view.height - 8.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::Begin("##collisionLegend", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PushFont(g_small);
    ImGui::TextUnformatted("Collision");
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
    ImGui::TextDisabled("%d faces, %d pieces, %d invisible walls%s", model.view.triangles, model.view.sectors,
                        model.view.walls, model.view.capped ? " (nearest)" : "");
    if (model.view.off > 0)
        ImGui::TextDisabled("Faded / dashed: collision off (%d)", model.view.off);
    // (a moved piece's shared collision - level_editor_shared_coll.h - and the level's triggers by kind)
    auto swatchLine = [](uint32_t argb, const char* text) {
        ImVec2 at = ImGui::GetCursorScreenPos();
        float side = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRect(at, ImVec2(at.x + side, at.y + side), Colour(argb | 0xFF000000u), 2.0f, 0,
                                            2.0f);
        ImGui::Dummy(ImVec2(side, side));
        ImGui::SameLine();
        ImGui::TextUnformatted(text);
    };
    swatchLine(View::kCompanionEdge, "Moved collision");
    swatchLine(View::kTakenEdge, "Original collision position (off)");
    swatchLine(View::kHoverWall, "Wall under cursor (Shift)");
    if (model.view.companions > 0 || model.view.taken > 0)
        ImGui::TextDisabled("%d moved pieces, %d faces moved", model.view.companions, model.view.taken);
    if (ImGui::TreeNode("Triggers")) {
        for (int k = 0; k < Triggers::kKinds; ++k) {
            auto kind = static_cast<Triggers::Kind>(k);
            ImVec2 at = ImGui::GetCursorScreenPos();
            float side = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + side, at.y + side),
                                                      Colour(Triggers::KindColour(kind) | 0xFF000000u), 2.0f);
            ImGui::Dummy(ImVec2(side, side));
            ImGui::SameLine();
            ImGui::TextUnformatted(Triggers::KindName(kind));
        }
        ImGui::TextDisabled("%d trigger faces", model.view.triggers);
        ImGui::TreePop();
    }
    ImGui::TextDisabled("Not shown: camera pass-through, not taggable");
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
        Muted("Invisible wall");
        ImGui::SameLine();
        Mono("0x%08X", model.view.wall);
        ImGui::PushFont(g_mono);
        ImGui::TextWrapped("%s", model.view.wallInfo.c_str());
        ImGui::PopFont();
        // (level_editor_walls.h: the game's own switch, CSector::SetActive; moved and turned as scenery)
        if (model.view.wallOff)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "%s", model.view.wallState.c_str());
        else
            ImGui::TextWrapped("%s", model.view.wallState.c_str());
        if (model.view.wallOff) {
            if (ImGui::Button("Restore Collision"))
                Ask(Request::kWallCollision, 1);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Turn collision back on  (Del)");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.22f, 0.30f, 0.85f));
            if (ImGui::Button("Disable Collision"))
                Ask(Request::kWallCollision, 0);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Turn this wall's collision off  (Del)");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!model.view.wallMoved && !model.view.wallOff);
        if (ImGui::Button("Revert##wall"))
            Ask(Request::kReset);
        ImGui::EndDisabled();
        if (ImGui::Button("Drop to Ground##wall"))
            Ask(Request::kDrop);
        ImGui::SameLine();
        if (ImGui::Button("Close##wall"))
            Ask(Request::kClearWall);
        
    }
    if (model.view.sceneryCollision >= 0 && !model.view.wall) {
        // A drawn piece's collision switched off by itself (it stays drawn; the same switch, level_editor_walls.h).
        ImGui::Spacing();
        ImGui::Separator();
        bool on = model.view.sceneryCollision == 1;
        if (ImGui::Checkbox(model.view.sceneryCollision == 2 ? "Collision (mixed)" : "Collision", &on))
            Ask(Request::kSceneryCollision, on ? 1 : 0);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off: visible but not solid");
    }
    if (model.view.canHide || model.view.hidden > 0) {
        ImGui::Spacing();
        ImGui::Separator();
        Muted("Editor Visibility");
        ImGui::BeginDisabled(!model.view.canHide);
        if (ImGui::Button("Hide"))
            Ask(Request::kViewHide);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hide Selection  (H)");
        ImGui::SameLine();
        if (ImGui::Button("Isolate"))
            Ask(Request::kViewIsolate);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Isolate Selection  (I)");
        ImGui::EndDisabled();
        if (model.view.hidden > 0) {
            ImGui::SameLine();
            if (ImGui::Button("Show All"))
                Ask(Request::kViewShowAll);
            ImGui::TextDisabled("%d hidden", model.view.hidden);
        }
    }
    Faces::InspectorSection(g_mono, g_small); // (map3: the collision faces selected - level_editor_faces.h)
}

// --- the panels ---

// The menu bar over the tool strip (level_editor_ui_strip.h).
void Toolbar(const Model& model)
{
    PlaceWindow(g_layout.menu);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##menubar", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(2);
    if (ImGui::BeginMenuBar()) {
        MenuBar(model, Ux8Fonts(), &g_requests);
        ImGui::EndMenuBar();
    }
    ImGui::End();

    PlaceWindow(g_layout.strip);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * g_scale, 4.0f * g_scale));
    ImGui::Begin("##toolstrip", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * g_scale, 3.0f * g_scale));
    UiStrip::State state;
    state.selection = std::strcmp(model.mode, "FLYING") != 0 && std::strcmp(model.mode, "ADD") != 0;
    Transform::Settings& transform = Transform::Current();
    state.tool = static_cast<int>(transform.mode);
    state.local = transform.space == Transform::Space::Local;
    state.snap = model.snapAlways;
    state.grid = Transform::Grid();
    state.grids = Transform::GridSizes();
    state.pickFilter = model.pickFilter;
    state.level = model.levelTitle;
    state.saved = model.editedSaved;
    state.unsaved = model.unsaved;
    state.budget = Budget::StripMeter(&state.budgetFill, &state.budgetArgb);
    int infos = 0;
    Checks::ShownCounts(&state.errors, &state.warnings, &infos);
    state.hidden = model.view.hidden;
    state.simulating = PlayTest::Simulating();
    state.keyOf = [](const char* id) {
        int index = Actions::Find(id);
        return index >= 0 ? Hints::FirstKey(Actions::KeysText(index)) : std::string();
    };
    const UiLayout::Rect& strip = g_layout.strip;
    float padX = 8.0f * g_scale, padY = std::max(0.0f, (strip.height - ImGui::GetFrameHeight()) * 0.5f);
    UiStrip::Output out;
    UiStrip::Draw(state, strip.x + padX, strip.y + padY, strip.width - 2.0f * padX, g_scale, &out);
    ImGui::PopStyleVar();
    if (out.budgetHovered)
        Budget::Tooltip();
    if (out.levelHovered)
        ImGui::SetTooltip("Editing a copy of %s. The original level is unchanged.\nLayout: %s", model.levelTitle.c_str(),
                          model.editedSaved ? (model.unsaved ? "saved, with unsaved changes" : "saved") : "not saved yet");
    for (const UiStrip::Event& e : out.events) {
        switch (e.kind) {
        case UiStrip::Event::kDeselect: Ask(Request::kDeselect); break;
        case UiStrip::Event::kTool: transform.mode = static_cast<Transform::Mode>(static_cast<int>(e.value)); break;
        case UiStrip::Event::kSpace:
            transform.space = transform.space == Transform::Space::World ? Transform::Space::Local : Transform::Space::World;
            break;
        case UiStrip::Event::kSnap: Ask(Request::kSnapAlways, e.value != 0.0f ? 1 : 0); break;
        case UiStrip::Event::kGrid: Transform::SetGrid(e.value); break;
        case UiStrip::Event::kPick: Ask(Request::kPickFilter, 0, static_cast<uint32_t>(e.value)); break;
        case UiStrip::Event::kProblems: Checks::ToggleWindow(); break;
        case UiStrip::Event::kBudget: Checks::OpenWindow(Checks::kBudgetTab); break;
        case UiStrip::Event::kShowAll: Ask(Request::kViewShowAll); break;
        case UiStrip::Event::kSimulate: Actions::Run(Actions::kSimulate); break;
        }
    }
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
        ImGui::PushStyleColor(ImGuiCol_Button, on ? Accent(0.75f) : ImVec4(0.2f, 0.21f, 0.225f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.6f, 0.61f, 0.63f, 1.0f));
        if (ImGui::SmallButton(name))
            Ask(Request::kFamilies, 0, model.nodes.families ^ Nodes::Bit(family));
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d %s - click to %s", counts[f], name, on ? "hide" : "show");
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
    // (Select similar on a node asks for its family here - level_editor_ux3.h)
    if (std::string asked = Ux3::TakeOutlinerSearch(); !asked.empty())
        std::snprintf(g_outlinerSearch, sizeof(g_outlinerSearch), "%s", asked.c_str());
    ImGui::InputTextWithHint("##search", "Search\xE2\x80\xA6",
                             g_outlinerSearch, sizeof(g_outlinerSearch));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Filter by name. Prefixes: rail: spawn: object: group:<name> label: locked: hidden:");
    // (only the rows the editor has changed - those with markers)
    ImGui::Checkbox("Changed Only", &g_changedOnly);
    if (ImGui::IsItemHovered()) {
        std::string legend = "Show changed items only. Markers:";
        for (int bit = 0; bit < kMarkCount; ++bit)
            legend += std::string("\n") + MarkLetter(bit) + "  " + MarkName(bit);
        ImGui::SetTooltip("%s", legend.c_str());
    }
    if (!model.marks.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d changed", static_cast<int>(model.marks.size()));
    }
    FamilyChips(model);
    ImGui::Separator();
    ImGui::BeginChild("##tree");
    Ux3::Groups(); // (the user's groups - level_editor_organize.h)
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    if (nodes.empty())
        Muted("No level loaded");
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
            auto found = model.marks.find(nodes[i].name);
            if (Ux3::RowPasses(nodes[i], g_cache.labels[i], g_outlinerSearch) &&
                MarkShown(found == model.marks.end() ? 0 : found->second, g_changedOnly))
                shown.push_back(i);
        }
        if (shown.empty())
            continue;
        bool holdsSelected = follow && selected < static_cast<int>(nodes.size()) && nodes[selected].family == family;
        if (holdsSelected || g_outlinerSearch[0] || g_changedOnly)
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
                // (the user's label beside the name - level_editor_organize.h)
                std::string userLabel = Ux3::RowLabel(node.name);
                std::string rowText = userLabel.empty() ? g_cache.labels[i] : g_cache.labels[i] + "  \xE2\x80\x94 " + userLabel;
                bool clicked = ImGui::Selectable(rowText.c_str(), isSelected,
                                                 ImGuiSelectableFlags_AllowDoubleClick |
                                                     ImGuiSelectableFlags_SpanAllColumns |
                                                     ImGuiSelectableFlags_AllowOverlap); // (the row's toggles)
                Ux3::RowMenu(node); // (right-click: label, groups, eye / lock / selectable)
                if (node.hidden)
                    ImGui::PopStyleColor();
                if (clicked) {
                    Ask(Request::kSelectNode, i);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        Ask(Request::kGoToNode, i);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click to select, double-click to focus");
                std::string tag = RowTag(node);
                // (the markers - what the editor has done to it - at the row's end, the tag before them)
                auto found = model.marks.find(node.name);
                uint8_t marks = found == model.marks.end() ? 0 : found->second;
                // (the Overlays menu's Outliner markers off: none shown - Changed only still filters by them)
                std::string letters = Overlays::Shown(Overlays::kOutlinerMarks) ? MarkLetters(marks) : std::string();
                float rowRight = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ScrollbarSize -
                                 Ux3::TogglesWidth();
                float marksWidth = letters.empty() ? 0.0f : ImGui::CalcTextSize(letters.c_str()).x + 6.0f * g_scale;
                if (!tag.empty()) {
                    float width = ImGui::CalcTextSize(tag.c_str()).x;
                    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), rowRight - marksWidth - width));
                    ImGui::TextColored(ImVec4(0.6f, 0.61f, 0.63f, 1.0f), "%s", tag.c_str());
                }
                if (!letters.empty()) {
                    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), rowRight - marksWidth + 6.0f * g_scale));
                    bool first = true;
                    for (int bit = 0; bit < kMarkCount; ++bit) {
                        if (!(marks & (1u << bit)))
                            continue;
                        if (!first)
                            ImGui::SameLine(0.0f, 0.0f);
                        first = false;
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colour(MarkColour(bit))), "%c", MarkLetter(bit));
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", MarkName(bit));
                    }
                }
                Ux3::RowToggles(node, rowRight); // (the eye, the lock, selectable - level_editor_organize.h)
                if (row == scrollTo)
                    ImGui::SetScrollHereY(0.4f);
                ImGui::PopID();
            }
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }
    Notes::OutlinerSection(g_mono, g_outlinerSearch); // (UX5: note pins - level_editor_notes.h)
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
        ImGui::SeparatorText("Node");
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
        Badge("Added", IM_COL32(46, 122, 67, 200));
        ImGui::NewLine();
        ImGui::PopFont();
    }
    if (node.hidden) {
        ImGui::PushFont(g_small);
        Badge("Hidden", IM_COL32(110, 110, 110, 220));
        ImGui::NewLine();
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s", "Hidden in game. Revert to show.");
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
        ImGui::TextWrapped("Shared with THUGPRO (ported from %s). %s affect online play.", model.nodes.sharedGame,
                           Nodes::FamilyName(node.family));
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
                std::snprintf(label, sizeof(label), "Whole %s (%d point%s)", model.nodes.chainNoun,
                              model.nodes.railPoints, model.nodes.railPoints == 1 ? "" : "s");
                if (ImGui::Checkbox(label, &whole))
                    Ask(Request::kWholeRail, whole ? 1 : 0);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Move all points together  (L)");
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
                Mono("hidden");
            else
                Mono("%d of %d restarts", node.order + 1, Nodes::Walked(Nodes::Family::Spawn));
        }
        if (node.family == Nodes::Family::Crown) {
            FieldLabel("Crown");
            if (node.hidden)
                Mono("hidden (%d active)", Nodes::Walked(Nodes::Family::Crown));
            else
                Mono("%d of %d", node.order,
                     Nodes::Walked(Nodes::Family::Crown));
            if (model.nodes.nodeOnly && !node.hidden) {
                ImGui::BeginDisabled(!model.nodes.crownUp);
                if (ImGui::SmallButton("Move Up"))
                    Ask(Request::kCrownMove, -1);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(node.added ? "Move earlier in crown order" : "Only added crowns can be reordered");
                ImGui::SameLine();
                ImGui::BeginDisabled(!model.nodes.crownDown);
                if (ImGui::SmallButton("Move Down"))
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
        ImGui::TextDisabled("Applies: %s", model.nodes.whenApplied);
        if (model.nodes.moved) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "Modified");
        }
        ImGui::PopFont();
        if (ImGui::Button("Drop to Ground"))
            Ask(Request::kDrop);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drop to Ground  (G)");
        ImGui::SameLine();
        ImGui::BeginDisabled(!model.nodes.moved && !model.nodes.hidden);
        if (ImGui::Button(model.nodes.hidden ? "Show" : "Revert"))
            Ask(Request::kReset);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (model.nodes.added || (model.nodes.canHide && !model.nodes.hidden)) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.14f, 0.13f, 1.0f));
            if (ImGui::Button(model.nodes.added ? "Delete" : "Hide"))
                Ask(Request::kDeleteNode);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(model.nodes.added ? "Delete  (Del)" : "Hide original node  (Del)");
            ImGui::SameLine();
        } else if (!model.nodes.added && !model.nodes.hidden && !model.nodes.whyNoHide.empty()) {
            ImGui::BeginDisabled(true);
            ImGui::Button("Hide");
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
    PlayTest::RestartSection(index); // (a restart's Play from here - level_editor_playtest.h)
    SpawnSection(index, &g_requests); // (a HORSE spot - the feature map: level_editor_spots_ui.cpp)
    if (ImGui::Button("Focus"))
        Ask(Request::kGoToNode, index);
    // Every field of its structure, typed and editable (phase 6: level_editor_fields_ui.cpp).
    FieldsSection(index, g_mono, g_small, &g_requests);
}

// The locks (level_editor_picking.h): each with Unlock.
void LockList(const Model& model)
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
    Muted("Parts");
    float rows = static_cast<float>(std::min<size_t>(members.size(), 10));
    ImGui::BeginChild("##members", ImVec2(0.0f, rows * ImGui::GetFrameHeightWithSpacing() + 4.0f * g_scale), false);
    for (size_t i = 0; i < members.size(); ++i) {
        const Model::Selection::Member& member = members[i];
        ImGui::PushID(static_cast<int>(i));
        int rail = member.rail ? 1 : 0;
        if (ImGui::SmallButton("x"))
            Ask(Request::kMemberRemove, rail, member.id);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Remove from selection");
        ImGui::SameLine();
        if (ImGui::SmallButton("Only"))
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
        Triggers::InspectorSection(model.nodes.selected); // (a trigger volume's settings - level_editor_triggers.h)
        Entities::InspectorSection(model.nodes.selected, 0, g_mono, &g_requests); // (sound, particles, ghosts)
        // (map3: a camera node's pitch, target, preview - level_editor_cameras.h)
        if (model.nodes.selected >= 0 && model.nodes.selected < static_cast<int>(Nodes::All().size()))
            Cameras::InspectorSection(Nodes::All()[model.nodes.selected].name, g_mono, g_small);
        if (model.nodes.selected >= 0 && model.nodes.selected < static_cast<int>(Nodes::All().size()))
            Goals::InspectorSection(Nodes::All()[model.nodes.selected].name, g_mono, g_small); // (a goal's point)
    } else if (s.objects == 0 && s.sectors == 0 && !model.view.wall) {
        ImGui::TextDisabled("Nothing selected");
    } else if (s.objects > 0 || s.sectors > 0) {
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
                // The scale along its own axes (level_editor_scale.h): X / Y / Z dragged to scrub, Ctrl+click to type;
                // one undo step a field. Locked (the default): all three keep their proportions.
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                Muted("Scale");
                ImGui::TableNextColumn();
                float values[3] = {s.scale.x, s.scale.y, s.scale.z};
                static const char* const kIds[3] = {"##scalex", "##scaley", "##scalez"};
                static const char* const kFormats[3] = {"x %.3f", "y %.3f", "z %.3f"};
                ImGui::PushFont(g_mono);
                for (int axis = 0; axis < 3; ++axis) {
                    ImGui::SetNextItemWidth(110.0f * g_scale);
                    float value = values[axis];
                    if (ImGui::DragFloat(kIds[axis], &value, 0.005f, Scale::kMin, Scale::kMax, kFormats[axis],
                                         ImGuiSliderFlags_AlwaysClamp)) {
                        Request request = {Request::kScaleSet};
                        for (int a = 0; a < 3; ++a)
                            request.a[a] = values[a];
                        if (g_scaleLocked && values[axis] > 0.0f) {
                            // (every axis by the same ratio, clamped so none leaves 0.25..4)
                            float ratio = value / values[axis];
                            for (int a = 0; a < 3; ++a)
                                ratio = std::clamp(ratio, Scale::kMin / values[a], Scale::kMax / values[a]);
                            for (int a = 0; a < 3; ++a)
                                request.a[a] = values[a] * ratio;
                        } else {
                            request.a[axis] = value;
                        }
                        g_requests.push_back(request);
                    }
                    if (ImGui::IsItemDeactivated())
                        Ask(Request::kScaleDone);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Local %c scale (%.2f to %.0f)", "XYZ"[axis], Scale::kMin, Scale::kMax);
                }
                ImGui::PopFont();
                ImGui::Checkbox("Uniform##scalelock", &g_scaleLocked);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Scale all axes together");
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
        // (map3: the selected scenery's trick-object cluster - level_editor_clusters.h)
        if (s.sectors > 0 && s.objects == 0)
            Clusters::InspectorSection(Scenery::Selection(), g_mono, g_small);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", model.selected.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Drop to Ground"))
            Ask(Request::kDrop);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drop to Ground  (G)");
        ImGui::SameLine();
        if (ImGui::Button("Deselect"))
            Ask(Request::kDeselect);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Deselect  (Esc)");
        if (model.selectionLockable) {
            ImGui::SameLine();
            if (ImGui::Button(model.selectionLocked ? "Unlock" : "Lock"))
                Ask(Request::kToggleLock);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Prevent selection in the view  (L)");
        }
        if (s.objects == 0 && s.sectors == 1)
            Triggers::ScenerySection(s.checksum); // (breaks when hit - the feature map: level_editor_triggers_ui.cpp)
        // (one object: its model, physics, path speed and preview - level_editor_entities.h)
        if (s.objects == 1)
            Entities::InspectorSection(-1, s.checksum, g_mono, &g_requests);
        if (model.nodes.selected >= 0)
            NodeSection(model, model.nodes.selected);
    }
    Ux3::InspectorSection(model); // (the clipboard, the shared fields of a multi-selection - level_editor_ux3.h)
    ImGui::Spacing();
    ImGui::Separator();
    Muted("Under Cursor");
    const std::vector<Nodes::Node>& all = Nodes::All();
    if (model.nodes.hovered >= 0 && model.nodes.hovered < static_cast<int>(all.size())) {
        const Nodes::Node& node = all[model.nodes.hovered];
        ImGui::TextWrapped("%s (%s)", Nodes::Label(node.name).c_str(), Nodes::Label(node.cls).c_str());
    } else {
        ImGui::TextWrapped("%s", model.hover.c_str());
    }
    ViewSection(model);
    Entities::ViewOptions(); // (Show uncreated objects)
    LockList(model);
    Cameras::ListSection(g_mono, g_small); // (map3: the camera nodes - level_editor_cameras.h)
    Goals::ListSection(g_mono, g_small);   // (Create-a-Goal goals of the edited copy - level_editor_goals.h)
    LevelSection(model.levelProps, g_mono, g_small, &g_requests); // (phase 6: level_editor_fields_ui.cpp)
    ImGui::End();
}

void StatusBar(const Model& model)
{
    PlaceWindow(g_layout.status);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * g_scale, 4.0f * g_scale));
    ImGui::Begin("##status", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    // Right: unsaved, the changes, the camera (elided before the hints are crowded out).
    char right[224];
    std::string view = Camera::Status(); // (an ortho view, orbiting - level_editor_camera.h)
    std::snprintf(right, sizeof(right), "%s%d changes  \xC2\xB7  %d moved  \xC2\xB7  %.0f, %.0f, %.0f  \xC2\xB7  speed %.0f%s%s",
                  model.unsaved ? "Unsaved  \xC2\xB7  " : "", model.changes, model.moved, model.camera.x, model.camera.y,
                  model.camera.z, model.speed, view.empty() ? "" : "  \xC2\xB7  ", view.c_str());
    const UiLayout::Rect& bar = g_layout.status;
    float padX = 8.0f * g_scale;
    ImGui::PushFont(g_small);
    float y = bar.y + std::max(0.0f, (bar.height - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::PopFont();
    // (UX8: key caps and mouse / controller glyphs - level_editor_ux8.h)
    UiStrip::StatusRow(model.message, model.keys, right, bar.x + padX, y, bar.width - 2.0f * padX, Ux8Fonts());
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
    ImGui::Begin("Keyboard Shortcuts", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
    // (the first-run tips again, the preferences - level_editor_prefs.h)
    if (ImGui::SmallButton("Show Tips"))
        Prefs::ShowTips();
    ImGui::SameLine();
    if (ImGui::SmallButton("Preferences"))
        Prefs::Open() = true;
    // (every action from the registry, with its keys as bound now - level_editor_actions.h; the details below)
    Ux3::HelpFromRegistry();
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
        for (const Overlays::HelpRow& extra : Overlays::HelpRows())
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
            ImGui::PushStyleColor(ImGuiCol_Button, Accent(0.85f));
        if (ImGui::Button(tab == 0 ? "Scenery" : "Objects") && !on)
            Ask(Request::kBrowserTab, tab);
        if (on)
            ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter\xE2\x80\xA6", g_browserSearch, sizeof(g_browserSearch));
    float buttons = ImGui::GetFrameHeightWithSpacing() * 3.0f; // (UX7: a row of the arrange tools')
    const std::vector<BrowserItem>& items = *model.browserItems;
    bool follow = model.browserIndex != g_browserShownIndex; // (the keys moved it: keep it in view)
    g_browserShownIndex = model.browserIndex;
    if (follow)
        g_browserKeysWon = true;
    // The entries: a grid of thumbnails or the list, All / Recent / Favourites (level_editor_thumbs.h).
    int hovered = Thumbs::ItemsPanel(model, g_browserSearch, follow, -buttons, g_mono, g_small, g_scale, &g_requests);
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
    Arrange::BrowserExtras(g_scale); // (UX7: Replace selection, Click places - level_editor_arrange.h)
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
    ImGui::Begin("Recover Autosave", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextUnformatted("An autosave is newer than the saved layout.");
    if (ImGui::Button("Restore"))
        Ask(Request::kRestoreAutosave);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Restore Autosave  (Ctrl+R)");
    ImGui::SameLine();
    if (ImGui::Button("Discard"))
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
    ImGui::Begin("Shared Level", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor(2);
    ImGui::TextWrapped("This level (ported from %s) is shared with THUGPRO. Changing spawns, crowns or flags makes "
                       "it differ from other players' copies. Layouts are not used online.",
                       model.nodes.sharedGame);
    ImGui::Spacing();
    if (ImGui::Button("OK"))
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
    ImGui::Begin("Place Node###place", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##placesearch", "Search\xE2\x80\xA6", g_placeSearch, sizeof(g_placeSearch));
    const std::vector<Model::PlaceView::Entry>& entries = model.place.entries;
    // (a list that scrolls: the level's peds, cars, sets, particles and sounds make it long - level_editor_entities.h)
    float listHeight = std::min(static_cast<float>(entries.size()) * ImGui::GetTextLineHeightWithSpacing() + 8.0f,
                                std::max(160.0f * g_scale, g_layout.viewport.height * 0.6f));
    ImGui::BeginChild("##placelist", ImVec2(0.0f, listHeight));
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
            ImGui::SetTooltip("%s", can ? "Click, then click in the view. Double-click: place at view center"
                                        : e.why.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::PushFont(g_small);
    if (model.place.armed >= 0 && model.place.armed < static_cast<int>(entries.size()))
        ImGui::TextWrapped("Click to place %s. Shift+click: place more. Esc: cancel.",
                           entries[model.place.armed].label.c_str());
    else
        Muted("Choose an item, then click in the view.");
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
    ImGui::Begin("Reset Level###confirmreset", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextWrapped("Revert all changes to %s and delete its saved layout and autosave?", model.levelTitle.c_str());
    ImGui::TextDisabled("Undo restores the changes until you leave the level.");
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

void PushRequest(const Request& request) { g_requests.push_back(request); }

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
    {
        Ux3::Fonts fonts;
        fonts.ui = g_ui;
        fonts.small = g_small;
        fonts.mono = g_mono;
        fonts.heading = g_heading;
        fonts.scale = g_scale;
        Ux3::SetFonts(fonts);
        Ux3::Settings settings;
        settings.snapAlways = model.snapAlways;
        settings.carry = model.carry;
        settings.collision = model.view.collision;
        settings.pickFilter = model.pickFilter;
        settings.canUndo = model.canUndo;
        settings.canRedo = model.canRedo;
        Ux3::NoteSettings(settings);
    }
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
    Prefs::Window(g_scale, g_heading, g_small); // (level_editor_prefs.h)
    Prefs::TipsWindow(g_scale, g_heading, g_mono);
    PlayTest::Window(g_scale, g_heading, g_small); // (level_editor_playtest.h)
    // (UX7: the Arrange window, the adjust-last panel - level_editor_arrange.h, level_editor_adjust.h)
    Arrange::Window(g_scale);
    Adjust::Panel(g_layout.viewport.x, g_layout.viewport.y + g_layout.viewport.height, g_scale);
    Triggers::Window(g_layout.viewport.x, g_layout.viewport.y, g_layout.viewport.width, g_layout.viewport.height, g_scale,
                     model.nodes.selected);
    // (UX5: the Problems window, the selected note's window)
    Checks::Window(g_layout.viewport.x, g_layout.viewport.y, g_layout.viewport.width, g_layout.viewport.height, g_scale,
                   g_mono);
    Notes::Window(g_layout.viewport.x, g_layout.viewport.y, g_layout.viewport.width, g_layout.viewport.height, g_scale);
    // (UX9: the gap wizard's and the ground tool's forms, the skateability legend - level_editor_ux9.h)
    Ux9::Windows(g_layout.viewport.x, g_layout.viewport.y, g_layout.viewport.width, g_layout.viewport.height, g_scale);
    Ux3::Windows(model);         // (the search, history, changes, keys, snapshots - level_editor_ux3.h)
    Ux8::Windows(g_layout.viewport.x, g_layout.viewport.y, g_layout.viewport.width, g_layout.viewport.height,
                 Ux8Fonts()); // (UX8: the pie, the crosshair - level_editor_ux8.h)
    Ux3::RememberSections();     // (the Inspector's sections folded as last time)
    ImGui::PopFont();
    ImGui::Render();
    UiBackend::Render();
    g_wantMouse = io.WantCaptureMouse;
    g_wantKeys = io.WantCaptureKeyboard;
}

} // namespace Front::LevelEditor::Ui
