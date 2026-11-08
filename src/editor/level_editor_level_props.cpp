// NOT ORIGINAL - the level editor's level properties (ours). See level_editor_level_props.h.

#include "editor/level_editor_level_props.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor_fields.h"
#include "base/core/checksums.h"
#include "engine/gfx/tod_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "game/skate/skate_module.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"

namespace frontend::level_editor::level_props {

namespace {

constexpr std::size_t kMaxSkyNameLength = 64;
constexpr int kColourChannelMax = 255;
constexpr int kSpanLimit = 100000; // the largest fog / sky distance accepted
constexpr std::size_t kPropsLineSize = 96; // the numbers of a level-properties line
constexpr std::size_t kMaxLevelNameLength = 64;
constexpr std::size_t kStartHexDigits = 8; // the start node's checksum

} // namespace

namespace {

const std::array<const char*, kStates> kTimeOfDayLabels = {{"morning", "afternoon", "evening", "night"}};

bool read_ints(const std::string& text, std::span<int> out, int count)
{
    std::string error;
    fields::Value v;
    if (!fields::parse(fields::Kind::kIntegers, text, &v, &error) || std::cmp_not_equal(v.list.size(), count)) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        core::at(out, i) = static_cast<int32_t>(core::at(v.list, i));
    }
    return true;
}

fields::Value int_field(int value)
{
    fields::Value v;
    v.kind = fields::Kind::kInteger;
    v.integer = value;
    return v;
}

} // namespace

const char* tod_name(int tod) { return tod >= 0 && tod < kStates ? core::at(kTimeOfDayLabels, tod) : nullptr; }

bool tod_from_name(const std::string& name, int* tod)
{
    for (int i = 0; i < kStates; ++i) {
        if (_stricmp(name.c_str(), core::at(kTimeOfDayLabels, i)) == 0) {
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
           (!a.has_ambient_ || std::memcmp(a.ambient.data(), b.ambient.data(), sizeof(a.ambient)) == 0) && a.has_fog_ == b.has_fog_ &&
           (!a.has_fog_ || std::memcmp(a.fog.data(), b.fog.data(), sizeof(a.fog)) == 0) &&
           _stricmp(a.sky.c_str(), b.sky.c_str()) == 0 && a.fall_depth == b.fall_depth && a.has_sun_ == b.has_sun_ &&
           (!a.has_sun_ || a.sun == b.sun);
}

bool vacant(const Props& props) { return get_same(props, Props{}); }

bool valid_sky(const std::string& sky)
{
    if (sky.empty() || sky.size() > kMaxSkyNameLength) {
        return false;
    }
    std::string lower;
    for (const char character : sky) {
        if ((std::isalnum(static_cast<unsigned char>(character)) == 0) && character != '_') {
            return false;
        }
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lower.find("_sky") != std::string::npos;
}

int get_channel(int value) { return std::clamp(value, 0, kColourChannelMax); }
int span_to(int value) { return std::clamp(value, 0, kSpanLimit); }

std::string write_record(const Props& props)
{
    if (vacant(props)) {
        return "";
    }
    std::string line = "level";
    std::array<char, kPropsLineSize> text{};
    if (!props.name.empty()) {
        line += " name=" + fields::escape(props.name);
    }
    if (props.start != 0U) {
        text::format_to(text, " start=%08X", props.start);
        line += text.data();
    }
    if (tod_name(props.tod)) {
        line += std::string(" tod=") + tod_name(props.tod);
    }
    if (props.time == 0 || props.time == 1) {
        line += (props.time != 0) ? " time=1" : " time=0";
    }
    if (props.has_ambient_) {
        text::format_to(text, " ambient=%d,%d,%d", core::at(props.ambient, 0), core::at(props.ambient, 1),
                        core::at(props.ambient, 2));
        line += text.data();
    }
    if (props.has_fog_) {
        text::format_to(text, " fog=%d,%d,%d,%d,%d", core::at(props.fog, 0), core::at(props.fog, 1),
                        core::at(props.fog, 2), core::at(props.fog, 3), core::at(props.fog, 4));
        line += text.data();
    }
    if (!props.sky.empty()) {
        line += " sky=" + fields::escape(props.sky);
    }
    if (props.fall_depth >= 0) {
        text::format_to(text, " fall=%d", props.fall_depth);
        line += text.data();
    }
    if (props.has_sun_) {
        text::format_to(text, " sun=%d,%d,%d,%d", core::at(props.sun, 0), core::at(props.sun, 1), core::at(props.sun, 2),
                        core::at(props.sun, 3));
        line += text.data();
    }
    return line + "\n";
}

bool read_record(std::istringstream& in, Props* props)
{
    Props parsed;
    std::string field;
    while (in >> field) {
        const size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0) {
            return false;
        }
        const std::string key = field.substr(0, equals);
        const std::string value = field.substr(equals + 1);
        if (key == "name") {
            if (!fields::unescape(value, &parsed.name) || parsed.name.empty() ||
                parsed.name.size() > kMaxLevelNameLength) {
                return false;
            }
        } else if (key == "start") {
            if (!fields::parse_name("0x" + value, &parsed.start) || value.size() != kStartHexDigits ||
                (parsed.start == 0U)) {
                return false;
            }
        } else if (key == "tod") {
            if (!tod_from_name(value, &parsed.tod)) {
                return false;
            }
        } else if (key == "time") {
            if (value != "0" && value != "1") {
                return false;
            }
            parsed.time = value == "1" ? 1 : 0;
        } else if (key == "ambient") {
            if (!read_ints(value, parsed.ambient, 3)) {
                return false;
            }
            parsed.has_ambient_ = true;
        } else if (key == "fog") {
            if (!read_ints(value, parsed.fog, kFogValues)) {
                return false;
            }
            parsed.has_fog_ = true;
        } else if (key == "sky") {
            if (!fields::unescape(value, &parsed.sky) || !valid_sky(parsed.sky)) {
                return false;
            }
        } else if (key == "fall") {
            std::array<int, 1> depth{};
            if (!read_ints(value, depth, 1) || core::at(depth, 0) < kFallDepthMin || core::at(depth, 0) > kFallDepthMax) {
                return false;
            }
            parsed.fall_depth = core::at(depth, 0);
        } else if (key == "sun") {
            if (!read_ints(value, parsed.sun, kStates) ||
                std::ranges::any_of(parsed.sun, [](int angle) { return angle < 0 || angle > kSunMax; })) {
                return false;
            }
            parsed.has_sun_ = true;
        } // (another key: a later version's - skipped)
    }
    *props = parsed;
    return true;
}

std::vector<std::pair<const char*, fields::Value>> structure_values(const Props& props)
{
    std::vector<std::pair<const char*, fields::Value>> out;
    if (tod_name(props.tod)) {
        fields::Value v;
        v.kind = fields::Kind::kName;
        v.checksum = fields::hash_of_name(tod_name(props.tod));
        out.emplace_back("tod_state", v);
    }
    if (props.time == 0 || props.time == 1) {
        out.emplace_back("tod_manager_active", int_field(props.time));
    }
    // (map4: our own keys - the engine reads no level structure's ambient_* / fog_*; on_level_up reads these)
    if (props.has_ambient_) {
        fields::Value v;
        v.kind = fields::Kind::kIntegers;
        for (const int channel : props.ambient) {
            v.list.push_back(static_cast<uint32_t>(get_channel(channel)));
        }
        out.emplace_back("ed_ambient", v);
    }
    if (props.has_fog_) {
        fields::Value v;
        v.kind = fields::Kind::kIntegers;
        for (int i = 0; i < 4; ++i) {
            v.list.push_back(static_cast<uint32_t>(get_channel(core::at(props.fog, i))));
        }
        v.list.push_back(static_cast<uint32_t>(span_to(core::at(props.fog, 4))));
        out.emplace_back("ed_fog", v);
    }
    if (valid_sky(props.sky)) {
        fields::Value v;
        v.kind = fields::Kind::kChars;
        v.text = props.sky;
        out.emplace_back("sky", v);
    }
    // (level flags: load_level's own keys - the stock levels write them as floats)
    if (props.fall_depth >= 0) {
        fields::Value v;
        v.kind = fields::Kind::kFloat;
        core::at(v.numbers, 0) = static_cast<float>(std::clamp(props.fall_depth, kFallDepthMin, kFallDepthMax));
        out.emplace_back("uber_frig_height", v);
    }
    if (props.has_sun_) {
        fields::Value v;
        v.kind = fields::Kind::kFloats;
        for (const int angle : props.sun) {
            v.floats.push_back(static_cast<float>(std::clamp(angle, 0, kSunMax)));
        }
        out.emplace_back("sun_theta", v);
    }
    return out;
}

bool get_override(const Props& edited, const Props& level, gfx::TodParams* params)
{
    const Props* ambient = nullptr; // (the edited level's, else the level's own)
    if (edited.has_ambient_) {
        ambient = &edited;
    } else if (level.has_ambient_) {
        ambient = &level;
    }
    if (ambient) {
        params->ambient_red_ = get_channel(core::at(ambient->ambient, 0));
        params->ambient_green_ = get_channel(core::at(ambient->ambient, 1));
        params->ambient_blue_ = get_channel(core::at(ambient->ambient, 2));
    }
    const Props* fog = nullptr;
    if (edited.has_fog_) {
        fog = &edited;
    } else if (level.has_fog_) {
        fog = &level;
    }
    if (!fog) {
        return false;
    }
    params->fog_red_ = get_channel(core::at(fog->fog, 0));
    params->fog_green_ = get_channel(core::at(fog->fog, 1));
    params->fog_blue_ = get_channel(core::at(fog->fog, 2));
    params->fog_alpha_ = get_channel(core::at(fog->fog, 3));
    params->fog_dist_ = span_to(core::at(fog->fog, 4));
    return true;
}

void read_override_keys(script::Struct* level, Props* out)
{
    static const uint32_t kAmbient = checksums::hash_from_text("ed_ambient");
    static const uint32_t kFog = checksums::hash_from_text("ed_fog");
    auto read = [&](uint32_t key, std::span<int> values, uint32_t count) {
        script::Array* array = nullptr;
        if (!level || !level->fetch_list(key, &array, 0) || !array ||
            array->kind_tag() != script::QbValueKind::kInteger || array->extent() != count) {
            return false;
        }
        for (uint32_t i = 0; i < count; ++i) {
            core::at(values, i) = static_cast<int32_t>(array->fetch_part(i));
        }
        return true;
    };
    out->has_ambient_ = read(kAmbient, out->ambient, 3);
    out->has_fog_ = read(kFog, out->fog, kFogValues);
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

// NOT ORIGINAL grouping - level_editor_level_props.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_level_props_state()).
struct LevelEditorLevelPropsState {
    Props props_{};
    // (map4) The level structure's ed_ambient / ed_fog (a bake's - or the edited copy's, written for its load).
    Props level_{};
    bool filter_set_{};
    std::vector<std::string> skies_;
    bool skies_made_{};
};

LevelEditorLevelPropsState& level_editor_level_props_state()
{
    static core::NoDestroy<LevelEditorLevelPropsState> state;
    return state.get();
}

bool get_filter(gfx::TodParams* params) { return get_override(level_editor_level_props_state().props_, level_editor_level_props_state().level_, params); }

void install_filter()
{
    if (!level_editor_level_props_state().filter_set_) {
        level_editor_level_props_state().filter_set_ = true;
        gfx::set_tod_params_filter(&get_filter);
    }
}

bool lighting_same(const Props& a, const Props& b)
{
    return a.has_ambient_ == b.has_ambient_ &&
           (!a.has_ambient_ || std::memcmp(a.ambient.data(), b.ambient.data(), sizeof(a.ambient)) == 0) && a.has_fog_ == b.has_fog_ &&
           (!a.has_fog_ || std::memcmp(a.fog.data(), b.fog.data(), sizeof(a.fog)) == 0);
}

// The level structure's values put in by export_to_struct, and what was there before.
struct Kept {
    uint32_t key;
    fields::Own before;
};
// NOT ORIGINAL grouping - the level properties written and kept (were globals). Made on first use, never destroyed
// (props_written()).
struct PropsWritten {
    script::Struct* written_{};
    std::vector<Kept> kept_;
};

PropsWritten& props_written()
{
    static core::NoDestroy<PropsWritten> state;
    return state.get();
}

// The level structure of the level loaded now (Levels.qb's Level_<x>: its load_script is the skate module's level).
script::Struct* current_level_structure()
{
    const modules::Skate* skate = modules::Skate::sole();
    const uint32_t load = skate ? skate->level() : 0;
    if (load == 0U) {
        return nullptr;
    }
    static const uint32_t kLoadScript = checksums::hash_from_text("load_script");
    for (script::SymbolTableEntry* entry = script::get_next_symbol(nullptr); entry;
         entry = script::get_next_symbol(entry)) {
        uint32_t value = 0;
        if (entry->type() == script::QbValueKind::kStruct && entry->value().structure() &&
            entry->value().structure()->read_hash_value(kLoadScript, &value, 0) && value == load) {
            return entry->value().structure();
        }
    }
    return nullptr;
}

void show_time_of_day(int tod)
{
    gfx::CtodManager* manager = gfx::tod_manager();
    if (manager && tod_name(tod)) {
        manager->set_tod_instant(checksums::hash_from_text(tod_name(tod)));
    }
}

} // namespace

const Props& at_hand() { return level_editor_level_props_state().props_; }

void assign_value(const Props& props)
{
    int tod = props.tod;
    if (tod != level_editor_level_props_state().props_.tod && tod < 0) {
        // (back to the level's own state)
        Props defaults;
        if (level_defaults(&defaults)) {
            tod = defaults.tod;
        }
    }
    const bool tod_changed = props.tod != level_editor_level_props_state().props_.tod;
    const bool fog_was = level_editor_level_props_state().props_.has_fog_ || level_editor_level_props_state().level_.has_fog_;
    const bool lighting = !lighting_same(props, level_editor_level_props_state().props_);
    level_editor_level_props_state().props_ = props;
    install_filter();
    if (tod_changed) {
        show_time_of_day(tod); // (its refresh sends the ambient / fog through the filter too)
    }
    if (lighting && !tod_changed) {
        reapply();
    }
    // (a fog the editor had forced on, taken off where the manager's fog is off: off again - the manager's own switch)
    gfx::CtodManager* manager = gfx::tod_manager();
    if (fog_was && !level_editor_level_props_state().props_.has_fog_ && !level_editor_level_props_state().level_.has_fog_ && manager && !manager->fog_active()) {
        manager->assign_fog_active(true);
        manager->set_fog_active(false);
    }
}

void on_level_up(bool network)
{
    Props level;
    if (!network) {
        read_override_keys(current_level_structure(), &level);
    }
    const bool changed = !lighting_same(level, level_editor_level_props_state().level_);
    level_editor_level_props_state().level_ = level;
    if (!changed) {
        return;
    }
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
    if (!manager || !manager->params_set() || manager->level_lights_reset()) {
        return;
    }
    install_filter();
    manager->apply_tod_params(manager->get_tod_params(-1));
}

bool level_defaults(Props* props)
{
    script::Struct* level = current_level_structure();
    if (!level) {
        return false;
    }
    Props level_props;
    const char* name = nullptr;
    if (level->fetch_text(checksums::hash_from_text("name"), &name, 0) && name) {
        level_props.name = name;
    }
    const char* sky = nullptr;
    if (level->fetch_text(checksums::hash_from_text("sky"), &sky, 0) && sky) {
        level_props.sky = sky;
    }
    uint32_t state = 0;
    level_props.tod = 1; // (load_level: afternoon when the level names none)
    if (level->read_hash_value(checksums::hash_from_text("tod_state"), &state, 0)) {
        for (int i = 0; i < kStates; ++i) {
            level_props.tod = state == checksums::hash_from_text(core::at(kTimeOfDayLabels, i)) ? i : level_props.tod;
        }
    }
    int value = 1;
    level_props.time =
        level->fetch_int_value(checksums::hash_from_text("tod_manager_active"), &value, 0) && value == 0 ? 0 : 1;
    // (level flags: the level's own, else load_level's defaults - uber_frig_default_height, no sun_theta)
    const fields::Own fall = fields::own_in(level, checksums::hash_from_text("uber_frig_height"), false);
    level_props.fall_depth = kDefaultFallDepth;
    if (fall.present && fall.value.kind == fields::Kind::kFloat) {
        level_props.fall_depth = static_cast<int>(core::at(fall.value.numbers, 0));
    } else if (fall.present && fall.value.kind == fields::Kind::kInteger) {
        level_props.fall_depth = fall.value.integer;
    }
    const fields::Own sun = fields::own_in(level, checksums::hash_from_text("sun_theta"), false);
    if (sun.present && sun.value.kind == fields::Kind::kFloats && sun.value.floats.size() == kStates) {
        level_props.has_sun_ = true;
        for (int i = 0; i < kStates; ++i) {
            core::at(level_props.sun, i) = static_cast<int>(core::at(sun.value.floats, i));
        }
    }
    // (map4) The ambient light and fog the level shows: the time-of-day manager's current state's - where the engine's
    // come from - with the level structure's ed_ keys (a bake's) over them.
    gfx::CtodManager* manager = gfx::tod_manager();
    if (manager && manager->params_set()) {
        gfx::TodParams now = manager->get_tod_params(-1);
        get_override(Props{}, level_editor_level_props_state().level_, &now);
        level_props.has_ambient_ = true;
        core::at(level_props.ambient, 0) = now.ambient_red_;
        core::at(level_props.ambient, 1) = now.ambient_green_;
        core::at(level_props.ambient, 2) = now.ambient_blue_;
        level_props.has_fog_ = true;
        std::array<int, kFogValues> fog = {{now.fog_red_, now.fog_green_, now.fog_blue_, now.fog_alpha_, now.fog_dist_}};
        std::memcpy(level_props.fog.data(), fog.data(), sizeof(fog));
    }
    *props = level_props;
    return true;
}

void forget_level()
{
    level_editor_level_props_state().props_ = Props{};
    level_editor_level_props_state().level_ = Props{}; // (before the new level's load sends its lighting: on_level_up reads its keys once it's up)
    level_editor_level_props_state().skies_made_ = false;
}

const std::vector<std::string>& skies()
{
    if (level_editor_level_props_state().skies_made_) {
        return level_editor_level_props_state().skies_;
    }
    level_editor_level_props_state().skies_made_ = true;
    level_editor_level_props_state().skies_.clear();
    static const uint32_t kSky = checksums::hash_from_text("sky");
    static const uint32_t kLoadScript = checksums::hash_from_text("load_script");
    for (script::SymbolTableEntry* entry = script::get_next_symbol(nullptr); entry;
         entry = script::get_next_symbol(entry)) {
        const char* sky = nullptr;
        uint32_t load = 0;
        // (a level structure: it names its load script - not some other structure with a sky=)
        if (entry->type() != script::QbValueKind::kStruct || (entry->value().structure() == nullptr) ||
            !entry->value().structure()->read_hash_value(kLoadScript, &load, 0) ||
            !entry->value().structure()->fetch_text(kSky, &sky, 0) || (sky == nullptr) || !valid_sky(sky)) {
            continue;
        }
        bool there = false;
        for (const std::string& known_sky : level_editor_level_props_state().skies_) {
            there = there || _stricmp(known_sky.c_str(), sky) == 0;
        }
        if (!there) {
            level_editor_level_props_state().skies_.emplace_back(sky);
        }
    }
    std::ranges::sort(level_editor_level_props_state().skies_,
                      [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
    return level_editor_level_props_state().skies_;
}

void export_to_struct(script::Struct* level, const Props& props)
{
    restore_structure();
    if (!level) {
        return;
    }
    const std::vector<std::pair<const char*, fields::Value>> values = structure_values(props);
    if (values.empty()) {
        return;
    }
    props_written().written_ = level;
    for (const auto& entry : values) {
        const uint32_t key = checksums::hash_from_text(entry.first);
        props_written().kept_.push_back({.key = key, .before = fields::own_in(level, key, false)});
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
    if (props_written().written_) {
        for (auto it = props_written().kept_.rbegin(); it != props_written().kept_.rend(); ++it) {
            fields::put_in(props_written().written_, it->key, false, it->before);
        }
    }
    props_written().written_ = nullptr;
    props_written().kept_.clear();
}

void on_edited_copy_up(const Props& props)
{
    static const uint32_t kClass = checksums::hash_from_text("Class");
    static const uint32_t kRestart = checksums::hash_from_text("Restart");
    script::Struct* node = (props.start != 0U) && level_nodes::node_manager_instance()
                               ? level_nodes::fetch_vertex(props.start, false)
                               : nullptr;
    uint32_t cls = 0;
    if (!node || !node->read_hash_value(kClass, &cls, 0) || cls != kRestart) {
        if (props.start != 0U) {
            get_log("level editor: the edited copy's start %08X isn't a restart of this level - not used", props.start);
        }
        return;
    }
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_name_key(checksums::Name{checksums::hash_from_text("node_name")}, props.start);
    script::launch_script(checksums::hash_from_text("ResetSkaters"), params, nullptr, false, "ResetSkaters");
    params->get_clear();
    script::delete_struct(params);
    get_log("level editor: the edited copy's start - the skater put at %08X", props.start);
}

} // namespace frontend::level_editor::level_props
