// NOT ORIGINAL - (round CS) a setting changed while the game runs (settings_live.h).

#include "engine/config/settings_live.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/config/ini_text.h"
#include "engine/config/settings_state.h"
#include "engine/config/settings_text.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"

namespace config::live {

namespace {

constexpr int kOwnerCount = static_cast<int>(Owner::kSelf) + 1;

struct LiveState {
    std::array<Applier, kOwnerCount> appliers{};
    Reloader reloader = nullptr;
};

LiveState& live_state()
{
    static core::NoDestroy<LiveState> state;
    return state.get();
}

std::string lower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string name_of(const Setting& setting) { return std::string(setting.section) + "/" + setting.key; }

// The file written by the one writer: a new file moved over the old one (the Options screen's way).
bool write_file(const std::string& path, const std::string& text)
{
    const std::string temporary = path + ".new";
    fs::OwnedStream file = fs::open_stream(temporary.c_str(), "wb", fs::StreamSharing::kSecure);
    if (!file) {
        return false;
    }
    bool ok = fs::disk().stream_write(file.get(), text.data(), 1, text.size()) == text.size();
    ok = fs::disk().stream_flush(file.get()) == 0 && ok;
    file.reset(); // (closed before it's moved)
    if (!ok || !fs::disk().replace(temporary.c_str(), path.c_str(), true)) {
        fs::disk().remove(temporary.c_str());
        return false;
    }
    return true;
}

const char* outcome_words(Outcome outcome)
{
    switch (outcome) {
    case Outcome::kNow:
        return "put to use";
    case Outcome::kReload:
        return "the level reloads with it";
    case Outcome::kNextStart:
        return "at the next start";
    }
    return "";
}

// The value as it reads ("(empty)" for nothing).
std::string shown(const std::string& value) { return value.empty() ? "(empty)" : value; }

// A level reload asked for (one for several changes).
Result reload_level(const Setting& setting)
{
    std::string why;
    const Reloader reload = live_state().reloader;
    if (reload == nullptr) {
        why = "no level reload here";
    } else if (reload(&why)) {
        return {.ok = true, .outcome = Outcome::kReload, .text = ""};
    }
    get_log("settings: %s - written; %s - at the next level load", name_of(setting).c_str(), why.c_str());
    return {.ok = true, .outcome = Outcome::kReload, .text = "at the next level load (" + why + ")"};
}

} // namespace

void set_applier(Owner owner, Applier apply) { core::at(live_state().appliers, static_cast<size_t>(owner)) = apply; }

bool has_applier(Owner owner) { return core::at(live_state().appliers, static_cast<size_t>(owner)) != nullptr; }

std::string missing_appliers()
{
    std::string missing;
    for (const Setting& setting : settings()) {
        if (setting.apply == Apply::kLive && !has_applier(setting.owner)) {
            missing += (missing.empty() ? "" : ", ") + name_of(setting);
        }
    }
    return missing;
}

void set_level_reloader(Reloader reload) { live_state().reloader = reload; }

std::string ini_path() { return game_folder_path(build_info::kFileName); }

bool write(const std::vector<Write>& writes)
{
    const std::string path = ini_path();
    std::string text;
    if (!fs::read_text(path, &text)) {
        text.clear(); // (no file yet: one is made)
    }
    std::string changed = text;
    for (const Write& each : writes) {
        if (each.setting < 0 || std::cmp_greater_equal(each.setting, settings().size())) {
            continue;
        }
        const Setting& setting = core::at(settings(), each.setting);
        const IniKey at{.section = setting.section, .key = setting.key};
        changed = each.remove ? ini_remove(changed, at) : ini_set(changed, at, each.value);
    }
    if (changed == text) {
        return true;
    }
    if (!write_file(path, changed)) {
        get_log("settings: couldn't write %s", path.c_str());
        return false;
    }
    build_info::forget_cached_reads(); // (the settings reads are cached: platform/platform_config.h)
    for (const Write& each : writes) {
        if (each.setting >= 0 && std::cmp_less(each.setting, settings().size())) {
            const Setting& setting = core::at(settings(), each.setting);
            get_log("settings: [%s] %s%s%s %s %s", setting.section, setting.key, each.remove ? "" : "=",
                    each.remove ? "" : each.value.c_str(), each.remove ? "removed from" : "written to", path.c_str());
        }
    }
    return true;
}

Result apply(int index)
{
    if (index < 0 || std::cmp_greater_equal(index, settings().size())) {
        return {.ok = false, .outcome = Outcome::kNow, .text = "no such setting"};
    }
    const Setting& setting = core::at(settings(), index);
    refresh_running();
    switch (setting.apply) {
    case Apply::kRelaunch:
        return {.ok = true,
                .outcome = Outcome::kNextStart,
                .text = std::string("at the next start - ") + (setting.restart != nullptr ? setting.restart : "")};
    case Apply::kReload:
        return reload_level(setting);
    case Apply::kPreset:
        return {.ok = true, .outcome = Outcome::kNow, .text = ""};
    case Apply::kLive:
        break;
    }
    const Applier owner = core::at(live_state().appliers, static_cast<size_t>(setting.owner));
    if (owner == nullptr) {
        get_log("settings: %s is live but nothing here puts it to use - written only", name_of(setting).c_str());
        return {.ok = false, .outcome = Outcome::kNow, .text = "written, but nothing puts it to use here"};
    }
    if (!owner(setting)) {
        return {.ok = true, .outcome = Outcome::kNow, .text = "written - nothing to change here now"};
    }
    return {.ok = true, .outcome = Outcome::kNow, .text = ""};
}

Result set_all(const std::vector<std::pair<int, std::string>>& changes)
{
    std::vector<Write> writes;
    writes.reserve(changes.size());
    for (const auto& [index, value] : changes) {
        writes.push_back({.setting = index, .value = value, .remove = false});
    }
    if (!write(writes)) {
        return {.ok = false, .outcome = Outcome::kNow, .text = "couldn't write " + ini_path()};
    }
    Result all{.ok = true, .outcome = Outcome::kNow, .text = ""};
    bool reloaded = false;
    for (const auto& [index, value] : changes) {
        const Setting& setting = core::at(settings(), index);
        if (setting.apply == Apply::kReload && reloaded) {
            continue; // (one load for them all)
        }
        Result one = apply(index);
        reloaded = reloaded || one.outcome == Outcome::kReload;
        all.ok = all.ok && one.ok;
        all.outcome = std::max(all.outcome, one.outcome);
        if (!one.text.empty()) {
            all.text += (all.text.empty() ? "" : "; ") + name_of(setting) + ": " + one.text;
        }
    }
    return all;
}

Result set(const std::string& name, const std::string& value)
{
    std::string why;
    const int index = find_named(name, &why);
    if (index < 0) {
        return {.ok = false, .outcome = Outcome::kNow, .text = why};
    }
    const Setting& setting = core::at(settings(), index);
    if (setting.apply == Apply::kPreset) {
        const int choice = match_choice(setting, trim(value));
        if (choice < 0) {
            std::string values;
            for (const Choice& each : setting.choices) {
                values += (values.empty() ? "" : " | ") + shown(each.value);
            }
            return {.ok = false, .outcome = Outcome::kNow, .text = name_of(setting) + " takes " + values};
        }
        std::vector<std::pair<int, std::string>> changes;
        for (const PresetValue& each : preset_values(choice)) {
            const int found = find(setting.section, each.key);
            if (found >= 0) {
                changes.emplace_back(found, each.value);
            }
        }
        Result result = set_all(changes);
        result.text = name_of(setting) + " " + shown(value) + " - " + std::to_string(changes.size()) + " settings " +
                      outcome_words(result.outcome) + (result.text.empty() ? "" : " (" + result.text + ")");
        return result;
    }
    std::string normal;
    if (!check_value(setting, value, &normal, &why)) {
        return {.ok = false, .outcome = Outcome::kNow, .text = why};
    }
    if (!write({{.setting = index, .value = normal, .remove = false}})) {
        return {.ok = false, .outcome = Outcome::kNow, .text = "couldn't write " + ini_path()};
    }
    Result result = apply(index);
    const std::string said = result.text;
    result.text = "[" + std::string(setting.section) + "] " + setting.key + "=" + normal + " - " +
                  (said.empty() ? outcome_words(result.outcome) : said);
    get_log("settings: %s", result.text.c_str());
    return result;
}

Result toggle(const std::string& name)
{
    std::string why;
    const int index = find_named(name, &why);
    if (index < 0) {
        return {.ok = false, .outcome = Outcome::kNow, .text = why};
    }
    const Setting& setting = core::at(settings(), index);
    if (setting.kind != Kind::kSwitch) {
        return {.ok = false, .outcome = Outcome::kNow, .text = name_of(setting) + " isn't on / off (set it)"};
    }
    const Resolution now = current();
    return set(name_of(setting), truthy(now.at(setting.section, setting.key).asked) ? "0" : "1");
}

Result reset(const std::string& name)
{
    std::string why;
    const int index = find_named(name, &why);
    if (index < 0) {
        return {.ok = false, .outcome = Outcome::kNow, .text = why};
    }
    const Setting& setting = core::at(settings(), index);
    if (setting.apply == Apply::kPreset) {
        return {.ok = false, .outcome = Outcome::kNow, .text = name_of(setting) + " isn't a key of its own"};
    }
    if (!write({{.setting = index, .value = {}, .remove = true}})) {
        return {.ok = false, .outcome = Outcome::kNow, .text = "couldn't write " + ini_path()};
    }
    Result result = apply(index);
    const std::string said = result.text;
    result.text = "[" + std::string(setting.section) + "] " + setting.key + " back at its default (" +
                  shown(setting.fallback) + ") - " + (said.empty() ? outcome_words(result.outcome) : said);
    get_log("settings: %s", result.text.c_str());
    return result;
}

std::string when_text(const Setting& setting)
{
    switch (setting.apply) {
    case Apply::kLive:
        return "changes at once";
    case Apply::kReload:
        return "reloads the level (offline; the run starts again)";
    case Apply::kRelaunch:
        return std::string("next start: ") + (setting.restart != nullptr ? setting.restart : "");
    case Apply::kPreset:
        return "sets the preset's settings";
    }
    return "";
}

std::string get(const std::string& name)
{
    std::string why;
    const int index = find_named(name, &why);
    if (index < 0) {
        return why;
    }
    const Setting& setting = core::at(settings(), index);
    const Resolution now = current();
    const Resolved& asked = now.at(setting.section, setting.key);
    const Resolved& runs = running().at(setting.section, setting.key);
    std::ostringstream out;
    out << "[" << setting.section << "] " << setting.key << " = " << shown(asked.asked)
        << (asked.set ? "" : " (default)") << "; default " << shown(setting.fallback) << "\n";
    if (setting.retired != nullptr) {
        out << "  retired: " << setting.retired << "\n";
        return out.str();
    }
    out << "  in effect: " << (runs.in_effect ? shown(runs.value) : std::string("no"));
    if (!runs.reason.empty()) {
        out << " (" << runs.reason << ")";
    }
    out << "\n";
    for (const std::string& conflict : runs.conflicts) {
        out << "  conflict: " << conflict << "\n";
    }
    for (const std::string& limit : runs.limits) {
        out << "  limited: " << limit << "\n";
    }
    if (setting.apply == Apply::kRelaunch && runs.asked != asked.asked) {
        out << "  running " << shown(runs.asked) << " until the next start\n";
    }
    out << "  a change: " << when_text(setting);
    if (!setting.choices.empty() && setting.kind != Kind::kSwitch) {
        out << "\n  values:";
        for (const Choice& each : setting.choices) {
            out << " " << shown(each.value);
        }
        out << (setting.open || setting.kind == Kind::kNumber ? " (or others)" : "");
    }
    return out.str();
}

std::string list(const std::string& filter)
{
    const std::string wanted = lower(trim(filter));
    const Resolution asked = current();
    const Resolution& runs = running();
    std::ostringstream out;
    int shown_count = 0;
    for (size_t i = 0; i < settings().size(); ++i) {
        const Setting& setting = core::at(settings(), i);
        if (setting.retired != nullptr || setting.apply == Apply::kPreset) {
            continue;
        }
        if (!wanted.empty() && lower(name_of(setting)).find(wanted) == std::string::npos) {
            continue;
        }
        const Resolved& now = core::at(asked.items, i);
        const Resolved& running_now = core::at(runs.items, i);
        out << (now.set ? "* " : "  ") << setting.section << "/" << setting.key << " = " << shown(now.asked);
        if (!running_now.in_effect) {
            out << "  (not in effect)";
        } else if (running_now.value != now.asked) {
            out << "  (runs " << shown(running_now.value) << ")";
        }
        out << (setting.apply == Apply::kRelaunch ? "  [start]" : setting.apply == Apply::kReload ? "  [reload]" : "")
            << "\n";
        ++shown_count;
    }
    out << shown_count << " settings (* set in the ini; [reload] reloads the level, [start] waits for the next start)";
    return out.str();
}

std::string restart_list()
{
    std::ostringstream out;
    int count = 0;
    for (const Setting& setting : settings()) {
        if (setting.apply == Apply::kRelaunch && setting.retired == nullptr) {
            out << setting.section << "/" << setting.key << " - " << (setting.restart != nullptr ? setting.restart : "")
                << "\n";
            ++count;
        }
    }
    out << count << " settings wait for the next start";
    return out.str();
}

} // namespace config::live
