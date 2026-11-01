// NOT ORIGINAL - the level editor's entities: the Inspector's sections (ours). See level_editor_entities.h.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_nodes.h"
#include "engine/objects/entity.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "imgui.h"

namespace frontend::level_editor::entities {

namespace {

const char* short_name(const std::string& path)
{
    const char* text = path.c_str();
    const char* base = std::max(std::strrchr(text, '\\'), std::strrchr(text, '/'));
    return base ? base + 1 : text;
}

bool get_live(uint32_t id)
{
    const objects::Entity* object = objects::find_object(id);
    return object && !(object->object_flags_ & objects::kObjectFlagDead);
}

// The rigid body's fields (THUG2's bouncy objects), in the order the levels write them.
constexpr auto kPhysics = std::to_array<const char*>({"coeff_restitution",
                                "coeff_friction",
                                "center_of_mass",
                                "skater_collision_impulse_factor",
                                "skater_collision_rotation_factor",
                                "skater_collision_assent",
                                "skater_collision_radius",
                                "SoundType"});

void field_request(std::vector<ui::Request>* requests, uint32_t node, uint32_t key, fields::Kind kind,
                   const std::string& text)
{
    ui::Request r = {ui::Request::Kind::kFieldSet};
    r.node = node;
    r.id = key;
    r.value = static_cast<int>(kind);
    r.text = text;
    requests->push_back(r);
}

void model_section(uint32_t id, script::Struct* s, std::vector<ui::Request>* requests)
{
    static const uint32_t kModel = checksums::hash_from_text("model");
    const char* model = nullptr;
    if (!s || !s->fetch_text(kModel, &model, 0) || !model || !get_live(id))
        return;
    const std::string now = model;
    ImGui::Spacing();
    ImGui::SeparatorText("Model");
    const std::vector<std::string>& models = detail::get_models();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##modelswap", short_name(now))) {
        for (size_t i = 0; i < models.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool same = _stricmp(models[i].c_str(), now.c_str()) == 0;
            if (ImGui::Selectable(short_name(models[i]), same) && !same) {
                const std::string& chosen = models[i];
                get_queue([id, now] { detail::watch_model(id, now); });
                field_request(requests, id, kModel, fields::Kind::kChars, chosen);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", models[i].c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Models already loaded by this level");
}

void physics_section(uint32_t name, script::Struct* s, std::vector<ui::Request>* requests)
{
    static const uint32_t kBouncy = checksums::hash_from_text("bouncy");
    static const uint32_t kContacts = checksums::hash_from_text("contacts");
    const int index = nodes::get_find(name);
    if (!s || index < 0 || !s->has_bare_flag(kBouncy))
        return;
    ImGui::Spacing();
    ImGui::SeparatorText("Physics");
    std::vector<fields::Row> rows = fields::rows_of(nodes::get_all()[index]);
    if (ImGui::BeginTable("##physics", 2, ImGuiTableFlags_SizingStretchProp)) {
        for (const char* field : kPhysics) {
            uint32_t key = checksums::hash_from_text(field);
            auto row = std::ranges::find_if(rows, [&](const fields::Row& r) { return r.key == key; });
            if (row == rows.end())
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", field);
            ImGui::TableNextColumn();
            const std::string text = fields::get_format(row->value, fields::get_name_of);
            if (!row->editable) {
                ImGui::TextUnformatted(text.c_str());
                continue;
            }
            std::array<char, 128> buffer{};
            std::snprintf(buffer.data(), sizeof(buffer), "%s", text.c_str());
            ImGui::PushID(field);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##v", buffer.data(), sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue) && text != buffer.data())
                field_request(requests, name, key, row->value.kind, buffer.data());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Applies on reload");
            ImGui::PopID();
        }
        script::Array* contacts = nullptr;
        if (s->fetch_list(kContacts, &contacts, 0) && contacts) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("contacts");
            ImGui::TableNextColumn();
            ImGui::Text("%u points", static_cast<unsigned>(contacts->extent_));
        }
        ImGui::EndTable();
    }
}

void moving_section(uint32_t id)
{
    if (!get_live(id) || !detail::has_path(id))
        return;
    ImGui::Spacing();
    ImGui::SeparatorText("Path Speed");
    const float set = detail::speed_set(id);
    bool own = set > 0.0f;
    if (ImGui::Checkbox("Path speed", &own))
        get_queue([id, own] { detail::change_speed(id, own ? detail::default_mph(id) : 0.0f); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Override the scripted path speed");
    if (own) {
        ImGui::SameLine();
        float mph = set;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat("mph##speed", &mph, 0.1f, kMinMph, kMaxMph, "%.1f", ImGuiSliderFlags_AlwaysClamp))
            get_queue([id, mph] { detail::change_speed(id, mph); });
        if (ImGui::IsItemActive())
            detail::set_ui_active(true);
    }
    const bool previewing = detail::get_previewing(id);
    if (ImGui::Button(previewing && detail::preview_playing() ? "Pause" : "Play"))
        get_queue([id] { detail::preview_play(id); });
    ImGui::SameLine();
    if (ImGui::Button("Step"))
        get_queue([id] { detail::preview_step(id); });
    if (previewing) {
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
            get_queue([] { stop_preview(); });
        ImGui::TextDisabled("%.0f of %.0f along the path at %.1f mph", detail::preview_distance(),
                            detail::preview_length(), detail::default_mph(id));
    } else {
        ImGui::TextDisabled("Preview along the path");
    }
}

void sound_section(uint32_t name)
{
    bool sphere = false;
    const float radius_now = detail::emitter_radius(name, &sphere);
    const EmitterSound e = detail::emitter_now(name);
    ImGui::Spacing();
    ImGui::SeparatorText("Sound");
    std::vector<std::pair<uint32_t, std::string>> sounds = detail::get_sounds();
    std::string current = "(none)";
    for (const auto& s : sounds) {
        if (s.first == e.sound)
            current = s.second;
    }
    if (e.sound && current == "(none)")
        current = nodes::caption(e.sound);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##emittersound", current.c_str())) {
        if (ImGui::Selectable("(none)", e.sound == 0) && e.sound != 0) {
            EmitterSound changed = e;
            changed.sound = 0;
            get_queue([changed] { detail::change_emitter(changed); });
        }
        for (size_t i = 0; i < sounds.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(sounds[i].second.c_str(), sounds[i].first == e.sound) && sounds[i].first != e.sound) {
                EmitterSound changed = e;
                changed.sound = sounds[i].first;
                get_queue([changed] { detail::change_emitter(changed); });
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    bool active = false;
    float volume = e.volume;
    if (ImGui::DragFloat("Volume %##emittervol", &volume, 1.0f, kMinVolume, kLoudest, "%.0f",
                         ImGuiSliderFlags_AlwaysClamp)) {
        EmitterSound changed = e;
        changed.volume = volume;
        get_queue([changed] { detail::change_emitter(changed); });
    }
    active = active || ImGui::IsItemActive();
    if (sphere) {
        float radius = e.radius > 0.0f ? e.radius : radius_now;
        if (ImGui::DragFloat("Radius##emitterradius", &radius, 2.0f, kMinRadius, 20000.0f, "%.0f",
                             ImGuiSliderFlags_AlwaysClamp)) {
            EmitterSound changed = e;
            changed.radius = radius;
            get_queue([changed] { detail::change_emitter(changed); });
        }
        active = active || ImGui::IsItemActive();
    } else {
        ImGui::TextDisabled("Area set by its level sector");
    }
    if (active)
        detail::set_ui_active(true);
    ImGui::TextDisabled("Loops while in range");
}

} // namespace

void inspector_section(int node, uint32_t object, ImFont* mono, std::vector<ui::Request>* requests)
{
    (void)mono;
    detail::set_ui_active(false);
    const std::vector<nodes::Elem>& all = nodes::get_all();
    uint32_t name = object;
    int family = -1;
    if (!name && node >= 0 && std::cmp_less(node, all.size()))
        name = all[node].name;
    const int index = name ? nodes::get_find(name) : -1;
    if (index >= 0)
        family = static_cast<int>(all[index].family);
    if (!name)
        return;
    script::Struct* s = level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(name, false) : nullptr;
    if (object) {
        model_section(object, s, requests);
        physics_section(object, s, requests);
        moving_section(object);
        return;
    }
    if (family == static_cast<int>(nodes::Family::kSound)) {
        sound_section(name);
    } else if (family == static_cast<int>(nodes::Family::kParticle) ||
               (family == static_cast<int>(nodes::Family::kObject) && !get_live(name))) {
        ImGui::Spacing();
        ImGui::SeparatorText(family == static_cast<int>(nodes::Family::kParticle) ? "Particle System" : "Not Spawned");
        ImGui::TextWrapped("%s", when_applied(name, family));
    }
    if (family == static_cast<int>(nodes::Family::kObject) && get_live(name)) {
        model_section(name, s, requests);
        physics_section(name, s, requests);
        moving_section(name);
    }
}

void view_options()
{
    ImGui::Checkbox("Show Unspawned Objects", &show_uncreated());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show objects created later by scripts");
}

} // namespace frontend::level_editor::entities
