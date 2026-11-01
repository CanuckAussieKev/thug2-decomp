// NOT ORIGINAL - the level editor's trigger volumes in the UI (ours; see level_editor_triggers.h): the Triggers window (T)
// and the Inspector's section for a trigger. ImGui only; every change is queued for the editor's next update
// (Triggers::Queue).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "editor/level_editor_cameras.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_triggers.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "editor/level_editor_playtest.h"
#include "imgui.h"

namespace frontend::level_editor::triggers {

namespace {

ImVec4 get_colour(uint32_t argb)
{
    return {static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(argb & 0xFF) / 255.0f, static_cast<float>(argb >> 24) / 255.0f};
}

std::string get_name_of(uint32_t checksum) { return name_for(checksum, script::kept_qb_name(checksum)); }

// The level's restarts (names), and the scripts its nodes' TriggerScripts name - made again when the nodes change.
std::vector<std::string> g_restarts, g_scripts, g_cameras; // (map4: the editor's camera nodes)
uint32_t g_lists_generation = 0xFFFFFFFF;
void update_lists()
{
    if (g_lists_generation == nodes::get_generation())
        return;
    g_lists_generation = nodes::get_generation();
    g_restarts.clear();
    g_scripts.clear();
    g_cameras.clear();
    for (const cameras::ViewCam& camera : cameras::get_all())
        g_cameras.push_back(camera.name);
    static const uint32_t kTriggerScript = 0x2CA8A299;
    for (const nodes::Elem& node : nodes::get_all()) {
        if (node.family == nodes::Family::kSpawn && !node.hidden && node.name)
            g_restarts.push_back(get_name_of(node.name));
        if (!node.trigger_script_ || node.added)
            continue;
        script::Struct* s = nodes::struct_of(node);
        uint32_t script = 0;
        if (s && s->read_hash_value(kTriggerScript, &script, 0) && script) {
            const std::string name = get_name_of(script);
            if (std::find(g_scripts.begin(), g_scripts.end(), name) == g_scripts.end())
                g_scripts.push_back(name);
        }
    }
    std::sort(g_scripts.begin(), g_scripts.end());
}

// A name field: typed, applied when let go (Enter or leaving it). Its buffer follows `value` while it isn't edited.
bool name_field(const char* label, std::string* value, char* buffer, size_t size, bool* editing)
{
    if (!*editing) {
        std::snprintf(buffer, size, "%s", value->c_str());
    }
    ImGui::InputText(label, buffer, size);
    *editing = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        *value = buffer;
        return true;
    }
    return false;
}

// A choice among `names` (and the value itself if it isn't one of them).
bool name_combo(const char* label, std::string* value, const std::vector<std::string>& names)
{
    bool changed = false;
    if (ImGui::BeginCombo(label, value->empty() ? "(none)" : value->c_str(), ImGuiComboFlags_HeightLarge)) {
        for (const std::string& name : names) {
            const bool chosen = name == *value;
            if (ImGui::Selectable(name.c_str(), chosen)) {
                *value = name;
                changed = true;
            }
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// A breakable's sound: one of the shared hits, typed, or none.
bool sound_combo(std::string* sound)
{
    bool changed = false;
    if (ImGui::BeginCombo("Sound", sound->empty() ? "(none)" : sound->c_str())) {
        if (ImGui::Selectable("(none)", sound->empty()) && !sound->empty()) {
            sound->clear();
            changed = true;
        }
        for (const char* name : break_sounds()) {
            if (ImGui::Selectable(name, *sound == name) && *sound != name) {
                *sound = name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    static char typed[64];
    static bool editing;
    changed |= name_field("Sound Name", sound, typed, sizeof(typed), &editing);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Must be a sound the level loads");
    return changed;
}

// A proximity node's scripts and shape (level_editor_triggers.h: ProximEdit).
void proxim_section(uint32_t node)
{
    update_lists();
    ImGui::Spacing();
    ImGui::SeparatorText("Proximity Node");
    const ProximEdit* now = proxim_of(node);
    ProximEdit e = now ? *now : ProximEdit{};
    e.node = node;
    ImGui::TextDisabled("Scripts run on enter and exit");
    static char g_enter_buffer[64], g_exit_buffer[64];
    static bool g_enter_editing, g_exit_editing;
    bool changed = false;
    changed |= name_combo("On entering", &e.enter, g_scripts);
    changed |= name_field("Or by name##enter", &e.enter, g_enter_buffer, sizeof(g_enter_buffer), &g_enter_editing);
    changed |= name_combo("On leaving", &e.exit, g_scripts);
    changed |= name_field("Or by name##exit", &e.exit, g_exit_buffer, sizeof(g_exit_buffer), &g_exit_editing);
    ImGui::TextDisabled("(none: node's own script)");
    int shape = e.shape ? (e.box ? 2 : 1) : 0;
    ImGui::TextUnformatted("Shape");
    ImGui::SameLine();
    bool shape_changed = ImGui::RadioButton("Original", &shape, 0);
    ImGui::SameLine();
    shape_changed |= ImGui::RadioButton("sphere", &shape, 1);
    ImGui::SameLine();
    shape_changed |= ImGui::RadioButton("box", &shape, 2);
    if (shape_changed) {
        e.shape = shape != 0;
        e.box = shape == 2;
        if (e.shape && !e.box && e.radius <= 0.0f)
            e.radius = 200.0f;
        if (e.box && e.size.x <= 0.0f)
            e.size = {400.0f, 200.0f, 400.0f};
        changed = true;
    }
    if (e.shape && !e.box) {
        float radius = e.radius;
        if (ImGui::DragFloat("Radius", &radius, 2.0f, kProximMin, kProximMax, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
            ProximEdit sized = e;
            sized.radius = radius;
            get_queue([sized] { set_proxim(sized, false); });
        }
        if (ImGui::IsItemActivated())
            get_queue([] { begin_edit(); });
    } else if (e.shape) {
        float size[3] = {e.size.x, e.size.y, e.size.z};
        if (ImGui::DragFloat3("Size##proxim", size, 5.0f, kProximMin, kProximMax, "%.0f",
                              ImGuiSliderFlags_AlwaysClamp)) {
            ProximEdit sized = e;
            sized.size = {size[0], size[1], size[2]};
            get_queue([sized] { set_proxim(sized, false); });
        }
        if (ImGui::IsItemActivated())
            get_queue([] { begin_edit(); });
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("World-aligned box");
    }
    if (now && ImGui::Button("Revert"))
        get_queue([node] { set_proxim(ProximEdit{node}, true); });
    ImGui::TextDisabled("Drag the handles to resize");
    if (changed)
        get_queue([e] { set_proxim(e, true); });
}

const Added* added_named(uint32_t name)
{
    for (const Added& a : added_triggers()) {
        if (node_create::name_hash(a.spec.name) == name)
            return &a;
    }
    return nullptr;
}

const LevelTrigger* level_named(uint32_t sector)
{
    for (const LevelTrigger& l : level_triggers()) {
        if (l.sector == sector)
            return &l;
    }
    return nullptr;
}

void level_trigger_details(const LevelTrigger& l)
{
    ImGui::TextColored(get_colour(l.off ? 0xFFA0A0A0u : kind_colour(l.kind)), "%s", describe_level_trigger(l).c_str());
    ImGui::TextDisabled("Level trigger %s: %d trigger faces%s", nodes::caption(l.sector).c_str(), l.face_count_,
                        l.has_node_ ? "" : " (no node)");
    ImGui::TextDisabled("TriggerScript: %s", l.script ? nodes::caption(l.script).c_str() : "(none)");
    ImGui::TextDisabled("Box (%.0f, %.0f, %.0f) .. (%.0f, %.0f, %.0f)", l.min.x, l.min.y, l.min.z, l.max.x, l.max.y,
                        l.max.z);
    const uint32_t sector = l.sector;
    const bool switchable = l.wall || l.all_trigger_;
    if (l.off)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.4f, 1.0f), "Disabled");
    ImGui::BeginDisabled(!switchable);
    if (ImGui::Button(l.off ? "Switch on" : "Switch off"))
        get_queue([sector, off = !l.off] { turn_off(sector, off); });
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(switchable ? "Enable or disable this trigger" : "Has non-trigger faces; can't be disabled");
    if (l.wall) {
        ImGui::SameLine();
        if (ImGui::Button("Move it"))
            get_queue([sector] { select_wall(sector); });
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Move as an invisible wall (size is fixed)");
    }
    ImGui::BeginDisabled(!switchable || !l.script);
    if (ImGui::Button("Make Editable Copy"))
        get_queue([sector] { make_editable(sector); });
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Replace with an editable trigger volume; the original is disabled");
}

} // namespace

void pane(float view_x, float view_y, float view_width, float view_height, float scale, int selected_node)
{
    (void)view_height;
    (void)selected_node;
    if (!get_shown())
        return;
    update_lists();
    ImGui::SetNextWindowPos(ImVec2(view_x + view_width - 8.0f * scale, view_y + 40.0f * scale), ImGuiCond_FirstUseEver,
                            ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(380.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Triggers###triggers", &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open)
            get_queue([] { set_shown(false); });
        return;
    }
    ImGui::TextDisabled("Add at view center:");
    if (ImGui::Button("Kill zone"))
        get_queue([] { add_kill(false); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bail and respawn at a restart");
    ImGui::SameLine();
    if (ImGui::Button("Water"))
        get_queue([] { add_kill(true); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Splash and respawn at a restart");
    ImGui::SameLine();
    if (ImGui::Button("Teleport"))
        get_queue([] { add_teleport(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Teleport the skater to a node");
    ImGui::SameLine();
    if (ImGui::Button("Script"))
        get_queue([] { append_script(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Run a level script");
    ImGui::SameLine();
    if (ImGui::Button("Gap"))
        get_queue([] { register_gap_entry(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Gap start and end");
    ImGui::SameLine();
    if (ImGui::Button("Jump boost"))
        get_queue([] { add_boost(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Launch the skater upward");
    ImGui::SameLine();
    if (ImGui::Button("Camera"))
        get_queue([] { add_camera(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Play a camera shot");
    ImGui::TextDisabled("%s", describe().c_str());
    ImGui::TextDisabled("Breakables: select scenery, then Breaks When Hit");
    // The legend.
    for (int k = 0; k < kKinds; ++k) {
        if (k % 5)
            ImGui::SameLine();
        ImGui::TextColored(get_colour(kind_colour(static_cast<Kind>(k))), "%s", kind_name(static_cast<Kind>(k)));
    }
    ImGui::Separator();
    // The nearest (the editor's, then the level's), a click selects / inspects.
    if (ImGui::BeginChild("##triggerlist", ImVec2(0.0f, 180.0f * scale), ImGuiChildFlags_Borders)) {
        for (const Added& a : added_triggers()) {
            if (!a.alive)
                continue;
            std::string text = a.spec.name + "  " + kind_name(a.spec.kind);
            if ((a.spec.kind == Kind::kGapStart || a.spec.kind == Kind::kGapEnd) && !a.spec.text.empty())
                text += std::string("  - ") + play_test::gap_note(clean_text(a.spec.text)); // (landed in a test run?)
            ImGui::PushStyleColor(ImGuiCol_Text, get_colour(a.problem ? 0xFFA0A0A0u : kind_colour(a.spec.kind)));
            const uint32_t name = node_create::name_hash(a.spec.name);
            if (ImGui::Selectable((text + "##a" + a.spec.name).c_str(), false))
                get_queue([name] { select_node_named(name); });
            ImGui::PopStyleColor();
        }
        int shown = 0;
        for (const LevelTrigger& l : level_triggers()) {
            if (++shown > 400)
                break;
            const std::string text =
                nodes::caption(l.sector) + "  " + describe_level_trigger(l) + (l.off ? "  (off)" : "");
            ImGui::PushStyleColor(ImGuiCol_Text, get_colour(l.off ? 0xFFA0A0A0u : kind_colour(l.kind)));
            const uint32_t sector = l.sector;
            const bool has_node = l.has_node_;
            if (ImGui::Selectable((text + "##l" + std::to_string(l.sector)).c_str(), l.sector == inspected_sector()))
                get_queue([sector, has_node] {
                    inspect(sector);
                    if (has_node)
                        select_node_named(sector);
                });
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    if (const LevelTrigger* l = level_named(inspected_sector())) {
        ImGui::Separator();
        level_trigger_details(*l);
    }
    ImGui::End();
    if (!open)
        get_queue([] { set_shown(false); });
}

void inspector_section(int index)
{
    const std::vector<nodes::Elem>& all = nodes::get_all();
    if (index < 0 || index >= static_cast<int>(all.size()))
        return;
    const uint32_t name = all[index].name;
    if (const LevelTrigger* l = level_named(name)) {
        ImGui::Spacing();
        ImGui::SeparatorText("Trigger Volume");
        level_trigger_details(*l);
        return;
    }
    const Added* a = added_named(name);
    if (!a) {
        if (is_proxim(name))
            proxim_section(name);
        return;
    }
    update_lists();
    ImGui::Spacing();
    ImGui::SeparatorText("Trigger Volume");
    Spec s = a->spec;
    const std::string key = s.name;
    bool changed = false;
    if (s.kind == Kind::kBreak) {
        // (a breakable: its piece and sound - its box is the piece's)
        ImGui::TextColored(get_colour(kind_colour(s.kind)), "Breakable: %s%s", s.target.c_str(),
                           a->broken ? " (broken)" : "");
        if (sound_combo(&s.sound))
            get_queue([key, s] { set_spec(key, s, true); });
        ImGui::TextDisabled("Shatters when hit");
        return;
    }
    // The kind.
    const Kind kinds[] = {Kind::kKill,   Kind::kJumpTo, Kind::kScripts, Kind::kGapStart,
                          Kind::kGapEnd, Kind::kBoost,  Kind::kViewCam};
    if (ImGui::BeginCombo("Kind", kind_name(s.kind))) {
        for (const Kind k : kinds) {
            if (ImGui::Selectable(kind_name(k), k == s.kind) && k != s.kind) {
                s.kind = k;
                if ((k == Kind::kGapStart || k == Kind::kGapEnd) && s.gap.empty())
                    s.gap = s.name; // (a gap of its own until it's given another end)
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    // The size (one undo step a drag).
    float size[3] = {s.size.x, s.size.y, s.size.z};
    if (ImGui::DragFloat3("Size", size, 5.0f, 10.0f, 50000.0f, "%.0f")) {
        Spec sized = s;
        sized.size = {size[0], size[1], size[2]};
        get_queue([key, sized] { set_spec(key, sized, false); });
    }
    if (ImGui::IsItemActivated())
        get_queue([] { begin_edit(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Width, height, depth");
    static char target[64], script[64], text[64];
    static bool g_target_editing, g_script_editing, g_text_editing;
    switch (s.kind) {
    case Kind::kKill:
        changed |= name_combo("Restart", &s.target, g_restarts);
        changed |= ImGui::Checkbox("Water", &s.water);
        break;
    case Kind::kJumpTo:
        changed |= name_combo("To", &s.target, g_restarts);
        changed |= name_field("Or a node", &s.target, target, sizeof(target), &g_target_editing);
        break;
    case Kind::kScripts:
        changed |= name_combo("Script", &s.script, g_scripts);
        changed |= name_field("Or by name", &s.script, script, sizeof(script), &g_script_editing);
        if (!a->problem && (a->calls.kill || a->calls.teleport || a->calls.end_gap_ || a->calls.start_gap_))
            ImGui::TextDisabled("It calls: %s", kind_name(kind_of_calls(a->calls, true)));
        break;
    case Kind::kGapStart:
    case Kind::kGapEnd: {
        ImGui::TextDisabled("Gap %s (%s)", s.gap.c_str(), s.kind == Kind::kGapStart ? "StartGap here" : "EndGap here");
        if (!s.text.empty()) {
            // (validated by skating - level_editor_playtest.h)
            const bool landed = play_test::gap_landed(clean_text(s.text));
            ImGui::TextColored(landed ? ImVec4(0.25f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.7f, 0.25f, 1.0f), "%s",
                               landed ? "Landed in a test run" : "Not yet landed in a test run");
        }
        changed |= name_field("Name", &s.text, text, sizeof(text), &g_text_editing);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Gap name");
        int score = s.score;
        if (ImGui::InputInt("Score", &score, 50, 500) && score != s.score) {
            s.score = std::max(0, std::min(1000000, score));
            changed = true;
        }
        if (ImGui::TreeNode("Flags")) {
            for (const char* flag : gap_option_labels()) {
                bool on = std::find(s.flags.begin(), s.flags.end(), flag) != s.flags.end();
                if (ImGui::Checkbox(flag, &on)) {
                    if (on)
                        s.flags.emplace_back(flag);
                    else
                        s.flags.erase(std::remove(s.flags.begin(), s.flags.end(), flag), s.flags.end());
                    changed = true;
                }
            }
            ImGui::TreePop();
        }
        if (ImGui::Button("Select Other End"))
            get_queue([key] { select_partner(key); });
        break;
    }
    case Kind::kBoost: {
        int strength = s.strength;
        if (ImGui::SliderInt("Strength", &strength, kBoostMin, kBoostMax, "%d", ImGuiSliderFlags_AlwaysClamp) &&
            strength != s.strength) {
            Spec boosted = s;
            boosted.strength = strength;
            get_queue([key, boosted] { set_spec(key, boosted, false); });
        }
        if (ImGui::IsItemActivated())
            get_queue([] { begin_edit(); });
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Upward speed (600 hop, 900 jump, 2000+ launch)");
        break;
    }
    case Kind::kViewCam: {
        // (map4) the camera played and for how long
        changed |= name_combo("Camera", &s.target, g_cameras);
        changed |= name_field("Or a node", &s.target, target, sizeof(target), &g_target_editing);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Camera node");
        float seconds = s.seconds;
        if (ImGui::SliderFloat("Seconds", &seconds, kCameraMinSeconds, kCameraMaxSeconds, "%.1f",
                               ImGuiSliderFlags_AlwaysClamp) &&
            seconds != s.seconds) {
            Spec timed = s;
            timed.seconds = seconds;
            get_queue([key, timed] { set_spec(key, timed, false); });
        }
        if (ImGui::IsItemActivated())
            get_queue([] { begin_edit(); });
        ImGui::TextDisabled("LevelEditor_PlayCamera camera=%s time=%.1f",
                            s.target.empty() ? "<camera>" : s.target.c_str(), s.seconds);
        break;
    }
    case Kind::kOther:
    case Kind::kBreak:
        break;
    }
    changed |= ImGui::Checkbox("Also on Exit", &s.exit);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Run the script when leaving too");
    if (s.replaces)
        ImGui::TextDisabled("Replaces %s", nodes::caption(s.replaces).c_str());
    if (a->problem)
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "Not active: %s", a->problem);
    else
        ImGui::TextColored(get_colour(kind_colour(s.kind)), "Runs %s on enter%s", get_script_name(s).c_str(),
                           s.exit ? " and exit" : "");

    if (changed)
        get_queue([key, s] { set_spec(key, s, true); });
}

void scenery_section(uint32_t sector)
{
    if (!sector)
        return;
    const Added* a = breakable_of(sector);
    bool on = a != nullptr;
    ImGui::Spacing();
    if (ImGui::Checkbox("Breaks When Hit", &on))
        get_queue([sector, on] { set_breakable(sector, on); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shatter when the skater hits it");
    if (!a)
        return;
    Spec s = a->spec;
    const std::string key = s.name;
    if (sound_combo(&s.sound))
        get_queue([key, s] { set_spec(key, s, true); });
    if (a->broken)
        ImGui::TextDisabled("Broken in play; restored in the editor");
}

} // namespace frontend::level_editor::triggers
