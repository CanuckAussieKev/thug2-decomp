// NOT ORIGINAL - the level editor's history, changes, search and organising: the panels (ours). See level_editor_ux3.h.
// Only ImGui's own API here (and imgui_internal.h for the Inspector's folding state).

#include "game/front/level_editor_ux3.h"

#include <SDL2/SDL.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_clip.h"
#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_organize.h"
#include "game/front/level_editor_ui.h"
#include "game/sys/log.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace Front::LevelEditor::Ux3 {

namespace {

Fonts g_fonts;
const ImVec4 kAccent(0.82f, 0.34f, 0.11f, 0.85f);
const ImVec4 kKeyColour(1.0f, 0.78f, 0.45f, 1.0f);
const ImVec4 kMuted(0.6f, 0.61f, 0.63f, 1.0f);

float S() { return g_fonts.scale > 0.0f ? g_fonts.scale : 1.0f; }

// The right end of the window's content (local x), a scrollbar's room left.
float RightEdge() { return ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ScrollbarSize; }

void Mono(const char* text)
{
    if (g_fonts.mono)
        ImGui::PushFont(g_fonts.mono);
    ImGui::TextUnformatted(text);
    if (g_fonts.mono)
        ImGui::PopFont();
}

void Heading(const char* text)
{
    if (g_fonts.heading)
        ImGui::PushFont(g_fonts.heading);
    ImGui::TextUnformatted(text);
    if (g_fonts.heading)
        ImGui::PopFont();
}

// A window over the view, placed the first time (then where the user leaves it this session).
bool BeginFloating(const char* title, bool* open, ImVec2 pos, ImVec2 size)
{
    ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    return ImGui::Begin(title, open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
}

// --- the command search ---

char g_query[128];
int g_highlight;
bool g_paletteWasOpen;

void Palette()
{
    bool& open = Open(kPaletteWindow);
    if (!open) {
        g_paletteWasOpen = false;
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.1f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(640.0f * S(), 0.0f));
    ImGui::Begin("Command Search###ux3palette", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize);
    if (!g_paletteWasOpen) {
        g_query[0] = 0;
        g_highlight = 0;
        ImGui::SetKeyboardFocusHere();
        g_paletteWasOpen = true;
    }
    ImGui::SetNextItemWidth(-1.0f);
    bool enter = ImGui::InputTextWithHint("##query", "Search commands\xE2\x80\xA6", g_query,
                                          sizeof(g_query), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemActive() || enter) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            ++g_highlight;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            --g_highlight;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            open = false;
    }
    const std::vector<Actions::Action>& all = Actions::All();
    std::vector<std::pair<int, int>> found; // (score, index)
    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        const Actions::Action& a = all[i];
        int score = std::max({Actions::FuzzyScore(g_query, a.name) * 2, Actions::FuzzyScore(g_query, a.category + " " + a.name),
                              std::strlen(g_query) >= 3 ? Actions::FuzzyScore(g_query, a.tip) / 2 : 0,
                              Actions::FuzzyScore(g_query, Actions::KeysText(i))});
        if (score > 0)
            found.push_back({score, i});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    g_highlight = found.empty() ? 0 : std::clamp(g_highlight, 0, static_cast<int>(found.size()) - 1);
    float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("##results", ImVec2(0.0f, rowHeight * std::min<float>(14.0f, std::max<float>(1.0f, static_cast<float>(found.size()))) + 8.0f * S()));
    int run = -1;
    for (int r = 0; r < static_cast<int>(found.size()); ++r) {
        int i = found[r].second;
        const Actions::Action& a = all[i];
        bool enabled = Actions::Enabled(i);
        ImGui::PushID(i);
        int checked = Actions::Checked(i);
        std::string text = (checked == 1 ? "[x] " : checked == 0 ? "[ ] " : "") + a.name;
        if (!enabled)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Selectable(text.c_str(), r == g_highlight))
            run = i;
        if (!enabled)
            ImGui::PopStyleColor();
        if (r == g_highlight && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
            ImGui::SetScrollHereY(0.5f);
        if (ImGui::IsItemHovered() && !a.tip.empty())
            ImGui::SetTooltip("%s%s", a.tip.c_str(), a.kind == Actions::Kind::Info ? "\n(mouse gesture)" : "");
        ImGui::SameLine(0.0f, 10.0f * S());
        ImGui::TextColored(kMuted, "%s", a.category.c_str());
        std::string keys = Actions::KeysText(i);
        if (!keys.empty()) {
            if (g_fonts.mono)
                ImGui::PushFont(g_fonts.mono);
            float width = ImGui::CalcTextSize(keys.c_str()).x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f * S(), RightEdge() - width));
            ImGui::TextColored(kKeyColour, "%s", keys.c_str());
            if (g_fonts.mono)
                ImGui::PopFont();
        }
        ImGui::PopID();
    }
    if (found.empty())
        ImGui::TextDisabled("Nothing matches");
    ImGui::EndChild();
    if (enter && !found.empty())
        run = found[g_highlight].second;
    ImGui::End();
    if (run >= 0) {
        open = false;
        if (Actions::Enabled(run)) {
            Queue([run] { Actions::Run(run); });
        } else {
            std::string why = all[run].kind == Actions::Kind::Info ? all[run].name + ": " + Actions::KeysText(run)
                                                                     : all[run].name + " is unavailable";
            Queue([why] { ShowMessage(why); });
        }
    }
}

// --- the undo history ---

void HistoryWindow()
{
    bool& open = Open(kHistoryWindow);
    if (!open)
        return;
    ImGuiIO& io = ImGui::GetIO();
    if (!BeginFloating("Undo history###ux3history", &open, ImVec2(io.DisplaySize.x - 700.0f * S(), 90.0f * S()),
                       ImVec2(340.0f * S(), 420.0f * S()))) {
        ImGui::End();
        return;
    }
    std::vector<HistoryEntry> undo = UndoHistory(), redo = RedoHistory();
    ImGui::TextDisabled("%d undo, %d redo",
                        static_cast<int>(undo.size()), static_cast<int>(redo.size()));
    ImGui::Separator();
    ImGui::BeginChild("##steps");
    int jump = 0;
    bool any = false;
    if (ImGui::Selectable("(start)", undo.empty()))
        jump = -static_cast<int>(undo.size()), any = true;
    for (int i = 0; i < static_cast<int>(undo.size()); ++i) {
        ImGui::PushID(i);
        bool current = i == static_cast<int>(undo.size()) - 1;
        char text[200];
        std::snprintf(text, sizeof(text), "%2d  %s", i + 1, undo[i].label.c_str());
        if (ImGui::Selectable(text, current))
            jump = -(static_cast<int>(undo.size()) - 1 - i), any = true;
        if (ImGui::IsItemHovered() && !undo[i].detail.empty())
            ImGui::SetTooltip("Selection: %s", undo[i].detail.c_str());
        if (current)
            ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    }
    for (int j = 0; j < static_cast<int>(redo.size()); ++j) {
        ImGui::PushID(1000 + j);
        char text[200];
        std::snprintf(text, sizeof(text), "%2d  %s  (undone)", static_cast<int>(undo.size()) + j + 1, redo[j].label.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Selectable(text, false))
            jump = j + 1, any = true;
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && !redo[j].detail.empty())
            ImGui::SetTooltip("Selection: %s", redo[j].detail.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
    if (any && jump)
        Queue([jump] { JumpHistory(jump); });
}

// --- the changes ---

char g_changesFilter[64];

void ChangesWindow()
{
    bool& open = Open(kChangesWindow);
    if (!open)
        return;
    ImGuiIO& io = ImGui::GetIO();
    if (!BeginFloating("Changes###ux3changes", &open, ImVec2(io.DisplaySize.x * 0.5f - 260.0f * S(), 90.0f * S()),
                       ImVec2(560.0f * S(), 480.0f * S()))) {
        ImGui::End();
        return;
    }
    const std::vector<ChangeRow>& rows = ChangeList();
    ImGui::TextDisabled("%d changes",
                       static_cast<int>(rows.size()));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter\xE2\x80\xA6", g_changesFilter, sizeof(g_changesFilter));
    ImGui::Separator();
    ImGui::BeginChild("##changes");
    if (rows.empty())
        ImGui::TextDisabled("No changes");
    for (const char* group : Changes::Groups()) {
        std::vector<const ChangeRow*> in;
        for (const ChangeRow& row : rows) {
            if (row.entry.group == group && Actions::FuzzyScore(g_changesFilter, row.entry.summary + " " + group) > 0)
                in.push_back(&row);
        }
        if (in.empty())
            continue;
        char header[96];
        std::snprintf(header, sizeof(header), "%s (%d)###chg_%s", group, static_cast<int>(in.size()), group);
        if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
            continue;
        for (const ChangeRow* row : in) {
            ImGui::PushID(row->entry.line);
            float buttons = ImGui::CalcTextSize("Go to  Revert").x + 4.0f * ImGui::GetStyle().FramePadding.x + 12.0f * S();
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - buttons);
            ImGui::TextUnformatted(row->entry.summary.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", row->record.c_str());
            ImGui::SameLine(RightEdge() - buttons + 6.0f * S());
            ImGui::BeginDisabled(row->entry.id == 0);
            if (ImGui::SmallButton("Go to"))
                GoTo(*row);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Revert"))
                Revert(*row);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Revert this change");
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// --- the keys ---

char g_keysFilter[64];

void KeysWindow()
{
    bool& open = Open(kKeysWindow);
    if (!open) {
        if (Actions::Capturing())
            Actions::CancelCapture();
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (!BeginFloating("Keys###ux3keys", &open, ImVec2(io.DisplaySize.x * 0.5f - 330.0f * S(), 80.0f * S()),
                       ImVec2(660.0f * S(), io.DisplaySize.y * 0.75f))) {
        ImGui::End();
        return;
    }
    KeysSection();
    ImGui::End();
}

// --- the snapshots ---

char g_snapshotName[40];
std::vector<std::string> g_snapshots;
uint32_t g_snapshotsAt;
bool g_snapshotsStale = true;

void SnapshotsWindow()
{
    bool& open = Open(kSnapshotsWindow);
    if (!open) {
        g_snapshotsStale = true;
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (!BeginFloating("Layout snapshots###ux3snapshots", &open, ImVec2(io.DisplaySize.x * 0.5f - 200.0f * S(), 110.0f * S()),
                       ImVec2(400.0f * S(), 360.0f * S()))) {
        ImGui::End();
        return;
    }
    if (g_snapshotsStale || SDL_GetTicks() - g_snapshotsAt > 2000) {
        g_snapshots = Organize::Snapshots();
        g_snapshotsAt = SDL_GetTicks();
        g_snapshotsStale = false;
    }
    std::string active = Organize::ActiveSnapshot();
    ImGui::TextDisabled("Named versions of the layout");
    ImGui::Text("Active: %s", active.empty() ? "(none)" : active.c_str());
    ImGui::Separator();
    ImGui::SetNextItemWidth(200.0f * S());
    bool enter = ImGui::InputTextWithHint("##name", "Name", g_snapshotName, sizeof(g_snapshotName),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Save Snapshot") || enter) && g_snapshotName[0]) {
        std::string name = g_snapshotName;
        Queue([name] {
            std::string message;
            Organize::SaveSnapshot(name, &message);
            ShowMessage(message, 5.0);
        });
        g_snapshotName[0] = 0;
        g_snapshotsStale = true;
    }
    ImGui::BeginChild("##snapshots");
    if (g_snapshots.empty())
        ImGui::TextDisabled("No snapshots");
    for (const std::string& name : g_snapshots) {
        ImGui::PushID(name.c_str());
        bool isActive = name == active;
        if (isActive)
            ImGui::TextColored(ImVec4(0.55f, 0.84f, 1.0f, 1.0f), "%s  (active)", name.c_str());
        else
            ImGui::TextUnformatted(name.c_str());
        ImGui::SameLine(RightEdge() - ImGui::CalcTextSize("Switch to  Delete").x - 30.0f * S());
        if (ImGui::SmallButton(isActive ? "Reload" : "Switch to")) {
            Queue([name] {
                std::string message;
                Organize::SwitchSnapshot(name, &message);
                ShowMessage(message, 6.0);
            });
            g_snapshotsStale = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Load this snapshot");
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete") && ImGui::GetIO().KeyShift) {
            Queue([name] {
                std::string message;
                Organize::DeleteSnapshot(name, &message);
                ShowMessage(message);
            });
            g_snapshotsStale = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shift+click to delete");
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

// --- the menus and name windows ---

char g_groupName[48];
char g_labelText[48];
uint32_t g_labelFor;
bool g_labelAsked, g_groupAsked;
int g_renameGroup = -1;
char g_renameText[48];
bool g_renameAsked;

void Menus()
{
    if (TakeSimilarMenu())
        ImGui::OpenPopup("Select similar###ux3similar");
    if (ImGui::BeginPopup("Select similar###ux3similar")) {
        Heading("Select Similar");
        ImGui::Separator();
        struct Item {
            const char* name;
            Organize::Similar by;
            const char* tip;
        };
        static const Item kItems[] = {
            {"Same mesh", Organize::Similar::Mesh, "Same model or scenery piece"},
            {"Same class", Organize::Similar::Class, "Same node class"},
            {"Same family", Organize::Similar::Family, "Same Outliner family"},
            {"Same type", Organize::Similar::Type, "Same node type"}};
        for (const Item& item : kItems) {
            Organize::Similar by = item.by;
            if (ImGui::MenuItem(item.name))
                Queue([by] { Organize::SelectSimilar(by); });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", item.tip);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Same Family (All)"))
            Queue([] { Organize::SelectFamily(); });
        if (ImGui::MenuItem("Invert Selection", Actions::KeysText(Actions::kInvertSelection).c_str()))
            Queue([] { Organize::InvertSelection(); });
        if (ImGui::MenuItem("Select all", Actions::KeysText(Actions::kSelectAll).c_str()))
            Queue([] { Organize::SelectAll(); });
        ImGui::EndPopup();
    }
    if (TakeGroupName() || g_groupAsked) {
        g_groupAsked = false;
        std::snprintf(g_groupName, sizeof(g_groupName), "Group");
        ImGui::OpenPopup("Group the selection###ux3group");
    }
    if (ImGui::BeginPopupModal("Group the selection###ux3group", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Group name:");
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##group", g_groupName, sizeof(g_groupName), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Make group") || enter) {
            std::string name = g_groupName;
            Queue([name] { Organize::MakeGroup(name); });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (g_labelAsked) {
        g_labelAsked = false;
        ImGui::OpenPopup("Label###ux3label");
    }
    if (ImGui::BeginPopupModal("Label###ux3label", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Label (empty clears):");
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##label", g_labelText, sizeof(g_labelText), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Set") || enter) {
            std::string text = g_labelText;
            uint32_t id = g_labelFor;
            Queue([id, text] {
                Organize::State next = Organize::Current();
                std::string clean;
                Organize::SetLabel(&next, id, Organize::CleanName(text, &clean) ? clean : "");
                Organize::Change(next, "Label");
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (g_renameAsked) {
        g_renameAsked = false;
        ImGui::OpenPopup("Rename group###ux3rename");
    }
    if (ImGui::BeginPopupModal("Rename group###ux3rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##rename", g_renameText, sizeof(g_renameText), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Rename") || enter) {
            std::string text = g_renameText;
            int index = g_renameGroup;
            Queue([index, text] { Organize::RenameGroup(index, text); });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// --- the Inspector's folding, remembered ---

// The Inspector's foldable sections (their ### ids - the windows' root, where they're drawn).
const char* const kSections[] = {"###fields",   "###levelprops", "###camera",   "###cameras", "###clusters",
                                 "###faceflags", "###locks",      "###ux3clip",  "###ux3multi"};
constexpr int kSectionCount = sizeof(kSections) / sizeof(kSections[0]);
int g_sectionState[kSectionCount];
bool g_sectionsLoaded;
ImGuiID g_sectionsWindow;

// --- the Inspector's shared fields ---

struct SharedEdit {
    uint32_t key = 0;
    char text[160] = "";
};
std::vector<SharedEdit> g_shared;
ImGuiID g_sharedActive;

} // namespace

// ---------------------------------------------------------------------------------------------------------------------

void SetFonts(const Fonts& fonts) { g_fonts = fonts; }

void Windows(const Ui::Model& model)
{
    (void)model;
    Palette();
    HistoryWindow();
    ChangesWindow();
    KeysWindow();
    SnapshotsWindow();
    Menus();
}

void HelpFromRegistry()
{
    const std::vector<Actions::Action>& all = Actions::All();
    std::vector<std::string> categories;
    for (const Actions::Action& a : all) {
        if (std::find(categories.begin(), categories.end(), a.category) == categories.end())
            categories.push_back(a.category);
    }
    ImGui::TextDisabled("All commands and their keys");
    if (ImGui::SmallButton("Keys..."))
        Open(kKeysWindow) = true;
    if (!ImGui::BeginTable("##registry", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
        return;
    ImGui::TableSetupColumn("##category", ImGuiTableColumnFlags_WidthFixed, 90.0f * S());
    ImGui::TableSetupColumn("##keys", ImGuiTableColumnFlags_WidthFixed, 190.0f * S());
    ImGui::TableSetupColumn("##what", ImGuiTableColumnFlags_WidthStretch);
    for (const std::string& category : categories) {
        bool first = true;
        for (int i = 0; i < static_cast<int>(all.size()); ++i) {
            const Actions::Action& a = all[i];
            if (a.category != category)
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (first)
                Heading(category.c_str());
            first = false;
            ImGui::TableNextColumn();
            if (g_fonts.mono)
                ImGui::PushFont(g_fonts.mono);
            ImGui::PushTextWrapPos(0.0f);
            std::string keys = Actions::KeysText(i);
            ImGui::TextColored(kKeyColour, "%s", keys.empty() ? "-" : keys.c_str());
            ImGui::PopTextWrapPos();
            if (g_fonts.mono)
                ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(a.name.c_str());
            if (ImGui::IsItemHovered() && !a.tip.empty())
                ImGui::SetTooltip("%s", a.tip.c_str());
        }
    }
    ImGui::EndTable();
}

void KeysSection()
{
    const std::vector<Actions::Action>& all = Actions::All();
    ImGui::TextDisabled("Click a key to rebind, + to add, right-click to remove");
    ImGui::SetNextItemWidth(220.0f * S());
    ImGui::InputTextWithHint("##keysfilter", "Filter\xE2\x80\xA6", g_keysFilter, sizeof(g_keysFilter));
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset all") && ImGui::GetIO().KeyShift) {
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
            Actions::ResetKeys(i);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shift+click to reset all keys");
    if (Actions::Capturing()) {
        const Actions::Action* a = Actions::Get(Actions::CapturingIndex());
        ImGui::TextColored(kKeyColour, "Press a key for \"%s\" (Esc: cancel)",
                           a ? a->name.c_str() : "?");
    }
    ImGui::Separator();
    ImGui::BeginChild("##keys");
    if (ImGui::BeginTable("##keytable", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed, 250.0f * S());
        ImGui::TableSetupColumn("##keys", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, 60.0f * S());
        std::string category;
        for (int i = 0; i < static_cast<int>(all.size()); ++i) {
            const Actions::Action& a = all[i];
            if (!a.rebindable || Actions::FuzzyScore(g_keysFilter, a.category + " " + a.name + " " + Actions::KeysText(i)) == 0)
                continue;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (a.category != category) {
                category = a.category;
                ImGui::TextColored(kMuted, "%s", category.c_str());
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(a.name.c_str());
            if (ImGui::IsItemHovered() && !a.tip.empty())
                ImGui::SetTooltip("%s", a.tip.c_str());
            ImGui::TableNextColumn();
            if (g_fonts.mono)
                ImGui::PushFont(g_fonts.mono);
            for (int k = 0; k < static_cast<int>(a.keys.size()); ++k) {
                ImGui::PushID(k);
                bool capturing = Actions::Capturing() && Actions::CapturingIndex() == i;
                std::string text = capturing ? "..." : Actions::FormatChord(a.keys[k]);
                if (ImGui::SmallButton(text.c_str()))
                    Actions::BeginCapture(i, k);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    std::vector<Actions::Chord> next = a.keys;
                    next.erase(next.begin() + k);
                    Actions::SetKeys(i, next);
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+"))
                Actions::BeginCapture(i, static_cast<int>(a.keys.size()));
            if (g_fonts.mono)
                ImGui::PopFont();
            std::vector<int> clashes = Actions::Conflicts(i);
            if (!clashes.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "also: %s%s", all[clashes.front()].name.c_str(),
                                   clashes.size() > 1 ? " ..." : "");
            }
            ImGui::TableNextColumn();
            bool asDefault = a.keys == a.defaults;
            ImGui::BeginDisabled(asDefault);
            if (ImGui::SmallButton("Reset"))
                Actions::ResetKeys(i);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Default: %s", Actions::FormatKeys(a.defaults).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

bool RowPasses(const Nodes::Node& node, const std::string& name, const char* search)
{
    static std::string last;
    static Organize::Filter filter;
    if (last != search) {
        last = search;
        filter = Organize::ParseFilter(last);
    }
    if (!search[0])
        return true;
    std::string label = filter.labeled || !filter.text.empty() ? RowLabel(node.name) : "";
    bool inGroup = !filter.group.empty() && Organize::InGroup(filter.group, node);
    bool locked = filter.locked && Organize::Locked(node.name);
    bool hidden = filter.hidden && Organize::HiddenInEditor(node);
    return Organize::RowPasses(filter, node.family, name, label, inGroup, locked, hidden);
}

std::string RowLabel(uint32_t name)
{
    const Organize::State& state = Organize::Current();
    return state.labels.empty() ? "" : Organize::LabelOf(state, name);
}

void Groups()
{
    const Organize::State& state = Organize::Current();
    if (state.groups.empty())
        return;
    char header[48];
    std::snprintf(header, sizeof(header), "Groups###ux3groups");
    bool open = ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    ImGui::TextDisabled("%d", static_cast<int>(state.groups.size()));
    if (!open)
        return;
    for (int i = 0; i < static_cast<int>(state.groups.size()); ++i) {
        const Organize::Group& g = state.groups[i];
        ImGui::PushID(i);
        char text[96];
        std::snprintf(text, sizeof(text), "%s  (%d)", g.name.c_str(), static_cast<int>(g.members.size()));
        if (ImGui::Selectable(text))
            Queue([i] { Organize::SelectGroup(i); });
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Click to select; right-click for options");
        if (ImGui::BeginPopupContextItem("##groupmenu")) {
            if (ImGui::MenuItem("Add Selection"))
                Queue([i] { Organize::AddToGroup(i); });
            if (ImGui::MenuItem("Rename...")) {
                g_renameGroup = i;
                std::snprintf(g_renameText, sizeof(g_renameText), "%s", g.name.c_str());
                g_renameAsked = true;
            }
            if (ImGui::MenuItem("Delete Group"))
                Queue([i] { Organize::DeleteGroup(i); });
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

float TogglesWidth()
{
    float glyph = ImGui::CalcTextSize("W").x;
    return 3.0f * (glyph + 6.0f * S()) + 6.0f * S();
}

void RowToggles(const Nodes::Node& node, float x)
{
    bool hidden = Organize::HiddenInEditor(node), locked = Organize::Locked(node.name),
         selectable = Organize::Selectable(node);
    struct Toggle {
        const char* on;
        const char* off;
        bool state;
        const char* tip;
    };
    const Toggle toggles[3] = {
        {"\xE2\x80\xA2", "-", !hidden, "Visible in editor"},
        {"L", "L", locked, "Locked against changes"},
        {"S", "S", selectable, "Selectable in the view"},
    };
    float step = ImGui::CalcTextSize("W").x + 6.0f * S();
    for (int t = 0; t < 3; ++t) {
        ImGui::SameLine(x + 6.0f * S() + step * static_cast<float>(t));
        const Toggle& toggle = toggles[t];
        // (lit when on: the lock orange; the eye and selectable light; off: dim)
        ImGui::PushID(t);
        ImGui::PushStyleColor(ImGuiCol_Text, toggle.state ? (t == 1 ? ImVec4(1.0f, 0.6f, 0.3f, 1.0f)
                                                                     : ImVec4(0.85f, 0.85f, 0.85f, 1.0f))
                                                          : ImVec4(0.38f, 0.39f, 0.41f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * S(), 0.0f));
        bool clicked = ImGui::SmallButton(toggle.state ? toggle.on : toggle.off);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", toggle.tip);
        if (clicked) {
            Nodes::Node copy = node;
            bool value = !toggle.state;
            if (t == 0)
                Queue([copy, value] { Organize::SetHiddenInEditor(copy, !value); });
            else if (t == 1)
                Queue([copy, value] { Organize::SetLocked(copy.name, value); });
            else
                Queue([copy, value] { Organize::SetSelectable(copy, value); });
        }
        ImGui::PopID();
    }
}

void RowMenu(const Nodes::Node& node)
{
    if (!ImGui::BeginPopupContextItem("##rowmenu"))
        return;
    Heading(Nodes::Label(node.name).c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("Label...")) {
        g_labelFor = node.name;
        std::snprintf(g_labelText, sizeof(g_labelText), "%s", RowLabel(node.name).c_str());
        g_labelAsked = true;
    }
    Nodes::Node copy = node;
    bool hidden = Organize::HiddenInEditor(node), locked = Organize::Locked(node.name), selectable = Organize::Selectable(node);
    if (ImGui::MenuItem("Hidden in Editor", nullptr, hidden))
        Queue([copy, hidden] { Organize::SetHiddenInEditor(copy, !hidden); });
    if (ImGui::MenuItem("Locked", nullptr, locked))
        Queue([copy, locked] { Organize::SetLocked(copy.name, !locked); });
    if (ImGui::MenuItem("Selectable", nullptr, selectable))
        Queue([copy, selectable] { Organize::SetSelectable(copy, !selectable); });
    ImGui::Separator();
    if (ImGui::MenuItem("Group Selection...", Actions::KeysText(Actions::kMakeGroup).c_str()))
        g_groupAsked = true;
    const Organize::State& state = Organize::Current();
    if (!state.groups.empty() && ImGui::BeginMenu("Add Selection to")) {
        for (int i = 0; i < static_cast<int>(state.groups.size()); ++i) {
            if (ImGui::MenuItem(state.groups[i].name.c_str()))
                Queue([i] { Organize::AddToGroup(i); });
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Select Similar...", Actions::KeysText(Actions::kSelectSimilar).c_str()))
        Queue([] { Actions::Trigger(Actions::kSelectSimilar); });
    ImGui::EndPopup();
}

std::string TakeOutlinerSearch() { return Organize::TakeOutlinerSearch(); }

void InspectorSection(const Ui::Model& model)
{
    (void)model;
    SelectionIds s = CurrentSelection();
    bool any = !s.objects.empty() || !s.sectors.empty() || s.node;
    ImGui::Spacing();
    // The clipboard (Ctrl+Shift+C / Ctrl+Shift+V).
    if (ImGui::CollapsingHeader("Copy / paste###ux3clip")) {
        ImGui::BeginDisabled(!any);
        if (ImGui::SmallButton("Copy Transform and Fields"))
            Queue([] {
                std::string m;
                Clip::CopyProps(&m);
                ShowMessage(m);
            });
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", Actions::KeysText(Actions::kCopyProps).c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(!any || !Clip::HaveTransform());
        if (ImGui::SmallButton("Paste Transform"))
            Queue([] {
                std::string m;
                Clip::PasteTransform(&m);
                ShowMessage(m);
            });
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", Actions::KeysText(Actions::kPasteTransform).c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(!any || Clip::FieldCount() == 0);
        if (ImGui::SmallButton("Paste fields"))
            Queue([] {
                std::string m;
                Clip::PasteFields(&m);
                ShowMessage(m);
            });
        ImGui::EndDisabled();
        ImGui::TextDisabled("Copied: %s", Clip::Describe().c_str());
    }
    // Several selected: their shared fields.
    std::vector<uint32_t> nodes;
    for (uint32_t id : s.objects.empty() ? s.sectors : s.objects) {
        if (Nodes::Find(id) >= 0 && nodes.size() < 64)
            nodes.push_back(id);
    }
    if (nodes.size() < 2)
        return;
    char header[64];
    std::snprintf(header, sizeof(header), "Shared fields (%d nodes)###ux3multi", static_cast<int>(nodes.size()));
    if (!ImGui::CollapsingHeader(header))
        return;
    // (the fields every selected node has and can edit; a value where they agree, "mixed" where not)
    std::vector<std::vector<Fields::Row>> rows;
    for (uint32_t id : nodes)
        rows.push_back(Fields::RowsOf(Nodes::All()[Nodes::Find(id)]));
    struct Shared {
        uint32_t key;
        Fields::Kind kind;
        std::string text;
        bool mixed;
    };
    std::vector<Shared> shared;
    for (const Fields::Row& row : rows.front()) {
        if (!row.editable || row.key == Fields::KeyPos() || row.key == Fields::KeyAngles() || row.key == Fields::KeyLinks())
            continue;
        std::string text = Fields::Format(row.value, Fields::NameOf);
        bool all = true, mixed = false;
        for (size_t n = 1; n < rows.size() && all; ++n) {
            auto it = std::find_if(rows[n].begin(), rows[n].end(), [&](const Fields::Row& r) { return r.key == row.key; });
            all = it != rows[n].end() && it->editable && it->value.kind == row.value.kind;
            mixed = mixed || (all && !Fields::SameValue(it->value, row.value));
        }
        if (all)
            shared.push_back({row.key, row.value.kind, text, mixed});
    }
    if (shared.empty()) {
        ImGui::TextDisabled("No shared editable fields");
        return;
    }
    ImGui::TextDisabled("Applies to all %d", static_cast<int>(nodes.size()));
    if (!ImGui::BeginTable("##shared", 2, ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("##k", ImGuiTableColumnFlags_WidthFixed, 120.0f * S());
    ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch);
    if (g_shared.size() != shared.size())
        g_shared.assign(shared.size(), SharedEdit{});
    for (size_t i = 0; i < shared.size(); ++i) {
        const Shared& f = shared[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", Fields::NameText(f.key, Fields::NameOf).c_str());
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(f.key));
        SharedEdit& edit = g_shared[i];
        ImGuiID id = ImGui::GetID("##value");
        // (the value shown afresh unless it's being typed in)
        if (g_sharedActive != id || edit.key != f.key) {
            edit.key = f.key;
            std::snprintf(edit.text, sizeof(edit.text), "%s", f.mixed ? "" : f.text.c_str());
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (g_fonts.mono)
            ImGui::PushFont(g_fonts.mono);
        bool enter = ImGui::InputTextWithHint("##value", f.mixed ? "mixed" : "", edit.text, sizeof(edit.text),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        if (g_fonts.mono)
            ImGui::PopFont();
        if (ImGui::IsItemActive())
            g_sharedActive = id;
        else if (g_sharedActive == id)
            g_sharedActive = 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s - %s", Fields::KindName(f.kind), f.mixed ? "mixed values" : "same on all");
        if (enter) {
            FieldSet set;
            set.key = f.key;
            set.kind = static_cast<int>(f.kind);
            set.text = edit.text;
            std::vector<uint32_t> targets = nodes;
            Queue([targets, set] {
                std::string message;
                int n = SetFieldsOnNodes(targets, {set}, "Field (all selected)", &message);
                ShowMessage(n ? "Set on " + std::to_string(n) + " nodes" + (message.empty() ? "" : " (" + message + ")")
                              : "Not set: " + message);
            });
            g_sharedActive = 0;
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void RememberSections()
{
    ImGuiWindow* window = ImGui::FindWindowByName("Inspector");
    if (!window)
        return;
    std::string ini = GameFolderPath("thug2-decomp.ini");
    if (!g_sectionsLoaded || g_sectionsWindow != window->ID) {
        g_sectionsLoaded = true;
        g_sectionsWindow = window->ID;
        for (int i = 0; i < kSectionCount; ++i) {
            int value = static_cast<int>(GetPrivateProfileIntA("editor_sections", kSections[i] + 3, -1, ini.c_str()));
            g_sectionState[i] = value;
            if (value == 0 || value == 1)
                window->StateStorage.SetInt(window->GetID(kSections[i]), value);
        }
        return;
    }
    for (int i = 0; i < kSectionCount; ++i) {
        int value = window->StateStorage.GetInt(window->GetID(kSections[i]), -1);
        if (value < 0 || value == g_sectionState[i])
            continue;
        g_sectionState[i] = value;
        WritePrivateProfileStringA("editor_sections", kSections[i] + 3, value ? "1" : "0", ini.c_str());
    }
}

} // namespace Front::LevelEditor::Ux3
