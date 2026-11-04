// NOT ORIGINAL - the level editor's entities: the Inspector's sections (ours). See level_editor_entities.h.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor_ui.h"
#include "engine/core/checksums.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_nodes.h"
#include "engine/objects/entity.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"

namespace frontend::level_editor::entities {

namespace {

constexpr std::size_t kLabelBufferSize = 128; // the text buffer of a label field
constexpr float kSpeedFieldWidth = 120.0F;    // width of the speed field
constexpr float kMphDragStep = 0.1F;          // the speed field's drag step
constexpr float kRadiusDragStep = 2.0F;       // the emitter radius field's drag step
constexpr float kMaxRadius = 20000.0F;        // the emitter radius field's upper limit

const char* short_name(const std::string& path)
{
    const char* text = path.c_str();
    const char* base = std::max(std::strrchr(text, '\\'), std::strrchr(text, '/'));
    return base ? std::next(base) : text;
}

bool get_live(uint32_t id)
{
    const objects::Entity* object = objects::find_object(id);
    return (object != nullptr) && ((object->object_flags() & objects::kObjectFlagDead) == 0U);
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

// A node and one of its fields' key.
struct NodeField {
    uint32_t node{};
    uint32_t key{};
};

void field_request(std::vector<ui::Request>* requests, NodeField node_field, fields::Kind kind,
                   const std::string& text)
{
    const auto node = node_field.node;
    const auto key = node_field.key;
    ui::Request r = {.kind = ui::Request::Kind::kFieldSet};
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
    if (!s || !s->fetch_text(kModel, &model, 0) || !model || !get_live(id)) {
        return;
    }
    const std::string now = model;
    ImGui::Spacing();
    ImGui::SeparatorText("Model");
    const std::vector<std::string>& models = detail::get_models();
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::BeginCombo("##modelswap", short_name(now))) {
        for (size_t i = 0; i < models.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool same = _stricmp(core::at(models, i).c_str(), now.c_str()) == 0;
            if (ImGui::Selectable(short_name(core::at(models, i)), same) && !same) {
                const std::string& chosen = core::at(models, i);
                get_queue([id, now] { detail::watch_model(id, now); });
                field_request(requests, {.node = id, .key = kModel}, fields::Kind::kChars, chosen);
            }
            if (ImGui::IsItemHovered()) {
                gui::tooltip("%s", core::at(models, i).c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    gui::text_disabled("Models already loaded by this level");
}

void physics_section(uint32_t name, script::Struct* s, std::vector<ui::Request>* requests)
{
    static const uint32_t kBouncy = checksums::hash_from_text("bouncy");
    static const uint32_t kContacts = checksums::hash_from_text("contacts");
    const int index = nodes::get_find(name);
    if (!s || index < 0 || !s->has_bare_flag(kBouncy)) {
        return;
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Physics");
    std::vector<fields::Row> rows = fields::rows_of(core::at(nodes::get_all(), index));
    if (ImGui::BeginTable("##physics", 2, ImGuiTableFlags_SizingStretchProp)) {
        for (const char* field : kPhysics) {
            uint32_t key = checksums::hash_from_text(field);
            auto row = std::ranges::find_if(rows, [&](const fields::Row& r) { return r.key == key; });
            if (row == rows.end()) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            gui::text_disabled("%s", field);
            ImGui::TableNextColumn();
            const std::string text = fields::get_format(row->value, fields::get_name_of);
            if (!row->editable) {
                ImGui::TextUnformatted(text.c_str());
                continue;
            }
            std::array<char, kLabelBufferSize> buffer{};
            text::format_to(buffer, "%s", text.c_str());
            ImGui::PushID(field);
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::InputText("##v", buffer.data(), sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue) &&
                text != buffer.data()) {
                field_request(requests, {.node = name, .key = key}, row->value.kind, buffer.data());
            }
            if (ImGui::IsItemHovered()) {
                gui::tooltip("Applies on reload");
            }
            ImGui::PopID();
        }
        script::Array* contacts = nullptr;
        if (s->fetch_list(kContacts, &contacts, 0) && contacts) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            gui::text_disabled("contacts");
            ImGui::TableNextColumn();
            gui::text("%u points", static_cast<unsigned>(contacts->extent()));
        }
        ImGui::EndTable();
    }
}

void moving_section(uint32_t id)
{
    if (!get_live(id) || !detail::has_path(id)) {
        return;
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Path Speed");
    const float set = detail::speed_set(id);
    bool own = set > 0.0F;
    if (ImGui::Checkbox("Path speed", &own)) {
        get_queue([id, own] { detail::change_speed(id, own ? detail::default_mph(id) : 0.0F); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Override the scripted path speed");
    }
    if (own) {
        ImGui::SameLine();
        float mph = set;
        ImGui::SetNextItemWidth(kSpeedFieldWidth);
        if (ImGui::DragFloat("mph##speed", &mph, kMphDragStep, kMinMph, kMaxMph, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp)) {
            get_queue([id, mph] { detail::change_speed(id, mph); });
        }
        if (ImGui::IsItemActive()) {
            detail::set_ui_active(true);
        }
    }
    const bool previewing = detail::get_previewing(id);
    if (ImGui::Button(previewing && detail::preview_playing() ? "Pause" : "Play")) {
        get_queue([id] { detail::preview_play(id); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Step")) {
        get_queue([id] { detail::preview_step(id); });
    }
    if (previewing) {
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            get_queue([] { stop_preview(); });
        }
        gui::text_disabled("%.0f of %.0f along the path at %.1f mph", detail::preview_distance(),
                            detail::preview_length(), detail::default_mph(id));
    } else {
        gui::text_disabled("Preview along the path");
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
        if (s.first == e.sound) {
            current = s.second;
        }
    }
    if ((e.sound != 0U) && current == "(none)") {
        current = nodes::caption(e.sound);
    }
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::BeginCombo("##emittersound", current.c_str())) {
        if (ImGui::Selectable("(none)", e.sound == 0) && e.sound != 0) {
            EmitterSound changed = e;
            changed.sound = 0;
            get_queue([changed] { detail::change_emitter(changed); });
        }
        for (size_t i = 0; i < sounds.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const auto& [sound, sound_name] = core::at(sounds, i);
            if (ImGui::Selectable(sound_name.c_str(), sound == e.sound) && sound != e.sound) {
                EmitterSound changed = e;
                changed.sound = sound;
                get_queue([changed] { detail::change_emitter(changed); });
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    bool active = false;
    float volume = e.volume;
    if (ImGui::DragFloat("Volume %##emittervol", &volume, 1.0F, kMinVolume, kLoudest, "%.0f",
                         ImGuiSliderFlags_AlwaysClamp)) {
        EmitterSound changed = e;
        changed.volume = volume;
        get_queue([changed] { detail::change_emitter(changed); });
    }
    active = active || ImGui::IsItemActive();
    if (sphere) {
        float radius = e.radius > 0.0F ? e.radius : radius_now;
        if (ImGui::DragFloat("Radius##emitterradius", &radius, kRadiusDragStep, kMinRadius, kMaxRadius, "%.0f",
                             ImGuiSliderFlags_AlwaysClamp)) {
            EmitterSound changed = e;
            changed.radius = radius;
            get_queue([changed] { detail::change_emitter(changed); });
        }
        active = active || ImGui::IsItemActive();
    } else {
        gui::text_disabled("Area set by its level sector");
    }
    if (active) {
        detail::set_ui_active(true);
    }
    gui::text_disabled("Loops while in range");
}

} // namespace

void inspector_section(int node, checksums::Name object_name, ImFont* mono, std::vector<ui::Request>* requests)
{
    const uint32_t object = object_name.value();
    (void)mono;
    detail::set_ui_active(false);
    const std::vector<nodes::Elem>& all = nodes::get_all();
    uint32_t name = object;
    int family = -1;
    if ((name == 0U) && node >= 0 && std::cmp_less(node, all.size())) {
        name = core::at(all, node).name;
    }
    const int index = (name != 0U) ? nodes::get_find(name) : -1;
    if (index >= 0) {
        family = static_cast<int>(core::at(all, index).family);
    }
    if (name == 0U) {
        return;
    }
    script::Struct* s = level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(name, false) : nullptr;
    if (object != 0U) {
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
        gui::text_wrapped("%s", when_applied(checksums::Name{name}, family));
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
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Show objects created later by scripts");
    }
}

} // namespace frontend::level_editor::entities
