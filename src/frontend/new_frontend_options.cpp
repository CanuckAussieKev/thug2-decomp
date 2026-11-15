// NOT ORIGINAL - the new front end's Options screen's settings: a view of the settings model (new_frontend_options.h).

#include "frontend/new_frontend_options.h"

#include <cassert>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/config/settings_model.h"

namespace frontend::new_frontend::options {

namespace {

bool get_same(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

// The setting whose key is `key`: its index, -1 if none.
int find_key(const char* key)
{
    const std::vector<Spec>& specs = get_specs();
    for (size_t i = 0; i < specs.size(); ++i) {
        if (get_same(core::at(specs, i).key, key)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

const std::vector<Spec>& get_specs()
{
    // (the model's settings that have a tab, in its order)
    static const std::vector<Spec> kSpecs = [] {
        std::vector<Spec> specs;
        const std::vector<config::Setting>& all = config::settings();
        for (size_t i = 0; i < all.size(); ++i) {
            const config::Setting& setting = core::at(all, i);
            if (setting.group == config::Group::kNone) {
                continue;
            }
            const int fallback = config::match_choice(setting, setting.fallback);
            specs.push_back({.section = setting.section,
                             .key = setting.key,
                             .label = setting.label,
                             .help = setting.help,
                             .choices = setting.choices,
                             .fallback = fallback < 0 ? 0 : fallback,
                             .apply = setting.apply,
                             .setting = static_cast<int>(i)});
        }
        return specs;
    }();
    return kSpecs;
}

std::string check_tables()
{
    std::string problems = config::check_model();
    const auto add = [&problems](const std::string& line) { problems += line + "\n"; };
    for (const config::GroupInfo& group : config::groups()) {
        bool any = false;
        for (const Spec& spec : get_specs()) {
            any = any || core::at(config::settings(), spec.setting).group == group.group;
        }
        if (!any) {
            add(std::string("group ") + group.id + " has no rows");
        }
    }
    for (int choice = 1; choice < 4; ++choice) {
        for (const PresetValue& set : preset_values(choice)) {
            if (get_find("Graphics", set.key) < 0) {
                add(std::string("preset ") + std::to_string(choice) + ": " + set.key + " isn't offered by the screen");
            }
        }
    }
    return problems;
}

const std::vector<GroupSet>& bunches()
{
    // (resolved from the model once: a setting's tab and place are its declaration's)
    static const std::vector<GroupSet> kGroups = [] {
        std::vector<GroupSet> groups;
        const std::vector<Spec>& specs = get_specs();
        for (const config::GroupInfo& info : config::groups()) {
            GroupSet group{.id = info.id, .name = info.name, .rows = {}};
            for (size_t i = 0; i < specs.size(); ++i) {
                if (core::at(config::settings(), core::at(specs, i).setting).group == info.group) {
                    group.rows.push_back(static_cast<int>(i));
                }
            }
            groups.push_back(std::move(group));
        }
        assert(check_tables().empty() && "the settings model: see check_tables()");
        return groups;
    }();
    return kGroups;
}

std::vector<RowState> row_states(const config::Resolution& resolution)
{
    const std::vector<Spec>& specs = get_specs();
    std::vector<RowState> rows(specs.size());
    for (size_t i = 0; i < specs.size(); ++i) {
        const Spec& spec = core::at(specs, i);
        RowState& row = core::at(rows, i);
        row.possible.assign(spec.choices.size(), true);
        if (spec.apply == Apply::kPreset || std::cmp_greater_equal(spec.setting, resolution.items.size())) {
            continue;
        }
        const config::Resolved& item = core::at(resolution.items, spec.setting);
        const config::Setting& setting = core::at(config::settings(), spec.setting);
        row.in_effect = item.in_effect;
        row.forced = item.forced;
        row.reason = item.reason;
        if (!item.conflicts.empty()) {
            row.conflict = item.conflicts.front();
        }
        for (size_t choice = 0; choice < spec.choices.size() && choice < item.blocked.size(); ++choice) {
            core::at(row.possible, choice) = core::at(item.blocked, choice).empty();
        }
        // (the value in use, where it isn't the one asked and is in effect: forced, or a choice not possible here)
        if (item.in_effect && item.value != item.asked) {
            row.shown = config::match_choice(setting, item.value);
        }
    }
    return rows;
}

int preset_option()
{
    static const int kIndex = find_key("graphics_preset");
    return kIndex;
}

const std::vector<PresetValue>& preset_values(int choice) { return config::preset_values(choice); }

int preset_of(const std::string& text)
{
    const std::vector<Spec>& specs = get_specs();
    const int choices = static_cast<int>(core::at(specs, preset_option()).choices.size());
    for (int choice = 1; choice < choices; ++choice) {
        bool all = true;
        for (const PresetValue& set : preset_values(choice)) {
            const int found = find_key(set.key);
            if (found < 0) {
                all = false;
                break;
            }
            const Spec& spec = core::at(specs, found);
            std::string value;
            const bool present = ini_get(text, {.section = spec.section, .key = spec.key}, &value);
            if (choice_of(spec, value, present) != choice_of(spec, set.value, true)) {
                all = false;
                break;
            }
        }
        if (all) {
            return choice;
        }
    }
    return 0;
}

int get_find(const std::string& section, const std::string& key)
{
    const std::vector<Spec>& specs = get_specs();
    for (size_t i = 0; i < specs.size(); ++i) {
        if (get_same(core::at(specs, i).section, section) && get_same(core::at(specs, i).key, key)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int choice_of(const Spec& spec, const std::string& value, bool present)
{
    if (!present) {
        return spec.fallback;
    }
    // (an empty value is the game's default; a value between the choices: the fallback as well)
    const config::Setting& setting = core::at(config::settings(), spec.setting);
    std::string read = value;
    for (const config::Alias& alias : setting.aliases) {
        if (get_same(read, alias.from)) {
            read = alias.to;
        }
    }
    const int matched = config::match_choice(setting, read);
    return matched >= 0 ? matched : spec.fallback;
}

} // namespace frontend::new_frontend::options
