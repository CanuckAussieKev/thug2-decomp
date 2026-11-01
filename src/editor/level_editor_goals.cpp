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
#include <vector>

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
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"
#include "engine/objects/compositemgr.h"

namespace frontend::level_editor::goal_edit {

// --- the pure parts -------------------------------------------------------------------------------------------------

namespace {

constexpr float kPi = 3.14159265f;

struct TypeInfo {
    const char* name;  // its checksum's name
    const char* title; // goals_menu_info's
    uint32_t checksum;
};
const TypeInfo kTypeInfo[kTypes] = {
    {"Skate", "SKATE letters", 0x54166ACD},    {"Combo", "COMBO letters", 0x4EC3CFB5},
    {"HighScore", "High Score", 0x6FE44C6D},   {"HighCombo", "High Combo", 0xEC414B76},
    {"Skatetris", "Skate-Tricks", 0x7FE2238A}, {"ComboSkatetris", "Combo Skate-Tricks", 0x0FC6F698},
    {"TrickTris", "Tricktris", 0xEA7BA666},    {"TrickBeat", "Trick to the Beat", 0xD0DD047B},
    {"Race", "Checkpoint / Race", 0x25904450}, {"Gap", "Gap", 0x61C5D092},
};

// goal_editor.qb's cag_key_combos (the sets' texts).
const char* const kComboSets[] = {"Basic Flip Tricks",      "Diagonal Flip Tricks",     "Basic Grab Tricks",
                                  "Diagonal Grab Tricks",   "Double Tap Basic Flips",   "Double Tap Diagonal Flips",
                                  "Double Tap Basic Grabs", "Double Tap Diagonal Grabs"};

const char* const kSkateLetters[] = {"S", "K", "A", "T", "E"};
const char* const kComboLetters[] = {"C", "O", "M", "B", "O"};
const char* const kComboSuffixes[] = {"c", "o", "m", "b", "o2"};

int get_index(KindTag type) { return static_cast<int>(type); }

// The record's numbers.
bool read_numbers(const std::string& text, float* out, int count)
{
    const char* at = text.c_str();
    for (int i = 0; i < count; ++i) {
        char* end = nullptr;
        out[i] = std::strtof(at, &end);
        if (end == at || !std::isfinite(out[i]))
            return false;
        at = end;
        if (i + 1 < count) {
            if (*at != ',')
                return false;
            ++at;
        }
    }
    return *at == '\0';
}

bool read_int(const std::string& text, int* out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    long v = std::strtol(text.c_str(), &end, 10);
    if (*end != '\0' || v < -100000000L || v > 100000000L)
        return false;
    *out = static_cast<int>(v);
    return true;
}

std::vector<std::string> get_split(const std::string& text, char by)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= text.size()) {
        size_t next = text.find(by, at);
        if (next == std::string::npos)
            next = text.size();
        out.push_back(text.substr(at, next - at));
        at = next + 1;
    }
    return out;
}

// The settings kept within Create-a-Goal's menus' limits (goal_editor_menu.qb's number items).
void confine(Goal* g)
{
    g->time = std::clamp(g->time, 15, 3600);
    if (g->type == KindTag::kHighScore || g->type == KindTag::kHighCombo)
        g->score = std::clamp(g->score, 1000, 10000000);
    g->laps = std::clamp(g->laps, 1, 10);
    g->checkpoint_time_ = std::clamp(g->checkpoint_time_, 0, 60);
    g->spin = std::clamp(g->spin, 0, 900) / 180 * 180;
    g->accel_interval_ = std::clamp(g->accel_interval_, 1, 10);
    if (!(g->accel_percent_ >= 0.01f))
        g->accel_percent_ = 0.01f;
    g->accel_percent_ = std::min(g->accel_percent_, 1.0f);
    g->combo_time_ = std::clamp(g->combo_time_, 5, 3000);
    g->stop_at_ = std::clamp(g->stop_at_, 5, std::max(5, g->time - 5));
    g->max_tricks_ = std::clamp(g->max_tricks_, 1, 15);
    g->combo_size_ = std::clamp(g->combo_size_, 1, 15);
    g->block_size_ = std::clamp(g->block_size_, 1, 15);
    g->to_win_ = std::clamp(g->to_win_, 1, 1000);
    g->sets &= (1u << combo_set_count(g->type)) - 1u;
    if (g->gaps.size() > 10)
        g->gaps.resize(10);
}

} // namespace

const char* current_type_name(KindTag type) { return kTypeInfo[get_index(type)].name; }
const char* type_title(KindTag type) { return kTypeInfo[get_index(type)].title; }
uint32_t get_kind_hash(KindTag type) { return kTypeInfo[get_index(type)].checksum; }

bool type_of(const std::string& name, KindTag* type)
{
    for (int i = 0; i < kTypes; ++i) {
        if (_stricmp(kTypeInfo[i].name, name.c_str()) == 0) {
            *type = static_cast<KindTag>(i);
            return true;
        }
    }
    return false;
}

bool kind_of_key(uint32_t checksum, KindTag* type)
{
    for (int i = 0; i < kTypes; ++i) {
        if (kTypeInfo[i].checksum == checksum) {
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
    if (!get_trick_type(type))
        return 0;
    return type == KindTag::kTrickBeat ? 4 : 8; // (create_key_combos_menu: array_size - 4 for TrickBeat)
}

const char* combo_set_name(int set) { return set >= 0 && set < 8 ? kComboSets[set] : ""; }

// change_species (0x004E6DA0): the pro and the start always; SKATE: 7 (and 11 letter entries from 2), COMBO: 7 (letter
// entries 2..6), Race: 4 to 12 (11 checkpoint entries from 2), every other type: 2.
int min_points(KindTag type)
{
    switch (type) {
    case KindTag::kSkate:
    case KindTag::kCombo:
        return 7;
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
        return 7;
    case KindTag::kRace:
        return 12;
    default:
        return 2;
    }
}

int point_kind(KindTag type, int index)
{
    if (index == 0)
        return 1;
    if (index == 1)
        return 2;
    if (index < 2 || index >= kMaxPoints)
        return 0;
    switch (type) {
    case KindTag::kSkate:
        return 3;
    case KindTag::kCombo:
        return index < 7 ? 3 : 0;
    case KindTag::kRace:
        return 4;
    default:
        return 0;
    }
}

std::string point_label(KindTag type, int index)
{
    if (index == 0)
        return "Pro";
    if (index == 1)
        return "Start";
    if (index >= 2 && index < 7 && type == KindTag::kSkate)
        return kSkateLetters[index - 2];
    if (index >= 2 && index < 7 && type == KindTag::kCombo)
        return kComboLetters[index - 2];
    return "Checkpoint " + std::to_string(index - 1);
}

std::string point_suffix(KindTag type, int index)
{
    if (index == 0)
        return "pro";
    if (index == 1)
        return "start";
    if (index >= 2 && index < 7 && type == KindTag::kSkate) {
        std::string letter = kSkateLetters[index - 2];
        return std::string(
            1, static_cast<char>(letter[0] - 'A' +
                                 'a')); // NOLINT(modernize-return-braced-init-list): {1, c} would be two characters
    }
    if (index >= 2 && index < 7 && type == KindTag::kCombo)
        return kComboSuffixes[index - 2];
    return "cp" + std::to_string(index - 1);
}

float starting_height(KindTag type, int index) { return point_kind(type, index) == 3 ? 27.0f : 0.0f; }

bool get_same(const Goal& a, const Goal& b)
{
    if (a.id != b.id || a.type != b.type || a.name != b.name || a.ped != b.ped || a.text != b.text || a.win != b.win ||
        a.time != b.time || a.score != b.score || a.walk != b.walk || a.laps != b.laps ||
        a.checkpoint_time_ != b.checkpoint_time_ || a.gaps != b.gaps || a.trick != b.trick || a.sets != b.sets ||
        a.spin != b.spin || a.accel_interval_ != b.accel_interval_ || a.accel_percent_ != b.accel_percent_ ||
        a.combo_time_ != b.combo_time_ || a.stop_at_ != b.stop_at_ || a.max_tricks_ != b.max_tricks_ ||
        a.single_combo_ != b.single_combo_ || a.combo_size_ != b.combo_size_ || a.block_size_ != b.block_size_ ||
        a.to_win_ != b.to_win_ || a.points.size() != b.points.size())
        return false;
    for (size_t i = 0; i < a.points.size(); ++i) {
        const Point &p = a.points[i], &q = b.points[i];
        if (p.pos.x != q.pos.x || p.pos.y != q.pos.y || p.pos.z != q.pos.z || p.heading != q.heading ||
            p.height != q.height)
            return false;
    }
    return true;
}

Goal get_defaults(KindTag type, const std::string& id)
{
    Goal g;
    g.id = id;
    g.type = type;
    g.name = type_title(type);
    // (change_species reads score from the type's generic parameters: Goal_HighScore_genericParams 15000,
    // goal_highcombo_genericParams 10000; init_edited_goal's 100 otherwise)
    g.score = type == KindTag::kHighScore ? 15000 : type == KindTag::kHighCombo ? 10000 : 100;
    if (type == KindTag::kTrickBeat)
        g.spin = 0; // (change_species)
    return g;
}

bool get_complete(const Goal& goal, std::string* why)
{
    auto fail = [&](const std::string& text) {
        if (why)
            *why = text;
        return false;
    };
    int count = static_cast<int>(goal.points.size());
    if (count < min_points(goal.type))
        return fail("It needs " + std::to_string(min_points(goal.type)) + " points (has " + std::to_string(count) +
                    ")");
    if (count > max_points(goal.type))
        return fail("Too many points");
    if (goal.type == KindTag::kGap && goal.gaps.empty())
        return fail("Pick at least one gap");
    if (get_trick_type(goal.type) && goal.sets == 0)
        return fail("Choose at least one key combo set");
    return true;
}

std::string clean_text(const std::string& text, size_t limit)
{
    std::string out;
    for (char c : text) {
        auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || c == '"' || c == '#' || c == '|')
            continue;
        if (out.size() >= limit)
            break;
        out.push_back(c);
    }
    size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos)
        return "";
    size_t last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1);
}

std::string write_record(const Goal& g)
{
    std::string out = "goal " + g.id + " type=" + current_type_name(g.type);
    auto text = [&](const char* key, const std::string& value) {
        out += std::string(" ") + key + "=\"" + value + "\"";
    };
    auto number = [&](const char* key, int value) { out += std::string(" ") + key + "=" + std::to_string(value); };
    text("name", g.name);
    text("ped", g.ped);
    text("text", g.text);
    text("win", g.win);
    number("time", g.time);
    number("score", g.score);
    out += std::string(" control=") + (g.walk ? "Walk" : "Skate");
    char buffer[200];
    for (size_t i = 0; i < g.points.size(); ++i) {
        const Point& p = g.points[i];
        std::snprintf(buffer, sizeof(buffer), " p%u=%.9g,%.9g,%.9g,%.9g,%.9g", static_cast<unsigned>(i), p.pos.x,
                      p.pos.y, p.pos.z, p.heading, p.height);
        out += buffer;
    }
    if (g.type == KindTag::kRace) {
        number("laps", g.laps);
        number("cptime", g.checkpoint_time_);
    }
    if (g.type == KindTag::kGap) {
        std::string gaps;
        for (size_t i = 0; i < g.gaps.size(); ++i)
            gaps += (i ? "|" : "") + g.gaps[i];
        text("gaps", gaps);
        text("trick", g.trick);
    }
    if (get_trick_type(g.type)) {
        std::string sets;
        for (int i = 0; i < 8; ++i) {
            if (g.sets & (1u << i))
                sets += (sets.empty() ? "" : ",") + std::to_string(i);
        }
        out += " sets=" + (sets.empty() ? std::string("none") : sets);
        number("spin", g.spin);
        number("accel", g.accel_interval_);
        std::snprintf(buffer, sizeof(buffer), " accelpct=%.9g", g.accel_percent_);
        out += buffer;
        number("tricktime", g.combo_time_);
        number("stopat", g.stop_at_);
        number("maxtricks", g.max_tricks_);
        number("single", g.single_combo_ ? 1 : 0);
        number("combo", g.combo_size_);
        number("block", g.block_size_);
        number("towin", g.to_win_);
    }
    return out.substr(5); // (after "goal ")
}

bool read_record(const std::string& text, Goal* out)
{
    // Tokens: the id, then key=value with a value either up to the next space or "quoted".
    size_t at = 0;
    auto skip = [&] {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r'))
            ++at;
    };
    skip();
    size_t end = text.find_first_of(" \t\r", at);
    std::string id = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (!node_create::valid_name(id) || id.size() > 40)
        return false;
    at = end == std::string::npos ? text.size() : end;
    Goal g;
    g.id = id;
    std::vector<std::string> seen;
    std::vector<std::pair<int, Point>> points;
    bool have_type = false;
    while (true) {
        skip();
        if (at >= text.size())
            break;
        size_t equals = text.find('=', at);
        if (equals == std::string::npos || equals == at)
            return false;
        std::string key = text.substr(at, equals - at);
        if (key.find_first_of(" \t\"") != std::string::npos)
            return false;
        at = equals + 1;
        std::string value;
        if (at < text.size() && text[at] == '"') {
            size_t close = text.find('"', at + 1);
            if (close == std::string::npos)
                return false;
            value = text.substr(at + 1, close - at - 1);
            at = close + 1;
            if (at < text.size() && text[at] != ' ' && text[at] != '\t' && text[at] != '\r')
                return false; // (text after the closing quote)
        } else {
            size_t stop = text.find_first_of(" \t\r", at);
            value = text.substr(at, stop == std::string::npos ? std::string::npos : stop - at);
            at = stop == std::string::npos ? text.size() : stop;
        }
        if (std::find(seen.begin(), seen.end(), key) != seen.end())
            return false;
        seen.push_back(key);
        bool ok = true;
        if (key == "type") {
            ok = type_of(value, &g.type);
            have_type = ok;
        } else if (key == "name") {
            g.name = clean_text(value, 49);
        } else if (key == "ped") {
            g.ped = clean_text(value, 20);
        } else if (key == "text") {
            g.text = clean_text(value, 99);
        } else if (key == "win") {
            g.win = clean_text(value, 99);
        } else if (key == "trick") {
            g.trick = clean_text(value, 49);
        } else if (key == "gaps") {
            g.gaps.clear();
            if (!value.empty()) {
                for (const std::string& gap : get_split(value, '|')) {
                    std::string clean = clean_text(gap, 60);
                    if (!clean.empty())
                        g.gaps.push_back(clean);
                }
            }
        } else if (key == "control") {
            ok = value == "Skate" || value == "Walk";
            g.walk = value == "Walk";
        } else if (key == "sets") {
            g.sets = 0;
            if (value != "none") {
                for (const std::string& s : get_split(value, ',')) {
                    int set = 0;
                    ok = ok && read_int(s, &set) && set >= 0 && set < 8;
                    if (ok)
                        g.sets |= 1u << set;
                }
            }
        } else if (key == "accelpct") {
            ok = read_numbers(value, &g.accel_percent_, 1);
        } else if (key.size() >= 2 && key[0] == 'p' && key.find_first_not_of("0123456789", 1) == std::string::npos) {
            int index = 0;
            float v[5];
            ok = read_int(key.substr(1), &index) && index >= 0 && index < kMaxPoints && read_numbers(value, v, 5);
            if (ok)
                points.emplace_back(index, Point{{v[0], v[1], v[2]}, v[3], v[4]});
        } else {
            struct IntKey {
                const char* key;
                int* field;
            } ints[] = {{"time", &g.time},
                        {"score", &g.score},
                        {"laps", &g.laps},
                        {"cptime", &g.checkpoint_time_},
                        {"spin", &g.spin},
                        {"accel", &g.accel_interval_},
                        {"tricktime", &g.combo_time_},
                        {"stopat", &g.stop_at_},
                        {"maxtricks", &g.max_tricks_},
                        {"combo", &g.combo_size_},
                        {"block", &g.block_size_},
                        {"towin", &g.to_win_}};
            bool known = false;
            for (const IntKey& k : ints) {
                if (key == k.key) {
                    known = true;
                    ok = read_int(value, k.field);
                }
            }
            if (key == "single") {
                int single = 0;
                ok = read_int(value, &single) && (single == 0 || single == 1);
                g.single_combo_ = single == 1;
            } // (another key: a later version's - skipped)
            (void)known;
        }
        if (!ok)
            return false;
    }
    if (!have_type)
        return false;
    // (the points: p0.. with none missing, within the type's most)
    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i].first != static_cast<int>(i))
            return false;
        g.points.push_back(points[i].second);
    }
    if (static_cast<int>(g.points.size()) > max_points(g.type))
        return false;
    confine(&g);
    *out = g;
    return true;
}

float get_image_angle(float heading) { return heading + kPi; }
float get_heading_of(float image_angle) { return image_angle - kPi; }

void fill_image(const Goal& goal, uint32_t level, int level_goal_index, const std::vector<int>& gap_numbers,
                GoalImage* out)
{
    uint8_t* b = out->bytes;
    std::memset(b, 0, kTexelEdge);
    auto u32 = [b](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(b + offset); };
    auto f32 = [b](uint32_t offset) -> float& { return *reinterpret_cast<float*>(b + offset); };
    auto text = [b](uint32_t offset, const std::string& value, size_t room) {
        size_t n = std::min(value.size(), room - 1);
        std::memcpy(b + offset, value.data(), n);
        b[offset + n] = 0;
    };
    // decode_from_struct (0x004E96D0): init_edited_goal, used, not won, level, level_goal_index, the type (change_species: the
    // points' kinds, the counts; the pro's / start's height 0), then the fields.
    park_editing::init_edited_goal(b);
    b[0x28A] = 1;
    b[0x289] = 0;
    u32(0x0) = level;
    u32(0x28C) = static_cast<uint32_t>(level_goal_index);
    u32(0x114) = get_kind_hash(goal.type);
    for (int i = 0; i < kMaxPoints; ++i) {
        u32(0x118 + 0x1C * i) = static_cast<uint32_t>(point_kind(goal.type, i));
        f32(0x12C + 0x1C * i) = 0.0f;
    }
    b[0x285] = static_cast<uint8_t>(min_points(goal.type));
    b[0x284] = static_cast<uint8_t>(max_points(goal.type));
    if (goal.type == KindTag::kTrickBeat)
        u32(0x2B8) = 0;
    u32(0x298) = static_cast<uint32_t>(goal.score);
    u32(0x290) = static_cast<uint32_t>(goal.time);
    u32(0x294) = goal.walk ? 0x726E85AAu : 0x54166ACDu; // (control_type: Walk / Skate)
    text(0x4, goal.name, 0x36 - 0x4);
    text(0x4B, goal.text, 0xAF - 0x4B);
    text(0xAF, goal.win, 0x113 - 0xAF);
    text(0x36, goal.ped, 0x4B - 0x36);
    int count = std::min(static_cast<int>(goal.points.size()), max_points(goal.type));
    b[0x286] = static_cast<uint8_t>(count);
    for (int i = 0; i < count; ++i) {
        const Point& p = goal.points[i];
        uint32_t at = 0x11C + 0x1C * i;
        f32(at) = p.pos.x;
        f32(at + 4) = p.pos.y;
        f32(at + 8) = p.pos.z;
        f32(at + 12) = 1.0f;
        f32(at + 16) = p.height;
        f32(at + 20) = get_image_angle(p.heading);
    }
    // The type's own (set_goal_specific_params' fields, 0x004E7590).
    switch (goal.type) {
    case KindTag::kRace:
        u32(0x29C) = static_cast<uint32_t>(goal.laps);
        u32(0x2A0) = static_cast<uint32_t>(goal.checkpoint_time_);
        break;
    case KindTag::kGap: {
        text(0x303, goal.trick, kTexelEdge - 0x303);
        int n = 0;
        for (int number : gap_numbers) {
            if (n >= 10 || number < 0 || number > 255)
                continue;
            b[0x2F8 + n++] = static_cast<uint8_t>(number);
        }
        b[0x302] = static_cast<uint8_t>(n);
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
            u32(0x2C0) = static_cast<uint32_t>(goal.combo_size_);
            if (goal.single_combo_)
                b[0x2BC] = 1;
        }
        if (goal.type == KindTag::kTrickTris) {
            u32(0x2C4) = static_cast<uint32_t>(goal.block_size_);
            u32(0x2C8) = static_cast<uint32_t>(goal.to_win_);
        }
        if (goal.type != KindTag::kTrickBeat) {
            u32(0x2A4) = static_cast<uint32_t>(goal.accel_interval_);
            f32(0x2A8) = goal.accel_percent_;
            u32(0x2AC) = static_cast<uint32_t>(goal.combo_time_);
            u32(0x2B0) = static_cast<uint32_t>(goal.stop_at_);
            u32(0x2B4) = static_cast<uint32_t>(goal.max_tricks_);
            u32(0x2B8) = static_cast<uint32_t>(goal.spin);
        }
        // (the key combo sets as AddKeyComboSet set_index=<i> adds them - 0x004E6C40: {set, require_perfect 0, spin
        // 0, num_taps 0})
        int sets = 0;
        for (int i = 0; i < combo_set_count(goal.type) && sets < 10; ++i) {
            if (!(goal.sets & (1u << i)))
                continue;
            b[0x2CC + 4 * sets] = static_cast<uint8_t>(i);
            ++sets;
        }
        u32(0x2F4) = static_cast<uint32_t>(sets);
    }
}

std::string play_node_name(int which, int index)
{
    char name[64];
    if (which == 0)
        std::snprintf(name, sizeof(name), "TRG_EditedGoal%d_Pro", index);
    else if (which == 1)
        std::snprintf(name, sizeof(name), "TRG_EditedGoal%d_Restart", index);
    else
        std::snprintf(name, sizeof(name), "TRG_EditedGoal%d_Object_%d", index, which - 2);
    return name;
}

std::string get_goal_id(int index) { return "CreatedGoal" + std::to_string(index); }

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

using Function = bool (*)(script::Struct*, script::Script*);

std::vector<Goal> g_goals;  // (their points' places: as their nodes had them when last read)
std::vector<int> g_in_play; // the play indexes in the goal manager now
std::vector<std::function<void()>> g_queue;
bool g_dragging;   // a field being dragged: one undo step
int g_select = -1; // a node to select (Nodes::All() index)

constexpr const char* kStem = "ed_goal";
constexpr const char* kGoalMarkerType = "ed_goal";

bool same_text(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; }
uint32_t get_checksums(const std::string& text) { return checksums::hash_from_text(text.c_str()); }

std::string point_node(const Goal& g, int index) { return g.id + "_" + point_suffix(g.type, index); }

int index_of_goal_id(const std::string& id)
{
    for (size_t i = 0; i < g_goals.size(); ++i)
        if (g_goals[i].id == id)
            return static_cast<int>(i);
    return -1;
}

// The goal and point a node is (false: not one of ours).
bool point_of(uint32_t node, int* goal, int* point)
{
    for (size_t i = 0; i < g_goals.size(); ++i) {
        for (size_t k = 0; k < g_goals[i].points.size(); ++k) {
            if (get_checksums(point_node(g_goals[i], static_cast<int>(k))) == node) {
                *goal = static_cast<int>(i);
                *point = static_cast<int>(k);
                return true;
            }
        }
    }
    return false;
}

node_create::Spec point_spec(const Goal& g, int index)
{
    node_create::Spec s;
    s.name = point_node(g, index);
    s.cls = "GenericNode";
    s.type = kGoalMarkerType;
    s.pos = g.points[index].pos;
    s.angles = {0.0f, g.points[index].heading, 0.0f};
    s.owned = true;
    return s;
}

// The goals' places read again from their nodes (a node deleted: its last place kept - Restore points makes it again).
void get_refresh()
{
    for (Goal& g : g_goals) {
        for (size_t k = 0; k < g.points.size(); ++k) {
            node_edit::Values v;
            if (node_edit::get_take_in(get_checksums(point_node(g, static_cast<int>(k))), &v)) {
                g.points[k].pos = v.pos;
                g.points[k].heading = v.angles.y;
            }
        }
    }
}

int missing_points(const Goal& g)
{
    int missing = 0;
    for (size_t k = 0; k < g.points.size(); ++k)
        missing += node_create::is_added(get_checksums(point_node(g, static_cast<int>(k)))) ? 0 : 1;
    return missing;
}

// Every point node of the goals made where missing / put where the goals have them.
void sync_nodes(const std::vector<Goal>& goals)
{
    std::string message;
    node_create::LinkChain make;
    for (const Goal& g : goals) {
        for (size_t k = 0; k < g.points.size(); ++k) {
            uint32_t name = get_checksums(point_node(g, static_cast<int>(k)));
            if (!node_create::is_added(name)) {
                if (!node_create::name_taken(point_node(g, static_cast<int>(k))))
                    make.push_back(point_spec(g, static_cast<int>(k)));
                continue;
            }
            node_edit::Values v;
            v.pos = g.points[k].pos;
            v.angles = {0.0f, g.points[k].heading, 0.0f};
            node_edit::Values now;
            if (!node_edit::get_take_in(name, &now) || !node_edit::get_same(now, v))
                node_edit::assign_value(name, v);
        }
    }
    if (!make.empty())
        node_create::create_all(make, &message);
}

void remove_node(const std::string& name)
{
    uint32_t checksum = get_checksums(name);
    if (!node_create::is_added(checksum))
        return;
    node_create::Spec removed;
    int at = -1;
    node_create::drop_out(checksum, &removed, &at);
    node_edit::get_forget(checksum);
}

// Every node of ours of type ed_goal that no goal of `goals` has: removed.
void remove_stray_points(const std::vector<Goal>& goals)
{
    for (const node_create::Spec& spec : node_create::at_hand()) {
        if (!same_text(spec.type, kGoalMarkerType) || !same_text(spec.cls, "GenericNode"))
            continue;
        bool kept = false;
        for (const Goal& g : goals)
            for (size_t k = 0; k < g.points.size() && !kept; ++k)
                kept = same_text(point_node(g, static_cast<int>(k)), spec.name);
        if (!kept)
            remove_node(spec.name);
    }
}

// --- undo: the goals as they are (their points' places too) ---

struct Step : ModuleStep {
    std::vector<Goal> goals;
    void get_restore() override
    {
        remove_stray_points(goals);
        g_goals = goals;
        sync_nodes(goals);
    }
    std::shared_ptr<ModuleStep> get_now() const override
    {
        get_refresh();
        auto now = std::make_shared<Step>();
        now->goals = g_goals;
        return now;
    }
};

void push_undo()
{
    get_refresh();
    auto step = std::make_shared<Step>();
    step->goals = g_goals;
    push_module_undo(step);
}

// --- the goal manager (the skate module's - 0x005591B0 - through its script commands) ---

modules::Skate* skate_module() { return modules::g_skate_module; }

// The current level as the goal editor keeps it (modules::Skate +0x378; Load_Sk5Ed_gameplay counts as Load_Sk5Ed - as
// 0x004E7D00 / 0x004EC010 / 0x004EA6C0 do).
uint32_t current_level()
{
    modules::Skate* skate = skate_module();
    if (!skate)
        return 0;
    uint32_t level = skate->level_;
    return level == 0xB664035D ? 0xE8B4B836 : level;
}

// A GoalManager_* script command with name=<goal> (they take no other parameter and don't use the script).
bool goal_command(Function command, uint32_t goal)
{
    auto* params = new (script::spawn_param_bag()) script::Struct;
    params->put_name_key(0xA1DC81F9u, goal); // name
    bool result = command(params, nullptr);
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
    auto* object = static_cast<objects::GameEntity*>(objects::find_object(get_checksums(name)));
    if (object && !(object->object_flags_ & objects::kObjectFlagDead))
        object->flag_as_dead();
}

// The play nodes of a goal (EditedGoal_Pro_Node / _Restart_Node / _Object_Node's fields, placed as
// write_edited_goal_node_positions 0x004E7A80 places them: Pos + (0, height, 0), Angles (0, angle - pi, 0), a letter's Type
// and model).
node_create::LinkChain play_nodes(const Goal& g, int index)
{
    node_create::LinkChain out;
    auto field = [](node_create::Prop::KindTag type, const char* key) {
        node_create::Prop f = {type, key};
        return f;
    };
    auto name = [&](const char* key, const char* value) {
        node_create::Prop f = field(node_create::Prop::kName, key);
        f.text = value;
        return f;
    };
    auto integer = [&](const char* key, int value) {
        node_create::Prop f = field(node_create::Prop::kInteger, key);
        f.integer = value;
        return f;
    };
    auto string = [&](const char* key, const char* value) {
        node_create::Prop f = field(node_create::Prop::kString, key);
        f.text = value;
        return f;
    };
    const float pi = objects::g_pi; // (the constant 0x004E79C0 subtracts)
    for (size_t k = 0; k < g.points.size(); ++k) {
        const Point& p = g.points[k];
        node_create::Spec s;
        s.name = play_node_name(static_cast<int>(k), index);
        s.pos = {p.pos.x, p.pos.y + p.height, p.pos.z};
        s.angles = {0.0f, get_image_angle(p.heading) - pi, 0.0f};
        s.owned = true;
        int kind = point_kind(g.type, static_cast<int>(k));
        if (kind == 1) {
            s.cls = "Pedestrian";
            s.type = "Ped_From_Profile";
            s.extra = {name("SkeletonName", "THPS6_human"),
                       name("DefaultAnimName", "WStandIdle1"),
                       name("PedAI_Type", "Stand"),
                       name("AnimName", "animload_THPS6_human"),
                       name("profile", "random_male_profile"),
                       integer("SuspendDistance", 3000),
                       integer("lod_dist1", 200),
                       integer("lod_dist2", 400)};
        } else if (kind == 2) {
            s.cls = "Restart";
            s.type = "Player1";
            node_create::Prop types = field(node_create::Prop::kNames, "restart_types");
            types.list = {"Player1"};
            node_create::Prop created = field(node_create::Prop::kFlag, "");
            created.text = "CreatedAtStart";
            s.extra = {created, string("RestartName", "Edited goal: Restart"), types};
        } else {
            s.cls = "GameObject";
            uint32_t kind_hash = get_kind_hash(g.type);
            const char* model = park_editing::goal_object_model(kind_hash, static_cast<int>(k) - 2);
            static const char* const kLetterTypes[2][5] = {{"Letter_S", "Letter_K", "Letter_A", "Letter_T", "Letter_E"},
                                                           {"Combo_C", "Combo_O", "Combo_M", "Combo_B", "Combo_O"}};
            if (kind == 3 && k >= 2 && k < 7)
                s.type = kLetterTypes[g.type == KindTag::kCombo ? 1 : 0][k - 2];
            s.extra = {integer("SuspendDistance", 0), integer("lod_dist1", 400), integer("lod_dist2", 401)};
            if (model)
                s.extra.push_back(string("model", model));
        }
        out.push_back(s);
    }
    return out;
}

// The gaps' numbers in the level's gap list now (the skate module's checklist, 0x005656C0).
std::vector<int> gap_ordinals(const Goal& g)
{
    std::vector<int> out;
    modules::Skate* skate = skate_module();
    auto* list = skate ? static_cast<goals::GapTally*>(skate->fetch_gap_list()) : nullptr;
    if (!list)
        return out;
    for (const std::string& gap : g.gaps) {
        int number = list->find_gap(gap.c_str());
        if (number >= 0)
            out.push_back(number);
        else
            get_log("level editor: goal %s - no gap \"%s\" in the level's gap list: left out", g.id.c_str(),
                    gap.c_str());
    }
    return out;
}

void take_out_of_play()
{
    if (g_in_play.empty())
        return;
    for (int index : g_in_play) {
        uint32_t goal = get_checksums(get_goal_id(index));
        if (skate_module() && goal_exists(goal)) {
            if (is_goal_running(goal))
                switch_off_goal(goal);
            drop_objective(goal);
        }
        // (edit_goal_menu_exit: <pro_name>:Die - and a letter / cone still standing)
        for (int which = 0; which < kMaxPoints; ++which) {
            remove_entity(play_node_name(which, index));
            remove_node(play_node_name(which, index));
        }
    }
    get_log("level editor: %u goals taken out of play", static_cast<unsigned>(g_in_play.size()));
    g_in_play.clear();
}

void put_in_play()
{
    take_out_of_play();
    std::string why;
    if (live_now() || !layout::editing_copy() || layout::get_refused(&why) || !skate_module() || g_goals.empty())
        return;
    get_refresh();
    uint32_t level = current_level();
    node_create::LinkChain nodes;
    std::vector<std::pair<int, const Goal*>> play;
    for (size_t i = 0; i < g_goals.size() && static_cast<int>(i) < kMaxGoals; ++i) {
        const Goal& g = g_goals[i];
        std::string incomplete;
        if (!get_complete(g, &incomplete)) {
            get_log("level editor: goal %s (%s) not played - %s", g.id.c_str(), g.name.c_str(), incomplete.c_str());
            continue;
        }
        int index = play_index(static_cast<int>(i));
        bool taken = false;
        for (const node_create::Spec& s : play_nodes(g, index)) {
            if (node_create::name_taken(s.name))
                taken = true;
        }
        if (taken || goal_exists(get_checksums(get_goal_id(index)))) {
            get_log("level editor: goal %s not played - %s is taken", g.id.c_str(), get_goal_id(index).c_str());
            continue;
        }
        node_create::LinkChain made = play_nodes(g, index);
        nodes.insert(nodes.end(), made.begin(), made.end());
        play.emplace_back(index, &g);
    }
    if (play.empty())
        return;
    std::string message;
    node_create::create_all(nodes, &message);
    for (const auto& [index, goal] : play) {
        // Create-a-Goal's own: AddEditedGoalToGoalManager (0x004EB6B0 on the record, MarkUnbeaten), then
        // GoalManager_InitializeGoal (its pro made from TRG_EditedGoal<n>_Pro).
        GoalImage image;
        fill_image(*goal, level, index, gap_ordinals(*goal), &image);
        reinterpret_cast<park_editing::EditedGoal*>(image.bytes)->add_to_goal_manager(true);
        uint32_t id = get_checksums(get_goal_id(index));
        if (!goal_exists(id)) {
            get_log("level editor: goal %s - the goal manager didn't take it", goal->id.c_str());
            continue;
        }
        initialize_goal(id);
        g_in_play.push_back(index);
        get_log("level editor: goal %s (%s, \"%s\") in play as %s", goal->id.c_str(), current_type_name(goal->type),
                goal->name.c_str(), get_goal_id(index).c_str());
    }
}

// Where the view looks, on the ground (the game's collision ray - a ProbeRay, as the triggers' Cast).
bool get_cast(V3 from, V3 to, V3* hit)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    maths::Vec4 start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    const float* point = &ray->hit_.point.x;
    *hit = {point[0], point[1], point[2]};
    return true;
}

V3 get_grounded(V3 at)
{
    V3 ground;
    if (get_cast(at + V3{0.0f, 200.0f, 0.0f}, at - V3{0.0f, 5000.0f, 0.0f}, &ground))
        return ground;
    return at;
}

std::string get_fresh_id()
{
    return node_create::fresh_name(kStem, [](const std::string& id) {
        if (index_of_goal_id(id) >= 0)
            return true;
        for (const char* suffix : {"_pro", "_start"})
            if (node_create::name_taken(id + suffix))
                return true;
        return false;
    });
}

std::string fresh_title(KindTag type)
{
    // (goal_editor_set_type: the menu's name, then "<name> 2", "<name> 3"... while a goal has it)
    auto taken = [](const std::string& name) {
        return std::any_of(g_goals.begin(), g_goals.end(), [&](const Goal& g) { return same_text(g.name, name); });
    };
    std::string name = type_title(type);
    for (int suffix = 2; taken(name) && suffix < 100; ++suffix)
        name = std::string(type_title(type)) + " " + std::to_string(suffix);
    return name;
}

void select_point(const Goal& g, int index)
{
    nodes::mark_stale();
    g_select = nodes::get_find(get_checksums(point_node(g, index)));
}

} // namespace

int take_select()
{
    int select = g_select;
    g_select = -1;
    return select;
}

std::vector<Goal> get_all()
{
    get_refresh();
    return g_goals;
}

bool is_goal_point(uint32_t node)
{
    int goal, point;
    return point_of(node, &goal, &point);
}

bool fabricate(KindTag type)
{
    ViewNow view;
    if (!current_view(&view)) {
        hud::get_message("No view available");
        return false;
    }
    if (static_cast<int>(g_goals.size()) >= kMaxGoals) {
        hud::get_message("Goal limit reached (10)", 4.0);
        return false;
    }
    TickCycle frame = frame_from_yaw_pitch(view.yaw, view.pitch);
    V3 forward = frame.back * -1.0f;
    V3 base;
    if (!get_cast(view.pos, view.pos + forward * 4000.0f, &base))
        base = view.pos + forward * 600.0f;
    base = get_grounded(base);
    V3 flat = make_unit({forward.x, 0.0f, forward.z});
    V3 right = make_unit(perpendicular({0.0f, 1.0f, 0.0f}, flat * -1.0f));
    Goal g = get_defaults(type, get_fresh_id());
    g.name = fresh_title(type);
    // The pro facing the camera, the start beside it facing away; letters / checkpoints in a line ahead.
    g.points.push_back({base, view.yaw + kPi, 0.0f});
    g.points.push_back({get_grounded(base + right * 150.0f), view.yaw, 0.0f});
    int count = min_points(type);
    for (int k = 2; k < count; ++k) {
        V3 at = get_grounded(base + flat * (300.0f * static_cast<float>(k - 1)));
        g.points.push_back({at, view.yaw, starting_height(type, k)});
    }
    for (Point& p : g.points) {
        p.heading = node_edit::wrap_angle(p.heading);
    }
    push_undo();
    g_goals.push_back(g);
    sync_nodes({g});
    select_point(g, 0);
    hud::get_message("Added goal \"" + g.name + "\"", 5.0);
    get_log("level editor: goal %s (%s) made at (%.1f, %.1f, %.1f)", g.id.c_str(), current_type_name(type), base.x,
            base.y, base.z);
    return true;
}

bool get_edit(const std::string& id, const Goal& changed, bool done)
{
    int i = index_of_goal_id(id);
    if (i < 0)
        return false;
    if (!g_dragging)
        push_undo();
    g_dragging = !done;
    get_refresh();
    Goal g = changed;
    g.id = g_goals[i].id;
    g.type = g_goals[i].type;
    g.name = clean_text(g.name, 49);
    if (g.name.empty())
        g.name = g_goals[i].name;
    g.ped = clean_text(g.ped, 20);
    g.text = clean_text(g.text, 99);
    g.win = clean_text(g.win, 99);
    g.trick = clean_text(g.trick, 49);
    // (the points' places are their nodes': only the heights come from the change)
    std::vector<Point> points = g_goals[i].points;
    for (size_t k = 0; k < points.size() && k < g.points.size(); ++k)
        points[k].height = std::clamp(g.points[k].height, 0.0f, 1000.0f); // (EditorCam_MaxHeight)
    g.points = points;
    confine(&g);
    g_goals[i] = g;
    return true;
}

bool drop_out(const std::string& id)
{
    int i = index_of_goal_id(id);
    if (i < 0)
        return false;
    push_undo();
    Goal g = g_goals[i];
    for (size_t k = 0; k < g.points.size(); ++k)
        remove_node(point_node(g, static_cast<int>(k)));
    g_goals.erase(g_goals.begin() + i);
    hud::get_message("Deleted goal \"" + g.name + "\"");
    return true;
}

bool add_checkpoint(const std::string& id)
{
    int i = index_of_goal_id(id);
    if (i < 0 || g_goals[i].type != KindTag::kRace ||
        static_cast<int>(g_goals[i].points.size()) >= max_points(KindTag::kRace))
        return false;
    push_undo();
    Goal& g = g_goals[i];
    const Point& last = g.points.back();
    V3 ahead = {std::sin(last.heading), 0.0f, std::cos(last.heading)};
    g.points.push_back({get_grounded(last.pos + ahead * 300.0f), last.heading, 0.0f});
    sync_nodes({g});
    select_point(g, static_cast<int>(g.points.size()) - 1);
    return true;
}

bool remove_checkpoint(const std::string& id)
{
    int i = index_of_goal_id(id);
    if (i < 0 || g_goals[i].type != KindTag::kRace ||
        static_cast<int>(g_goals[i].points.size()) <= min_points(KindTag::kRace))
        return false;
    push_undo();
    Goal& g = g_goals[i];
    remove_node(point_node(g, static_cast<int>(g.points.size()) - 1));
    g.points.pop_back();
    return true;
}

bool restore_points(const std::string& id)
{
    int i = index_of_goal_id(id);
    if (i < 0 || !missing_points(g_goals[i]))
        return false;
    push_undo();
    sync_nodes({g_goals[i]});
    return true;
}

bool export_goal(const std::string& id, std::string* message)
{
    int i = index_of_goal_id(id);
    if (i < 0) {
        *message = "No such goal";
        return false;
    }
    get_refresh();
    const Goal& g = g_goals[i];
    std::string why;
    if (!get_complete(g, &why)) {
        *message = "Not complete: " + why;
        return false;
    }
    // The goal editor's component (the object GoalEditor, CreateGoalEditor at start-up: its goaleditor 0x81F01058).
    auto* editor = static_cast<objects::GameEntity*>(objects::find_object(get_checksums("GoalEditor")));
    uint8_t* component = editor ? reinterpret_cast<uint8_t*>(editor->fetch_module(0x81F01058)) : nullptr;
    uint32_t level = current_level();
    if (!component || !level) {
        *message = "Goal editor unavailable";
        return false;
    }
    // (as NewEditorGoal 0x004EA6C0: the first level_goal_index no goal of this level has, then a free slot; at most 10
    // a level - MaxEditedGoalsReached)
    constexpr int kSlots = 50;
    constexpr uint32_t kSlotSize = 0x338;
    uint8_t* slots = component + 0x18;
    auto used = [&](int s) { return slots[s * kSlotSize + 0x28A] != 0; };
    auto level_of = [&](int s) { return *reinterpret_cast<uint32_t*>(slots + s * kSlotSize); };
    auto index_of = [&](int s) { return *reinterpret_cast<int*>(slots + s * kSlotSize + 0x28C); };
    int index = -1;
    for (int candidate = 0; candidate < kMaxGoals && index < 0; ++candidate) {
        bool taken = false;
        for (int s = 0; s < kSlots; ++s)
            taken = taken || (used(s) && level_of(s) == level && index_of(s) == candidate);
        if (!taken)
            index = candidate;
    }
    int slot = -1;
    for (int s = 0; s < kSlots && slot < 0; ++s)
        if (!used(s))
            slot = s;
    if (index < 0 || slot < 0) {
        *message = index < 0 ? "This level has ten created goals already" : "The goal editor is full (50 goals)";
        return false;
    }
    GoalImage image;
    fill_image(g, level, index, gap_ordinals(g), &image);
    std::memcpy(slots + slot * kSlotSize, image.bytes, kSlotSize);
    *message = "\"" + g.name + "\" copied to Create-a-Goal as this level's goal " + std::to_string(index + 1) + "";
    get_log("level editor: goal %s exported to the goal editor (slot %d, CreatedGoal%d)", g.id.c_str(), slot, index);
    return true;
}

std::vector<Goal> get_capture() { return get_all(); }

int get_apply(const std::vector<Goal>& goals)
{
    int made = 0;
    for (const Goal& g : goals) {
        if (index_of_goal_id(g.id) >= 0 || static_cast<int>(g_goals.size()) >= kMaxGoals)
            continue;
        bool clash = false;
        for (size_t k = 0; k < g.points.size(); ++k)
            clash = clash || (node_create::name_taken(point_node(g, static_cast<int>(k))) &&
                              !node_create::is_added(get_checksums(point_node(g, static_cast<int>(k)))));
        if (clash) {
            get_log("level editor: goal %s - the level has a node of its points' names already: not made",
                    g.id.c_str());
            continue;
        }
        g_goals.push_back(g);
        ++made;
    }
    sync_nodes(g_goals);
    if (made)
        get_log("level editor: the layout's %d goals put in place", made);
    if (!live_now())
        put_in_play();
    return made;
}

void reset_all()
{
    take_out_of_play();
    for (const Goal& g : g_goals)
        for (size_t k = 0; k < g.points.size(); ++k)
            remove_node(point_node(g, static_cast<int>(k)));
    g_goals.clear();
}

void forget_level()
{
    // (the level's nodes and objects went with it; a goal of ours the goal manager still has is removed - the game's
    // level change normally takes every goal out first)
    if (skate_module()) {
        for (int index : g_in_play) {
            uint32_t goal = get_checksums(get_goal_id(index));
            if (goal_exists(goal))
                drop_objective(goal);
        }
    }
    g_in_play.clear();
    g_goals.clear();
    g_queue.clear();
    g_dragging = false;
    g_select = -1;
}

void kickoff() { take_out_of_play(); }

void halt()
{
    g_dragging = false;
    put_in_play();
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
    get_refresh();
}

void get_queue(std::function<void()> action) { g_queue.push_back(std::move(action)); }
void end_drag() { g_dragging = false; }
int get_missing(const std::string& id)
{
    int i = index_of_goal_id(id);
    return i < 0 ? 0 : missing_points(g_goals[i]);
}
bool point_of_node(uint32_t node, std::string* id, int* point)
{
    int goal;
    if (!point_of(node, &goal, point))
        return false;
    *id = g_goals[goal].id;
    return true;
}
void choose_one(const std::string& id, int point)
{
    int i = index_of_goal_id(id);
    if (i >= 0 && point >= 0 && point < static_cast<int>(g_goals[i].points.size()))
        select_point(g_goals[i], point);
}
bool in_play() { return !g_in_play.empty(); }

// The level's gap list's names (the Gap goal's choices).
std::vector<std::string> get_level_gaps()
{
    std::vector<std::string> out;
    modules::Skate* skate = skate_module();
    auto* list = skate ? static_cast<goals::GapTally*>(skate->fetch_gap_list()) : nullptr;
    if (!list)
        return out;
    int count = list->total_of();
    for (int i = 0; i < count; ++i)
        out.emplace_back(list->get_gap_title(i));
    return out;
}

void add_overlay(V3 camera_pos, const TickCycle& frame, const Lens& lens, const uint32_t viewport[4], float near_z,
                 uint32_t selected, std::vector<ui::Seg>* lines, std::vector<ui::Overlay::OverlayText>* texts)
{
    auto project = [&](V3 p, float* x, float* y) {
        float nx, ny;
        if (!cast_onto(camera_pos, frame, lens, p, near_z, &nx, &ny))
            return false;
        ndc_to_pixel(viewport, nx, ny, x, y);
        return true;
    };
    auto line = [&](V3 a, V3 b, uint32_t argb, float thickness) {
        if (!clip_to_near(camera_pos, frame, near_z + 0.01f, &a, &b))
            return;
        float ax, ay, bx, by;
        if (project(a, &ax, &ay) && project(b, &bx, &by))
            lines->push_back({ax, ay, bx, by, argb, thickness});
    };
    std::string selected_goal;
    int selected_point = -1;
    point_of_node(selected, &selected_goal, &selected_point);
    for (const Goal& g : g_goals) {
        bool lit = g.id == selected_goal;
        uint32_t colour = lit ? 0xFFFFE070 : 0xFFE0A030;
        for (size_t k = 0; k < g.points.size(); ++k) {
            const Point& p = g.points[k];
            V3 d = p.pos - camera_pos;
            if (get_dot(d, d) > 8000.0f * 8000.0f)
                continue;
            V3 top = p.pos + V3{0.0f, p.height, 0.0f};
            if (p.height > 0.0f)
                line(p.pos, top, colour, 1.0f); // (a letter floats its height above its node)
            // (its heading: a short arrow on the ground)
            V3 ahead = {std::sin(p.heading) * 40.0f, 0.0f, std::cos(p.heading) * 40.0f};
            line(p.pos, p.pos + ahead, colour, lit ? 2.0f : 1.25f);
            float x, y;
            if (get_dot(d, d) < 3000.0f * 3000.0f && project(top + V3{0.0f, 12.0f, 0.0f}, &x, &y)) {
                std::string label = point_label(g.type, static_cast<int>(k));
                if (k == 0)
                    label.insert(0, g.name + ": ");
                texts->push_back({x, y, colour, label});
            }
            // (a race's checkpoints joined in order; the start to the first)
            if (g.type == KindTag::kRace && k >= 2) {
                const Point& from = k == 2 ? g.points[1] : g.points[k - 1];
                line(from.pos + V3{0.0f, 4.0f, 0.0f}, p.pos + V3{0.0f, 4.0f, 0.0f}, lit ? 0xC0FFE070 : 0x80E0A030,
                     1.0f);
            }
        }
        // (the pro to its start)
        if (g.points.size() >= 2)
            line(g.points[0].pos, g.points[1].pos, lit ? 0x90FFE070 : 0x50E0A030, 1.0f);
    }
}

std::string describe()
{
    int complete = 0;
    for (const Goal& g : g_goals)
        complete += get_complete(g) ? 1 : 0;
    char text[96];
    std::snprintf(text, sizeof(text), "%u goals (%d complete, %u in play)", static_cast<unsigned>(g_goals.size()),
                  complete, static_cast<unsigned>(g_in_play.size()));
    return text;
}

} // namespace frontend::level_editor::goal_edit
