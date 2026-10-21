// NOT ORIGINAL - the level editor's entities: the Inspector's sections (ours). See level_editor_entities.h.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/front/level_editor_entities.h"
#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_nodes.h"
#include "game/obj/object.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "imgui.h"

namespace Front::LevelEditor::Entities {

namespace {

const char* ShortName(const std::string& path)
{
    const char* text = path.c_str();
    const char* base = std::max(std::strrchr(text, '\\'), std::strrchr(text, '/'));
    return base ? base + 1 : text;
}

bool Live(uint32_t id)
{
    Obj::CObject* object = Obj::FindObject(id);
    return object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD);
}

// The rigid body's fields (THUG2's bouncy objects), in the order the levels write them.
const char* const kPhysics[] = {"coeff_restitution",
                                "coeff_friction",
                                "center_of_mass",
                                "skater_collision_impulse_factor",
                                "skater_collision_rotation_factor",
                                "skater_collision_assent",
                                "skater_collision_radius",
                                "SoundType"};

void FieldRequest(std::vector<Ui::Request>* requests, uint32_t node, uint32_t key, Fields::Kind kind,
                  const std::string& text)
{
    Ui::Request r = {Ui::Request::kFieldSet};
    r.node = node;
    r.id = key;
    r.value = static_cast<int>(kind);
    r.text = text;
    requests->push_back(r);
}

void ModelSection(uint32_t id, Script::CStruct* s, std::vector<Ui::Request>* requests)
{
    static const uint32_t kModel = Crc::GenerateCRCFromString("model");
    const char* model = nullptr;
    if (!s || !s->GetString(kModel, &model, 0) || !model || !Live(id))
        return;
    std::string now = model;
    ImGui::Spacing();
    ImGui::SeparatorText("Model");
    const std::vector<std::string>& models = Detail::Models();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##modelswap", ShortName(now))) {
        for (size_t i = 0; i < models.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            bool same = _stricmp(models[i].c_str(), now.c_str()) == 0;
            if (ImGui::Selectable(ShortName(models[i]), same) && !same) {
                std::string chosen = models[i];
                Queue([id, now] { Detail::WatchModel(id, now); });
                FieldRequest(requests, id, kModel, Fields::Kind::String, chosen);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", models[i].c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("A model another object of this level has (loaded). Made again where it stands once the game "
                        "removes the old one (at the latest on leaving the editor); saved as its model= field.");
}

void PhysicsSection(uint32_t name, Script::CStruct* s, std::vector<Ui::Request>* requests)
{
    static const uint32_t kBouncy = Crc::GenerateCRCFromString("bouncy");
    static const uint32_t kContacts = Crc::GenerateCRCFromString("contacts");
    int index = Nodes::Find(name);
    if (!s || index < 0 || !s->ContainsFlag(kBouncy))
        return;
    ImGui::Spacing();
    ImGui::SeparatorText("Physics (bouncy object)");
    std::vector<Fields::Row> rows = Fields::RowsOf(Nodes::All()[index]);
    if (ImGui::BeginTable("##physics", 2, ImGuiTableFlags_SizingStretchProp)) {
        for (const char* field : kPhysics) {
            uint32_t key = Crc::GenerateCRCFromString(field);
            auto row = std::find_if(rows.begin(), rows.end(), [&](const Fields::Row& r) { return r.key == key; });
            if (row == rows.end())
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", field);
            ImGui::TableNextColumn();
            std::string text = Fields::Format(row->value, Fields::NameOf);
            if (!row->editable) {
                ImGui::TextUnformatted(text.c_str());
                continue;
            }
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
            ImGui::PushID(field);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##v", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue) &&
                text != buffer)
                FieldRequest(requests, name, key, row->value.kind, buffer);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Enter sets it (on reload: the level makes the rigid body with it; Ctrl+Z undoes)");
            ImGui::PopID();
        }
        Script::CArray* contacts = nullptr;
        if (s->GetArray(kContacts, &contacts, 0) && contacts) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("contacts");
            ImGui::TableNextColumn();
            ImGui::Text("%u points (the fields table)", static_cast<unsigned>(contacts->m_size));
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("A set of these: Place (N) > Bouncy set.");
}

void MovingSection(uint32_t id)
{
    if (!Live(id) || !Detail::HasPath(id))
        return;
    ImGui::Spacing();
    ImGui::SeparatorText("Moving along its path");
    float set = Detail::SpeedSet(id);
    bool own = set > 0.0f;
    if (ImGui::Checkbox("Path speed", &own))
        Queue([id, own] { Detail::ChangeSpeed(id, own ? Detail::DefaultMph(id) : 0.0f); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off: the speed its scripts give it. On: Obj_SetPathVelocity with this speed whenever it's made "
                          "(a script setting its own speed later wins). Saved (speed record).");
    if (own) {
        ImGui::SameLine();
        float mph = set;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat("mph##speed", &mph, 0.1f, kMinMph, kMaxMph, "%.1f", ImGuiSliderFlags_AlwaysClamp))
            Queue([id, mph] { Detail::ChangeSpeed(id, mph); });
        if (ImGui::IsItemActive())
            Detail::SetUiActive(true);
    }
    bool previewing = Detail::Previewing(id);
    if (ImGui::Button(previewing && Detail::PreviewPlaying() ? "Pause" : "Play"))
        Queue([id] { Detail::PreviewPlay(id); });
    ImGui::SameLine();
    if (ImGui::Button("Step"))
        Queue([id] { Detail::PreviewStep(id); });
    if (previewing) {
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
            Queue([] { StopPreview(); });
        ImGui::TextDisabled("%.0f of %.0f along the path at %.1f mph", Detail::PreviewDistance(), Detail::PreviewLength(),
                            Detail::DefaultMph(id));
    } else {
        ImGui::TextDisabled("Preview: moves it along its path while paused (no script runs; Stop puts it back).");
    }
}

void SoundSection(uint32_t name)
{
    bool sphere = false;
    float radiusNow = Detail::EmitterRadius(name, &sphere);
    EmitterSound e = Detail::EmitterNow(name);
    ImGui::Spacing();
    ImGui::SeparatorText("Sound");
    std::vector<std::pair<uint32_t, std::string>> sounds = Detail::Sounds();
    std::string current = "(none)";
    for (const auto& s : sounds) {
        if (s.first == e.sound)
            current = s.second;
    }
    if (e.sound && current == "(none)")
        current = Nodes::Label(e.sound);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##emittersound", current.c_str())) {
        if (ImGui::Selectable("(none)", e.sound == 0) && e.sound != 0) {
            EmitterSound changed = e;
            changed.sound = 0;
            Queue([changed] { Detail::ChangeEmitter(changed); });
        }
        for (size_t i = 0; i < sounds.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(sounds[i].second.c_str(), sounds[i].first == e.sound) && sounds[i].first != e.sound) {
                EmitterSound changed = e;
                changed.sound = sounds[i].first;
                Queue([changed] { Detail::ChangeEmitter(changed); });
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    bool active = false;
    float volume = e.volume;
    if (ImGui::DragFloat("Volume %##emittervol", &volume, 1.0f, kMinVolume, kMaxVolume, "%.0f",
                         ImGuiSliderFlags_AlwaysClamp)) {
        EmitterSound changed = e;
        changed.volume = volume;
        Queue([changed] { Detail::ChangeEmitter(changed); });
    }
    active = active || ImGui::IsItemActive();
    if (sphere) {
        float radius = e.radius > 0.0f ? e.radius : radiusNow;
        if (ImGui::DragFloat("Radius##emitterradius", &radius, 2.0f, kMinRadius, 20000.0f, "%.0f",
                             ImGuiSliderFlags_AlwaysClamp)) {
            EmitterSound changed = e;
            changed.radius = radius;
            Queue([changed] { Detail::ChangeEmitter(changed); });
        }
        active = active || ImGui::IsItemActive();
    } else {
        ImGui::TextDisabled("Its shape is the level sector of its name (a box): no radius.");
    }
    if (active)
        Detail::SetUiActive(true);
    ImGui::TextDisabled("Played from the emitter's nearest point to the listener with the sound's own range, again "
                        "whenever it ends, while in range - silent while the game is paused. Saved (emitter record).");
}

} // namespace

void InspectorSection(int node, uint32_t object, ImFont* mono, std::vector<Ui::Request>* requests)
{
    (void)mono;
    Detail::SetUiActive(false);
    const std::vector<Nodes::Node>& all = Nodes::All();
    uint32_t name = object;
    int family = -1;
    if (!name && node >= 0 && node < static_cast<int>(all.size()))
        name = all[node].name;
    int index = name ? Nodes::Find(name) : -1;
    if (index >= 0)
        family = static_cast<int>(all[index].family);
    if (!name)
        return;
    Script::CStruct* s = SkateScript::NodeManagerInstance() ? SkateScript::GetNode(name, false) : nullptr;
    if (object) {
        ModelSection(object, s, requests);
        PhysicsSection(object, s, requests);
        MovingSection(object);
        return;
    }
    if (family == static_cast<int>(Nodes::Family::Sound)) {
        SoundSection(name);
    } else if (family == static_cast<int>(Nodes::Family::Particle) ||
               (family == static_cast<int>(Nodes::Family::Object) && !Live(name))) {
        ImGui::Spacing();
        ImGui::SeparatorText(family == static_cast<int>(Nodes::Family::Particle) ? "Particle system" : "Not created yet");
        ImGui::TextWrapped("%s", WhenApplied(name, family));
    }
    if (family == static_cast<int>(Nodes::Family::Object) && Live(name)) {
        ModelSection(name, s, requests);
        PhysicsSection(name, s, requests);
        MovingSection(name);
    }
}

void ViewOptions()
{
    ImGui::Checkbox("Show uncreated objects", &ShowUncreated());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Objects whose nodes the level hasn't made (not CreatedAtStart - a script creates them later) "
                          "drawn as ghosts: a box and the name. Click the diamond to select and move one.");
}

} // namespace Front::LevelEditor::Entities
