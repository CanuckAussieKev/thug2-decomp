// NOT ORIGINAL - the level editor's trigger volumes in the UI (ours; see level_editor_triggers.h): the Triggers window (T)
// and the Inspector's section for a trigger. ImGui only; every change is queued for the editor's next update
// (Triggers::Queue).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/front/level_editor_cameras.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_triggers.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/front/level_editor_playtest.h"
#include "imgui.h"

namespace Front::LevelEditor::Triggers {

namespace {

ImVec4 Colour(uint32_t argb)
{
    return ImVec4(static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(argb & 0xFF) / 255.0f, static_cast<float>(argb >> 24) / 255.0f);
}

std::string NameOf(uint32_t checksum) { return NameFor(checksum, Script::KeptQbName(checksum)); }

// The level's restarts (names), and the scripts its nodes' TriggerScripts name - made again when the nodes change.
std::vector<std::string> g_restarts, g_scripts, g_cameras; // (map4: the editor's camera nodes)
uint32_t g_listsGeneration = 0xFFFFFFFF;
void UpdateLists()
{
    if (g_listsGeneration == Nodes::Generation())
        return;
    g_listsGeneration = Nodes::Generation();
    g_restarts.clear();
    g_scripts.clear();
    g_cameras.clear();
    for (const Cameras::Camera& camera : Cameras::All())
        g_cameras.push_back(camera.name);
    static const uint32_t kTriggerScript = 0x2CA8A299;
    for (const Nodes::Node& node : Nodes::All()) {
        if (node.family == Nodes::Family::Spawn && !node.hidden && node.name)
            g_restarts.push_back(NameOf(node.name));
        if (!node.triggerScript || node.added)
            continue;
        Script::CStruct* s = Nodes::StructOf(node);
        uint32_t script = 0;
        if (s && s->GetChecksum(kTriggerScript, &script, 0) && script) {
            std::string name = NameOf(script);
            if (std::find(g_scripts.begin(), g_scripts.end(), name) == g_scripts.end())
                g_scripts.push_back(name);
        }
    }
    std::sort(g_scripts.begin(), g_scripts.end());
}

// A name field: typed, applied when let go (Enter or leaving it). Its buffer follows `value` while it isn't edited.
bool NameField(const char* label, std::string* value, char* buffer, size_t size, bool* editing)
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
bool NameCombo(const char* label, std::string* value, const std::vector<std::string>& names)
{
    bool changed = false;
    if (ImGui::BeginCombo(label, value->empty() ? "(none)" : value->c_str(), ImGuiComboFlags_HeightLarge)) {
        for (const std::string& name : names) {
            bool chosen = name == *value;
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
bool SoundCombo(std::string* sound)
{
    bool changed = false;
    if (ImGui::BeginCombo("Sound", sound->empty() ? "(none)" : sound->c_str())) {
        if (ImGui::Selectable("(none)", sound->empty()) && !sound->empty()) {
            sound->clear();
            changed = true;
        }
        for (const char* name : BreakSounds()) {
            if (ImGui::Selectable(name, *sound == name) && *sound != name) {
                *sound = name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    static char typed[64];
    static bool editing;
    changed |= NameField("Sound Name", sound, typed, sizeof(typed), &editing);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Must be a sound the level loads");
    return changed;
}

// A proximity node's scripts and shape (level_editor_triggers.h: ProximEdit).
void ProximSection(uint32_t node)
{
    UpdateLists();
    ImGui::Spacing();
    ImGui::SeparatorText("Proximity Node");
    const ProximEdit* now = ProximOf(node);
    ProximEdit e = now ? *now : ProximEdit{};
    e.node = node;
    ImGui::TextDisabled("Scripts run on enter and exit");
    static char enterBuffer[64], exitBuffer[64];
    static bool enterEditing, exitEditing;
    bool changed = false;
    changed |= NameCombo("On entering", &e.enter, g_scripts);
    changed |= NameField("Or by name##enter", &e.enter, enterBuffer, sizeof(enterBuffer), &enterEditing);
    changed |= NameCombo("On leaving", &e.exit, g_scripts);
    changed |= NameField("Or by name##exit", &e.exit, exitBuffer, sizeof(exitBuffer), &exitEditing);
    ImGui::TextDisabled("(none: node's own script)");
    int shape = e.shape ? (e.box ? 2 : 1) : 0;
    ImGui::TextUnformatted("Shape");
    ImGui::SameLine();
    bool shapeChanged = ImGui::RadioButton("Original", &shape, 0);
    ImGui::SameLine();
    shapeChanged |= ImGui::RadioButton("sphere", &shape, 1);
    ImGui::SameLine();
    shapeChanged |= ImGui::RadioButton("box", &shape, 2);
    if (shapeChanged) {
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
            Queue([sized] { SetProxim(sized, false); });
        }
        if (ImGui::IsItemActivated())
            Queue([] { BeginEdit(); });
    } else if (e.shape) {
        float size[3] = {e.size.x, e.size.y, e.size.z};
        if (ImGui::DragFloat3("Size##proxim", size, 5.0f, kProximMin, kProximMax, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
            ProximEdit sized = e;
            sized.size = {size[0], size[1], size[2]};
            Queue([sized] { SetProxim(sized, false); });
        }
        if (ImGui::IsItemActivated())
            Queue([] { BeginEdit(); });
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("World-aligned box");
    }
    if (now && ImGui::Button("Revert"))
        Queue([node] { SetProxim(ProximEdit{node}, true); });
    ImGui::TextDisabled("Drag the handles to resize");
    if (changed)
        Queue([e] { SetProxim(e, true); });
}

const Added* AddedNamed(uint32_t name)
{
    for (const Added& a : AddedTriggers()) {
        if (NodeCreate::Checksum(a.spec.name) == name)
            return &a;
    }
    return nullptr;
}

const LevelTrigger* LevelNamed(uint32_t sector)
{
    for (const LevelTrigger& l : LevelTriggers()) {
        if (l.sector == sector)
            return &l;
    }
    return nullptr;
}

void LevelTriggerDetails(const LevelTrigger& l)
{
    ImGui::TextColored(Colour(l.off ? 0xFFA0A0A0u : KindColour(l.kind)), "%s", DescribeLevelTrigger(l).c_str());
    ImGui::TextDisabled("Level trigger %s: %d trigger faces%s", Nodes::Label(l.sector).c_str(), l.faceCount,
                        l.hasNode ? "" : " (no node)");
    ImGui::TextDisabled("TriggerScript: %s", l.script ? Nodes::Label(l.script).c_str() : "(none)");
    ImGui::TextDisabled("Box (%.0f, %.0f, %.0f) .. (%.0f, %.0f, %.0f)", l.min.x, l.min.y, l.min.z, l.max.x, l.max.y,
                        l.max.z);
    uint32_t sector = l.sector;
    bool switchable = l.wall || l.allTrigger;
    if (l.off)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.4f, 1.0f), "Disabled");
    ImGui::BeginDisabled(!switchable);
    if (ImGui::Button(l.off ? "Switch on" : "Switch off"))
        Queue([sector, off = !l.off] { TurnOff(sector, off); });
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(switchable ? "Enable or disable this trigger" : "Has non-trigger faces; can't be disabled");
    if (l.wall) {
        ImGui::SameLine();
        if (ImGui::Button("Move it"))
            Queue([sector] { SelectWall(sector); });
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Move as an invisible wall (size is fixed)");
    }
    ImGui::BeginDisabled(!switchable || !l.script);
    if (ImGui::Button("Make Editable Copy"))
        Queue([sector] { MakeEditable(sector); });
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Replace with an editable trigger volume; the original is disabled");
}

} // namespace

void Window(float viewX, float viewY, float viewWidth, float viewHeight, float scale, int selectedNode)
{
    (void)viewHeight;
    (void)selectedNode;
    if (!Shown())
        return;
    UpdateLists();
    ImGui::SetNextWindowPos(ImVec2(viewX + viewWidth - 8.0f * scale, viewY + 40.0f * scale), ImGuiCond_FirstUseEver,
                            ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(380.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Triggers (T)###triggers", &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open)
            Queue([] { SetShown(false); });
        return;
    }
    ImGui::TextDisabled("Add at view center:");
    if (ImGui::Button("Kill zone"))
        Queue([] { AddKill(false); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bail and respawn at a restart");
    ImGui::SameLine();
    if (ImGui::Button("Water"))
        Queue([] { AddKill(true); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Splash and respawn at a restart");
    ImGui::SameLine();
    if (ImGui::Button("Teleport"))
        Queue([] { AddTeleport(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Teleport the skater to a node");
    ImGui::SameLine();
    if (ImGui::Button("Script"))
        Queue([] { AddScript(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Run a level script");
    ImGui::SameLine();
    if (ImGui::Button("Gap"))
        Queue([] { AddGap(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Gap start and end");
    ImGui::SameLine();
    if (ImGui::Button("Jump boost"))
        Queue([] { AddBoost(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Launch the skater upward");
    ImGui::SameLine();
    if (ImGui::Button("Camera"))
        Queue([] { AddCamera(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Play a camera shot");
    ImGui::TextDisabled("%s", Describe().c_str());
    ImGui::TextDisabled("Breakables: select scenery, then Breaks When Hit");
    // The legend.
    for (int k = 0; k < kKinds; ++k) {
        if (k % 5)
            ImGui::SameLine();
        ImGui::TextColored(Colour(KindColour(static_cast<Kind>(k))), "%s", KindName(static_cast<Kind>(k)));
    }
    ImGui::Separator();
    // The nearest (the editor's, then the level's), a click selects / inspects.
    if (ImGui::BeginChild("##triggerlist", ImVec2(0.0f, 180.0f * scale), ImGuiChildFlags_Borders)) {
        for (const Added& a : AddedTriggers()) {
            if (!a.alive)
                continue;
            std::string text = a.spec.name + "  " + KindName(a.spec.kind);
            if ((a.spec.kind == Kind::GapStart || a.spec.kind == Kind::GapEnd) && !a.spec.text.empty())
                text += std::string("  - ") + PlayTest::GapNote(CleanText(a.spec.text)); // (landed in a test run?)
            ImGui::PushStyleColor(ImGuiCol_Text, Colour(a.problem ? 0xFFA0A0A0u : KindColour(a.spec.kind)));
            uint32_t name = NodeCreate::Checksum(a.spec.name);
            if (ImGui::Selectable((text + "##a" + a.spec.name).c_str(), false))
                Queue([name] { SelectNodeNamed(name); });
            ImGui::PopStyleColor();
        }
        int shown = 0;
        for (const LevelTrigger& l : LevelTriggers()) {
            if (++shown > 400)
                break;
            std::string text = Nodes::Label(l.sector) + "  " + DescribeLevelTrigger(l) + (l.off ? "  (off)" : "");
            ImGui::PushStyleColor(ImGuiCol_Text, Colour(l.off ? 0xFFA0A0A0u : KindColour(l.kind)));
            uint32_t sector = l.sector;
            bool hasNode = l.hasNode;
            if (ImGui::Selectable((text + "##l" + std::to_string(l.sector)).c_str(), l.sector == InspectedSector()))
                Queue([sector, hasNode] {
                    Inspect(sector);
                    if (hasNode)
                        SelectNodeNamed(sector);
                });
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    if (const LevelTrigger* l = LevelNamed(InspectedSector())) {
        ImGui::Separator();
        LevelTriggerDetails(*l);
    }
    ImGui::End();
    if (!open)
        Queue([] { SetShown(false); });
}

void InspectorSection(int index)
{
    const std::vector<Nodes::Node>& all = Nodes::All();
    if (index < 0 || index >= static_cast<int>(all.size()))
        return;
    uint32_t name = all[index].name;
    if (const LevelTrigger* l = LevelNamed(name)) {
        ImGui::Spacing();
        ImGui::SeparatorText("Trigger Volume");
        LevelTriggerDetails(*l);
        return;
    }
    const Added* a = AddedNamed(name);
    if (!a) {
        if (IsProxim(name))
            ProximSection(name);
        return;
    }
    UpdateLists();
    ImGui::Spacing();
    ImGui::SeparatorText("Trigger Volume");
    Spec s = a->spec;
    std::string key = s.name;
    bool changed = false;
    if (s.kind == Kind::Break) {
        // (a breakable: its piece and sound - its box is the piece's)
        ImGui::TextColored(Colour(KindColour(s.kind)), "Breakable: %s%s", s.target.c_str(),
                           a->broken ? " (broken)" : "");
        if (SoundCombo(&s.sound))
            Queue([key, s] { SetSpec(key, s, true); });
        ImGui::TextDisabled("Shatters when hit");
        return;
    }
    // The kind.
    const Kind kinds[] = {Kind::Kill,   Kind::Teleport, Kind::Script, Kind::GapStart,
                          Kind::GapEnd, Kind::Boost,    Kind::Camera};
    if (ImGui::BeginCombo("Kind", KindName(s.kind))) {
        for (Kind k : kinds) {
            if (ImGui::Selectable(KindName(k), k == s.kind) && k != s.kind) {
                s.kind = k;
                if ((k == Kind::GapStart || k == Kind::GapEnd) && s.gap.empty())
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
        Queue([key, sized] { SetSpec(key, sized, false); });
    }
    if (ImGui::IsItemActivated())
        Queue([] { BeginEdit(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Width, height, depth");
    static char target[64], script[64], text[64];
    static bool targetEditing, scriptEditing, textEditing;
    switch (s.kind) {
    case Kind::Kill:
        changed |= NameCombo("Restart", &s.target, g_restarts);
        changed |= ImGui::Checkbox("Water", &s.water);
        break;
    case Kind::Teleport:
        changed |= NameCombo("To", &s.target, g_restarts);
        changed |= NameField("Or a node", &s.target, target, sizeof(target), &targetEditing);
        break;
    case Kind::Script:
        changed |= NameCombo("Script", &s.script, g_scripts);
        changed |= NameField("Or by name", &s.script, script, sizeof(script), &scriptEditing);
        if (!a->problem && (a->calls.kill || a->calls.teleport || a->calls.endGap || a->calls.startGap))
            ImGui::TextDisabled("It calls: %s", KindName(KindOfCalls(a->calls, true)));
        break;
    case Kind::GapStart:
    case Kind::GapEnd: {
        ImGui::TextDisabled("Gap %s (%s)", s.gap.c_str(), s.kind == Kind::GapStart ? "StartGap here" : "EndGap here");
        if (!s.text.empty()) {
            // (validated by skating - level_editor_playtest.h)
            bool landed = PlayTest::GapLanded(CleanText(s.text));
            ImGui::TextColored(landed ? ImVec4(0.25f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.7f, 0.25f, 1.0f), "%s",
                               landed ? "Landed in a test run" : "Not yet landed in a test run");
        }
        changed |= NameField("Name", &s.text, text, sizeof(text), &textEditing);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Gap name");
        int score = s.score;
        if (ImGui::InputInt("Score", &score, 50, 500) && score != s.score) {
            s.score = std::max(0, std::min(1000000, score));
            changed = true;
        }
        if (ImGui::TreeNode("Flags")) {
            for (const char* flag : GapFlags()) {
                bool on = std::find(s.flags.begin(), s.flags.end(), flag) != s.flags.end();
                if (ImGui::Checkbox(flag, &on)) {
                    if (on)
                        s.flags.push_back(flag);
                    else
                        s.flags.erase(std::remove(s.flags.begin(), s.flags.end(), flag), s.flags.end());
                    changed = true;
                }
            }
            ImGui::TreePop();
        }
        if (ImGui::Button("Select Other End"))
            Queue([key] { SelectPartner(key); });
        break;
    }
    case Kind::Boost: {
        int strength = s.strength;
        if (ImGui::SliderInt("Strength", &strength, kBoostMin, kBoostMax, "%d", ImGuiSliderFlags_AlwaysClamp) &&
            strength != s.strength) {
            Spec boosted = s;
            boosted.strength = strength;
            Queue([key, boosted] { SetSpec(key, boosted, false); });
        }
        if (ImGui::IsItemActivated())
            Queue([] { BeginEdit(); });
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Upward speed (600 hop, 900 jump, 2000+ launch)");
        break;
    }
    case Kind::Camera: {
        // (map4) the camera played and for how long
        changed |= NameCombo("Camera", &s.target, g_cameras);
        changed |= NameField("Or a node", &s.target, target, sizeof(target), &targetEditing);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Camera node");
        float seconds = s.seconds;
        if (ImGui::SliderFloat("Seconds", &seconds, kCameraMinSeconds, kCameraMaxSeconds, "%.1f",
                               ImGuiSliderFlags_AlwaysClamp) &&
            seconds != s.seconds) {
            Spec timed = s;
            timed.seconds = seconds;
            Queue([key, timed] { SetSpec(key, timed, false); });
        }
        if (ImGui::IsItemActivated())
            Queue([] { BeginEdit(); });
        ImGui::TextDisabled("LevelEditor_PlayCamera camera=%s time=%.1f",
                           s.target.empty() ? "<camera>" : s.target.c_str(), s.seconds);
        break;
    }
    case Kind::Other:
    case Kind::Break:
        break;
    }
    changed |= ImGui::Checkbox("Also on Exit", &s.exit);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Run the script when leaving too");
    if (s.replaces)
        ImGui::TextDisabled("Replaces %s", Nodes::Label(s.replaces).c_str());
    if (a->problem)
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "Not active: %s", a->problem);
    else
        ImGui::TextColored(Colour(KindColour(s.kind)), "Runs %s on enter%s",
                           ScriptName(s).c_str(), s.exit ? " and exit" : "");
    
    if (changed)
        Queue([key, s] { SetSpec(key, s, true); });
}

void ScenerySection(uint32_t sector)
{
    if (!sector)
        return;
    const Added* a = BreakableOf(sector);
    bool on = a != nullptr;
    ImGui::Spacing();
    if (ImGui::Checkbox("Breaks When Hit", &on))
        Queue([sector, on] { SetBreakable(sector, on); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shatter when the skater hits it");
    if (!a)
        return;
    Spec s = a->spec;
    std::string key = s.name;
    if (SoundCombo(&s.sound))
        Queue([key, s] { SetSpec(key, s, true); });
    if (a->broken)
        ImGui::TextDisabled("Broken in play; restored in the editor");
}

} // namespace Front::LevelEditor::Triggers
