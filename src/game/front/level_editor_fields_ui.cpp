// NOT ORIGINAL - the level editor's full Inspector (phase 6, ours): a node's every field with a typed editor, and the
// level properties (level_editor_fields.h, level_editor_level_props.h, level_editor_ui.h). Only ImGui's own API here
// and the editor's pure / read-only calls (the node layer, Fields::RowsOf); what's changed goes to the editor as
// requests.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_level_props.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_ui.h"
#include "imgui.h"

namespace Front::LevelEditor::Ui {

namespace {

const ImVec4 kError = ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
const ImVec4 kWarn = ImVec4(1.0f, 0.78f, 0.45f, 1.0f);
const ImVec4 kLive = ImVec4(0.45f, 0.85f, 0.55f, 1.0f);
const ImVec4 kReload = ImVec4(0.56f, 0.71f, 0.92f, 1.0f);

using Buffer = std::array<char, 512>;

// The text being typed into each field (kept while its box is active; otherwise the field's value now).
struct Typing {
    int node = -1;
    uint32_t generation = 0;
    uint32_t active = 0;
    bool activeSet = false;
    std::unordered_map<uint32_t, Buffer> buffers;
    // The "add a field" row.
    Buffer newName = {}, newValue = {};
    int newKind = 0;
} g_typing;

void Copy(Buffer* buffer, const std::string& text)
{
    std::snprintf(buffer->data(), buffer->size(), "%s", text.c_str());
}

void SetField(std::vector<Request>* requests, uint32_t node, uint32_t key, Fields::Kind kind, const std::string& text,
              const std::string& label = "")
{
    Request request = {Request::kFieldSet};
    request.node = node;
    request.id = key;
    request.value = static_cast<int>(kind);
    request.text = text;
    request.label = label;
    requests->push_back(request);
}

void Simple(std::vector<Request>* requests, Request::Kind kind, uint32_t node, uint32_t key)
{
    Request request = {kind};
    request.node = node;
    request.id = key;
    requests->push_back(request);
}

const char* WhenLabel(Fields::When when)
{
    switch (when) {
    case Fields::When::Live: return "live";
    case Fields::When::Reload: return "on reload";
    case Fields::When::Elsewhere: return "above";
    default: return "read-only";
    }
}

// A typed value checked as it's typed: the error ("" when fine) and a note (a name the game doesn't know).
std::string Check(const Fields::Row& row, uint32_t self, const char* text, std::string* note)
{
    Fields::Value value;
    std::string error;
    note->clear();
    if (!Fields::Parse(row.value.kind, text, &value, &error))
        return error;
    if (row.key == Fields::KeyLinks()) {
        if (!Fields::ValidLinks(value.list, self, [](uint32_t c) { return Nodes::Find(c) >= 0; }, Fields::NameOf, &error))
            return error;
    } else if (value.kind == Fields::Kind::Name && !Fields::NameOf(value.checksum)) {
        *note = "not a name the game knows (its checksum is used)";
    } else if (value.kind == Fields::Kind::Names) {
        for (uint32_t c : value.list) {
            if (!Fields::NameOf(c))
                *note = "a name the game doesn't know is among them";
        }
    }
    return "";
}

const Fields::Kind kAddKinds[] = {Fields::Kind::Integer, Fields::Kind::Float, Fields::Kind::Name,
                                  Fields::Kind::String,  Fields::Kind::Vector, Fields::Kind::Flag,
                                  Fields::Kind::Names};

} // namespace

void FieldsSection(int nodeIndex, ImFont* mono, ImFont* smallFont, std::vector<Request>* requests)
{
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()))
        return;
    const Nodes::Node& node = nodes[nodeIndex];
    if (g_typing.node != nodeIndex || g_typing.generation != Nodes::Generation()) {
        if (g_typing.node != nodeIndex) {
            g_typing.buffers.clear();
            g_typing.activeSet = false;
        }
        g_typing.node = nodeIndex;
        g_typing.generation = Nodes::Generation();
    }
    std::vector<Fields::Row> rows = Fields::RowsOf(node);
    bool anyEdited = false;
    for (const Fields::Row& row : rows)
        anyEdited = anyEdited || row.edited;
    ImGui::Spacing();
    char header[64];
    std::snprintf(header, sizeof(header), "All fields (%d)%s###fields", static_cast<int>(rows.size()),
                  anyEdited ? " - edited" : "");
    if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
        return;
    bool stillActive = false;
    if (ImGui::BeginTable("##fields", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("##when", ImGuiTableColumnFlags_WidthFixed);
        ImGui::PushFont(mono);
        for (const Fields::Row& row : rows) {
            ImGui::PushID(static_cast<int>(row.key));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::string name = Fields::NameText(row.key, Fields::NameOf);
            if (row.included)
                ImGui::TextDisabled("%s", name.c_str());
            else if (row.edited)
                ImGui::TextColored(kWarn, "%s", name.c_str());
            else
                ImGui::TextUnformatted(name.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s%s%s", Fields::KindName(row.value.kind),
                                  row.included     ? " - from the class's shared structure (ncomp_...)"
                                  : row.overridden ? " - this node's own, over the shared structure's"
                                                   : "",
                                  row.edited ? "\nChanged by the editor (Home puts it back)" : "");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string text = Fields::Format(row.value, Fields::NameOf);
            if (!row.editable) {
                ImGui::BeginDisabled(true);
                ImGui::TextWrapped("%s", text.c_str());
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", row.why.c_str());
            } else if (row.value.kind == Fields::Kind::Flag) {
                bool on = row.value.on;
                if (ImGui::Checkbox("##flag", &on))
                    SetField(requests, node.name, row.key, Fields::Kind::Flag, on ? "on" : "off");
            } else {
                bool active = g_typing.activeSet && g_typing.active == row.key;
                Buffer& buffer = g_typing.buffers[row.key];
                if (!active)
                    Copy(&buffer, text);
                bool enter = ImGui::InputText("##value", buffer.data(), buffer.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                bool left = ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::IsItemActive()) {
                    g_typing.active = row.key;
                    g_typing.activeSet = true;
                    stillActive = true;
                }
                std::string note;
                std::string error = active || enter || left ? Check(row, node.name, buffer.data(), &note) : "";
                if ((enter || left) && std::strcmp(buffer.data(), text.c_str()) != 0) {
                    if (error.empty())
                        SetField(requests, node.name, row.key, row.value.kind, buffer.data());
                    g_typing.activeSet = false;
                } else if (enter || left) {
                    g_typing.activeSet = false;
                }
                if (!error.empty() || !note.empty()) {
                    ImGui::PushFont(smallFont);
                    ImGui::TextColored(error.empty() ? kWarn : kError, "%s", error.empty() ? note.c_str() : error.c_str());
                    ImGui::PopFont();
                }
            }
            ImGui::TableNextColumn();
            ImGui::PushFont(smallFont);
            ImVec4 colour = row.when == Fields::When::Live     ? kLive
                            : row.when == Fields::When::Reload ? kReload
                                                               : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
            ImGui::TextColored(row.editable || row.when == Fields::When::Elsewhere ? colour
                                                                                    : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled],
                               "%s", row.editable || row.when == Fields::When::Elsewhere ? WhenLabel(row.when) : "read-only");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", row.editable ? row.note : row.why.c_str());
            if (row.edited) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Home"))
                    Simple(requests, Request::kFieldHome, node.name, row.key);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Back as the level has it");
            }
            if (row.editable && !row.included && row.value.kind != Fields::Kind::Flag && row.key != Fields::KeyLinks()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("x"))
                    Simple(requests, Request::kFieldRemove, node.name, row.key);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(row.overridden ? "Take this node's own value out (the shared structure's counts again)"
                                                     : "Take the field out of the node");
            }
            ImGui::PopFont();
            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::EndTable();
    }
    if (!stillActive && g_typing.activeSet) {
        // (the active box went away without being let go: a row that's gone)
        bool there = false;
        for (const Fields::Row& row : rows)
            there = there || row.key == g_typing.active;
        g_typing.activeSet = there && g_typing.activeSet;
    }
    // A field the node doesn't have yet.
    if (!node.added) {
        ImGui::PushFont(smallFont);
        ImGui::TextDisabled("Add a field (on reload):");
        ImGui::PopFont();
        ImGui::PushFont(mono);
        ImGui::SetNextItemWidth(130.0f * ImGui::GetFontSize() / 13.0f);
        ImGui::InputTextWithHint("##newname", "name", g_typing.newName.data(), g_typing.newName.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::BeginCombo("##newkind", Fields::KindName(kAddKinds[g_typing.newKind]))) {
            for (int i = 0; i < static_cast<int>(sizeof(kAddKinds) / sizeof(kAddKinds[0])); ++i) {
                if (ImGui::Selectable(Fields::KindName(kAddKinds[i]), i == g_typing.newKind))
                    g_typing.newKind = i;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-60.0f * ImGui::GetFontSize() / 13.0f);
        ImGui::InputTextWithHint("##newvalue", "value", g_typing.newValue.data(), g_typing.newValue.size());
        ImGui::PopFont();
        uint32_t key = 0;
        Fields::Value value;
        std::string error;
        std::string name = g_typing.newName.data();
        bool nameOk = Fields::ParseName(name, &key) && name[0] != '#' && name[0] != '0';
        bool valueOk = Fields::Parse(kAddKinds[g_typing.newKind], g_typing.newValue.data(), &value, &error);
        bool exists = false;
        for (const Fields::Row& row : rows)
            exists = exists || row.key == key;
        const char* why = name.empty()  ? "Type the field's name"
                          : !nameOk     ? "A name: letters, digits and _ (not starting with a digit)"
                          : exists      ? "The node has that field: edit it in the table"
                          : key == Fields::KeyName() || key == Fields::KeyClass() || key == Fields::KeyType() ||
                                    key == Fields::KeyPos() || key == Fields::KeyAngles() || key == Fields::KeyLinks()
                              ? "Not that one (its identity / place / links are edited elsewhere)"
                          : !valueOk ? "The value isn't one"
                                     : nullptr;
        ImGui::SameLine();
        ImGui::BeginDisabled(why != nullptr);
        if (ImGui::Button("Add")) {
            SetField(requests, node.name, key, kAddKinds[g_typing.newKind], g_typing.newValue.data(), name);
            g_typing.newName = {};
            g_typing.newValue = {};
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", why ? (why == std::string("The value isn't one") ? error.c_str() : why)
                                        : "Added to this node (saved; the level reads it when the edited copy loads)");
    }
    ImGui::PushFont(smallFont);
    if (anyEdited) {
        if (ImGui::SmallButton("Put back every field"))
            Simple(requests, Request::kFieldHome, node.name, 0);
        ImGui::SameLine();
    }
    ImGui::TextDisabled("live: read each time it's used; on reload: when the edited copy is next loaded. Greyed: "
                        "the class's shared structure - a value typed there is this node's own.");
    ImGui::PopFont();
}

// --- the level properties ---

namespace {

struct LevelTyping {
    Buffer name = {};
    bool nameActive = false;
} g_level;

void SendProps(std::vector<Request>* requests, const LevelProps::Props& props, bool done)
{
    Request request = {Request::kLevelProps};
    request.props = props;
    requests->push_back(request);
    if (done)
        requests->push_back(Request{Request::kLevelPropsDone});
}

void When(ImFont* smallFont, const char* when, const char* tip)
{
    ImGui::SameLine();
    ImGui::PushFont(smallFont);
    ImGui::TextColored(std::strcmp(when, "on reload") == 0 ? kReload : kLive, "%s", when);
    ImGui::PopFont();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
}

} // namespace

void LevelSection(const Model::LevelView& level, ImFont* mono, ImFont* smallFont, std::vector<Request>* requests)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Level properties###levelprops"))
        return;
    if (!level.inLevel) {
        ImGui::TextDisabled("Not in a level.");
        return;
    }
    ImGui::PushFont(smallFont);
    ImGui::TextWrapped("The edited copy's own settings: kept in its layout (Ctrl+S), put in place when the edited copy "
                       "loads from the level select's EDITED LEVELS - never on the level as shipped, never online.");
    ImGui::PopFont();
    LevelProps::Props props = level.props;
    const LevelProps::Props& own = level.defaults;
    if (ImGui::BeginTable("##level", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
        // Its name in the level select.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Name");
        ImGui::TableNextColumn();
        if (!g_level.nameActive)
            Copy(&g_level.name, props.name);
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        bool enter = ImGui::InputTextWithHint("##name", level.haveDefaults ? own.name.c_str() : "the level's",
                                              g_level.name.data(), 65, ImGuiInputTextFlags_EnterReturnsTrue);
        bool left = ImGui::IsItemDeactivatedAfterEdit();
        g_level.nameActive = ImGui::IsItemActive();
        if ((enter || left) && props.name != g_level.name.data()) {
            props.name = g_level.name.data();
            SendProps(requests, props, true);
        }
        When(smallFont, "level select", "The EDITED LEVELS entry reads \"<name> (EDITED)\" next time the level select "
                                    "opens (after Ctrl+S); the toolbar shows it now. Empty: the level's own name.");
        // Where the skater starts.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Start");
        ImGui::TableNextColumn();
        std::string current = "(the level's own)";
        for (const auto& r : level.restarts)
            current = r.first == props.start ? r.second : current;
        if (props.start && current == "(the level's own)")
            current = "(a restart that's gone)";
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        ImGui::PushFont(mono);
        if (ImGui::BeginCombo("##start", current.c_str())) {
            if (ImGui::Selectable("(the level's own)", props.start == 0)) {
                props.start = 0;
                SendProps(requests, props, true);
            }
            for (const auto& r : level.restarts) {
                ImGui::PushID(static_cast<int>(r.first));
                if (ImGui::Selectable(r.second.c_str(), props.start == r.first)) {
                    props.start = r.first;
                    SendProps(requests, props, true);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopFont();
        When(smallFont, "on reload", "The skater is put at this restart once the edited copy is up.");
        if (level.selectedRestart && level.selectedRestart != props.start) {
            if (ImGui::SmallButton("Use the selected restart")) {
                props.start = level.selectedRestart;
                SendProps(requests, props, true);
            }
        }
        // Time of day.
        static const char* const kStates[] = {"morning", "afternoon", "evening", "night"};
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Time of day");
        ImGui::TableNextColumn();
        char ownState[48];
        std::snprintf(ownState, sizeof(ownState), "(the level's: %s)",
                      level.haveDefaults && LevelProps::TodName(own.tod) ? LevelProps::TodName(own.tod) : "afternoon");
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::BeginCombo("##tod", props.tod >= 0 ? kStates[props.tod] : ownState)) {
            if (ImGui::Selectable(ownState, props.tod < 0)) {
                props.tod = -1;
                SendProps(requests, props, true);
            }
            for (int i = 0; i < 4; ++i) {
                if (ImGui::Selectable(kStates[i], props.tod == i)) {
                    props.tod = i;
                    SendProps(requests, props, true);
                }
            }
            ImGui::EndCombo();
        }
        When(smallFont, "live", "Shown at once (the time-of-day manager's state); the edited copy starts in it.");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Time passes");
        ImGui::TableNextColumn();
        const char* ownTime = level.haveDefaults && own.time == 0 ? "(the level's: no)" : "(the level's: yes)";
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::BeginCombo("##time", props.time == 1 ? "yes" : props.time == 0 ? "no" : ownTime)) {
            const char* labels[] = {ownTime, "no", "yes"};
            for (int i = -1; i <= 1; ++i) {
                if (ImGui::Selectable(labels[i + 1], props.time == i)) {
                    props.time = i;
                    SendProps(requests, props, true);
                }
            }
            ImGui::EndCombo();
        }
        When(smallFont, "on reload", "Whether the time of day moves on (career mode only - the game's own rule).");
        // Its sky (the feature map's sky row): another level's sky scene.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Sky");
        ImGui::TableNextColumn();
        std::string ownSky = "(the level's: " + (level.haveDefaults && !own.sky.empty() ? own.sky : std::string("none")) + ")";
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        ImGui::PushFont(mono);
        if (ImGui::BeginCombo("##sky", props.sky.empty() ? ownSky.c_str() : props.sky.c_str())) {
            if (ImGui::Selectable(ownSky.c_str(), props.sky.empty())) {
                props.sky.clear();
                SendProps(requests, props, true);
            }
            for (const std::string& sky : LevelProps::Skies()) {
                if (ImGui::Selectable(sky.c_str(), _stricmp(sky.c_str(), props.sky.c_str()) == 0)) {
                    props.sky = sky;
                    SendProps(requests, props, true);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopFont();
        When(smallFont, "on reload", "The level structure's sky= - the sky scene load_level loads before the level "
                                    "(any level's: the skies are loose files). Shown when the edited copy next loads "
                                    "from the level select.");
        // Ambient light.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Ambient");
        ImGui::TableNextColumn();
        bool ambient = props.hasAmbient;
        if (ImGui::Checkbox("##ambient", &ambient)) {
            props.hasAmbient = ambient;
            if (ambient && level.haveDefaults)
                std::memcpy(props.ambient, own.ambient, sizeof(props.ambient));
            SendProps(requests, props, true);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!props.hasAmbient);
        const int* shown = props.hasAmbient ? props.ambient : own.ambient;
        float colour[3] = {shown[0] / 255.0f, shown[1] / 255.0f, shown[2] / 255.0f};
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::ColorEdit3("##ambientcolour", colour, ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB)) {
            for (int i = 0; i < 3; ++i)
                props.ambient[i] = LevelProps::Channel(static_cast<int>(colour[i] * 255.0f + 0.5f));
            SendProps(requests, props, false);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            requests->push_back(Request{Request::kLevelPropsDone});
        ImGui::EndDisabled();
        When(smallFont, "on reload", "The level structure's ambient_red / _green / _blue (as Levels.qb sets them).");
        // Fog.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Fog");
        ImGui::TableNextColumn();
        bool fog = props.hasFog;
        if (ImGui::Checkbox("##fog", &fog)) {
            props.hasFog = fog;
            if (fog && level.haveDefaults)
                std::memcpy(props.fog, own.fog, sizeof(props.fog));
            SendProps(requests, props, true);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!props.hasFog);
        const int* f = props.hasFog ? props.fog : own.fog;
        float fogColour[4] = {f[0] / 255.0f, f[1] / 255.0f, f[2] / 255.0f, f[3] / 255.0f};
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        bool changed = false;
        if (ImGui::ColorEdit4("##fogcolour", fogColour,
                              ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_AlphaBar)) {
            for (int i = 0; i < 4; ++i)
                props.fog[i] = LevelProps::Channel(static_cast<int>(fogColour[i] * 255.0f + 0.5f));
            changed = true;
        }
        bool done = ImGui::IsItemDeactivatedAfterEdit();
        int distance = f[4];
        ImGui::SetNextItemWidth(-90.0f * ImGui::GetFontSize() / 13.0f);
        if (ImGui::DragInt("##fogdist", &distance, 10.0f, 0, 100000, "distance %d")) {
            props.fog[4] = LevelProps::Distance(distance);
            changed = true;
        }
        done = done || ImGui::IsItemDeactivatedAfterEdit();
        if (changed)
            SendProps(requests, props, false);
        if (done)
            requests->push_back(Request{Request::kLevelPropsDone});
        ImGui::EndDisabled();
        When(smallFont, "on reload", "The level structure's fog_red / _green / _blue / _alpha / _dist (as Levels.qb sets "
                                 "them; 0 alpha: no fog).");
        ImGui::EndTable();
    }
    if (!LevelProps::Empty(level.props)) {
        if (ImGui::SmallButton("All back as the level has them"))
            SendProps(requests, LevelProps::Props{}, true);
    }
}

} // namespace Front::LevelEditor::Ui
