// NOT ORIGINAL - the level editor's goals (ours): THUG2's Create-a-Goal goals in the edited copy. See
// level_editor_goals.h. The ImGui parts are in level_editor_goals_ui.cpp.

#include "game/front/level_editor_goals.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

#include "game/core/crc.h"
#include "game/ed/goal_editor.h"
#include "game/ed/goal_editor_util.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_nodes.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor::Goals {

// --- the pure parts -------------------------------------------------------------------------------------------------

namespace {

constexpr float kPi = 3.14159265f;

struct TypeInfo {
    const char* name;  // its checksum's name
    const char* title; // goals_menu_info's
    uint32_t checksum;
};
const TypeInfo kTypeInfo[kTypes] = {
    {"Skate", "SKATE letters", 0x54166ACD},
    {"Combo", "COMBO letters", 0x4EC3CFB5},
    {"HighScore", "High Score", 0x6FE44C6D},
    {"HighCombo", "High Combo", 0xEC414B76},
    {"Skatetris", "Skate-Tricks", 0x7FE2238A},
    {"ComboSkatetris", "Combo Skate-Tricks", 0x0FC6F698},
    {"TrickTris", "Tricktris", 0xEA7BA666},
    {"TrickBeat", "Trick to the Beat", 0xD0DD047B},
    {"Race", "Checkpoint / Race", 0x25904450},
    {"Gap", "Gap", 0x61C5D092},
};

// goal_editor.qb's cag_key_combos (the sets' texts).
const char* const kComboSets[] = {"Basic Flip Tricks",      "Diagonal Flip Tricks",      "Basic Grab Tricks",
                                  "Diagonal Grab Tricks",   "Double Tap Basic Flips",    "Double Tap Diagonal Flips",
                                  "Double Tap Basic Grabs", "Double Tap Diagonal Grabs"};

const char* const kSkateLetters[] = {"S", "K", "A", "T", "E"};
const char* const kComboLetters[] = {"C", "O", "M", "B", "O"};
const char* const kComboSuffixes[] = {"c", "o", "m", "b", "o2"};

int Index(Type type) { return static_cast<int>(type); }

// The record's numbers.
bool ReadNumbers(const std::string& text, float* out, int count)
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

bool ReadInt(const std::string& text, int* out)
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

std::vector<std::string> Split(const std::string& text, char by)
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
void Clamp(Goal* g)
{
    g->time = std::clamp(g->time, 15, 3600);
    if (g->type == Type::HighScore || g->type == Type::HighCombo)
        g->score = std::clamp(g->score, 1000, 10000000);
    g->laps = std::clamp(g->laps, 1, 10);
    g->checkpointTime = std::clamp(g->checkpointTime, 0, 60);
    g->spin = std::clamp(g->spin, 0, 900) / 180 * 180;
    g->accelInterval = std::clamp(g->accelInterval, 1, 10);
    if (!(g->accelPercent >= 0.01f))
        g->accelPercent = 0.01f;
    g->accelPercent = std::min(g->accelPercent, 1.0f);
    g->trickTime = std::clamp(g->trickTime, 5, 3000);
    g->stopAt = std::clamp(g->stopAt, 5, std::max(5, g->time - 5));
    g->maxTricks = std::clamp(g->maxTricks, 1, 15);
    g->comboSize = std::clamp(g->comboSize, 1, 15);
    g->blockSize = std::clamp(g->blockSize, 1, 15);
    g->toWin = std::clamp(g->toWin, 1, 1000);
    g->sets &= (1u << ComboSetCount(g->type)) - 1u;
    if (g->gaps.size() > 10)
        g->gaps.resize(10);
}

} // namespace

const char* TypeName(Type type) { return kTypeInfo[Index(type)].name; }
const char* TypeTitle(Type type) { return kTypeInfo[Index(type)].title; }
uint32_t TypeChecksum(Type type) { return kTypeInfo[Index(type)].checksum; }

bool TypeOf(const std::string& name, Type* type)
{
    for (int i = 0; i < kTypes; ++i) {
        if (_stricmp(kTypeInfo[i].name, name.c_str()) == 0) {
            *type = static_cast<Type>(i);
            return true;
        }
    }
    return false;
}

bool TypeOfChecksum(uint32_t checksum, Type* type)
{
    for (int i = 0; i < kTypes; ++i) {
        if (kTypeInfo[i].checksum == checksum) {
            *type = static_cast<Type>(i);
            return true;
        }
    }
    return false;
}

bool TrickType(Type type)
{
    return type == Type::Skatetris || type == Type::ComboSkatetris || type == Type::TrickTris || type == Type::TrickBeat;
}

int ComboSetCount(Type type)
{
    if (!TrickType(type))
        return 0;
    return type == Type::TrickBeat ? 4 : 8; // (create_key_combos_menu: array_size - 4 for TrickBeat)
}

const char* ComboSetName(int set) { return set >= 0 && set < 8 ? kComboSets[set] : ""; }

// SetType (0x004E6DA0): the pro and the start always; SKATE: 7 (and 11 letter entries from 2), COMBO: 7 (letter
// entries 2..6), Race: 4 to 12 (11 checkpoint entries from 2), every other type: 2.
int MinPoints(Type type)
{
    switch (type) {
    case Type::Skate:
    case Type::Combo:
        return 7;
    case Type::Race:
        return 4;
    default:
        return 2;
    }
}

int MaxPoints(Type type)
{
    switch (type) {
    case Type::Skate:
    case Type::Combo:
        return 7;
    case Type::Race:
        return 12;
    default:
        return 2;
    }
}

int PointKind(Type type, int index)
{
    if (index == 0)
        return 1;
    if (index == 1)
        return 2;
    if (index < 2 || index >= kMaxPoints)
        return 0;
    switch (type) {
    case Type::Skate:
        return 3;
    case Type::Combo:
        return index < 7 ? 3 : 0;
    case Type::Race:
        return 4;
    default:
        return 0;
    }
}

std::string PointLabel(Type type, int index)
{
    if (index == 0)
        return "Pro";
    if (index == 1)
        return "Start";
    if (index >= 2 && index < 7 && type == Type::Skate)
        return kSkateLetters[index - 2];
    if (index >= 2 && index < 7 && type == Type::Combo)
        return kComboLetters[index - 2];
    return "Checkpoint " + std::to_string(index - 1);
}

std::string PointSuffix(Type type, int index)
{
    if (index == 0)
        return "pro";
    if (index == 1)
        return "start";
    if (index >= 2 && index < 7 && type == Type::Skate) {
        std::string letter = kSkateLetters[index - 2];
        return std::string(1, static_cast<char>(letter[0] - 'A' + 'a'));
    }
    if (index >= 2 && index < 7 && type == Type::Combo)
        return kComboSuffixes[index - 2];
    return "cp" + std::to_string(index - 1);
}

float DefaultHeight(Type type, int index) { return PointKind(type, index) == 3 ? 27.0f : 0.0f; }

bool Same(const Goal& a, const Goal& b)
{
    if (a.id != b.id || a.type != b.type || a.name != b.name || a.ped != b.ped || a.text != b.text || a.win != b.win ||
        a.time != b.time || a.score != b.score || a.walk != b.walk || a.laps != b.laps ||
        a.checkpointTime != b.checkpointTime || a.gaps != b.gaps || a.trick != b.trick || a.sets != b.sets ||
        a.spin != b.spin || a.accelInterval != b.accelInterval || a.accelPercent != b.accelPercent ||
        a.trickTime != b.trickTime || a.stopAt != b.stopAt || a.maxTricks != b.maxTricks ||
        a.singleCombo != b.singleCombo || a.comboSize != b.comboSize || a.blockSize != b.blockSize ||
        a.toWin != b.toWin || a.points.size() != b.points.size())
        return false;
    for (size_t i = 0; i < a.points.size(); ++i) {
        const Point &p = a.points[i], &q = b.points[i];
        if (p.pos.x != q.pos.x || p.pos.y != q.pos.y || p.pos.z != q.pos.z || p.heading != q.heading ||
            p.height != q.height)
            return false;
    }
    return true;
}

Goal Defaults(Type type, const std::string& id)
{
    Goal g;
    g.id = id;
    g.type = type;
    g.name = TypeTitle(type);
    // (SetType reads score from the type's generic parameters: Goal_HighScore_genericParams 15000,
    // goal_highcombo_genericParams 10000; InitEditedGoal's 100 otherwise)
    g.score = type == Type::HighScore ? 15000 : type == Type::HighCombo ? 10000 : 100;
    if (type == Type::TrickBeat)
        g.spin = 0; // (SetType)
    return g;
}

bool Complete(const Goal& goal, std::string* why)
{
    auto fail = [&](const std::string& text) {
        if (why)
            *why = text;
        return false;
    };
    int count = static_cast<int>(goal.points.size());
    if (count < MinPoints(goal.type))
        return fail("It needs " + std::to_string(MinPoints(goal.type)) + " points (has " + std::to_string(count) + ")");
    if (count > MaxPoints(goal.type))
        return fail("Too many points");
    if (goal.type == Type::Gap && goal.gaps.empty())
        return fail("Pick at least one gap");
    if (TrickType(goal.type) && goal.sets == 0)
        return fail("Choose at least one key combo set");
    return true;
}

std::string CleanText(const std::string& text, size_t limit)
{
    std::string out;
    for (char c : text) {
        unsigned char u = static_cast<unsigned char>(c);
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

std::string WriteRecord(const Goal& g)
{
    std::string out = "goal " + g.id + " type=" + TypeName(g.type);
    auto text = [&](const char* key, const std::string& value) { out += std::string(" ") + key + "=\"" + value + "\""; };
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
        std::snprintf(buffer, sizeof(buffer), " p%u=%.9g,%.9g,%.9g,%.9g,%.9g", static_cast<unsigned>(i), p.pos.x, p.pos.y,
                      p.pos.z, p.heading, p.height);
        out += buffer;
    }
    if (g.type == Type::Race) {
        number("laps", g.laps);
        number("cptime", g.checkpointTime);
    }
    if (g.type == Type::Gap) {
        std::string gaps;
        for (size_t i = 0; i < g.gaps.size(); ++i)
            gaps += (i ? "|" : "") + g.gaps[i];
        text("gaps", gaps);
        text("trick", g.trick);
    }
    if (TrickType(g.type)) {
        std::string sets;
        for (int i = 0; i < 8; ++i) {
            if (g.sets & (1u << i))
                sets += (sets.empty() ? "" : ",") + std::to_string(i);
        }
        out += " sets=" + (sets.empty() ? std::string("none") : sets);
        number("spin", g.spin);
        number("accel", g.accelInterval);
        std::snprintf(buffer, sizeof(buffer), " accelpct=%.9g", g.accelPercent);
        out += buffer;
        number("tricktime", g.trickTime);
        number("stopat", g.stopAt);
        number("maxtricks", g.maxTricks);
        number("single", g.singleCombo ? 1 : 0);
        number("combo", g.comboSize);
        number("block", g.blockSize);
        number("towin", g.toWin);
    }
    return out.substr(5); // (after "goal ")
}

bool ReadRecord(const std::string& text, Goal* out)
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
    if (!NodeCreate::ValidName(id) || id.size() > 40)
        return false;
    at = end == std::string::npos ? text.size() : end;
    Goal g;
    g.id = id;
    std::vector<std::string> seen;
    std::vector<std::pair<int, Point>> points;
    bool haveType = false;
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
            ok = TypeOf(value, &g.type);
            haveType = ok;
        } else if (key == "name") {
            g.name = CleanText(value, 49);
        } else if (key == "ped") {
            g.ped = CleanText(value, 20);
        } else if (key == "text") {
            g.text = CleanText(value, 99);
        } else if (key == "win") {
            g.win = CleanText(value, 99);
        } else if (key == "trick") {
            g.trick = CleanText(value, 49);
        } else if (key == "gaps") {
            g.gaps.clear();
            if (!value.empty()) {
                for (const std::string& gap : Split(value, '|')) {
                    std::string clean = CleanText(gap, 60);
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
                for (const std::string& s : Split(value, ',')) {
                    int set = 0;
                    ok = ok && ReadInt(s, &set) && set >= 0 && set < 8;
                    if (ok)
                        g.sets |= 1u << set;
                }
            }
        } else if (key == "accelpct") {
            ok = ReadNumbers(value, &g.accelPercent, 1);
        } else if (key.size() >= 2 && key[0] == 'p' && key.find_first_not_of("0123456789", 1) == std::string::npos) {
            int index = 0;
            float v[5];
            ok = ReadInt(key.substr(1), &index) && index >= 0 && index < kMaxPoints && ReadNumbers(value, v, 5);
            if (ok)
                points.push_back({index, Point{{v[0], v[1], v[2]}, v[3], v[4]}});
        } else {
            struct IntKey {
                const char* key;
                int* field;
            } ints[] = {{"time", &g.time},           {"score", &g.score},         {"laps", &g.laps},
                        {"cptime", &g.checkpointTime}, {"spin", &g.spin},         {"accel", &g.accelInterval},
                        {"tricktime", &g.trickTime}, {"stopat", &g.stopAt},       {"maxtricks", &g.maxTricks},
                        {"combo", &g.comboSize},     {"block", &g.blockSize},     {"towin", &g.toWin}};
            bool known = false;
            for (const IntKey& k : ints) {
                if (key == k.key) {
                    known = true;
                    ok = ReadInt(value, k.field);
                }
            }
            if (key == "single") {
                int single = 0;
                ok = ReadInt(value, &single) && (single == 0 || single == 1);
                g.singleCombo = single == 1;
            } // (another key: a later version's - skipped)
            (void)known;
        }
        if (!ok)
            return false;
    }
    if (!haveType)
        return false;
    // (the points: p0.. with none missing, within the type's most)
    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i].first != static_cast<int>(i))
            return false;
        g.points.push_back(points[i].second);
    }
    if (static_cast<int>(g.points.size()) > MaxPoints(g.type))
        return false;
    Clamp(&g);
    *out = g;
    return true;
}

float ImageAngle(float heading) { return heading + kPi; }
float HeadingOf(float imageAngle) { return imageAngle - kPi; }

void FillImage(const Goal& goal, uint32_t level, int levelGoalIndex, const std::vector<int>& gapNumbers, GoalImage* out)
{
    uint8_t* b = out->bytes;
    std::memset(b, 0, kImageSize);
    auto u32 = [b](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(b + offset); };
    auto f32 = [b](uint32_t offset) -> float& { return *reinterpret_cast<float*>(b + offset); };
    auto text = [b](uint32_t offset, const std::string& value, size_t room) {
        size_t n = std::min(value.size(), room - 1);
        std::memcpy(b + offset, value.data(), n);
        b[offset + n] = 0;
    };
    // ReadFromStructure (0x004E96D0): InitEditedGoal, used, not won, level, level_goal_index, the type (SetType: the
    // points' kinds, the counts; the pro's / start's height 0), then the fields.
    Ed::InitEditedGoal(b);
    b[0x28A] = 1;
    b[0x289] = 0;
    u32(0x0) = level;
    u32(0x28C) = static_cast<uint32_t>(levelGoalIndex);
    u32(0x114) = TypeChecksum(goal.type);
    for (int i = 0; i < kMaxPoints; ++i) {
        u32(0x118 + 0x1C * i) = static_cast<uint32_t>(PointKind(goal.type, i));
        f32(0x12C + 0x1C * i) = 0.0f;
    }
    b[0x285] = static_cast<uint8_t>(MinPoints(goal.type));
    b[0x284] = static_cast<uint8_t>(MaxPoints(goal.type));
    if (goal.type == Type::TrickBeat)
        u32(0x2B8) = 0;
    u32(0x298) = static_cast<uint32_t>(goal.score);
    u32(0x290) = static_cast<uint32_t>(goal.time);
    u32(0x294) = goal.walk ? 0x726E85AAu : 0x54166ACDu; // (control_type: Walk / Skate)
    text(0x4, goal.name, 0x36 - 0x4);
    text(0x4B, goal.text, 0xAF - 0x4B);
    text(0xAF, goal.win, 0x113 - 0xAF);
    text(0x36, goal.ped, 0x4B - 0x36);
    int count = std::min(static_cast<int>(goal.points.size()), MaxPoints(goal.type));
    b[0x286] = static_cast<uint8_t>(count);
    for (int i = 0; i < count; ++i) {
        const Point& p = goal.points[i];
        uint32_t at = 0x11C + 0x1C * i;
        f32(at) = p.pos.x;
        f32(at + 4) = p.pos.y;
        f32(at + 8) = p.pos.z;
        f32(at + 12) = 1.0f;
        f32(at + 16) = p.height;
        f32(at + 20) = ImageAngle(p.heading);
    }
    // The type's own (SetGoalSpecificParams' fields, 0x004E7590).
    switch (goal.type) {
    case Type::Race:
        u32(0x29C) = static_cast<uint32_t>(goal.laps);
        u32(0x2A0) = static_cast<uint32_t>(goal.checkpointTime);
        break;
    case Type::Gap: {
        text(0x303, goal.trick, kImageSize - 0x303);
        int n = 0;
        for (int number : gapNumbers) {
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
    if (TrickType(goal.type)) {
        // (0x004E7590: Combo Skate-Tricks its combo_size / single_combo, Tricktris its block size / total; those and
        // Skate-Tricks the shared ones (0x004E74F0); every trick goal its key combo sets (0x004E73F0) - Trick to the
        // Beat only those, its spin staying SetType's 0)
        if (goal.type == Type::ComboSkatetris) {
            u32(0x2C0) = static_cast<uint32_t>(goal.comboSize);
            if (goal.singleCombo)
                b[0x2BC] = 1;
        }
        if (goal.type == Type::TrickTris) {
            u32(0x2C4) = static_cast<uint32_t>(goal.blockSize);
            u32(0x2C8) = static_cast<uint32_t>(goal.toWin);
        }
        if (goal.type != Type::TrickBeat) {
            u32(0x2A4) = static_cast<uint32_t>(goal.accelInterval);
            f32(0x2A8) = goal.accelPercent;
            u32(0x2AC) = static_cast<uint32_t>(goal.trickTime);
            u32(0x2B0) = static_cast<uint32_t>(goal.stopAt);
            u32(0x2B4) = static_cast<uint32_t>(goal.maxTricks);
            u32(0x2B8) = static_cast<uint32_t>(goal.spin);
        }
        // (the key combo sets as AddKeyComboSet set_index=<i> adds them - 0x004E6C40: {set, require_perfect 0, spin
        // 0, num_taps 0})
        int sets = 0;
        for (int i = 0; i < ComboSetCount(goal.type) && sets < 10; ++i) {
            if (!(goal.sets & (1u << i)))
                continue;
            b[0x2CC + 4 * sets] = static_cast<uint8_t>(i);
            ++sets;
        }
        u32(0x2F4) = static_cast<uint32_t>(sets);
    }
}

std::string PlayNodeName(int which, int index)
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

std::string GoalId(int index) { return "CreatedGoal" + std::to_string(index); }

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

using CFunction = bool(__cdecl*)(Script::CStruct*, Script::CScript*);

std::vector<Goal> g_goals;               // (their points' places: as their nodes had them when last read)
std::vector<int> g_inPlay;               // the play indexes in the goal manager now
std::vector<std::function<void()>> g_queue;
bool g_dragging;                         // a field being dragged: one undo step
int g_select = -1;                       // a node to select (Nodes::All() index)

constexpr const char* kStem = "ed_goal";
constexpr const char* kPointType = "ed_goal";

bool SameText(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; }
uint32_t Crc(const std::string& text) { return Crc::GenerateCRCFromString(text.c_str()); }

std::string PointNode(const Goal& g, int index) { return g.id + "_" + PointSuffix(g.type, index); }

int FindGoal(const std::string& id)
{
    for (size_t i = 0; i < g_goals.size(); ++i)
        if (g_goals[i].id == id)
            return static_cast<int>(i);
    return -1;
}

// The goal and point a node is (false: not one of ours).
bool PointOf(uint32_t node, int* goal, int* point)
{
    for (size_t i = 0; i < g_goals.size(); ++i) {
        for (size_t k = 0; k < g_goals[i].points.size(); ++k) {
            if (Crc(PointNode(g_goals[i], static_cast<int>(k))) == node) {
                *goal = static_cast<int>(i);
                *point = static_cast<int>(k);
                return true;
            }
        }
    }
    return false;
}

NodeCreate::Spec PointSpec(const Goal& g, int index)
{
    NodeCreate::Spec s;
    s.name = PointNode(g, index);
    s.cls = "GenericNode";
    s.type = kPointType;
    s.pos = g.points[index].pos;
    s.angles = {0.0f, g.points[index].heading, 0.0f};
    s.owned = true;
    return s;
}

// The goals' places read again from their nodes (a node deleted: its last place kept - Restore points makes it again).
void Refresh()
{
    for (Goal& g : g_goals) {
        for (size_t k = 0; k < g.points.size(); ++k) {
            NodeEdit::Values v;
            if (NodeEdit::Read(Crc(PointNode(g, static_cast<int>(k))), &v)) {
                g.points[k].pos = v.pos;
                g.points[k].heading = v.angles.y;
            }
        }
    }
}

int MissingPoints(const Goal& g)
{
    int missing = 0;
    for (size_t k = 0; k < g.points.size(); ++k)
        missing += NodeCreate::IsAdded(Crc(PointNode(g, static_cast<int>(k)))) ? 0 : 1;
    return missing;
}

// Every point node of the goals made where missing / put where the goals have them.
void SyncNodes(const std::vector<Goal>& goals)
{
    std::string message;
    NodeCreate::List make;
    for (const Goal& g : goals) {
        for (size_t k = 0; k < g.points.size(); ++k) {
            uint32_t name = Crc(PointNode(g, static_cast<int>(k)));
            if (!NodeCreate::IsAdded(name)) {
                if (!NodeCreate::NameTaken(PointNode(g, static_cast<int>(k))))
                    make.push_back(PointSpec(g, static_cast<int>(k)));
                continue;
            }
            NodeEdit::Values v;
            v.pos = g.points[k].pos;
            v.angles = {0.0f, g.points[k].heading, 0.0f};
            NodeEdit::Values now;
            if (!NodeEdit::Read(name, &now) || !NodeEdit::Same(now, v))
                NodeEdit::Set(name, v);
        }
    }
    if (!make.empty())
        NodeCreate::CreateAll(make, &message);
}

void RemoveNode(const std::string& name)
{
    uint32_t checksum = Crc(name);
    if (!NodeCreate::IsAdded(checksum))
        return;
    NodeCreate::Spec removed;
    int at = -1;
    NodeCreate::Remove(checksum, &removed, &at);
    NodeEdit::Forget(checksum);
}

// Every node of ours of type ed_goal that no goal of `goals` has: removed.
void RemoveStrayPoints(const std::vector<Goal>& goals)
{
    for (const NodeCreate::Spec& spec : NodeCreate::Current()) {
        if (!SameText(spec.type, kPointType) || !SameText(spec.cls, "GenericNode"))
            continue;
        bool kept = false;
        for (const Goal& g : goals)
            for (size_t k = 0; k < g.points.size() && !kept; ++k)
                kept = SameText(PointNode(g, static_cast<int>(k)), spec.name);
        if (!kept)
            RemoveNode(spec.name);
    }
}

// --- undo: the goals as they are (their points' places too) ---

struct Step : ModuleStep {
    std::vector<Goal> goals;
    void Restore() override
    {
        RemoveStrayPoints(goals);
        g_goals = goals;
        SyncNodes(goals);
    }
    std::shared_ptr<ModuleStep> Now() const override
    {
        Refresh();
        auto now = std::make_shared<Step>();
        now->goals = g_goals;
        return now;
    }
};

void PushUndo()
{
    Refresh();
    auto step = std::make_shared<Step>();
    step->goals = g_goals;
    PushModuleUndo(step);
}

// --- the goal manager (the skate module's - 0x005591B0 - through its script commands) ---

uint8_t* SkateModule() { return GameVariable<uint8_t*>(0x007CE478); }

// The current level as the goal editor keeps it (Mdl::Skate +0x378; Load_Sk5Ed_gameplay counts as Load_Sk5Ed - as
// 0x004E7D00 / 0x004EC010 / 0x004EA6C0 do).
uint32_t CurrentLevel()
{
    uint8_t* skate = SkateModule();
    if (!skate)
        return 0;
    uint32_t level = *reinterpret_cast<uint32_t*>(skate + 0x378);
    return level == 0xB664035D ? 0xE8B4B836 : level;
}

// A GoalManager_* script command with name=<goal> (they take no other parameter and don't use the script).
bool GoalCommand(CFunction command, uint32_t goal)
{
    Script::CStruct* params = Script::NewStruct()->Init();
    params->AddChecksum(0xA1DC81F9u, goal); // name
    bool result = command(params, nullptr);
    params->Clear();
    Script::DeleteStruct(params);
    return result;
}
bool GoalExists(uint32_t goal) { return GoalCommand(&Game::CFunc_GoalManager_GoalExists, goal); }       // GoalManager_GoalExists
bool GoalIsActive(uint32_t goal) { return GoalCommand(&Game::CFunc_GoalManager_GoalIsActive, goal); }     // GoalManager_GoalIsActive
void DeactivateGoal(uint32_t goal) { GoalCommand(&Game::CFunc_GoalManager_DeactivateGoal, goal); }          // GoalManager_DeactivateGoal
void RemoveGoal(uint32_t goal) { GoalCommand(&Game::CFunc_GoalManager_RemoveGoal, goal); }              // GoalManager_RemoveGoal
void InitializeGoal(uint32_t goal) { GoalCommand(&Game::CFunc_GoalManager_InitializeGoal, goal); }          // GoalManager_InitializeGoal

void KillObject(const std::string& name)
{
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(Crc(name)));
    if (object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD))
        object->MarkAsDead();
}

// The play nodes of a goal (EditedGoal_Pro_Node / _Restart_Node / _Object_Node's fields, placed as
// WriteEditedGoalNodePositions 0x004E7A80 places them: Pos + (0, height, 0), Angles (0, angle - pi, 0), a letter's Type
// and model).
NodeCreate::List PlayNodes(const Goal& g, int index)
{
    NodeCreate::List out;
    auto field = [](NodeCreate::Field::Type type, const char* key) {
        NodeCreate::Field f = {type, key};
        return f;
    };
    auto name = [&](const char* key, const char* value) {
        NodeCreate::Field f = field(NodeCreate::Field::kName, key);
        f.text = value;
        return f;
    };
    auto integer = [&](const char* key, int value) {
        NodeCreate::Field f = field(NodeCreate::Field::kInteger, key);
        f.integer = value;
        return f;
    };
    auto string = [&](const char* key, const char* value) {
        NodeCreate::Field f = field(NodeCreate::Field::kString, key);
        f.text = value;
        return f;
    };
    const float pi = GameVariable<float>(0x006453D8); // (the constant 0x004E79C0 subtracts)
    for (size_t k = 0; k < g.points.size(); ++k) {
        const Point& p = g.points[k];
        NodeCreate::Spec s;
        s.name = PlayNodeName(static_cast<int>(k), index);
        s.pos = {p.pos.x, p.pos.y + p.height, p.pos.z};
        s.angles = {0.0f, ImageAngle(p.heading) - pi, 0.0f};
        s.owned = true;
        int kind = PointKind(g.type, static_cast<int>(k));
        if (kind == 1) {
            s.cls = "Pedestrian";
            s.type = "Ped_From_Profile";
            s.extra = {name("SkeletonName", "THPS6_human"), name("DefaultAnimName", "WStandIdle1"),
                       name("PedAI_Type", "Stand"),          name("AnimName", "animload_THPS6_human"),
                       name("profile", "random_male_profile"), integer("SuspendDistance", 3000),
                       integer("lod_dist1", 200),            integer("lod_dist2", 400)};
        } else if (kind == 2) {
            s.cls = "Restart";
            s.type = "Player1";
            NodeCreate::Field types = field(NodeCreate::Field::kNames, "restart_types");
            types.list = {"Player1"};
            NodeCreate::Field created = field(NodeCreate::Field::kFlag, "");
            created.text = "CreatedAtStart";
            s.extra = {created, string("RestartName", "Edited goal: Restart"), types};
        } else {
            s.cls = "GameObject";
            uint32_t typeChecksum = TypeChecksum(g.type);
            const char* model = Ed::GoalObjectModel(typeChecksum, static_cast<int>(k) - 2);
            static const char* const kLetterTypes[2][5] = {{"Letter_S", "Letter_K", "Letter_A", "Letter_T", "Letter_E"},
                                                           {"Combo_C", "Combo_O", "Combo_M", "Combo_B", "Combo_O"}};
            if (kind == 3 && k >= 2 && k < 7)
                s.type = kLetterTypes[g.type == Type::Combo ? 1 : 0][k - 2];
            s.extra = {integer("SuspendDistance", 0), integer("lod_dist1", 400), integer("lod_dist2", 401)};
            if (model)
                s.extra.push_back(string("model", model));
        }
        out.push_back(s);
    }
    return out;
}

// The gaps' numbers in the level's gap list now (the skate module's checklist, 0x005656C0).
std::vector<int> GapNumbers(const Goal& g)
{
    std::vector<int> out;
    uint8_t* skate = SkateModule();
    auto* list = skate ? static_cast<Game::CGapChecklist*>(reinterpret_cast<Mdl::Skate*>(skate)->GetGapChecklist()) : nullptr;
    if (!list)
        return out;
    for (const std::string& gap : g.gaps) {
        int number = list->FindGap(gap.c_str());
        if (number >= 0)
            out.push_back(number);
        else
            Log("level editor: goal %s - no gap \"%s\" in the level's gap list: left out", g.id.c_str(), gap.c_str());
    }
    return out;
}

void TakeOutOfPlay()
{
    if (g_inPlay.empty())
        return;
    for (int index : g_inPlay) {
        uint32_t goal = Crc(GoalId(index));
        if (SkateModule() && GoalExists(goal)) {
            if (GoalIsActive(goal))
                DeactivateGoal(goal);
            RemoveGoal(goal);
        }
        // (edit_goal_menu_exit: <pro_name>:Die - and a letter / cone still standing)
        for (int which = 0; which < kMaxPoints; ++which) {
            KillObject(PlayNodeName(which, index));
            RemoveNode(PlayNodeName(which, index));
        }
    }
    Log("level editor: %u goals taken out of play", static_cast<unsigned>(g_inPlay.size()));
    g_inPlay.clear();
}

void PutInPlay()
{
    TakeOutOfPlay();
    std::string why;
    if (Active() || !Layout::EditingCopy() || Layout::Refused(&why) || !SkateModule() || g_goals.empty())
        return;
    Refresh();
    uint32_t level = CurrentLevel();
    NodeCreate::List nodes;
    std::vector<std::pair<int, const Goal*>> play;
    for (size_t i = 0; i < g_goals.size() && static_cast<int>(i) < kMaxGoals; ++i) {
        const Goal& g = g_goals[i];
        std::string incomplete;
        if (!Complete(g, &incomplete)) {
            Log("level editor: goal %s (%s) not played - %s", g.id.c_str(), g.name.c_str(), incomplete.c_str());
            continue;
        }
        int index = PlayIndex(static_cast<int>(i));
        bool taken = false;
        for (const NodeCreate::Spec& s : PlayNodes(g, index)) {
            if (NodeCreate::NameTaken(s.name))
                taken = true;
        }
        if (taken || GoalExists(Crc(GoalId(index)))) {
            Log("level editor: goal %s not played - %s is taken", g.id.c_str(), GoalId(index).c_str());
            continue;
        }
        NodeCreate::List made = PlayNodes(g, index);
        nodes.insert(nodes.end(), made.begin(), made.end());
        play.push_back({index, &g});
    }
    if (play.empty())
        return;
    std::string message;
    NodeCreate::CreateAll(nodes, &message);
    for (const auto& [index, goal] : play) {
        // Create-a-Goal's own: AddEditedGoalToGoalManager (0x004EB6B0 on the record, MarkUnbeaten), then
        // GoalManager_InitializeGoal (its pro made from TRG_EditedGoal<n>_Pro).
        GoalImage image;
        FillImage(*goal, level, index, GapNumbers(*goal), &image);
        reinterpret_cast<Ed::CEditedGoal*>(image.bytes)->AddToGoalManager(true);
        uint32_t id = Crc(GoalId(index));
        if (!GoalExists(id)) {
            Log("level editor: goal %s - the goal manager didn't take it", goal->id.c_str());
            continue;
        }
        InitializeGoal(id);
        g_inPlay.push_back(index);
        Log("level editor: goal %s (%s, \"%s\") in play as %s", goal->id.c_str(), TypeName(goal->type),
            goal->name.c_str(), GoalId(index).c_str());
    }
}

// Where the view looks, on the ground (the game's collision ray - a CFeeler, as the triggers' Cast).
bool Cast(V3 from, V3 to, V3* hit)
{
    alignas(16) uint8_t feeler[0x100] = {};
    auto* ray = reinterpret_cast<Nx::CFeeler*>(feeler);
    ray->Construct();
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!ray->GetCollisionAlong(&start, &end, false))
        return false;
    const float* point = reinterpret_cast<const float*>(feeler + 0x34);
    *hit = {point[0], point[1], point[2]};
    return true;
}

V3 Grounded(V3 at)
{
    V3 ground;
    if (Cast(at + V3{0.0f, 200.0f, 0.0f}, at - V3{0.0f, 5000.0f, 0.0f}, &ground))
        return ground;
    return at;
}

std::string FreshId()
{
    return NodeCreate::FreshName(kStem, [](const std::string& id) {
        if (FindGoal(id) >= 0)
            return true;
        for (const char* suffix : {"_pro", "_start"})
            if (NodeCreate::NameTaken(id + suffix))
                return true;
        return false;
    });
}

std::string FreshTitle(Type type)
{
    // (goal_editor_set_type: the menu's name, then "<name> 2", "<name> 3"... while a goal has it)
    auto taken = [](const std::string& name) {
        return std::any_of(g_goals.begin(), g_goals.end(), [&](const Goal& g) { return SameText(g.name, name); });
    };
    std::string name = TypeTitle(type);
    for (int suffix = 2; taken(name) && suffix < 100; ++suffix)
        name = std::string(TypeTitle(type)) + " " + std::to_string(suffix);
    return name;
}

void SelectPoint(const Goal& g, int index)
{
    Nodes::Invalidate();
    g_select = Nodes::Find(Crc(PointNode(g, index)));
}

} // namespace

int TakeSelect()
{
    int select = g_select;
    g_select = -1;
    return select;
}

std::vector<Goal> All()
{
    Refresh();
    return g_goals;
}

bool IsGoalPoint(uint32_t node)
{
    int goal, point;
    return PointOf(node, &goal, &point);
}

bool Create(Type type)
{
    ViewNow view;
    if (!CurrentView(&view)) {
        Hud::Message("No view available");
        return false;
    }
    if (static_cast<int>(g_goals.size()) >= kMaxGoals) {
        Hud::Message("Goal limit reached (10)", 4.0);
        return false;
    }
    Frame frame = FrameFromYawPitch(view.yaw, view.pitch);
    V3 forward = frame.back * -1.0f;
    V3 base;
    if (!Cast(view.pos, view.pos + forward * 4000.0f, &base))
        base = view.pos + forward * 600.0f;
    base = Grounded(base);
    V3 flat = Normalize({forward.x, 0.0f, forward.z});
    V3 right = Normalize(Cross({0.0f, 1.0f, 0.0f}, flat * -1.0f));
    Goal g = Defaults(type, FreshId());
    g.name = FreshTitle(type);
    // The pro facing the camera, the start beside it facing away; letters / checkpoints in a line ahead.
    g.points.push_back({base, view.yaw + kPi, 0.0f});
    g.points.push_back({Grounded(base + right * 150.0f), view.yaw, 0.0f});
    int count = MinPoints(type);
    for (int k = 2; k < count; ++k) {
        V3 at = Grounded(base + flat * (300.0f * static_cast<float>(k - 1)));
        g.points.push_back({at, view.yaw, DefaultHeight(type, k)});
    }
    for (Point& p : g.points) {
        p.heading = NodeEdit::WrapAngle(p.heading);
    }
    PushUndo();
    g_goals.push_back(g);
    SyncNodes({g});
    SelectPoint(g, 0);
    Hud::Message("Added goal \"" + g.name + "\"", 5.0);
    Log("level editor: goal %s (%s) made at (%.1f, %.1f, %.1f)", g.id.c_str(), TypeName(type), base.x, base.y, base.z);
    return true;
}

bool Edit(const std::string& id, const Goal& changed, bool done)
{
    int i = FindGoal(id);
    if (i < 0)
        return false;
    if (!g_dragging)
        PushUndo();
    g_dragging = !done;
    Refresh();
    Goal g = changed;
    g.id = g_goals[i].id;
    g.type = g_goals[i].type;
    g.name = CleanText(g.name, 49);
    if (g.name.empty())
        g.name = g_goals[i].name;
    g.ped = CleanText(g.ped, 20);
    g.text = CleanText(g.text, 99);
    g.win = CleanText(g.win, 99);
    g.trick = CleanText(g.trick, 49);
    // (the points' places are their nodes': only the heights come from the change)
    std::vector<Point> points = g_goals[i].points;
    for (size_t k = 0; k < points.size() && k < g.points.size(); ++k)
        points[k].height = std::clamp(g.points[k].height, 0.0f, 1000.0f); // (EditorCam_MaxHeight)
    g.points = points;
    Clamp(&g);
    g_goals[i] = g;
    return true;
}

bool Remove(const std::string& id)
{
    int i = FindGoal(id);
    if (i < 0)
        return false;
    PushUndo();
    Goal g = g_goals[i];
    for (size_t k = 0; k < g.points.size(); ++k)
        RemoveNode(PointNode(g, static_cast<int>(k)));
    g_goals.erase(g_goals.begin() + i);
    Hud::Message("Deleted goal \"" + g.name + "\"");
    return true;
}

bool AddCheckpoint(const std::string& id)
{
    int i = FindGoal(id);
    if (i < 0 || g_goals[i].type != Type::Race || static_cast<int>(g_goals[i].points.size()) >= MaxPoints(Type::Race))
        return false;
    PushUndo();
    Goal& g = g_goals[i];
    const Point& last = g.points.back();
    V3 ahead = {std::sin(last.heading), 0.0f, std::cos(last.heading)};
    g.points.push_back({Grounded(last.pos + ahead * 300.0f), last.heading, 0.0f});
    SyncNodes({g});
    SelectPoint(g, static_cast<int>(g.points.size()) - 1);
    return true;
}

bool RemoveCheckpoint(const std::string& id)
{
    int i = FindGoal(id);
    if (i < 0 || g_goals[i].type != Type::Race || static_cast<int>(g_goals[i].points.size()) <= MinPoints(Type::Race))
        return false;
    PushUndo();
    Goal& g = g_goals[i];
    RemoveNode(PointNode(g, static_cast<int>(g.points.size()) - 1));
    g.points.pop_back();
    return true;
}

bool RestorePoints(const std::string& id)
{
    int i = FindGoal(id);
    if (i < 0 || !MissingPoints(g_goals[i]))
        return false;
    PushUndo();
    SyncNodes({g_goals[i]});
    return true;
}

bool Export(const std::string& id, std::string* message)
{
    int i = FindGoal(id);
    if (i < 0) {
        *message = "No such goal";
        return false;
    }
    Refresh();
    const Goal& g = g_goals[i];
    std::string why;
    if (!Complete(g, &why)) {
        *message = "Not complete: " + why;
        return false;
    }
    // The goal editor's component (the object GoalEditor, CreateGoalEditor at start-up: its goaleditor 0x81F01058).
    auto* editor = static_cast<Obj::CCompositeObject*>(Obj::FindObject(Crc("GoalEditor")));
    uint8_t* component = editor ? reinterpret_cast<uint8_t*>(editor->GetComponent(0x81F01058)) : nullptr;
    uint32_t level = CurrentLevel();
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
    auto levelOf = [&](int s) { return *reinterpret_cast<uint32_t*>(slots + s * kSlotSize); };
    auto indexOf = [&](int s) { return *reinterpret_cast<int*>(slots + s * kSlotSize + 0x28C); };
    int index = -1;
    for (int candidate = 0; candidate < kMaxGoals && index < 0; ++candidate) {
        bool taken = false;
        for (int s = 0; s < kSlots; ++s)
            taken = taken || (used(s) && levelOf(s) == level && indexOf(s) == candidate);
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
    FillImage(g, level, index, GapNumbers(g), &image);
    std::memcpy(slots + slot * kSlotSize, image.bytes, kSlotSize);
    *message = "\"" + g.name + "\" copied to Create-a-Goal as this level's goal " + std::to_string(index + 1) +
               "";
    Log("level editor: goal %s exported to the goal editor (slot %d, CreatedGoal%d)", g.id.c_str(), slot, index);
    return true;
}

std::vector<Goal> Capture() { return All(); }

int Apply(const std::vector<Goal>& goals)
{
    int made = 0;
    for (const Goal& g : goals) {
        if (FindGoal(g.id) >= 0 || static_cast<int>(g_goals.size()) >= kMaxGoals)
            continue;
        bool clash = false;
        for (size_t k = 0; k < g.points.size(); ++k)
            clash = clash || (NodeCreate::NameTaken(PointNode(g, static_cast<int>(k))) &&
                              !NodeCreate::IsAdded(Crc(PointNode(g, static_cast<int>(k)))));
        if (clash) {
            Log("level editor: goal %s - the level has a node of its points' names already: not made", g.id.c_str());
            continue;
        }
        g_goals.push_back(g);
        ++made;
    }
    SyncNodes(g_goals);
    if (made)
        Log("level editor: the layout's %d goals put in place", made);
    if (!Active())
        PutInPlay();
    return made;
}

void ResetAll()
{
    TakeOutOfPlay();
    for (const Goal& g : g_goals)
        for (size_t k = 0; k < g.points.size(); ++k)
            RemoveNode(PointNode(g, static_cast<int>(k)));
    g_goals.clear();
}

void ForgetLevel()
{
    // (the level's nodes and objects went with it; a goal of ours the goal manager still has is removed - the game's
    // level change normally takes every goal out first)
    if (SkateModule()) {
        for (int index : g_inPlay) {
            uint32_t goal = Crc(GoalId(index));
            if (GoalExists(goal))
                RemoveGoal(goal);
        }
    }
    g_inPlay.clear();
    g_goals.clear();
    g_queue.clear();
    g_dragging = false;
    g_select = -1;
}

void Start() { TakeOutOfPlay(); }

void Stop()
{
    g_dragging = false;
    PutInPlay();
}

void Update()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
    Refresh();
}

void Queue(std::function<void()> action) { g_queue.push_back(std::move(action)); }
void EndDrag() { g_dragging = false; }
int Missing(const std::string& id)
{
    int i = FindGoal(id);
    return i < 0 ? 0 : MissingPoints(g_goals[i]);
}
bool PointOfNode(uint32_t node, std::string* id, int* point)
{
    int goal;
    if (!PointOf(node, &goal, point))
        return false;
    *id = g_goals[goal].id;
    return true;
}
void Select(const std::string& id, int point)
{
    int i = FindGoal(id);
    if (i >= 0 && point >= 0 && point < static_cast<int>(g_goals[i].points.size()))
        SelectPoint(g_goals[i], point);
}
bool InPlay() { return !g_inPlay.empty(); }

// The level's gap list's names (the Gap goal's choices).
std::vector<std::string> LevelGaps()
{
    std::vector<std::string> out;
    uint8_t* skate = SkateModule();
    auto* list = skate ? static_cast<Game::CGapChecklist*>(reinterpret_cast<Mdl::Skate*>(skate)->GetGapChecklist()) : nullptr;
    if (!list)
        return out;
    int count = list->Count();
    for (int i = 0; i < count; ++i)
        out.push_back(list->GetGapName(i));
    return out;
}

void AddOverlay(V3 cameraPos, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                uint32_t selected, std::vector<Ui::Line>* lines, std::vector<Ui::Overlay::Text>* texts)
{
    auto project = [&](V3 p, float* x, float* y) {
        float nx, ny;
        if (!Project(cameraPos, frame, lens, p, nearZ, &nx, &ny))
            return false;
        NdcToPixel(viewport, nx, ny, x, y);
        return true;
    };
    auto line = [&](V3 a, V3 b, uint32_t argb, float thickness) {
        if (!ClipToNear(cameraPos, frame, nearZ + 0.01f, &a, &b))
            return;
        float ax, ay, bx, by;
        if (project(a, &ax, &ay) && project(b, &bx, &by))
            lines->push_back({ax, ay, bx, by, argb, thickness});
    };
    std::string selectedGoal;
    int selectedPoint = -1;
    PointOfNode(selected, &selectedGoal, &selectedPoint);
    for (const Goal& g : g_goals) {
        bool lit = g.id == selectedGoal;
        uint32_t colour = lit ? 0xFFFFE070 : 0xFFE0A030;
        for (size_t k = 0; k < g.points.size(); ++k) {
            const Point& p = g.points[k];
            V3 d = p.pos - cameraPos;
            if (Dot(d, d) > 8000.0f * 8000.0f)
                continue;
            V3 top = p.pos + V3{0.0f, p.height, 0.0f};
            if (p.height > 0.0f)
                line(p.pos, top, colour, 1.0f); // (a letter floats its height above its node)
            // (its heading: a short arrow on the ground)
            V3 ahead = {std::sin(p.heading) * 40.0f, 0.0f, std::cos(p.heading) * 40.0f};
            line(p.pos, p.pos + ahead, colour, lit ? 2.0f : 1.25f);
            float x, y;
            if (Dot(d, d) < 3000.0f * 3000.0f && project(top + V3{0.0f, 12.0f, 0.0f}, &x, &y)) {
                std::string label = PointLabel(g.type, static_cast<int>(k));
                if (k == 0)
                    label = g.name + ": " + label;
                texts->push_back({x, y, colour, label});
            }
            // (a race's checkpoints joined in order; the start to the first)
            if (g.type == Type::Race && k >= 2) {
                const Point& from = k == 2 ? g.points[1] : g.points[k - 1];
                line(from.pos + V3{0.0f, 4.0f, 0.0f}, p.pos + V3{0.0f, 4.0f, 0.0f}, lit ? 0xC0FFE070 : 0x80E0A030, 1.0f);
            }
        }
        // (the pro to its start)
        if (g.points.size() >= 2)
            line(g.points[0].pos, g.points[1].pos, lit ? 0x90FFE070 : 0x50E0A030, 1.0f);
    }
}

std::string Describe()
{
    int complete = 0;
    for (const Goal& g : g_goals)
        complete += Complete(g) ? 1 : 0;
    char text[96];
    std::snprintf(text, sizeof(text), "%u goals (%d complete, %u in play)", static_cast<unsigned>(g_goals.size()),
                  complete, static_cast<unsigned>(g_inPlay.size()));
    return text;
}

} // namespace Front::LevelEditor::Goals
