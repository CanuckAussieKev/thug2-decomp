// NOT ORIGINAL - the level editor's goals (ours): THUG2's Create-a-Goal goals in the edited copy. See
// level_editor_goals.h. The ImGui parts are in level_editor_goals_ui.cpp.

#include "editor/level_editor_goals.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_ui.h"
#include "engine/core/checksums.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/goal_editor_util.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "engine/maths/vec4.h"
#include "engine/maths/trig.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::goal_edit {

// --- the pure parts -------------------------------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kGoalEditor = 0x81F01058U;        // GoalEditor
constexpr std::uint32_t kLoadSk5Ed = 0xE8B4B836U;         // Load_Sk5Ed
constexpr std::uint32_t kLoadSk5EdGameplay = 0xB664035DU; // Load_Sk5Ed_gameplay
constexpr std::uint32_t kName = 0xA1DC81F9U;              // Name
constexpr std::uint32_t kSkate = 0x54166ACDU;             // Skate
constexpr std::uint32_t kWalk = 0x726E85AAU;              // Walk
constexpr long kMaxFieldNumber = 100000000L; // a number field's range (plus and minus)
constexpr std::size_t kLineTextSize = 200;
constexpr std::size_t kGoalPrefixLength = 5; // "goal "
constexpr std::size_t kMaxIdLength = 40;
constexpr std::size_t kMaxNameChars = 49;    // the goal's name, in characters
constexpr std::size_t kMaxPedChars = 20;
constexpr std::size_t kMaxMessageChars = 99; // the description and the win message
constexpr std::size_t kMaxTrickChars = 49;
constexpr int kPointValueCount = 5;      // x, y, z, heading and height
constexpr int kLabelBytes = 0x32;        // the game's buffers (the characters and the terminator)
constexpr int kSummaryBytes = 0x64;
constexpr int kWinBytes = 0x64;
constexpr int kPedBytes = 0x15;
constexpr int kMaxGapEntries = 10;
constexpr int kMaxGapNumber = 255;
constexpr int kMaxChosenSets = 10;
constexpr std::size_t kNameTextSize = 64;
constexpr int kSuspendDistance = 3000;   // a goal's default object distances
constexpr int kLod1Distance = 200;
constexpr int kLod2Distance = 400;
constexpr int kLetterLod1Distance = 400; // a letter's
constexpr int kLetterLod2Distance = 401;
constexpr float kGroundCastRise = 200.0F;   // the ground under a point is looked for from this far above ...
constexpr float kGroundCastDepth = 5000.0F; // ... down to this far below it
constexpr int kMaxNameSuffix = 100;
constexpr double kLimitMessageSeconds = 4.0;
constexpr float kPlaceReach = 4000.0F;   // a new goal is placed where the view looks, within this
constexpr float kPlaceDistance = 600.0F; // ... or this far ahead
constexpr float kPointSpacing = 150.0F;  // the next point is put this far from the last
constexpr double kAddedMessageSeconds = 5.0;
constexpr float kMaxPointHeight = 1000.0F; // EditorCam_MaxHeight
constexpr float kMaxDrawReach = 8000.0F;
constexpr float kLitWidth = 2.0F;
constexpr float kUnlitWidth = 1.25F;
constexpr float kLabelReach = 3000.0F;
constexpr float kLabelRise = 12.0F;
constexpr float kLinkLift = 4.0F;
constexpr uint32_t kLitLinkColour = 0xC0FFE070;   // ARGB
constexpr uint32_t kUnlitLinkColour = 0x80E0A030; // ARGB
constexpr uint32_t kLitPairColour = 0x90FFE070;   // ARGB
constexpr uint32_t kUnlitPairColour = 0x50E0A030; // ARGB


struct TypeInfo {
    const char* name;  // its checksum's name
    const char* title; // goals_menu_info's
    uint32_t checksum;
};
const std::array<TypeInfo, kTypes> kTypeInfo = {{
    {.name = "Skate", .title = "SKATE letters", .checksum = 0x54166ACD},    {.name = "Combo", .title = "COMBO letters", .checksum = 0x4EC3CFB5},
    {.name = "HighScore", .title = "High Score", .checksum = 0x6FE44C6D},   {.name = "HighCombo", .title = "High Combo", .checksum = 0xEC414B76},
    {.name = "Skatetris", .title = "Skate-Tricks", .checksum = 0x7FE2238A}, {.name = "ComboSkatetris", .title = "Combo Skate-Tricks", .checksum = 0x0FC6F698},
    {.name = "TrickTris", .title = "Tricktris", .checksum = 0xEA7BA666},    {.name = "TrickBeat", .title = "Trick to the Beat", .checksum = 0xD0DD047B},
    {.name = "Race", .title = "Checkpoint / Race", .checksum = 0x25904450}, {.name = "Gap", .title = "Gap", .checksum = 0x61C5D092},
}};

// goal_editor.qb's cag_key_combos (the sets' texts).
constexpr auto kComboSets = std::to_array<const char*>({"Basic Flip Tricks",      "Diagonal Flip Tricks",     "Basic Grab Tricks",
                                  "Diagonal Grab Tricks",   "Double Tap Basic Flips",   "Double Tap Diagonal Flips",
                                  "Double Tap Basic Grabs", "Double Tap Diagonal Grabs"});

constexpr auto kSkateLetters = std::to_array<const char*>({"S", "K", "A", "T", "E"});
constexpr auto kComboLetters = std::to_array<const char*>({"C", "O", "M", "B", "O"});
constexpr auto kComboSuffixes = std::to_array<const char*>({"c", "o", "m", "b", "o2"});

int get_index(KindTag type) { return static_cast<int>(type); }

// The record's numbers.
bool read_numbers(const std::string& text, std::span<float> out, int count)
{
    const char* at = text.c_str();
    for (int i = 0; i < count; ++i) {
        char* end = nullptr;
        core::at(out, i) = std::strtof(at, &end);
        if (end == at || !std::isfinite(core::at(out, i))) {
            return false;
        }
        at = end;
        if (i + 1 < count) {
            if (*at != ',') {
                return false;
            }
            at = std::next(at);
        }
    }
    return *at == '\0';
}

bool read_int(const std::string& text, int* out)
{
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (*end != '\0' || v < -kMaxFieldNumber || v > kMaxFieldNumber) {
        return false;
    }
    *out = static_cast<int>(v);
    return true;
}

std::vector<std::string> get_split(const std::string& text, char by)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= text.size()) {
        size_t next = text.find(by, at);
        if (next == std::string::npos) {
            next = text.size();
        }
        out.push_back(text.substr(at, next - at));
        at = next + 1;
    }
    return out;
}

// The settings kept within Create-a-Goal's menus' limits (goal_editor_menu.qb's number items).
void confine(Goal* goal)
{
    goal->time = std::clamp(goal->time, kMinTimeLimit, kMaxTimeLimit);
    if (goal->type == KindTag::kHighScore || goal->type == KindTag::kHighCombo) {
        goal->score = std::clamp(goal->score, kMinHighScore, kMaxHighScore);
    }
    goal->laps = std::clamp(goal->laps, 1, kMaxLaps);
    goal->checkpoint_time_ = std::clamp(goal->checkpoint_time_, 0, kMaxCheckpointTime);
    goal->spin = std::clamp(goal->spin, 0, kMaxSpin) / kSpinStep * kSpinStep;
    goal->accel_interval_ = std::clamp(goal->accel_interval_, 1, kMaxAccelInterval);
    if (!(goal->accel_percent_ >= kMinAccelPercent)) {
        goal->accel_percent_ = kMinAccelPercent;
    }
    goal->accel_percent_ = std::min(goal->accel_percent_, 1.0F);
    goal->combo_time_ = std::clamp(goal->combo_time_, kMinComboTime, kMaxComboTime);
    goal->stop_at_ = std::clamp(goal->stop_at_, kMinStopAt, std::max(kMinStopAt, goal->time - kStopAtMargin));
    goal->max_tricks_ = std::clamp(goal->max_tricks_, 1, kMaxTrickCount);
    goal->combo_size_ = std::clamp(goal->combo_size_, 1, kMaxTrickCount);
    goal->block_size_ = std::clamp(goal->block_size_, 1, kMaxTrickCount);
    goal->to_win_ = std::clamp(goal->to_win_, 1, kMaxToWin);
    goal->sets &= (1U << combo_set_count(goal->type)) - 1U;
    if (goal->gaps.size() > kMaxGaps) {
        goal->gaps.resize(kMaxGaps);
    }
}

} // namespace

const char* current_type_name(KindTag type) { return core::at(kTypeInfo, get_index(type)).name; }
const char* type_title(KindTag type) { return core::at(kTypeInfo, get_index(type)).title; }
uint32_t get_kind_hash(KindTag type) { return core::at(kTypeInfo, get_index(type)).checksum; }

bool type_of(const std::string& name, KindTag* type)
{
    for (int i = 0; i < kTypes; ++i) {
        if (_stricmp(core::at(kTypeInfo, i).name, name.c_str()) == 0) {
            *type = static_cast<KindTag>(i);
            return true;
        }
    }
    return false;
}

bool kind_of_key(uint32_t checksum, KindTag* type)
{
    for (int i = 0; i < kTypes; ++i) {
        if (core::at(kTypeInfo, i).checksum == checksum) {
            *type = static_cast<KindTag>(i);
            return true;
        }
    }
    return false;
}

bool get_trick_type(KindTag type)
{
    return type == KindTag::kSkatetris || type == KindTag::kComboSkatetris || type == KindTag::kTrickTris ||
           type == KindTag::kTrickBeat;
}

int combo_set_count(KindTag type)
{
    if (!get_trick_type(type)) {
        return 0;
    }
    return type == KindTag::kTrickBeat ? 4 : kComboSetCount; // (create_key_combos_menu: array_size - 4 for TrickBeat)
}

const char* combo_set_name(int set) { return set >= 0 && set < kComboSetCount ? core::at(kComboSets, set) : ""; }

// change_species (0x004E6DA0): the pro and the start always; SKATE: 7 (and 11 letter entries from 2), COMBO: 7 (letter
// entries 2..6), Race: 4 to 12 (11 checkpoint entries from 2), every other type: 2.
int min_points(KindTag type)
{
    switch (type) {
    case KindTag::kSkate:
    case KindTag::kCombo:
        return kSkatePointCount;
    case KindTag::kRace:
        return 4;
    default:
        return 2;
    }
}

int max_points(KindTag type)
{
    switch (type) {
    case KindTag::kSkate:
    case KindTag::kCombo:
        return kSkatePointCount;
    case KindTag::kRace:
        return kRaceMaxPointCount;
    default:
        return 2;
    }
}

int point_kind(KindTag type, int index)
{
    if (index == 0) {
        return 1;
    }
    if (index == 1) {
        return 2;
    }
    if (index < 2 || index >= kMaxPoints) {
        return 0;
    }
    switch (type) {
    case KindTag::kSkate:
        return 3;
    case KindTag::kCombo:
        return index < kSkatePointCount ? 3 : 0;
    case KindTag::kRace:
        return 4;
    default:
        return 0;
    }
}

std::string point_label(KindTag type, int index)
{
    if (index == 0) {
        return "Pro";
    }
    if (index == 1) {
        return "Start";
    }
    if (index >= 2 && index < kSkatePointCount && type == KindTag::kSkate) {
        return core::at(kSkateLetters, index - 2);
    }
    if (index >= 2 && index < kSkatePointCount && type == KindTag::kCombo) {
        return core::at(kComboLetters, index - 2);
    }
    return "Checkpoint " + std::to_string(index - 1);
}

std::string point_suffix(KindTag type, int index)
{
    if (index == 0) {
        return "pro";
    }
    if (index == 1) {
        return "start";
    }
    if (index >= 2 && index < kSkatePointCount && type == KindTag::kSkate) {
        std::string letter = core::at(kSkateLetters, index - 2);
        return {static_cast<char>(core::at(letter, 0) - 'A' + 'a')}; // (one character: the letter in lower case)
    }
    if (index >= 2 && index < kSkatePointCount && type == KindTag::kCombo) {
        return core::at(kComboSuffixes, index - 2);
    }
    return "cp" + std::to_string(index - 1);
}

float starting_height(KindTag type, int index) { return point_kind(type, index) == 3 ? kLetterHeight : 0.0F; }

bool get_same(const Goal& a, const Goal& b)
{
    if (a.id != b.id || a.type != b.type || a.name != b.name || a.ped != b.ped || a.text != b.text || a.win != b.win ||
        a.time != b.time || a.score != b.score || a.walk != b.walk || a.laps != b.laps ||
        a.checkpoint_time_ != b.checkpoint_time_ || a.gaps != b.gaps || a.trick != b.trick || a.sets != b.sets ||
        a.spin != b.spin || a.accel_interval_ != b.accel_interval_ || a.accel_percent_ != b.accel_percent_ ||
        a.combo_time_ != b.combo_time_ || a.stop_at_ != b.stop_at_ || a.max_tricks_ != b.max_tricks_ ||
        a.single_combo_ != b.single_combo_ || a.combo_size_ != b.combo_size_ || a.block_size_ != b.block_size_ ||
        a.to_win_ != b.to_win_ || a.points.size() != b.points.size()) {
        return false;
    }
    for (size_t i = 0; i < a.points.size(); ++i) {
        const Point& point = core::at(a.points, i);
        const Point& other_point = core::at(b.points, i);
        if (point.pos.x != other_point.pos.x || point.pos.y != other_point.pos.y || point.pos.z != other_point.pos.z ||
            point.heading != other_point.heading || point.height != other_point.height) {
            return false;
        }
    }
    return true;
}

Goal get_defaults(KindTag type, const std::string& id)
{
    Goal goal;
    goal.id = id;
    goal.type = type;
    goal.name = type_title(type);
    // (change_species reads score from the type's generic parameters: Goal_HighScore_genericParams 15000,
    // goal_highcombo_genericParams 10000; init_edited_goal's 100 otherwise)
    if (type == KindTag::kHighScore) {
        goal.score = kHighScoreTarget;
    } else if (type == KindTag::kHighCombo) {
        goal.score = kHighComboTarget;
    } else {
        goal.score = kDefaultScore;
    }
    if (type == KindTag::kTrickBeat) {
        goal.spin = 0; // (change_species)
    }
    return goal;
}

bool get_complete(const Goal& goal, std::string* why)
{
    auto fail = [&](const std::string& text) {
        if (why) {
            *why = text;
        }
        return false;
    };
    const int count = static_cast<int>(goal.points.size());
    if (count < min_points(goal.type)) {
        return fail("It needs " + std::to_string(min_points(goal.type)) + " points (has " + std::to_string(count) +
                    ")");
    }
    if (count > max_points(goal.type)) {
        return fail("Too many points");
    }
    if (goal.type == KindTag::kGap && goal.gaps.empty()) {
        return fail("Pick at least one gap");
    }
    if (get_trick_type(goal.type) && goal.sets == 0) {
        return fail("Choose at least one key combo set");
    }
    return true;
}

std::string clean_text(const std::string& text, size_t limit)
{
    std::string out;
    for (const char character : text) {
        auto u = static_cast<unsigned char>(character);
        if (u < kFirstPrintableChar || u == kDeleteChar || character == '"' || character == '#' || character == '|') {
            continue;
        }
        if (out.size() >= limit) {
            break;
        }
        out.push_back(character);
    }
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1);
}

std::string write_record(const Goal& goal)
{
    std::string out = "goal " + goal.id + " type=" + current_type_name(goal.type);
    auto text = [&](const char* key, const std::string& value) {
        out += std::string(" ") + key + "=\"" + value + "\"";
    };
    auto number = [&](const char* key, int value) { out += std::string(" ") + key + "=" + std::to_string(value); };
    text("name", goal.name);
    text("ped", goal.ped);
    text("text", goal.text);
    text("win", goal.win);
    number("time", goal.time);
    number("score", goal.score);
    out += std::string(" control=") + (goal.walk ? "Walk" : "Skate");
    std::array<char, kLineTextSize> buffer{};
    for (size_t i = 0; i < goal.points.size(); ++i) {
        const Point& point = core::at(goal.points, i);
        text::format_to(buffer, " p%u=%.9g,%.9g,%.9g,%.9g,%.9g", static_cast<unsigned>(i), point.pos.x, point.pos.y,
                        point.pos.z, point.heading, point.height);
        out += buffer.data();
    }
    if (goal.type == KindTag::kRace) {
        number("laps", goal.laps);
        number("cptime", goal.checkpoint_time_);
    }
    if (goal.type == KindTag::kGap) {
        std::string gaps;
        for (size_t i = 0; i < goal.gaps.size(); ++i) {
            gaps += ((i != 0U) ? "|" : "") + core::at(goal.gaps, i);
        }
        text("gaps", gaps);
        text("trick", goal.trick);
    }
    if (get_trick_type(goal.type)) {
        std::string sets;
        for (int i = 0; i < kComboSetCount; ++i) {
            if ((goal.sets & (1U << i)) != 0U) {
                sets += (sets.empty() ? "" : ",") + std::to_string(i);
            }
        }
        out += " sets=" + (sets.empty() ? std::string("none") : sets);
        number("spin", goal.spin);
        number("accel", goal.accel_interval_);
        text::format_to(buffer, " accelpct=%.9g", goal.accel_percent_);
        out += buffer.data();
        number("tricktime", goal.combo_time_);
        number("stopat", goal.stop_at_);
        number("maxtricks", goal.max_tricks_);
        number("single", goal.single_combo_ ? 1 : 0);
        number("combo", goal.combo_size_);
        number("block", goal.block_size_);
        number("towin", goal.to_win_);
    }
    return out.substr(kGoalPrefixLength); // (after "goal ")
}

bool read_record(const std::string& text, Goal* out)
{
    // Tokens: the id, then key=value with a value either up to the next space or "quoted".
    size_t at = 0;
    auto skip = [&] {
        while (at < text.size() &&
               (core::at(text, at) == ' ' || core::at(text, at) == '\t' || core::at(text, at) == '\r')) {
            ++at;
        }
    };
    skip();
    const size_t end = text.find_first_of(" \t\r", at);
    const std::string id = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (!node_create::valid_name(id) || id.size() > kMaxIdLength) {
        return false;
    }
    at = end == std::string::npos ? text.size() : end;
    Goal goal;
    goal.id = id;
    std::vector<std::string> seen;
    std::vector<std::pair<int, Point>> points;
    bool have_type = false;
    while (true) {
        skip();
        if (at >= text.size()) {
            break;
        }
        const size_t equals = text.find('=', at);
        if (equals == std::string::npos || equals == at) {
            return false;
        }
        std::string key = text.substr(at, equals - at);
        if (key.find_first_of(" \t\"") != std::string::npos) {
            return false;
        }
        at = equals + 1;
        std::string value;
        if (at < text.size() && core::at(text, at) == '"') {
            const size_t close = text.find('"', at + 1);
            if (close == std::string::npos) {
                return false;
            }
            value = text.substr(at + 1, close - at - 1);
            at = close + 1;
            if (at < text.size() && core::at(text, at) != ' ' && core::at(text, at) != '\t' &&
                core::at(text, at) != '\r') {
                return false; // (text after the closing quote)
            }
        } else {
            const size_t stop = text.find_first_of(" \t\r", at);
            value = text.substr(at, stop == std::string::npos ? std::string::npos : stop - at);
            at = stop == std::string::npos ? text.size() : stop;
        }
        if (std::ranges::find(seen, key) != seen.end()) {
            return false;
        }
        seen.push_back(key);
        bool ok = true;
        if (key == "type") {
            ok = type_of(value, &goal.type);
            have_type = ok;
        } else if (key == "name") {
            goal.name = clean_text(value, kMaxNameChars);
        } else if (key == "ped") {
            goal.ped = clean_text(value, kMaxPedChars);
        } else if (key == "text") {
            goal.text = clean_text(value, kMaxMessageChars);
        } else if (key == "win") {
            goal.win = clean_text(value, kMaxMessageChars);
        } else if (key == "trick") {
            goal.trick = clean_text(value, kMaxTrickChars);
        } else if (key == "gaps") {
            goal.gaps.clear();
            if (!value.empty()) {
                for (const std::string& gap : get_split(value, '|')) {
                    const std::string clean = clean_text(gap, 60);
                    if (!clean.empty()) {
                        goal.gaps.push_back(clean);
                    }
                }
            }
        } else if (key == "control") {
            ok = value == "Skate" || value == "Walk";
            goal.walk = value == "Walk";
        } else if (key == "sets") {
            goal.sets = 0;
            if (value != "none") {
                for (const std::string& part : get_split(value, ',')) {
                    int set = 0;
                    ok = ok && read_int(part, &set) && set >= 0 && set < kComboSetCount;
                    if (ok) {
                        goal.sets |= 1U << set;
                    }
                }
            }
        } else if (key == "accelpct") {
            ok = read_numbers(value, std::span<float>(&goal.accel_percent_, 1), 1);
        } else if (key.size() >= 2 && core::at(key, 0) == 'p' && key.find_first_not_of("0123456789", 1) == std::string::npos) {
            int index = 0;
            std::array<float, kPointValueCount> v{};
            ok = read_int(key.substr(1), &index) && index >= 0 && index < kMaxPoints && read_numbers(value, v, kPointValueCount);
            if (ok) {
                points.emplace_back(index, Point{.pos = {.x = core::at(v, 0), .y = core::at(v, 1), .z = core::at(v, 2)}, .heading = core::at(v, 3), .height = core::at(v, 4)});
            }
        } else {
            struct IntKey {
                const char* key;
                int* field;
            };
            const auto ints = std::to_array<IntKey>({{.key = "time", .field = &goal.time},
                                                     {.key = "score", .field = &goal.score},
                                                     {.key = "laps", .field = &goal.laps},
                                                     {.key = "cptime", .field = &goal.checkpoint_time_},
                                                     {.key = "spin", .field = &goal.spin},
                                                     {.key = "accel", .field = &goal.accel_interval_},
                                                     {.key = "tricktime", .field = &goal.combo_time_},
                                                     {.key = "stopat", .field = &goal.stop_at_},
                                                     {.key = "maxtricks", .field = &goal.max_tricks_},
                                                     {.key = "combo", .field = &goal.combo_size_},
                                                     {.key = "block", .field = &goal.block_size_},
                                                     {.key = "towin", .field = &goal.to_win_}});
            bool known = false;
            for (const IntKey& int_key_entry : ints) {
                if (key == int_key_entry.key) {
                    known = true;
                    ok = read_int(value, int_key_entry.field);
                }
            }
            if (key == "single") {
                int single = 0;
                ok = read_int(value, &single) && (single == 0 || single == 1);
                goal.single_combo_ = single == 1;
            } // (another key: a later version's - skipped)
            (void)known;
        }
        if (!ok) {
            return false;
        }
    }
    if (!have_type) {
        return false;
    }
    // (the points: p0.. with none missing, within the type's most)
    std::ranges::sort(points, [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < points.size(); ++i) {
        if (core::at(points, i).first != ints::as_signed(i)) {
            return false;
        }
        goal.points.push_back(core::at(points, i).second);
    }
    if (static_cast<int>(goal.points.size()) > max_points(goal.type)) {
        return false;
    }
    confine(&goal);
    *out = goal;
    return true;
}

float get_image_angle(float heading) { return heading + maths::kPi; }
float get_heading_of(float image_angle) { return image_angle - maths::kPi; }

void fill_image(const Goal& goal, GoalInLevel goal_in_level, const std::vector<int>& gap_numbers,
                GoalImage* out)
{
    const auto level = goal_in_level.level;
    const auto level_goal_index = goal_in_level.level_goal_index_;
    park_editing::EditedGoal& edited_goal = out->goal;
    edited_goal = park_editing::EditedGoal{}; // (cleared, then init_edited_goal: the record's constructor)
    auto text = [](auto& field, const std::string& value, size_t room) {
        const size_t count = std::min(value.size(), room - 1);
        std::copy_n(value.begin(), count, field.begin());
        core::at(field, count) = 0;
    };
    // decode_from_struct (0x004E96D0): init_edited_goal, used, not won, level, level_goal_index, the type (change_species: the
    // points' kinds, the counts; the pro's / start's height 0), then the fields.
    edited_goal.set_taken_count(true);
    edited_goal.set_won(false);
    edited_goal.set_level(level);
    edited_goal.set_level_goal_number(level_goal_index);
    edited_goal.set_kind_tag(get_kind_hash(goal.type));
    for (int i = 0; i < kMaxPoints; ++i) {
        park_editing::EditedGoalPoint& point = core::at(edited_goal.path_vertices(), i);
        point.kind = point_kind(goal.type, i);
        point.height = 0.0F;
    }
    edited_goal.set_min_points(static_cast<uint8_t>(min_points(goal.type)));
    edited_goal.set_max_points(static_cast<uint8_t>(max_points(goal.type)));
    if (goal.type == KindTag::kTrickBeat) {
        edited_goal.set_twist_rate(0);
    }
    edited_goal.set_points_held(goal.score);
    edited_goal.set_time_limit(goal.time);
    edited_goal.set_control_type(goal.walk ? kWalk : kSkate); // (control_type: Walk / Skate)
    text(edited_goal.label(), goal.name, kLabelBytes);
    text(edited_goal.summary_text(), goal.text, kSummaryBytes);
    text(edited_goal.win_message(), goal.win, kWinBytes);
    text(edited_goal.ped_name(), goal.ped, kPedBytes);
    const int count = std::min(static_cast<int>(goal.points.size()), max_points(goal.type));
    edited_goal.set_vertex_total(static_cast<uint8_t>(count));
    for (int i = 0; i < count; ++i) {
        const Point& goal_point = core::at(goal.points, i);
        park_editing::EditedGoalPoint& point = core::at(edited_goal.path_vertices(), i);
        point.pos = {.x = goal_point.pos.x, .y = goal_point.pos.y, .z = goal_point.pos.z, .w = 1.0F};
        point.height = goal_point.height;
        point.angle = get_image_angle(goal_point.heading);
    }
    // The type's own (set_goal_specific_params' fields, 0x004E7590).
    switch (goal.type) {
    case KindTag::kRace:
        edited_goal.set_race_total_laps(goal.laps);
        edited_goal.set_race_per_checkpoint_time(goal.checkpoint_time_);
        break;
    case KindTag::kGap: {
        text(edited_goal.required_trick(), goal.trick, edited_goal.required_trick().size());
        int written = 0;
        for (const int number : gap_numbers) {
            if (written >= kMaxGapEntries || number < 0 || number > kMaxGapNumber) {
                continue;
            }
            core::at(edited_goal.gaps(), written++) = static_cast<uint8_t>(number);
        }
        edited_goal.set_num_gaps(static_cast<uint8_t>(written));
        break;
    }
    default:
        break;
    }
    if (get_trick_type(goal.type)) {
        // (0x004E7590: Combo Skate-Tricks its combo_size / single_combo, Tricktris its block size / total; those and
        // Skate-Tricks the shared ones (0x004E74F0); every trick goal its key combo sets (0x004E73F0) - Trick to the
        // Beat only those, its spin staying change_species's 0)
        if (goal.type == KindTag::kComboSkatetris) {
            edited_goal.set_chain_length(goal.combo_size_);
            if (goal.single_combo_) {
                edited_goal.set_single_combo(true);
            }
        }
        if (goal.type == KindTag::kTrickTris) {
            edited_goal.set_stack_block_span(goal.block_size_);
            edited_goal.set_tricktris_total_to_win(goal.to_win_);
        }
        if (goal.type != KindTag::kTrickBeat) {
            edited_goal.set_acceleration_interval(goal.accel_interval_);
            edited_goal.set_acceleration_percent(goal.accel_percent_);
            edited_goal.set_trick_time(goal.combo_time_);
            edited_goal.set_time_to_stop_adding_tricks(goal.stop_at_);
            edited_goal.set_max_tricks(goal.max_tricks_);
            edited_goal.set_twist_rate(goal.spin);
        }
        // (the key combo sets as AddKeyComboSet set_index=<i> adds them - 0x004E6C40: {set, require_perfect 0, spin
        // 0, num_taps 0})
        int sets = 0;
        for (int i = 0; i < combo_set_count(goal.type) && sets < kMaxChosenSets; ++i) {
            if ((goal.sets & (1U << i)) == 0U) {
                continue;
            }
            core::at(edited_goal.key_combo_sets(), sets).set = static_cast<uint8_t>(i);
            ++sets;
        }
        edited_goal.set_num_key_combo_sets(sets);
    }
}

std::string play_node_name(int which, int index)
{
    std::array<char, kNameTextSize> name{};
    if (which == 0) {
        text::format_to(name, "TRG_EditedGoal%d_Pro", index);
    } else if (which == 1) {
        text::format_to(name, "TRG_EditedGoal%d_Restart", index);
    } else {
        text::format_to(name, "TRG_EditedGoal%d_Object_%d", index, which - 2);
    }
    return name.data();
}

std::string get_goal_id(int index) { return "CreatedGoal" + std::to_string(index); }

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

using Function = bool (*)(script::Struct*, script::Script*);

// NOT ORIGINAL grouping - level_editor_goals.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_goals_state()).
struct LevelEditorGoalsState {
    std::vector<Goal> goals_; // (their points' places: as their nodes had them when last read)
    std::vector<int> in_play_; // the play indexes in the goal manager now
    std::vector<std::function<void()>> queue_;
    bool dragging_{}; // a field being dragged: one undo step
    int select_ = -1; // a node to select (Nodes::All() index)
};

LevelEditorGoalsState& level_editor_goals_state()
{
    static core::NoDestroy<LevelEditorGoalsState> state;
    return state.get();
}

constexpr const char* kStem = "ed_goal";
constexpr const char* kGoalMarkerType = "ed_goal";

bool same_text(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; }
uint32_t get_checksums(const std::string& text) { return checksums::hash_from_text(text.c_str()); }

std::string point_node(const Goal& goal, int index) { return goal.id + "_" + point_suffix(goal.type, index); }

int index_of_goal_id(const std::string& id)
{
    for (size_t i = 0; i < level_editor_goals_state().goals_.size(); ++i) {
        if (core::at(level_editor_goals_state().goals_, i).id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Which goal a node belongs to and which of its points it is.
struct GoalPoint {
    int* goal{};
    int* point{};
};

// The goal and point a node is (false: not one of ours).
bool point_of(uint32_t node, GoalPoint goal_point)
{
    auto* const goal = goal_point.goal;
    auto* const point = goal_point.point;
    for (size_t i = 0; i < level_editor_goals_state().goals_.size(); ++i) {
        for (size_t k = 0; k < core::at(level_editor_goals_state().goals_, i).points.size(); ++k) {
            if (get_checksums(point_node(core::at(level_editor_goals_state().goals_, i), static_cast<int>(k))) == node) {
                *goal = static_cast<int>(i);
                *point = static_cast<int>(k);
                return true;
            }
        }
    }
    return false;
}

node_create::Spec point_spec(const Goal& goal, int index)
{
    node_create::Spec spec;
    spec.name = point_node(goal, index);
    spec.cls = "GenericNode";
    spec.type = kGoalMarkerType;
    spec.pos = core::at(goal.points, index).pos;
    spec.angles = {.x = 0.0F, .y = core::at(goal.points, index).heading, .z = 0.0F};
    spec.owned = true;
    return spec;
}

// The goals' places read again from their nodes (a node deleted: its last place kept - Restore points makes it again).
void get_refresh()
{
    for (Goal& goal : level_editor_goals_state().goals_) {
        for (size_t k = 0; k < goal.points.size(); ++k) {
            node_edit::Values v;
            if (node_edit::get_take_in(get_checksums(point_node(goal, static_cast<int>(k))), &v)) {
                core::at(goal.points, k).pos = v.pos;
                core::at(goal.points, k).heading = v.angles.y;
            }
        }
    }
}

int missing_points(const Goal& goal)
{
    int missing = 0;
    for (size_t k = 0; k < goal.points.size(); ++k) {
        missing += node_create::is_added(get_checksums(point_node(goal, static_cast<int>(k)))) ? 0 : 1;
    }
    return missing;
}

// Every point node of the goals made where missing / put where the goals have them.
void sync_nodes(const std::vector<Goal>& goals)
{
    std::string message;
    node_create::LinkChain make;
    for (const Goal& goal : goals) {
        for (size_t k = 0; k < goal.points.size(); ++k) {
            const uint32_t name = get_checksums(point_node(goal, static_cast<int>(k)));
            if (!node_create::is_added(name)) {
                if (!node_create::name_taken(point_node(goal, static_cast<int>(k)))) {
                    make.push_back(point_spec(goal, static_cast<int>(k)));
                }
                continue;
            }
            node_edit::Values v;
            v.pos = core::at(goal.points, k).pos;
            v.angles = {.x = 0.0F, .y = core::at(goal.points, k).heading, .z = 0.0F};
            node_edit::Values now;
            if (!node_edit::get_take_in(name, &now) || !node_edit::get_same(now, v)) {
                node_edit::assign_value(name, v);
            }
        }
    }
    if (!make.empty()) {
        node_create::create_all(make, &message);
    }
}

void remove_node(const std::string& name)
{
    const uint32_t checksum = get_checksums(name);
    if (!node_create::is_added(checksum)) {
        return;
    }
    node_create::Spec removed;
    int at = -1;
    node_create::drop_out(checksum, &removed, &at);
    node_edit::get_forget(checksum);
}

// Every node of ours of type ed_goal that no goal of `goals` has: removed.
void remove_stray_points(const std::vector<Goal>& goals)
{
    for (const node_create::Spec& spec : node_create::at_hand()) {
        if (!same_text(spec.type, kGoalMarkerType) || !same_text(spec.cls, "GenericNode")) {
            continue;
        }
        bool kept = false;
        for (const Goal& goal : goals) {
            for (size_t k = 0; k < goal.points.size() && !kept; ++k) {
                kept = same_text(point_node(goal, static_cast<int>(k)), spec.name);
            }
        }
        if (!kept) {
            remove_node(spec.name);
        }
    }
}

// --- undo: the goals as they are (their points' places too) ---

struct Step : ModuleStep {
    std::vector<Goal> goals;
    void get_restore() override
    {
        remove_stray_points(goals);
        level_editor_goals_state().goals_ = goals;
        sync_nodes(goals);
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        get_refresh();
        auto now = std::make_shared<Step>();
        now->goals = level_editor_goals_state().goals_;
        return now;
    }
};

void push_undo()
{
    get_refresh();
    auto step = std::make_shared<Step>();
    step->goals = level_editor_goals_state().goals_;
    push_module_undo(step);
}

// --- the goal manager (the skate module's - 0x005591B0 - through its script commands) ---


// The current level as the goal editor keeps it (modules::Skate +0x378; Load_Sk5Ed_gameplay counts as Load_Sk5Ed - as
// 0x004E7D00 / 0x004EC010 / 0x004EA6C0 do).
uint32_t current_level()
{
    const modules::Skate* skate = modules::Skate::sole();
    if (!skate) {
        return 0;
    }
    const uint32_t level = skate->level();
    return level == kLoadSk5EdGameplay ? kLoadSk5Ed : level;
}

// A GoalManager_* script command with name=<goal> (they take no other parameter and don't use the script).
bool goal_command(Function command, uint32_t goal)
{
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_name_key(checksums::Name{kName}, goal); // name
    const bool result = command(params, nullptr);
    params->get_clear();
    script::delete_struct(params);
    return result;
}
bool goal_exists(uint32_t goal)
{
    return goal_command(&goals::cfunc_goal_manager_goal_exists, goal);
} // GoalManager_GoalExists
bool is_goal_running(uint32_t goal)
{
    return goal_command(&goals::cfunc_goal_manager_goal_is_active, goal);
} // GoalManager_GoalIsActive
void switch_off_goal(uint32_t goal)
{
    goal_command(&goals::cfunc_goal_manager_deactivate_goal, goal);
} // GoalManager_DeactivateGoal
void drop_objective(uint32_t goal)
{
    goal_command(&goals::cfunc_goal_manager_remove_goal, goal);
} // GoalManager_RemoveGoal
void initialize_goal(uint32_t goal)
{
    goal_command(&goals::cfunc_goal_manager_initialize_goal, goal);
} // GoalManager_InitializeGoal

void remove_entity(const std::string& name)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(get_checksums(name)));
    if (object && ((object->object_flags() & objects::kObjectFlagDead) == 0U)) {
        object->flag_as_dead();
    }
}

// The play nodes of a goal (EditedGoal_Pro_Node / _Restart_Node / _Object_Node's fields, placed as
// write_edited_goal_node_positions 0x004E7A80 places them: Pos + (0, height, 0), Angles (0, angle - pi, 0), a letter's Type
// and model).
node_create::LinkChain play_nodes(const Goal& goal, int index)
{
    node_create::LinkChain out;
    auto field = [](node_create::Prop::KindTag type, const char* key) {
        node_create::Prop prop = {.type = type, .key = key};
        return prop;
    };
    auto name = [&](const char* key, const char* value) {
        node_create::Prop prop = field(node_create::Prop::kName, key);
        prop.text = value;
        return prop;
    };
    auto integer = [&](const char* key, int value) {
        node_create::Prop prop = field(node_create::Prop::kInteger, key);
        prop.integer = value;
        return prop;
    };
    auto string = [&](const char* key, const char* value) {
        node_create::Prop prop = field(node_create::Prop::kString, key);
        prop.text = value;
        return prop;
    };
    const float pi = maths::kPi; // (the constant 0x004E79C0 subtracts)
    for (size_t k = 0; k < goal.points.size(); ++k) {
        const Point& point = core::at(goal.points, k);
        node_create::Spec spec;
        spec.name = play_node_name(static_cast<int>(k), index);
        spec.pos = {.x = point.pos.x, .y = point.pos.y + point.height, .z = point.pos.z};
        spec.angles = {.x = 0.0F, .y = get_image_angle(point.heading) - pi, .z = 0.0F};
        spec.owned = true;
        const int kind = point_kind(goal.type, static_cast<int>(k));
        if (kind == 1) {
            spec.cls = "Pedestrian";
            spec.type = "Ped_From_Profile";
            spec.extra = {name("SkeletonName", "THPS6_human"),
                          name("DefaultAnimName", "WStandIdle1"),
                          name("PedAI_Type", "Stand"),
                          name("AnimName", "animload_THPS6_human"),
                          name("profile", "random_male_profile"),
                          integer("SuspendDistance", kSuspendDistance),
                          integer("lod_dist1", kLod1Distance),
                          integer("lod_dist2", kLod2Distance)};
        } else if (kind == 2) {
            spec.cls = "Restart";
            spec.type = "Player1";
            node_create::Prop types = field(node_create::Prop::kNames, "restart_types");
            types.list = {"Player1"};
            node_create::Prop created = field(node_create::Prop::kFlag, "");
            created.text = "CreatedAtStart";
            spec.extra = {created, string("RestartName", "Edited goal: Restart"), types};
        } else {
            spec.cls = "GameObject";
            const uint32_t kind_hash = get_kind_hash(goal.type);
            const char* model = park_editing::goal_object_model(checksums::Name{kind_hash}, static_cast<int>(k) - 2);
            static const std::array<std::array<const char*, 5>, 2> kLetterTypes = {{{"Letter_S", "Letter_K", "Letter_A", "Letter_T", "Letter_E"},
                                                           {"Combo_C", "Combo_O", "Combo_M", "Combo_B", "Combo_O"}}};
            if (kind == 3 && k >= 2 && k < kSkatePointCount) {
                spec.type = core::at(core::at(kLetterTypes, goal.type == KindTag::kCombo ? 1 : 0), k - 2);
            }
            spec.extra = {integer("SuspendDistance", 0), integer("lod_dist1", kLetterLod1Distance),
                          integer("lod_dist2", kLetterLod2Distance)};
            if (model) {
                spec.extra.push_back(string("model", model));
            }
        }
        out.push_back(spec);
    }
    return out;
}

// The gaps' numbers in the level's gap list now (the skate module's checklist, 0x005656C0).
std::vector<int> gap_ordinals(const Goal& goal)
{
    std::vector<int> out;
    modules::Skate* skate = modules::Skate::sole();
    auto* list = skate ? static_cast<goals::GapTally*>(skate->fetch_gap_list()) : nullptr;
    if (!list) {
        return out;
    }
    for (const std::string& gap : goal.gaps) {
        const int number = list->find_gap(gap.c_str());
        if (number >= 0) {
            out.push_back(number);
        } else {
            get_log("level editor: goal %s - no gap \"%s\" in the level's gap list: left out", goal.id.c_str(),
                    gap.c_str());
        }
    }
    return out;
}

void take_out_of_play()
{
    if (level_editor_goals_state().in_play_.empty()) {
        return;
    }
    for (const int index : level_editor_goals_state().in_play_) {
        const uint32_t goal = get_checksums(get_goal_id(index));
        if (modules::Skate::sole() && goal_exists(goal)) {
            if (is_goal_running(goal)) {
                switch_off_goal(goal);
            }
            drop_objective(goal);
        }
        // (edit_goal_menu_exit: <pro_name>:Die - and a letter / cone still standing)
        for (int which = 0; which < kMaxPoints; ++which) {
            remove_entity(play_node_name(which, index));
            remove_node(play_node_name(which, index));
        }
    }
    get_log("level editor: %u goals taken out of play", static_cast<unsigned>(level_editor_goals_state().in_play_.size()));
    level_editor_goals_state().in_play_.clear();
}

void put_in_play()
{
    take_out_of_play();
    std::string why;
    if (live_now() || !layout::editing_copy() || layout::get_refused(&why) || !modules::Skate::sole() ||
        level_editor_goals_state().goals_.empty()) {
        return;
    }
    get_refresh();
    const uint32_t level = current_level();
    node_create::LinkChain nodes;
    std::vector<std::pair<int, const Goal*>> play;
    for (size_t i = 0; i < level_editor_goals_state().goals_.size() && static_cast<int>(i) < kMaxGoals; ++i) {
        const Goal& goal = core::at(level_editor_goals_state().goals_, i);
        std::string incomplete;
        if (!get_complete(goal, &incomplete)) {
            get_log("level editor: goal %s (%s) not played - %s", goal.id.c_str(), goal.name.c_str(),
                    incomplete.c_str());
            continue;
        }
        const int index = play_index(static_cast<int>(i));
        bool taken = false;
        for (const node_create::Spec& spec : play_nodes(goal, index)) {
            if (node_create::name_taken(spec.name)) {
                taken = true;
            }
        }
        if (taken || goal_exists(get_checksums(get_goal_id(index)))) {
            get_log("level editor: goal %s not played - %s is taken", goal.id.c_str(), get_goal_id(index).c_str());
            continue;
        }
        node_create::LinkChain made = play_nodes(goal, index);
        nodes.insert(nodes.end(), made.begin(), made.end());
        play.emplace_back(index, &goal);
    }
    if (play.empty()) {
        return;
    }
    std::string message;
    node_create::create_all(nodes, &message);
    for (const auto& [index, goal] : play) {
        // Create-a-Goal's own: AddEditedGoalToGoalManager (0x004EB6B0 on the record, MarkUnbeaten), then
        // GoalManager_InitializeGoal (its pro made from TRG_EditedGoal<n>_Pro).
        GoalImage image{};
        fill_image(*goal, {.level = level, .level_goal_index_ = index}, gap_ordinals(*goal), &image);
        image.goal.add_to_goal_manager(true);
        const uint32_t id = get_checksums(get_goal_id(index));
        if (!goal_exists(id)) {
            get_log("level editor: goal %s - the goal manager didn't take it", goal->id.c_str());
            continue;
        }
        initialize_goal(id);
        level_editor_goals_state().in_play_.push_back(index);
        get_log("level editor: goal %s (%s, \"%s\") in play as %s", goal->id.c_str(), current_type_name(goal->type),
                goal->name.c_str(), get_goal_id(index).c_str());
    }
}

// Where the view looks, on the ground (the game's collision ray - a ProbeRay, as the triggers' Cast).
bool get_cast(V3 from, V3 to, V3* hit)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray->get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    const auto& point = ray->hit().point;
    *hit = {.x = point[0], .y = point[1], .z = point[2]};
    return true;
}

V3 get_grounded(V3 at)
{
    V3 ground{};
    if (get_cast(at + V3{.x = 0.0F, .y = kGroundCastRise, .z = 0.0F},
                 at - V3{.x = 0.0F, .y = kGroundCastDepth, .z = 0.0F}, &ground)) {
        return ground;
    }
    return at;
}

std::string get_fresh_id()
{
    return node_create::fresh_name(kStem, [](const std::string& id) {
        if (index_of_goal_id(id) >= 0) {
            return true;
        }
        return std::ranges::any_of(std::array{"_pro", "_start"},
                                   [&](const char* suffix) { return node_create::name_taken(id + suffix); });
    });
}

std::string fresh_title(KindTag type)
{
    // (goal_editor_set_type: the menu's name, then "<name> 2", "<name> 3"... while a goal has it)
    auto taken = [](const std::string& name) {
        return std::ranges::any_of(level_editor_goals_state().goals_,
                                   [&](const Goal& goal) { return same_text(goal.name, name); });
    };
    std::string name = type_title(type);
    for (int suffix = 2; taken(name) && suffix < kMaxNameSuffix; ++suffix) {
        name = std::string(type_title(type)) + " " + std::to_string(suffix);
    }
    return name;
}

void select_point(const Goal& goal, int index)
{
    nodes::mark_stale();
    level_editor_goals_state().select_ = nodes::get_find(get_checksums(point_node(goal, index)));
}

} // namespace

int take_select()
{
    const int select = level_editor_goals_state().select_;
    level_editor_goals_state().select_ = -1;
    return select;
}

std::vector<Goal> get_all()
{
    get_refresh();
    return level_editor_goals_state().goals_;
}

bool is_goal_point(uint32_t node)
{
    int goal = 0;
    int point = 0;
    return point_of(node, {.goal = &goal, .point = &point});
}

bool fabricate(KindTag type)
{
    ViewNow view{};
    if (!current_view(&view)) {
        hud::get_message("No view available");
        return false;
    }
    if (static_cast<int>(level_editor_goals_state().goals_.size()) >= kMaxGoals) {
        hud::get_message("Goal limit reached (10)", kLimitMessageSeconds);
        return false;
    }
    const TickCycle frame = frame_from_yaw_pitch(view.yaw, view.pitch);
    const V3 forward = frame.back * -1.0F;
    V3 base{};
    if (!get_cast(view.pos, view.pos + forward * kPlaceReach, &base)) {
        base = view.pos + forward * kPlaceDistance;
    }
    base = get_grounded(base);
    const V3 flat = make_unit({.x = forward.x, .y = 0.0F, .z = forward.z});
    const V3 right = make_unit(perpendicular({.x = 0.0F, .y = 1.0F, .z = 0.0F}, flat * -1.0F));
    Goal goal = get_defaults(type, get_fresh_id());
    goal.name = fresh_title(type);
    // The pro facing the camera, the start beside it facing away; letters / checkpoints in a line ahead.
    goal.points.push_back({.pos = base, .heading = view.yaw + maths::kPi, .height = 0.0F});
    goal.points.push_back({.pos = get_grounded(base + right * kPointSpacing), .heading = view.yaw, .height = 0.0F});
    const int count = min_points(type);
    for (int k = 2; k < count; ++k) {
        const V3 at = get_grounded(base + flat * (300.0F * static_cast<float>(k - 1)));
        goal.points.push_back({.pos = at, .heading = view.yaw, .height = starting_height(type, k)});
    }
    for (Point& point : goal.points) {
        point.heading = node_edit::wrap_angle(point.heading);
    }
    push_undo();
    level_editor_goals_state().goals_.push_back(goal);
    sync_nodes({goal});
    select_point(goal, 0);
    hud::get_message("Added goal \"" + goal.name + "\"", kAddedMessageSeconds);
    get_log("level editor: goal %s (%s) made at (%.1f, %.1f, %.1f)", goal.id.c_str(), current_type_name(type), base.x,
            base.y, base.z);
    return true;
}

bool get_edit(const std::string& id, const Goal& changed, bool done)
{
    const int index = index_of_goal_id(id);
    if (index < 0) {
        return false;
    }
    if (!level_editor_goals_state().dragging_) {
        push_undo();
    }
    level_editor_goals_state().dragging_ = !done;
    get_refresh();
    Goal goal = changed;
    goal.id = core::at(level_editor_goals_state().goals_, index).id;
    goal.type = core::at(level_editor_goals_state().goals_, index).type;
    goal.name = clean_text(goal.name, kMaxNameChars);
    if (goal.name.empty()) {
        goal.name = core::at(level_editor_goals_state().goals_, index).name;
    }
    goal.ped = clean_text(goal.ped, kMaxPedChars);
    goal.text = clean_text(goal.text, kMaxMessageChars);
    goal.win = clean_text(goal.win, kMaxMessageChars);
    goal.trick = clean_text(goal.trick, kMaxTrickChars);
    // (the points' places are their nodes': only the heights come from the change)
    std::vector<Point> points = core::at(level_editor_goals_state().goals_, index).points;
    for (size_t k = 0; k < points.size() && k < goal.points.size(); ++k) {
        core::at(points, k).height =
            std::clamp(core::at(goal.points, k).height, 0.0F, kMaxPointHeight); // (EditorCam_MaxHeight)
    }
    goal.points = points;
    confine(&goal);
    core::at(level_editor_goals_state().goals_, index) = goal;
    return true;
}

bool drop_out(const std::string& id)
{
    const int index = index_of_goal_id(id);
    if (index < 0) {
        return false;
    }
    push_undo();
    const Goal goal = core::at(level_editor_goals_state().goals_, index);
    for (size_t k = 0; k < goal.points.size(); ++k) {
        remove_node(point_node(goal, static_cast<int>(k)));
    }
    level_editor_goals_state().goals_.erase(level_editor_goals_state().goals_.begin() + index);
    hud::get_message("Deleted goal \"" + goal.name + "\"");
    return true;
}

bool add_checkpoint(const std::string& id)
{
    const int index = index_of_goal_id(id);
    if (index < 0 || core::at(level_editor_goals_state().goals_, index).type != KindTag::kRace ||
        static_cast<int>(core::at(level_editor_goals_state().goals_, index).points.size()) >=
            max_points(KindTag::kRace)) {
        return false;
    }
    push_undo();
    Goal& goal = core::at(level_editor_goals_state().goals_, index);
    const Point& last = goal.points.back();
    const V3 ahead = {.x = std::sin(last.heading), .y = 0.0F, .z = std::cos(last.heading)};
    goal.points.push_back(
        {.pos = get_grounded(last.pos + ahead * kPointSpacing), .heading = last.heading, .height = 0.0F});
    sync_nodes({goal});
    select_point(goal, static_cast<int>(goal.points.size()) - 1);
    return true;
}

bool remove_checkpoint(const std::string& id)
{
    const int index = index_of_goal_id(id);
    if (index < 0 || core::at(level_editor_goals_state().goals_, index).type != KindTag::kRace ||
        static_cast<int>(core::at(level_editor_goals_state().goals_, index).points.size()) <=
            min_points(KindTag::kRace)) {
        return false;
    }
    push_undo();
    Goal& goal = core::at(level_editor_goals_state().goals_, index);
    remove_node(point_node(goal, static_cast<int>(goal.points.size()) - 1));
    goal.points.pop_back();
    return true;
}

bool restore_points(const std::string& id)
{
    const int index = index_of_goal_id(id);
    if (index < 0 || (missing_points(core::at(level_editor_goals_state().goals_, index)) == 0)) {
        return false;
    }
    push_undo();
    sync_nodes({core::at(level_editor_goals_state().goals_, index)});
    return true;
}

bool export_goal(const std::string& id, std::string* message)
{
    const int goal_index = index_of_goal_id(id);
    if (goal_index < 0) {
        *message = "No such goal";
        return false;
    }
    get_refresh();
    const Goal& goal = core::at(level_editor_goals_state().goals_, goal_index);
    std::string why;
    if (!get_complete(goal, &why)) {
        *message = "Not complete: " + why;
        return false;
    }
    // The goal editor's component (the object GoalEditor, CreateGoalEditor at start-up: its goaleditor 0x81F01058).
    auto* editor = core::down_cast<objects::GameEntity>(objects::find_object(get_checksums("GoalEditor")));
    auto* component =
        editor ? core::down_cast<park_editing::ObjectiveAuthoring>(editor->fetch_module(kGoalEditor)) : nullptr;
    const uint32_t level = current_level();
    if (!component || (level == 0U)) {
        *message = "Goal editor unavailable";
        return false;
    }
    // (as NewEditorGoal 0x004EA6C0: the first level_goal_index no goal of this level has, then a free slot; at most 10
    // a level - MaxEditedGoalsReached)
    constexpr int kSlots = 50;
    auto& slots = component->goals();
    auto used = [&](int slot) { return core::at(slots, slot).taken_count(); };
    auto level_of = [&](int slot) { return core::at(slots, slot).level(); };
    auto index_of = [&](int slot) { return core::at(slots, slot).level_goal_number(); };
    int index = -1;
    for (int candidate = 0; candidate < kMaxGoals && index < 0; ++candidate) {
        bool taken = false;
        for (int slot = 0; slot < kSlots; ++slot) {
            taken = taken || (used(slot) && level_of(slot) == level && index_of(slot) == candidate);
        }
        if (!taken) {
            index = candidate;
        }
    }
    int slot = -1;
    for (int slot_index = 0; slot_index < kSlots && slot < 0; ++slot_index) {
        if (!used(slot_index)) {
            slot = slot_index;
        }
    }
    if (index < 0 || slot < 0) {
        *message = index < 0 ? "This level has ten created goals already" : "The goal editor is full (50 goals)";
        return false;
    }
    GoalImage image{};
    fill_image(goal, {.level = level, .level_goal_index_ = index}, gap_ordinals(goal), &image);
    core::at(slots, slot) = image.goal;
    *message = "\"" + goal.name + "\" copied to Create-a-Goal as this level's goal " + std::to_string(index + 1) + "";
    get_log("level editor: goal %s exported to the goal editor (slot %d, CreatedGoal%d)", goal.id.c_str(), slot, index);
    return true;
}

std::vector<Goal> get_capture() { return get_all(); }

int get_apply(const std::vector<Goal>& goals)
{
    int made = 0;
    for (const Goal& goal : goals) {
        if (index_of_goal_id(goal.id) >= 0 || static_cast<int>(level_editor_goals_state().goals_.size()) >= kMaxGoals) {
            continue;
        }
        bool clash = false;
        for (size_t k = 0; k < goal.points.size(); ++k) {
            clash = clash || (node_create::name_taken(point_node(goal, static_cast<int>(k))) &&
                              !node_create::is_added(get_checksums(point_node(goal, static_cast<int>(k)))));
        }
        if (clash) {
            get_log("level editor: goal %s - the level has a node of its points' names already: not made",
                    goal.id.c_str());
            continue;
        }
        level_editor_goals_state().goals_.push_back(goal);
        ++made;
    }
    sync_nodes(level_editor_goals_state().goals_);
    if (made != 0) {
        get_log("level editor: the layout's %d goals put in place", made);
    }
    if (!live_now()) {
        put_in_play();
    }
    return made;
}

void reset_all()
{
    take_out_of_play();
    for (const Goal& goal : level_editor_goals_state().goals_) {
        for (size_t k = 0; k < goal.points.size(); ++k) {
            remove_node(point_node(goal, static_cast<int>(k)));
        }
    }
    level_editor_goals_state().goals_.clear();
}

void forget_level()
{
    // (the level's nodes and objects went with it; a goal of ours the goal manager still has is removed - the game's
    // level change normally takes every goal out first)
    if (modules::Skate::sole()) {
        for (const int index : level_editor_goals_state().in_play_) {
            const uint32_t goal = get_checksums(get_goal_id(index));
            if (goal_exists(goal)) {
                drop_objective(goal);
            }
        }
    }
    level_editor_goals_state().in_play_.clear();
    level_editor_goals_state().goals_.clear();
    level_editor_goals_state().queue_.clear();
    level_editor_goals_state().dragging_ = false;
    level_editor_goals_state().select_ = -1;
}

void kickoff() { take_out_of_play(); }

void halt()
{
    level_editor_goals_state().dragging_ = false;
    put_in_play();
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(level_editor_goals_state().queue_);
    for (auto& action : queue) {
        action();
    }
    get_refresh();
}

void get_queue(std::function<void()> action) { level_editor_goals_state().queue_.push_back(std::move(action)); }
void end_drag() { level_editor_goals_state().dragging_ = false; }
int get_missing(const std::string& id)
{
    const int index = index_of_goal_id(id);
    return index < 0 ? 0 : missing_points(core::at(level_editor_goals_state().goals_, index));
}
bool point_of_node(uint32_t node, std::string* id, int* point)
{
    int goal = 0;
    if (!point_of(node, {.goal = &goal, .point = point})) {
        return false;
    }
    *id = core::at(level_editor_goals_state().goals_, goal).id;
    return true;
}
void choose_one(const std::string& id, int point)
{
    const int index = index_of_goal_id(id);
    if (index >= 0 && point >= 0 &&
        std::cmp_less(point, core::at(level_editor_goals_state().goals_, index).points.size())) {
        select_point(core::at(level_editor_goals_state().goals_, index), point);
    }
}
bool in_play() { return !level_editor_goals_state().in_play_.empty(); }

// The level's gap list's names (the Gap goal's choices).
std::vector<std::string> get_level_gaps()
{
    std::vector<std::string> out;
    modules::Skate* skate = modules::Skate::sole();
    auto* list = skate ? static_cast<goals::GapTally*>(skate->fetch_gap_list()) : nullptr;
    if (!list) {
        return out;
    }
    const int count = list->total_of();
    for (int i = 0; i < count; ++i) {
        out.emplace_back(list->get_gap_title(i));
    }
    return out;
}

void add_overlay(V3 camera_pos, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, OverlayPick overlay_pick, std::vector<ui::Seg>* lines, std::vector<ui::Overlay::OverlayText>* texts)
{
    const auto near_z = overlay_pick.near_z_;
    const auto selected = overlay_pick.selected;
    auto project = [&](V3 point, float* x, float* y) {
        float nx = 0.0F;
        float ny = 0.0F;
        if (!cast_onto(camera_pos, frame, lens, point, near_z, &nx, &ny)) {
            return false;
        }
        ndc_to_pixel(viewport, nx, ny, x, y);
        return true;
    };
    auto line = [&](V3 a, V3 b, uint32_t argb, float thickness) {
        if (!clip_to_near(camera_pos, frame, near_z + kNearPlaneMargin, &a, &b)) {
            return;
        }
        float ax = 0.0F;
        float ay = 0.0F;
        float bx = 0.0F;
        float by = 0.0F;
        if (project(a, &ax, &ay) && project(b, &bx, &by)) {
            lines->push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
        }
    };
    std::string selected_goal;
    int selected_point = -1;
    point_of_node(selected, &selected_goal, &selected_point);
    for (const Goal& goal : level_editor_goals_state().goals_) {
        const bool lit = goal.id == selected_goal;
        const uint32_t colour = lit ? 0xFFFFE070 : 0xFFE0A030;
        for (size_t k = 0; k < goal.points.size(); ++k) {
            const Point& point = core::at(goal.points, k);
            const V3 delta = point.pos - camera_pos;
            if (get_dot(delta, delta) > kMaxDrawReach * kMaxDrawReach) {
                continue;
            }
            const V3 top = point.pos + V3{.x = 0.0F, .y = point.height, .z = 0.0F};
            if (point.height > 0.0F) {
                line(point.pos, top, colour, 1.0F); // (a letter floats its height above its node)
            }
            // (its heading: a short arrow on the ground)
            const V3 ahead = {.x = std::sin(point.heading) * 40.0F, .y = 0.0F, .z = std::cos(point.heading) * 40.0F};
            line(point.pos, point.pos + ahead, colour, lit ? kLitWidth : kUnlitWidth);
            float x = 0.0F;
            float y = 0.0F;
            if (get_dot(delta, delta) < kLabelReach * kLabelReach &&
                project(top + V3{.x = 0.0F, .y = kLabelRise, .z = 0.0F}, &x, &y)) {
                std::string label = point_label(goal.type, static_cast<int>(k));
                if (k == 0) {
                    label.insert(0, goal.name + ": ");
                }
                texts->push_back({.x = x, .y = y, .argb = colour, .text = label});
            }
            // (a race's checkpoints joined in order; the start to the first)
            if (goal.type == KindTag::kRace && k >= 2) {
                const Point& from = k == 2 ? core::at(goal.points, 1) : core::at(goal.points, k - 1);
                line(from.pos + V3{.x = 0.0F, .y = kLinkLift, .z = 0.0F},
                     point.pos + V3{.x = 0.0F, .y = kLinkLift, .z = 0.0F}, lit ? kLitLinkColour : kUnlitLinkColour,
                     1.0F);
            }
        }
        // (the pro to its start)
        if (goal.points.size() >= 2) {
            line(core::at(goal.points, 0).pos, core::at(goal.points, 1).pos, lit ? kLitPairColour : kUnlitPairColour,
                 1.0F);
        }
    }
}

std::string describe()
{
    int complete = 0;
    for (const Goal& goal : level_editor_goals_state().goals_) {
        complete += get_complete(goal) ? 1 : 0;
    }
    std::array<char, kLineTextSize> text{};
    text::format_to(text, "%u goals (%d complete, %u in play)", static_cast<unsigned>(level_editor_goals_state().goals_.size()), complete,
                    static_cast<unsigned>(level_editor_goals_state().in_play_.size()));
    return text.data();
}

} // namespace frontend::level_editor::goal_edit
