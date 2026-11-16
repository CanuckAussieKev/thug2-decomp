// NOT ORIGINAL - the settings model's resolver and its checks (settings_model.h): the ini's values, the machine's
// capabilities and the level's facts in, each setting's effective value, whether it is in effect and why not out.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/config/settings_model.h"
#include "engine/config/settings_text.h"

namespace config {

namespace {

namespace t = config::text;

bool same_text(const char* a, const char* b)
{
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    return std::strlen(a) == std::strlen(b) && _stricmp(a, b) == 0;
}

// A number's value (false if the text isn't one).
bool number_of(const std::string& text, double* out)
{
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    *out = std::strtod(text.c_str(), &end);
    return end != nullptr && *end == '\0' && end != text.c_str();
}

// Two values the same: numbers as numbers ("1.50" is "1.5"), names in any case.
bool same_value(const std::string& a, const char* b)
{
    double x = 0.0;
    double y = 0.0;
    if (number_of(a, &x) && number_of(b, &y)) {
        return x == y;
    }
    return same_text(a.c_str(), b);
}

std::string trimmed(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string lower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

// GetPrivateProfileInt's reading: an optional sign and the digits after it (none: 0).
long leading_int(const std::string& text)
{
    constexpr int kDecimal = 10;
    return std::strtol(text.c_str(), nullptr, kDecimal);
}

const char* cap_reason(Cap cap)
{
    switch (cap) {
    case Cap::kVulkan:
        return t::kNeedsVulkan;
    case Cap::kRayQueries:
        return t::kNeedsRayGpu;
    case Cap::k64Bit:
        return t::kNeeds64Bit;
    case Cap::kNvidia:
        return t::kNeedsNvidia;
    case Cap::kRtx:
        return t::kNeedsRtx;
    case Cap::kRtx40:
        return t::kNeedsRtx40;
    case Cap::kRtx50:
        return t::kNeedsRtx50;
    case Cap::kSurfaceMaps:
        return t::kNeedsSurfaceMaps;
    case Cap::kMaterialLibrary:
        return t::kNeedsMaterials;
    case Cap::kHeightMaps:
        return t::kNeedsHeightMaps;
    case Cap::kReliefMaps:
        return t::kNeedsReliefMaps;
    case Cap::kLevelOutdoor:
        return t::kLevelIndoors;
    case Cap::kLevelLightData:
        return t::kLevelLightNotBuilt;
    case Cap::kLevelLightCurrent:
        return t::kLevelLightOutOfDate;
    case Cap::kLevelSunInBake:
        return t::kLevelNoSunlight;
    case Cap::kNetworkGame:
        return t::kNeedsNetworkGame;
    case Cap::kLevelWithoutFog:
    default:
        return t::kLevelHasFog;
    }
}

bool has_cap(const Inputs& in, Cap cap)
{
    const Capabilities& caps = in.caps;
    constexpr int kRtx40 = 40;
    constexpr int kRtx50 = 50;
    switch (cap) {
    case Cap::kVulkan:
        return caps.vulkan;
    case Cap::kRayQueries:
        return caps.ray_queries;
    case Cap::k64Bit:
        return caps.x64;
    case Cap::kNvidia:
        return caps.vendor == kVendorNvidia;
    case Cap::kRtx:
        return caps.vendor == kVendorNvidia && caps.rtx > 0;
    case Cap::kRtx40:
        return caps.vendor == kVendorNvidia && caps.rtx >= kRtx40;
    case Cap::kRtx50:
        return caps.vendor == kVendorNvidia && caps.rtx >= kRtx50;
    case Cap::kSurfaceMaps:
        return caps.surface_maps;
    case Cap::kMaterialLibrary:
        return caps.material_library;
    case Cap::kHeightMaps:
        return caps.height_maps;
    case Cap::kReliefMaps:
        return caps.relief_maps;
    case Cap::kLevelOutdoor:
        return in.level.outdoor;
    case Cap::kLevelLightData:
        return in.level.light != LevelLight::kMissing;
    case Cap::kLevelLightCurrent:
        return in.level.light != LevelLight::kStale;
    case Cap::kLevelSunInBake:
        return in.level.light != LevelLight::kNoSun;
    case Cap::kNetworkGame:
        return in.level.network;
    case Cap::kLevelWithoutFog:
    default:
        return !in.level.known || !in.level.fog;
    }
}

const Resolved& empty_resolved()
{
    static const Resolved kEmpty{.asked = {}, .set = false, .value = {}, .in_effect = false};
    return kEmpty;
}

// One resolution: each setting worked out the first time something asks for it (its rules ask for others).
class Resolver final : public Reader {
public:
    explicit Resolver(const Inputs& inputs) : in_(inputs)
    {
        const std::vector<Setting>& all = settings();
        out_.items.resize(all.size());
        state_.assign(all.size(), State::kNot);
        asked_.resize(all.size());
        wanted_.resize(all.size());
        for (size_t i = 0; i < all.size(); ++i) {
            read_asked(i);
        }
    }

    Resolution run()
    {
        for (size_t i = 0; i < out_.items.size(); ++i) {
            resolve_one(i);
        }
        const std::vector<Conflict>& list = conflicts();
        for (size_t i = 0; i < list.size(); ++i) {
            const Conflict& conflict = core::at(list, i);
            if (!holds(conflict.when)) {
                continue;
            }
            out_.active.push_back(static_cast<int>(i));
            for (const char* key : conflict.keys) {
                const int found = find(key);
                if (found >= 0) {
                    core::at(out_.items, found).conflicts.emplace_back(conflict.reason);
                }
            }
        }
        const std::vector<Limit>& limited = limits();
        for (size_t i = 0; i < limited.size(); ++i) {
            const Limit& limit = core::at(limited, i);
            if (!holds(limit.when)) {
                continue;
            }
            out_.limited.push_back(static_cast<int>(i));
            for (const char* key : limit.keys) {
                const int found = find(key);
                if (found >= 0 && core::at(out_.items, found).in_effect) {
                    core::at(out_.items, found).limits.emplace_back(limit.reason);
                }
            }
        }
        return std::move(out_);
    }

    [[nodiscard]] std::string value(const char* key) const override
    {
        const int found = find(key);
        return found < 0 ? std::string() : resolved(static_cast<size_t>(found)).value;
    }
    [[nodiscard]] bool in_effect(const char* key) const override
    {
        const int found = find(key);
        return found >= 0 && resolved(static_cast<size_t>(found)).in_effect;
    }
    [[nodiscard]] const Capabilities& caps() const override { return in_.caps; }

private:
    enum class State : uint8_t { kNot, kWorking, kDone };

    // The value asked: the ini's (empty: the default), as the setting's kind reads it.
    void read_asked(size_t index)
    {
        const Setting& setting = core::at(settings(), index);
        Resolved& item = core::at(out_.items, index);
        std::string asked = setting.fallback;
        const auto found = in_.ini.find(ini_slot(setting.section, setting.key));
        if (found != in_.ini.end() && !trimmed(found->second).empty()) {
            item.set = true;
            asked = trimmed(found->second);
        }
        for (const Alias& alias : setting.aliases) {
            if (same_text(asked.c_str(), alias.from)) {
                asked = alias.to;
            }
        }
        switch (setting.kind) {
        case Kind::kSwitch:
            asked = leading_int(asked) != 0 ? "1" : "0";
            break;
        case Kind::kChoice: {
            const int choice = match_choice(setting, asked);
            if (choice >= 0) {
                asked = core::at(setting.choices, choice).value;
            } else if (!setting.open) {
                item.reason = t::kNotAChoice;
                asked = setting.fallback;
            }
            break;
        }
        case Kind::kNumber: {
            double number = 0.0;
            if (!number_of(asked, &number)) {
                if (item.set) {
                    item.reason = t::kNotAChoice;
                }
                asked = setting.fallback;
            }
            const int choice = match_choice(setting, asked);
            if (choice >= 0) {
                asked = core::at(setting.choices, choice).value;
            }
            break;
        }
        case Kind::kText:
        default:
            break;
        }
        item.asked = asked;
        core::at(asked_, index) = std::move(asked);
    }

    [[nodiscard]] const Resolved& resolved(size_t index) const
    {
        resolve_one(index);
        return core::at(out_.items, index);
    }

    void resolve_one(size_t index) const
    {
        if (core::at(state_, index) != State::kNot) {
            return; // (done - or a rule reading its own setting back: the value so far; check_model rejects such cycles)
        }
        core::at(state_, index) = State::kWorking;
        const Setting& setting = core::at(settings(), index);
        Resolved& item = core::at(out_.items, index);
        std::string value = core::at(asked_, index);
        // forces (the first that holds)
        for (const Force& force : forces()) {
            if (same_text(force.key, setting.key) && holds(force.when)) {
                value = force.value;
                item.forced = true;
                item.reason = force.reason;
                break;
            }
        }
        // the choices' needs: the choice used instead while one fails (a few steps at most)
        item.blocked.assign(setting.choices.size(), std::string());
        for (size_t i = 0; i < setting.choices.size(); ++i) {
            const Choice& choice = core::at(setting.choices, i);
            for (const Need& need : choice.needs) {
                if (!holds(need.cond)) {
                    core::at(item.blocked, i) = reason_for(need);
                    break;
                }
            }
        }
        for (size_t steps = 0; steps < setting.choices.size(); ++steps) {
            const int choice = match_choice(setting, value);
            if (choice < 0 || core::at(item.blocked, choice).empty()) {
                break;
            }
            item.reason = core::at(item.blocked, choice);
            const Choice& blocked = core::at(setting.choices, choice);
            value = blocked.instead != nullptr ? blocked.instead : setting.fallback;
        }
        core::at(wanted_, index) = value;
        // the setting's own needs
        item.in_effect = true;
        if (setting.retired != nullptr) {
            item.in_effect = false;
            item.reason = setting.retired;
        } else {
            for (const Need& need : setting.needs) {
                if (!holds(need.cond)) {
                    item.in_effect = false;
                    item.reason = root_reason(need);
                    break;
                }
            }
        }
        if (!item.in_effect && setting.off != nullptr) {
            value = setting.off;
        }
        item.value = std::move(value);
        item.changed = !item.in_effect || !same_value(item.value, core::at(asked_, index).c_str());
        core::at(state_, index) = State::kDone;
    }

    // The words for a need that fails: its own, or made from its condition.
    [[nodiscard]] static std::string reason_for(const Need& need)
    {
        if (need.reason != nullptr) {
            return need.reason;
        }
        const Cond& cond = need.cond;
        if (cond.op == Cond::Op::kHas) {
            return cap_reason(cond.cap);
        }
        if (cond.op == Cond::Op::kTextureSet) {
            return t::kNotInstalled;
        }
        // (one setting - is, or is not: "Needs " and its name)
        const Cond& one = cond.op == Cond::Op::kNot && cond.parts.size() == 1 ? cond.parts.front() : cond;
        const int found = one.key != nullptr ? find(one.key) : -1;
        if (found >= 0) {
            const Setting& other = core::at(settings(), found);
            return std::string(t::kNeeds) + (other.label != nullptr ? other.label : other.key);
        }
        return t::kNeeds;
    }

    // A need on one setting that fails because that setting isn't what it was asked to be - out of effect, or a choice
    // this machine can't run standing in for it - while what was asked would meet the need: that setting's reason (the
    // root - "Needs a ray tracing GPU" rather than "Needs Shadows").
    [[nodiscard]] std::string root_reason(const Need& need) const
    {
        const Cond& cond = need.cond;
        if (cond.op == Cond::Op::kIs && cond.key != nullptr) {
            const int found = find(cond.key);
            if (found >= 0) {
                const Resolved& other = resolved(static_cast<size_t>(found));
                const auto meets = [&cond](const std::string& value) {
                    return std::ranges::any_of(cond.values, [&](const char* each) { return same_value(value, each); });
                };
                const bool asked_meets = meets(core::at(asked_, found)) || meets(core::at(wanted_, found));
                if (asked_meets && !meets(other.value) && !other.reason.empty()) {
                    return other.reason;
                }
            }
        }
        return reason_for(need);
    }

    [[nodiscard]] bool holds(const Cond& cond) const
    {
        switch (cond.op) {
        case Cond::Op::kAlways:
            return true;
        case Cond::Op::kIs: {
            const std::string now = value(cond.key);
            return std::ranges::any_of(cond.values, [&](const char* each) { return same_value(now, each); });
        }
        case Cond::Op::kAskedIs: {
            const int found = find(cond.key);
            const std::string& asked = found >= 0 ? core::at(asked_, found) : std::string();
            return std::ranges::any_of(cond.values, [&](const char* each) { return same_value(asked, each); });
        }
        case Cond::Op::kAll:
            return std::ranges::all_of(cond.parts, [this](const Cond& part) { return holds(part); });
        case Cond::Op::kAny:
            return std::ranges::any_of(cond.parts, [this](const Cond& part) { return holds(part); });
        case Cond::Op::kNot:
            return cond.parts.empty() || !holds(cond.parts.front());
        case Cond::Op::kHas:
            return has_cap(in_, cond.cap);
        case Cond::Op::kTextureSet:
            return same_text(cond.key, "original") ||
                   std::ranges::any_of(in_.caps.texture_sets, [&](const std::string& set) { return same_text(set.c_str(), cond.key); });
        case Cond::Op::kTest:
            return cond.test != nullptr && cond.test(*this);
        default:
            return false;
        }
    }

    const Inputs& in_;
    // (worked out as they are asked for: a rule's reading resolves the setting it reads)
    mutable Resolution out_;
    mutable std::vector<State> state_;
    std::vector<std::string> asked_;          // the value asked, as its kind reads it
    mutable std::vector<std::string> wanted_; // after the forces and the choices' needs, before its own needs
};

// --- check_model ---

// The settings a condition reads the effective values of (not the values as asked: they can't make a cycle).
void keys_of(const Cond& cond, std::vector<const char*>* out)
{
    switch (cond.op) {
    case Cond::Op::kIs:
        out->push_back(cond.key);
        break;
    case Cond::Op::kTest:
        out->insert(out->end(), cond.values.begin(), cond.values.end());
        break;
    case Cond::Op::kAll:
    case Cond::Op::kAny:
    case Cond::Op::kNot:
        for (const Cond& part : cond.parts) {
            keys_of(part, out);
        }
        break;
    default:
        break;
    }
}

void check_cond(const Cond& cond, const std::string& where, std::string* problems)
{
    const auto add = [&](const std::string& line) { *problems += where + ": " + line + "\n"; };
    switch (cond.op) {
    case Cond::Op::kIs:
    case Cond::Op::kAskedIs: {
        const int found = cond.key != nullptr ? find(cond.key) : -1;
        if (found < 0) {
            add(std::string(cond.key != nullptr ? cond.key : "(null)") + " is no setting");
            break;
        }
        const Setting& other = core::at(settings(), found);
        if (cond.values.empty()) {
            add(std::string("no values for ") + cond.key);
        }
        for (const char* each : cond.values) {
            const bool fits = other.kind == Kind::kText || other.open || other.kind == Kind::kNumber ||
                              match_choice(other, each) >= 0 || (other.off != nullptr && same_text(other.off, each));
            if (!fits) {
                add(std::string(each) + " is no value of " + cond.key);
            }
        }
        if (cond.op == Cond::Op::kIs && !other.needs.empty() && other.off == nullptr) {
            add(std::string(cond.key) + " has needs of its own but no off value (a rule reads it)");
        }
        int same = 0;
        for (const Setting& each : settings()) {
            same += same_text(each.key, cond.key) ? 1 : 0;
        }
        if (same != 1) {
            add(std::string(cond.key) + " names " + std::to_string(same) + " settings (a rule's key must be one)");
        }
        break;
    }
    case Cond::Op::kAll:
    case Cond::Op::kAny:
        for (const Cond& part : cond.parts) {
            check_cond(part, where, problems);
        }
        break;
    case Cond::Op::kNot:
        if (cond.parts.size() != 1) {
            add("a not without one part");
        }
        for (const Cond& part : cond.parts) {
            check_cond(part, where, problems);
        }
        break;
    case Cond::Op::kTest: {
        if (cond.test == nullptr) {
            add("a computed test without its function");
        }
        for (const char* key : cond.values) {
            if (find(key) < 0) {
                add(std::string(key) + " is no setting");
            }
        }
        break;
    }
    default:
        break;
    }
}

// A need whose words can be made from its condition (one setting, or one capability).
bool reason_made(const Need& need)
{
    const Cond& cond = need.cond;
    const bool one_setting = cond.op == Cond::Op::kIs ||
                             (cond.op == Cond::Op::kNot && cond.parts.size() == 1 && cond.parts.front().op == Cond::Op::kIs);
    return need.reason != nullptr || one_setting || cond.op == Cond::Op::kHas || cond.op == Cond::Op::kTextureSet;
}

} // namespace

// --- conditions ---

Cond always() { return {}; }
Cond is(const char* key, std::vector<const char*> values) { return {.op = Cond::Op::kIs, .key = key, .values = std::move(values)}; }
Cond is_not(const char* key, std::vector<const char*> values) { return negate(is(key, std::move(values))); }
Cond on(const char* key) { return is(key, {"1"}); }
Cond asked_is(const char* key, std::vector<const char*> values)
{
    return {.op = Cond::Op::kAskedIs, .key = key, .values = std::move(values)};
}
Cond all(std::vector<Cond> parts) { return {.op = Cond::Op::kAll, .parts = std::move(parts)}; }
Cond any(std::vector<Cond> parts) { return {.op = Cond::Op::kAny, .parts = std::move(parts)}; }
Cond negate(Cond part)
{
    Cond made{.op = Cond::Op::kNot};
    made.parts.push_back(std::move(part));
    return made;
}
Cond has(Cap cap) { return {.op = Cond::Op::kHas, .cap = cap}; }
Cond installed(const char* texture_set) { return {.op = Cond::Op::kTextureSet, .key = texture_set}; }
Cond computed(bool (*test)(const Reader& reader), std::vector<const char*> keys)
{
    return {.op = Cond::Op::kTest, .values = std::move(keys), .test = test};
}

// --- lookups ---

int find(const char* key)
{
    const std::vector<Setting>& all = settings();
    for (size_t i = 0; i < all.size(); ++i) {
        if (same_text(core::at(all, i).key, key)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int find(const char* section, const char* key)
{
    const std::vector<Setting>& all = settings();
    for (size_t i = 0; i < all.size(); ++i) {
        if (same_text(core::at(all, i).section, section) && same_text(core::at(all, i).key, key)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int match_choice(const Setting& setting, const std::string& value)
{
    const std::string wanted = trimmed(value);
    for (size_t i = 0; i < setting.choices.size(); ++i) {
        if (same_value(wanted, core::at(setting.choices, i).value)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool truthy(const std::string& value)
{
    return !value.empty() && value != "0" && !same_text(value.c_str(), "off");
}

std::string ini_slot(const char* section, const char* key) { return lower(section) + "/" + lower(key); }

int rtx_series(const std::string& device_name)
{
    const std::string name = lower(device_name);
    const size_t at = name.find("rtx");
    if (at == std::string::npos) {
        return 0;
    }
    size_t digits = at + 3;
    while (digits < name.size() && (core::at(name, digits) == ' ' || core::at(name, digits) == '-')) {
        ++digits;
    }
    size_t end = digits;
    while (end < name.size() && std::isdigit(static_cast<unsigned char>(core::at(name, end))) != 0) {
        ++end;
    }
    constexpr int kWorkstation = 30; // (RTX A-series and other workstation cards without a model number: DLSS, no DLSS-G)
    constexpr int kFirstSeries = 20; // (the RTX 20s: the first with DLSS)
    constexpr int kTens = 10;
    constexpr size_t kModelDigits = 4; // (a GeForce model: 5070, 4090 - its first two digits the series)
    if (end - digits == kModelDigits) {
        const int series = ((core::at(name, digits) - '0') * kTens) + (core::at(name, digits + 1) - '0');
        return series >= kFirstSeries ? series : kWorkstation;
    }
    return kWorkstation;
}

// --- the resolution ---

const Resolved& Resolution::at(const char* key) const
{
    const int found = find(key);
    return found < 0 || std::cmp_greater_equal(found, items.size()) ? empty_resolved() : core::at(items, found);
}

const Resolved& Resolution::at(const char* section, const char* key) const
{
    const int found = find(section, key);
    return found < 0 || std::cmp_greater_equal(found, items.size()) ? empty_resolved() : core::at(items, found);
}

bool Resolution::on(const char* key) const
{
    const Resolved& item = at(key);
    return item.in_effect && truthy(item.value);
}

float Resolution::number(const char* key, float fallback) const
{
    double number = 0.0;
    return number_of(at(key).value, &number) ? static_cast<float>(number) : fallback;
}

int Resolution::integer(const char* key, int fallback) const
{
    const std::string& text = at(key).value;
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    return end == text.c_str() ? fallback : static_cast<int>(parsed);
}

Resolution resolve(const Inputs& inputs)
{
    Resolver resolver(inputs);
    return resolver.run();
}

namespace {

// One setting's declaration: its key once, its default and choices, its rules' keys and words.
void check_setting(const Setting& setting, std::set<std::string>* slots, std::string* problems)
{
    const auto add = [problems](const std::string& line) { *problems += line + "\n"; };
    const std::string name = std::string("[") + setting.section + "] " + setting.key;
    if (!slots->insert(ini_slot(setting.section, setting.key)).second) {
        add(name + " is declared twice");
    }
    const bool listed_default = match_choice(setting, setting.fallback) >= 0;
    if ((setting.kind == Kind::kChoice || setting.kind == Kind::kSwitch) && !listed_default) {
        add(name + ": its default isn't one of its choices");
    }
    for (size_t i = 0; i < setting.choices.size(); ++i) {
        const Choice& choice = core::at(setting.choices, i);
        if (std::cmp_not_equal(match_choice(setting, choice.value), i)) {
            add(name + ": two choices are one value");
        }
        for (const Need& need : choice.needs) {
            check_cond(need.cond, name + " choice " + choice.value, problems);
            if (!reason_made(need)) {
                add(name + ": a choice's need has no words");
            }
        }
        if (choice.instead != nullptr && match_choice(setting, choice.instead) < 0) {
            add(name + ": a choice's stand-in isn't a choice");
        }
    }
    if (setting.group != Group::kNone && (setting.label == nullptr || setting.choices.empty())) {
        add(name + ": offered by the Options screen without a name or choices");
    }
    if (setting.group != Group::kNone && setting.apply != Apply::kPreset && !listed_default) {
        add(name + ": its default isn't one of the choices the Options screen offers");
    }
    for (const Need& need : setting.needs) {
        check_cond(need.cond, name, problems);
        if (!reason_made(need)) {
            add(name + ": a need has no words (only one setting's or one capability's are made)");
        }
    }
    if (setting.apply == Apply::kLive && setting.effect == Effect::kNone && std::strcmp(setting.section, "Graphics") == 0 &&
        setting.group == Group::kNone) {
        add(name + ": live without the renderer's part it changes");
    }
}

// The forces' and the conflicts' keys, values and conditions.
void check_forces_and_conflicts(std::string* problems)
{
    const auto add = [problems](const std::string& line) { *problems += line + "\n"; };
    for (const Force& force : forces()) {
        const int found = find(force.key);
        if (found < 0) {
            add(std::string("force on ") + force.key + ": no setting");
            continue;
        }
        const Setting& target = core::at(settings(), found);
        if (target.kind != Kind::kText && !target.open && match_choice(target, force.value) < 0) {
            add(std::string("force on ") + force.key + ": " + force.value + " isn't a choice");
        }
        check_cond(force.when, std::string("force on ") + force.key, problems);
    }
    for (const Conflict& conflict : conflicts()) {
        for (const char* key : conflict.keys) {
            if (find(key) < 0) {
                add(std::string("conflict: ") + key + " is no setting");
            }
        }
        check_cond(conflict.when, "conflict", problems);
    }
    for (const Limit& limit : limits()) {
        for (const char* key : limit.keys) {
            if (find(key) < 0) {
                add(std::string("limit: ") + key + " is no setting");
            }
        }
        if (limit.reason == nullptr) {
            add("limit: no words");
        }
        check_cond(limit.when, "limit", problems);
    }
}

// The settings each setting's rules read the effective values of: its needs, its choices' needs and the forces on it.
std::vector<std::vector<int>> rule_reads()
{
    const std::vector<Setting>& all = settings();
    std::vector<std::vector<int>> reads(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        const Setting& setting = core::at(all, i);
        std::vector<const char*> keys;
        for (const Need& need : setting.needs) {
            keys_of(need.cond, &keys);
        }
        for (const Choice& choice : setting.choices) {
            for (const Need& need : choice.needs) {
                keys_of(need.cond, &keys);
            }
        }
        for (const Force& force : forces()) {
            if (same_text(force.key, setting.key)) {
                keys_of(force.when, &keys);
            }
        }
        for (const char* key : keys) {
            const int found = find(key);
            if (found >= 0) {
                core::at(reads, i).push_back(found);
            }
        }
    }
    return reads;
}

// A cycle of rules (a setting whose rules come back to read it): the resolver would read a value it is still making.
void check_cycles(std::string* problems)
{
    const std::vector<Setting>& all = settings();
    const std::vector<std::vector<int>> reads = rule_reads();
    enum class Mark : uint8_t { kNotSeen, kOnPath, kDone };
    std::vector<Mark> mark(all.size(), Mark::kNotSeen);
    const auto visit = [&](auto&& self, int node) -> bool {
        core::at(mark, node) = Mark::kOnPath;
        for (const int next : core::at(reads, node)) {
            if (core::at(mark, next) == Mark::kOnPath) {
                *problems += std::string("a cycle of rules through ") + core::at(all, node).key + " and " + core::at(all, next).key + "\n";
                return false;
            }
            if (core::at(mark, next) == Mark::kNotSeen && !self(self, next)) {
                return false;
            }
        }
        core::at(mark, node) = Mark::kDone;
        return true;
    };
    for (size_t i = 0; i < all.size(); ++i) {
        if (core::at(mark, i) == Mark::kNotSeen) {
            visit(visit, static_cast<int>(i));
        }
    }
}

// The graphics presets' keys and values (graphics_preset's choices after Custom).
void check_presets(std::string* problems)
{
    const int presets = static_cast<int>(core::at(settings(), find("graphics_preset")).choices.size());
    for (int choice = 1; choice < presets; ++choice) {
        for (const PresetValue& set : preset_values(choice)) {
            const int found = find("Graphics", set.key);
            if (found < 0) {
                *problems += std::string("preset ") + std::to_string(choice) + ": " + set.key + " is no [Graphics] setting\n";
            } else if (match_choice(core::at(settings(), found), set.value) < 0) {
                *problems += std::string("preset ") + std::to_string(choice) + ": " + set.key + "=" + set.value + " is no choice\n";
            }
        }
    }
}

} // namespace

std::string check_model()
{
    std::string problems;
    std::set<std::string> slots;
    for (const Setting& setting : settings()) {
        check_setting(setting, &slots, &problems);
    }
    check_forces_and_conflicts(&problems);
    check_cycles(&problems);
    if (find("graphics_preset") < 0) {
        problems += "no graphics_preset\n";
    } else {
        check_presets(&problems);
    }
    return problems;
}

} // namespace config
