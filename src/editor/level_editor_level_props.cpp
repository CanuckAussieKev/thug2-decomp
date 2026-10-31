// NOT ORIGINAL - the level editor's level properties (ours). See level_editor_level_props.h.

#include "editor/level_editor_level_props.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/core/checksums.h"
#include "engine/gfx/tod_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "game/skate/skate_module.h"

namespace frontend::level_editor::level_props {

namespace {

const char* const kTimeOfDayLabels[kStates] = {"morning", "afternoon", "evening", "night"};

bool read_ints(const std::string& text, int* out, int count)
{
    std::string error;
    fields::Value v;
    if (!fields::parse(fields::Kind::kIntegers, text, &v, &error) || static_cast<int>(v.list.size()) != count)
        return false;
    for (int i = 0; i < count; ++i)
        out[i] = static_cast<int32_t>(v.list[i]);
    return true;
}

fields::Value Int(int value)
{
    fields::Value v;
    v.kind = fields::Kind::kInteger;
    v.integer = value;
    return v;
}

} // namespace

const char* tod_name(int tod) { return tod >= 0 && tod < kStates ? kTimeOfDayLabels[tod] : nullptr; }

bool tod_from_name(const std::string& name, int* tod)
{
    for (int i = 0; i < kStates; ++i) {
        if (_stricmp(name.c_str(), kTimeOfDayLabels[i]) == 0) {
            *tod = i;
            return true;
        }
    }
    return false;
}

bool get_same(const Props& a, const Props& b)
{
    return a.name == b.name && a.start == b.start && a.tod == b.tod && a.time == b.time &&
           a.has_ambient_ == b.has_ambient_ &&
           (!a.has_ambient_ || std::memcmp(a.ambient, b.ambient, sizeof(a.ambient)) == 0) && a.has_fog_ == b.has_fog_ &&
           (!a.has_fog_ || std::memcmp(a.fog, b.fog, sizeof(a.fog)) == 0) &&
           _stricmp(a.sky.c_str(), b.sky.c_str()) == 0;
}

bool vacant(const Props& props) { return get_same(props, Props{}); }

bool valid_sky(const std::string& sky)
{
    if (sky.empty() || sky.size() > 64)
        return false;
    std::string lower;
    for (char c : sky) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower.find("_sky") != std::string::npos;
}

int get_channel(int value) { return value < 0 ? 0 : value > 255 ? 255 : value; }
int span_to(int value) { return value < 0 ? 0 : value > 100000 ? 100000 : value; }

std::string write_record(const Props& props)
{
    if (vacant(props))
        return "";
    std::string line = "level";
    char text[96];
    if (!props.name.empty())
        line += " name=" + fields::escape(props.name);
    if (props.start) {
        std::snprintf(text, sizeof(text), " start=%08X", props.start);
        line += text;
    }
    if (tod_name(props.tod))
        line += std::string(" tod=") + tod_name(props.tod);
    if (props.time == 0 || props.time == 1)
        line += props.time ? " time=1" : " time=0";
    if (props.has_ambient_) {
        std::snprintf(text, sizeof(text), " ambient=%d,%d,%d", props.ambient[0], props.ambient[1], props.ambient[2]);
        line += text;
    }
    if (props.has_fog_) {
        std::snprintf(text, sizeof(text), " fog=%d,%d,%d,%d,%d", props.fog[0], props.fog[1], props.fog[2], props.fog[3],
                      props.fog[4]);
        line += text;
    }
    if (!props.sky.empty())
        line += " sky=" + fields::escape(props.sky);
    return line + "\n";
}

bool read_record(std::istringstream& in, Props* props)
{
    Props p;
    std::string field;
    while (in >> field) {
        size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0)
            return false;
        std::string key = field.substr(0, equals), value = field.substr(equals + 1);
        if (key == "name") {
            if (!fields::unescape(value, &p.name) || p.name.empty() || p.name.size() > 64)
                return false;
        } else if (key == "start") {
            if (!fields::parse_name("0x" + value, &p.start) || value.size() != 8 || !p.start)
                return false;
        } else if (key == "tod") {
            if (!tod_from_name(value, &p.tod))
                return false;
        } else if (key == "time") {
            if (value != "0" && value != "1")
                return false;
            p.time = value == "1" ? 1 : 0;
        } else if (key == "ambient") {
            if (!read_ints(value, p.ambient, 3))
                return false;
            p.has_ambient_ = true;
        } else if (key == "fog") {
            if (!read_ints(value, p.fog, 5))
                return false;
            p.has_fog_ = true;
        } else if (key == "sky") {
            if (!fields::unescape(value, &p.sky) || !valid_sky(p.sky))
                return false;
        } // (another key: a later version's - skipped)
    }
    *props = p;
    return true;
}

std::vector<std::pair<const char*, fields::Value>> structure_values(const Props& props)
{
    std::vector<std::pair<const char*, fields::Value>> out;
    if (tod_name(props.tod)) {
        fields::Value v;
        v.kind = fields::Kind::kName;
        v.checksum = fields::hash_of_name(tod_name(props.tod));
        out.push_back({"tod_state", v});
    }
    if (props.time == 0 || props.time == 1)
        out.push_back({"tod_manager_active", Int(props.time)});
    // (map4: our own keys - the engine reads no level structure's ambient_* / fog_*; on_level_up reads these)
    if (props.has_ambient_) {
        fields::Value v;
        v.kind = fields::Kind::kIntegers;
        for (int i = 0; i < 3; ++i)
            v.list.push_back(static_cast<uint32_t>(get_channel(props.ambient[i])));
        out.push_back({"ed_ambient", v});
    }
    if (props.has_fog_) {
        fields::Value v;
        v.kind = fields::Kind::kIntegers;
        for (int i = 0; i < 4; ++i)
            v.list.push_back(static_cast<uint32_t>(get_channel(props.fog[i])));
        v.list.push_back(static_cast<uint32_t>(span_to(props.fog[4])));
        out.push_back({"ed_fog", v});
    }
    if (valid_sky(props.sky)) {
        fields::Value v;
        v.kind = fields::Kind::kChars;
        v.text = props.sky;
        out.push_back({"sky", v});
    }
    return out;
}

bool get_override(const Props& edited, const Props& level, gfx::TodParams* params)
{
    const Props* ambient = edited.has_ambient_ ? &edited : level.has_ambient_ ? &level : nullptr;
    if (ambient) {
        params->ambient_red_ = get_channel(ambient->ambient[0]);
        params->ambient_green_ = get_channel(ambient->ambient[1]);
        params->ambient_blue_ = get_channel(ambient->ambient[2]);
    }
    const Props* fog = edited.has_fog_ ? &edited : level.has_fog_ ? &level : nullptr;
    if (!fog)
        return false;
    params->fog_red_ = get_channel(fog->fog[0]);
    params->fog_green_ = get_channel(fog->fog[1]);
    params->fog_blue_ = get_channel(fog->fog[2]);
    params->fog_alpha_ = get_channel(fog->fog[3]);
    params->fog_dist_ = span_to(fog->fog[4]);
    return true;
}

void read_override_keys(script::Struct* level, Props* out)
{
    static const uint32_t kAmbient = checksums::hash_from_text("ed_ambient"),
                          kFog = checksums::hash_from_text("ed_fog");
    auto read = [&](uint32_t key, int* values, uint32_t count) {
        script::Array* array = nullptr;
        if (!level || !level->fetch_list(key, &array, 0) || !array || array->kind_tag_ != script::kQbvalInteger ||
            array->extent_ != count)
            return false;
        for (uint32_t i = 0; i < count; ++i)
            values[i] = static_cast<int32_t>(array->fetch_part(i));
        return true;
    };
    out->has_ambient_ = read(kAmbient, out->ambient, 3);
    out->has_fog_ = read(kFog, out->fog, 5);
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

Props g_props;
// (map4) The level structure's ed_ambient / ed_fog (a bake's - or the edited copy's, written for its load).
Props g_level;
bool g_filter_set;

bool get_filter(gfx::TodParams* params) { return get_override(g_props, g_level, params); }

void install_filter()
{
    if (!g_filter_set) {
        g_filter_set = true;
        gfx::set_tod_params_filter(&get_filter);
    }
}

bool lighting_same(const Props& a, const Props& b)
{
    return a.has_ambient_ == b.has_ambient_ &&
           (!a.has_ambient_ || std::memcmp(a.ambient, b.ambient, sizeof(a.ambient)) == 0) && a.has_fog_ == b.has_fog_ &&
           (!a.has_fog_ || std::memcmp(a.fog, b.fog, sizeof(a.fog)) == 0);
}
std::vector<std::string> g_skies;
bool g_skies_made;

// The level structure's values put in by export_to_struct, and what was there before.
struct Kept {
    uint32_t key;
    fields::Own before;
};
script::Struct* g_written;
std::vector<Kept> g_kept;

// The level structure of the level loaded now (Levels.qb's Level_<x>: its load_script is the skate module's level).
script::Struct* current_level_structure()
{
    modules::Skate* skate = modules::g_skate_module;
    uint32_t load = skate ? skate->level_ : 0;
    if (!load)
        return nullptr;
    static const uint32_t kLoadScript = checksums::hash_from_text("load_script");
    for (script::SymbolTableEntry* e = script::get_next_symbol(nullptr); e; e = script::get_next_symbol(e)) {
        uint32_t value = 0;
        if (e->type == script::kQbvalStruct && e->structure && e->structure->read_hash_value(kLoadScript, &value, 0) &&
            value == load)
            return e->structure;
    }
    return nullptr;
}

void show_time_of_day(int tod)
{
    gfx::CtodManager* manager = gfx::tod_manager();
    if (manager && tod_name(tod))
        manager->set_tod_instant(checksums::hash_from_text(tod_name(tod)));
}

} // namespace

const Props& at_hand() { return g_props; }

void assign_value(const Props& props)
{
    int tod = props.tod;
    if (tod != g_props.tod && tod < 0) {
        // (back to the level's own state)
        Props defaults;
        if (level_defaults(&defaults))
            tod = defaults.tod;
    }
    bool tod_changed = props.tod != g_props.tod;
    bool fog_was = g_props.has_fog_ || g_level.has_fog_;
    bool lighting = !lighting_same(props, g_props);
    g_props = props;
    install_filter();
    if (tod_changed)
        show_time_of_day(tod); // (its refresh sends the ambient / fog through the filter too)
    if (lighting && !tod_changed)
        reapply();
    // (a fog the editor had forced on, taken off where the manager's fog is off: off again - the manager's own switch)
    gfx::CtodManager* manager = gfx::tod_manager();
    if (fog_was && !g_props.has_fog_ && !g_level.has_fog_ && manager && !manager->fog_active_) {
        manager->fog_active_ = true;
        manager->set_fog_active(false);
    }
}

void on_level_up(bool network)
{
    Props level;
    if (!network)
        read_override_keys(current_level_structure(), &level);
    bool changed = !lighting_same(level, g_level);
    g_level = level;
    if (!changed)
        return;
    install_filter();
    reapply();
    get_log("level editor: the level structure's own lighting (ed_ambient / ed_fog) put in place:%s%s",
            level.has_ambient_ ? " ambient" : "", level.has_fog_ ? " fog" : "");
}

void reapply()
{
    gfx::CtodManager* manager = gfx::tod_manager();
    // (no parameters yet: nothing to send; lights a script set from a named state - reset_level_lights - kept: the
    // override goes with the next state change)
    if (!manager || !manager->params_set_ || manager->level_lights_reset_)
        return;
    install_filter();
    manager->apply_tod_params(manager->get_tod_params(-1));
}

bool level_defaults(Props* props)
{
    script::Struct* level = current_level_structure();
    if (!level)
        return false;
    Props p;
    const char* name = nullptr;
    if (level->fetch_text(checksums::hash_from_text("name"), &name, 0) && name)
        p.name = name;
    const char* sky = nullptr;
    if (level->fetch_text(checksums::hash_from_text("sky"), &sky, 0) && sky)
        p.sky = sky;
    uint32_t state = 0;
    p.tod = 1; // (load_level: afternoon when the level names none)
    if (level->read_hash_value(checksums::hash_from_text("tod_state"), &state, 0)) {
        for (int i = 0; i < kStates; ++i)
            p.tod = state == checksums::hash_from_text(kTimeOfDayLabels[i]) ? i : p.tod;
    }
    int value = 1;
    p.time = level->fetch_int_value(checksums::hash_from_text("tod_manager_active"), &value, 0) && value == 0 ? 0 : 1;
    // (map4) The ambient light and fog the level shows: the time-of-day manager's current state's - where the engine's
    // come from - with the level structure's ed_ keys (a bake's) over them.
    gfx::CtodManager* manager = gfx::tod_manager();
    if (manager && manager->params_set_) {
        gfx::TodParams now = manager->get_tod_params(-1);
        get_override(Props{}, g_level, &now);
        p.has_ambient_ = true;
        p.ambient[0] = now.ambient_red_;
        p.ambient[1] = now.ambient_green_;
        p.ambient[2] = now.ambient_blue_;
        p.has_fog_ = true;
        int fog[5] = {now.fog_red_, now.fog_green_, now.fog_blue_, now.fog_alpha_, now.fog_dist_};
        std::memcpy(p.fog, fog, sizeof(fog));
    }
    *props = p;
    return true;
}

void forget_level()
{
    g_props = Props{};
    g_level = Props{}; // (before the new level's load sends its lighting: on_level_up reads its keys once it's up)
    g_skies_made = false;
}

const std::vector<std::string>& skies()
{
    if (g_skies_made)
        return g_skies;
    g_skies_made = true;
    g_skies.clear();
    static const uint32_t kSky = checksums::hash_from_text("sky"),
                          kLoadScript = checksums::hash_from_text("load_script");
    for (script::SymbolTableEntry* e = script::get_next_symbol(nullptr); e; e = script::get_next_symbol(e)) {
        const char* sky = nullptr;
        uint32_t load = 0;
        // (a level structure: it names its load script - not some other structure with a sky=)
        if (e->type != script::kQbvalStruct || !e->structure || !e->structure->read_hash_value(kLoadScript, &load, 0) ||
            !e->structure->fetch_text(kSky, &sky, 0) || !sky || !valid_sky(sky))
            continue;
        bool there = false;
        for (const std::string& s : g_skies)
            there = there || _stricmp(s.c_str(), sky) == 0;
        if (!there)
            g_skies.push_back(sky);
    }
    std::sort(g_skies.begin(), g_skies.end(),
              [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
    return g_skies;
}

void export_to_struct(script::Struct* level, const Props& props)
{
    restore_structure();
    if (!level)
        return;
    std::vector<std::pair<const char*, fields::Value>> values = structure_values(props);
    if (values.empty())
        return;
    g_written = level;
    for (const auto& entry : values) {
        uint32_t key = checksums::hash_from_text(entry.first);
        g_kept.push_back({key, fields::own_in(level, key, false)});
        fields::Own own;
        own.present = true;
        own.value = entry.second;
        fields::put_in(level, key, false, own);
    }
    get_log("level editor: the edited copy's level properties written into its level structure (%u values)",
            static_cast<unsigned>(values.size()));
}

void restore_structure()
{
    if (g_written) {
        for (auto it = g_kept.rbegin(); it != g_kept.rend(); ++it)
            fields::put_in(g_written, it->key, false, it->before);
    }
    g_written = nullptr;
    g_kept.clear();
}

void on_edited_copy_up(const Props& props)
{
    static const uint32_t kClass = checksums::hash_from_text("Class"), kRestart = checksums::hash_from_text("Restart");
    script::Struct* node =
        props.start && level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(props.start, false) : nullptr;
    uint32_t cls = 0;
    if (!node || !node->read_hash_value(kClass, &cls, 0) || cls != kRestart) {
        if (props.start)
            get_log("level editor: the edited copy's start %08X isn't a restart of this level - not used", props.start);
        return;
    }
    script::Struct* params = script::spawn_param_bag()->prepare_all();
    params->put_name_key(checksums::hash_from_text("node_name"), props.start);
    script::launch_script(checksums::hash_from_text("ResetSkaters"), params, nullptr, false, "ResetSkaters");
    params->get_clear();
    script::delete_struct(params);
    get_log("level editor: the edited copy's start - the skater put at %08X", props.start);
}

} // namespace frontend::level_editor::level_props
