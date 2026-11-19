// NOT ORIGINAL - (round CS) a setting's name and value checked as the settings model takes them (settings_live.h:
// find_named, check_value) - pure: no files, no game (thug2_options_check runs them).

#include "engine/config/settings_live.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

#include "base/core/checked_index.h"
#include "engine/config/settings_model.h"

namespace config::live {

namespace {

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

std::string shown(const std::string& value) { return value.empty() ? "(empty)" : value; }

// A number written whole ("1.5", "-1", "2e1"): strtod reads all of it.
bool is_number(const std::string& text)
{
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(value);
}

} // namespace
int find_named(const std::string& name, std::string* why)
{
    std::string text = trim(name);
    std::string section;
    if (!text.empty() && text.front() == '[') {
        const size_t close = text.find(']');
        if (close != std::string::npos) {
            section = trim(text.substr(1, close - 1));
            text = trim(text.substr(close + 1));
        }
    } else if (const size_t slash = text.find('/'); slash != std::string::npos) {
        section = trim(text.substr(0, slash));
        text = trim(text.substr(slash + 1));
    }
    const int found = section.empty() ? find(text.c_str()) : find(section.c_str(), text.c_str());
    if (found < 0 && why != nullptr) {
        *why = "no setting " + trim(name) + " (settings lists them)";
    }
    return found;
}

bool check_value(const Setting& setting, const std::string& value, std::string* normal, std::string* why)
{
    const auto refuse = [why](const std::string& reason) {
        if (why != nullptr) {
            *why = reason;
        }
        return false;
    };
    if (setting.retired != nullptr) {
        return refuse(name_of(setting) + " is retired: " + setting.retired);
    }
    if (setting.apply == Apply::kPreset) {
        return refuse(name_of(setting) + " isn't a key of its own (set picks a preset's keys)");
    }
    std::string asked = trim(value);
    for (const Alias& alias : setting.aliases) {
        if (lower(asked) == lower(alias.from)) {
            asked = alias.to;
        }
    }
    switch (setting.kind) {
    case Kind::kSwitch: {
        const std::string word = lower(asked);
        if (word == "1" || word == "on" || word == "true" || word == "yes") {
            *normal = "1";
        } else if (word == "0" || word == "off" || word == "false" || word == "no") {
            *normal = "0";
        } else {
            return refuse(name_of(setting) + " is on / off (0 / 1), not " + shown(asked));
        }
        return true;
    }
    case Kind::kChoice: {
        const int choice = match_choice(setting, asked);
        if (choice >= 0) {
            *normal = core::at(setting.choices, choice).value;
            return true;
        }
        if (setting.open && !asked.empty()) {
            *normal = asked; // (tod's HH:MM, max_fps' own rate: read as written)
            return true;
        }
        std::string values;
        for (const Choice& each : setting.choices) {
            values += (values.empty() ? "" : " | ") + shown(each.value);
        }
        return refuse(name_of(setting) + " takes " + values + ", not " + shown(asked));
    }
    case Kind::kNumber:
        if (!is_number(asked)) {
            return refuse(name_of(setting) + " is a number, not " + shown(asked));
        }
        *normal = asked;
        return true;
    case Kind::kText:
        *normal = asked;
        return true;
    }
    return refuse(name_of(setting) + ": an unknown kind");
}

} // namespace config::live
