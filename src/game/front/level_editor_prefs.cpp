// NOT ORIGINAL - the level editor's preferences and first-run tips (ours). See level_editor_prefs.h. Only ImGui's own
// API here (the renderer-independence rules: level_editor_ui_backend.h).

#include "game/front/level_editor_prefs.h"

#include <algorithm>

#include "game/front/level_editor_ux3.h"
#include "game/front/level_editor_ux8.h"
#include "game/sys/log.h"
#include "imgui.h"

namespace Front::LevelEditor::Prefs {

Values Clamped(Values v)
{
    v.flySpeed = std::clamp(v.flySpeed, 50, 20000);
    v.flyAccelMs = std::clamp(v.flyAccelMs, 0, 3000);
    v.lookPct = std::clamp(v.lookPct, 10, 500);
    v.gizmoPct = std::clamp(v.gizmoPct, 25, 400);
    v.uiScalePct = v.uiScalePct <= 0 ? 0 : std::clamp(v.uiScalePct, 75, 400);
    v.autosaveSeconds = std::clamp(v.autosaveSeconds, 10, 3600);
    v.snapMove = std::clamp(v.snapMove, 1, 1000);
    v.snapTurn = std::clamp(v.snapTurn, 1, 90);
    v.accent = std::clamp(v.accent, 0, kAccentCount - 1);
    return v;
}

void AccentRgb(int accent, float rgb[3])
{
    static const float kRgb[kAccentCount][3] = {
        {0.82f, 0.34f, 0.11f}, // orange (the plan's mockup: the editor's own until now)
        {0.20f, 0.48f, 0.86f}, // blue
        {0.24f, 0.62f, 0.30f}, // green
        {0.58f, 0.36f, 0.84f}, // purple
        {0.80f, 0.22f, 0.26f}, // red
        {0.10f, 0.60f, 0.62f}, // teal
    };
    const float* c = kRgb[std::clamp(accent, 0, kAccentCount - 1)];
    rgb[0] = c[0];
    rgb[1] = c[1];
    rgb[2] = c[2];
}

const char* AccentName(int accent)
{
    static const char* const kNames[kAccentCount] = {"Orange", "Blue", "Green", "Purple", "Red", "Teal"};
    return kNames[std::clamp(accent, 0, kAccentCount - 1)];
}

namespace {

bool g_loaded;
Values g_values;
int g_styleGeneration;
bool g_open;
bool g_tips;     // (the tips window is up)
bool g_tipsShownThisRun;

// One [editor] key per value: its name, and the value as an int.
struct Key {
    const char* name;
    int Values::*field;
};
const Key kIntKeys[] = {
    {"fly_speed", &Values::flySpeed},       {"fly_accel_ms", &Values::flyAccelMs}, {"look_pct", &Values::lookPct},
    {"gizmo_pct", &Values::gizmoPct},       {"ui_scale_pct", &Values::uiScalePct}, {"autosave_s", &Values::autosaveSeconds},
    {"snap_move", &Values::snapMove},       {"snap_turn", &Values::snapTurn},      {"accent", &Values::accent},
};
struct Flag {
    const char* name;
    bool Values::*field;
};
const Flag kFlags[] = {
    {"invert_y", &Values::invertY},
    {"wasd_flies", &Values::wasdFlies},
    {"speed_by_selection", &Values::speedBySelection},
    {"tips_seen", &Values::tipsSeen},
};

void Load()
{
    Values v;
    for (const Key& k : kIntKeys)
        v.*k.field = ConfigInt("editor", k.name, v.*k.field);
    for (const Flag& f : kFlags)
        v.*f.field = ConfigInt("editor", f.name, v.*f.field ? 1 : 0) != 0;
    g_values = Clamped(v);
    g_loaded = true;
}

void Save(const Values& before, const Values& after)
{
    for (const Key& k : kIntKeys) {
        if (before.*k.field != after.*k.field)
            ConfigSetInt("editor", k.name, after.*k.field);
    }
    for (const Flag& f : kFlags) {
        if (before.*f.field != after.*f.field)
            ConfigSetInt("editor", f.name, after.*f.field ? 1 : 0);
    }
}

void SaveAll()
{
    for (const Key& k : kIntKeys)
        ConfigSetInt("editor", k.name, g_values.*k.field);
    for (const Flag& f : kFlags)
        ConfigSetInt("editor", f.name, g_values.*f.field ? 1 : 0);
}

void Tip(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

} // namespace

const Values& Get()
{
    if (!g_loaded)
        Load();
    return g_values;
}

void Set(const Values& values, bool save)
{
    Values before = Get();
    Values after = Clamped(values);
    if (after.uiScalePct != before.uiScalePct || after.accent != before.accent)
        ++g_styleGeneration;
    g_values = after;
    if (save)
        Save(before, after);
}

int StyleGeneration() { return g_styleGeneration; }

bool& Open() { return g_open; }

void Window(float scale, ImFont* heading, ImFont* small)
{
    if (!g_open)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440.0f * scale, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin("Preferences###editorprefs", &g_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    Values v = Get();
    bool changed = false;
    // (changes apply at once; the ini is written when a slider is let go, or at a click)
    static bool s_wasActive;
    bool active = false;
    auto section = [&](const char* title) {
        ImGui::Spacing();
        if (heading)
            ImGui::PushFont(heading);
        ImGui::TextUnformatted(title);
        if (heading)
            ImGui::PopFont();
        ImGui::Separator();
    };
    auto slider = [&](const char* label, int* value, int lo, int hi, const char* format, const char* tip) {
        ImGui::SetNextItemWidth(200.0f * scale);
        if (ImGui::SliderInt(label, value, lo, hi, format, ImGuiSliderFlags_AlwaysClamp))
            changed = true;
        active |= ImGui::IsItemActive();
        if (tip)
            Tip(tip);
    };
    auto check = [&](const char* label, bool* value, const char* tip) {
        if (ImGui::Checkbox(label, value))
            changed = true;
        if (tip)
            Tip(tip);
    };
    section("Camera");
    slider("Fly Speed", &v.flySpeed, 50, 20000, "%d units/s", "Initial fly speed");
    slider("Acceleration", &v.flyAccelMs, 0, 3000, "%d ms", "Time to reach full speed");
    slider("Mouse Sensitivity", &v.lookPct, 10, 500, "%d%%", "Look and orbit sensitivity");
    check("Invert Mouse Y", &v.invertY, nullptr);
    check("WASD Always Flies", &v.wasdFlies, "Fly without holding the right mouse button");
    check("Fly Speed from Selection Size", &v.speedBySelection, "Frame Selection sets fly speed from its size");
    section("Editing");
    slider("Gizmo Size", &v.gizmoPct, 25, 400, "%d%%", nullptr);
    slider("Grid Size", &v.snapMove, 1, 1000, "%d units", nullptr);
    slider("Rotation Snap", &v.snapTurn, 1, 90, "%d degrees", nullptr);
    slider("Autosave Interval", &v.autosaveSeconds, 10, 3600, "%d s", nullptr);
    section("Look");
    bool autoScale = v.uiScalePct == 0;
    if (ImGui::Checkbox("Automatic UI Scale", &autoScale)) {
        v.uiScalePct = autoScale ? 0 : 100;
        changed = true;
    }
    Tip("Based on window size (1080p: 100%, 1440p: 125%, 4K: 200%)");
    if (!autoScale)
        slider("UI Scale", &v.uiScalePct, 75, 400, "%d%%", nullptr);
    ImGui::TextUnformatted("Accent");
    for (int i = 0; i < kAccentCount; ++i) {
        float rgb[3];
        AccentRgb(i, rgb);
        ImGui::SameLine(0.0f, 6.0f * scale);
        ImGui::PushID(i);
        ImVec4 colour(rgb[0], rgb[1], rgb[2], 1.0f);
        if (ImGui::ColorButton(AccentName(i), colour, ImGuiColorEditFlags_NoTooltip,
                               ImVec2((i == v.accent ? 22.0f : 16.0f) * scale, 16.0f * scale))) {
            v.accent = i;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", AccentName(i));
        ImGui::PopID();
    }
    Ux8::PrefsSection(); // (UX8: controller mode - level_editor_ux8.h)
    // The keymap (level_editor_actions.h - UX3's Keys section, [editor_keys] in the ini).
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Keys###prefskeys")) {
        ImGui::BeginChild("##prefskeys", ImVec2(0.0f, 380.0f * scale), ImGuiChildFlags_Borders);
        Ux3::KeysSection();
        ImGui::EndChild();
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (small)
        ImGui::PushFont(small);
    ImGui::TextDisabled("Saved in thug2-decomp.ini [editor]");
    if (small)
        ImGui::PopFont();
    if (ImGui::Button("Defaults")) {
        Values d;
        d.tipsSeen = v.tipsSeen;
        v = d;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Show tips"))
        ShowTips();
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        g_open = false;
    if (changed || active)
        Set(v, false);
    if ((changed && !active) || (s_wasActive && !active))
        SaveAll();
    s_wasActive = active;
    ImGui::End();
}

void ShowTips()
{
    g_tips = true;
    g_tipsShownThisRun = true;
}

void TipsWindow(float scale, ImFont* heading, ImFont* mono)
{
    // (the first time the editor opens: once a run until dismissed for good)
    if (!g_tips && !g_tipsShownThisRun && !Get().tipsSeen) {
        g_tips = true;
        g_tipsShownThisRun = true;
    }
    if (!g_tips)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.42f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(470.0f * scale, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    ImGui::Begin("Level Editor###editortips", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    if (heading)
        ImGui::PushFont(heading);
    ImGui::TextUnformatted("Essential Keys");
    if (heading)
        ImGui::PopFont();
    ImGui::Spacing();
    static const char* const kTips[][2] = {
        {"Right mouse + W A S D", "Fly (Q / E: down / up, Shift: faster)"},
        {"Click, drag", "Select and move (Shift+click: add, Esc: deselect)"},
        {"F", "Frame selection (Alt+drag: orbit)"},
        {"F5", "Play from here; F5 again returns"},
        {"F1", "Keyboard shortcuts (Ctrl+S: save)"},
    };
    if (ImGui::BeginTable("##tips", 2, ImGuiTableFlags_SizingFixedFit)) {
        for (const auto& tip : kTips) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (mono)
                ImGui::PushFont(mono);
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s", tip[0]);
            if (mono)
                ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 300.0f * scale);
            ImGui::TextUnformatted(tip[1]);
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    bool dismiss = ImGui::Button("Got it");
    ImGui::SameLine();
    ImGui::TextDisabled("(Help > Show Tips shows this again)");
    ImGui::End();
    if (dismiss || !open) {
        g_tips = false;
        if (!Get().tipsSeen) {
            Values v = Get();
            v.tipsSeen = true;
            Set(v);
        }
    }
}

} // namespace Front::LevelEditor::Prefs
