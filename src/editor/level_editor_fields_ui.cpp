// NOT ORIGINAL - the level editor's full Inspector (phase 6, ours): a node's every field with a typed editor, and the
// level properties (level_editor_fields.h, level_editor_level_props.h, level_editor_ui.h). Only ImGui's own API here
// and the editor's pure / read-only calls (the node layer, Fields::rows_of); what's changed goes to the editor as
// requests.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

#include "editor/level_editor_fields.h"
#include "editor/level_editor_level_props.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_ui.h"
#include "imgui.h"
#include "engine/core/ints.h"

namespace frontend::level_editor::ui {

namespace {

constexpr ImVec4 kError = ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
constexpr ImVec4 kWarn = ImVec4(1.0f, 0.78f, 0.45f, 1.0f);
constexpr ImVec4 kLive = ImVec4(0.45f, 0.85f, 0.55f, 1.0f);
constexpr ImVec4 kReload = ImVec4(0.56f, 0.71f, 0.92f, 1.0f);

using ScratchBuf = std::array<char, 512>;

// The text being typed into each field (kept while its box is active; otherwise the field's value now).
struct Typing {
    int node = -1;
    uint32_t generation = 0;
    uint32_t active = 0;
    bool active_set_ = false;
    std::unordered_map<uint32_t, ScratchBuf> buffers;
    // The "add a field" row.
    ScratchBuf new_name_ = {}, new_value_ = {};
    int new_kind_ = 0;
};
Typing& typing()
{
    static Typing value;
    return value;
}

void dup_from(ScratchBuf* buffer, const std::string& text)
{
    std::snprintf(buffer->data(), buffer->size(), "%s", text.c_str());
}

void set_field(std::vector<Request>* requests, uint32_t node, uint32_t key, fields::Kind kind, const std::string& text,
               const std::string& label = "")
{
    Request request = {Request::Kind::kFieldSet};
    request.node = node;
    request.id = key;
    request.value = static_cast<int>(kind);
    request.text = text;
    request.label = label;
    requests->push_back(request);
}

void simple(std::vector<Request>* requests, Request::Kind kind, uint32_t node, uint32_t key)
{
    Request request = {kind};
    request.node = node;
    request.id = key;
    requests->push_back(request);
}

const char* when_label(fields::When when)
{
    switch (when) {
    case fields::When::kLive:
        return "live";
    case fields::When::kReload:
        return "on reload";
    case fields::When::kElsewhere:
        return "above";
    default:
        return "read-only";
    }
}

// A typed value checked as it's typed: the error ("" when fine) and a note (a name the game doesn't know).
std::string get_check(const fields::Row& row, uint32_t self, const char* text, std::string* note)
{
    fields::Value value;
    std::string error;
    note->clear();
    if (!fields::parse(row.value.kind, text, &value, &error))
        return error;
    if (row.key == fields::key_links()) {
        if (!fields::valid_links(
                value.list, self, [](uint32_t c) { return nodes::get_find(c) >= 0; }, fields::get_name_of, &error))
            return error;
    } else if (value.kind == fields::Kind::kName && !fields::get_name_of(value.checksum)) {
        *note = "unknown name (checksum used)";
    } else if (value.kind == fields::Kind::kNames) {
        for (const uint32_t c : value.list) {
            if (!fields::get_name_of(c))
                *note = "contains an unknown name";
        }
    }
    return "";
}

constexpr auto kAddKinds = std::to_array<fields::Kind>({fields::Kind::kInteger, fields::Kind::kFloat, fields::Kind::kName,
                                  fields::Kind::kChars,   fields::Kind::kVec4,  fields::Kind::kBitMark,
                                  fields::Kind::kNames});

} // namespace

void fields_section(int node_index, ImFont* mono, ImFont* small_font, std::vector<Request>* requests)
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (node_index < 0 || std::cmp_greater_equal(node_index, nodes.size()))
        return;
    const nodes::Elem& node = nodes[node_index];
    if (typing().node != node_index || typing().generation != nodes::get_generation()) {
        if (typing().node != node_index) {
            typing().buffers.clear();
            typing().active_set_ = false;
        }
        typing().node = node_index;
        typing().generation = nodes::get_generation();
    }
    const std::vector<fields::Row> rows = fields::rows_of(node);
    bool any_edited = false;
    for (const fields::Row& row : rows)
        any_edited = any_edited || row.edited;
    ImGui::Spacing();
    std::array<char, 64> header{};
    std::snprintf(header.data(), sizeof(header), "All fields (%d)%s###fields", static_cast<int>(rows.size()),
                  any_edited ? " - edited" : "");
    if (!ImGui::CollapsingHeader(header.data(), ImGuiTreeNodeFlags_DefaultOpen))
        return;
    bool still_active = false;
    if (ImGui::BeginTable("##fields", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("##when", ImGuiTableColumnFlags_WidthFixed);
        ImGui::PushFont(mono);
        for (const fields::Row& row : rows) {
            ImGui::PushID(static_cast<int>(row.key));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string name = fields::get_name_text(row.key, fields::get_name_of);
            if (row.included)
                ImGui::TextDisabled("%s", name.c_str());
            else if (row.edited)
                ImGui::TextColored(kWarn, "%s", name.c_str());
            else
                ImGui::TextUnformatted(name.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s%s%s", fields::kind_name(row.value.kind),
                                  row.included     ? " (shared by the class)"
                                  : row.overridden ? " (overrides the class)"
                                                   : "",
                                  row.edited ? "\nModified" : "");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            const std::string text = fields::get_format(row.value, fields::get_name_of);
            if (!row.editable) {
                ImGui::BeginDisabled(true);
                ImGui::TextWrapped("%s", text.c_str());
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", row.why.c_str());
            } else if (row.value.kind == fields::Kind::kBitMark) {
                bool on = row.value.on;
                if (ImGui::Checkbox("##flag", &on))
                    set_field(requests, node.name, row.key, fields::Kind::kBitMark, on ? "on" : "off");
            } else {
                const bool active = typing().active_set_ && typing().active == row.key;
                ScratchBuf& buffer = typing().buffers[row.key];
                if (!active)
                    dup_from(&buffer, text);
                const bool enter =
                    ImGui::InputText("##value", buffer.data(), buffer.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                const bool left = ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::IsItemActive()) {
                    typing().active = row.key;
                    typing().active_set_ = true;
                    still_active = true;
                }
                std::string note;
                const std::string error =
                    active || enter || left ? get_check(row, node.name, buffer.data(), &note) : "";
                if ((enter || left) && std::strcmp(buffer.data(), text.c_str()) != 0) {
                    if (error.empty())
                        set_field(requests, node.name, row.key, row.value.kind, buffer.data());
                    typing().active_set_ = false;
                } else if (enter || left) {
                    typing().active_set_ = false;
                }
                if (!error.empty() || !note.empty()) {
                    ImGui::PushFont(small_font);
                    ImGui::TextColored(error.empty() ? kWarn : kError, "%s",
                                       error.empty() ? note.c_str() : error.c_str());
                    ImGui::PopFont();
                }
            }
            ImGui::TableNextColumn();
            ImGui::PushFont(small_font);
            const ImVec4 colour = row.when == fields::When::kLive     ? kLive
                                  : row.when == fields::When::kReload ? kReload
                                                                      : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
            ImGui::TextColored(
                row.editable || row.when == fields::When::kElsewhere ? colour
                                                                     : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled],
                "%s", row.editable || row.when == fields::When::kElsewhere ? when_label(row.when) : "read-only");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", row.editable ? row.note : row.why.c_str());
            if (row.edited) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Home"))
                    simple(requests, Request::Kind::kFieldHome, node.name, row.key);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Revert");
            }
            if (row.editable && !row.included && row.value.kind != fields::Kind::kBitMark &&
                row.key != fields::key_links()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("x"))
                    simple(requests, Request::Kind::kFieldRemove, node.name, row.key);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(row.overridden ? "Remove override" : "Remove field");
            }
            ImGui::PopFont();
            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::EndTable();
    }
    if (!still_active && typing().active_set_) {
        // (the active box went away without being let go: a row that's gone)
        bool there = false;
        for (const fields::Row& row : rows)
            there = there || row.key == typing().active;
        typing().active_set_ = there && typing().active_set_;
    }
    // A field the node doesn't have yet.
    if (!node.added) {
        ImGui::PushFont(small_font);
        ImGui::TextDisabled("Add Field:");
        ImGui::PopFont();
        ImGui::PushFont(mono);
        ImGui::SetNextItemWidth(130.0f * ImGui::GetFontSize() / 13.0f);
        ImGui::InputTextWithHint("##newname", "name", typing().new_name_.data(), typing().new_name_.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::BeginCombo("##newkind", fields::kind_name(kAddKinds[typing().new_kind_]))) {
            for (int i = 0; i < ints::as_signed(sizeof(kAddKinds) / sizeof(kAddKinds[0])); ++i) {
                if (ImGui::Selectable(fields::kind_name(kAddKinds[i]), i == typing().new_kind_))
                    typing().new_kind_ = i;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-60.0f * ImGui::GetFontSize() / 13.0f);
        ImGui::InputTextWithHint("##newvalue", "value", typing().new_value_.data(), typing().new_value_.size());
        ImGui::PopFont();
        uint32_t key = 0;
        fields::Value value;
        std::string error;
        std::string name = typing().new_name_.data();
        const bool name_ok = fields::parse_name(name, &key) && name[0] != '#' && name[0] != '0';
        const bool value_ok = fields::parse(kAddKinds[typing().new_kind_], typing().new_value_.data(), &value, &error);
        bool exists = false;
        for (const fields::Row& row : rows)
            exists = exists || row.key == key;
        const char* why = name.empty() ? "Enter a field name"
                          : !name_ok   ? "Use letters, digits and _"
                          : exists     ? "Field already exists"
                          : key == fields::get_key_name() || key == fields::key_class() ||
                                  key == fields::get_key_type() || key == fields::key_pos() ||
                                  key == fields::key_angles() || key == fields::key_links()
                              ? "Edited elsewhere"
                          : !value_ok ? "Invalid value"
                                      : nullptr;
        ImGui::SameLine();
        ImGui::BeginDisabled(why != nullptr);
        if (ImGui::Button("Add")) {
            set_field(requests, node.name, key, kAddKinds[typing().new_kind_], typing().new_value_.data(), name);
            typing().new_name_ = {};
            typing().new_value_ = {};
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", why ? (why == std::string("Invalid value") ? error.c_str() : why)
                                        : "Add field (applies on reload)");
    }
    ImGui::PushFont(small_font);
    if (any_edited) {
        if (ImGui::SmallButton("Revert All Fields"))
            simple(requests, Request::Kind::kFieldHome, node.name, 0);
        ImGui::SameLine();
    }
    ImGui::TextDisabled("Grey: shared by the class");
    ImGui::PopFont();
}

// --- the level properties ---

namespace {

struct LevelTyping {
    ScratchBuf name = {};
    bool name_active_ = false;
} g_level;

void send_props(std::vector<Request>* requests, const level_props::Props& props, bool done)
{
    Request request = {Request::Kind::kLevelProps};
    request.props = props;
    requests->push_back(request);
    if (done)
        requests->push_back(Request{Request::Kind::kLevelPropsDone});
}

void get_when(ImFont* small_font, const char* when, const char* tip)
{
    ImGui::SameLine();
    ImGui::PushFont(small_font);
    ImGui::TextColored(std::strcmp(when, "on reload") == 0 ? kReload : kLive, "%s", when);
    ImGui::PopFont();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
}

} // namespace

void level_section(const ShapeMesh::LevelView& level, ImFont* mono, ImFont* small_font, std::vector<Request>* requests)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Level properties###levelprops"))
        return;
    if (!level.in_level_) {
        ImGui::TextDisabled("No level loaded");
        return;
    }
    ImGui::PushFont(small_font);
    ImGui::TextDisabled("Saved with the layout");
    ImGui::PopFont();
    level_props::Props props = level.props;
    const level_props::Props& own = level.defaults;
    if (ImGui::BeginTable("##level", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
        // Its name in the level select.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Name");
        ImGui::TableNextColumn();
        if (!g_level.name_active_)
            dup_from(&g_level.name, props.name);
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        const bool enter = ImGui::InputTextWithHint("##name", level.have_defaults_ ? own.name.c_str() : "Original",
                                                    g_level.name.data(), 65, ImGuiInputTextFlags_EnterReturnsTrue);
        const bool left = ImGui::IsItemDeactivatedAfterEdit();
        g_level.name_active_ = ImGui::IsItemActive();
        if ((enter || left) && props.name != g_level.name.data()) {
            props.name = g_level.name.data();
            send_props(requests, props, true);
        }
        get_when(small_font, "level select", "Name in the level select. Empty: original name.");
        // Where the skater starts.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Start");
        ImGui::TableNextColumn();
        std::string current = "(original)";
        for (const auto& r : level.restarts)
            current = r.first == props.start ? r.second : current;
        if (props.start && current == "(original)")
            current = "(missing restart)";
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        ImGui::PushFont(mono);
        if (ImGui::BeginCombo("##start", current.c_str())) {
            if (ImGui::Selectable("(original)", props.start == 0)) {
                props.start = 0;
                send_props(requests, props, true);
            }
            for (const auto& r : level.restarts) {
                ImGui::PushID(static_cast<int>(r.first));
                if (ImGui::Selectable(r.second.c_str(), props.start == r.first)) {
                    props.start = r.first;
                    send_props(requests, props, true);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopFont();
        get_when(small_font, "on reload", "Start position");
        if (level.selected_restart_ && level.selected_restart_ != props.start) {
            if (ImGui::SmallButton("Use Selected Restart")) {
                props.start = level.selected_restart_;
                send_props(requests, props, true);
            }
        }
        // Time of day.
        static const auto kStates = std::to_array<const char*>({"morning", "afternoon", "evening", "night"});
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Time of day");
        ImGui::TableNextColumn();
        std::array<char, 48> own_state{};
        std::snprintf(own_state.data(), sizeof(own_state), "(original: %s)",
                      level.have_defaults_ && level_props::tod_name(own.tod) ? level_props::tod_name(own.tod)
                                                                             : "afternoon");
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::BeginCombo("##tod", props.tod >= 0 ? kStates[props.tod] : own_state.data())) {
            if (ImGui::Selectable(own_state.data(), props.tod < 0)) {
                props.tod = -1;
                send_props(requests, props, true);
            }
            for (int i = 0; i < 4; ++i) {
                if (ImGui::Selectable(kStates[i], props.tod == i)) {
                    props.tod = i;
                    send_props(requests, props, true);
                }
            }
            ImGui::EndCombo();
        }
        get_when(small_font, "live", "Time of day");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Time passes");
        ImGui::TableNextColumn();
        const char* own_time = level.have_defaults_ && own.time == 0 ? "(original: no)" : "(original: yes)";
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::BeginCombo("##time", props.time == 1 ? "yes" : props.time == 0 ? "no" : own_time)) {
            const auto labels = std::to_array<const char*>({own_time, "no", "yes"});
            for (int i = -1; i <= 1; ++i) {
                if (ImGui::Selectable(labels[i + 1], props.time == i)) {
                    props.time = i;
                    send_props(requests, props, true);
                }
            }
            ImGui::EndCombo();
        }
        get_when(small_font, "on reload", "Advance time of day (career only)");
        // Its sky (the feature map's sky row): another level's sky scene.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Sky");
        ImGui::TableNextColumn();
        const std::string own_sky =
            "(original: " + (level.have_defaults_ && !own.sky.empty() ? own.sky : std::string("none")) + ")";
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        ImGui::PushFont(mono);
        if (ImGui::BeginCombo("##sky", props.sky.empty() ? own_sky.c_str() : props.sky.c_str())) {
            if (ImGui::Selectable(own_sky.c_str(), props.sky.empty())) {
                props.sky.clear();
                send_props(requests, props, true);
            }
            for (const std::string& sky : level_props::skies()) {
                if (ImGui::Selectable(sky.c_str(), _stricmp(sky.c_str(), props.sky.c_str()) == 0)) {
                    props.sky = sky;
                    send_props(requests, props, true);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopFont();
        get_when(small_font, "on reload", "Sky scene");
        // Ambient light.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Ambient");
        ImGui::TableNextColumn();
        bool ambient = props.has_ambient_;
        if (ImGui::Checkbox("##ambient", &ambient)) {
            props.has_ambient_ = ambient;
            if (ambient && level.have_defaults_)
                std::memcpy(props.ambient.data(), own.ambient.data(), sizeof(props.ambient));
            send_props(requests, props, true);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!props.has_ambient_);
        const int* shown = (props.has_ambient_ ? props.ambient : own.ambient).data();
        std::array<float, 3> colour = {{static_cast<float>(shown[0]) / 255.0f, static_cast<float>(shown[1]) / 255.0f,
                           static_cast<float>(shown[2]) / 255.0f}};
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::ColorEdit3("##ambientcolour", colour.data(), ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB)) {
            for (int i = 0; i < 3; ++i)
                // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
                props.ambient[i] = level_props::get_channel(static_cast<int>(colour[i] * 255.0f + 0.5f));
            send_props(requests, props, false);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            requests->push_back(Request{Request::Kind::kLevelPropsDone});
        ImGui::EndDisabled();
        get_when(small_font, "live", "Ambient light colour");
        // Fog.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Fog");
        ImGui::TableNextColumn();
        bool fog = props.has_fog_;
        if (ImGui::Checkbox("##fog", &fog)) {
            props.has_fog_ = fog;
            if (fog && level.have_defaults_)
                std::memcpy(props.fog.data(), own.fog.data(), sizeof(props.fog));
            send_props(requests, props, true);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!props.has_fog_);
        const int* f = (props.has_fog_ ? props.fog : own.fog).data();
        std::array<float, 4> fog_colour = {{static_cast<float>(f[0]) / 255.0f, static_cast<float>(f[1]) / 255.0f,
                               static_cast<float>(f[2]) / 255.0f, static_cast<float>(f[3]) / 255.0f}};
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        bool changed = false;
        if (ImGui::ColorEdit4("##fogcolour", fog_colour.data(),
                              ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB |
                                  ImGuiColorEditFlags_AlphaBar)) {
            for (int i = 0; i < 4; ++i)
                // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
                props.fog[i] = level_props::get_channel(static_cast<int>(fog_colour[i] * 255.0f + 0.5f));
            changed = true;
        }
        bool done = ImGui::IsItemDeactivatedAfterEdit();
        int distance = f[4];
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::DragInt("##fogdist", &distance, 10.0f, 0, 100000, "distance %d")) {
            props.fog[4] = level_props::span_to(distance);
            changed = true;
        }
        done = done || ImGui::IsItemDeactivatedAfterEdit();
        if (changed)
            send_props(requests, props, false);
        if (done)
            requests->push_back(Request{Request::Kind::kLevelPropsDone});
        ImGui::EndDisabled();
        get_when(small_font, "live", "Fog colour, alpha and distance (alpha 0: no fog)");
        ImGui::EndTable();
    }
    if (!level_props::vacant(level.props)) {
        if (ImGui::SmallButton("Revert All"))
            send_props(requests, level_props::Props{}, true);
    }
}

} // namespace frontend::level_editor::ui
