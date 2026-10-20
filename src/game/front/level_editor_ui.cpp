// NOT ORIGINAL - the level editor's UI (ours). See level_editor_ui.h. Only ImGui's own API here: the platform and the
// renderer are level_editor_ui_backend.cpp's.

#include "game/front/level_editor_ui.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_ui_backend.h"
#include "game/front/level_editor_ui_fonts.h"
#include "game/front/level_editor_ui_layout.h"
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
    ImGui::SameLine();
    ImGui::BeginDisabled(model.selection.objects == 0 && model.selection.sectors == 0);
    if (ImGui::SmallButton("Ground (G)"))
        Ask(Request::kDrop);
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 14.0f * g_scale);
    ImGui::PushFont(g_mono);
    ImGui::TextDisabled("%s", model.level.c_str());
    ImGui::PopFont();
    // Right: undo / redo / save, help, add.
    float right = (ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x);
    float width = ImGui::CalcTextSize("Undo  Redo  Save layout  Help (F1)  Add (B)").x + 70.0f * g_scale;
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
        ImGui::SetTooltip("Ctrl+S: this level's layout, put back whenever it loads");
    ImGui::SameLine();
    if (ImGui::SmallButton("Help (F1)"))
        Ask(Request::kToggleHelp);
    ImGui::SameLine();
    if (ImGui::SmallButton("Add (B)"))
        Ask(Request::kToggleBrowser);
    ImGui::End();
}

// The Outliner's rows: each node's label (made again when the node arrays change) and the families' node lists.
struct OutlinerCache {
    const Nodes::Node* data = nullptr;
    size_t size = 0;
    std::vector<std::string> labels;
    std::vector<int> byFamily[Nodes::kFamilies];
};
OutlinerCache g_cache;
int g_lastSelected = -1;

void UpdateCache()
{
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    if (g_cache.data == nodes.data() && g_cache.size == nodes.size())
        return;
    g_cache.data = nodes.data();
    g_cache.size = nodes.size();
    g_cache.labels.clear();
    for (std::vector<int>& list : g_cache.byFamily)
        list.clear();
    for (size_t i = 0; i < nodes.size(); ++i) {
        g_cache.labels.push_back(nodes[i].name ? Nodes::Label(nodes[i].name) : "(no name)");
        g_cache.byFamily[static_cast<int>(nodes[i].family)].push_back(static_cast<int>(i));
    }
}

// What a row shows after the name: a spawn's types, a crown's index, a flag's colour, a rail's links.
std::string RowTag(const Nodes::Node& node)
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
                if (ImGui::Selectable(g_cache.labels[i].c_str(), isSelected,
                                      ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) {
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
    const Nodes::Node* data = nullptr;
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
    // Online: spawns, crowns and flags in a level shared with THUGPRO.
    if (Nodes::Online(node.family) && model.nodes.levelShared) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.37f, 0.6f, 0.35f));
        ImGui::BeginChild("##online", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::PushFont(g_heading);
        ImGui::TextColored(ImVec4(0.56f, 0.71f, 0.92f, 1.0f), "Online");
        ImGui::PopFont();
        ImGui::TextWrapped("This level is ported from %s and shared with THUGPRO. Its %s decide where players start "
                           "and play in net games: editing them changes online parity (the THUGPRO register, "
                           "tools/compat/thugpro). Read-only for now.",
                           model.nodes.sharedGame, Nodes::FamilyName(node.family));
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    if (ImGui::BeginTable("##node", 2, ImGuiTableFlags_SizingFixedFit)) {
        if (node.hasPos) {
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
            Mono("%d of %d restarts", node.order + 1, Nodes::Counts()[static_cast<int>(Nodes::Family::Spawn)]);
        }
        if (node.family == Nodes::Family::Crown) {
            FieldLabel("Crown");
            Mono("#%d of %d (the order the network sends)", node.order,
                 Nodes::Counts()[static_cast<int>(Nodes::Family::Crown)]);
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
    if (ImGui::Button("Go to"))
        Ask(Request::kGoToNode, index);
    // Every field of its structure.
    if (g_fields.node != index || g_fields.data != nodes.data()) {
        g_fields.node = index;
        g_fields.data = nodes.data();
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
            if (s.objects > 1 || s.sectors > 1) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                Muted("Selected");
                ImGui::TableNextColumn();
                Mono("%d", s.objects > 0 ? s.objects : s.sectors);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextWrapped("%s", model.selected.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Ground (G)"))
            Ask(Request::kDrop);
        ImGui::SameLine();
        if (ImGui::Button("Deselect (Esc)"))
            Ask(Request::kDeselect);
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
    ImGui::SetNextWindowSize(ImVec2(620.0f * g_scale, 0.0f));
    bool open = true;
    ImGui::Begin("Level editor - keys (F1 or Esc closes this)", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize);
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
        {"Selecting", "Click", "an object or scenery (scenery takes its group)"},
        {"", "Shift+click", "adds / removes"},
        {"", "Alt+click, middle click", "one piece of scenery"},
        {"", "Esc, Backspace", "deselects"},
        {"Moving", "Drag", "moves it (Shift: up / down; wheel: push / pull; Ctrl: snap to 25)"},
        {"", "Drag an arrow", "along that axis only"},
        {"", "W A S D / Q E", "nudge it"},
        {"", "Z / X", "turn (objects 15\xC2\xB0, scenery a quarter)"},
        {"", "G", "drops it onto the ground"},
        {"Editing", "Ctrl+D", "duplicates"},
        {"", "Delete", "hides scenery (deletes a copy)"},
        {"", "Home", "puts it back where the level had it"},
        {"", "Ctrl+Z / Ctrl+Y", "undo / redo"},
        {"Adding", "B", "the level's pieces and objects: Up / Down choose, Left / Right switch, F shows, Enter "
                        "places a copy"},
        {"Saving", "Ctrl+S", "this level's layout - it comes back with the level"},
        {"", "Ctrl+R / Ctrl+Shift+R", "restore / discard an older autosave"},
        {"Leaving", "Esc", "first cancels a drag, closes a window or deselects; then leaves"},
        {"Camera", "P", "writes where the camera is to the log (a menu backdrop view)"},
        {"Panels", "Mouse", "over a panel the UI takes the mouse; the keys stay the editor's unless you type"},
    };
    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
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
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s", row.keys);
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

void Browser(const Model& model)
{
    if (!model.browser || !model.browserItems)
        return;
    ImGui::SetNextWindowPos(ImVec2(g_layout.viewport.x + g_layout.viewport.width - 8.0f * g_scale,
                                   g_layout.viewport.y + 8.0f * g_scale),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(360.0f * g_scale, std::min(520.0f * g_scale, g_layout.viewport.height - 16.0f * g_scale)));
    bool open = true;
    char title[64];
    std::snprintf(title, sizeof(title), "Add  (%d)###browser", static_cast<int>(model.browserItems->size()));
    ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
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
        if (selected && follow)
            ImGui::SetScrollHereY(0.5f);
        ImGui::PopID();
    }
    ImGui::PopFont();
    ImGui::EndChild();
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

} // namespace

bool& Enabled()
{
    static bool on = ConfigInt("features", "editor_ui", 1) != 0;
    return on;
}

bool Running() { return g_running; }

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
    ImGui::PopFont();
    ImGui::Render();
    UiBackend::Render();
    g_wantMouse = io.WantCaptureMouse;
    g_wantKeys = io.WantCaptureKeyboard;
}

} // namespace Front::LevelEditor::Ui
