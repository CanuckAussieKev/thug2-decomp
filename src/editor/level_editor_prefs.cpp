// NOT ORIGINAL - the level editor's preferences and first-run tips (ours). See level_editor_prefs.h. Only ImGui's own
// API here (the renderer-independence rules: level_editor_ui_backend.h).

#include "editor/level_editor_prefs.h"

#include <algorithm>
#include <array>

#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/core/checked_index.h"

namespace frontend::level_editor::prefs {

Values get_clamped(Values v)
{
    v.fly_speed_ = std::clamp(v.fly_speed_, 50, 20000);
    v.fly_accel_ms_ = std::clamp(v.fly_accel_ms_, 0, 3000);
    v.look_pct_ = std::clamp(v.look_pct_, 10, 500);
    v.gizmo_pct_ = std::clamp(v.gizmo_pct_, 25, 400);
    v.ui_scale_pct_ = v.ui_scale_pct_ <= 0 ? 0 : std::clamp(v.ui_scale_pct_, 75, 400);
    v.autosave_seconds_ = std::clamp(v.autosave_seconds_, 10, 3600);
    v.snap_move_ = std::clamp(v.snap_move_, 1, 1000);
    v.snap_turn_ = std::clamp(v.snap_turn_, 1, 90);
    v.accent = std::clamp(v.accent, 0, kAccentCount - 1);
    return v;
}

void get_accent_rgb(int accent, float rgb[3])
{
    static const std::array<std::array<float, 3>, kAccentCount> kRgb = {{
        {0.82f, 0.34f, 0.11f}, // orange (the plan's mockup: the editor's own until now)
        {0.20f, 0.48f, 0.86f}, // blue
        {0.24f, 0.62f, 0.30f}, // green
        {0.58f, 0.36f, 0.84f}, // purple
        {0.80f, 0.22f, 0.26f}, // red
        {0.10f, 0.60f, 0.62f}, // teal
    }};
    const float* c = core::at(kRgb, std::clamp(accent, 0, kAccentCount - 1)).data();
    rgb[0] = c[0];
    rgb[1] = c[1];
    rgb[2] = c[2];
}

const char* accent_name(int accent)
{
    static const std::array<const char*, kAccentCount> kNames = {{"Orange", "Blue", "Green", "Purple", "Red", "Teal"}};
    return core::at(kNames, std::clamp(accent, 0, kAccentCount - 1));
}

namespace {

bool g_loaded;
Values g_values;
int g_style_generation;
bool g_open;
bool g_tips; // (the tips window is up)
bool g_tips_shown_this_run;

// One [editor] key per value: its name, and the value as an int.
struct Key {
    const char* name;
    int Values::* field;
};
constexpr auto kIntKeys = std::to_array<Key>({
    {"fly_speed", &Values::fly_speed_},
    {"fly_accel_ms", &Values::fly_accel_ms_},
    {"look_pct", &Values::look_pct_},
    {"gizmo_pct", &Values::gizmo_pct_},
    {"ui_scale_pct", &Values::ui_scale_pct_},
    {"autosave_s", &Values::autosave_seconds_},
    {"snap_move", &Values::snap_move_},
    {"snap_turn", &Values::snap_turn_},
    {"accent", &Values::accent},
});
struct BitMark {
    const char* name;
    bool Values::* field;
};
constexpr auto kFlags = std::to_array<BitMark>({
    {"invert_y", &Values::invert_y_},
    {"wasd_flies", &Values::wasd_flies_},
    {"speed_by_selection", &Values::speed_by_selection_},
    {"tips_seen", &Values::tips_seen_},
});

void read_in()
{
    Values v;
    for (const Key& k : kIntKeys)
        v.*k.field = config_int("editor", k.name, v.*k.field);
    for (const BitMark& f : kFlags)
        v.*f.field = config_int("editor", f.name, v.*f.field ? 1 : 0) != 0;
    g_values = get_clamped(v);
    g_loaded = true;
}

void store_away(const Values& before, const Values& after)
{
    for (const Key& k : kIntKeys) {
        if (before.*k.field != after.*k.field)
            config_set_int("editor", k.name, after.*k.field);
    }
    for (const BitMark& f : kFlags) {
        if (before.*f.field != after.*f.field)
            config_set_int("editor", f.name, after.*f.field ? 1 : 0);
    }
}

void save_all()
{
    for (const Key& k : kIntKeys)
        config_set_int("editor", k.name, g_values.*k.field);
    for (const BitMark& f : kFlags)
        config_set_int("editor", f.name, g_values.*f.field ? 1 : 0);
}

void get_tip(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

} // namespace

const Values& get_pull()
{
    if (!g_loaded)
        read_in();
    return g_values;
}

void assign_value(const Values& values, bool save)
{
    const Values before = get_pull();
    const Values after = get_clamped(values);
    if (after.ui_scale_pct_ != before.ui_scale_pct_ || after.accent != before.accent)
        ++g_style_generation;
    g_values = after;
    if (save)
        store_away(before, after);
}

int style_generation() { return g_style_generation; }

bool& unseal() { return g_open; }

void pane(float scale, ImFont* heading, ImFont* small)
{
    if (!g_open)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440.0f * scale, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin("Preferences###editorprefs", &g_open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    Values v = get_pull();
    bool changed = false;
    // (changes apply at once; the ini is written when a slider is let go, or at a click)
    static bool s_prev_focused;
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
            get_tip(tip);
    };
    auto check = [&](const char* label, bool* value, const char* tip) {
        if (ImGui::Checkbox(label, value))
            changed = true;
        if (tip)
            get_tip(tip);
    };
    section("Camera");
    slider("Fly Speed", &v.fly_speed_, 50, 20000, "%d units/s", "Initial fly speed");
    slider("Acceleration", &v.fly_accel_ms_, 0, 3000, "%d ms", "Time to reach full speed");
    slider("Mouse Sensitivity", &v.look_pct_, 10, 500, "%d%%", "Look and orbit sensitivity");
    check("Invert Mouse Y", &v.invert_y_, nullptr);
    check("WASD Always Flies", &v.wasd_flies_, "Fly without holding the right mouse button");
    check("Fly Speed from Selection Size", &v.speed_by_selection_, "Frame Selection sets fly speed from its size");
    section("Editing");
    slider("Gizmo Size", &v.gizmo_pct_, 25, 400, "%d%%", nullptr);
    slider("Grid Size", &v.snap_move_, 1, 1000, "%d units", nullptr);
    slider("Rotation Snap", &v.snap_turn_, 1, 90, "%d degrees", nullptr);
    slider("Autosave Interval", &v.autosave_seconds_, 10, 3600, "%d s", nullptr);
    section("Look");
    bool auto_scale = v.ui_scale_pct_ == 0;
    if (ImGui::Checkbox("Automatic UI Scale", &auto_scale)) {
        v.ui_scale_pct_ = auto_scale ? 0 : 100;
        changed = true;
    }
    get_tip("Based on window size (1080p: 100%, 1440p: 125%, 4K: 200%)");
    if (!auto_scale)
        slider("UI Scale", &v.ui_scale_pct_, 75, 400, "%d%%", nullptr);
    ImGui::TextUnformatted("Accent");
    for (int i = 0; i < kAccentCount; ++i) {
        std::array<float, 3> rgb{};
        get_accent_rgb(i, rgb.data());
        ImGui::SameLine(0.0f, 6.0f * scale);
        ImGui::PushID(i);
        const ImVec4 colour(core::at(rgb, 0), core::at(rgb, 1), core::at(rgb, 2), 1.0f);
        if (ImGui::ColorButton(accent_name(i), colour, ImGuiColorEditFlags_NoTooltip,
                               ImVec2((i == v.accent ? 22.0f : 16.0f) * scale, 16.0f * scale))) {
            v.accent = i;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", accent_name(i));
        ImGui::PopID();
    }
    ux8::prefs_section(); // (UX8: controller mode - level_editor_ux8.h)
    // The keymap (level_editor_actions.h - UX3's Keys section, [editor_keys] in the ini).
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Keys###prefskeys")) {
        ImGui::BeginChild("##prefskeys", ImVec2(0.0f, 380.0f * scale), ImGuiChildFlags_Borders);
        ux3::keys_section();
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
        d.tips_seen_ = v.tips_seen_;
        v = d;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Show tips"))
        show_tips();
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        g_open = false;
    if (changed || active)
        assign_value(v, false);
    if ((changed && !active) || (s_prev_focused && !active))
        save_all();
    s_prev_focused = active;
    ImGui::End();
}

void show_tips()
{
    g_tips = true;
    g_tips_shown_this_run = true;
}

void tips_window(float scale, ImFont* heading, ImFont* mono)
{
    // (the first time the editor opens: once a run until dismissed for good)
    if (!g_tips && !g_tips_shown_this_run && !get_pull().tips_seen_) {
        g_tips = true;
        g_tips_shown_this_run = true;
    }
    if (!g_tips)
        return;
    const ImGuiIO& io = ImGui::GetIO();
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
    const bool dismiss = ImGui::Button("Got it");
    ImGui::SameLine();
    ImGui::TextDisabled("(Help > Show Tips shows this again)");
    ImGui::End();
    if (dismiss || !open) {
        g_tips = false;
        if (!get_pull().tips_seen_) {
            Values v = get_pull();
            v.tips_seen_ = true;
            assign_value(v);
        }
    }
}

} // namespace frontend::level_editor::prefs
