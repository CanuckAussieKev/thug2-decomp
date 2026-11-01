// NOT ORIGINAL - the level editor's goals: the Inspector's sections (Dear ImGui). See level_editor_goals.h.

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

#include "editor/level_editor_goals.h"
#include "editor/level_editor_hud.h"
#include "imgui.h"

namespace frontend::level_editor::goal_edit {

namespace {

int g_new_type = 0;

// A text field: typed, applied when let go (Enter or leaving it); its buffer follows `value` while it isn't edited.
struct TextBuffer {
    char text[128] = {};
    bool editing = false;
};
std::map<std::string, TextBuffer> g_buffers;

bool text_field(const char* label, const std::string& key, std::string* value, size_t limit, bool multiline)
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
bool int_field(const char* label, int* value, float speed, int low, int high, const char* format, bool* done)
{
    ImGui::SetNextItemWidth(150.0f * ImGui::GetFontSize() / 13.0f);
    bool changed = ImGui::DragInt(label, value, speed, low, high, format, ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivated())
        *done = true;
    return changed;
}

void get_apply(const Goal& g, bool done)
{
    Goal changed = g;
    get_queue([changed, done] { get_edit(changed.id, changed, done); });
}

void current_settings(const Goal& goal, ImFont* mono)
{
    Goal g = goal;
    bool done = false, changed = false;
    std::string key = g.id + "/";
    if (text_field("Name##goal", key + "name", &g.name, 16, false))
        changed = done = true; // (Create-a-Goal's keyboard: 16 characters)
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Name shown in View Goals");
    if (text_field("Ped name##goal", key + "ped", &g.ped, 16, false))
        changed = done = true;
    ImGui::TextUnformatted("Goal text");
    if (text_field("##goaltext", key + "text", &g.text, 99, true))
        changed = done = true;
    ImGui::TextUnformatted("Win message");
    if (text_field("##goalwin", key + "win", &g.win, 99, true))
        changed = done = true;
    ImGui::PushFont(mono);
    if (int_field("Time limit##goal", &g.time, 1.0f, 15, 3600, "%d s", &done))
        changed = true;
    if (g.type == KindTag::kHighScore || g.type == KindTag::kHighCombo) {
        if (int_field(g.type == KindTag::kHighScore ? "High score##goal" : "High combo##goal", &g.score, 250.0f, 1000,
                      10000000, "%d", &done))
            changed = true;
    }
    if (g.type == KindTag::kRace) {
        if (int_field("Laps##goal", &g.laps, 0.1f, 1, 10, "%d", &done))
            changed = true;
        if (int_field("Per-checkpoint##goal", &g.checkpoint_time_, 0.2f, 0, 60, g.checkpoint_time_ ? "%d s" : "Off",
                      &done))
            changed = true;
    }
    ImGui::PopFont();
    // (goal_update_control: SKATE letters and gap goals keep theirs; the trick goals have none)
    if (!get_trick_type(g.type) && g.type != KindTag::kSkate && g.type != KindTag::kGap) {
        int control = g.walk ? 1 : 0;
        ImGui::SetNextItemWidth(150.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::Combo("Control##goal", &control, "Skate\0Walk\0")) {
            g.walk = control == 1;
            changed = done = true;
        }
    }
    if (g.type == KindTag::kGap) {
        ImGui::TextUnformatted("Gaps");
        std::vector<std::string> gaps = get_level_gaps();
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
        if (text_field("Required trick##goal", key + "trick", &g.trick, 49, false))
            changed = done = true;
    }
    if (get_trick_type(g.type)) {
        ImGui::TextUnformatted("Key Combo Sets");
        for (int set = 0; set < combo_set_count(g.type); ++set) {
            bool on = (g.sets & (1u << set)) != 0;
            if (ImGui::Checkbox(combo_set_name(set), &on)) {
                g.sets = on ? g.sets | (1u << set) : g.sets & ~(1u << set);
                changed = done = true;
            }
        }
        ImGui::PushFont(mono);
        if (g.type != KindTag::kTrickBeat) {
            if (int_field("Spin##goal", &g.spin, 2.0f, 0, 900, g.spin ? "%d" : "Off", &done))
                changed = true;
        }
        if (g.type == KindTag::kComboSkatetris) {
            if (ImGui::Checkbox("Single combo##goal", &g.single_combo_))
                changed = done = true;
            if (int_field("Combo size##goal", &g.combo_size_, 0.1f, 1, 15, "%d", &done))
                changed = true;
        }
        if (g.type == KindTag::kTrickTris) {
            if (int_field("Block size##goal", &g.block_size_, 0.1f, 1, 15, "%d", &done))
                changed = true;
            if (int_field("Total to win##goal", &g.to_win_, 0.5f, 1, 1000, "%d", &done))
                changed = true;
        }
        if (g.type == KindTag::kSkatetris || (g.type == KindTag::kComboSkatetris && !g.single_combo_)) {
            if (int_field("Accel. interval##goal", &g.accel_interval_, 0.1f, 1, 10, "%d", &done))
                changed = true;
            int percent = static_cast<int>(g.accel_percent_ * 100.0f + 0.01f);
            if (int_field("Accel. percent##goal", &percent, 0.2f, 1, 100, "%d%%", &done)) {
                g.accel_percent_ = percent / 100.0f;
                changed = true;
            }
            if (int_field("Trick time##goal", &g.combo_time_, 5.0f, 5, 3000, "%d", &done))
                changed = true;
            if (int_field("Stop adding at##goal", &g.stop_at_, 0.5f, 5, std::max(5, g.time - 5), "%d s", &done))
                changed = true;
            if (int_field("Max tricks##goal", &g.max_tricks_, 0.1f, 1, 15, "%d", &done))
                changed = true;
        }
        ImGui::PopFont();
    }
    if (changed)
        get_apply(g, done);
    else if (done)
        get_queue([] { end_drag(); });
}

} // namespace

void inspector_section(uint32_t node, ImFont* mono, ImFont* small_font)
{
    std::string id;
    int point = -1;
    if (!node || !point_of_node(node, &id, &point))
        return;
    std::vector<Goal> all = get_all();
    auto it = std::find_if(all.begin(), all.end(), [&](const Goal& g) { return g.id == id; });
    if (it == all.end())
        return;
    const Goal& g = *it;
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Goal###goal", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::TextWrapped("%s - %s (%s)", g.name.c_str(), type_title(g.type), point_label(g.type, point).c_str());
    std::string why;
    if (get_complete(g, &why)) {
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "Complete");
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.35f, 1.0f), "Not played: %s", why.c_str());
    }
    int missing = get_missing(g.id);
    if (missing > 0) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%d point%s deleted", missing, missing == 1 ? "" : "s");
        ImGui::SameLine();
        std::string goal_id = g.id;
        if (ImGui::SmallButton("Restore Points"))
            get_queue([goal_id] { restore_points(goal_id); });
    }
    // The point: its height (letters float above their node).
    if (point >= 2) {
        Goal changed = g;
        float height = changed.points[point].height;
        ImGui::PushFont(mono);
        ImGui::SetNextItemWidth(150.0f * ImGui::GetFontSize() / 13.0f);
        bool dragged =
            ImGui::DragFloat("Height##goalpoint", &height, 0.5f, 0.0f, 1000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        bool done = ImGui::IsItemDeactivated();
        ImGui::PopFont();
        if (dragged) {
            changed.points[point].height = height;
            get_apply(changed, done);
        } else if (done) {
            get_queue([] { end_drag(); });
        }
    }
    // The goal's points: select one.
    ImGui::PushFont(small_font);
    ImGui::TextUnformatted("Points:");
    for (size_t k = 0; k < g.points.size(); ++k) {
        ImGui::SameLine();
        std::string label = point_label(g.type, static_cast<int>(k));
        if (label.starts_with("Checkpoint "))
            label = "CP" + label.substr(11);
        label += "##goalpt" + std::to_string(k);
        std::string goal_id = g.id;
        int index = static_cast<int>(k);
        if (ImGui::SmallButton(label.c_str()))
            get_queue([goal_id, index] { choose_one(goal_id, index); });
    }
    ImGui::PopFont();
    if (g.type == KindTag::kRace) {
        std::string goal_id = g.id;
        if (static_cast<int>(g.points.size()) < max_points(g.type) && ImGui::Button("Add checkpoint##goal"))
            get_queue([goal_id] { add_checkpoint(goal_id); });
        if (static_cast<int>(g.points.size()) > min_points(g.type)) {
            ImGui::SameLine();
            if (ImGui::Button("Remove last##goal"))
                get_queue([goal_id] { remove_checkpoint(goal_id); });
        }
    }
    current_settings(g, mono);
    ImGui::Spacing();
    std::string goal_id = g.id;
    if (ImGui::Button("Copy to Create-a-Goal##goal")) {
        get_queue([goal_id] {
            std::string message;
            export_goal(goal_id, &message);
            hud::get_message(message, 6.0);
        });
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy to the game's Create-a-Goal list");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.22f, 0.30f, 0.85f));
    if (ImGui::Button("Delete goal##goal"))
        get_queue([goal_id] { drop_out(goal_id); });
    ImGui::PopStyleColor();
}

void list_section(ImFont* mono, ImFont* small_font)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Goals###goals"))
        return;
    ImGui::PushFont(small_font);
    ImGui::TextDisabled("Goals for this level, saved with the layout");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(170.0f * ImGui::GetFontSize() / 13.0f);
    if (ImGui::BeginCombo("##goaltype", type_title(static_cast<KindTag>(g_new_type)))) {
        for (int t = 0; t < kTypes; ++t) {
            if (ImGui::Selectable(type_title(static_cast<KindTag>(t)), t == g_new_type))
                g_new_type = t;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    std::vector<Goal> all = get_all();
    if (static_cast<int>(all.size()) >= kMaxGoals)
        ImGui::BeginDisabled();
    if (ImGui::Button("New goal")) {
        auto type = static_cast<KindTag>(g_new_type);
        get_queue([type] { fabricate(type); });
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
        bool complete = get_complete(g);
        ImGui::TextColored(complete ? ImVec4(0.90f, 0.80f, 0.45f, 1.0f) : ImVec4(0.75f, 0.55f, 0.40f, 1.0f), "%s",
                           g.name.c_str());
        if (ImGui::IsItemHovered()) {
            std::string why;
            ImGui::SetTooltip("%s - %s", type_title(g.type), get_complete(g, &why) ? "complete" : why.c_str());
        }
        ImGui::SameLine();
        std::string goal_id = g.id;
        if (ImGui::SmallButton("Select"))
            get_queue([goal_id] { choose_one(goal_id, 0); });
        ImGui::PopID();
    }
    ImGui::PopFont();
}

} // namespace frontend::level_editor::goal_edit
