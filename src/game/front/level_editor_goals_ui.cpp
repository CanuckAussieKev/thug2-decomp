// NOT ORIGINAL - the level editor's goals: the Inspector's sections (Dear ImGui). See level_editor_goals.h.

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

#include "game/front/level_editor_goals.h"
#include "game/front/level_editor_hud.h"
#include "imgui.h"

namespace Front::LevelEditor::Goals {

namespace {

int g_newType = 0;

// A text field: typed, applied when let go (Enter or leaving it); its buffer follows `value` while it isn't edited.
struct TextBuffer {
    char text[128] = {};
    bool editing = false;
};
std::map<std::string, TextBuffer> g_buffers;

bool TextField(const char* label, const std::string& key, std::string* value, size_t limit, bool multiline)
{
    TextBuffer& b = g_buffers[key];
    if (!b.editing)
        std::snprintf(b.text, sizeof(b.text), "%s", value->c_str());
    size_t size = std::min(sizeof(b.text), limit + 1);
    if (multiline)
        ImGui::InputTextMultiline(label, b.text, size, ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.2f));
    else
        ImGui::InputText(label, b.text, size);
    b.editing = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        *value = b.text;
        return true;
    }
    return false;
}

// A number dragged: `*done` false while it is (the drag is one undo step).
bool IntField(const char* label, int* value, float speed, int low, int high, const char* format, bool* done)
{
    ImGui::SetNextItemWidth(150.0f * ImGui::GetFontSize() / 13.0f);
    bool changed = ImGui::DragInt(label, value, speed, low, high, format, ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivated())
        *done = true;
    return changed;
}

void Apply(const Goal& g, bool done)
{
    Goal changed = g;
    Queue([changed, done] { Edit(changed.id, changed, done); });
}

void Settings(const Goal& goal, ImFont* mono)
{
    Goal g = goal;
    bool done = false, changed = false;
    std::string key = g.id + "/";
    if (TextField("Name##goal", key + "name", &g.name, 16, false))
        changed = done = true; // (Create-a-Goal's keyboard: 16 characters)
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Name shown in View Goals");
    if (TextField("Ped name##goal", key + "ped", &g.ped, 16, false))
        changed = done = true;
    ImGui::TextUnformatted("Goal text");
    if (TextField("##goaltext", key + "text", &g.text, 99, true))
        changed = done = true;
    ImGui::TextUnformatted("Win message");
    if (TextField("##goalwin", key + "win", &g.win, 99, true))
        changed = done = true;
    ImGui::PushFont(mono);
    if (IntField("Time limit##goal", &g.time, 1.0f, 15, 3600, "%d s", &done))
        changed = true;
    if (g.type == Type::HighScore || g.type == Type::HighCombo) {
        if (IntField(g.type == Type::HighScore ? "High score##goal" : "High combo##goal", &g.score, 250.0f, 1000,
                     10000000, "%d", &done))
            changed = true;
    }
    if (g.type == Type::Race) {
        if (IntField("Laps##goal", &g.laps, 0.1f, 1, 10, "%d", &done))
            changed = true;
        if (IntField("Per-checkpoint##goal", &g.checkpointTime, 0.2f, 0, 60, g.checkpointTime ? "%d s" : "Off", &done))
            changed = true;
    }
    ImGui::PopFont();
    // (goal_update_control: SKATE letters and gap goals keep theirs; the trick goals have none)
    if (!TrickType(g.type) && g.type != Type::Skate && g.type != Type::Gap) {
        int control = g.walk ? 1 : 0;
        ImGui::SetNextItemWidth(150.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::Combo("Control##goal", &control, "Skate\0Walk\0")) {
            g.walk = control == 1;
            changed = done = true;
        }
    }
    if (g.type == Type::Gap) {
        ImGui::TextUnformatted("Gaps");
        std::vector<std::string> gaps = LevelGaps();
        for (const std::string& name : g.gaps) {
            if (std::find(gaps.begin(), gaps.end(), name) == gaps.end())
                gaps.push_back(name); // (one the list hasn't now: shown so it can be taken off)
        }
        if (gaps.empty())
            ImGui::TextDisabled("No gaps");
        ImGui::BeginChild("##goalgaps", ImVec2(0.0f, std::min(160.0f, 22.0f * gaps.size() + 4.0f)), true);
        for (const std::string& name : gaps) {
            auto it = std::find(g.gaps.begin(), g.gaps.end(), name);
            bool on = it != g.gaps.end();
            if (ImGui::Checkbox(name.c_str(), &on)) {
                if (on && g.gaps.size() < 10)
                    g.gaps.push_back(name);
                else if (!on)
                    g.gaps.erase(it);
                changed = done = true;
            }
        }
        ImGui::EndChild();
        if (TextField("Required trick##goal", key + "trick", &g.trick, 49, false))
            changed = done = true;
    }
    if (TrickType(g.type)) {
        ImGui::TextUnformatted("Key Combo Sets");
        for (int set = 0; set < ComboSetCount(g.type); ++set) {
            bool on = (g.sets & (1u << set)) != 0;
            if (ImGui::Checkbox(ComboSetName(set), &on)) {
                g.sets = on ? g.sets | (1u << set) : g.sets & ~(1u << set);
                changed = done = true;
            }
        }
        ImGui::PushFont(mono);
        if (g.type != Type::TrickBeat) {
            if (IntField("Spin##goal", &g.spin, 2.0f, 0, 900, g.spin ? "%d" : "Off", &done))
                changed = true;
        }
        if (g.type == Type::ComboSkatetris) {
            if (ImGui::Checkbox("Single combo##goal", &g.singleCombo))
                changed = done = true;
            if (IntField("Combo size##goal", &g.comboSize, 0.1f, 1, 15, "%d", &done))
                changed = true;
        }
        if (g.type == Type::TrickTris) {
            if (IntField("Block size##goal", &g.blockSize, 0.1f, 1, 15, "%d", &done))
                changed = true;
            if (IntField("Total to win##goal", &g.toWin, 0.5f, 1, 1000, "%d", &done))
                changed = true;
        }
        if (g.type == Type::Skatetris || (g.type == Type::ComboSkatetris && !g.singleCombo)) {
            if (IntField("Accel. interval##goal", &g.accelInterval, 0.1f, 1, 10, "%d", &done))
                changed = true;
            int percent = static_cast<int>(g.accelPercent * 100.0f + 0.01f);
            if (IntField("Accel. percent##goal", &percent, 0.2f, 1, 100, "%d%%", &done)) {
                g.accelPercent = percent / 100.0f;
                changed = true;
            }
            if (IntField("Trick time##goal", &g.trickTime, 5.0f, 5, 3000, "%d", &done))
                changed = true;
            if (IntField("Stop adding at##goal", &g.stopAt, 0.5f, 5, std::max(5, g.time - 5), "%d s", &done))
                changed = true;
            if (IntField("Max tricks##goal", &g.maxTricks, 0.1f, 1, 15, "%d", &done))
                changed = true;
        }
        ImGui::PopFont();
    }
    if (changed)
        Apply(g, done);
    else if (done)
        Queue([] { EndDrag(); });
}

} // namespace

void InspectorSection(uint32_t node, ImFont* mono, ImFont* smallFont)
{
    std::string id;
    int point = -1;
    if (!node || !PointOfNode(node, &id, &point))
        return;
    std::vector<Goal> all = All();
    auto it = std::find_if(all.begin(), all.end(), [&](const Goal& g) { return g.id == id; });
    if (it == all.end())
        return;
    const Goal& g = *it;
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Goal###goal", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::TextWrapped("%s - %s (%s)", g.name.c_str(), TypeTitle(g.type), PointLabel(g.type, point).c_str());
    std::string why;
    if (Complete(g, &why)) {
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "Complete");
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.35f, 1.0f), "Not played: %s", why.c_str());
    }
    int missing = Missing(g.id);
    if (missing > 0) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%d point%s deleted", missing, missing == 1 ? "" : "s");
        ImGui::SameLine();
        std::string goalId = g.id;
        if (ImGui::SmallButton("Restore Points"))
            Queue([goalId] { RestorePoints(goalId); });
    }
    // The point: its height (letters float above their node).
    if (point >= 2) {
        Goal changed = g;
        float height = changed.points[point].height;
        ImGui::PushFont(mono);
        ImGui::SetNextItemWidth(150.0f * ImGui::GetFontSize() / 13.0f);
        bool dragged = ImGui::DragFloat("Height##goalpoint", &height, 0.5f, 0.0f, 1000.0f, "%.1f",
                                        ImGuiSliderFlags_AlwaysClamp);
        bool done = ImGui::IsItemDeactivated();
        ImGui::PopFont();
        if (dragged) {
            changed.points[point].height = height;
            Apply(changed, done);
        } else if (done) {
            Queue([] { EndDrag(); });
        }
    }
    // The goal's points: select one.
    ImGui::PushFont(smallFont);
    ImGui::TextUnformatted("Points:");
    for (size_t k = 0; k < g.points.size(); ++k) {
        ImGui::SameLine();
        std::string label = PointLabel(g.type, static_cast<int>(k));
        if (label.rfind("Checkpoint ", 0) == 0)
            label = "CP" + label.substr(11);
        label += "##goalpt" + std::to_string(k);
        std::string goalId = g.id;
        int index = static_cast<int>(k);
        if (ImGui::SmallButton(label.c_str()))
            Queue([goalId, index] { Select(goalId, index); });
    }
    ImGui::PopFont();
    if (g.type == Type::Race) {
        std::string goalId = g.id;
        if (static_cast<int>(g.points.size()) < MaxPoints(g.type) && ImGui::Button("Add checkpoint##goal"))
            Queue([goalId] { AddCheckpoint(goalId); });
        if (static_cast<int>(g.points.size()) > MinPoints(g.type)) {
            ImGui::SameLine();
            if (ImGui::Button("Remove last##goal"))
                Queue([goalId] { RemoveCheckpoint(goalId); });
        }
    }
    Settings(g, mono);
    ImGui::Spacing();
    std::string goalId = g.id;
    if (ImGui::Button("Copy to Create-a-Goal##goal")) {
        Queue([goalId] {
            std::string message;
            Export(goalId, &message);
            Hud::Message(message, 6.0);
        });
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy to the game's Create-a-Goal list");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.22f, 0.30f, 0.85f));
    if (ImGui::Button("Delete goal##goal"))
        Queue([goalId] { Remove(goalId); });
    ImGui::PopStyleColor();
    
}

void ListSection(ImFont* mono, ImFont* smallFont)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Goals###goals"))
        return;
    ImGui::PushFont(smallFont);
    ImGui::TextDisabled("Goals for this level, saved with the layout");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(170.0f * ImGui::GetFontSize() / 13.0f);
    if (ImGui::BeginCombo("##goaltype", TypeTitle(static_cast<Type>(g_newType)))) {
        for (int t = 0; t < kTypes; ++t) {
            if (ImGui::Selectable(TypeTitle(static_cast<Type>(t)), t == g_newType))
                g_newType = t;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    std::vector<Goal> all = All();
    if (static_cast<int>(all.size()) >= kMaxGoals)
        ImGui::BeginDisabled();
    if (ImGui::Button("New goal")) {
        Type type = static_cast<Type>(g_newType);
        Queue([type] { Create(type); });
    }
    if (static_cast<int>(all.size()) >= kMaxGoals)
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add at view center");
    if (all.empty())
        ImGui::TextDisabled("No goals (max 10)");
    ImGui::PushFont(mono);
    for (const Goal& g : all) {
        ImGui::PushID(g.id.c_str());
        bool complete = Complete(g);
        ImGui::TextColored(complete ? ImVec4(0.90f, 0.80f, 0.45f, 1.0f) : ImVec4(0.75f, 0.55f, 0.40f, 1.0f), "%s",
                           g.name.c_str());
        if (ImGui::IsItemHovered()) {
            std::string why;
            ImGui::SetTooltip("%s - %s", TypeTitle(g.type), Complete(g, &why) ? "complete" : why.c_str());
        }
        ImGui::SameLine();
        std::string goalId = g.id;
        if (ImGui::SmallButton("Select"))
            Queue([goalId] { Select(goalId, 0); });
        ImGui::PopID();
    }
    ImGui::PopFont();
}

} // namespace Front::LevelEditor::Goals
