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
    changed |= NameField("Or a sound by name", sound, typed, sizeof(typed), &editing);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("PlaySound <name>: a sound the level loads (its sfx script's LoadSound) - one it doesn't load "
                          "plays nothing");
    return changed;
}

// A proximity node's scripts and shape (level_editor_triggers.h: ProximEdit).
void ProximSection(uint32_t node)
{
    UpdateLists();
    ImGui::Spacing();
    ImGui::SeparatorText("Proximity node");
    const ProximEdit* now = ProximOf(node);
    ProximEdit e = now ? *now : ProximEdit{};
    e.node = node;
    ImGui::TextWrapped("Runs a script when the skater comes in and when it goes out (its own script tells them apart "
                       "with \"if Inside\"). Choose either here:");
    static char enterBuffer[64], exitBuffer[64];
    static bool enterEditing, exitEditing;
    bool changed = false;
    changed |= NameCombo("On entering", &e.enter, g_scripts);
    changed |= NameField("Or by name##enter", &e.enter, enterBuffer, sizeof(enterBuffer), &enterEditing);
    changed |= NameCombo("On leaving", &e.exit, g_scripts);
    changed |= NameField("Or by name##exit", &e.exit, exitBuffer, sizeof(exitBuffer), &exitEditing);
    ImGui::TextDisabled("(none: the node's own script runs, if it has one)");
    int shape = e.shape ? (e.box ? 2 : 1) : 0;
    ImGui::TextUnformatted("Shape");
    ImGui::SameLine();
    bool shapeChanged = ImGui::RadioButton("as the level has it", &shape, 0);
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
            ImGui::SetTooltip("A box along the world's axes (the game's BoundingBox shape) about the node's place");
    }
    if (now && ImGui::Button("Put back as the level has it"))
        Queue([node] { SetProxim(ProximEdit{node}, true); });
    ImGui::TextWrapped("Drag the dots on its shape to resize it; it moves with its node. Shows at once (offline).");
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
                        l.hasNode ? "" : " (no node of its name)");
    ImGui::TextDisabled("TriggerScript: %s", l.script ? Nodes::Label(l.script).c_str() : "(none)");
    ImGui::TextDisabled("Box (%.0f, %.0f, %.0f) .. (%.0f, %.0f, %.0f)", l.min.x, l.min.y, l.min.z, l.max.x, l.max.y,
                        l.max.z);
    uint32_t sector = l.sector;
    bool switchable = l.wall || l.allTrigger;
    if (l.off)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.4f, 1.0f), "Off (skating through it does nothing)");
    ImGui::BeginDisabled(!switchable);
    if (ImGui::Button(l.off ? "Switch on" : "Switch off"))
        Queue([sector, off = !l.off] { TurnOff(sector, off); });
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(switchable ? "Its collision switched off / on as an invisible wall's (level_editor_walls: the game's "
                                       "own switch, saved as its `wall` record)"
                                     : "Its collision has faces that aren't a trigger's (they'd go too)");
    if (l.wall) {
        ImGui::SameLine();
        if (ImGui::Button("Move it"))
            Queue([sector] { SelectWall(sector); });
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Selects it as an invisible wall: drag it, the arrows, W A S D / Q E, Z / X (quarter turns), "
                              "Home - saved as its `wall` record. Its size stays; for that, make an editable copy.");
    }
    ImGui::BeginDisabled(!switchable || !l.script);
    if (ImGui::Button("Make an editable copy"))
        Queue([sector] { MakeEditable(sector); });
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("A trigger volume of the editor's over its faces' box, running the same TriggerScript - it moves, "
                          "turns, resizes and changes kind; the original is switched off while the copy is there.");
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
    ImGui::TextDisabled("Add where the view looks:");
    if (ImGui::Button("Kill zone"))
        Queue([] { AddKill(false); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("SK3_KillSkater: the skater bails and goes back to a restart (the nearest - choose another in "
                          "the Inspector)");
    ImGui::SameLine();
    if (ImGui::Button("Water"))
        Queue([] { AddKill(true); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("SK3_KillSkater_Water: a splash, then back to a restart");
    ImGui::SameLine();
    if (ImGui::Button("Teleport"))
        Queue([] { AddTeleport(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("TeleportSkaterToNode: the skater is moved to a node (the nearest restart - choose another)");
    ImGui::SameLine();
    if (ImGui::Button("Script"))
        Queue([] { AddScript(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Runs one of the level's scripts when skated through");
    ImGui::SameLine();
    if (ImGui::Button("Gap"))
        Queue([] { AddGap(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A gap: its start here, its end further on (StartGap / EndGap) - in the pause menu's gap list");
    ImGui::SameLine();
    if (ImGui::Button("Jump boost"))
        Queue([] { AddBoost(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A launch pad: trigger_jump_boost speed=<strength> (THUG2's own - Berlin's fans, THAW's Ruins) "
                          "throws the skater up when skated through");
    ImGui::SameLine();
    if (ImGui::Button("Camera"))
        Queue([] { AddCamera(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Plays one of the editor's cameras (V makes one) for a few seconds when skated through - the "
                          "game's own virtual_cam (PlaySkaterCamAnim, as the goals' cameras) - then the skater's camera");
    ImGui::TextDisabled("%s", Describe().c_str());
    ImGui::TextDisabled("Breakables: select a scenery piece - the Inspector's \"Breaks when hit\"");
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
        ImGui::SeparatorText("Trigger volume");
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
    ImGui::SeparatorText("Trigger volume");
    Spec s = a->spec;
    std::string key = s.name;
    bool changed = false;
    if (s.kind == Kind::Break) {
        // (a breakable: its piece and sound - its box is the piece's)
        ImGui::TextColored(Colour(KindColour(s.kind)), "Breakable: %s%s", s.target.c_str(),
                           a->broken ? " (broken - back when the editor opens)" : "");
        if (SoundCombo(&s.sound))
            Queue([key, s] { SetSpec(key, s, true); });
        ImGui::TextWrapped("Hitting the piece shatters it (Shatter, then Kill - as Boston's glass); its collision is off "
                           "while it's breakable. Delete (or the piece's \"Breaks when hit\") puts it back.");
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
        ImGui::SetTooltip("Width (x), height (y), depth (z) about its centre, turned with its heading (Z / X)");
    static char target[64], script[64], text[64];
    static bool targetEditing, scriptEditing, textEditing;
    switch (s.kind) {
    case Kind::Kill:
        changed |= NameCombo("Restart", &s.target, g_restarts);
        changed |= ImGui::Checkbox("Water (a splash)", &s.water);
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
        changed |= NameField("Name", &s.text, text, sizeof(text), &textEditing);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The gap's name in the gap list and on screen (both ends)");
        int score = s.score;
        if (ImGui::InputInt("Score", &score, 50, 500) && score != s.score) {
            s.score = std::max(0, std::min(1000000, score));
            changed = true;
        }
        if (ImGui::TreeNode("Flags (StartGap's)")) {
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
        if (ImGui::Button("Select the other end"))
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
            ImGui::SetTooltip("The skater's upward speed as it goes through (trigger_jump_boost speed=): 600 a hop, 900 a "
                              "jump, 2000+ a launch");
        break;
    }
    case Kind::Camera: {
        // (map4) the camera played and for how long
        changed |= NameCombo("Camera", &s.target, g_cameras);
        changed |= NameField("Or a node", &s.target, target, sizeof(target), &targetEditing);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Any node with targetOffset / positionOffset (an editor camera), or else its Pos / Angles");
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
        ImGui::TextWrapped("LevelEditor_PlayCamera camera=%s time=%.1f - a level script may call it too.",
                           s.target.empty() ? "<camera>" : s.target.c_str(), s.seconds);
        break;
    }
    case Kind::Other:
    case Kind::Break:
        break;
    }
    changed |= ImGui::Checkbox("Also on leaving it", &s.exit);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Runs its script when the skater leaves the box too (the game fires a volume only on the way\n"
                          "in; leaving is seen by the editor each frame - the local skater only, offline)");
    if (s.replaces)
        ImGui::TextDisabled("Stands in for the level's %s (off while this is there)", Nodes::Label(s.replaces).c_str());
    if (a->problem)
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "Not active: %s", a->problem);
    else
        ImGui::TextColored(Colour(KindColour(s.kind)), "Active: skating into it%s runs %s",
                           s.exit ? " or out of it" : "", ScriptName(s).c_str());
    ImGui::TextWrapped("Move, turn and drop it as any node (drag, the arrows, W A S D / Q E, Z / X, G); drag the dots on "
                       "its faces to resize it; Delete removes it; Ctrl+Z undoes.");
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
    if (ImGui::Checkbox("Breaks when hit", &on))
        Queue([sector, on] { SetBreakable(sector, on); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The piece shatters when the skater hits it (a trigger over its box runs Shatter, as Boston's "
                          "glass): its collision is off while it's breakable. Off: as the level has it.");
    if (!a)
        return;
    Spec s = a->spec;
    std::string key = s.name;
    if (SoundCombo(&s.sound))
        Queue([key, s] { SetSpec(key, s, true); });
    if (a->broken)
        ImGui::TextDisabled("Broken while playing - back each time the editor opens");
}

} // namespace Front::LevelEditor::Triggers
